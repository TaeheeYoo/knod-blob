// SPDX-License-Identifier: GPL-2.0-or-later
//
// The prologue body, as a macro, because the pass kernel is the same prologue
// with a fixed verdict in place of a program.

#ifndef PROLOGUE_INC
#define PROLOGUE_INC
#include <linux/knod_persistent.h>
#if KNOD_ISA != 10 && KNOD_ISA != 11
#error "persistent shader requires GFX10/GFX11 Wave64"
#endif

	.macro KNOD_PROLOGUE_BODY
	// A re-uploaded shader can land on the address an old one is still
	// cached at.  This must be the first instruction so that whichever
	// version the SQC is serving runs it before the two diverge.
	//
	// The invalidate runs once when a persistent shader lifetime starts.
	// KNOD_NO_ICACHE_INV builds it out for controlled measurements, and is
	// only safe while nothing re-uploads a shader - no attach, no program
	// change - for the length of the run.
#if !defined(KNOD_NO_ICACHE_INV)
	s_icache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
#endif

	// Protected persistent-launch SGPRs: 0=physical wave TID base,
	// 2=mailbox batch slot, 6:7=loop PC, 8:9=control,
	// 10:11=expected sequence. The BPF body/helpers must not write them.
	// Kernel descriptor enables the stable kernarg pointer in s[8:9].
	v_readfirstlane_b32 s0, v[TID]
	s_and_b32 s0, s0, 1023
	s_mov_b32 s2, 0
	s_mov_b64 s[10:11], 1
	s_waitcnt lgkmcnt(0)
	s_getpc_b64 s[6:7]
.Lpersistent_poll\@:
	s_mov_b64 exec, -1
	v_mov_b32 v[P0], s8
	v_mov_b32 v[P1], s9
	global_load_dwordx2 v[P2:P2+1], v[P0:P0+1], off glc dlc
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s[PS0], v[P2]
	s_cmp_eq_u32 s[PS0], KNOD_PERSIST_VERSION
	s_cbranch_scc0 .Lpersistent_exit\@
	v_readfirstlane_b32 s[PS0], v[P3]
	s_cmp_eq_u32 s[PS0], 0
	s_cbranch_scc0 .Lpersistent_exit\@
	s_lshl_b32 s[PS0], s2, 9
	s_add_u32 s[PS0], s[PS0], KNOD_PERSIST_SLOT_BASE
	v_add_co_u32 v[P0], vcc, s[PS0], v[P0]
	V_ADD_CI_U32 v[P1], vcc, 0, v[P1], vcc
	global_load_dwordx2 v[P2:P2+1], v[P0:P0+1], off glc dlc
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s[PS0], v[P2]
	v_readfirstlane_b32 s[PS1], v[P3]
	s_cmp_eq_u64 s[PS0:PS1], s[10:11]
	s_cbranch_scc1 .Lpersistent_ready\@
	s_sleep 1
	s_branch .Lpersistent_poll\@
.Lpersistent_exit\@:
	s_endpgm
.Lpersistent_ready\@:
#ifdef KNOD_CYCLES
	// Where the shader's time goes.  s_getreg reads a register, so it waits
	// on nothing and adds only its own issue to whichever part it closes.
	//
	// It reads here, past the wait and inside the loop, so that the first
	// part is what a batch costs rather than how long the mailbox sat
	// empty - which no lane spends working, and which a twenty-bit counter
	// would wrap on anyway.
	s_getreg_b32 s[PROBE0], hwreg(HW_REG_SHADER_CYCLES, 0, 20)
#endif
	// Acquire the submitted batch before reading reused parameters/data.
	//
	// One wave does it for the workgroup.  In CU mode all four share the
	// CU's L0 and scalar cache, so three of the invalidates buy nothing,
	// and because each wave arrives on its own reading of the mailbox a
	// late one would throw away what an early one had already fetched.
	// GL1 is wider than the workgroup - a whole shader array - so every
	// extra invalidate is also paid by the queues running beside this one.
	s_cmp_eq_u32 s0, 0
	s_cbranch_scc0 .Lpersistent_acquired\@
	buffer_gl1_inv
	buffer_gl0_inv
	s_dcache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
