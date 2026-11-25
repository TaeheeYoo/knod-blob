/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The GDA engine's mlx5 part (KNOD_PERSIST_FMT_MLX5_CX5): a plain cyclic RQ
 * and its CQ, and a send queue, each with a doorbell record in the ring
 * buffer; only the SQ rings at the NIC.  The hooks gda_engine.h calls.
 */
#ifndef KNOD_BLOB_NIC_MLX5_H
#define KNOD_BLOB_NIC_MLX5_H

#include "gda.h"
#include "mlx5.h"

/* op_own's place in a CQE's last dword, fm_ce_se's in a ctrl segment's
 * third, and a send WQE's size in the 16-byte units the NIC counts.
 */
#define CQE_OP_OWN_SHIFT	(8 * (offsetof(struct knod_mlx5_cqe64, op_own) - \
				      offsetof(struct knod_mlx5_cqe64, wqe_counter)))
#define CTRL_FM_CE_SE_SHIFT	(8 * (offsetof(struct knod_mlx5_wqe_ctrl_seg, \
					       fm_ce_se) % 4))
#define SEND_DS			(sizeof(struct knod_mlx5_send_wqe) / 16)

/* A full SQ is one the NIC tells from an empty one. */
#define NIC_SQ_SPARE	0

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

static INLINE void nic_load(struct gda *g, entry_t e)
{
	SET(g, CQE_COMP, get32(&e->cqe_comp));
	SET(g, SQN, get32(&e->sqn));
	SET(g, TX_MKEY, get32(&e->tx_mkey_be));
}

/* Post every RQ entry, entry i to RX page i, and say so. */
static INLINE void nic_rx_post(struct gda *g)
{
	entry_t e = g->entry;
	uint32_t size = GL(g, RQ_SIZE), stride_log = get32(&e->rq_log_stride);
	uint32_t frag = bswap32(get32(&e->frag)), mkey = get32(&e->mkey_be);
	uint32_t i;

	for (i = lane_id(); i < size; i += 64) {
		uint64_t a = gda_page_dma(g, i) + gda_page_off(g, i);

		put128(g->ring + KNOD_PERSIST_RING_RQ_OFF + (i << stride_log),
		       (v4u){ frag, mkey, bswap32(a >> 32), bswap32(a) });
	}
	stores_out();
	if (!lane_id())
		put32(g->ring + KNOD_PERSIST_RING_RQ_DB, bswap32(size));
}

/* A compressed CQ's entries: one the NIC wrote on the pass round the ring
 * @pos is on; one holding a session, and its mini CQEs.
 */
#define CQE_VALIDITY_SHIFT	(8 * (offsetof(struct knod_mlx5_cqe64, signature) - \
				      offsetof(struct knod_mlx5_cqe64, wqe_counter)))

static inline int cqe_valid(uint32_t tail, uint32_t pos, uint32_t log)
{
	return ((tail >> CQE_VALIDITY_SHIFT) & 0xff) == ((pos >> log) & 0xff);
}

static inline int cqe_session(uint32_t tail)
{
	return ((tail >> (CQE_OP_OWN_SHIFT + KNOD_MLX5_CQE_FORMAT_SHIFT)) &
		KNOD_MLX5_CQE_FORMAT_MASK) == KNOD_MLX5_CQE_COMPRESSED;
}

static inline uint32_t cqe_minis(uint32_t tail)
{
	return (tail >> (CQE_OP_OWN_SHIFT + KNOD_MLX5_CQE_OPCODE_SHIFT)) + 1;
}

/* A full CQE of a compressed CQ: one written this pass, not a session, and
 * not the invalid one a fresh ring holds.
 */
static inline int cqe_full(uint32_t tail, uint32_t pos, uint32_t log)
{
	return cqe_valid(tail, pos, log) && !cqe_session(tail) &&
	       (tail >> (CQE_OP_OWN_SHIFT + KNOD_MLX5_CQE_OPCODE_SHIFT)) !=
	       KNOD_MLX5_CQE_INVALID;
}

/* KNOD_PERSIST_PASS_CSUM_OK if the NIC found a full CQE's IP and L4
 * checksums good.
 */
static inline uint32_t cqe_csum(GLOBAL struct knod_mlx5_cqe64 *c)
{
	uint32_t ok = KNOD_MLX5_CQE_L3_OK | KNOD_MLX5_CQE_L4_OK;

	_Static_assert(offsetof(struct knod_mlx5_cqe64, hds_ip_ext) % 4 == 0,
		       "the low byte of a dword");
	return (get32(&c->hds_ip_ext) & ok) == ok ? KNOD_PERSIST_PASS_CSUM_OK : 0;
}

