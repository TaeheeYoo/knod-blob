// SPDX-License-Identifier: GPL-2.0-or-later
//
// The two epilogue bodies, as macros, so the pass kernel can end the same way
// a translated program does.

#ifndef EPILOGUE_INC
#define EPILOGUE_INC
#include <linux/knod_persistent.h>

	.macro KNOD_EPILOGUE_PRE_BODY
#ifdef KNOD_CYCLES
	// The program is over, so its count is known and both finished counts
	// can go away; s33 then times the epilogue.  EXEC is whatever the
	// program left, and every in-bounds lane has a count to report.
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]
	s_getreg_b32 s[PS0], hwreg(HW_REG_SHADER_CYCLES, 0, 20)
	s_sub_u32 s[PS1], s[PS0], s[PROBE1]
	v_mov_b32 v[P0], s[PROBE0]
	v_mov_b32 v[P0+1], s[PS1]
	global_store_dwordx2 v[SLOT:SLOT+1], v[P0:P0+1], off \
			     offset:KNOD_BLOB_BD_PROBE
	s_mov_b32 s[PROBE1], s[PS0]
#endif

	s_andn2_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1], \
		    s[DONE_MASK:DONE_MASK+1]
	v_mov_b32 v[BPF_R0], XDP_DROP
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]

	// Where the packet sits now.  A program that moved its start or its end
	// moved these registers; one that did not gets back what the prologue
	// read, so this is worth writing either way.
	V_SUB_U32 v[P6], v[DATA_END], v[DATA]			// len
	v_lshlrev_b32 v[P6], 16, v[P6]
	V_SUB_U32 v[P7], v[DATA], v[PAGE_BASE]			// off
	v_or_b32 v[P6], v[P6], v[P7]

	// The verdict, the bounds, and the page they are on, in one store.
	// They sit in the descriptor eight bytes apart with the half of act
	// nothing reads between them, so four dwords covers all three - and the
	// page goes back unchanged only to make that possible.
	//
	// One store rather than two because that is what a batch costs:
	// stores are posted, so a wave never waits for one, but the release
	// that completes the batch does, and until then they keep batches from
	// overlapping.  Dropping one of two took the drop path from 27.4 Mpps
	// to 48.2.
	v_mov_b32 v[P4], v[BPF_R0]
	v_mov_b32 v[P5], 0
	v_mov_b32 v[P7], v[PAGE_IDX]
	global_store_dwordx4 v[SLOT:SLOT+1], v[P4:P4+3], off \
			     offset:KNOD_BLOB_BD_ACT glc slc

#if defined(KNOD_HWID_PROBE)
	// Which compute unit ran this wave, so the host can count how many one
	// batch really reached.  The grid asks for a workgroup per queue per
	// xgroup, but where those land is the hardware's choice, and a unit with
	// the registers for several of them may take several.
	//
	// It goes in the half of act nothing reads on this path: the driver
	// takes the verdict as a u32 and the shader stores it as one, so bytes
	// 12..15 are untouched. The register goes by number because RDNA names
	// it HW_ID1. Its fields are decoded by generation on the host.
	s_getreg_b32 s[PS6], hwreg(23, 0, 32)		// HW_ID1
	v_mov_b32 v[P5], s[PS6]
	global_store_dword v[SLOT:SLOT+1], v[P5], off \
			   offset:(KNOD_BLOB_BD_ACT + 4) glc slc
#endif

	.endm

	.macro KNOD_EPILOGUE_POST_BODY
#ifdef KNOD_CYCLES
	// Both paths arrive here - the one that passed by falling through, the
	// one that did not by the branch - so this closes the epilogue for
	// every lane.  The store is past the last reading and prices itself
	// out of what it reports.
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]
	s_getreg_b32 s[PS0], hwreg(HW_REG_SHADER_CYCLES, 0, 20)
	s_sub_u32 s[PS1], s[PS0], s[PROBE1]
	v_mov_b32 v[P0], s[PS1]
	global_store_dword v[SLOT:SLOT+1], v[P0], off \
			   offset:(KNOD_BLOB_BD_PROBE + 8)
#endif
	// Each physical wave finishes its packet writes before WG convergence.
	// Empty packet waves also arrive here; INITIAL_EXEC is not a barrier mask.
	s_waitcnt vmcnt(0) lgkmcnt(0)
	s_waitcnt_vscnt null, 0
	s_mov_b64 exec, -1
	s_barrier
	s_cmp_eq_u32 s0, 0
	s_cbranch_scc0 .Lpersistent_next\@
	s_mov_b64 exec, 1
	// One writer per WG and mailbox batch slot; no system atomic counter.
	s_lshl_b32 s[PS0], s[WG_Y], 3
	s_lshl_b32 s[PS1], s2, 9
	s_add_u32 s[PS0], s[PS0], s[PS1]
	s_add_u32 s[PS0], s[PS0], (KNOD_PERSIST_SLOT_BASE + KNOD_PERSIST_DONE)
	v_mov_b32 v[P0], s8
	v_mov_b32 v[P1], s9
	v_add_co_u32 v[P0], vcc, s[PS0], v[P0]
	V_ADD_CI_U32 v[P1], vcc, 0, v[P1], vcc
	v_mov_b32 v[P2], s10
	v_mov_b32 v[P3], s11
	global_store_dwordx2 v[P0:P0+1], v[P2:P2+1], off glc slc
	s_waitcnt_vscnt null, 0
.Lpersistent_next\@:
	s_add_u32 s10, s10, 1
	s_addc_u32 s11, s11, 0
	s_add_u32 s2, s2, 1
	s_cmp_eq_u32 s2, KNOD_PERSIST_SLOTS
	s_cselect_b32 s2, 0, s2
	s_setpc_b64 s[6:7]
	.endm

#endif /* EPILOGUE_INC */
