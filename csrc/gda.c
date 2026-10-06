// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The GDA engine: a queue's rings, run on the GPU.
 *
 * One workgroup per queue, and up to KNOD_PERSIST_GDA_WAVES_MAX of its waves
 * take packets, 64 CQEs each a round: wave w the ones at ci + 64w.  Wave 0
 * alone keeps the rings - the doorbell records, the SQ, the TX completions,
 * the control block - and the waves meet in a few bytes of LDS past the
 * program's stack, four barriers a round.
 *
 * RQ entry i always holds RX page i.  The NIC consumes the RQ in order and
 * completes the SQ in order, so no page ever needs to move between entries:
 * an entry whose packet went out on the SQ is held until the NIC says it was
 * sent, and the RQ producer stops at the oldest such entry.
 *
 * src/gda_engine.S calls gda_init() once, then every round gda_round_begin(),
 * the program and gda_round_end().  What lasts from one call to the next is
 * in three VGPRs it keeps across the program: the lanes of one hold the ring
 * state, one value each (GL_*), and the other two the packet count.  Each
 * function takes them and hands them back.  They are read a lane at a time
 * where needed rather than unpacked, which would want more scalars at once
 * than a function gets without a stack.
 */
#include <linux/knod_persistent.h>
#include <linux/knod_mlx5.h>
#include "map.h"

#define LDS		__attribute__((address_space(3)))
/* Into the three functions the engine calls: a call out of one would be to
 * code nothing places.
 */
#define INLINE		inline __attribute__((always_inline))

#define offsetof(t, f)	__builtin_offsetof(t, f)

/* op_own's place in a CQE's last dword, fm_ce_se's in a ctrl segment's
 * third, and a send WQE's size in the 16-byte units the NIC counts.
 */
#define CQE_OP_OWN_SHIFT	(8 * (offsetof(struct knod_mlx5_cqe64, op_own) - \
				      offsetof(struct knod_mlx5_cqe64, wqe_counter)))
#define CTRL_FM_CE_SE_SHIFT	(8 * (offsetof(struct knod_mlx5_wqe_ctrl_seg, \
					       fm_ce_se) % 4))
#define SEND_DS			(sizeof(struct knod_mlx5_send_wqe) / 16)

/* The verdicts, as enum xdp_action has them. */
#define XDP_DROP	1
#define XDP_PASS	2
#define XDP_TX		3

typedef uint32_t v4u __attribute__((ext_vector_type(4)));
typedef uint32_t v16u __attribute__((ext_vector_type(16)));

typedef GLOBAL struct knod_persistent_control *ctl_t;
typedef GLOBAL struct knod_persistent_gda *entry_t;

/* The state lanes.  Wave 0's ring state is the one that counts. */
enum {
	GL_RQ_SIZE,
	GL_SQ_PC,		/* WQEs posted, free-running */
	GL_SQ_CC,		/* WQEs the NIC completed */
	GL_TX_CI,		/* TX CQ consumer index */
	GL_SQ_MASK,		/* zero: no SQ, XDP_TX drops */
	GL_SQN,
	GL_TX_MKEY,
	GL_TX_CQ_LOG,
	GL_SQ,			/* and the next */
	GL_DB = GL_SQ + 2,	/* the NIC's doorbell */
	GL_RX_DMA = GL_DB + 2,	/* page index -> the NIC's address */
	GL_TX_PKTS = GL_RX_DMA + 2,
	GL_TX_FULL = GL_TX_PKTS + 2,
	GL_TX_GEN = GL_TX_FULL + 2,
	GL_RX_BASE,
	GL_HEADROOM = GL_RX_BASE + 2,
	GL_RQ_LOG,
	GL_CQ_LOG,
	GL_PARAM,
	GL_LDS = GL_PARAM + 2,	/* where the waves meet */
	GL_WAVES,
	GL_PAGE_SHIFT,
	GL_N,			/* packets this round, all waves */
	GL_RUN,			/* this wave's of them */
	GL_ROUNDS,		/* rounds that took packets */
	GL_STAGGER = GL_ROUNDS + 2,
	GL_STAGGER_MASK,
	GL_PASS_PC,		/* PASS entries appended, free-running */
	GL_PASS_CC,		/* ...the host has finished with, or the floor */
	GL_PASS_FLOOR,		/* entries before it are an earlier build's */
	GL_PASS_RING,
	GL_PASS_MASK = GL_PASS_RING + 2,
	GL_WAKE,		/* the host waits for PASS entries */
	GL_IDLE_ROUNDS,		/* consecutive empty rounds */
	GL_TX_DEAD,		/* the SQ went to error: nothing more on it */
	GL_LAST_BOUND,		/* debug: the RQ bound last written */
	GL_CI,			/* CQ consumer index, every wave in step */
	GL_RING,		/* doorbell records, RQ, CQs: the ring buffer */
	GL_COUNT = GL_RING + 2,
};
_Static_assert(GL_COUNT <= 64, "a lane each");

