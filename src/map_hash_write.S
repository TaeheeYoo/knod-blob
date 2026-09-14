// SPDX-License-Identifier: GPL-2.0-or-later
//
// Hash map update and delete.
//
// Both change the chain, so both take the bucket's lock, and taking a lock in
// a wave means the lanes cannot run together the way a lookup's do.  The
// structure that follows is all about that:
//
//   for each distinct bucket among the live lanes     .Lbucket
//     one lane spins on the lock                      .Llock
//     for each lane wanting that bucket, one at a time    .Llane
//       walk the chain                                .Lchain
//         found     -> overwrite the value, or unlink and hand to the GC
//         not found -> take an element and link it in, or nothing to do
//     release the lock
//
// Lanes are grouped by bucket first so that two lanes on different buckets do
// not serialise behind each other, and only then serialised within a bucket,
// where they genuinely conflict.
//
// One routine per key length, as with lookup.  The value length is read from
// the descriptor and branched on, because making a routine per value length
// too would multiply out to fourteen times fourteen.

#include "common.inc"
#include "jhash.inc"

#define K0		KEY			// v28-v41, live throughout

// What is left once the key and the value have their halves of the window.
// The stored key cannot be held whole against the search key the way a lookup
// holds it - the value is in those registers - so it is compared a dword at a
// time through MCHUNK.
#define BADDR		(T0 + 0)		// v22:v23 the bucket cell
#define EADDR		(T0 + 2)		// v24:v25 the element being read
#define CHAIN		(T0 + 4)		// v26     next index
#define MCHUNK		(T0 + 5)		// v27     one dword of a stored key
#define TMP		THI0			// v56
#define TMP2		THI1			// v57

// jhash runs before the walk, so it borrows the walk's registers.
#define HA		BADDR
#define HB		(BADDR + 1)
#define HC		EADDR
#define HT		(EADDR + 1)

#define KNOD_BPF_HASH_NEXT_END		0x7FFFFFFF
#define KNOD_BPF_HASH_NEXT_MASK		0x7FFFFFFF
#define KNOD_BPF_HASH_DELETED		0x80000000
#define ELEM_OFF_NEXT			0
#define ELEM_OFF_KV			KNOD_BLOB_ELEM_KV_OFF

	.text

// ELECT_ONE  keep
//
// Narrows EXEC to the lowest live lane, having saved the mask it started from
// in s[\keep].  mbcnt counts the live lanes below this one, so the lane that
// counts zero is the first - which is not the same as lane zero, and has to be
// found this way because lane zero may not be live.
.macro	ELECT_ONE keep
	s_mov_b64 s[\keep:\keep+1], exec
	v_mbcnt_lo_u32_b32 v[TMP], exec_lo, 0
	v_mbcnt_hi_u32_b32 v[TMP], exec_hi, v[TMP]
	v_cmp_eq_u32 vcc, 0, v[TMP]
	s_and_b64 exec, exec, vcc
.endm

// DEFINE_HASH_WRITE  n, mode, pc
//
//   mode 1  update:  the key's value is replaced, or the key is inserted
//   mode 2  delete:  the key's element is unlinked and handed to the GC
//   pc 1    the value is per-instance: stores land in this instance's slot,
//           value_off + workgroup_id_y * per_instance_size (update only)
//
// r0 is zero either way: the emitter reports success for every lane, missing
// key and full map included, and this keeps to that.
.macro	DEFINE_HASH_WRITE n, mode, pc=0
.if \mode == 1
.if \pc
	.globl knod_update_percpu_hash_k\n
	.p2align 2
knod_update_percpu_hash_k\n:
.else
	.globl knod_update_hash_k\n
	.p2align 2
knod_update_hash_k\n:
.endif
.else
	.globl knod_delete_hash_k\n
	.p2align 2