/* What a lane's CQE told nic_rx_poll(), for nic_rx_read(): the entry and its
 * last dword - for a packet in a session the session's, the packet @d into
 * it - and whether it is a full CQE.
 */
struct nic_rx {
	GLOBAL struct knod_mlx5_cqe64 *c;
	uint32_t tail;
	uint32_t d;
	int full;
};

/* Lane i's completion, of the one at @pos: whether the NIC is done with it.
 * Only the entries' last dwords: the WQE counter, the owner bit or validity
 * byte, and op_own.  On a compressed CQ an entry the NIC skipped is done when
 * the session before it covers it: one a lane below has, or for a wave's
 * first lanes one in the wave before.  A round starts with a whole entry.
 */
static INLINE int nic_rx_poll(struct gda *g, uint32_t pos, struct nic_rx *rx)
{
	GLOBAL uint8_t *cq = g->ring + KNOD_PERSIST_RING_CQ_OFF;
	uint32_t log = GL(g, CQ_LOG), j, d, tail;
	uint64_t below;
	int v;

	rx->c = cqe_at(cq, pos, log);
	rx->tail = cqe_tail(rx->c);
	rx->d = 0;
	if (!GL(g, CQE_COMP)) {
		rx->full = cqe_ours(rx->tail, pos, log);
		return rx->full;
	}
	rx->full = cqe_full(rx->tail, pos, log);
	v = rx->full || (cqe_valid(rx->tail, pos, log) && cqe_session(rx->tail));
	below = ballot(v) & lanes_below(lane_id());
	j = below ? 63 - __builtin_clzll(below) : lane_id();
	tail = bpermute(j, rx->tail);
	if (v)
		return 1;
	d = lane_id() - j;
	if (!below) {
		for (d = lane_id() + 1; d < KNOD_MLX5_MINI_CQES_MAX; d++) {
			tail = cqe_tail(cqe_at(cq, pos - d, log));
			if (cqe_valid(tail, pos - d, log))
				break;
		}
		if (d == KNOD_MLX5_MINI_CQES_MAX)
			return 0;
	}
	if (!cqe_session(tail) || d >= cqe_minis(tail))
		return 0;
	rx->c = cqe_at(cq, pos - d, log);
	rx->tail = tail;
	rx->d = d;
	return 1;
}

/* The run the wave takes: a round that would end inside a session ends
 * before it, so the next starts with a whole entry.  And the wave's last full
 * CQE's checksums, which the sessions after it share.
 */
static INLINE uint32_t nic_rx_polled(struct gda *g, uint32_t run,
				     struct nic_rx *rx)
{
	uint32_t tail, d, t = 0;
	uint64_t f;

	if (!GL(g, CQE_COMP))
		return run;
	if (run == 64 && g->wave == GL(g, WAVES) - 1) {
		tail = __builtin_amdgcn_readlane(rx->tail, 63);
		d = __builtin_amdgcn_readlane(rx->d, 63);
		if (!__builtin_amdgcn_readlane(rx->full, 63) &&
		    d + 1 < cqe_minis(tail))
			run -= d + 1;
	}
	f = ballot(rx->full) & lanes_below(run);
	if (f)
		t = 0x100 | cqe_csum(cqe_at(g->ring + KNOD_PERSIST_RING_CQ_OFF,
					    GL(g, CI) + 64 * g->wave + 63 -
					    __builtin_clzll(f), GL(g, CQ_LOG)));
	if (!lane_id())
		g->meet->t[g->wave] = t;
	return run;
}

/* The rest of it, once ours: and the lanes whose CQE completes another RQ
 * entry than @pos is for.  The RQ entry is the page: entry i is page i,
 * which a mini CQE leaves to its place.
 */
static INLINE uint64_t nic_rx_read(struct gda *g, struct nic_rx *rx,
				   uint32_t pos, int ordered, uint32_t *len,
				   uint32_t *rss, uint32_t *page, uint32_t *kind)
{
	GLOBAL struct knod_mlx5_mini_cqe8 *m;

	*kind = rx->tail >> CQE_OP_OWN_SHIFT;
	if (!rx->full) {
		m = (GLOBAL struct knod_mlx5_mini_cqe8 *)rx->c + rx->d;
		*len = bswap32(get32(&m->byte_cnt));
		if (ordered)
			*rss = get32(&m->rx_hash_result);
		*page = pos & 0xffff;
	} else {
		*len = bswap32(get32(&rx->c->byte_cnt));
		if (ordered)
			*rss = get32(&rx->c->rss_hash_result);
		*page = wqe_counter(rx->tail);
	}
	return ballot((pos & 0xffff) != *page);
}

