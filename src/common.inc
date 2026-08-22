// SPDX-License-Identifier: GPL-2.0-or-later
//
// Short names for the register assignment the kernel publishes.  Everything
// numeric comes from the uapi header so the two cannot drift; only spelling
// and the ISA shims live here.
//
// Names are register numbers rather than operands so they work in range
// syntax: v[KEY] for one, v[KEY:KEY+1] for a pair.

#ifndef COMMON_INC
#define COMMON_INC

#include <linux/knod_blob.h>

#define DESC		KNOD_BLOB_SPLICE_DESC_SREG
#define KEY		KNOD_BLOB_SPLICE_KEY_VREG
#define VAL		KNOD_BLOB_SPLICE_VAL_VREG
#define RET		KNOD_BLOB_SPLICE_RET_VREG

// KNOD_BLOB_SPLICE_TMP_VREG (v28) upwards is ours to clobber.
#define T0		(KNOD_BLOB_SPLICE_TMP_VREG + 0)
#define T1		(KNOD_BLOB_SPLICE_TMP_VREG + 1)
#define T2		(KNOD_BLOB_SPLICE_TMP_VREG + 2)
#define T3		(KNOD_BLOB_SPLICE_TMP_VREG + 3)
#define T4		(KNOD_BLOB_SPLICE_TMP_VREG + 4)
#define T5		(KNOD_BLOB_SPLICE_TMP_VREG + 5)
#define T6		(KNOD_BLOB_SPLICE_TMP_VREG + 6)
#define T7		(KNOD_BLOB_SPLICE_TMP_VREG + 7)

// KNOD_BLOB_SPLICE_TMP_SREG (s48) upwards is ours to clobber.  Not s32-s35:
// s[34:35] is the done mask and is live across a splice.
#define S0		(KNOD_BLOB_SPLICE_TMP_SREG + 0)
#define S1		(KNOD_BLOB_SPLICE_TMP_SREG + 1)
#define S2		(KNOD_BLOB_SPLICE_TMP_SREG + 2)
#define S3		(KNOD_BLOB_SPLICE_TMP_SREG + 3)

// KNOD_BLOB_EXEC_SAVE_SREG (s36) upwards is ours to save EXEC in.
#define XSAVE0		(KNOD_BLOB_EXEC_SAVE_SREG + 0)
#define XSAVE1		(KNOD_BLOB_EXEC_SAVE_SREG + 2)
#define XSAVE2		(KNOD_BLOB_EXEC_SAVE_SREG + 4)

//
// Prologue and epilogue.  These are the ends of a program rather than
// something spliced into it, so they take no arguments; the names below are
// what the hardware hands them and what they hand the program.
//
#define DISPATCH	KNOD_BLOB_PRO_DISPATCH_SREG
#define WG_X		KNOD_BLOB_PRO_WG_X_SREG
#define WG_Y		KNOD_BLOB_PRO_WG_Y_SREG
#define PARAM		KNOD_BLOB_PRO_PARAM_SREG
#define FRAME		KNOD_BLOB_PRO_FRAME_SREG
#define TID		KNOD_BLOB_PRO_TID_VREG

#define SLOT		KNOD_BLOB_PRO_SLOT_VREG
#define CTX		KNOD_BLOB_PRO_CTX_VREG
#define IDX		KNOD_BLOB_PRO_IDX_VREG
#define DATA		KNOD_BLOB_PRO_DATA_VREG
#define DATA_END	KNOD_BLOB_PRO_DATA_END_VREG
#define PAGE_BASE	KNOD_BLOB_PRO_PAGE_BASE_VREG

