// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GDA: what wraps a program when the shader runs the NIC's rings itself.
 *
 * The prologue sets up exactly the registers KNOD_PROLOGUE_BODY sets up - the
 * program cannot tell the difference - but its packets come from the NIC's CQ
 * rather than a batch the host built.  The epilogue sends what the program
 * sent on the queue's XDP SQ, hands what it passed to the host, and gives the
 * rest back to the RQ.
 *
 * One workgroup per queue, and up to KNOD_PERSIST_GDA_WAVES_MAX of its waves
 * take packets, 64 CQEs each a round: wave w the ones at ci + 64w.  Wave 0
 * alone keeps the rings - the doorbell records, the SQ, the TX completions,
 * the control block - and the waves meet in a few bytes of LDS past the
 * program's stack, four barriers a round.  The rest of the workgroup ends.
 *
 * RQ entry i always holds RX page i.  The NIC consumes the RQ in order and
 * completes the SQ in order, so no page ever needs to move between entries:
 * an entry whose packet went out on the SQ is held until the NIC says it was
 * sent, one passed to the host until the host says it has copied it out, and
 * the RQ producer stops at the oldest such entry.  Every WQE and every PASS
 * entry records the RQ position its packet came in on, which is all that
 * takes.
 *
 * This file is compiled once per routine that needs it (KNOD_GDA_ROUTINE),
 * into that routine's own section, and called from the glue in gda.inc.  The
 * glue calls with every lane on and moves the results into the registers the
 * JIT expects.  Between calls the engine's state lives in v[GS]
 * (gda_lanes.h), which the glue keeps and nothing here can reach past.
 *
 * The glue does not call the entry points: it jumps to them, and they jump
 * back to where it says (back_to_glue*()).  A function that is called keeps
 * the registers the calling convention gives the caller, and with this many
 * live values that means spilling to scratch, which the shader does not have.
 * So the entry points are built as kernels, which keep nothing, and take
 * their arguments from v0 upwards where the glue left them (the asm at the top
 * of each, which has to come before anything else).  Everything else is
 * inlined into them, for the same reason.
 */

#include "gda_hw.h"
#include "gda_lanes.h"
#include <linux/knod_blob.h>
#include <linux/knod_persistent.h>

#ifndef KNOD_GDA_ROUTINE
# error "build with -DKNOD_GDA_ROUTINE=<the routine this is for>"
#endif

#define __STR(x)	#x
#define STR(x)		__STR(x)
#define __CAT(a, b)	a##b
#define CAT(a, b)	__CAT(a, b)
/* gda_<routine>__<name>: not knod_, which tools/pack.py takes for a routine. */
#define ENTRY(name)	CAT(gda_, CAT(KNOD_GDA_ROUTINE, CAT(__, name)))
#define __entry		__attribute__((amdgpu_kernel, noreturn, used, \
				       visibility("hidden"), \
				       section(".text." STR(KNOD_GDA_ROUTINE) ".c")))

#define XDP_DROP	1
#define XDP_PASS	2
#define XDP_TX		3

/* The mlx5 send WQE the epilogue writes: the fields it sets, nothing more. */
#define MLX5_OPCODE_SEND	0x0a
#define MLX5_CQE_REQ_ERR	0x0d
#define MLX5_CQE_INVALID	0x0f
#define MLX5_CTRL_CQ_UPDATE	0x08	/* fm_ce_se */
#define MLX5_SEND_DS		3	/* ctrl, eth, one data segment */

typedef struct knod_persistent_control __global	ctl_t;
typedef struct knod_persistent_gda __global	entry_t;

/* The meeting place in LDS, KNOD_PERSIST_GDA_LDS_BYTES of it. */
struct gda_lds {
	u32	cmd;		/* nonzero: stop */
	u32	pass_pc;	/* PASS entries appended before this round */
	u32	sq_pc;
	u32	sq_cc;
	u32	n[4] __attribute__((aligned(16)));	/* each wave's run of CQEs */
	u32	k[4] __attribute__((aligned(16)));	/* each wave's XDP_TX count */
	u32	p[4] __attribute__((aligned(16)));	/* each wave's XDP_PASS count */
};
_Static_assert(__builtin_offsetof(struct gda_lds, n) == 16 &&
	       __builtin_offsetof(struct gda_lds, p) == 48, "LDS layout");
_Static_assert(sizeof(struct gda_lds) <= KNOD_PERSIST_GDA_LDS_BYTES, "LDS");
_Static_assert(KNOD_PERSIST_GDA_WAVES_MAX == 4, "four waves of tallies");

/* ---- v[GS] ---- */

static __always_inline u32 get(u32 gs, u32 l)
{
	return lane_value(gs, l);
}

