// SPDX-License-Identifier: GPL-2.0-or-later
//
// GDA: what wraps a program when the shader runs the NIC's rings itself.
//
// The engine is C, in src/gda/gda.c; this is only the glue between it and the
// JIT's register contract.  Each routine is laid out as
//
//	.text.<routine>		the glue below, entered by the JIT
//	.text.<routine>.c	the C entry points it jumps to (gda.c, built
//				for this routine)
//	.text.<routine>.~	the rest of the glue, then __end_<routine>
//
// which tools/knod.ld keeps in that order, so the routine is one piece the JIT
// can splice.  The prologue's first part runs once and jumps over the C to its
// second part, which a round starts at and which falls through into the
// program.
//
// The C entry points are built as kernels and are jumped to rather than
// called (see gda.c), so they keep no register for us: everything that has to
// outlive a round is in v[GS] (src/gda/gda_lanes.h), which they never reach
// past, and the scalars the program expects are put back from it after each
// jump.

#ifndef GDA_INC
#define GDA_INC
#include "gda/gda_lanes.h"

#define GS		KNOD_BLOB_PRO_GDA_VREG
// The second of the three VGPRs the engine has (KNOD_BLOB_PRO_GDA_VREGS),
// which the C never reaches either (tools/check_c.py).
#define GDA_SAVE	(KNOD_BLOB_PRO_GDA_VREG + 1)

// Jump to the C entry point \fn, with where to come back in v[\ret:\ret+1].
// It comes back to just after this, with its results in v0 upwards and every
// SGPR below about s50 changed.  s[30:31], which a probe build holds across
// the program (KNOD_BLOB_PROBE_SREG), is kept in v[GDA_SAVE] meanwhile.
.macro GDA_CALL fn, ret
	v_writelane_b32 v[GDA_SAVE], s30, 0
	v_writelane_b32 v[GDA_SAVE], s31, 1
	s_getpc_b64 s[30:31]
.Lgda_from\@:
	s_add_u32 s30, s30, .Lgda_back\@ - .Lgda_from\@
	s_addc_u32 s31, s31, 0
	v_mov_b32 v[\ret], s30
	v_mov_b32 v[\ret + 1], s31
	s_getpc_b64 s[30:31]
	s_add_u32 s30, s30, \fn@rel32@lo + 4
	s_addc_u32 s31, s31, \fn@rel32@hi + 12
	s_setpc_b64 s[30:31]
.Lgda_back\@:
	s_mov_b64 exec, -1
	v_readlane_b32 s30, v[GDA_SAVE], 0
	v_readlane_b32 s31, v[GDA_SAVE], 1
.endm

// The scalars the hardware handed the shader that the program may still
// read, which the C took no care of.
.macro GDA_RESTORE_HW
	v_readlane_b32 s[DISPATCH], v[GS], GL_HW_DISPATCH
	v_readlane_b32 s[DISPATCH + 1], v[GS], GL_HW_DISPATCH + 1
	v_readlane_b32 s8, v[GS], GL_HW_KERNARG
	v_readlane_b32 s9, v[GS], GL_HW_KERNARG + 1
	v_readlane_b32 s[WG_X], v[GS], GL_HW_WG_X
	v_readlane_b32 s[WG_Y], v[GS], GL_HW_WG_Y
.endm

// The end of a routine, past its C, for tools/pack.py to size it by.
.macro GDA_ROUTINE_END name
	.section .text.\name\().~, "ax", @progbits
	.globl __end_\name
__end_\name:
.endm

// Before the program: once, then every round from .Lgda_round_\name, which
// waits for packets and falls through into the program with them.
.macro KNOD_GDA_PROLOGUE_BODY name
#if !defined(KNOD_NO_ICACHE_INV)
	s_icache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
#endif
	// start(tid = v0, wg_x, wg_y, kernarg, dispatch) -> v[GS] in v0
	s_mov_b64 exec, -1
	v_mov_b32 v1, s[WG_X]
	v_mov_b32 v2, s[WG_Y]
	v_mov_b32 v3, s8
	v_mov_b32 v4, s9
	v_mov_b32 v5, s[DISPATCH]
	v_mov_b32 v6, s[DISPATCH + 1]
	GDA_CALL gda_\name\()__start, 7
	v_mov_b32 v[GS], v0
	s_getpc_b64 s[30:31]
	s_add_u32 s30, s30, .Lgda_round_\name\()@rel32@lo + 4
	s_addc_u32 s31, s31, .Lgda_round_\name\()@rel32@hi + 12
	s_setpc_b64 s[30:31]

	.section .text.\name\().~, "ax", @progbits
