// SPDX-License-Identifier: GPL-2.0-or-later
//
// What runs before the translated BPF program.
//
// It works out which packet this lane is for and hands the program a context,
// the packet's bounds, and the address of the buffer descriptor the epilogue
// will write back to.  Lanes with no packet are switched off and stay off.
//
// Two things follow the program rather than the hardware and are emitted by
// the JIT after this: zeroing the EXEC-save scalars, one per pair the control
// flow needs, and preloading the packet cache, one load per sixteen bytes the
// program reaches into.

#include "common.inc"

	.text

	.globl knod_prologue
	.p2align 2
knod_prologue:
	// A re-uploaded shader can land on the address an old one is still
	// cached at.  This must be the first instruction so that whichever
	// version the SQC is serving runs it before the two diverge.
	s_icache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)

	s_load_dwordx2 s[PARAM:PARAM+1], s[DISPATCH:DISPATCH+1], \
		       KNOD_BLOB_AQL_KERNARG

	// The workitem id arrives packed in the low bits of v0.
	v_bfe_i32 v[IDX], v[TID], 0, 10
	s_mov_b32 s[FRAME], 0
	s_waitcnt vmcnt(0) lgkmcnt(0)

	// The shift counts, in the two pairs they sit in.
	s_load_dwordx2 s[PS2:PS2+1], s[PARAM:PARAM+1], \
		       KNOD_BLOB_PARAM_BATCH_SHIFT
	s_load_dwordx2 s[PS4:PS4+1], s[PARAM:PARAM+1], \
		       KNOD_BLOB_PARAM_PAGE_SHIFT
	s_waitcnt vmcnt(0) lgkmcnt(0)

	// idx = (workgroup_id_x << wg_shift) + workitem_id.  The shift count
	// is a scalar now, so the group id comes across to a VGPR first rather
	// than putting a second scalar on the constant bus.
	v_mov_b32 v[P14], s[WG_X]
	v_lshlrev_b32 v[P14], s[PS2+1], v[P14]
	V_ADD_U32 v[IDX], v[P14], v[IDX]

	s_mov_b64 s[DONE_MASK:DONE_MASK+1], 0

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
	v_lshl_add_u32 v[IDX], v[P14], s[PS2], v[IDX]

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

	// off and len share a dword, off in the low half.
	v_and_b32 v[P16], 0xffff, v[P12]
	v_add_co_u32 v[DATA], vcc, v[P16], v[DATA]
	V_ADD_CI_U32 v[DATA+1], vcc, 0, v[DATA+1], vcc
	v_lshrrev_b32 v[DATA_END], 16, v[P12]
	v_add_co_u32 v[DATA_END], vcc, v[DATA], v[DATA_END]
	V_ADD_CI_U32 v[DATA_END+1], vcc, 0, v[DATA+1], vcc

	.size knod_prologue, . - knod_prologue