/* The full CQE last before wave @w's, this round's or a round before's: of
 * the waves in the round, those before the first short one.
 */
static INLINE uint32_t mlx5_title(struct gda *g, uint32_t w)
{
	uint32_t t = GL(g, TITLE), n = GL(g, N), i;

	for (i = 0; i < w && 64 * i < n; i++)
		if (g->meet->t[i])
			t = g->meet->t[i];
	return t;
}

/* Completion @pos's checksums, every lane of the wave at once:
 * KNOD_PERSIST_PASS_CSUM_OK if the NIC found the IP and L4 ones good.  A
 * packet in a session has those of the full CQE before it: the NIC
 * compresses only CQEs that match it.
 */
static INLINE uint32_t nic_rx_csum(struct gda *g, uint32_t pos)
{
	uint32_t log = GL(g, CQ_LOG), own, j;
	GLOBAL struct knod_mlx5_cqe64 *c =
		cqe_at(g->ring + KNOD_PERSIST_RING_CQ_OFF, pos, log);
	uint64_t below;
	int full;

	own = cqe_csum(c);
	if (!GL(g, CQE_COMP))
		return own;
	full = cqe_full(cqe_tail(c), pos, log);
	below = ballot(full) & lanes_below(lane_id());
	j = bpermute(below ? 63 - __builtin_clzll(below) : lane_id(), own);
	if (full)
		return own;
	return below ? j : mlx5_title(g, g->wave) & 0xff;
}

/* Each wave's, the round's checksums read and before wave 0 hands its CQ
 * entries back: they are left as of this pass round the ring, so none the
 * NIC skips next pass still says it is on that one; and the full CQE last
 * of the round is the next one's title.
 */
static INLINE void nic_rx_retire(struct gda *g, uint32_t pos)
{
	uint32_t log = GL(g, CQ_LOG);

	if (!GL(g, CQE_COMP))
		return;
	if (64 * g->wave + lane_id() < GL(g, N))
		put32(&cqe_at(g->ring + KNOD_PERSIST_RING_CQ_OFF, pos,
			      log)->wqe_counter,
		      KNOD_MLX5_CQE_INVALID << (CQE_OP_OWN_SHIFT +
						KNOD_MLX5_CQE_OPCODE_SHIFT) |
		      ((pos >> log) & 0xff) << CQE_VALIDITY_SHIFT);
	SET(g, TITLE, mlx5_title(g, KNOD_PERSIST_GDA_WAVES_MAX));
}

/* Wave 0's: the CQ entries back up to @ci and the RQ ones up to @bound. */
static INLINE void nic_rx_release(struct gda *g, uint32_t ci, uint32_t bound)
{
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

/*
 * Take what the NIC has sent off the TX CQ: SQ_CC and TX_CI move.  One CQE
 * per round that sent, on its last WQE, so the run of new ones ends on a
 * round boundary.  @pc is the producer.
 */
static INLINE void nic_tx_reap(struct gda *g, uint32_t pc)
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

/* One send WQE: the packet's place in the RX buffer, nothing inlined. */
static INLINE void nic_tx_post(struct gda *g, uint32_t counter, uint32_t page,
			       uint32_t off, uint32_t len, int signal)
{
	GLOBAL uint8_t *wqe = (GLOBAL uint8_t *)GL64(g, SQ) +
		((counter & GL(g, SQ_MASK)) << KNOD_MLX5_WQEBB_SHIFT);
	uint64_t addr = gda_page_dma(g, page) + off;
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

/* Wave 0's: the SQ up to @pc, every WQE already out.  The record, then the
 * doorbell: the last WQE's first eight bytes, which are all a function of its
 * counter.
 */
static INLINE void nic_tx_ring(struct gda *g, uint32_t pc)
{
	if (!lane_id())
		put32(g->ring + KNOD_PERSIST_RING_SQ_DB, bswap32(pc & 0xffff));
	stores_out();
	if (!lane_id())
		put64((GLOBAL void *)GL64(g, DB),
		      (uint64_t)bswap32(GL(g, SQN) << 8 | SEND_DS) << 32 |
		      bswap32(((pc - 1) & 0xffff) << 8 |
			      KNOD_MLX5_OPCODE_SEND));
}

#endif /* KNOD_BLOB_NIC_MLX5_H */