.Lpersistent_acquired\@:
	// Nobody reads the batch until that invalidate has landed.
	s_barrier
	global_load_dwordx2 v[P2:P2+1], v[P0:P0+1], off offset:KNOD_PERSIST_PARAM glc dlc
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s[PARAM], v[P2]
	v_readfirstlane_b32 s[PARAM+1], v[P3]
	v_mbcnt_lo_u32_b32 v[TID], -1, 0
	v_mbcnt_hi_u32_b32 v[TID], -1, v[TID]
	V_ADD_U32 v[TID], s0, v[TID]

	// The workitem id arrives packed in the low bits of v0.
	v_bfe_u32 v[IDX], v[TID], 0, 10
	s_mov_b32 s[FRAME], 0
	s_waitcnt lgkmcnt(0)

	// Batch/WG sizes and page/ring shifts, in two pairs. Issued but not
	// waited for: the queue address below needs only the param pointer, so
	// the descriptor fetch can be in flight while these land.
	s_load_dwordx2 s[PS2:PS2+1], s[PARAM:PARAM+1], \
		       KNOD_BLOB_PARAM_PACKETS_PER_RXQ
	s_load_dwordx2 s[PS4:PS4+1], s[PARAM:PARAM+1], \
		       KNOD_BLOB_PARAM_PAGE_SHIFT

	// &param->queues[queue_id], where the queue id is workgroup_id_y.
	v_mov_b32 v[P0], s[WG_Y]
	v_mov_b32 v[P0+1], 0
	v_mov_b32 v[P10], v[P0]
	v_mad_u64_u32 v[P0:P0+1], s[PS0:PS0+1], KNOD_BLOB_QUEUE_SIZE, \
		      v[P10], s[PARAM:PARAM+1]
	v_add_co_u32 v[P0], vcc, KNOD_BLOB_PARAM_QUEUES, v[P0]
	V_ADD_CI_U32 v[P0+1], vcc, 0, v[P0+1], vcc

	// The whole descriptor: pool and base addresses, then the count and
	// the ring cursor.
	global_load_dwordx4 v[P2:P2+3], v[P0:P0+1], off
	global_load_dwordx4 v[P6:P6+3], v[P0:P0+1], off \
			    offset:KNOD_BLOB_QUEUE_COUNT

	s_mov_b64 s[DONE_MASK:DONE_MASK+1], 0

	// idx = workgroup_id_x * workgroup_size + workitem_id. The size
	// is a scalar now, so the group id comes across to a VGPR first rather
	// than putting a second scalar on the constant bus.
	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[P14], s[WG_X]
	v_mul_lo_u32 v[P14], s[PS2+1], v[P14]
	V_ADD_U32 v[IDX], v[P14], v[IDX]

	s_waitcnt vmcnt(0)

	// Lanes past this queue's count have no packet.  v_cmpx leaves EXEC
	// narrowed for the rest of the program; the mask is kept so the
	// epilogue can tell which lanes were ever live.
	v_cmpx_lt_u32 v[IDX], v[P6]
	s_mov_b64 s[INITIAL_EXEC:INITIAL_EXEC+1], exec

	// The index within the queue is wanted later, before it is turned into
	// an index across all of them.
	v_mov_b32 v[P18], v[IDX]
	v_mov_b32 v[P14], s[WG_Y]
	v_mul_lo_u32 v[P14], s[PS2], v[P14]
	V_ADD_U32 v[IDX], v[P14], v[IDX]

	// r1 = &param->sub[idx], the xdp_md the program is called with.
	v_mad_u64_u32 v[BPF_R1:BPF_R1+1], s[PS0:PS0+1], KNOD_BLOB_SUB_SIZE, \
		      v[IDX], s[PARAM:PARAM+1]
	v_add_co_u32 v[BPF_R1], vcc, KNOD_BLOB_PARAM_SUB, v[BPF_R1]
	V_ADD_CI_U32 v[BPF_R1+1], vcc, 0, v[BPF_R1+1], vcc
	v_mov_b32 v[CTX], v[BPF_R1]
	v_mov_b32 v[CTX+1], v[BPF_R1+1]

	// r10 starts at the top of the frame.
	v_mov_b32 v[BPF_R10], KNOD_BLOB_BPF_STACK_SIZE
	v_mov_b32 v[BPF_R10+1], 0

	// slot = (ring_start + idx) & ring_mask, and the descriptor for it.
	V_ADD_U32 v[P10], v[P6+2], v[P18]
	v_and_b32 v[P10], v[P6+3], v[P10]
	v_lshlrev_b32 v[SLOT], s[PS4+1], v[P10]
	v_add_co_u32 v[SLOT], vcc, v[SLOT], v[P2]
	V_ADD_CI_U32 v[SLOT+1], vcc, 0, v[P2+1], vcc

	// Which page the packet is on, and where in it.
	global_load_dwordx2 v[P12:P12+1], v[SLOT:SLOT+1], off \
			    offset:KNOD_BLOB_BD_OFF
	s_waitcnt vmcnt(0)

	// data = base + (page_idx << page_shift).  The shift crosses 32 bits,
	// so the high half takes what the low half pushed out.
	v_lshlrev_b32 v[DATA], s[PS4], v[P12+1]
	s_sub_u32 s[PS6], 32, s[PS4]
	v_lshrrev_b32 v[DATA+1], s[PS6], v[P12+1]
	v_add_co_u32 v[DATA], vcc, v[P2+2], v[DATA]
	V_ADD_CI_U32 v[DATA+1], vcc, v[P2+3], v[DATA+1], vcc

	// Keep the page start: adjust_head moves data but not this.
	v_mov_b32 v[PAGE_BASE], v[DATA]
	v_mov_b32 v[PAGE_BASE+1], v[DATA+1]

	// And which page it was, for the epilogue to hand back untouched.
	v_mov_b32 v[PAGE_IDX], v[P12+1]

	// off and len share a dword, off in the low half.
	v_and_b32 v[P16], 0xffff, v[P12]
	v_add_co_u32 v[DATA], vcc, v[P16], v[DATA]
	V_ADD_CI_U32 v[DATA+1], vcc, 0, v[DATA+1], vcc
	v_lshrrev_b32 v[DATA_END], 16, v[P12]
	v_add_co_u32 v[DATA_END], vcc, v[DATA], v[DATA_END]
	V_ADD_CI_U32 v[DATA_END+1], vcc, 0, v[DATA+1], vcc

#ifdef KNOD_CYCLES
	// s32 becomes what the prologue took, s33 when it ended.
	s_getreg_b32 s[PROBE1], hwreg(HW_REG_SHADER_CYCLES, 0, 20)
	s_sub_u32 s[PROBE0], s[PROBE1], s[PROBE0]
#endif
	.endm

#endif /* PROLOGUE_INC */
