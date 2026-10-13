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

/* What a lane's CQE told nic_rx_poll(), for nic_rx_read(). */
struct nic_rx {
	GLOBAL struct knod_mlx5_cqe64 *c;
	uint32_t tail;
};

/* Lane i's completion, of the one at @pos: whether the NIC is done with it.
 * Only its last dword: the WQE counter, the owner bit and the opcode.
 */
static INLINE int nic_rx_poll(struct gda *g, uint32_t pos, struct nic_rx *rx)
{
	uint32_t log = GL(g, CQ_LOG);

	rx->c = cqe_at(g->ring + KNOD_PERSIST_RING_CQ_OFF, pos, log);
	rx->tail = cqe_tail(rx->c);
	return cqe_ours(rx->tail, pos, log);
}

/* The rest of it, once ours: and the lanes whose CQE completes another RQ
 * entry than @pos is for.  The RQ entry is the page: entry i is page i.
 */
static INLINE uint64_t nic_rx_read(struct gda *g, struct nic_rx *rx,
				   uint32_t pos, int ordered, uint32_t *len,
				   uint32_t *rss, uint32_t *page, uint32_t *kind)
{
	*kind = rx->tail >> CQE_OP_OWN_SHIFT;
	*len = bswap32(get32(&rx->c->byte_cnt));
	if (ordered)
		*rss = get32(&rx->c->rss_hash_result);
	*page = wqe_counter(rx->tail);
	return ballot((pos & 0xffff) != *page);
}

/* Lane 0's: the CQ entries back up to @ci and the RQ ones up to @bound. */
static INLINE void nic_rx_release(struct gda *g, uint32_t ci, uint32_t bound)
{
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
