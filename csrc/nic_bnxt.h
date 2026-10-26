/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The GDA engine's bnxt part (KNOD_PERSIST_FMT_BNXT_P5, _P7): an RX ring and
 * its CQ, a TX ring and its CQ, and no doorbell records - every ring rings at
 * the one doorbell register.  The hooks gda_engine.h calls.  BNXT_GEN says
 * which: P7 puts the ring's pass in a doorbell's index, completes RX in its
 * V3 format and can complete TX coalesced.
 *
 * bnxt reads a full ring as an empty one, so the RQ has more entries than are
 * ever posted: every one is written once, entry i with page i modulo the
 * posted count, and after that only the producer moves.
 */
#ifndef KNOD_BLOB_NIC_BNXT_H
#define KNOD_BLOB_NIC_BNXT_H

#include "gda.h"
#include "bnxt.h"

#if BNXT_GEN != 5 && BNXT_GEN != 7
# error "BNXT_GEN is 5 or 7"
#endif

/* A full SQ is one bnxt reads as empty. */
#define NIC_SQ_SPARE	1

#define KEY(g, k)	__builtin_amdgcn_readlane((g)->s, GL_KEY + (k))
#define SET_KEY(g, e, k)						\
	((g)->s = writelane((g)->s, get64(&(e)->db_key[k]) >> 32, GL_KEY + (k)))

/* bnxt: a completion entry is ours when its valid bit is set on the ring's
 * even passes and clear on its odd ones; @pos counts entries.
 */
static inline int bnxt_ours(uint32_t v, uint32_t pos, uint32_t log)
{
	return (v & KNOD_BNXT_CMP_V) != ((pos >> log) & 1);
}

static inline GLOBAL uint8_t *bnxt_cmp_at(GLOBAL uint8_t *cq, uint32_t pos,
					  uint32_t log)
{
	return cq + ((pos & ((1u << log) - 1)) << KNOD_BNXT_CMP_SHIFT);
}

/* bnxt: every ring of the function rings at the one doorbell, which ring in
 * the key's high half and its index in the low.  Lane 0's.
 */
static INLINE void bnxt_ring(struct gda *g, uint32_t key, uint32_t idx)
{
	put64((GLOBAL void *)GL64(g, DB), (uint64_t)KEY(g, key) << 32 | idx);
}

/* A free-running @at as a doorbell index, for a ring of @mask + 1 entries:
 * where in the ring, and on P7 which pass of it, the epoch.
 */
static inline uint32_t bnxt_db_idx(uint32_t at, uint32_t mask)
{
	return (at & mask) |
	       (BNXT_GEN == 7 && (at & (mask + 1)) ? KNOD_BNXT_DB_EPOCH : 0);
}

static INLINE void nic_load(struct gda *g, entry_t e)
{
	SET(g, RQ_RING_MASK, (1u << get32(&e->rq_ring_log)) - 1);
	SET_KEY(g, e, KNOD_PERSIST_DB_RQ);
	SET_KEY(g, e, KNOD_PERSIST_DB_RX_CQ);
	SET_KEY(g, e, KNOD_PERSIST_DB_SQ);
	SET_KEY(g, e, KNOD_PERSIST_DB_TX_CQ);
}

/* Every RQ entry, each with the page it always holds; the first RQ_SIZE of
 * them posted.
 */
static INLINE void nic_rx_post(struct gda *g)
{
	uint32_t size = GL(g, RQ_SIZE), frag = get32(&g->entry->frag);
	uint32_t i, p;

	for (i = lane_id(); i <= GL(g, RQ_RING_MASK); i += 64) {
		uint64_t a;

		p = i & (size - 1);
		a = gda_page_dma(g, p) + gda_page_off(g, p);
		put128(g->ring + KNOD_PERSIST_RING_RQ_OFF +
		       (i << KNOD_BNXT_BD_SHIFT),
		       (v4u){ frag << KNOD_BNXT_LEN_SHIFT |
			      KNOD_BNXT_RX_BD_PACKET | KNOD_BNXT_RX_BD_EOP,
			      p, a, a >> 32 });
	}
	stores_out();
	if (!lane_id())
		bnxt_ring(g, KNOD_PERSIST_DB_RQ,
			  bnxt_db_idx(size, GL(g, RQ_RING_MASK)));
}

struct nic_rx {
	GLOBAL struct knod_bnxt_rx_cmp *b;
};

