// SPDX-License-Identifier: GPL-2.0-or-later
//
// What runs after the translated BPF program.
//
// It tells the host what the program decided and where the packet ended up,
// by writing both into the buffer descriptor the prologue found, and for a
// packet the program let through it also claims a delivery slot.
//
// Two pieces rather than one, because the JIT puts the packet-cache writeback
// between them and how long that is follows the program.

#include "common.inc"

	.text

// Lanes that never had a packet, and lanes that left through an early exit,
// were switched off along the way.  Turn the ones that exited back on so their
// verdict is written too, then restore the mask the prologue left.
//
// Everything below writes the buffer descriptor the prologue put in v[SLOT].
	.globl knod_epilogue_pre
	.p2align 2
knod_epilogue_pre:
	s_andn2_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1], \
		    s[DONE_MASK:DONE_MASK+1]
	v_mov_b32 v[BPF_R0], XDP_DROP
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]

	global_store_dword v[SLOT:SLOT+1], v[BPF_R0], off \
			   offset:KNOD_BLOB_BD_ACT glc slc

	// Where the packet sits now.  A program that moved its start or its
	// end moved these registers; one that did not gets back the offset and
	// length the prologue read, so this is worth doing either way.
	V_SUB_U32 v[P4], v[DATA_END], v[DATA]			// len
	v_lshlrev_b32 v[P4], 16, v[P4]
	V_SUB_U32 v[P5], v[DATA], v[PAGE_BASE]			// off
	v_or_b32 v[P4], v[P4], v[P5]
	global_store_dword v[SLOT:SLOT+1], v[P4], off \
			   offset:KNOD_BLOB_BD_OFF glc slc

	.size knod_epilogue_pre, . - knod_epilogue_pre

// A packet the program passed needs somewhere for the host to find it: a slot
// in this queue's delivery buffer, claimed by bumping the queue's counter, and
// its index recorded so the host knows which descriptor the slot belongs to.
//
// Lanes whose verdict was anything else have nothing left to do, and if no
// lane passed the whole thing is skipped.
	.globl knod_epilogue_post
	.p2align 2
knod_epilogue_post:
	v_cmp_eq_u32 vcc, XDP_PASS, v[BPF_R0]
	s_cbranch_vccz .Lend
	s_and_b64 exec, exec, vcc

	// &param->pass_count[queue_id]
	v_mov_b32 v[P18], s[PARAM]
	v_mov_b32 v[P18+1], s[PARAM+1]
	v_mov_b32 v[BPF_R1], s[WG_Y]
	v_lshlrev_b32 v[BPF_R1], 2, v[BPF_R1]
	V_ADD_U32 v[P18], v[BPF_R1], v[P18]

	// Claim a slot.  Every passing lane adds one and takes back what the
	// counter held, which is its own index.
	v_mov_b32 v[P20], 1
	global_atomic_add v[P20], v[P18:P18+1], v[P20], off \
			  offset:KNOD_BLOB_PARAM_PASS_COUNT glc
	s_waitcnt vmcnt(0)

	// param->pass_indices[slot] = this descriptor.  Indices are shorts, so
	// the slot is doubled to reach one - and doubled again below, where the
	// same number picks the delivery slot.
	V_SUB_U32 v[P18], v[P18], v[BPF_R1]
	v_lshlrev_b32 v[P20], 1, v[P20]
	V_ADD_U32 v[P18], v[P20], v[P18]
	global_store_short v[P18:P18+1], v[IDX], off \
			   offset:KNOD_BLOB_PARAM_PASS_INDICES glc slc

	// The delivery buffer for this queue, and how far apart its slots are.
	// A slot is a page, so that follows the page size and comes from the
	// dispatch rather than from here.
	s_lshl_b32 s[PS2], s[WG_Y], 3
	s_load_dwordx2 s[PS0:PS0+1], s[PARAM:PARAM+1], s[PS2] \
		       offset:KNOD_BLOB_PARAM_PASS_META
	s_load_dwordx2 s[PS4:PS4+1], s[PARAM:PARAM+1], \
		       KNOD_BLOB_PARAM_PAGE_SHIFT
	s_waitcnt lgkmcnt(0)
	s_sub_u32 s[PS4+1], s[PS4], 1
	v_lshlrev_b32 v[P22], s[PS4+1], v[P20]
	v_mov_b32 v[P22+1], 0
	v_add_co_u32 v[P22], vcc, s[PS0], v[P22]

	// The high half takes no carry: the buffer is one allocation and a
	// slot offset cannot reach across four gigabytes of it.
	v_mov_b32 v[P22+1], s[PS0+1]

	// The slot header: how long the packet is, then where it lives.
	V_SUB_U32 v[BPF_R0], v[DATA_END], v[DATA]
	global_store_dword v[P22:P22+1], v[BPF_R0], off glc slc
	global_store_dwordx2 v[P22:P22+1], v[DATA:DATA+1], off \
			     offset:8 glc slc

.Lend:
	s_endpgm

	.size knod_epilogue_post, . - knod_epilogue_post
