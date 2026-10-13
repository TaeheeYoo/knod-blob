// SPDX-License-Identifier: GPL-2.0-or-later
//
// The kernel every GDA shader starts in, around the engine in csrc/: gda_init()
// once, then every round gda_round_begin(), the program and gda_round_end().
// The program goes right after it, and the call's offset to it is
// <engine>_call's to patch.
//
// A macro, for one engine per NIC format and kind: GDA_ENGINE starts one on
// the functions of csrc/gda_<nic>.c, the bodies of those go after it, and
// GDA_ENGINE_END ends it where the program goes.  An ordered engine runs the
// program once per rank of a flow's packets (gda_rank()).
//
// Between calls this keeps the control block, the queue and the wave in
// KNOD_BLOB_ENGINE_SREG, which a program leaves alone and the functions keep,
// and the engine's state in v[GS:GS+2], which neither reaches.

#ifndef KNOD_GDA_ENGINE_INC
#define KNOD_GDA_ENGINE_INC

#include "common.inc"
#include <linux/knod_persistent.h>

#define GS		KNOD_BLOB_PRO_GDA_VREG
#define RANK		KNOD_BLOB_PRO_RANK_VREG	// each packet's, and flags
#define PASS		KNOD_BLOB_PRO_PASS_SREG	// the rank being run
#define PASSES		(PASS + 1)		// the round's highest parked
#define GT		24		// s[24:25], which nothing holds here
#define E_CTL		(KNOD_BLOB_ENGINE_SREG + 0)	// s[100:101]
#define E_QUEUE		KNOD_BLOB_PRO_QUEUE_SREG	// s102
#define E_WAVE		(KNOD_BLOB_ENGINE_SREG + 3)	// s103
#define RET		KNOD_BLOB_PRO_RET_SREG

#if E_QUEUE != KNOD_BLOB_ENGINE_SREG + 2
# error "the queue is the engine's third scalar"
#endif

// Call cfn_\fn with the control block, queue and wave as its first three
// arguments; the rest are the caller's to put in v4 up.
.macro	ENGINE_CALL fn
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
.endm

// @engine is the symbol, @nic the functions' suffix, @ordered 1 for an
// ordered engine.
.macro GDA_ENGINE engine, nic, ordered
	.text
	.globl \engine
	.p2align 2
\engine:
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
	v_readfirstlane_b32 s[E_WAVE], v0
	s_and_b32 s[E_WAVE], s[E_WAVE], 1023
	s_lshr_b32 s[E_WAVE], s[E_WAVE], 6
	s_mov_b32 s[E_QUEUE], s13
	s_mov_b64 s[E_CTL:E_CTL+1], s[8:9]

	ENGINE_CALL gda_init_\nic
	v_mov_b32 v[GS], v0
	v_mov_b32 v[GS+1], v1
	v_mov_b32 v[GS+2], v2

.Lgda_round_\engine:
	v_mov_b32 v4, v[GS]
	v_mov_b32 v5, v[GS+1]
	v_mov_b32 v6, v[GS+2]
	.if \ordered
	ENGINE_CALL gda_round_begin_ordered_\nic
.else
	ENGINE_CALL gda_round_begin_\nic
.endif
	v_mov_b32 v[GS], v13
	v_mov_b32 v[GS+1], v14
	v_mov_b32 v[GS+2], v15
.if \ordered
	// The ranks out of the page's top half.
	v_lshrrev_b32 v[RANK], 16, v0
	v_and_b32 v0, 0xffff, v0
.endif

	// What the program reads its packets from.  v0 is the page, v1 the
	// packet's offset in it, v2 its length, v3 the lane in the queue -
	// which the JIT also finds the lane's LDS stack by - v4 in all queues,
	// v[5:6] the page, v[7:8] its xdp_md; v[9:10] are the lanes that have
	// one and v[11:12] the parameter block.
	v_readfirstlane_b32 s[INITIAL_EXEC], v9
	v_readfirstlane_b32 s[INITIAL_EXEC+1], v10
	v_readfirstlane_b32 s[PARAM], v11
	v_readfirstlane_b32 s[PARAM+1], v12
	s_mov_b32 s13, s[E_QUEUE]
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
	v_mov_b32 v[KNOD_BLOB_PRO_LOCAL_IDX_VREG], v3
	v_mov_b32 v[BPF_R1], v7
	v_mov_b32 v[BPF_R1+1], v8