knod_delete_hash_k\n:
.endif
	v_mov_b32 v[RET], 0
	v_mov_b32 v[RET+1], 0

	// hash the key, exactly as a lookup does
	s_load_dword s[S0], s[DESC:DESC+1], DESC_KEY_SIZE
	s_load_dword s[S1], s[DESC:DESC+1], DESC_HASHRND
	s_waitcnt lgkmcnt(0)
	s_add_i32 s[S0], s[S0], s[S1]
	JHASH HA, HB, HC, HT, K0, \n, S0

	s_load_dword s[S1], s[DESC:DESC+1], DESC_N_BUCKETS
	s_load_dwordx2 s[S2:S2+1], s[DESC:DESC+1], DESC_BUCKET
	s_waitcnt lgkmcnt(0)
	s_add_i32 s[S1], s[S1], -1
	v_and_b32 v[HC], s[S1], v[HC]

	// &bucket[hash].  HC is EADDR, so this has to land in BADDR first and
	// EADDR is set from it at the top of each walk.
	v_mov_b32 v[TMP], s[S2]
	v_mov_b32 v[TMP2], s[S2+1]
	v_mad_u64_u32 v[BADDR:BADDR+1], s[S2:S2+1], 4, v[HC], v[TMP:TMP2]

	s_mov_b64 s[XSAVE0:XSAVE0+1], exec	// every lane, restored at .Lout

.Lbucket_\@:
	// Take the first live lane's bucket and keep the lanes that share it.
	v_readfirstlane_b32 s[S3], v[BADDR]
	v_cmp_eq_u32 vcc, s[S3], v[BADDR]
	s_and_saveexec_b64 s[XSAVE1:XSAVE1+1], vcc
	s_mov_b64 s[XSAVE2:XSAVE2+1], exec

	// The lock sits lock_offset past the bucket heads.
	s_load_dword s[S1], s[DESC:DESC+1], DESC_LOCK_OFFSET
	s_waitcnt lgkmcnt(0)
	v_add_co_u32 v[EADDR], vcc, s[S1], v[BADDR]
	V_ADD_CI_U32 v[EADDR+1], vcc, 0, v[BADDR+1], vcc

	ELECT_ONE XSAVE3
.Llock_\@:
	v_mov_b32 v[TMP], 1
	global_atomic_swap v[TMP], v[EADDR:EADDR+1], v[TMP], off glc
	s_waitcnt vmcnt(0)
	v_cmp_ne_u32 vcc, 0, v[TMP]
	s_cbranch_vccnz .Llock_\@
	s_mov_b64 exec, s[XSAVE2:XSAVE2+1]

.Llane_\@:
	ELECT_ONE XSAVE3
	s_mov_b64 s[XSAVE4:XSAVE4+1], exec	// the one lane being served

	// The walk starts at the bucket cell, whose "next" is the head.
	v_mov_b32 v[EADDR], v[BADDR]
	v_mov_b32 v[EADDR+1], v[BADDR+1]

.Lchain_\@:
	global_load_dword v[CHAIN], v[EADDR:EADDR+1], off MAP_COHERENT
	s_waitcnt vmcnt(0)
	v_and_b32 v[CHAIN], KNOD_BPF_HASH_NEXT_MASK, v[CHAIN]
	v_cmp_eq_u32 vcc, KNOD_BPF_HASH_NEXT_END, v[CHAIN]
	s_cbranch_vccnz .Lmissing_\@

	// The element the index names.  EADDR moves off the previous link, so
	// the previous one is kept in TMP:TMP2 for a delete to unlink through.
	v_mov_b32 v[TMP], v[EADDR]
	v_mov_b32 v[TMP2], v[EADDR+1]
	s_load_dword s[S1], s[DESC:DESC+1], DESC_ELEM_SIZE
	s_load_dwordx2 s[S2:S2+1], s[DESC:DESC+1], DESC_ELEMS
	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[MCHUNK], s[S2]
	v_mov_b32 v[EADDR+1], s[S2+1]
	v_mov_b32 v[EADDR], v[MCHUNK]
	v_mad_u64_u32 v[EADDR:EADDR+1], s[S2:S2+1], s[S1], v[CHAIN], \
		      v[EADDR:EADDR+1]

	// Compare the stored key a dword at a time; the first mismatch leaves
	// for the next link.  Both sides zero the padding, so the last dword
	// compares equal without masking.
	.set _i, 0
	.rept \n
	global_load_dword v[MCHUNK], v[EADDR:EADDR+1], \
			  off offset:(ELEM_OFF_KV + _i * 4) MAP_COHERENT
	s_waitcnt vmcnt(0)
	v_cmp_ne_u32 vcc, v[K0 + _i], v[MCHUNK]
	s_cbranch_vccnz .Lnext_\@
	.set _i, _i + 1
	.endr
	s_branch .Lfound_\@