static __always_inline u64 get64(u32 gs, u32 l)
{
	return get(gs, l) | (u64)get(gs, l + 1) << 32;
}

static __always_inline u32 set(u32 gs, u32 l, u32 v)
{
	return with_lane(gs, l, v);
}

static __always_inline u32 set64(u32 gs, u32 l, u64 v)
{
	return set(set(gs, l, (u32)v), l + 1, (u32)(v >> 32));
}

/* ---- where things are ---- */

static __always_inline ctl_t *ctl(u32 gs)
{
	return (ctl_t *)get64(gs, GL_HW_KERNARG);
}

static __always_inline entry_t *entry(u32 gs)
{
	return &ctl(gs)->gda[get(gs, GL_HW_WG_Y)];
}

static __always_inline u8 __global *ring(u32 gs)
{
	return (u8 __global *)get64(gs, GL_RING);
}

#define RING(gs, type, off)	((type __global *)(ring(gs) + (off)))

static __always_inline struct gda_lds __lds *lds(u32 gs)
{
	return (struct gda_lds __lds *)(unsigned long)get(gs, GL_LDS);
}

static __always_inline bool lane0(void)
{
	return lane_id() == 0;
}

/* The later of two free-running counts. */
static __always_inline u32 later(u32 a, u32 b)
{
	return (s32)(a - b) < 0 ? b : a;
}

/* ---- the control block ---- */

/* What outlives the shader, and what the host reads, to the queue's entry. */
static __always_inline void save(u32 gs)
{
	entry_t *e = entry(gs);

	if (!lane0())
		return;
	store_out(&e->ci, get(gs, GL_CI));
	store_out(&e->packets, get64(gs, GL_PKTS));
	store_out(&e->rounds, get64(gs, GL_ROUNDS));
	store_out(&e->tx_packets, get64(gs, GL_TX_PKTS));
	store_out(&e->tx_full, get64(gs, GL_TX_FULL));
	store_out(&e->sq_pc, get(gs, GL_SQ_PC));
	store_out(&e->sq_cc, get(gs, GL_SQ_CC));
	store_out(&e->tx_ci, get(gs, GL_TX_CI));
	store_out(&e->tx_posted_gen, get(gs, GL_TX_GEN));
}

/*
 * Wave 0 leaving: the others are told at the barrier they are all waiting at,
 * then where the rings got to goes to the entry for the next shader.
 */
static __always_inline void stop(u32 gs)
{
	if (lane0())
		lds(gs)->cmd = 1;
	workgroup_barrier();
	save(gs);
	wait_stores();
	end_wave();
}

struct header {
	u32 version, stop, pause, pass_wake;
};

/* The control block's first 16 bytes, in one read: it is in host memory. */
static __always_inline struct header read_header(u32 gs)
{
	u32x4 h = load_coherent_x4(&ctl(gs)->version);

	_Static_assert(__builtin_offsetof(struct knod_persistent_control,
					  pass_wake) == 12, "header");
	return (struct header){
		.version = uniform(h[0]),
		.stop = uniform(h[1]),
		.pause = uniform(h[2]),
		.pass_wake = uniform(h[3]),
	};
}

/*
 * Wave 0: park while the host changes maps, or leave if it wants the shader
 * gone; the other waves wait at the next barrier either way.
 */
static __always_inline struct header park_or_stop(u32 gs, struct header h)
{
	if (h.version != KNOD_PERSIST_VERSION || h.stop)
		stop(gs);
	if (!h.pause)
		return h;

	/* Where the rings got to, then that this queue is parked: the host may
	 * read anything once it sees the ack.
	 */
	save(gs);
	wait_stores();
	if (lane0())
		store_out(&entry(gs)->pause_ack, h.pause);
	wait_stores();
	do {
		sleep(8);
		h = read_header(gs);
		if (h.version != KNOD_PERSIST_VERSION || h.stop)
			stop(gs);
	} while (h.pause);
	return h;
}

/* ---- the rings ---- */

/*
 * Take what the NIC has sent off the TX CQ: sq_cc and tx_ci move.  One CQE
 * per round that sent, on its last WQE, so the run of new ones ends on a round
 * boundary.
 */