/* The meeting place in LDS. */
struct gda_meet {
	uint32_t cmd;		/* nonzero: stop */
	uint32_t pass_pc;	/* PASS entries appended before this round */
	uint32_t sq_pc;
	uint32_t sq_cc;
	uint32_t n[KNOD_PERSIST_GDA_WAVES_MAX];	/* each wave's run of CQEs */
	uint32_t k[KNOD_PERSIST_GDA_WAVES_MAX];	/* ...its XDP_TX count */
	uint32_t p[KNOD_PERSIST_GDA_WAVES_MAX];	/* ...its XDP_PASS count */
};
_Static_assert(sizeof(struct gda_meet) <= KNOD_PERSIST_GDA_LDS_BYTES,
	       "the meeting place");

struct gda {
	uint32_t s;		/* the state lanes */
	uint64_t packets;
	ctl_t ctl;
	entry_t entry;
	uint32_t queue, wave;
	GLOBAL uint8_t *ring;
	LDS struct gda_meet *meet;
};

#define GL(g, f)	__builtin_amdgcn_readlane((g)->s, GL_##f)
#define GL64(g, f)	((uint64_t)__builtin_amdgcn_readlane((g)->s,	\
							 GL_##f + 1) << 32 |	\
			 (uint32_t)__builtin_amdgcn_readlane((g)->s, GL_##f))
#define SET(g, f, v)	((g)->s = writelane((g)->s, (v), GL_##f))
#define SET64(g, f, v)	do {						\
	uint64_t __v = (v);						\
	SET(g, f, (uint32_t)__v);					\
	(g)->s = writelane((g)->s, __v >> 32, GL_##f + 1);		\
} while (0)

/* Into one lane whatever EXEC has. */
static inline uint32_t writelane(uint32_t s, uint32_t v, int lane)
{
	__asm__ volatile("v_writelane_b32 %0, %1, %2"
			 : "+v"(s) : "s"(uniform32(v)), "i"(lane));
	return s;
}

static inline uint32_t bswap32(uint32_t v)
{
	return __builtin_bswap32(v);
}

/* Rings, doorbell records and the NIC's doorbell: past every cache. */
static inline void put32(GLOBAL void *p, uint32_t v)
{
	__builtin_nontemporal_store(v, (GLOBAL uint32_t *)p);
}

static inline void put64(GLOBAL void *p, uint64_t v)
{
	__builtin_nontemporal_store(v, (GLOBAL uint64_t *)p);
}

static inline void put128(GLOBAL void *p, v4u v)
{
	__builtin_nontemporal_store(v, (GLOBAL v4u *)p);
}

static inline uint32_t get32(GLOBAL const void *p)
{
	return load((gptr)p);
}

static inline uint64_t get64(GLOBAL const void *p)
{
	return (uint64_t)load((gptr)p + 1) << 32 | load((gptr)p);
}

/* Every store so far is out before any after. */
static inline void stores_out(void)
{
	__builtin_amdgcn_fence(__ATOMIC_RELEASE, "agent");
}

/* What one wave wrote to LDS, the others see after it. */
static inline void barrier(void)
{
	__builtin_amdgcn_fence(__ATOMIC_RELEASE, "workgroup");
	__builtin_amdgcn_s_barrier();
	__builtin_amdgcn_fence(__ATOMIC_ACQUIRE, "workgroup");
}

/* Keep the compiler from moving memory accesses across it. */
#define ORDER()		__asm__ volatile("" ::: "memory")

static inline uint64_t lanes_below(uint32_t n)
{
	return n >= 64 ? ~0ull : (1ull << n) - 1;
}

/* The lowest lane not in @m, or 64. */
static inline uint32_t run_of(uint64_t m)
{
	return ~m ? __builtin_ctzll(~m) : 64;
}

static inline uint32_t rank(uint64_t m)
{
	return __builtin_amdgcn_mbcnt_hi(m >> 32,
					 __builtin_amdgcn_mbcnt_lo(m, 0));
}

static inline uint64_t ballot(int c)
{
	return __builtin_amdgcn_ballot_w64(c);
}

/* An owned CQE: its owner bit is the pass round the ring @pos is on, and its
 * opcode is not the invalid one a fresh ring holds.  @w is its last dword.
 */
static inline int cqe_ours(uint32_t w, uint32_t pos, uint32_t log)
{
	uint32_t op_own = w >> CQE_OP_OWN_SHIFT;

	return (op_own & KNOD_MLX5_CQE_OWNER_MASK) == ((pos >> log) & 1) &&
	       (op_own >> KNOD_MLX5_CQE_OPCODE_SHIFT) != KNOD_MLX5_CQE_INVALID;
}

static inline GLOBAL struct knod_mlx5_cqe64 *cqe_at(GLOBAL uint8_t *cq,
						     uint32_t pos, uint32_t log)
{
	return (GLOBAL struct knod_mlx5_cqe64 *)
		(cq + ((pos & ((1u << log) - 1)) << KNOD_MLX5_CQE_SHIFT));
}

/* A CQE's last dword: the WQE counter, the signature and op_own. */
static inline uint32_t cqe_tail(GLOBAL struct knod_mlx5_cqe64 *c)
{
	return get32(&c->wqe_counter);
}

static inline uint32_t wqe_counter(uint32_t tail)
{
	return __builtin_bswap16(tail & 0xffff);
}

/* What outlives the shader, and what the host reads, to the queue's entry. */
static INLINE void gda_save(struct gda *g)
{
	entry_t e = g->entry;

	if (lane_id())
		return;
	put32(&e->ci, GL(g, CI));
	put64(&e->packets, g->packets);
	put64(&e->rounds, GL64(g, ROUNDS));
	put128(&e->tx_packets,
	       (v4u){ GL(g, TX_PKTS), __builtin_amdgcn_readlane(g->s, GL_TX_PKTS + 1),
		      GL(g, TX_FULL), __builtin_amdgcn_readlane(g->s, GL_TX_FULL + 1) });
	put128(&e->sq_pc, (v4u){ GL(g, SQ_PC), GL(g, SQ_CC), GL(g, TX_CI),
				 GL(g, TX_GEN) });
}

/* Wave 0 leaving: the others are told at the barrier they wait at, then where
 * the rings got to goes to the entry for the next shader.
 */
static INLINE __attribute__((noreturn)) void gda_stop(struct gda *g)
{
	if (!lane_id())
		g->meet->cmd = 1;
	barrier();
	gda_save(g);
	stores_out();
	__builtin_amdgcn_endpgm();
}

/*
 * Take what the NIC has sent off the TX CQ: SQ_CC and TX_CI move.  One CQE
 * per round that sent, on its last WQE, so the run of new ones ends on a
 * round boundary.  @pc is the producer.
 */
static INLINE void gda_tx_reap(struct gda *g, uint32_t pc)
{
	uint32_t ci = GL(g, TX_CI), log = GL(g, TX_CQ_LOG);
	uint32_t pos = ci + lane_id(), tail, n, last, cc;
	GLOBAL struct knod_mlx5_cqe64 *c;
	uint64_t err;

	c = cqe_at(g->ring + KNOD_PERSIST_RING_TX_CQ_OFF, pos, log);
	tail = cqe_tail(c);
	n = run_of(ballot(cqe_ours(tail, pos, log)));
	if (!n)
		return;
	ci += n;

	/* An error: the SQ has stopped for good and flushes the rest, a CQE
	 * each, off the rounds' boundaries.  None of it is sent, so nothing is
	 * held for it; and nothing more goes on it.
	 */
	err = ballot(tail >> (CQE_OP_OWN_SHIFT + KNOD_MLX5_CQE_OPCODE_SHIFT) ==
		     KNOD_MLX5_CQE_REQ_ERR) & lanes_below(n);
	if (err) {
		cc = pc;
		SET(g, TX_DEAD, 1);
	} else {
		/* Everything up to the last one's WQE is sent: widen it to the
		 * free-running producer, never more than an SQ ahead.
		 */
		last = wqe_counter(__builtin_amdgcn_readlane(tail, n - 1)) + 1;
		cc = pc - ((pc - last) & 0xffff);
	}
	SET(g, SQ_CC, cc);
	SET(g, TX_CI, ci);
	if (!lane_id())
		put32(g->ring + KNOD_PERSIST_RING_TX_CQ_DB,
		      bswap32(ci & 0xffffff));
}

static INLINE void gda_regress_dbg(struct gda *g, uint32_t bound, uint32_t prev,
			    uint32_t pass_cc, uint32_t cand)
{
	GLOBAL uint32_t *d = g->entry->regress_dbg;

	if (lane_id())
		return;
	put32(&d[KNOD_PERSIST_REGRESS_DELTA], bound - prev);
	add(&d[KNOD_PERSIST_REGRESS_HITS], 1);
	put32(&d[KNOD_PERSIST_REGRESS_S2], GL(g, CI));
	put32(&d[KNOD_PERSIST_REGRESS_BOUND], bound);
	put32(&d[KNOD_PERSIST_REGRESS_SQ_PC], GL(g, SQ_PC));
	put32(&d[KNOD_PERSIST_REGRESS_SQ_CC], GL(g, SQ_CC));
	put32(&d[KNOD_PERSIST_REGRESS_PASS_CC], pass_cc);
	put32(&d[KNOD_PERSIST_REGRESS_CAND], cand);
}

/*
 * Hand the CQ entries back up to CI, and the RQ entries up to the oldest one
 * whose packet is still waiting to be sent or copied out.
 */
static INLINE void gda_rq_release(struct gda *g)
{
	uint32_t ci = GL(g, CI), pc = GL(g, SQ_PC), cc = GL(g, SQ_CC);
	uint32_t bound = ci, ppc, pcc, floor, cand = 0, prev;

	if (pc != cc)
		bound = get32(g->ring + KNOD_PERSIST_RING_RQPOS_OFF +
			      4 * (cc & GL(g, SQ_MASK)));

	/* The oldest packet the host is still copying out, if that is older. */
	ppc = GL(g, PASS_PC);
	pcc = GL(g, PASS_CC);
	if (ppc != pcc) {
		pcc = get32(&g->entry->pass_cc);
		floor = GL(g, PASS_FLOOR);
		if ((int32_t)(pcc - floor) < 0)
			pcc = floor;
		SET(g, PASS_CC, pcc);
		if (ppc != pcc) {
			cand = get32(g->ring + KNOD_PERSIST_RING_PASS_RQPOS_OFF +
				     4 * (pcc & GL(g, PASS_MASK)));
			if (ci - cand > ci - bound)
				bound = cand;
		}
	}

	/* Debug: the bound only ever moves on; count one that goes back. */
	prev = GL(g, LAST_BOUND);
	SET(g, LAST_BOUND, bound);
	if ((int32_t)(bound - prev) < 0)
		gda_regress_dbg(g, bound, prev, pcc, cand);

	if (lane_id())
		return;
	put32(g->ring + KNOD_PERSIST_RING_CQ_DB, bswap32(ci & 0xffffff));
	/* The CQ's room is out before the RQ entries that fill it, as
	 * mlx5e_poll_rx_cq() orders them: a NIC that sees the entries first
	 * completes into a CQ it still thinks full, and overruns it.
	 */
	stores_out();
	put32(g->ring + KNOD_PERSIST_RING_RQ_DB,
	      bswap32((bound + GL(g, RQ_SIZE)) & 0xffff));
}

/* Wave 0: park while the host changes maps, or leave if it wants the shader
 * gone.  The other waves wait at the next barrier either way.
 */
static INLINE void gda_park_or_stop(struct gda *g)
{
	GLOBAL struct knod_persistent_control *ctl = g->ctl;
	uint32_t pause;

	if (get32(&ctl->version) != KNOD_PERSIST_VERSION || get32(&ctl->stop))
		gda_stop(g);
	pause = get32(&ctl->pause);
	if (!pause)
		return;

	/* Where the rings got to, then that this queue is parked: the host may
	 * read anything once it sees the ack.
	 */
	gda_save(g);
	stores_out();
	if (!lane_id())
		put32(&g->entry->pause_ack, pause);
	stores_out();
	do {
		__builtin_amdgcn_s_sleep(8);
		if (get32(&ctl->version) != KNOD_PERSIST_VERSION ||
		    get32(&ctl->stop))
			gda_stop(g);
	} while (get32(&ctl->pause));
}

/* Post every RQ entry, entry i to RX page i: a round only ever gives an entry
 * back as it was.
 */
static INLINE void gda_post_rq(struct gda *g)
{
	entry_t e = g->entry;
	uint32_t size = GL(g, RQ_SIZE), stride_log = get32(&e->rq_log_stride);
	uint32_t frag = bswap32(get32(&e->frag)), mkey = get32(&e->mkey_be);
	uint32_t headroom = GL(g, HEADROOM), stagger = GL(g, STAGGER);
	uint32_t smask = GL(g, STAGGER_MASK);
	GLOBAL uint64_t *rx_dma = (GLOBAL uint64_t *)GL64(g, RX_DMA);
	uint32_t i;

	for (i = lane_id(); i < size; i += 64) {
		uint64_t a = rx_dma[i] + headroom + stagger * (i & smask);

		put128(g->ring + KNOD_PERSIST_RING_RQ_OFF + (i << stride_log),
		       (v4u){ frag, mkey, bswap32(a >> 32), bswap32(a) });
	}
	stores_out();
	if (lane_id())
		return;
	put32(g->ring + KNOD_PERSIST_RING_RQ_DB, bswap32(size));
	put64(&e->ci, (uint64_t)get32(&e->gen) << 32);
	put32(&e->pass_floor, GL(g, PASS_FLOOR));
	stores_out();
}

static INLINE void gda_load(struct gda *g, ctl_t ctl, uint32_t queue,
			    uint32_t wave)
{
	g->ctl = ctl;
	g->entry = &ctl->gda[queue];
	g->queue = queue;
	g->wave = wave;
	g->ring = (GLOBAL uint8_t *)GL64(g, RING);
	g->meet = (LDS struct gda_meet *)(uintptr_t)GL(g, LDS);
}

/*
 * Pick up the rings where the last shader left them, or post them afresh for
 * a new build.  A wave the queue does not need, or a queue whose rings the NIC
 * did not give us, ends here.
 */
v4u cfn_gda_init(ctl_t ctl, uint32_t queue, uint32_t wave)
{
	struct gda g = { .s = 0 };
	entry_t e;
	uint64_t sq, db;
	uint32_t mask, pcc, floor, waves;
	int fresh;

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	queue = uniform32(queue);
	wave = uniform32(wave);
	e = &ctl->gda[queue];
	waves = get32(&ctl->gda_waves);
	if (!get32(&e->live) || wave >= waves)
		__builtin_amdgcn_endpgm();

	SET(&g, RQ_LOG, get32(&e->rq_log));
	SET(&g, RQ_SIZE, 1u << get32(&e->rq_log));
	SET(&g, CQ_LOG, get32(&e->cq_log));
	SET(&g, HEADROOM, get32(&e->headroom));
	SET64(&g, RX_DMA, get64(&e->rx_dma));
	SET64(&g, TX_PKTS, get64(&e->tx_packets));
	SET64(&g, TX_FULL, get64(&e->tx_full));
	g.packets = get64(&e->packets);

	/* The SQ, if the NIC gave us that too and its doorbell is mapped. */
	sq = get64(&e->sq);
	db = get64(&ctl->tx_db[queue]);
	mask = sq && db ? get32(&e->sq_mask) : 0;
	SET(&g, SQ_MASK, mask);
	SET64(&g, SQ, sq);
	SET64(&g, DB, db);
	SET(&g, SQN, get32(&e->sqn));
	SET(&g, TX_MKEY, get32(&e->tx_mkey_be));
	SET(&g, TX_CQ_LOG, get32(&e->tx_cq_log));
	SET(&g, TX_GEN, get32(&e->tx_gen));
	/* Where the last shader left this build of the SQ, or its start. */
	if (mask && get32(&e->tx_gen) == get32(&e->tx_posted_gen)) {
		SET(&g, SQ_PC, get32(&e->sq_pc));
		SET(&g, SQ_CC, get32(&e->sq_cc));
		SET(&g, TX_CI, get32(&e->tx_ci));
	}

	SET64(&g, PASS_RING, get64(&e->pass_ring));
	SET(&g, PASS_MASK, get32(&e->pass_mask));
	SET(&g, PASS_PC, get32(&e->pass_pc));
	pcc = get32(&e->pass_cc);
	floor = get32(&e->pass_floor);
	SET(&g, PASS_FLOOR, floor);
	SET(&g, PASS_CC, (int32_t)(pcc - floor) < 0 ? floor : pcc);
	SET(&g, STAGGER, get32(&e->stagger));
	SET(&g, STAGGER_MASK, get32(&e->stagger_mask));
	SET64(&g, ROUNDS, get64(&e->rounds));
	SET(&g, WAVES, waves);
	SET64(&g, PARAM, get64(&ctl->gda_param));
	SET(&g, LDS, get32(&ctl->gda_lds));
	SET64(&g, RX_BASE, get64(&e->rx_base));
	SET(&g, PAGE_SHIFT,
	    load((gptr)(GL64(&g, PARAM) + KNOD_BLOB_PARAM_PAGE_SHIFT)));
	SET(&g, CI, get32(&e->ci));
	SET64(&g, RING, get64(&e->ring));
	fresh = get32(&e->posted_gen) != get32(&e->gen);
	gda_load(&g, ctl, queue, wave);

	/* LDS starts undefined: no stop until wave 0 says so.  Every wave has
	 * read the entry before wave 0 writes it.
	 */
	if (!wave && !lane_id())
		g.meet->cmd = 0;
	barrier();

	/* Posted for this build of the rings already: pick up where the last
	 * shader left the CQ.  Otherwise wave 0 posts every RQ entry.
	 */
	if (fresh) {
		SET(&g, CI, 0);
		/* What the host still copies out came in on the last build's
		 * RQ.
		 */
		SET(&g, PASS_FLOOR, GL(&g, PASS_PC));
		SET(&g, PASS_CC, GL(&g, PASS_PC));
		if (!wave)
			gda_post_rq(&g);
	}

	return (v4u){ g.s, g.packets, g.packets >> 32, 0 };
}

/* The RQ entry a CQE says differs from the one its position is for: the NIC
 * and CI disagree about where the RQ is.
 */
static INLINE void gda_sync_dbg(struct gda *g, uint64_t off, uint32_t pos,
			 uint32_t entry)
{
	uint32_t l = __builtin_ctzll(off);
	GLOBAL uint32_t *d = g->entry->sync_dbg;

	if (lane_id())
		return;
	add(&d[KNOD_PERSIST_SYNC_HITS], 1);
	put32(&d[KNOD_PERSIST_SYNC_ENTRIES],
	      (__builtin_amdgcn_readlane(pos, l) & 0xffff) |
	      __builtin_amdgcn_readlane(entry, l) << 16);
}

/*
 * Wait for a round's packets and hand this wave's to the program.  Wave 0
 * reads the control block and parks or stops for all of them; then every wave
 * reads its 64 CQEs and the round is the runs of every wave up to the first
 * that was short.  While there is none, what was sent still frees RQ entries,
 * and nothing else would.
 *
 * Returns, per lane, what the program's registers want: the page, where the
 * packet is in it and how long, the lane's index in the queue and in all
 * queues, the page's address, the lane's xdp_md, then which lanes have one,
 * the parameter block, and the state back.
 */
v16u cfn_gda_round_begin(ctl_t ctl, uint32_t queue, uint32_t wave,
			 uint32_t s, uint32_t pk_lo, uint32_t pk_hi)
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };
	uint32_t pos, tail, run, n, full, i, waves, idle, len, page, off, idx;
	GLOBAL struct knod_mlx5_cqe64 *c;
	uint64_t mine, page_base, param, ctx, bad;
	v16u r;

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_load(&g, ctl, uniform32(queue), uniform32(wave));
	waves = GL(&g, WAVES);

	for (;;) {
		if (!g.wave) {
			gda_park_or_stop(&g);
			SET(&g, WAKE, get32(&ctl->pass_wake));
		}
		barrier();
		if (g.meet->cmd)
			__builtin_amdgcn_endpgm();

		/* Lane i reads CQE ci + 64w + i's last dword: the RQ entry it
		 * completes, its opcode and its owner bit.
		 */
		pos = GL(&g, CI) + 64 * g.wave + lane_id();
		c = cqe_at(g.ring + KNOD_PERSIST_RING_CQ_OFF, pos,
			   GL(&g, CQ_LOG));
		tail = cqe_tail(c);
		run = run_of(ballot(cqe_ours(tail, pos, GL(&g, CQ_LOG))));
		if (!lane_id())
			g.meet->n[g.wave] = run;
		/* Maps the host changed while we were parked, and anything
		 * else a program reads that someone else writes; once, for the
		 * workgroup, before any wave's program runs.
		 */
		if (!g.wave && run) {
			__builtin_amdgcn_fence(__ATOMIC_ACQUIRE, "agent");
			__builtin_amdgcn_s_dcache_inv();
		}
		barrier();

		n = 0;
		mine = 0;
		full = 1;
		for (i = 0; i < KNOD_PERSIST_GDA_WAVES_MAX; i++) {
			uint32_t r = i < waves ? g.meet->n[i] * full : 0;

			n += r;
			if (i == g.wave)
				mine = r;
			if (r != 64)
				full = 0;
		}
		SET(&g, N, n);
		SET(&g, RUN, mine);
		if (n)
			break;

		if (!g.wave &&
		    (GL(&g, PASS_PC) != GL(&g, PASS_CC) ||
		     GL(&g, SQ_PC) != GL(&g, SQ_CC))) {
			if (GL(&g, SQ_PC) != GL(&g, SQ_CC))
				gda_tx_reap(&g, GL(&g, SQ_PC));
			gda_rq_release(&g);
		}
		idle = GL(&g, IDLE_ROUNDS);
		if (idle < 64) {
			SET(&g, IDLE_ROUNDS, idle + 1);
			__builtin_amdgcn_s_sleep(1);
		} else {
#pragma clang loop unroll(disable)
			for (i = 0; i < 1024; i++)
				__builtin_amdgcn_s_sleep(15);
		}
	}
	SET(&g, IDLE_ROUNDS, 0);

	/* The CQE is ours through its owner word before its payload is read:
	 * both together can take a new owner with the previous byte count.
	 */
	ORDER();
	len = bswap32(get32(&c->byte_cnt));

	/* The RQ entry this CQE completes, and so the page: entry i is page
	 * i.
	 */
	page = wqe_counter(tail);
	bad = ballot((pos & 0xffff) != page) & lanes_below(mine);
	if (bad)
		gda_sync_dbg(&g, bad, pos, page);
	page &= GL(&g, RQ_SIZE) - 1;

	/* page base = rx_base + (page << page_shift); data past the headroom
	 * and the page's stagger.
	 */
	page_base = GL64(&g, RX_BASE) + ((uint64_t)page << GL(&g, PAGE_SHIFT));
	off = GL(&g, HEADROOM) + GL(&g, STAGGER) * (page & GL(&g, STAGGER_MASK));
	idx = g.queue * waves * 64 + 64 * g.wave + lane_id();
	param = GL64(&g, PARAM);
	ctx = param + KNOD_BLOB_PARAM_SUB + (uint64_t)idx * KNOD_BLOB_SUB_SIZE;

	r.s0 = page;
	r.s1 = off;
	r.s2 = len;
	r.s3 = 64 * g.wave + lane_id();
	r.s4 = idx;
	r.s5 = page_base;
	r.s6 = page_base >> 32;
	r.s7 = ctx;
	r.s8 = ctx >> 32;
	r.s9 = lanes_below(mine);
	r.sa = lanes_below(mine) >> 32;
	r.sb = param;
	r.sc = param >> 32;
	r.sd = g.s;
	r.se = g.packets;
	r.sf = g.packets >> 32;
	return r;
}