.if \ordered
	// The first pass is every lane's; the program parks the ones that
	// wait for their flow.
	s_mov_b32 s[PASS], 0
	s_branch .Lgda_call_\engine
.Lgda_pass_\engine:
	// Then, a rank at a time, the parked lanes of that rank, where the
	// program resumes them; the others keep what their pass left in
	// their registers.  The lane's index goes back in: the last pass's
	// calls may have used the register.
	s_barrier
	buffer_gl0_inv
	buffer_gl1_inv
	s_mov_b64 exec, -1
	v_and_b32 v0, KNOD_BLOB_RANK_MASK | KNOD_BLOB_RANK_PARKED, v[RANK]
	s_or_b32 s[GT], s[PASS], KNOD_BLOB_RANK_PARKED
	v_cmp_eq_u32 vcc, s[GT], v0
	// SCC is whether any lane is, and VCC is not read straight after the
	// compare that wrote it.
	s_and_b64 s[INITIAL_EXEC:INITIAL_EXEC+1], vcc, exec
	s_cbranch_scc0 .Lgda_ran_\engine
	v_mbcnt_lo_u32_b32 v[KNOD_BLOB_PRO_LOCAL_IDX_VREG], -1, 0
	v_mbcnt_hi_u32_b32 v[KNOD_BLOB_PRO_LOCAL_IDX_VREG], -1, \
			   v[KNOD_BLOB_PRO_LOCAL_IDX_VREG]
	s_lshl_b32 s[GT], s[E_WAVE], 6
	v_add_nc_u32 v[KNOD_BLOB_PRO_LOCAL_IDX_VREG], s[GT], \
		  v[KNOD_BLOB_PRO_LOCAL_IDX_VREG]
.Lgda_call_\engine:
.endif
	s_mov_b64 s[DONE_MASK:DONE_MASK+1], 0
	// A wave with none still runs it, with no lanes, to meet the others
	// at gda_round_end()'s barriers.
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]

	s_getpc_b64 s[GT:GT+1]
.Lgda_pc_\engine:
	s_add_u32 s[GT], s[GT], .Lgda_program_\engine - .Lgda_pc_\engine
	s_addc_u32 s[GT+1], s[GT+1], 0
	// s32 is still the stack every call gets: the functions keep it.
	s_swappc_b64 s[RET:RET+1], s[GT:GT+1]

.if \ordered
.Lgda_ran_\engine:
	// After the first pass, how many more the parked lanes want.  The
	// program waited for its stores before returning; once every wave is
	// at the next pass's barrier, all of this one's are in L2, and the
	// next reads past the caches in front of it.
	s_cmp_lg_u32 s[PASS], 0
	s_cbranch_scc1 .Lgda_counted_\engine
	// Every lane's: the state is a field a lane, and the program left
	// EXEC at its own.
	s_mov_b64 exec, -1
	v_mov_b32 v4, v[GS]
	v_mov_b32 v5, v[RANK]
	ENGINE_CALL gda_parked_\nic
	// The parked lanes' ranks, now worked out, and the passes they want.
	v_mov_b32 v[RANK], v0
	v_readfirstlane_b32 s[PASSES], v1
.Lgda_counted_\engine:
	s_cmp_ge_u32 s[PASS], s[PASSES]
	s_cbranch_scc1 .Lgda_passed_\engine
	s_add_u32 s[PASS], s[PASS], 1
	s_branch .Lgda_pass_\engine
.Lgda_passed_\engine:
	// Every lane with a packet, for gda_round_end().
	s_mov_b64 exec, -1
	v_and_b32 v0, KNOD_BLOB_RANK_MASK, v[RANK]
	s_mov_b32 s[GT], KNOD_BLOB_RANK_NONE
	v_cmp_ne_u32 vcc, s[GT], v0
	s_mov_b64 s[INITIAL_EXEC:INITIAL_EXEC+1], vcc
.endif

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
	ENGINE_CALL gda_round_end_\nic
	v_mov_b32 v[GS], v0
	v_mov_b32 v[GS+1], v1
	v_mov_b32 v[GS+2], v2
	s_branch .Lgda_round_\engine
.endm

.macro GDA_ENGINE_END engine
.Lgda_program_\engine:
	.size \engine, . - \engine
	.globl \engine\()_call
	.set \engine\()_call, .Lgda_pc_\engine + 4 - \engine
.endm

#define GDA_CAT_(a, b)	a##b
#define GDA_CAT(a, b)	GDA_CAT_(a, b)

#endif
