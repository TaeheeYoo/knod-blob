/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * What the map routines in C share: the descriptor, how map memory is
 * reached, and the wave-level helpers.
 */
#ifndef KNOD_CSRC_MAP_H
#define KNOD_CSRC_MAP_H

#include <stdint.h>

typedef uint32_t __u32;
typedef uint64_t __u64;
#include <linux/knod_blob.h>

#define GLOBAL		__attribute__((address_space(1)))
/* Unchanged while a program runs, so read with scalar loads. */
#define CONSTANT	__attribute__((address_space(4)))

typedef const CONSTANT struct knod_blob_map_desc *desc_t;
typedef GLOBAL uint32_t *gptr;

static inline uint32_t load(gptr p)
{
	return __scoped_atomic_load_n(p, __ATOMIC_RELAXED,
				      __MEMORY_SCOPE_DEVICE);
}

static inline void store(gptr p, uint32_t v)
{
	__scoped_atomic_store_n(p, v, __ATOMIC_RELAXED, __MEMORY_SCOPE_DEVICE);
}

static inline uint32_t add(gptr p, uint32_t v)
{
	return __scoped_atomic_fetch_add(p, v, __ATOMIC_RELAXED,
					 __MEMORY_SCOPE_DEVICE);
}

/* Every store before it lands before any store after it. */
static inline void release(void)
{
	__builtin_amdgcn_fence(__ATOMIC_RELEASE, "agent");
}

static inline uint32_t uniform32(uint32_t v)
{
	return __builtin_amdgcn_readfirstlane(v);
}

static inline uint64_t uniform64(uint64_t v)
{
	return (uint64_t)uniform32(v >> 32) << 32 | uniform32(v);
}

/* Every lane is handed the same descriptor. */
static inline desc_t uniform_desc(desc_t d)
{
	return (desc_t)uniform64((uint64_t)d);
}

static inline uint32_t lane_id(void)
{
	return __builtin_amdgcn_mbcnt_hi(~0u, __builtin_amdgcn_mbcnt_lo(~0u, 0));
}


static inline uint64_t readlane64(uint64_t v, uint32_t lane)
{
	return (uint64_t)__builtin_amdgcn_readlane(v >> 32, lane) << 32 |
	       (uint32_t)__builtin_amdgcn_readlane(v, lane);
}

/*
 * The key and the value are passed as that many dwords, each its own
 * argument: an aggregate or an array would be passed through a stack.
 */
#define A1(t)	t##_0
#define A2(t)	A1(t), t##_1
#define A3(t)	A2(t), t##_2
#define A4(t)	A3(t), t##_3
#define A5(t)	A4(t), t##_4
#define A6(t)	A5(t), t##_5
#define A7(t)	A6(t), t##_6
#define A8(t)	A7(t), t##_7
#define A9(t)	A8(t), t##_8
#define A10(t)	A9(t), t##_9
#define A11(t)	A10(t), t##_10
#define A12(t)	A11(t), t##_11
#define A13(t)	A12(t), t##_12
#define A14(t)	A13(t), t##_13

#endif
