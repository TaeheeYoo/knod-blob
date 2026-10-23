/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The GDA engine's common ground: the state it keeps in a VGPR's lanes, where
 * a queue's waves meet in LDS, and the GPU primitives every part uses - the
 * engine in gda_engine.h, the NIC parts in nic_*.h, the ordered engine's in
 * gda_order.c.
 */
#ifndef KNOD_BLOB_GDA_H
#define KNOD_BLOB_GDA_H

#include <linux/knod_persistent.h>
#include "map.h"

#define LDS		__attribute__((address_space(3)))
/* Into the functions the engine calls: a call out of one would be to code
 * nothing places.
 */
#define INLINE		inline __attribute__((always_inline))

#define offsetof(t, f)	__builtin_offsetof(t, f)

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
	GL_SQN,			/* mlx5's */
	GL_TX_MKEY,		/* mlx5's */
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
	GL_RQ_RING_MASK = GL_RING + 2,	/* bnxt's: RQ entries - 1 */
	GL_KEY,			/* bnxt's: each ring's doorbell key, high half */
	GL_CQ_TOLD = GL_KEY + KNOD_PERSIST_DB_KEYS,	/* bnxt's: CI the NIC has */
	GL_TX_CQ_TOLD,		/* bnxt's: TX_CI the NIC has */
	GL_RQ_TOLD,		/* bnxt's: the RQ bound the NIC has */
	GL_COUNT,
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
	/* The ordered engine's alone, past what the other has. */
	uint32_t count;		/* packets this round */
	uint32_t top[KNOD_PERSIST_GDA_WAVES_MAX];	/* ...its highest parked rank */
	uint32_t part[KNOD_PERSIST_GDA_WAVES_MAX * 2];	/* gda_gate()'s bits */
	uint32_t act[KNOD_PERSIST_GDA_WAVES_MAX * 2];
	uint16_t key[KNOD_PERSIST_GDA_WAVES_MAX * 64];	/* each packet's flow */
};
_Static_assert(offsetof(struct gda_meet, count) <= KNOD_PERSIST_GDA_LDS_BYTES,
	       "the meeting place");
_Static_assert(sizeof(struct gda_meet) <= KNOD_PERSIST_GDA_ORDER_LDS_BYTES,
	       "the ordered engine's meeting place");

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
#define KEY(g, k)	__builtin_amdgcn_readlane((g)->s, GL_KEY + (k))
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

/* An RX page's address, as the NIC has it, and where in it a packet goes. */
static INLINE uint64_t gda_page_dma(struct gda *g, uint32_t page)
{
	return ((GLOBAL uint64_t *)GL64(g, RX_DMA))[page];
}

static INLINE uint32_t gda_page_off(struct gda *g, uint32_t page)
{
	return GL(g, HEADROOM) + GL(g, STAGGER) * (page & GL(g, STAGGER_MASK));
}

static INLINE LDS struct gda_meet *meet_at(uint32_t off)
{
	return (LDS struct gda_meet *)(uintptr_t)uniform32(off);
}

/* Whether any lane of the round was about to write: the same in every wave
 * once gda_gate()'s barrier is behind them all.
 */
static INLINE int any_active(LDS struct gda_meet *meet)
{
	uint32_t any = 0, i;

	for (i = 0; i < KNOD_PERSIST_GDA_WAVES_MAX * 2; i++)
		any |= meet->act[i];
	return uniform32(any) != 0;
}

/* The highest @v among @lanes, all lanes together. */
static inline uint32_t wave_max8(uint32_t v, uint64_t lanes)
{
	uint32_t m = 0;

	for (int b = 7; b >= 0; b--)
		if (ballot(v >= (m | 1u << b)) & lanes)
			m |= 1u << b;
	return m;
}

typedef uint32_t v2u __attribute__((ext_vector_type(2)));

#endif /* KNOD_BLOB_GDA_H */
