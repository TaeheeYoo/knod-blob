// SPDX-License-Identifier: GPL-2.0-or-later
//
// Hash map lookup, update and delete: csrc/map_hash.c, called.
//
// Each routine moves the descriptor and its key - for an update, the value
// too - from where the splice linkage puts them to where the calling
// convention wants them, calls the function placed right after it, and steps
// over that on the way back.  The JIT splices the routine and the function
// together.  One routine per key length, because the key arrives in registers
// and the length decides how many.

#include "common.inc"

#if KNOD_ISA == 10
# include "cfn.10.inc"
#else
# include "cfn.11.inc"
#endif

.macro	ROUTINE fn, n, val=0
	.globl knod_\fn\()\n
	.p2align 2
knod_\fn\()\n:
	CFN_SAVE_\fn\()\n
	v_mov_b32 v0, s[DESC]
	v_mov_b32 v1, s[DESC+1]
	.set _i, 0
	.rept \n
	v_mov_b32 v[2 + _i], v[KEY + _i]
	.set _i, _i + 1
	.endr
.if \val
	// After the key: for a fourteen-dword key the last of these land on
	// its first two, which have been read by then.
	.set _i, 0
	.rept KNOD_BLOB_VALUE_CHUNKS_MAX
	v_mov_b32 v[2 + \n + _i], v[VAL + _i]
	.set _i, _i + 1
	.endr
.endif
	s_mov_b32 s32, KNOD_BLOB_CALL_STACK_OFF
	s_getpc_b64 s[CFN_TMP_\fn\()\n:CFN_TMP_\fn\()\n+1]
.Lpc_\fn\()\n:
	s_add_u32 s[CFN_TMP_\fn\()\n], s[CFN_TMP_\fn\()\n], \
		  .Lcfn_\fn\()\n - .Lpc_\fn\()\n
	s_addc_u32 s[CFN_TMP_\fn\()\n+1], s[CFN_TMP_\fn\()\n+1], 0
	s_swappc_b64 s[30:31], s[CFN_TMP_\fn\()\n:CFN_TMP_\fn\()\n+1]
	CFN_RESTORE_\fn\()\n
	s_branch .Lend_\fn\()\n
	CFN_BODY_\fn\()\n
.Lend_\fn\()\n:
	KNOD_BLOB_RET
	.size knod_\fn\()\n, . - knod_\fn\()\n
	.globl knod_\fn\()\n\()_xsave
	.set knod_\fn\()\n\()_xsave, KNOD_BLOB_EXEC_SAVE_PAIRS_MAX
.endm

	.text

.irp n, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
	ROUTINE lookup_hash_k, \n
	ROUTINE lookup_percpu_hash_k, \n
	ROUTINE update_hash_k, \n, 1
	ROUTINE update_percpu_hash_k, \n, 1
	ROUTINE delete_hash_k, \n
.endr
