// SPDX-License-Identifier: GPL-2.0-or-later
//
// The kernel every GDA shader starts in, around the engine in csrc/gda.c:
// gda_init() once, then every round gda_round_begin(), the program and
// gda_round_end().  The program goes right after it, and the call's offset to
// it is knod_gda_engine_call's to patch.
//
// Between calls this keeps the control block in s[8:9], the queue in s0 and
// the wave in s2, which a program leaves alone, and the engine's state in
// v[GS:GS+2], which no program reaches.  The functions keep s34 and up, so
// the scalars are carried across them there.

#include "common.inc"
#include "gda_program.inc"
#include "cfn.inc"

#define GS		KNOD_BLOB_PRO_GDA_VREG
#define GT		(KNOD_BLOB_PRO_TMP_SREG + 10)	// s[24:25]
#define E_CTL		8
#define E_QUEUE		0
#define E_WAVE		2

// Call cfn_\fn with the control block, queue and wave as its first three
// arguments; the rest are the caller's to put in v4 up.
.macro	ENGINE_CALL fn
	s_mov_b64 s[34:35], s[E_CTL:E_CTL+1]
	s_mov_b32 s36, s[E_QUEUE]
	s_mov_b32 s37, s[E_WAVE]
	s_mov_b64 exec, -1
	v_mov_b32 v0, s[E_CTL]
	v_mov_b32 v1, s[E_CTL+1]
	v_mov_b32 v2, s[E_QUEUE]
	v_mov_b32 v3, s[E_WAVE]
	s_mov_b32 s32, KNOD_BLOB_CALL_STACK_OFF
	s_getpc_b64 s[GT:GT+1]
.Lpc_\fn:
	s_add_u32 s[GT], s[GT], .Lcfn_\fn - .Lpc_\fn
	s_addc_u32 s[GT+1], s[GT+1], 0
	s_swappc_b64 s[30:31], s[GT:GT+1]
	s_mov_b64 s[E_CTL:E_CTL+1], s[34:35]
	s_mov_b32 s[E_QUEUE], s36
	s_mov_b32 s[E_WAVE], s37
.endm

	.text
	.globl knod_gda_engine
	.p2align 2
knod_gda_engine:
#if !defined(KNOD_NO_ICACHE_INV)
	s_icache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
#endif
#if KNOD_ISA == 10
	// FLAT_SCRATCH is the ring's base, out of the descriptor, plus this
	// wave's slot in it.  Set once: it lasts as long as the wave.
	s_and_b32 s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1], \
		  s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1], 0xffff
	s_add_u32 s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG], \
		  s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG], \
		  s[KNOD_BLOB_PRO_SCRATCH_WAVE_SREG]
	s_addc_u32 s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1], \
		   s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1], 0
	s_setreg_b32 hwreg(HW_REG_FLAT_SCR_LO), s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG]
	s_setreg_b32 hwreg(HW_REG_FLAT_SCR_HI), \
		     s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1]
#endif
	v_readfirstlane_b32 s[E_WAVE], v[TID]
	s_and_b32 s[E_WAVE], s[E_WAVE], 1023
	s_lshr_b32 s[E_WAVE], s[E_WAVE], 6
	s_mov_b32 s[E_QUEUE], s[WG_Y]

	ENGINE_CALL gda_init
	v_mov_b32 v[GS], v0
	v_mov_b32 v[GS+1], v1
	v_mov_b32 v[GS+2], v2

.Lgda_round:
	v_mov_b32 v4, v[GS]
	v_mov_b32 v5, v[GS+1]
	v_mov_b32 v6, v[GS+2]
	ENGINE_CALL gda_round_begin
	v_mov_b32 v[GS], v13
	v_mov_b32 v[GS+1], v14
	v_mov_b32 v[GS+2], v15

	// What the program reads its packets from.  v0 is the page, v1 the
	// packet's offset in it, v2 its length, v3 the lane in the queue -
	// which the JIT also finds the lane's LDS stack by - v4 in all queues,
	// v[5:6] the page, v[7:8] its xdp_md; v[9:10] are the lanes that have
	// one and v[11:12] the parameter block.
	v_readfirstlane_b32 s[INITIAL_EXEC], v9
	v_readfirstlane_b32 s[INITIAL_EXEC+1], v10
	v_readfirstlane_b32 s[PARAM], v11
	v_readfirstlane_b32 s[PARAM+1], v12
	s_mov_b32 s[WG_Y], s[E_QUEUE]
	v_mov_b32 v[PAGE_IDX], v0
	v_mov_b32 v[PKT_OFF], v1
	v_mov_b32 v[IDX], v4
	v_mov_b32 v[PAGE_BASE], v5
	v_mov_b32 v[PAGE_BASE+1], v6
	v_add_co_u32 v[DATA], vcc, v5, v1
	V_ADD_CI_U32 v[DATA+1], vcc, 0, v6, vcc
	v_add_co_u32 v[DATA_END], vcc, v[DATA], v2
	V_ADD_CI_U32 v[DATA_END+1], vcc, 0, v[DATA+1], vcc
	v_mov_b32 v[CTX], v7
	v_mov_b32 v[CTX+1], v8
	v_mov_b32 v[TID], v3
	v_mov_b32 v[KNOD_BLOB_PRO_LOCAL_IDX_VREG], v3
	v_mov_b32 v[BPF_R1], v7
	v_mov_b32 v[BPF_R1+1], v8
	s_mov_b64 s[DONE_MASK:DONE_MASK+1], 0
	// A wave with none still runs it, with no lanes, to meet the others
	// at gda_round_end()'s barriers.
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]

	s_getpc_b64 s[GT:GT+1]
.Lgda_pc:
	s_add_u32 s[GT], s[GT], .Lgda_program - .Lgda_pc
	s_addc_u32 s[GT+1], s[GT+1], 0
	s_swappc_b64 s[6:7], s[GT:GT+1]

	// Its verdicts, and where the packets are now: a program may have
	// moved their start or end.
	s_mov_b64 exec, -1
	v_mov_b32 v7, v[BPF_R0]
	v_mov_b32 v8, v[PAGE_IDX]
	V_SUB_U32 v9, v[DATA], v[PAGE_BASE]
	V_SUB_U32 v10, v[DATA_END], v[DATA]
	v_mov_b32 v11, s[INITIAL_EXEC]
	v_mov_b32 v12, s[INITIAL_EXEC+1]
	v_mov_b32 v4, v[GS]
	v_mov_b32 v5, v[GS+1]
	v_mov_b32 v6, v[GS+2]
	ENGINE_CALL gda_round_end
	v_mov_b32 v[GS], v0
	v_mov_b32 v[GS+1], v1
	v_mov_b32 v[GS+2], v2
	s_branch .Lgda_round

	CFN_BODY_gda_init
	CFN_BODY_gda_round_begin
	CFN_BODY_gda_round_end
.Lgda_program:
	.size knod_gda_engine, . - knod_gda_engine
	.globl knod_gda_engine_call
	.set knod_gda_engine_call, .Lgda_pc + 4 - knod_gda_engine
