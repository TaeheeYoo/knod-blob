// SPDX-License-Identifier: GPL-2.0-or-later
//
// Array map routines.
//
// An array map indexes straight into storage, so the key length never changes
// the code and one entry covers every map of this type.  The value length
// does change, and a routine cannot be built per length the way the hash ones
// are per key length - key and value lengths would multiply out.  It comes
// from the descriptor and is branched on instead, which costs only the
// branches: the length is the same for every lane, so they are scalar.

#include "common.inc"

#define EBASE		T0		// value storage base
#define ADDR		T2		// the slot being written

	.text

// STORE_VALUE  chunks, stride
//
// Writes s[\chunks] dwords to v[ADDR:ADDR+1], taking them from v[VAL..] when
// \stride is one and from v[VAL] over and over when it is zero, which is what
// a delete wants.
//
// Only whole dwords: elements of an array sit value_size apart with nothing
// between them, so a partial tail would have to be cut byte by byte out of a
// register chosen at run time, and a VGPR cannot be chosen at run time.  The
// JIT sends values that are not a multiple of four to the emitter instead.
.macro	STORE_VALUE chunks, stride
	.set _sv, 0
	.rept KNOD_BLOB_VALUE_CHUNKS_MAX
	s_cmp_ge_u32 s[\chunks], (_sv + 1)
	s_cbranch_scc0 .Lstored\@
	global_store_dword v[ADDR:ADDR+1], v[VAL + _sv * \stride], \
			   off offset:(_sv * 4)
	.set _sv, _sv + 1
	.endr
.Lstored\@:
.endm

// DEFINE_ARRAY  name, percpu, mode
//
//   mode 0  lookup:  r0 = key < max_entries ? elems + key * value_size : 0
//   mode 1  update:  that slot takes the value, r0 = 0
//   mode 2  delete:  that slot is zeroed, r0 = 0
//
// Out-of-bounds lanes are parked rather than branched around: EXEC keeps the
// lanes that passed the bounds check and the parked mask is ORed back at the
// end.  A parked lane keeps the zero written before the narrowing, which is
// what a miss looks like to the caller.
.macro	DEFINE_ARRAY name, percpu, mode
	.globl knod_\name
	.p2align 2
knod_\name:
	// value_size and max_entries are adjacent in the descriptor.  The key
	// is one chunk and already in a register.
	s_load_dwordx2 s[S0:S0+1], s[DESC:DESC+1], DESC_VALUE_SIZE
	s_load_dwordx2 s[S2:S2+1], s[DESC:DESC+1], DESC_ELEMS

	v_mov_b32 v[RET], 0
	v_mov_b32 v[RET+1], 0

	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[ADDR], s[S1]		// max_entries

	// Park the lanes whose key is out of range.  s[S1] is spare from here.
	v_cmp_ge_u32 vcc, v[KEY], v[ADDR]
	s_and_b64 s[XSAVE0:XSAVE0+1], exec, vcc
	s_andn2_b64 exec, exec, vcc
	s_cbranch_execz .Lout_\name

	v_mov_b32 v[EBASE], s[S2]
	v_mov_b32 v[EBASE+1], s[S2+1]
.if \percpu
	// One instance per queue, and a queue is one workgroup, so the queue
	// id indexes them.  Nothing outside this workgroup writes this
	// instance, which is why these routines leave the caches alone where
	// the shared ones may not.
	s_load_dword s[S1], s[DESC:DESC+1], DESC_PER_INSTANCE
	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[ADDR], s[KNOD_BLOB_PRO_WG_Y_SREG]
	v_mad_u64_u32 v[EBASE:EBASE+1], s[S2:S2+1], s[S1], v[ADDR], \
		      v[EBASE:EBASE+1]
.endif

.if \mode == 0
	v_mad_u64_u32 v[RET:RET+1], s[S2:S2+1], s[S0], v[KEY], v[EBASE:EBASE+1]
.else
	v_mad_u64_u32 v[ADDR:ADDR+1], s[S2:S2+1], s[S0], v[KEY], \
		      v[EBASE:EBASE+1]
	s_lshr_b32 s[S1], s[S0], 2
.if \mode == 2
	// A delete is an update of zeros, and one zero serves every dword.
	v_mov_b32 v[VAL], 0
	STORE_VALUE S1, 0
.else
	STORE_VALUE S1, 1
.endif
.endif

.Lout_\name:
	s_or_b64 exec, exec, s[XSAVE0:XSAVE0+1]
	KNOD_BLOB_RET
	.size knod_\name, . - knod_\name

// The JIT reserves this many EXEC-save pairs for the routine.
	.globl knod_\name\()_xsave
	.set knod_\name\()_xsave, 1
.endm

	DEFINE_ARRAY lookup_array, 0, 0
	DEFINE_ARRAY update_array, 0, 1
	DEFINE_ARRAY delete_array, 0, 2
	DEFINE_ARRAY lookup_percpu_array, 1, 0
	DEFINE_ARRAY update_percpu_array, 1, 1
	DEFINE_ARRAY delete_percpu_array, 1, 2