.Lnext_\@:
	// EADDR is the element; its next is at offset zero, which is what the
	// top of the walk reloads.
	s_branch .Lchain_\@

.Lfound_\@:
.if \mode == 1
	// Overwrite in place.  The lock keeps other writers out; a reader may
	// see the value change under it, which is what the emitter does too.
	s_load_dword s[S0], s[DESC:DESC+1], DESC_VALUE_SIZE
	s_waitcnt lgkmcnt(0)
	s_lshr_b32 s[S1], s[S0], 2
.if \pc
	// This instance's slot: value_off + workgroup_id_y * per_instance_size.
	s_load_dword s[S2], s[DESC:DESC+1], DESC_PER_INSTANCE
	s_waitcnt lgkmcnt(0)
	s_mul_i32 s[S2], s[S2], s[WG_Y]
	v_add_co_u32 v[EADDR], vcc, s[S2], v[EADDR]
	V_ADD_CI_U32 v[EADDR+1], vcc, 0, v[EADDR+1], vcc
.endif
	.set _i, 0
	.rept KNOD_BLOB_VALUE_CHUNKS_MAX
	s_cmp_ge_u32 s[S1], (_i + 1)
	s_cbranch_scc0 .Ldone_\@
	global_store_dword v[EADDR:EADDR+1], v[VAL + _i], \
			   off offset:(KNOD_BLOB_ELEM_VALUE_OFF(\n) + _i * 4)
	.set _i, _i + 1
	.endr
.else
	// Unlink: the previous link takes this element's next.  Then mark the
	// element deleted so a walker in flight stops trusting it, and hand
	// the index to the GC list for the host to reclaim.
	global_load_dword v[MCHUNK], v[EADDR:EADDR+1], \
			  off offset:ELEM_OFF_NEXT MAP_COHERENT
	s_waitcnt vmcnt(0)
	global_store_dword v[TMP:TMP2], v[MCHUNK], off
	v_or_b32 v[MCHUNK], KNOD_BPF_HASH_DELETED, v[MCHUNK]
	global_store_dword v[EADDR:EADDR+1], v[MCHUNK], off
	s_waitcnt vmcnt(0)

	s_load_dwordx2 s[S0:S0+1], s[DESC:DESC+1], DESC_GC_COUNT
	s_load_dwordx2 s[S2:S2+1], s[DESC:DESC+1], DESC_GC_LIST
	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[EADDR], s[S0]
	v_mov_b32 v[EADDR+1], s[S0+1]
	v_mov_b32 v[MCHUNK], 1
	global_atomic_add v[MCHUNK], v[EADDR:EADDR+1], v[MCHUNK], off glc
	s_waitcnt vmcnt(0)
	v_mov_b32 v[TMP], s[S2]
	v_mov_b32 v[TMP2], s[S2+1]
	v_mad_u64_u32 v[EADDR:EADDR+1], s[S0:S0+1], 4, v[MCHUNK], \
		      v[TMP:TMP2]
	global_store_dword v[EADDR:EADDR+1], v[CHAIN], off
.endif
	s_branch .Ldone_\@

.Lmissing_\@:
.if \mode == 1
	// Take an element off the free list.  Every lane decrements before any
	// of them knows whether there was anything left and the branch is
	// taken for the whole wave, so when one comes up short none of them
	// takes an element and all the decrements go back.
	s_load_dwordx2 s[S0:S0+1], s[DESC:DESC+1], DESC_FREE_CUR
	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[EADDR], s[S0]
	v_mov_b32 v[EADDR+1], s[S0+1]
	v_mov_b32 v[TMP], -1
	global_atomic_add v[TMP], v[EADDR:EADDR+1], v[TMP], off glc
	s_waitcnt vmcnt(0)
	V_SUB_U32 v[TMP], v[TMP], 1
	v_cmp_gt_i32 vcc, 0, v[TMP]
	s_cbranch_vccz .Lgot_\@
	v_mov_b32 v[MCHUNK], 1
	global_atomic_add v[EADDR:EADDR+1], v[MCHUNK], off
	s_branch .Ldone_\@
