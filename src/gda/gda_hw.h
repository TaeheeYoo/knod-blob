/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The few things the GDA engine needs from the hardware that C does not say
 * by itself: lanes and waves, the memory model the rings need, and the
 * instructions with no C spelling.  Everything here is a builtin or one line
 * of inline asm, and is the only place either appears.
 *
 * Code runs Wave64, in CU mode, called from the glue with every lane on.
 */
#ifndef KNOD_GDA_HW_H
#define KNOD_GDA_HW_H

#include <linux/types.h>

typedef _Bool bool;
#define true	1
#define false	0

/* Address spaces: global memory (VRAM, GTT, the NIC's BAR) and LDS. */
#define __global	__attribute__((address_space(1)))
#define __lds		__attribute__((address_space(3)))

typedef u32 u32x2 __attribute__((ext_vector_type(2)));
typedef u32 u32x4 __attribute__((ext_vector_type(4)));
typedef u32 u32x16 __attribute__((ext_vector_type(16)));

#define __always_inline	inline __attribute__((always_inline, unused))

/* ---- lanes ---- */

static __always_inline u32 lane_id(void)
{
	return __builtin_amdgcn_mbcnt_hi(~0u, __builtin_amdgcn_mbcnt_lo(~0u, 0));
}

/* The lanes for which @c holds, of the ones on. */
static __always_inline u64 ballot(bool c)
{
	return __builtin_amdgcn_ballot_w64(c);
}

/* How many lanes below this one are in @mask. */
static __always_inline u32 rank(u64 mask)
{
	return __builtin_amdgcn_mbcnt_hi((u32)(mask >> 32),
					 __builtin_amdgcn_mbcnt_lo((u32)mask, 0));
}

/* The lanes below @n. */
static __always_inline u64 lanes_below(u32 n)
{
	return n >= 64 ? ~0ull : (1ull << n) - 1;
}

/* How many lanes from lane 0 up @ok holds for without a gap. */
static __always_inline u32 run_from_0(u64 ok)
{
	return ~ok ? (u32)__builtin_ctzll(~ok) : 64;
}

/* Lane @l's value of @v, the same in every lane: an SGPR. */
static __always_inline u32 lane_value(u32 v, u32 l)
{
	return __builtin_amdgcn_readlane(v, l);
}

/* @v is the same in every lane; say so, so it becomes an SGPR. */
static __always_inline u32 uniform(u32 v)
{
	return __builtin_amdgcn_readfirstlane(v);
}

static __always_inline u64 uniform64(u64 v)
{
	return uniform((u32)v) | (u64)uniform((u32)(v >> 32)) << 32;
}

/* @v with lane @l's value replaced by @x, which is the same in every lane. */
static __always_inline u32 with_lane(u32 v, u32 l, u32 x)
{
#if __has_builtin(__builtin_amdgcn_writelane)
	return __builtin_amdgcn_writelane(uniform(x), l, v);
#else
	__asm__("v_writelane_b32 %0, %1, %2"
		: "+v"(v) : "s"(uniform(x)), "s"(uniform(l)));
	return v;
#endif
}

/* ---- memory ---- */

/*
 * What another agent writes - the host, the NIC, another CU - has to be read
 * past the caches that are not shared with it: glc dlc.
 */
#define load_coherent(p) \
	__scoped_atomic_load_n((p), __ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM)

/* What another agent reads goes out past L2's write-back: glc slc. */
#define store_out(p, v)	__builtin_nontemporal_store((v), (p))

/* Every store this wave issued has reached memory. */
static __always_inline void wait_stores(void)
{
	__asm__ volatile("s_waitcnt_vscnt null, 0" ::: "memory");
}

/*
 * The caches a wave's loads go through, emptied of anything the host may
 * have changed behind them: maps written while the shader was parked.
 */
static __always_inline void invalidate_caches(void)
{
	__asm__ volatile("buffer_gl1_inv\n\t"
			 "buffer_gl0_inv\n\t"
			 "s_dcache_inv\n\t"
			 "s_waitcnt vmcnt(0) lgkmcnt(0)" ::: "memory");
}

/*
 * Every wave of the workgroup gets here, with what each wrote to LDS.  Only
 * LDS: a workgroup fence would also drain every store on gfx11, and nothing
 * the waves tell each other goes through memory.  CU mode, so no L0 to
 * invalidate either.
 */
static __always_inline void workgroup_barrier(void)
{
	__asm__ volatile("s_waitcnt lgkmcnt(0)" ::: "memory");
	__builtin_amdgcn_s_barrier();
	__asm__ volatile("" ::: "memory");
}

/*
 * GFX10 wants an LDS access and a VMEM one on either side of a branch kept
 * apart by this, and the compiler cannot see the branches into and out of the
 * C.  Nothing on GFX11.
 */
static __always_inline void lds_vmem_branch_fence(void)
{
#if KNOD_ISA == 10
	__asm__ volatile("s_waitcnt_vscnt null, 0" ::: "memory");
#endif
}

/* 16 bytes another agent writes, read in one go past the caches. */
static __always_inline u32x4 load_coherent_x4(const void __global *p)
{
	u32x4 v;

	__asm__ volatile("global_load_dwordx4 %0, %1, off glc dlc\n\t"
			 "s_waitcnt vmcnt(0)"
			 : "=v"(v) : "v"(p) : "memory");
	return v;
}

/* ---- the rest ---- */

#define sleep(n)	__builtin_amdgcn_s_sleep(n)

/* This wave ends.  Only on a condition every lane agrees on. */
#define end_wave()	__builtin_amdgcn_endpgm()

#define MSG_INTERRUPT	1
/* An interrupt to the host carrying @id in its low 23 bits. */
#define interrupt_host(id)	__builtin_amdgcn_s_sendmsg(MSG_INTERRUPT, (id))

/*
 * Back to the glue at @ret with the results in v0 upwards, where it takes
 * them from.  The glue passed @ret; see GDA_CALL.
 */
#define back_to_glue1(ret, a)						\
	do {								\
		__asm__ volatile("s_setpc_b64 %0" ::			\
				 "s"(uniform64(ret)), "{v0}"(a));	\
		__builtin_trap();	/* not reached */		\
	} while (0)

#define back_to_glue_round(ret, a0, a1, a2, a3, a4, a5, a6, a7, a8,	\
			   a9, a10, a11, a12, a13, a14)			\
	do {								\
		__asm__ volatile("s_setpc_b64 %0" ::			\
				 "s"(uniform64(ret)),			\
				 "{v0}"(a0), "{v1}"(a1), "{v2}"(a2),	\
				 "{v3}"(a3), "{v4}"(a4), "{v5}"(a5),	\
				 "{v6}"(a6), "{v7}"(a7), "{v8}"(a8),	\
				 "{v9}"(a9), "{v10}"(a10), "{v11}"(a11), \
				 "{v12}"(a12), "{v13}"(a13), "{v14}"(a14)); \
		__builtin_trap();	/* not reached */		\
	} while (0)

#define be16(x)		__builtin_bswap16(x)
#define be32(x)		__builtin_bswap32(x)

#endif