/* Completion @pos: both its halves, entries 2 @pos and the next, each with a
 * valid bit.
 */
static INLINE int nic_rx_poll(struct gda *g, uint32_t pos, struct nic_rx *rx)
{
	uint32_t log = GL(g, CQ_LOG), w;

	rx->b = (GLOBAL struct knod_bnxt_rx_cmp *)
		bnxt_cmp_at(g->ring + KNOD_PERSIST_RING_CQ_OFF, 2 * pos, log);
	w = get32(&((GLOBAL struct knod_bnxt_rx_cmp_ext *)(rx->b + 1))->errors_v2);
	return bnxt_ours(get32(&rx->b->misc_v1), 2 * pos, log) &&
	       bnxt_ours(w, 2 * pos + 1, log);
}

/* The RX descriptor's opaque is its page.  Anything but a plain RX
 * completion, or another page than @pos is for, is the rings out of step.
 */
static INLINE uint64_t nic_rx_read(struct gda *g, struct nic_rx *rx,
				   uint32_t pos, int ordered, uint32_t *len,
				   uint32_t *rss, uint32_t *page, uint32_t *kind)
{
	/* The first half whole, in one request, past the caches. */
	v4u c = *(volatile GLOBAL v4u *)rx->b;
	uint32_t w = c.x;

	_Static_assert(offsetof(struct knod_bnxt_rx_cmp, opaque) == 4 &&
		       offsetof(struct knod_bnxt_rx_cmp, rss_hash) == 12,
		       "the first half's dwords");
	*kind = w & KNOD_BNXT_CMP_TYPE;
	*len = w >> KNOD_BNXT_LEN_SHIFT;
	if (ordered)
		*rss = c.w;
	*page = c.y;
	return ballot((pos & (GL(g, RQ_SIZE) - 1)) != *page ||
		      (w & KNOD_BNXT_CMP_TYPE) !=
		      (BNXT_GEN == 7 ? KNOD_BNXT_CMP_TYPE_RX_L2_V3 :
				       KNOD_BNXT_CMP_TYPE_RX_L2));
}

/* Completion @pos's checksums, out of its second half: whether the NIC
 * checked the L4 one, the inner one under a tunnel too, and found it good.
 */
static INLINE uint32_t nic_rx_csum(struct gda *g, uint32_t pos)
{
	GLOBAL struct knod_bnxt_rx_cmp_ext *x = (GLOBAL struct knod_bnxt_rx_cmp_ext *)
		bnxt_cmp_at(g->ring + KNOD_PERSIST_RING_CQ_OFF, 2 * pos + 1,
			    GL(g, CQ_LOG));
	uint32_t flags = get32(&x->flags2), errors = get32(&x->errors_v2);

	if (!(flags & (KNOD_BNXT_RX_L4_CS_CALC | KNOD_BNXT_RX_T_L4_CS_CALC)) ||
	    (errors & (KNOD_BNXT_RX_L4_CS_ERROR | KNOD_BNXT_RX_T_L4_CS_ERROR)))
		return 0;
	return KNOD_PERSIST_PASS_CSUM_OK |
	       (flags & KNOD_BNXT_RX_T_L4_CS_CALC ? KNOD_PERSIST_PASS_CSUM_INNER : 0);
}

/*
 * How far behind the CQ's consumer the NIC may be told it is.  The NIC drops
 * nothing for a CQ it thinks full, it overruns it, so what it has completed
 * past the consumer it was told must fit: at most @held more than the real
 * one, the most it can complete before the shader takes them, and the told
 * one up to this many behind that.  Half the room, for slack.
 */
static INLINE uint32_t bnxt_cq_slack(uint32_t entries, uint32_t held)
{
	return entries > held ? (entries - held) / 2 : 0;
}

/*
 * Wave 0's: the CQ entries back up to @ci - two entries a completion - and the
 * RQ ones up to @bound, each only once the NIC's idea of it is far enough
 * behind to matter: for the RQ, a quarter of what is posted, which leaves the
 * NIC three quarters of it ahead.  Both go to the one register in the order
 * written, so nothing waits between them.
 */