.Lgot_\@:

	// elem_id = queue[cur]
	s_load_dwordx2 s[S0:S0+1], s[DESC:DESC+1], DESC_QUEUE
	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[MCHUNK], s[S0]
	v_mov_b32 v[EADDR+1], s[S0+1]
	v_mov_b32 v[EADDR], v[MCHUNK]
	v_mad_u64_u32 v[EADDR:EADDR+1], s[S0:S0+1], 4, v[TMP], \
		      v[EADDR:EADDR+1]
	global_load_dword v[CHAIN], v[EADDR:EADDR+1], off MAP_COHERENT
	s_waitcnt vmcnt(0)

	// &elems[elem_id]
	s_load_dword s[S1], s[DESC:DESC+1], DESC_ELEM_SIZE
	s_load_dwordx2 s[S2:S2+1], s[DESC:DESC+1], DESC_ELEMS
	s_waitcnt lgkmcnt(0)
	v_mov_b32 v[MCHUNK], s[S2]
	v_mov_b32 v[EADDR+1], s[S2+1]
	v_mov_b32 v[EADDR], v[MCHUNK]
	v_mad_u64_u32 v[EADDR:EADDR+1], s[S2:S2+1], s[S1], v[CHAIN], \
		      v[EADDR:EADDR+1]

	// The new element points at the old head, then the key, then the
	// value, and only then does the head point at it - so a reader never
	// reaches an element whose contents are not there yet.
	global_load_dword v[MCHUNK], v[BADDR:BADDR+1], off MAP_COHERENT
	s_waitcnt vmcnt(0)
	global_store_dword v[EADDR:EADDR+1], v[MCHUNK], off offset:ELEM_OFF_NEXT

	.set _i, 0
	.rept \n
	global_store_dword v[EADDR:EADDR+1], v[K0 + _i], \
			   off offset:(ELEM_OFF_KV + _i * 4)
	.set _i, _i + 1
	.endr

	s_load_dword s[S0], s[DESC:DESC+1], DESC_VALUE_SIZE
	s_waitcnt lgkmcnt(0)
	s_lshr_b32 s[S1], s[S0], 2
.if \pc
	// A fresh element's other instance slots are left zero by the host
	// clearing them before the element is put back on the free list; this
	// lane only writes its own.
	s_load_dword s[S2], s[DESC:DESC+1], DESC_PER_INSTANCE
	s_waitcnt lgkmcnt(0)
	s_mul_i32 s[S2], s[S2], s[WG_Y]
	v_add_co_u32 v[EADDR], vcc, s[S2], v[EADDR]
	V_ADD_CI_U32 v[EADDR+1], vcc, 0, v[EADDR+1], vcc
.endif
	.set _i, 0
	.rept KNOD_BLOB_VALUE_CHUNKS_MAX
	s_cmp_ge_u32 s[S1], (_i + 1)
	s_cbranch_scc0 .Llinked_\@
	global_store_dword v[EADDR:EADDR+1], v[VAL + _i], \
			   off offset:(KNOD_BLOB_ELEM_VALUE_OFF(\n) + _i * 4)
	.set _i, _i + 1
	.endr
.Llinked_\@:
	KNOD_WAIT_STORES
	global_store_dword v[BADDR:BADDR+1], v[CHAIN], off
.endif

.Ldone_\@:
	// The next lane can read this lane's newly written key or value.
	KNOD_WAIT_STORES
	// This lane is served; go back for the next one sharing the bucket.
	s_andn2_b64 exec, s[XSAVE3:XSAVE3+1], s[XSAVE4:XSAVE4+1]
	s_cbranch_execnz .Llane_\@

	// Release the lock, from one lane, and move to the next bucket.
	s_mov_b64 exec, s[XSAVE2:XSAVE2+1]
	ELECT_ONE XSAVE3
	s_load_dword s[S1], s[DESC:DESC+1], DESC_LOCK_OFFSET
	s_waitcnt lgkmcnt(0)
	v_add_co_u32 v[EADDR], vcc, s[S1], v[BADDR]
	V_ADD_CI_U32 v[EADDR+1], vcc, 0, v[BADDR+1], vcc
	v_mov_b32 v[TMP], 0
	KNOD_WAIT_STORES
	global_store_dword v[EADDR:EADDR+1], v[TMP], off glc
	KNOD_WAIT_STORES

	// What is left is what came into this round less the bucket just done.
	// Restoring the saved mask alone would bring the served lanes back and
	// the loop would never end.
	s_andn2_b64 exec, s[XSAVE1:XSAVE1+1], s[XSAVE2:XSAVE2+1]
	s_cbranch_execnz .Lbucket_\@