static __always_inline u32 tx_reap(u32 gs)
{
	u32 log = get(gs, GL_TX_CQ_LOG);
	u32 tx_ci = get(gs, GL_TX_CI);
	u32 sq_pc = get(gs, GL_SQ_PC);
	u32 sq_cc;
	u32 pos = tx_ci + lane_id();
	u32 dw60 = load_coherent(RING(gs, u32, KNOD_PERSIST_RING_TX_CQ_OFF +
					   ((pos & ((1u << log) - 1)) << 6) + 60));
	u32 opcode = dw60 >> 28;
	bool ok = ((pos >> log) & 1) == ((dw60 >> 24) & 1) &&
		  opcode != MLX5_CQE_INVALID;
	u32 n = run_from_0(ballot(ok));

	if (!n)
		return gs;
	tx_ci += n;

	if (ballot(opcode == MLX5_CQE_REQ_ERR) & lanes_below(n)) {
		/* The SQ has stopped for good and flushes the rest, a CQE each,
		 * off the rounds' boundaries.  None of it is sent, so nothing is
		 * held for it; and nothing more goes on it.
		 */
		sq_cc = sq_pc;
		gs = set(gs, GL_TX_DEAD, 1);
	} else {
		/* Everything up to and including the last one's WQE is sent:
		 * widen its 16-bit counter to the free-running producer, which
		 * is never more than an SQ ahead.
		 */
		u32 done = be16((u16)lane_value(dw60, n - 1)) + 1;

		sq_cc = sq_pc - ((sq_pc - done) & 0xffff);
	}
	if (lane0())
		store_out(RING(gs, u32, KNOD_PERSIST_RING_TX_CQ_DB),
			  be32(tx_ci & 0xffffff));
	gs = set(gs, GL_SQ_CC, sq_cc);
	return set(gs, GL_TX_CI, tx_ci);
}

/* Debug: the bound only ever moves on; count one that goes back. */
static __always_inline void note_regress(u32 gs, u32 ci, u32 last, u32 bound,
					 u32 pass_cc, u32 cand)
{
	u32 __global *dbg = entry(gs)->regress_dbg;

	if (!lane0())
		return;
	store_out(&dbg[2], bound - last);
	__scoped_atomic_fetch_add(&dbg[0], 1, __ATOMIC_RELAXED,
				  __MEMORY_SCOPE_SYSTEM);
	store_out(&dbg[1], ci);
	store_out(&dbg[3], bound);
	store_out(&dbg[4], get(gs, GL_SQ_PC));
	store_out(&dbg[5], get(gs, GL_SQ_CC));
	store_out(&dbg[6], pass_cc);
	store_out(&dbg[7], cand);
}

/*
 * Hand the CQ entries back up to @ci, and the RQ entries up to the oldest one
 * whose packet is still waiting to be sent or copied out.
 */
static __always_inline u32 rq_release(u32 gs, u32 ci)
{
	u32 bound = ci;
	u32 sq_pc = get(gs, GL_SQ_PC), sq_cc = get(gs, GL_SQ_CC);
	u32 pass_pc = get(gs, GL_PASS_PC), pass_cc = get(gs, GL_PASS_CC);
	u32 cand = 0, last;

	/* The oldest packet still waiting to be sent. */
	if (sq_pc != sq_cc)
		bound = uniform(load_coherent(RING(gs, u32,
			KNOD_PERSIST_RING_RQPOS_OFF +
			(sq_cc & get(gs, GL_SQ_MASK)) * 4)));

	/* The oldest packet the host is still copying out, if that is older. */
	if (pass_pc != pass_cc) {
		pass_cc = later(uniform(load_coherent(&entry(gs)->pass_cc)),
				get(gs, GL_PASS_FLOOR));
		gs = set(gs, GL_PASS_CC, pass_cc);
		if (pass_pc != pass_cc) {
			cand = uniform(load_coherent(RING(gs, u32,
				KNOD_PERSIST_RING_PASS_RQPOS_OFF +
				(pass_cc & get(gs, GL_PASS_MASK)) * 4)));
			if (ci - cand > ci - bound)
				bound = cand;
		}
	}

	last = get(gs, GL_LAST_BOUND);
	gs = set(gs, GL_LAST_BOUND, bound);
	if ((s32)(bound - last) < 0)
		note_regress(gs, ci, last, bound, pass_cc, cand);

	if (lane0()) {
		store_out(RING(gs, u32, KNOD_PERSIST_RING_CQ_DB),
			  be32(ci & 0xffffff));
		/* The CQ's room is out before the RQ entries that fill it, as
		 * mlx5e_poll_rx_cq() orders them: a NIC that sees the entries
		 * first completes into a CQ it still thinks full, and overruns
		 * it.
		 */
		wait_stores();
		store_out(RING(gs, u32, KNOD_PERSIST_RING_RQ_DB),
			  be32((bound + get(gs, GL_RQ_SIZE)) & 0xffff));
	}
	return gs;
}

/* ================================================================
 * Start: once per shader.  Back to the glue with v[GS], or ends the wave.
 * ================================================================ */

