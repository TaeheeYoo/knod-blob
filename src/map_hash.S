// SPDX-License-Identifier: GPL-2.0-or-later
//
// Hash map lookup.
//
// One routine per key length, because the key is copied, hashed and compared a
// dword at a time and the length decides how many times.  The JIT knows the
// length when it splices, so it picks the routine rather than the routine
// looping over a runtime count.

#include "common.inc"
#include "jhash.inc"

// The key and the stored key it is compared against are both held whole, and
// between them they take all but four registers of the scratch window.  That
// is what sets the 56-byte key limit, not any property of hashing.
#define K0		(KNOD_BLOB_SPLICE_TMP_VREG + 0)		// v28-v41
#define M0		(KNOD_BLOB_SPLICE_TMP_VREG + 14)	// v42-v55

// jhash state and its scratch borrow the stored-key registers, which are not
// written until the walk starts and the hash is finished before then.
#define HA		(M0 + 0)
#define HB		(M0 + 1)
#define HC		(M0 + 2)
#define HT		(M0 + 3)

// What is left of the window, plus the two pairs below it that a lookup has
// no use for: it is given no value, and the key now arrives in registers.
#define EBASE		22		// element storage base
#define EADDR		VAL		// the element being examined
#define NEND		(KNOD_BLOB_SPLICE_TMP_VREG + 28)
#define CHAIN		(KNOD_BLOB_SPLICE_TMP_VREG + 29)

#if (M0 + KNOD_BLOB_KEY_CHUNKS_MAX) > (KNOD_BLOB_SPLICE_TMP_VREG_END - 1)
# error "key registers collide with the scratch at the top of the window"
#endif

#define KNOD_BPF_HASH_NEXT_END		0x7FFFFFFF
#define KNOD_BPF_HASH_NEXT_MASK		0x7FFFFFFF
#define ELEM_OFF_NEXT			0
#define ELEM_OFF_KV			KNOD_BLOB_ELEM_KV_OFF

	.text

//
// knod_lookup_hash_k<n>
//
//   r0 = the value belonging to the key, or 0
//
// The chain is walked with the lanes still resident: a lane whose chain ends
// is removed from EXEC rather than branched away, so lanes at different depths
// share the walk and the loop runs as long as any lane is still looking.  The
// mask is put back at the end and the lanes that dropped out keep the zero
// written up front, which is what a miss looks like.
//
.macro	DEFINE_LOOKUP_HASH n
	.globl knod_lookup_hash_k\n
	.p2align 2
knod_lookup_hash_k\n:
	s_load_dwordx2 s[S0:S0+1], s[DESC:DESC+1], DESC_BUCKET
	s_load_dwordx2 s[S2:S2+1], s[DESC:DESC+1], DESC_ELEMS

	// The key is already in v[K0..], which is where the compares below
	// want it.
	v_mov_b32 v[RET], 0
	v_mov_b32 v[RET+1], 0

	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[NEND], s[S0]		// bucket heads, until .Lnext
	v_mov_b32 v[CHAIN], s[S0+1]
	v_mov_b32 v[EBASE], s[S2]
	v_mov_b32 v[EBASE+1], s[S2+1]

	// What jhash starts from is loaded here rather than with the rest
	// because the scalars only come free on the line above.
	s_load_dword s[S0], s[DESC:DESC+1], DESC_KEY_SIZE
	s_load_dword s[S1], s[DESC:DESC+1], DESC_HASHRND
	s_load_dword s[S3], s[DESC:DESC+1], DESC_N_BUCKETS
	s_waitcnt lgkmcnt(0)
	s_add_i32 s[S0], s[S0], s[S1]

	JHASH HA, HB, HC, HT, K0, \n, S0

	// bucket_id = hash & (n_buckets - 1)
	s_add_i32 s[S3], s[S3], -1
	v_and_b32 v[HC], s[S3], v[HC]

	// &bucket[bucket_id].  s[S0:S0+1] is only the carry out; the bucket
	// base it held is in v[NEND:CHAIN], which the mad may not write over.
	v_mad_u64_u32 v[EADDR:EADDR+1], s[S0:S0+1], 4, v[HC], v[NEND:CHAIN]

	// Every lane starts the walk; the mask comes back at .Lout.
	s_mov_b64 s[XSAVE0:XSAVE0+1], exec
	v_mov_b32 v[NEND], KNOD_BPF_HASH_NEXT_END