static INLINE void nic_rx_release(struct gda *g, uint32_t ci, uint32_t bound)
{
	uint32_t log = GL(g, CQ_LOG), size = GL(g, RQ_SIZE);
	int cq, rq;

	cq = ci - GL(g, CQ_TOLD) >= bnxt_cq_slack(1u << (log - 1), size);
	rq = bound - GL(g, RQ_TOLD) >= size / 4;
	if (cq)
		SET(g, CQ_TOLD, ci);
	if (rq)
		SET(g, RQ_TOLD, bound);
	if (lane_id())
		return;
	if (cq)
		bnxt_ring(g, KNOD_PERSIST_DB_RX_CQ,
			  bnxt_db_idx(2 * ci, (1u << log) - 1));
	if (rq)
		bnxt_ring(g, KNOD_PERSIST_DB_RQ,
			  bnxt_db_idx(bound + size, GL(g, RQ_RING_MASK)));
}

/* What the NIC has sent, off the TX CQ: SQ_CC and TX_CI move.  One completion
 * a round, on its last descriptor.
 */
static INLINE void nic_tx_reap(struct gda *g, uint32_t pc)
{
	uint32_t ci = GL(g, TX_CI), log = GL(g, TX_CQ_LOG);
	uint32_t pos = ci + lane_id(), n, done;
	GLOBAL struct knod_bnxt_tx_cmp *c;

	c = (GLOBAL struct knod_bnxt_tx_cmp *)
		bnxt_cmp_at(g->ring + KNOD_PERSIST_RING_TX_CQ_OFF, pos, log);
	n = run_of(ballot(bnxt_ours(get32(&c->errors_v), pos, log)));
	if (!n)
		return;
	/* Each one's descriptor carries its whole counter: everything up to
	 * the last one's is sent.  A coalesced one says where the NIC's
	 * consumer is instead: widened to the producer, never an SQ ahead.
	 */
	ORDER();
	done = __builtin_amdgcn_readlane(get32(&c->opaque), n - 1) + 1;
	if (BNXT_GEN == 7) {
		uint32_t last = __builtin_amdgcn_readlane(get32(&c->flags_type),
							  n - 1);

		if ((last & KNOD_BNXT_CMP_TYPE) == KNOD_BNXT_CMP_TYPE_TX_COAL)
			done = pc - ((pc - __builtin_amdgcn_readlane(
					get32(&c->sq_cons_idx), n - 1)) &
				     GL(g, SQ_MASK));
	}
	ci += n;
	SET(g, SQ_CC, done);
	SET(g, TX_CI, ci);
	/* As the RX CQ's: a completion a round, at most an SQ of them. */
	if (ci - GL(g, TX_CQ_TOLD) <
	    bnxt_cq_slack(1u << log, GL(g, SQ_MASK) + 1))
		return;
	SET(g, TX_CQ_TOLD, ci);
	if (!lane_id())
		bnxt_ring(g, KNOD_PERSIST_DB_TX_CQ,
			  bnxt_db_idx(ci, (1u << log) - 1));
}

/* bnxt's: one descriptor a packet, its counter in the opaque that comes back
 * in the completion, and only the round's last asking for one.
 */
static INLINE void nic_tx_post(struct gda *g, uint32_t counter, uint32_t page,
			       uint32_t off, uint32_t len, int signal)
{
	GLOBAL uint8_t *bd = (GLOBAL uint8_t *)GL64(g, SQ) +
		((counter & GL(g, SQ_MASK)) << KNOD_BNXT_BD_SHIFT);
	uint64_t addr = gda_page_dma(g, page) + off;
	uint32_t hint = len >> 9;

	/* What bnxt_lhint_arr[] has: up to 512, 1023, 2047 and the rest. */
	hint = hint >= 4 ? 3 : hint >= 2 ? 2 : hint;
	put128(bd, (v4u){ len << KNOD_BNXT_LEN_SHIFT |
			  1 << KNOD_BNXT_TX_BD_CNT_SHIFT |
			  hint << KNOD_BNXT_TX_BD_LHINT_SHIFT |
			  KNOD_BNXT_TX_BD_PACKET_END |
			  (signal ? 0 : KNOD_BNXT_TX_BD_NO_CMPL),
			  counter, addr, addr >> 32 });
}

/* Wave 0's: the SQ up to @pc, every descriptor already out. */
static INLINE void nic_tx_ring(struct gda *g, uint32_t pc)
{
	if (!lane_id())
		bnxt_ring(g, KNOD_PERSIST_DB_SQ,
			  bnxt_db_idx(pc, GL(g, SQ_MASK)));
}

#endif /* KNOD_BLOB_NIC_BNXT_H */