#if defined(KNOD_GDA_WANT_START)
__entry void ENTRY(start)(void)
{
	u32 tid, wg_x, wg_y, kernarg_lo, kernarg_hi, dispatch_lo, dispatch_hi;
	u32 ret_lo, ret_hi;
	__asm__ volatile("" : "={v0}"(tid), "={v1}"(wg_x), "={v2}"(wg_y),
			      "={v3}"(kernarg_lo), "={v4}"(kernarg_hi),
			      "={v5}"(dispatch_lo), "={v6}"(dispatch_hi),
			      "={v7}"(ret_lo), "={v8}"(ret_hi));
	u64 ret = ret_lo | (u64)ret_hi << 32;
	u64 kernarg = kernarg_lo | (u64)kernarg_hi << 32;
	u64 dispatch = dispatch_lo | (u64)dispatch_hi << 32;
	u32 wave = uniform(tid) & 1023;
	ctl_t *c = (ctl_t *)uniform64(kernarg);
	u32 q = uniform(wg_y);
	entry_t *e = &c->gda[q];
	u64 tx_db = uniform64(load_coherent(&c->tx_db[q]));
	u32 gs = 0;
	u32 sq_mask, tx_gen, ci, posted_gen, gen, rq_size;
	u32 pass_cc, pass_floor;
	struct gda_lds __lds *meet;

	/* Not a queue whose rings the NIC gave us. */
	if (!uniform(load_coherent(&e->live)))
		end_wave();

	gs = set64(gs, GL_HW_KERNARG, (u64)c);
	gs = set64(gs, GL_HW_DISPATCH, uniform64(dispatch));
	gs = set(gs, GL_HW_WG_X, uniform(wg_x));
	gs = set(gs, GL_HW_WG_Y, q);
	gs = set(gs, GL_WAVE, wave);

	gs = set64(gs, GL_RING, uniform64(load_coherent(&e->ring)));
	gs = set64(gs, GL_RX_DMA, uniform64(load_coherent(&e->rx_dma)));
	gs = set64(gs, GL_PKTS, uniform64(load_coherent(&e->packets)));
	gs = set64(gs, GL_ROUNDS, uniform64(load_coherent(&e->rounds)));
	gs = set(gs, GL_RQ_LOG, uniform(load_coherent(&e->rq_log)));
	rq_size = 1u << get(gs, GL_RQ_LOG);
	gs = set(gs, GL_RQ_SIZE, rq_size);
	gs = set(gs, GL_CQ_LOG, uniform(load_coherent(&e->cq_log)));
	gs = set(gs, GL_HEADROOM, uniform(load_coherent(&e->headroom)));
	gs = set64(gs, GL_RX_BASE, uniform64(load_coherent(&e->rx_base)));
	gs = set64(gs, GL_TX_PKTS, uniform64(load_coherent(&e->tx_packets)));
	gs = set64(gs, GL_TX_FULL, uniform64(load_coherent(&e->tx_full)));
	gs = set(gs, GL_STAGGER, uniform(load_coherent(&e->stagger)));
	gs = set(gs, GL_STAGGER_MASK, uniform(load_coherent(&e->stagger_mask)));

	/* The SQ, if the NIC gave us that too and its doorbell is mapped. */
	gs = set64(gs, GL_SQ, uniform64(load_coherent(&e->sq)));
	sq_mask = uniform(load_coherent(&e->sq_mask));
	if (!get64(gs, GL_SQ) || !tx_db)
		sq_mask = 0;
	gs = set(gs, GL_SQ_MASK, sq_mask);
	gs = set64(gs, GL_DB, tx_db);
	gs = set(gs, GL_SQN, uniform(load_coherent(&e->sqn)));
	gs = set(gs, GL_TX_MKEY, uniform(load_coherent(&e->tx_mkey_be)));
	gs = set(gs, GL_TX_CQ_LOG, uniform(load_coherent(&e->tx_cq_log)));
	tx_gen = uniform(load_coherent(&e->tx_gen));
	gs = set(gs, GL_TX_GEN, tx_gen);
	/* Where the last shader left this build of the SQ, or its start. */
	if (tx_gen == uniform(load_coherent(&e->tx_posted_gen)) && sq_mask) {
		gs = set(gs, GL_SQ_PC, uniform(load_coherent(&e->sq_pc)));
		gs = set(gs, GL_SQ_CC, uniform(load_coherent(&e->sq_cc)));
		gs = set(gs, GL_TX_CI, uniform(load_coherent(&e->tx_ci)));
	}

	/* The PASS ring, and how far the host has got with it. */
	gs = set64(gs, GL_PASS_RING, uniform64(load_coherent(&e->pass_ring)));
	gs = set(gs, GL_PASS_MASK, uniform(load_coherent(&e->pass_mask)));
	gs = set(gs, GL_PASS_PC, uniform(load_coherent(&e->pass_pc)));
	pass_floor = uniform(load_coherent(&e->pass_floor));
	pass_cc = later(uniform(load_coherent(&e->pass_cc)), pass_floor);
	gs = set(gs, GL_PASS_FLOOR, pass_floor);
	gs = set(gs, GL_PASS_CC, pass_cc);

	/* The parameter block, the meeting place and how many waves meet
	 * there, from the control block's header.  The rest of the workgroup
	 * ends.
	 */
	gs = set(gs, GL_WAVES, uniform(load_coherent(&c->gda_waves)));
	if (wave / 64 >= get(gs, GL_WAVES))
		end_wave();
	gs = set64(gs, GL_PARAM, uniform64(load_coherent(&c->gda_param)));
	gs = set(gs, GL_LDS, uniform(load_coherent(&c->gda_lds)));
	gs = set(gs, GL_PAGE_SHIFT,
		 *(u32 __global *)(get64(gs, GL_PARAM) + KNOD_BLOB_PARAM_PAGE_SHIFT));

	/* Every wave has read the entry before wave 0 writes it: these are
	 * all in SGPRs, so loaded, before the barrier.
	 */
	ci = uniform(load_coherent(&e->ci));
	posted_gen = uniform(load_coherent(&e->posted_gen));
	gen = uniform(load_coherent(&e->gen));
	meet = lds(gs);
	/* LDS starts undefined: no stop until wave 0 says so. */
	if (wave == 0 && lane0())
		meet->cmd = 0;
	workgroup_barrier();

	if (posted_gen != gen) {
		/* A new build of the rings: start the CQ over, and wave 0
		 * posts every RQ entry, entry i to RX page i - a round only
		 * ever gives an entry back as it was.
		 */
		u32 frag = uniform(load_coherent(&e->frag));
		u32 key = uniform(load_coherent(&e->mkey_be));
		u32 stride_log = uniform(load_coherent(&e->rq_log_stride));
		u64 __global *rx_dma = (u64 __global *)get64(gs, GL_RX_DMA);

		ci = 0;
		/* What the host still copies out came in on the last build's
		 * RQ.
		 */
		gs = set(gs, GL_PASS_FLOOR, get(gs, GL_PASS_PC));
		gs = set(gs, GL_PASS_CC, get(gs, GL_PASS_PC));

		if (wave == 0) {
			for (u32 i0 = 0; i0 < rq_size; i0 += 64) {
				u32 i = i0 + lane_id();
				u32 off = get(gs, GL_HEADROOM) + get(gs, GL_STAGGER) *
					  (i & get(gs, GL_STAGGER_MASK));
				u64 addr = rx_dma[i] + off;
				u32x4 wqe = { be32(frag), key, be32((u32)(addr >> 32)),
					      be32((u32)addr) };

				store_out(RING(gs, u32x4, KNOD_PERSIST_RING_RQ_OFF +
					       (i << stride_log)), wqe);
			}
			wait_stores();
			/* Up for the NIC, and recorded as posted for this
			 * build.
			 */
			if (lane0()) {
				store_out(RING(gs, u32, KNOD_PERSIST_RING_RQ_DB),
					  be32(rq_size));
				store_out(&e->ci, 0u);
				store_out(&e->posted_gen, gen);
				store_out(&e->pass_floor, get(gs, GL_PASS_FLOOR));
			}
			wait_stores();
		}
	}
	back_to_glue1(ret, set(gs, GL_CI, ci));
}
#endif