.Lnext\n:
	global_load_dword v[CHAIN], v[EADDR:EADDR+1], off
	s_waitcnt vmcnt(0)
	// The top bit marks a deleted element; it is not part of the index.
	v_and_b32 v[CHAIN], KNOD_BPF_HASH_NEXT_MASK, v[CHAIN]

	// Lanes that reached the end of their chain stop here.
	v_cmp_eq_u32 vcc, v[NEND], v[CHAIN]
	s_andn2_b64 exec, exec, vcc
	s_cbranch_execz .Lout\n

	// elem = elems + elem_id * elem_size
	s_load_dword s[S3], s[DESC:DESC+1], DESC_ELEM_SIZE
	s_waitcnt lgkmcnt(0)
	v_mad_u64_u32 v[EADDR:EADDR+1], s[S0:S0+1], s[S3], v[CHAIN], v[EBASE:EBASE+1]

	.set _i, 0
	.rept \n
	global_load_dword v[M0 + _i], v[EADDR:EADDR+1], off offset:(ELEM_OFF_KV + _i * 4)
	.set _i, _i + 1
	.endr
	s_waitcnt vmcnt(0)

	// Fold the per-dword comparisons into one mask rather than branching
	// on each: the padding is zero on both sides, so the last dword
	// compares equal without masking.
	v_cmp_eq_u32 vcc, v[K0], v[M0]
	s_and_b64 s[XSAVE1:XSAVE1+1], exec, vcc
	.set _i, 1
	.rept (\n - 1)
	v_cmp_eq_u32 vcc, v[K0 + _i], v[M0 + _i]
	s_and_b64 s[XSAVE1:XSAVE1+1], s[XSAVE1:XSAVE1+1], vcc
	.set _i, _i + 1
	.endr

	// Matching lanes take the value and leave the walk.
	s_and_saveexec_b64 s[XSAVE2:XSAVE2+1], s[XSAVE1:XSAVE1+1]
	s_cbranch_execz .Lmiss\n
	v_add_co_u32 v[RET], vcc, KNOD_BLOB_ELEM_VALUE_OFF(\n), v[EADDR]
	V_ADD_CI_U32 v[RET+1], vcc, 0, v[EADDR+1], vcc
.Lmiss\n:
	// exec = lanes still walking = (those that were walking) - (matched)
	s_andn2_b64 exec, s[XSAVE2:XSAVE2+1], s[XSAVE1:XSAVE1+1]
	s_cbranch_execz .Lout\n

	// Follow the chain.  v[EADDR:EADDR+1] already points at the element
	// and its next index sits at offset zero, which is what .Lnext reloads.
	s_branch .Lnext\n

.Lout\n:
	s_mov_b64 exec, s[XSAVE0:XSAVE0+1]
	KNOD_BLOB_RET
	.size knod_lookup_hash_k\n, . - knod_lookup_hash_k\n

	.globl knod_lookup_hash_k\n\()_xsave
	.set knod_lookup_hash_k\n\()_xsave, 3
.endm

// One per DIV_ROUND_UP(key_size, 4) the JIT will accept.  Spelled out rather
// than generated, because the length is pasted into label names as well as
// counted with, and only a literal does both.
	DEFINE_LOOKUP_HASH 1
	DEFINE_LOOKUP_HASH 2
	DEFINE_LOOKUP_HASH 3
	DEFINE_LOOKUP_HASH 4
	DEFINE_LOOKUP_HASH 5
	DEFINE_LOOKUP_HASH 6
	DEFINE_LOOKUP_HASH 7
	DEFINE_LOOKUP_HASH 8
	DEFINE_LOOKUP_HASH 9
	DEFINE_LOOKUP_HASH 10
	DEFINE_LOOKUP_HASH 11
	DEFINE_LOOKUP_HASH 12
	DEFINE_LOOKUP_HASH 13
	DEFINE_LOOKUP_HASH 14