#define P0		(KNOD_BLOB_PRO_TMP_VREG + 0)
#define P1		(KNOD_BLOB_PRO_TMP_VREG + 1)
#define P2		(KNOD_BLOB_PRO_TMP_VREG + 2)
#define P3		(KNOD_BLOB_PRO_TMP_VREG + 3)
#define P4		(KNOD_BLOB_PRO_TMP_VREG + 4)
#define P5		(KNOD_BLOB_PRO_TMP_VREG + 5)
#define P6		(KNOD_BLOB_PRO_TMP_VREG + 6)
#define P7		(KNOD_BLOB_PRO_TMP_VREG + 7)
#define P8		(KNOD_BLOB_PRO_TMP_VREG + 8)
#define P9		(KNOD_BLOB_PRO_TMP_VREG + 9)
#define P10		(KNOD_BLOB_PRO_TMP_VREG + 10)
#define P12		(KNOD_BLOB_PRO_TMP_VREG + 12)
#define P14		(KNOD_BLOB_PRO_TMP_VREG + 14)
#define P16		(KNOD_BLOB_PRO_TMP_VREG + 16)
#define P18		(KNOD_BLOB_PRO_TMP_VREG + 18)
#define P20		(KNOD_BLOB_PRO_TMP_VREG + 20)
#define P22		(KNOD_BLOB_PRO_TMP_VREG + 22)

#define PS0		(KNOD_BLOB_PRO_TMP_SREG + 0)
#define PS2		(KNOD_BLOB_PRO_TMP_SREG + 2)
#define PS4		(KNOD_BLOB_PRO_TMP_SREG + 4)
#define PS6		(KNOD_BLOB_PRO_TMP_SREG + 6)

// BPF registers the prologue seeds and the epilogue reads back: r0 carries the
// verdict, r1 the context, r10 the frame pointer.
#define BPF_R0		0
#define BPF_R1		2
#define BPF_R10		20

// What the epilogue writes into the buffer descriptor, and what it tells the
// host by writing it.
#define XDP_ABORTED	0
#define XDP_DROP	1
#define XDP_PASS	2

// The JIT's own scalars.
#define DONE_MASK	KNOD_BLOB_DONE_MASK_SREG
#define INITIAL_EXEC	KNOD_BLOB_INITIAL_EXEC_SREG

// struct knod_blob_map_desc
#define DESC_KEY_SIZE		KNOD_BLOB_DESC_KEY_SIZE
#define DESC_VALUE_SIZE		KNOD_BLOB_DESC_VALUE_SIZE
#define DESC_MAX_ENTRIES	KNOD_BLOB_DESC_MAX_ENTRIES
#define DESC_ELEM_SIZE		KNOD_BLOB_DESC_ELEM_SIZE
#define DESC_BUCKET		KNOD_BLOB_DESC_BUCKET
#define DESC_ELEMS		KNOD_BLOB_DESC_ELEMS
#define DESC_QUEUE		KNOD_BLOB_DESC_QUEUE
#define DESC_GC_LIST		KNOD_BLOB_DESC_GC_LIST
#define DESC_GC_COUNT		KNOD_BLOB_DESC_GC_COUNT
#define DESC_PER_INSTANCE	KNOD_BLOB_DESC_PER_INSTANCE
#define DESC_N_BUCKETS		KNOD_BLOB_DESC_N_BUCKETS
#define DESC_LOCK_OFFSET	KNOD_BLOB_DESC_LOCK_OFFSET
#define DESC_HASHRND		KNOD_BLOB_DESC_HASHRND

// Mnemonics that were renamed after gfx9.  The encodings differ too, so this
// is a spelling shim, not a compatibility layer.
#if KNOD_ISA == 9
# define V_ADD_CI_U32	v_addc_co_u32
#else
# define V_ADD_CI_U32	v_add_co_ci_u32
#endif

// The carry-less add and subtract keep their plain names on gfx9 and gfx11 but
// not on gfx10, which only accepts the _nc spelling.
#if KNOD_ISA == 10
# define V_ADD_U32	v_add_nc_u32
# define V_SUB_U32	v_sub_nc_u32
#else
# define V_ADD_U32	v_add_u32
# define V_SUB_U32	v_sub_u32
#endif

// Splice linkage ends by falling through into whatever the JIT emits next, so
// there is nothing to do.  Call linkage would return through s[30:31] - and
// would have to be handed the descriptor somewhere else, since that is where
// it lives here.
#if KNOD_BLOB_LINK == KNOD_BLOB_LINK_CALL
# define KNOD_BLOB_RET	s_setpc_b64 s[KNOD_BLOB_CALL_RET_ADDR_SREG:KNOD_BLOB_CALL_RET_ADDR_SREG+1]
#else
# define KNOD_BLOB_RET
#endif

#endif /* COMMON_INC */