/* ================================================================
 * Round: the rest of the prologue, every round.  Waits for packets and
 * returns what the glue hands the program.
 * ================================================================ */

#if defined(KNOD_GDA_WANT_ROUND)

/* Fold the waves' runs of CQEs into the round: one counts only if every wave
 * before it had a full one.
 */
static __always_inline u32 tally_runs(u32 gs, u32 *mine)
{
	u32x4 runs = *(u32x4 __lds *)lds(gs)->n;	/* one ds_read_b128 */
	u32 waves = get(gs, GL_WAVES), me = get(gs, GL_WAVE) / 64;
	u32 n = 0;
	bool full = true;

	*mine = 0;
	for (u32 w = 0; w < 4; w++) {
		u32 run = w < waves && full ? uniform(runs[w]) : 0;

		n += run;
		if (w == me)
			*mine = run;
		full = run == 64;
	}
	return n;
}

__entry void ENTRY(round)(void)
{
	u32 gs, ret_lo, ret_hi;
	__asm__ volatile("" : "={v0}"(gs), "={v1}"(ret_lo), "={v2}"(ret_hi));
	u64 ret = ret_lo | (u64)ret_hi << 32;
	u32 wave = get(gs, GL_WAVE), me = wave / 64;
	u32 ci = get(gs, GL_CI);
	u32 cq_log = get(gs, GL_CQ_LOG);
	struct gda_lds __lds *meet = lds(gs);
	u32 pos, bc, dw60, n, mine;

	for (;;) {
		/* Wave 0 reads the control block and parks or stops for all
		 * of them; the rest wait for it at the barrier.
		 */
		if (wave == 0) {
			struct header h = park_or_stop(gs, read_header(gs));

			gs = set(gs, GL_WAKE, h.pass_wake);
		}
		workgroup_barrier();
		lds_vmem_branch_fence();	/* finish's stores, if any left */
		if (uniform(meet->cmd))
			end_wave();

		/* Lane i of wave w reads CQE ci + 64w + i: its byte count, and
		 * its last dword - the RQ entry it completes, its opcode and
		 * its owner bit.  Ours when the owner bit is the pass round the
		 * ring the position is on and the opcode is not the invalid one
		 * a fresh ring holds.  This wave's share is the unbroken run
		 * from its head.
		 */
		pos = ci + wave + lane_id();
		{
			u32 __global *cqe = RING(gs, u32, KNOD_PERSIST_RING_CQ_OFF +
						 ((pos & ((1u << cq_log) - 1)) << 6));
			u32 op_own;
			u32 run;

			bc = load_coherent(&cqe[44 / 4]);
			dw60 = load_coherent(&cqe[60 / 4]);
			op_own = dw60 >> 24;
			run = run_from_0(ballot(((pos >> cq_log) & 1) ==
						(op_own & 1) &&
						(op_own >> 4) != MLX5_CQE_INVALID));
			if (lane0())
				meet->n[me] = run;
			/* Maps the host changed while we were parked, and
			 * anything else a program reads that someone else
			 * writes; once, for the workgroup, before any wave's
			 * program runs.
			 */
			if (wave == 0 && run)
				invalidate_caches();
		}
		workgroup_barrier();

		n = tally_runs(gs, &mine);
		gs = set(gs, GL_N, n);
		if (n)
			break;

		/* Nothing new.  What was sent still frees RQ entries, and
		 * nothing else would: with every entry held the NIC has
		 * nowhere to put a packet.
		 */
		if (wave == 0 &&
		    (get(gs, GL_PASS_PC) != get(gs, GL_PASS_CC) ||
		     get(gs, GL_SQ_PC) != get(gs, GL_SQ_CC))) {
			if (get(gs, GL_SQ_PC) != get(gs, GL_SQ_CC))
				gs = tx_reap(gs);
			gs = rq_release(gs, ci);
		}
		sleep(1);
	}

	/* This wave's share, which may be none; its program still runs, with
	 * no lanes, to meet the others at the epilogue's barriers.
	 */
	{
		u64 exec = lanes_below(mine);
		u32 wqe = be16((u16)dw60);	/* the RQ entry, so the page */
		u32 page_idx = wqe & (get(gs, GL_RQ_SIZE) - 1);
		u64 page_base = get64(gs, GL_RX_BASE) +
				((u64)page_idx << get(gs, GL_PAGE_SHIFT));
		u32 pkt_off = get(gs, GL_HEADROOM) + get(gs, GL_STAGGER) *
			      (page_idx & get(gs, GL_STAGGER_MASK));
		u64 data = page_base + pkt_off;
		u64 data_end = data + be32(bc);
		u32 tid = wave + lane_id();
		u32 idx = (get(gs, GL_HW_WG_Y) * get(gs, GL_WAVES) << 6) + tid;
		u64 ctx = get64(gs, GL_PARAM) + KNOD_BLOB_PARAM_SUB +
			  (u64)idx * KNOD_BLOB_SUB_SIZE;
		u64 bad;

		/* Debug: the CQE's RQ entry is the position's, or the NIC and
		 * the CQ consumer index disagree about where the RQ is.
		 */
		bad = ballot((pos & 0xffff) != wqe) & exec;
		if (bad) {
			u32 l = __builtin_ctzll(bad);
			u32 __global *dbg = entry(gs)->sync_dbg;

			if (lane0()) {
				__scoped_atomic_fetch_add(&dbg[0], 1,
					__ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM);
				store_out(&dbg[1], (lane_value(pos, l) & 0xffff) |
						   lane_value(wqe, l) << 16);
			}
		}

		back_to_glue_round(ret, gs, (u32)exec, (u32)(exec >> 32),
				   page_idx, pkt_off,
				   (u32)data, (u32)(data >> 32),
				   (u32)data_end, (u32)(data_end >> 32),
				   (u32)page_base, (u32)(page_base >> 32),
				   (u32)ctx, (u32)(ctx >> 32), idx, tid);
	}
}
#endif

