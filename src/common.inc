// SPDX-License-Identifier: GPL-2.0-or-later
//
// Short names for the register assignment the kernel publishes.  Everything
// numeric comes from the uapi header so the two cannot drift; only spelling
// and the RDNA ISA shims live here.
//
// Names are register numbers rather than operands so they work in range
// syntax: v[KEY] for one, v[KEY:KEY+1] for a pair.

#ifndef COMMON_INC
#define COMMON_INC

#include <linux/knod_blob.h>

// What the engine leaves a program.
#define PARAM		KNOD_BLOB_PRO_PARAM_SREG
#define PKT_OFF		KNOD_BLOB_PRO_OFF_VREG
#define CTX		KNOD_BLOB_PRO_CTX_VREG
#define IDX		KNOD_BLOB_PRO_IDX_VREG
#define DATA		KNOD_BLOB_PRO_DATA_VREG
#define DATA_END	KNOD_BLOB_PRO_DATA_END_VREG
#define PAGE_BASE	KNOD_BLOB_PRO_PAGE_BASE_VREG
#define PAGE_IDX	KNOD_BLOB_PRO_PAGE_IDX_VREG

// BPF registers: r0 carries the verdict, r1 the context, r10 the frame
// pointer.
#define BPF_R0		KNOD_BLOB_BPF_VREG(0)
#define BPF_R1		KNOD_BLOB_BPF_VREG(1)
#define BPF_R10		KNOD_BLOB_BPF_VREG(10)

#define XDP_DROP	1
#define XDP_PASS	2

// The JIT's own scalars.
#define DONE_MASK	KNOD_BLOB_DONE_MASK_SREG
#define INITIAL_EXEC	KNOD_BLOB_INITIAL_EXEC_SREG

#define V_ADD_CI_U32	v_add_co_ci_u32

// GFX10 requires the _nc spelling; GFX11 uses the plain spelling.
#if KNOD_ISA == 10
# define V_SUB_U32	v_sub_nc_u32
#else
# define V_SUB_U32	v_sub_u32
#endif

#endif /* COMMON_INC */
