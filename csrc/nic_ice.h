/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The GDA engine's ice part (KNOD_PERSIST_FMT_ICE_E810): an RX descriptor
 * ring the NIC writes each packet back into, a TX ring that writes back DONE
 * on the descriptors that asked, a tail register for each, and no completion
 * queues.  The hooks gda_engine.h calls.
 *
 * ice reads a full ring as an empty one, so as bnxt's the RQ has more entries
 * than are ever posted: position p is slot p of the ring, holding page p
 * modulo the posted count.  The write-back takes a slot's address with it, so
 * a slot is written again as the tail moves over it.  A slot past the tail
 * still holds what was written back a pass ago, DD and all: a completion is
 * the shader's only before the tail.
 *
 * The TX descriptors that ask for a completion - the last of each round - go
 * on a list in the ring buffer's TX CQ place, which the shader reads them off
 * in order: GL_TXRS_PC appended, GL_TX_CI taken.
 */
#ifndef KNOD_BLOB_NIC_ICE_H
#define KNOD_BLOB_NIC_ICE_H

#include "gda.h"
#include "ice.h"

/* A full ring is one ice reads as empty. */
#define NIC_SQ_SPARE	1

static INLINE void nic_load(struct gda *g, entry_t e)
{
	SET(g, RQ_RING_MASK, (1u << get32(&e->rq_ring_log)) - 1);
	SET64(g, RX_DB, get64(&e->rx_db));
}

/* Where the tail is for the RQ bound @told: as far past it as is posted. */
static INLINE uint32_t ice_tail(struct gda *g, uint32_t told)
{
	return (told + GL(g, RQ_SIZE)) & ~(KNOD_ICE_RX_TAIL_ALIGN - 1);
}

/* Positions [@from, @to) posted again, each slot with its page. */
static INLINE void ice_rx_fill(struct gda *g, uint32_t from, uint32_t to)
{
	uint32_t size = GL(g, RQ_SIZE), mask = GL(g, RQ_RING_MASK), i, p;
	uint64_t a;

	for (i = lane_id(); i < to - from; i += 64) {
		p = from + i;
		a = gda_page_dma(g, p & (size - 1)) +
		    gda_page_off(g, p & (size - 1));
		put128(g->ring + KNOD_PERSIST_RING_RQ_OFF +
		       ((p & mask) << KNOD_ICE_RX_DESC_SHIFT),
		       (v4u){ a, a >> 32, 0, 0 });
	}
	stores_out();
	if (!lane_id())
		put32((GLOBAL void *)GL64(g, RX_DB), to & mask);
}

static INLINE void nic_rx_post(struct gda *g)
{
	ice_rx_fill(g, 0, ice_tail(g, 0));
}

struct nic_rx {
	GLOBAL struct knod_ice_rx_wb *d;
};

static INLINE int nic_rx_poll(struct gda *g, uint32_t pos, struct nic_rx *rx)
{
	uint32_t tail = ice_tail(g, GL(g, RQ_TOLD));

	rx->d = (GLOBAL struct knod_ice_rx_wb *)
		(g->ring + KNOD_PERSIST_RING_RQ_OFF +
		 ((pos & GL(g, RQ_RING_MASK)) << KNOD_ICE_RX_DESC_SHIFT));
	return (get32(&rx->d->status_error0) & KNOD_ICE_RX_DD) &&
	       (int32_t)(tail - pos) > 0;
}

/* Position p holds page p: there is no opaque to say so.  A packet that took
 * more than its one buffer, its EOF elsewhere, is out of step.
 */
static INLINE uint64_t nic_rx_read(struct gda *g, struct nic_rx *rx,
				   uint32_t pos, int ordered, uint32_t *len,
				   uint32_t *rss, uint32_t *page, uint32_t *kind)
{
	v4u c = *(volatile GLOBAL v4u *)rx->d;

	_Static_assert(offsetof(struct knod_ice_rx_wb, pkt_len) == 4 &&
		       offsetof(struct knod_ice_rx_wb, status_error0) == 8 &&
		       offsetof(struct knod_ice_rx_wb, rss_hash) == 12,
		       "the write-back's dwords");
	*len = c.y & KNOD_ICE_RX_LEN_MASK;
	if (ordered)
		*rss = c.w;
	*page = pos & (GL(g, RQ_SIZE) - 1);
	*kind = c.z & 0xffff;
	return ballot(!(c.z & KNOD_ICE_RX_EOF));
}

/* Wave 0's: the RQ entries back up to @bound once a quarter of what is posted
 * has come back - those slots written again, then the tail.  No CQ to give
 * back.
 */
static INLINE void nic_rx_release(struct gda *g, uint32_t ci, uint32_t bound)
{
	uint32_t told = GL(g, RQ_TOLD);

	if (bound - told < GL(g, RQ_SIZE) / 4)
		return;
	SET(g, RQ_TOLD, bound);
	ice_rx_fill(g, ice_tail(g, told), ice_tail(g, bound));
}

/* What the NIC has sent: the descriptors on the completion list it wrote
 * DONE back on, in order.
 */
static INLINE void nic_tx_reap(struct gda *g, uint32_t pc)
{
	uint32_t ci = GL(g, TX_CI), mask = (1u << GL(g, TX_CQ_LOG)) - 1;
	uint32_t pos = ci + lane_id(), at, w, n;
	GLOBAL uint8_t *sq = (GLOBAL uint8_t *)GL64(g, SQ);

	at = get32(g->ring + KNOD_PERSIST_RING_TX_CQ_OFF + 4 * (pos & mask));
	w = get32(sq + ((at & GL(g, SQ_MASK)) << KNOD_ICE_TX_DESC_SHIFT) +
		  offsetof(struct knod_ice_tx_desc, cmd_type_offset_bsz));
	n = run_of(ballot(lane_id() < GL(g, TXRS_PC) - ci &&
			  (w & KNOD_ICE_TX_DTYPE) == KNOD_ICE_TX_DTYPE_DONE));
	if (!n)
		return;
	SET(g, SQ_CC, __builtin_amdgcn_readlane(at, n - 1) + 1);
	SET(g, TX_CI, ci + n);
}

static INLINE void nic_tx_post(struct gda *g, uint32_t counter, uint32_t page,
			       uint32_t off, uint32_t len, int signal)
{
	GLOBAL uint8_t *d = (GLOBAL uint8_t *)GL64(g, SQ) +
		((counter & GL(g, SQ_MASK)) << KNOD_ICE_TX_DESC_SHIFT);
	uint64_t addr = gda_page_dma(g, page) + off;
	uint32_t cmd = KNOD_ICE_TX_CMD_EOP | (signal ? KNOD_ICE_TX_CMD_RS : 0);

	put128(d, (v4u){ addr, addr >> 32, cmd << KNOD_ICE_TX_CMD_SHIFT,
			 len << (KNOD_ICE_TX_LEN_SHIFT - 32) });
}

/* Wave 0's: the round's last descriptor, the one that asked for a completion,
 * onto the list, then the tail.
 */
static INLINE void nic_tx_ring(struct gda *g, uint32_t pc)
{
	uint32_t rs = GL(g, TXRS_PC), mask = (1u << GL(g, TX_CQ_LOG)) - 1;

	if (!lane_id())
		put32(g->ring + KNOD_PERSIST_RING_TX_CQ_OFF + 4 * (rs & mask),
		      pc - 1);
	SET(g, TXRS_PC, rs + 1);
	if (!lane_id())
		put32((GLOBAL void *)GL64(g, DB), pc & GL(g, SQ_MASK));
}

#endif /* KNOD_BLOB_NIC_ICE_H */