/* ================================================================
 * Finish: the epilogue, every round.  Back to the glue with v[GS].
 * ================================================================ */

#if defined(KNOD_GDA_WANT_FINISH)
/* What a lane hands on: how far into its page the packet is, and how long. */
struct packet {
	u32 page_idx, off, len;
};

/* One send WQE: rank @r of this wave's sends, at @idx on the SQ. */
static __always_inline void post_send(u32 gs, struct packet p, u32 idx,
				      bool last)
{
	u8 __global *sq = (u8 __global *)get64(gs, GL_SQ) +
			  (idx & get(gs, GL_SQ_MASK)) * 64;
	u64 __global *rx_dma = (u64 __global *)get64(gs, GL_RX_DMA);
	u64 addr = rx_dma[p.page_idx] + p.off;
	/* ctrl: opmod_idx_opcode and qpn_ds, both big endian, then fm_ce_se:
	 * a completion for the round's last WQE only.
	 */
	u32x4 ctrl = { be32((idx & 0xffff) << 8 | MLX5_OPCODE_SEND),
		       be32(get(gs, GL_SQN) << 8 | MLX5_SEND_DS),
		       last ? MLX5_CTRL_CQ_UPDATE << 24 : 0, 0 };
	/* eth: nothing inlined, no offloads. */
	u32x4 eth = { 0, 0, 0, 0 };
	/* data: byte count, lkey, address.  Big endian, but for the lkey,
	 * which the NIC handed over already swapped.
	 */
	u32x4 dseg = { be32(p.len), get(gs, GL_TX_MKEY),
		       be32((u32)(addr >> 32)), be32((u32)addr) };

	store_out((u32x4 __global *)(sq + 0), ctrl);
	store_out((u32x4 __global *)(sq + 16), eth);
	store_out((u32x4 __global *)(sq + 32), dseg);
}

