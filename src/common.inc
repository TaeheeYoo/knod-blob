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

#define PS0		(KNOD_BLOB_PRO_TMP_SREG + 0)
#define PS2		(KNOD_BLOB_PRO_TMP_SREG + 2)
#define PS4		(KNOD_BLOB_PRO_TMP_SREG + 4)
#define PS6		(KNOD_BLOB_PRO_TMP_SREG + 6)

// BPF registers the prologue seeds: r1 is the context, r10 the frame pointer.
#define BPF_R1		2
#define BPF_R10		20

// The JIT's own scalars.  These follow the ISA rather than the program, and
// the two generations disagree; unifying them would retire this.
#if KNOD_ISA == 10
# define DONE_MASK	32
# define INITIAL_EXEC	104
#else
# define DONE_MASK	34
# define INITIAL_EXEC	100
#endif

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

#endif /* COMMON_INC */