.Lout_\@:
	s_mov_b64 exec, s[XSAVE0:XSAVE0+1]
	KNOD_BLOB_RET
.if \mode == 1
.if \pc
	.size knod_update_percpu_hash_k\n, . - knod_update_percpu_hash_k\n
	.globl knod_update_percpu_hash_k\n\()_xsave
	.set knod_update_percpu_hash_k\n\()_xsave, 5
.else
	.size knod_update_hash_k\n, . - knod_update_hash_k\n
	.globl knod_update_hash_k\n\()_xsave
	.set knod_update_hash_k\n\()_xsave, 5
.endif
.else
	.size knod_delete_hash_k\n, . - knod_delete_hash_k\n
	.globl knod_delete_hash_k\n\()_xsave
	.set knod_delete_hash_k\n\()_xsave, 5
.endif
.endm

// One per DIV_ROUND_UP(key_size, 4) the JIT will accept.  Spelled out rather
// than generated, because the length is pasted into symbol names as well as
// counted with, and only a literal does both.
	DEFINE_HASH_WRITE 1, 1
	DEFINE_HASH_WRITE 2, 1
	DEFINE_HASH_WRITE 3, 1
	DEFINE_HASH_WRITE 4, 1
	DEFINE_HASH_WRITE 5, 1
	DEFINE_HASH_WRITE 6, 1
	DEFINE_HASH_WRITE 7, 1
	DEFINE_HASH_WRITE 8, 1
	DEFINE_HASH_WRITE 9, 1
	DEFINE_HASH_WRITE 10, 1
	DEFINE_HASH_WRITE 11, 1
	DEFINE_HASH_WRITE 12, 1
	DEFINE_HASH_WRITE 13, 1
	DEFINE_HASH_WRITE 14, 1

	DEFINE_HASH_WRITE 1, 2
	DEFINE_HASH_WRITE 2, 2
	DEFINE_HASH_WRITE 3, 2
	DEFINE_HASH_WRITE 4, 2
	DEFINE_HASH_WRITE 5, 2
	DEFINE_HASH_WRITE 6, 2
	DEFINE_HASH_WRITE 7, 2
	DEFINE_HASH_WRITE 8, 2
	DEFINE_HASH_WRITE 9, 2
	DEFINE_HASH_WRITE 10, 2
	DEFINE_HASH_WRITE 11, 2
	DEFINE_HASH_WRITE 12, 2
	DEFINE_HASH_WRITE 13, 2
	DEFINE_HASH_WRITE 14, 2

	DEFINE_HASH_WRITE 1, 1, 1
	DEFINE_HASH_WRITE 2, 1, 1
	DEFINE_HASH_WRITE 3, 1, 1
	DEFINE_HASH_WRITE 4, 1, 1
	DEFINE_HASH_WRITE 5, 1, 1
	DEFINE_HASH_WRITE 6, 1, 1
	DEFINE_HASH_WRITE 7, 1, 1
	DEFINE_HASH_WRITE 8, 1, 1
	DEFINE_HASH_WRITE 9, 1, 1
	DEFINE_HASH_WRITE 10, 1, 1
	DEFINE_HASH_WRITE 11, 1, 1
	DEFINE_HASH_WRITE 12, 1, 1
	DEFINE_HASH_WRITE 13, 1, 1
	DEFINE_HASH_WRITE 14, 1, 1

// The build concatenates every source into one file, so these names are
// shared with whatever follows.  Handing them back keeps the next file's
// meaning its own, and keeps a collision an error rather than an ordering.
#undef K0
#undef BADDR
#undef EADDR
#undef CHAIN
#undef MCHUNK
#undef TMP
#undef TMP2
#undef HA
#undef HB
#undef HC
#undef HT
#undef KNOD_BPF_HASH_NEXT_END
#undef KNOD_BPF_HASH_NEXT_MASK
#undef KNOD_BPF_HASH_DELETED
#undef ELEM_OFF_NEXT
#undef ELEM_OFF_KV