.Lgda_round_\name:
	// Where the epilogue comes back to.
	s_getpc_b64 s[6:7]
	v_writelane_b32 v[GS], s6, GL_ROUND_PC
	v_writelane_b32 v[GS], s7, GL_ROUND_PC + 1

	// round(v[GS]) -> v[GS], exec, then what the program is handed
	s_mov_b64 exec, -1
	v_mov_b32 v0, v[GS]
	GDA_CALL gda_\name\()__round, 1
#if KNOD_ISA == 10
	// The program's first LDS or VMEM access is across a branch from the
	// C's last; GFX10 wants them kept apart.
	s_waitcnt_vscnt null, 0
#endif
	v_mov_b32 v[GS], v0
	v_readfirstlane_b32 s[INITIAL_EXEC], v1
	v_readfirstlane_b32 s[INITIAL_EXEC + 1], v2
	v_mov_b32 v[PAGE_IDX], v3
	v_mov_b32 v[PKT_OFF], v4
	v_mov_b32 v[DATA], v5
	v_mov_b32 v[DATA + 1], v6
	v_mov_b32 v[DATA_END], v7
	v_mov_b32 v[DATA_END + 1], v8
	v_mov_b32 v[PAGE_BASE], v9
	v_mov_b32 v[PAGE_BASE + 1], v10
	v_mov_b32 v[CTX], v11
	v_mov_b32 v[CTX + 1], v12
	v_mov_b32 v[IDX], v13
	v_mov_b32 v[P18], v14			// the lane's index in the queue
	v_mov_b32 v[TID], v14
	v_mov_b32 v[BPF_R1], v[CTX]		// r1 = &param->sub[idx]
	v_mov_b32 v[BPF_R1 + 1], v[CTX + 1]
	v_mov_b32 v[BPF_R10], KNOD_BLOB_BPF_STACK_SIZE	// r10 at the top of the frame
	v_mov_b32 v[BPF_R10 + 1], 0
	GDA_RESTORE_HW
	v_readlane_b32 s[PARAM], v[GS], GL_PARAM
	v_readlane_b32 s[PARAM + 1], v[GS], GL_PARAM + 1
	s_mov_b32 s[FRAME], 0
	s_mov_b64 s[DONE_MASK:DONE_MASK+1], 0
	// This wave's share, which may be none; its program still runs, with
	// no lanes, to meet the others at the epilogue's barriers.
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]
.endm

// After the program: send, pass and give back what the round took, then the
// next round.
.macro KNOD_GDA_EPILOGUE_BODY name
	// A lane that did not return drops, as in KNOD_EPILOGUE_PRE_BODY.
	s_andn2_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1], \
		    s[DONE_MASK:DONE_MASK+1]
	v_mov_b32 v[BPF_R0], XDP_DROP

	// Whatever the program wrote - maps above all - is out before the round
	// ends: parking is where the host reads and changes them.
	s_waitcnt vmcnt(0) lgkmcnt(0)
	s_waitcnt_vscnt null, 0

	// finish(verdict = v0, page_idx, data, data_end, page_base, the lanes
	// that ran, v[GS]) -> v[GS] in v0
	s_mov_b64 exec, -1
	v_mov_b32 v1, v[PAGE_IDX]
	v_mov_b32 v2, v[DATA]
	v_mov_b32 v3, v[DATA + 1]
	v_mov_b32 v4, v[DATA_END]
	v_mov_b32 v5, v[DATA_END + 1]
	v_mov_b32 v6, v[PAGE_BASE]
	v_mov_b32 v7, v[PAGE_BASE + 1]
	v_mov_b32 v8, s[INITIAL_EXEC]
	v_mov_b32 v9, s[INITIAL_EXEC + 1]
	v_mov_b32 v10, v[GS]
	GDA_CALL gda_\name\()__finish, 11
	v_mov_b32 v[GS], v0
	// Nothing runs before the round's own call, which puts the hardware's
	// scalars back after it.
	v_readlane_b32 s6, v[GS], GL_ROUND_PC
	v_readlane_b32 s7, v[GS], GL_ROUND_PC + 1
	s_setpc_b64 s[6:7]
.endm

#endif /* GDA_INC */