/* Fold each wave's XDP_TX into the SQ's room, in wave order, so the WQEs
 * follow the packets' order on the RQ.
 */
struct gda_share {
	uint32_t asked;		/* XDP_TX all waves asked for */
	uint32_t given;		/* WQEs all waves got */
	uint32_t base;		/* this wave's first, from the round's */
	uint32_t mine;		/* this wave's */
	uint32_t last;		/* the last wave with one */
};

static INLINE struct gda_share gda_share(struct gda *g, uint32_t left)
{
	struct gda_share x = { 0 };
	uint32_t i, k;

	for (i = 0; i < KNOD_PERSIST_GDA_WAVES_MAX; i++) {
		k = i < GL(g, WAVES) ? g->meet->k[i] : 0;
		x.asked += k;
		k = k < left ? k : left;
		left -= k;
		if (i == g->wave) {
			x.base = x.given;
			x.mine = k;
		}
		if (k)
			x.last = i;
		x.given += k;
	}
	return x;
}

/* One send WQE: the packet's place in the RX buffer, nothing inlined. */
static INLINE void gda_post_wqe(struct gda *g, uint32_t counter, uint32_t page,
			 uint32_t off, uint32_t len, int signal)
{
	GLOBAL uint64_t *rx_dma = (GLOBAL uint64_t *)GL64(g, RX_DMA);
	GLOBAL uint8_t *wqe = (GLOBAL uint8_t *)GL64(g, SQ) +
		((counter & GL(g, SQ_MASK)) << KNOD_MLX5_WQEBB_SHIFT);
	uint64_t addr = rx_dma[page] + off;
	union {
		struct knod_mlx5_send_wqe w;
		v4u v[3];
	} u;

	_Static_assert(sizeof(u.w) == sizeof(u.v), "three stores");
	u.w.ctrl.opmod_idx_opcode =
		bswap32((counter & 0xffff) << 8 | KNOD_MLX5_OPCODE_SEND);
	u.w.ctrl.qpn_ds = bswap32(GL(g, SQN) << 8 | SEND_DS);
	u.v[0].s2 = signal ? KNOD_MLX5_CTRL_CQ_UPDATE << CTRL_FM_CE_SE_SHIFT : 0;
	u.w.ctrl.imm = 0;
	u.v[1] = 0;
	u.w.data.byte_count = bswap32(len);
	u.w.data.lkey = GL(g, TX_MKEY);		/* already big endian */
	u.v[2].s2 = bswap32(addr >> 32);
	u.v[2].s3 = bswap32(addr);
	put128(wqe, u.v[0]);
	put128(wqe + offsetof(struct knod_mlx5_send_wqe, eth), u.v[1]);
	put128(wqe + offsetof(struct knod_mlx5_send_wqe, data), u.v[2]);
}

