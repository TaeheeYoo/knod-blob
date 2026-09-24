// SPDX-License-Identifier: GPL-2.0-or-later
//
// GDA stage 2: what wraps a program when the shader runs the NIC's receive
// rings itself.  The prologue sets up exactly the registers
// KNOD_PROLOGUE_BODY sets up - the program cannot tell the difference - but
// its packets come from the NIC's CQ rather than a batch the host built; the
// epilogue hands every entry the program saw back to the NIC's RQ.
//
// One wave per queue, KNOD_GDA_LANES packets a round.  The rest of the
// workgroup ends at once.
//
// Across a program only s0, s2, s[6:7], s[8:9], s[10:11] and the named
// registers survive; here that is s2 = the CQ consumer index and s[10:11] =
// the ring buffer, with v73 = the RQ's size and v[74:75] = the packet count,
// in the part of the last VGPR granule no program reaches.  Everything else
// is read from the queue's control entry once a round.

#ifndef GDA_INC
#define GDA_INC
#include <linux/knod_persistent.h>

#define GDA_RQ_SIZE	73
#define GDA_PKTS	74	// v[74:75]
#define KNOD_GDA_LANES	64
#define GT		(KNOD_BLOB_PRO_TMP_SREG + 10)	// s[24:25]

// v[P0:P1] = this queue's control entry.  Scalar temps: PS0, PS1.
	.macro GDA_ENTRY_ADDR
	s_mul_i32 s[PS0], s[WG_Y], KNOD_PERSIST_GDA_BYTES
	s_add_u32 s[PS0], s[PS0], KNOD_PERSIST_GDA
	v_mov_b32 v[P0], s8
	v_mov_b32 v[P1], s9
	v_add_co_u32 v[P0], vcc, s[PS0], v[P0]
	V_ADD_CI_U32 v[P1], vcc, 0, v[P1], vcc
	.endm

// Park while the host changes maps, or leave if it wants the shader gone.
// Expects v[P0:P1] = the entry and v[P4:P5] = the control block's version and
// stop, v[P6] its pause, as read this round.  Lane 0.
	.macro GDA_PARK_OR_STOP
	v_readfirstlane_b32 s[PS0], v[P4]
	s_cmp_eq_u32 s[PS0], KNOD_PERSIST_VERSION
	s_cbranch_scc0 .Lgda_stop\@
	v_readfirstlane_b32 s[PS0], v[P5]
	s_cmp_eq_u32 s[PS0], 0
	s_cbranch_scc0 .Lgda_stop\@
	v_readfirstlane_b32 s[PS1], v[P6]
	s_cmp_eq_u32 s[PS1], 0
	s_cbranch_scc1 .Lgda_parked\@
	// Say where the CQ got to, then that this queue is parked: the host may
	// read anything once it sees the ack.
	v_mov_b32 v[P7], s2
	global_store_dword v[P0:P1], v[P7], off \
			   offset:KNOD_PERSIST_GDA_CI glc slc
	s_waitcnt_vscnt null, 0
	global_store_dword v[P0:P1], v[P6], off \
			   offset:KNOD_PERSIST_GDA_PAUSE_ACK glc slc
	s_waitcnt_vscnt null, 0
.Lgda_wait\@:
	s_sleep 8
	v_mov_b32 v[P2], s8
	v_mov_b32 v[P3], s9
	global_load_dwordx4 v[P4:P4+3], v[P2:P3], off glc dlc
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s[PS0], v[P4]
	s_cmp_eq_u32 s[PS0], KNOD_PERSIST_VERSION
	s_cbranch_scc0 .Lgda_stop\@
	v_readfirstlane_b32 s[PS0], v[P5]
	s_cmp_eq_u32 s[PS0], 0
	s_cbranch_scc0 .Lgda_stop\@
	v_readfirstlane_b32 s[PS1], v[P6]
	s_cmp_eq_u32 s[PS1], 0
	s_cbranch_scc0 .Lgda_wait\@
	s_branch .Lgda_parked\@
.Lgda_stop\@:
	// Where the CQ got to, for whichever shader comes next.
	v_mov_b32 v[P7], s2
	global_store_dword v[P0:P1], v[P7], off \
			   offset:KNOD_PERSIST_GDA_CI glc slc
	s_waitcnt_vscnt null, 0
	s_endpgm