__entry void ENTRY(finish)(void)
{
	u32 verdict, page_idx, data_lo, data_hi, end_lo, end_hi, base_lo, base_hi;
	u32 live_lo, live_hi, gs, ret_lo, ret_hi;
	__asm__ volatile("" : "={v0}"(verdict), "={v1}"(page_idx),
			      "={v2}"(data_lo), "={v3}"(data_hi),
			      "={v4}"(end_lo), "={v5}"(end_hi),
			      "={v6}"(base_lo), "={v7}"(base_hi),
			      "={v8}"(live_lo), "={v9}"(live_hi),
			      "={v10}"(gs), "={v11}"(ret_lo), "={v12}"(ret_hi));
	u64 data = data_lo | (u64)data_hi << 32;
	u64 data_end = end_lo | (u64)end_hi << 32;
	u64 page_base = base_lo | (u64)base_hi << 32;
	u64 live = live_lo | (u64)live_hi << 32;
	u64 ret = ret_lo | (u64)ret_hi << 32;
	struct gda_lds __lds *meet = lds(gs);
	u32 wave = get(gs, GL_WAVE), me = wave / 64;
	u32 waves = get(gs, GL_WAVES);
	u32 sq_mask = get(gs, GL_SQ_MASK);
	u32 ci = get(gs, GL_CI);
	u32 rq_pos = ci + wave + lane_id();	/* the RQ position this lane's
						 * packet came in on */
	bool on = (uniform64(live) >> lane_id()) & 1;
	/* With no SQ an XDP_TX drops. */
	u64 sends = sq_mask ? ballot(on && verdict == XDP_TX) : 0;
	u64 passes = ballot(on && verdict == XDP_PASS);
	struct packet p = { page_idx, (u32)(data - page_base),
			    (u32)(data_end - data) };
	u32 sq_pc, left, prefix = 0, base = 0, my_sends = 0, asked = 0;
	u32 given, last_wave = 0, pass_pc, pass_base = 0, pass_total = 0;
	u32x4 sends_by, passes_by;

	/* What this wave sends and hands to the host, for the others to see.
	 * Wave 0 takes what the NIC has sent since the last look, and says
	 * where the SQ and the PASS ring stand.
	 */
	if (lane0()) {
		meet->k[me] = __builtin_popcountll(sends);
		meet->p[me] = __builtin_popcountll(passes);
	}
	if (wave == 0) {
		u32 pc, cc;

		if (sq_mask && get(gs, GL_SQ_PC) != get(gs, GL_SQ_CC))
			gs = tx_reap(gs);
		pc = get(gs, GL_SQ_PC);
		cc = get(gs, GL_SQ_CC);
		/* A dead SQ has room for none: the waves see it full. */
		if (get(gs, GL_TX_DEAD))
			cc = pc - sq_mask - 1;
		if (lane0()) {
			meet->sq_pc = pc;
			meet->sq_cc = cc;
			meet->pass_pc = get(gs, GL_PASS_PC);
		}
	}
	workgroup_barrier();

	/* The SQ's room goes to the waves in order, so the WQEs follow the
	 * packets' order on the RQ; so does the PASS ring.
	 */
	pass_pc = uniform(meet->pass_pc);
	sq_pc = uniform(meet->sq_pc);
	left = sq_mask - (sq_pc - uniform(meet->sq_cc)) + 1;
	sends_by = *(u32x4 __lds *)meet->k;	/* one ds_read_b128 each */
	passes_by = *(u32x4 __lds *)meet->p;
	for (u32 w = 0; w < 4; w++) {
		u32 k = w < waves ? uniform(sends_by[w]) : 0;
		u32 np = w < waves ? uniform(passes_by[w]) : 0;
		u32 got = k < left ? k : left;

		asked += k;
		left -= got;
		if (w == me) {
			base = prefix;
			my_sends = got;
		}
		if (got)
			last_wave = w;
		prefix += got;
		pass_total += np;
		if (w < me)
			pass_base += np;
	}
	given = prefix;

	/* One send WQE per lane that goes, at sq_pc + base + its rank; lanes
	 * past the room the wave got drop.  Each WQE says which RQ position
	 * its packet came in on, so that whatever WQE the NIC's completions
	 * stop at, the RQ is held from the right place.
	 */
	if (my_sends && (sends >> lane_id() & 1)) {
		u32 r = rank(sends);

		if (r < my_sends) {
			u32 idx = sq_pc + base + r;

			post_send(gs, p, idx,
				  me == last_wave && r == my_sends - 1);
			store_out(RING(gs, u32, KNOD_PERSIST_RING_RQPOS_OFF +
				       (idx & sq_mask) * 4), rq_pos);
		}
	}

	/* One PASS entry per lane that passes, at pass_pc + base + its rank:
	 * the page, where the packet is in it and how long; and in the ring
	 * buffer, the RQ position it holds until the host is done.
	 */
	if (passes >> lane_id() & 1) {
		u32 slot = (pass_pc + pass_base + rank(passes)) &
			   get(gs, GL_PASS_MASK);
		u32x2 __global *pe = (u32x2 __global *)get64(gs, GL_PASS_RING) +
				     slot;

		store_out(pe, ((u32x2){ p.page_idx, p.off | p.len << 16 }));
		store_out(RING(gs, u32, KNOD_PERSIST_RING_PASS_RQPOS_OFF +
			       slot * 4), rq_pos);
	}

	/* Every wave's WQEs and PASS entries are in memory before wave 0 says
	 * they are there.
	 */
	wait_stores();
	workgroup_barrier();

	if (wave == 0) {
		if (pass_total) {
			pass_pc += pass_total;
			gs = set(gs, GL_PASS_PC, pass_pc);
			if (lane0())
				store_out(&entry(gs)->pass_pc, pass_pc);
			/* The host sleeps until PASS entries come: take the
			 * request, so the other rounds leave it be, and
			 * interrupt once they are in memory.
			 */
			if (get(gs, GL_WAKE)) {
				gs = set(gs, GL_WAKE, 0);
				if (lane0())
					store_out(&ctl(gs)->pass_wake, 0u);
				wait_stores();
				interrupt_host(KNOD_PERSIST_PASS_IRQ);
			}
		}

		gs = set64(gs, GL_TX_FULL, get64(gs, GL_TX_FULL) + (asked - given));
		if (given) {
			u32 pc = sq_pc + given;
			u32 lo = be32(((pc - 1) & 0xffff) << 8 | MLX5_OPCODE_SEND);
			u32 hi = be32(get(gs, GL_SQN) << 8 | MLX5_SEND_DS);

			gs = set64(gs, GL_TX_PKTS, get64(gs, GL_TX_PKTS) + given);
			gs = set(gs, GL_SQ_PC, pc);
			/* The record, then the doorbell: the last WQE's first
			 * eight bytes, which are all a function of its counter.
			 */
			if (lane0())
				store_out(RING(gs, u32, KNOD_PERSIST_RING_SQ_DB),
					  be32(pc & 0xffff));
			wait_stores();
			if (lane0())
				store_out((u64 __global *)get64(gs, GL_DB),
					  (u64)hi << 32 | lo);
		}
	}

	/* Every wave moves its consumer index; wave 0 gives the CQ and RQ
	 * entries back - every verdict but a send or a pass as a drop does.
	 */
	ci += get(gs, GL_N);
	gs = set(gs, GL_CI, ci);
	if (wave == 0) {
		gs = rq_release(gs, ci);
		gs = set64(gs, GL_PKTS, get64(gs, GL_PKTS) + get(gs, GL_N));
		gs = set64(gs, GL_ROUNDS, get64(gs, GL_ROUNDS) + 1);
		save(gs);
	}
	back_to_glue1(ret, gs);
}
#endif