/*
 * Carry out the program's verdicts: what it sent goes on the SQ, what it
 * passed to the host's PASS ring, and wave 0 tells the NIC and the host and
 * gives the CQ and RQ entries back - every verdict but a send as a drop does.
 *
 * @verdict, @page, @off and @len per lane, of the lanes in @lo:@hi.
 */
v4u cfn_gda_round_end(ctl_t ctl, uint32_t queue, uint32_t wave, uint32_t s,
		      uint32_t pk_lo, uint32_t pk_hi, uint32_t verdict,
		      uint32_t page, uint32_t off, uint32_t len, uint32_t lo,
		      uint32_t hi)
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };
	uint64_t live = (uint64_t)uniform32(hi) << 32 | uniform32(lo);
	uint32_t pc, cc, left, ppc, pbase = 0, ptotal = 0, i, p, n, pos;
	int in = (live >> lane_id()) & 1;
	uint64_t tx, pass;
	struct gda_share x;

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_load(&g, ctl, uniform32(queue), uniform32(wave));
	n = GL(&g, N);

	/* What this wave sends and hands to the host, for the others to see.
	 * With no SQ an XDP_TX drops.  Wave 0 takes what the NIC has sent since
	 * the last look, and says where the SQ and the PASS ring stand.
	 */
	tx = GL(&g, SQ_MASK) ? ballot(in && verdict == XDP_TX) : 0;
	pass = ballot(in && verdict == XDP_PASS);
	if (!lane_id()) {
		g.meet->k[g.wave] = __builtin_popcountll(tx);
		g.meet->p[g.wave] = __builtin_popcountll(pass);
	}
	if (!g.wave) {
		pc = GL(&g, SQ_PC);
		if (GL(&g, SQ_MASK) && pc != GL(&g, SQ_CC))
			gda_tx_reap(&g, pc);
		cc = GL(&g, SQ_CC);
		/* A dead SQ has room for none: the waves see it full. */
		if (GL(&g, TX_DEAD))
			cc = pc - GL(&g, SQ_MASK) - 1;
		if (!lane_id()) {
			g.meet->sq_pc = pc;
			g.meet->sq_cc = cc;
			g.meet->pass_pc = GL(&g, PASS_PC);
		}
	}
	barrier();

	pc = g.meet->sq_pc;
	cc = g.meet->sq_cc;
	ppc = g.meet->pass_pc;
	for (i = 0; i < KNOD_PERSIST_GDA_WAVES_MAX; i++) {
		p = i < GL(&g, WAVES) ? g.meet->p[i] : 0;
		ptotal += p;
		if (i < g.wave)
			pbase += p;
	}
	left = GL(&g, SQ_MASK) - (pc - cc) + 1;
	x = gda_share(&g, left);

	/* Each WQE says which RQ position its packet came in on, so that
	 * whatever WQE the NIC's completions stop at, the RQ is held from the
	 * right place.  Only the round's last WQE asks for a completion.
	 */
	pos = GL(&g, CI) + 64 * g.wave + lane_id();
	if (x.mine && ((tx >> lane_id()) & 1) && rank(tx) < x.mine) {
		uint32_t counter = pc + x.base + rank(tx);

		gda_post_wqe(&g, counter, page, off, len,
			     g.wave == x.last && rank(tx) == x.mine - 1);
		put32(g.ring + KNOD_PERSIST_RING_RQPOS_OFF +
		      4 * (counter & GL(&g, SQ_MASK)), pos);
	}

	/* One PASS entry per lane that passes: the page, where the packet is
	 * in it and how long; and the RQ position it holds until the host is
	 * done.
	 */
	if ((pass >> lane_id()) & 1) {
		uint32_t slot = (ppc + pbase + rank(pass)) & GL(&g, PASS_MASK);

		put64((GLOBAL uint64_t *)GL64(&g, PASS_RING) + slot,
		      (uint64_t)(off | len << 16) << 32 | page);
		put32(g.ring + KNOD_PERSIST_RING_PASS_RQPOS_OFF + 4 * slot,
		      pos);
	}

	/* Every wave's entries are in memory before wave 0 says they are. */
	stores_out();
	barrier();

	if (!g.wave) {
		if (ptotal) {
			ppc += ptotal;
			SET(&g, PASS_PC, ppc);
			if (!lane_id())
				put32(&g.entry->pass_pc, ppc);
			/* The host sleeps until PASS entries come: take the
			 * request, so the other rounds leave it be, and
			 * interrupt once they are in memory.
			 */
			if (GL(&g, WAKE)) {
				SET(&g, WAKE, 0);
				if (!lane_id())
					put32(&ctl->pass_wake, 0);
				stores_out();
				__builtin_amdgcn_s_sendmsg(1, KNOD_PERSIST_PASS_IRQ);
			}
		}
		SET64(&g, TX_FULL, GL64(&g, TX_FULL) + x.asked - x.given);
		if (x.given) {
			SET64(&g, TX_PKTS, GL64(&g, TX_PKTS) + x.given);
			pc += x.given;
			SET(&g, SQ_PC, pc);
			/* The record, then the doorbell: the last WQE's first
			 * eight bytes, which are all a function of its counter.
			 */
			if (!lane_id())
				put32(g.ring + KNOD_PERSIST_RING_SQ_DB,
				      bswap32(pc & 0xffff));
			stores_out();
			if (!lane_id())
				put64((GLOBAL void *)GL64(&g, DB),
				      (uint64_t)bswap32(GL(&g, SQN) << 8 |
							SEND_DS) << 32 |
				      bswap32(((pc - 1) & 0xffff) << 8 |
					      KNOD_MLX5_OPCODE_SEND));
		}
	}

	/* Every wave moves its consumer index; wave 0 gives the CQ and RQ
	 * entries back.
	 */
	SET(&g, CI, GL(&g, CI) + n);
	if (!g.wave) {
		gda_rq_release(&g);
		g.packets += n;
		SET64(&g, ROUNDS, GL64(&g, ROUNDS) + 1);
		gda_save(&g);
	}
	return (v4u){ g.s, g.packets, g.packets >> 32, 0 };
}