.Lgda_parked\@:
	.endm

	.macro KNOD_GDA_PROLOGUE_BODY
#if !defined(KNOD_NO_ICACHE_INV)
	s_icache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
#endif
	// One wave per queue.
	v_readfirstlane_b32 s0, v[TID]
	s_and_b32 s0, s0, 1023
	s_cmp_eq_u32 s0, 0
	s_cbranch_scc0 .Lgda_end\@

	s_mov_b64 exec, 1
	GDA_ENTRY_ADDR
	global_load_dwordx4 v[P2:P2+3], v[P0:P1], off glc dlc	// ring, rx_dma
	global_load_dwordx4 v[P6:P6+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_RQ_LOG glc dlc
	global_load_dwordx4 v[P12:P12+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_HEADROOM glc dlc
	global_load_dwordx2 v[P16:P16+1], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_CI glc dlc
	global_load_dwordx2 v[P20:P20+1], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_PACKETS glc dlc
	s_waitcnt vmcnt(0)
	// Not a queue whose rings the NIC gave us.
	v_readfirstlane_b32 s[PS0], v[P12+2]
	s_cmp_eq_u32 s[PS0], 0
	s_cbranch_scc1 .Lgda_end\@

	v_readfirstlane_b32 s10, v[P2]
	v_readfirstlane_b32 s11, v[P2+1]
	s_mov_b64 exec, -1
	v_readfirstlane_b32 s[PS0], v[P6]		// rq_log
	s_lshl_b32 s[PS0], 1, s[PS0]
	v_mov_b32 v[GDA_RQ_SIZE], s[PS0]
	v_readfirstlane_b32 s[PS0], v[P20]
	v_readfirstlane_b32 s[PS1], v[P20+1]
	v_mov_b32 v[GDA_PKTS], s[PS0]
	v_mov_b32 v[GDA_PKTS+1], s[PS1]

	// Posted for this build of the rings already: pick up where the last
	// shader left the CQ.  Otherwise post every RQ entry, entry i to RX
	// page i - a round only ever gives an entry back as it was.
	v_readfirstlane_b32 s2, v[P16]			// ci
	v_readfirstlane_b32 s[PS0], v[P16+1]		// posted_gen
	v_readfirstlane_b32 s[PS1], v[P12+3]		// gen
	s_cmp_eq_u32 s[PS0], s[PS1]
	s_cbranch_scc1 .Lgda_posted\@

	v_mbcnt_lo_u32_b32 v[P10], -1, 0
	v_mbcnt_hi_u32_b32 v[P10], -1, v[P10]
	v_readfirstlane_b32 s[PS2], v[P6+1]		// log2 entry bytes
	v_readfirstlane_b32 s[PS2+1], v[P6+3]		// byte count
	v_readfirstlane_b32 s[PS4], v[P12]		// headroom
	v_readfirstlane_b32 s[PS4+1], v[P12+1]		// key, big endian
	v_readfirstlane_b32 s[PS6], v[P2+2]		// rx_dma
	v_readfirstlane_b32 s[PS6+1], v[P2+3]
	v_readfirstlane_b32 s[PS6+2], v[GDA_RQ_SIZE]
	s_mov_b32 s[PS0], 0
.Lgda_post\@:
	V_ADD_U32 v[P8], s[PS0], v[P10]			// entry
	v_lshlrev_b32 v[P9], 3, v[P8]
	v_add_co_u32 v[P20], vcc, s[PS6], v[P9]
	v_mov_b32 v[P20+1], s[PS6+1]
	V_ADD_CI_U32 v[P20+1], vcc, 0, v[P20+1], vcc
	global_load_dwordx2 v[P22:P22+1], v[P20:P20+1], off
	v_lshlrev_b32 v[P9], s[PS2], v[P8]
	v_add_co_u32 v[P9], vcc, KNOD_PERSIST_RING_RQ_OFF, v[P9]
	v_add_co_u32 v[P20], vcc, s10, v[P9]
	v_mov_b32 v[P20+1], s11
	V_ADD_CI_U32 v[P20+1], vcc, 0, v[P20+1], vcc
	s_waitcnt vmcnt(0)
	v_add_co_u32 v[P22], vcc, s[PS4], v[P22]
	V_ADD_CI_U32 v[P22+1], vcc, 0, v[P22+1], vcc
	v_mov_b32 v[P24], s[PS2+1]
	v_perm_b32 v[P24], v[P24], v[P24], 0x00010203
	v_mov_b32 v[P24+1], s[PS4+1]
	v_perm_b32 v[P24+2], v[P22+1], v[P22+1], 0x00010203
	v_perm_b32 v[P24+3], v[P22], v[P22], 0x00010203
	global_store_dwordx4 v[P20:P20+1], v[P24:P24+3], off glc slc
	s_add_u32 s[PS0], s[PS0], 64
	s_cmp_lt_u32 s[PS0], s[PS6+2]
	s_cbranch_scc1 .Lgda_post\@
	s_waitcnt_vscnt null, 0
	// Up for the NIC, and recorded as posted for this build.
	s_mov_b32 s2, 0
	s_mov_b64 exec, 1
	v_perm_b32 v[P8], v[GDA_RQ_SIZE], v[GDA_RQ_SIZE], 0x00010203
	v_mov_b32 v[P20], s10
	v_mov_b32 v[P20+1], s11
	global_store_dword v[P20:P20+1], v[P8], off \
			   offset:KNOD_PERSIST_RING_RQ_DB glc slc
	v_mov_b32 v[P8], 0
	v_mov_b32 v[P9], v[P12+3]
	global_store_dwordx2 v[P0:P1], v[P8:P9], off \
			     offset:KNOD_PERSIST_GDA_CI glc slc
	s_waitcnt_vscnt null, 0
.Lgda_posted\@:
	s_getpc_b64 s[6:7]

	// ---- a round ----
	// This round's view of the control block and the queue's entry, then
	// the CQ.  Lane 0 for the first two; the CQ is lane-parallel.
	s_mov_b64 exec, 1
	GDA_ENTRY_ADDR
	v_mov_b32 v[P2], s8
	v_mov_b32 v[P3], s9
	global_load_dwordx4 v[P4:P4+3], v[P2:P3], off glc dlc	// version, stop, pause
	global_load_dwordx2 v[P8:P9], v[P2:P3], off \
			    offset:KNOD_PERSIST_GDA_PARAM glc dlc
	global_load_dwordx4 v[P12:P12+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_RQ_LOG glc dlc	// rq_log, stride, cq_log, frag
	global_load_dwordx4 v[P16:P16+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_HEADROOM glc dlc	// headroom, ...
	global_load_dwordx4 v[P20:P20+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_RX_BASE glc dlc	// rx_base, bds
	s_waitcnt vmcnt(0)
	GDA_PARK_OR_STOP
	v_readfirstlane_b32 s[PARAM], v[P8]
	v_readfirstlane_b32 s[PARAM+1], v[P9]
	v_readfirstlane_b32 s[PS2], v[P12]		// rq_log
	v_readfirstlane_b32 s[PS2+1], v[P12+2]		// cq_log
	v_readfirstlane_b32 s[PS4], v[P16]		// headroom
	v_readfirstlane_b32 s[PS4+1], v[P20]		// rx_base
	v_readfirstlane_b32 s[PS6], v[P20+1]
	v_readfirstlane_b32 s[PS6+1], v[P20+2]		// bds
	v_readfirstlane_b32 s[PS6+2], v[P20+3]

	// Lane i reads CQE ci + i: its byte count, and its last dword - the
	// RQ entry it completes, its opcode and its owner bit.
	s_mov_b64 exec, -1
	v_mbcnt_lo_u32_b32 v[P10], -1, 0
	v_mbcnt_hi_u32_b32 v[P10], -1, v[P10]
	V_ADD_U32 v[P10+1], s2, v[P10]			// position
	s_lshl_b32 s[PS0], 1, s[PS2+1]
	s_sub_u32 s[PS0], s[PS0], 1
	v_and_b32 v[P12], s[PS0], v[P10+1]
	v_lshlrev_b32 v[P12], 6, v[P12]
	v_add_co_u32 v[P12], vcc, KNOD_PERSIST_RING_CQ_OFF, v[P12]
	v_add_co_u32 v[P24], vcc, s10, v[P12]
	v_mov_b32 v[P24+1], s11
	V_ADD_CI_U32 v[P24+1], vcc, 0, v[P24+1], vcc
	global_load_dword v[P28], v[P24:P24+1], off offset:44 glc dlc
	global_load_dword v[P28+1], v[P24:P24+1], off offset:60 glc dlc
	s_waitcnt vmcnt(0)
	// Ours when the owner bit is the pass round the ring the position is
	// on and the opcode is not the invalid one a fresh ring holds.
	v_lshrrev_b32 v[P12+1], 24, v[P28+1]		// op_own
	v_lshrrev_b32 v[P14], s[PS2+1], v[P10+1]
	v_and_b32 v[P14], 1, v[P14]
	v_and_b32 v[P14+1], 1, v[P12+1]
	v_cmp_eq_u32_e64 s[PS0:PS1], v[P14], v[P14+1]
	v_lshrrev_b32 v[P12+1], 4, v[P12+1]
	v_cmp_ne_u32_e64 s[GT:GT+1], 15, v[P12+1]
	s_and_b64 s[PS0:PS1], s[PS0:PS1], s[GT:GT+1]
	// Only the unbroken run from the head is this round's.
	s_not_b64 s[GT:GT+1], s[PS0:PS1]
	s_ff1_i32_b64 s[PS6+3], s[GT:GT+1]
	s_cmp_lt_i32 s[PS6+3], 0
	s_cselect_b32 s[PS6+3], 64, s[PS6+3]
	s_cmp_eq_u32 s[PS6+3], 0
	s_cbranch_scc0 .Lgda_work\@
	s_sleep 1
	s_setpc_b64 s[6:7]
.Lgda_work\@:
	// Maps the host changed while we were parked, and anything else a
	// program reads that someone else writes.
	buffer_gl1_inv
	buffer_gl0_inv
	s_dcache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
	s_load_dword s[GT], s[PARAM:PARAM+1], KNOD_BLOB_PARAM_PAGE_SHIFT

	s_bfm_b64 s[PS0:PS1], s[PS6+3], 0
	s_cmp_eq_u32 s[PS6+3], 64
	s_cselect_b64 s[PS0:PS1], -1, s[PS0:PS1]
	s_mov_b64 exec, s[PS0:PS1]
	s_mov_b64 s[INITIAL_EXEC:INITIAL_EXEC+1], exec
	s_mov_b64 s[DONE_MASK:DONE_MASK+1], 0

	// The RQ entry this CQE completes, and so the page: entry i is page i.
	v_perm_b32 v[P12+1], v[P28+1], v[P28+1], 0x0c0c0001
	s_lshl_b32 s[PS0], 1, s[PS2]
	s_sub_u32 s[PS0], s[PS0], 1
	v_and_b32 v[PAGE_IDX], s[PS0], v[P12+1]
	v_perm_b32 v[P14], v[P28], v[P28], 0x00010203	// byte count

	// page base = rx_base + (page << page_shift); data past the headroom.
	s_waitcnt lgkmcnt(0)
	s_sub_u32 s[GT+1], 32, s[GT]
	v_lshlrev_b32 v[P14+1], s[GT], v[PAGE_IDX]
	v_lshrrev_b32 v[P16], s[GT+1], v[PAGE_IDX]
	v_add_co_u32 v[PAGE_BASE], vcc, s[PS4+1], v[P14+1]
	v_mov_b32 v[PAGE_BASE+1], s[PS6]
	V_ADD_CI_U32 v[PAGE_BASE+1], vcc, v[P16], v[PAGE_BASE+1], vcc
	v_add_co_u32 v[DATA], vcc, s[PS4], v[PAGE_BASE]
	V_ADD_CI_U32 v[DATA+1], vcc, 0, v[PAGE_BASE+1], vcc
	v_add_co_u32 v[DATA_END], vcc, v[DATA], v[P14]
	V_ADD_CI_U32 v[DATA_END+1], vcc, 0, v[DATA+1], vcc

	// The descriptor a program's bounds are read from: offset and length
	// sharing a dword, then the page.
	v_lshlrev_b32 v[P16+1], 6, v[P10]
	v_add_co_u32 v[SLOT], vcc, s[PS6+1], v[P16+1]
	v_mov_b32 v[SLOT+1], s[PS6+2]
	V_ADD_CI_U32 v[SLOT+1], vcc, 0, v[SLOT+1], vcc
	v_lshlrev_b32 v[P20], 16, v[P14]
	v_or_b32 v[P20], s[PS4], v[P20]
	v_mov_b32 v[P20+1], v[PAGE_IDX]
	global_store_dwordx2 v[SLOT:SLOT+1], v[P20:P20+1], off \
			     offset:KNOD_BLOB_BD_OFF glc slc
	s_waitcnt_vscnt null, 0

	// The rest of what KNOD_PROLOGUE_BODY leaves the program: which lane
	// of which queue, r1 = &param->sub[idx], r10 at the top of the frame.
	v_mov_b32 v[TID], v[P10]
	v_mov_b32 v[P18], v[P10]
	s_mul_i32 s[PS0], s[WG_Y], KNOD_GDA_LANES
	V_ADD_U32 v[IDX], s[PS0], v[P10]
	v_mad_u64_u32 v[BPF_R1:BPF_R1+1], s[PS0:PS0+1], KNOD_BLOB_SUB_SIZE, \
		      v[IDX], s[PARAM:PARAM+1]
	v_add_co_u32 v[BPF_R1], vcc, KNOD_BLOB_PARAM_SUB, v[BPF_R1]
	V_ADD_CI_U32 v[BPF_R1+1], vcc, 0, v[BPF_R1+1], vcc
	v_mov_b32 v[CTX], v[BPF_R1]
	v_mov_b32 v[CTX+1], v[BPF_R1+1]
	v_mov_b32 v[BPF_R10], KNOD_BLOB_BPF_STACK_SIZE
	v_mov_b32 v[BPF_R10+1], 0
	s_mov_b32 s[FRAME], 0
	s_branch .Lgda_go\@
.Lgda_end\@:
	s_endpgm
.Lgda_go\@:
	.endm

	.macro KNOD_GDA_EPILOGUE_BODY
	// A lane that did not return drops, as in KNOD_EPILOGUE_PRE_BODY.
	s_andn2_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1], \
		    s[DONE_MASK:DONE_MASK+1]
	v_mov_b32 v[BPF_R0], XDP_DROP
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]

	// Whatever the program wrote - maps above all - is out before the round
	// ends: parking is where the host reads and changes them.
	s_waitcnt vmcnt(0) lgkmcnt(0)
	s_waitcnt_vscnt null, 0

	// M2a: every verdict hands its RQ entry straight back, as a drop does.
	s_bcnt1_i32_b64 s[PS0], s[INITIAL_EXEC:INITIAL_EXEC+1]
	s_add_u32 s2, s2, s[PS0]
	s_mov_b64 exec, 1
	v_add_co_u32 v[GDA_PKTS], vcc, s[PS0], v[GDA_PKTS]
	V_ADD_CI_U32 v[GDA_PKTS+1], vcc, 0, v[GDA_PKTS+1], vcc

	// The CQ entries back, then the RQ entries: producer = ci + RQ size.
	v_mov_b32 v[P2], s2
	v_and_b32 v[P2], 0xffffff, v[P2]
	v_perm_b32 v[P2], v[P2], v[P2], 0x00010203
	V_ADD_U32 v[P3], s2, v[GDA_RQ_SIZE]
	v_and_b32 v[P3], 0xffff, v[P3]
	v_perm_b32 v[P3], v[P3], v[P3], 0x00010203
	v_mov_b32 v[P4], s10
	v_mov_b32 v[P5], s11
	global_store_dword v[P4:P5], v[P2], off \
			   offset:KNOD_PERSIST_RING_CQ_DB glc slc
	global_store_dword v[P4:P5], v[P3], off \
			   offset:KNOD_PERSIST_RING_RQ_DB glc slc

	// The count, for the host to read.
	GDA_ENTRY_ADDR
	global_store_dwordx2 v[P0:P1], v[GDA_PKTS:GDA_PKTS+1], off \
			     offset:KNOD_PERSIST_GDA_PACKETS glc slc
	s_setpc_b64 s[6:7]
	.endm

#endif /* GDA_INC */
