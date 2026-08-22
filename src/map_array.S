// SPDX-License-Identifier: GPL-2.0-or-later
//
// Array map routines.
//
// An array map indexes straight into storage, so the key length never changes
// the code and one entry covers every map of this type.

#include "common.inc"

	.text

// knod_lookup_array
//
//   r0 = key < max_entries ? elems + key * value_size : 0
//
// Out-of-bounds lanes are parked rather than branched around: EXEC keeps the
// lanes that passed the bounds check and the parked mask is ORed back at the
// end.  A parked lane keeps the zero written before the narrowing, which is
// what a miss looks like to the caller.
	.globl knod_lookup_array
	.p2align 2
knod_lookup_array:
	// value_size and max_entries are adjacent in the descriptor.  The key
	// is one chunk and already in a register.
	s_load_dwordx2 s[S0:S0+1], s[DESC:DESC+1], DESC_VALUE_SIZE
	s_load_dwordx2 s[S2:S2+1], s[DESC:DESC+1], DESC_ELEMS

	v_mov_b32 v[RET], 0
	v_mov_b32 v[RET+1], 0

	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[T2], s[S1]			// max_entries

	// Park the lanes whose key is out of range.
	v_cmp_ge_u32 vcc, v[KNOD_BLOB_SPLICE_KEY_VREG], v[T2]
	s_and_b64 s[XSAVE0:XSAVE0+1], exec, vcc
	s_andn2_b64 exec, exec, vcc
	s_cbranch_execz .Llookup_array_out

	v_mov_b32 v[T3], s[S2]
	v_mov_b32 v[T4], s[S2+1]
	v_mad_u64_u32 v[RET:RET+1], s[S0:S0+1], s[S0], \
		      v[KNOD_BLOB_SPLICE_KEY_VREG], v[T3:T4]

.Llookup_array_out:
	s_or_b64 exec, exec, s[XSAVE0:XSAVE0+1]
	KNOD_BLOB_RET
	.size knod_lookup_array, . - knod_lookup_array

// The JIT reserves this many EXEC-save pairs for the routine.
	.globl knod_lookup_array_xsave
	.set knod_lookup_array_xsave, 1
