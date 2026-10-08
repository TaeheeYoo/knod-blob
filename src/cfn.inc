// SPDX-License-Identifier: GPL-2.0-or-later
//
// Calling a function compiled from csrc/.
//
// ROUTINE  fn, nkey, val
//
// Defines the routine knod_<fn>, which moves the map descriptor, the \nkey
// dwords of the key and, with \val, the value from where the splice linkage
// puts them to where the calling convention wants them, calls cfn_<fn>, and
// moves its result to r0.
// That is knod_<fn>_body, which the JIT places once after the program, and
// knod_<fn>_call says where the call's offset to it goes.

#ifndef CFN_INC
#define CFN_INC

#if KNOD_ISA == 10
# include "cfn.10.inc"
#else
# include "cfn.11.inc"
#endif

.macro	ROUTINE fn, nkey, val=0
	.globl knod_\fn
	.p2align 2
knod_\fn:
	CFN_SAVE_\fn
	v_mov_b32 v0, s[DESC]
	v_mov_b32 v1, s[DESC+1]
	.set _i, 0
	.rept \nkey
	v_mov_b32 v[2 + _i], v[KEY + _i]
	.set _i, _i + 1
	.endr
.if \val
	// After the key: for a fourteen-dword key the last of these land on
	// its first two, which have been read by then.
	.set _i, 0
	.rept KNOD_BLOB_VALUE_CHUNKS_MAX
	v_mov_b32 v[2 + \nkey + _i], v[VAL + _i]
	.set _i, _i + 1
	.endr
.endif
	s_mov_b32 s32, KNOD_BLOB_CALL_STACK_OFF
	s_getpc_b64 s[CFN_TMP_\fn:CFN_TMP_\fn+1]
.Lpc_\fn:
	s_add_u32 s[CFN_TMP_\fn], s[CFN_TMP_\fn], \
		  .Lcfn_\fn - .Lpc_\fn
	s_addc_u32 s[CFN_TMP_\fn+1], s[CFN_TMP_\fn+1], 0
	s_swappc_b64 s[30:31], s[CFN_TMP_\fn:CFN_TMP_\fn+1]
	v_mov_b32 v[KNOD_BLOB_SPLICE_R0_VREG], v0
	v_mov_b32 v[KNOD_BLOB_SPLICE_R0_VREG + 1], v1
	CFN_RESTORE_\fn
	.size knod_\fn, . - knod_\fn
	.globl knod_\fn\()_xsave
	.set knod_\fn\()_xsave, KNOD_BLOB_EXEC_SAVE_PAIRS_MAX
	.globl knod_\fn\()_call
	.set knod_\fn\()_call, .Lpc_\fn + 4 - knod_\fn

	.globl knod_\fn\()_body
	.p2align 2
knod_\fn\()_body:
	CFN_BODY_\fn
	.size knod_\fn\()_body, . - knod_\fn\()_body
.endm

#endif
