// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bob Jenkins' lookup3, the same one lib/jhash.h implements, so that a key
// hashes to the bucket the kernel put it in.
//
// rol32(x, n) is a rotate, and v_alignbit_b32 d, x, x, s extracts bits
// [s+31:s] of {x, x}, which is a rotate right by s.  So a left rotate by n is
// an alignbit by 32 - n.

#ifndef JHASH_INC
#define JHASH_INC

#define JHASH_INITVAL	0xdeadbeef

// d = rol32(s, n), clobbering nothing else.
.macro	ROL32 d, s, n
	v_alignbit_b32 v[\d], v[\s], v[\s], (32 - \n)
.endm

// x -= rol32(y, n)
.macro	SUB_ROL32 x, y, n, tmp
	ROL32 \tmp, \y, \n
	V_SUB_U32 v[\x], v[\x], v[\tmp]
.endm

// The six-step mix.  a, b, c are the state; tmp is scratch.
.macro	JHASH_MIX a, b, c, tmp
	V_SUB_U32 v[\a], v[\a], v[\c]
	ROL32 \tmp, \c, 4
	v_xor_b32 v[\a], v[\a], v[\tmp]
	V_ADD_U32 v[\c], v[\c], v[\b]

	V_SUB_U32 v[\b], v[\b], v[\a]
	ROL32 \tmp, \a, 6
	v_xor_b32 v[\b], v[\b], v[\tmp]
	V_ADD_U32 v[\a], v[\a], v[\c]

	V_SUB_U32 v[\c], v[\c], v[\b]
	ROL32 \tmp, \b, 8
	v_xor_b32 v[\c], v[\c], v[\tmp]
	V_ADD_U32 v[\b], v[\b], v[\a]

	V_SUB_U32 v[\a], v[\a], v[\c]
	ROL32 \tmp, \c, 16
	v_xor_b32 v[\a], v[\a], v[\tmp]
	V_ADD_U32 v[\c], v[\c], v[\b]

	V_SUB_U32 v[\b], v[\b], v[\a]
	ROL32 \tmp, \a, 19
	v_xor_b32 v[\b], v[\b], v[\tmp]
	V_ADD_U32 v[\a], v[\a], v[\c]

	V_SUB_U32 v[\c], v[\c], v[\b]
	ROL32 \tmp, \b, 4
	v_xor_b32 v[\c], v[\c], v[\tmp]
	V_ADD_U32 v[\b], v[\b], v[\a]
.endm

// The seven-step avalanche that ends a hash.
.macro	JHASH_FINAL a, b, c, tmp
	v_xor_b32 v[\c], v[\c], v[\b]
	SUB_ROL32 \c, \b, 14, \tmp
	v_xor_b32 v[\a], v[\a], v[\c]
	SUB_ROL32 \a, \c, 11, \tmp
	v_xor_b32 v[\b], v[\b], v[\a]
	SUB_ROL32 \b, \a, 25, \tmp
	v_xor_b32 v[\c], v[\c], v[\b]
	SUB_ROL32 \c, \b, 16, \tmp
	v_xor_b32 v[\a], v[\a], v[\c]
	SUB_ROL32 \a, \c, 4, \tmp
	v_xor_b32 v[\b], v[\b], v[\a]
	SUB_ROL32 \b, \a, 14, \tmp
	v_xor_b32 v[\c], v[\c], v[\b]
	SUB_ROL32 \c, \b, 24, \tmp
.endm

//
// JHASH  a, b, c, tmp, key, nwords, start
//
// Hashes \nwords dwords starting at v[\key] and leaves the result in v[\c].
// The caller has already loaded the key, one dword per register, because it
// needs those same registers to compare against the stored key afterwards.
//
// jhash() folds three dwords per round and finishes with whatever is left, so
// the length is baked in here rather than looped over: every routine knows its
// key length, which is what makes one entry per length worth having.
//
// s[\start] is the key length plus the map's seed, which jhash() adds to its
// constant before it begins.  It comes in a register rather than being folded
// into that constant because neither part is known here: the length is the
// map's, in bytes, which \nwords only rounds up to, and the seed is drawn per
// map at run time.
//
.macro	JHASH a, b, c, tmp, key, nwords, start
	v_mov_b32 v[\a], JHASH_INITVAL
	V_ADD_U32 v[\a], s[\start], v[\a]
	v_mov_b32 v[\b], v[\a]
	v_mov_b32 v[\c], v[\a]

	.set _jh_i, 0
	.set _jh_left, \nwords
	// Full rounds while more than three dwords remain.
	.rept (\nwords - 1) / 3
	V_ADD_U32 v[\a], v[\a], v[\key + _jh_i + 0]
	V_ADD_U32 v[\b], v[\b], v[\key + _jh_i + 1]
	V_ADD_U32 v[\c], v[\c], v[\key + _jh_i + 2]
	JHASH_MIX \a, \b, \c, \tmp
	.set _jh_i, _jh_i + 3
	.set _jh_left, _jh_left - 3
	.endr

	// One to three dwords are always left; jhash adds them in reverse.
	.if _jh_left >= 3
	V_ADD_U32 v[\c], v[\c], v[\key + _jh_i + 2]
	.endif
	.if _jh_left >= 2
	V_ADD_U32 v[\b], v[\b], v[\key + _jh_i + 1]
	.endif
	.if _jh_left >= 1
	V_ADD_U32 v[\a], v[\a], v[\key + _jh_i + 0]
	.endif

	JHASH_FINAL \a, \b, \c, \tmp
.endm

#endif /* JHASH_INC */
