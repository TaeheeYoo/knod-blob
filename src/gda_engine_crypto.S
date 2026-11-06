// SPDX-License-Identifier: GPL-2.0-or-later
//
// knod_crypto's kernel, KNOD_BLOB_CRYPTO_KERNEL: a batch of AEAD requests, a
// workgroup of one wave each, as include/uapi/linux/knod_crypto.h has it.
// The host dispatches it with the GDA engine's kernel descriptor layout -
// kernarg at s[8:9], the workgroup at s12, gfx10's scratch wave offset at
// s14 - and it calls cfn_crypto_kernel(args, workgroup, lds) and ends.

#include "common.inc"
#include <linux/knod_crypto.h>

	.text
	.globl knod_crypto_kernel
	.p2align 2
knod_crypto_kernel:
	// Only for code just installed: every wave's s_icache_inv would
	// throw out the others' fetched code.
	s_load_dword s24, s[8:9], KNOD_CRYPTO_ARGS_FLAGS_OFF
	s_waitcnt lgkmcnt(0)
	s_bitcmp1_b32 s24, 0
	s_cbranch_scc0 .Lcrypto_cached
	s_icache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
.Lcrypto_cached:
#if KNOD_ISA == 10
	s_and_b32 s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1], \
		  s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1], 0xffff
	s_add_u32 s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG], \
		  s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG], \
		  s[KNOD_BLOB_PRO_SCRATCH_WAVE_SREG]
	s_addc_u32 s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1], \
		   s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1], 0
	s_setreg_b32 hwreg(HW_REG_FLAT_SCR_LO), s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG]
	s_setreg_b32 hwreg(HW_REG_FLAT_SCR_HI), \
		     s[KNOD_BLOB_PRO_SCRATCH_DESC_SREG + 1]
#endif
	s_mov_b64 exec, -1
	v_mov_b32 v0, s8
	v_mov_b32 v1, s9
	v_mov_b32 v2, s12
	v_mov_b32 v3, 0			// the workgroup's LDS
	s_mov_b32 s32, 0			// no BPF stack below it
	s_getpc_b64 s[24:25]
.Lpc_crypto_kernel:
	s_add_u32 s24, s24, .Lcfn_crypto_kernel - .Lpc_crypto_kernel
	s_addc_u32 s25, s25, 0
	s_swappc_b64 s[30:31], s[24:25]
	s_endpgm
	CFN_BODY_crypto_kernel
	.size knod_crypto_kernel, . - knod_crypto_kernel
