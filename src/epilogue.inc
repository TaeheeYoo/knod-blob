// SPDX-License-Identifier: GPL-2.0-or-later
//
// The two epilogue bodies, as macros, so the pass kernel can end the same way
// a translated program does.

#ifndef EPILOGUE_INC
#define EPILOGUE_INC

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

	global_store_dword v[SLOT:SLOT+1], v[BPF_R0], off \
			   offset:KNOD_BLOB_BD_ACT glc slc

#if defined(KNOD_HWID_PROBE)
	// Which compute unit ran this wave, so the host can count how many one
	// dispatch really reached.  The grid asks for a workgroup per queue per
	// xgroup, but where those land is the hardware's choice, and a unit with
	// the registers for several of them may take several.
	//
	// It goes in the half of act nothing reads on this path: the driver
	// takes the verdict as a u32 and the shader stores it as one, so bytes
	// 12..15 are untouched.  IPsec does read them, so this is a BPF-only
	// build.  The register goes by number because the name is not spelled
	// the same everywhere: GCN keeps this in HW_ID, RDNA split it and put
	// the unit in HW_ID1.  They pack the fields differently too, so the
	// host decodes by generation rather than the shader normalising it.
#if KNOD_ISA == 9
	s_getreg_b32 s[PS6], hwreg(4, 0, 32)		// HW_ID
#else
	s_getreg_b32 s[PS6], hwreg(23, 0, 32)		// HW_ID1
#endif
	v_mov_b32 v[P5], s[PS6]
	global_store_dword v[SLOT:SLOT+1], v[P5], off \
			   offset:(KNOD_BLOB_BD_ACT + 4) glc slc
#endif

	// Where the packet sits now.  A program that moved its start or its
	// end moved these registers; one that did not gets back the offset and
	// length the prologue read, so this is worth doing either way.
	V_SUB_U32 v[P4], v[DATA_END], v[DATA]			// len
	v_lshlrev_b32 v[P4], 16, v[P4]
	V_SUB_U32 v[P5], v[DATA], v[PAGE_BASE]			// off
	v_or_b32 v[P4], v[P4], v[P5]
	global_store_dword v[SLOT:SLOT+1], v[P4], off \
			   offset:KNOD_BLOB_BD_OFF glc slc
	.endm

	.macro KNOD_EPILOGUE_POST_BODY
	v_cmp_eq_u32 vcc, XDP_PASS, v[BPF_R0]
	s_cbranch_vccz .Lend\@
	s_and_b64 exec, exec, vcc

	// &param->pass_count[queue_id]
	v_mov_b32 v[P18], s[PARAM]
	v_mov_b32 v[P18+1], s[PARAM+1]
	v_mov_b32 v[BPF_R1], s[WG_Y]
	v_lshlrev_b32 v[BPF_R1], 2, v[BPF_R1]
	V_ADD_U32 v[P18], v[BPF_R1], v[P18]

	// Claim a slot each.  One lane claims them all: the counter is one
	// dword per queue in host memory, so a lane apiece meant every lane of
	// the wave taking its turn at the same address across the bus, one
	// after another, with the wait below exposing all of it.  It was
	// two thirds of the shader's time.
	//
	// mbcnt gives a lane how many live lanes are below it, which is both
	// its place in the run this wave is claiming and, for the lowest one,
	// zero - so the same number elects the lane that does the claiming.
	v_mbcnt_lo_u32_b32 v[P20], exec_lo, 0
	v_mbcnt_hi_u32_b32 v[P20], exec_hi, v[P20]
	s_bcnt1_i32_b64 s[PS0], exec

	s_mov_b64 s[PS2:PS2+1], exec
	v_cmp_eq_u32 vcc, 0, v[P20]
	s_and_b64 exec, exec, vcc

	v_mov_b32 v[P22], s[PS0]
	global_atomic_add v[P22], v[P18:P18+1], v[P22], off \
			  offset:KNOD_BLOB_PARAM_PASS_COUNT glc
	s_waitcnt vmcnt(0)

	// What came back is where the run starts.  readfirstlane takes it from
	// the one lane still live, before the rest come back to add their place
	// to it.
	v_readfirstlane_b32 s[PS4], v[P22]
	s_mov_b64 exec, s[PS2:PS2+1]
	V_ADD_U32 v[P20], s[PS4], v[P20]

	// param->pass_indices[slot] = this descriptor.  Indices are shorts, so
	// the slot is doubled to reach one - and doubled again below, where the
	// same number picks the delivery slot.
	V_SUB_U32 v[P18], v[P18], v[BPF_R1]
	v_lshlrev_b32 v[P20], 1, v[P20]
	V_ADD_U32 v[P18], v[P20], v[P18]
	global_store_short v[P18:P18+1], v[IDX], off \
			   offset:KNOD_BLOB_PARAM_PASS_INDICES glc slc

	// What used to follow wrote a {length, address} header into a delivery
	// slot for this queue.  Nothing reads it: PASS goes to the host through
	// the act handler and knod_d2h_copy, which take both from the
	// descriptor.  So it was two stores and a scalar load waited on, per
	// passing lane, into memory no one looks at.

.Lend\@:
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
	s_endpgm
	.endm

#endif /* EPILOGUE_INC */
