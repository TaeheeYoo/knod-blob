// SPDX-License-Identifier: GPL-2.0-or-later
//
// The crypto pool's kernels, KNOD_BLOB_GDA_POOL, one per NIC format:
// knod_gda_pool_f<KNOD_PERSIST_FMT_*>.  The host dispatches one on the
// context's second AQL queue, its own kernel object behind the GDA engine's
// kernel descriptor - kernarg at s[8:9], workgroup y at s13 - and each
// workgroup runs cfn_gda_pool(ctl, workgroup y, wave) until pool_stop.

#include "gda_engine.inc"

.macro	GDA_POOL sym, nic
	.text
	.globl \sym
	.p2align 2
\sym:
	s_icache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
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
	v_readfirstlane_b32 s[E_WAVE], v0
	s_and_b32 s[E_WAVE], s[E_WAVE], 1023
	s_lshr_b32 s[E_WAVE], s[E_WAVE], 6
	s_mov_b32 s[E_QUEUE], s13
	s_mov_b64 s[E_CTL:E_CTL+1], s[8:9]
	ENGINE_CALL gda_pool_\nic
	s_endpgm
.endm

// The function's body after it, named out in full: the Makefile finds the
// engines' functions by grepping these files for CFN_BODY_.
.macro	GDA_POOL_END sym
	.size \sym, . - \sym
.endm

#define POOL_MLX5_CX5	GDA_CAT(knod_gda_pool_f, KNOD_PERSIST_FMT_MLX5_CX5)
#define POOL_BNXT_P5	GDA_CAT(knod_gda_pool_f, KNOD_PERSIST_FMT_BNXT_P5)
#define POOL_BNXT_P7	GDA_CAT(knod_gda_pool_f, KNOD_PERSIST_FMT_BNXT_P7)

	GDA_POOL POOL_MLX5_CX5, mlx5_cx5_ipsec
	CFN_BODY_gda_pool_mlx5_cx5_ipsec
	GDA_POOL_END POOL_MLX5_CX5

	GDA_POOL POOL_BNXT_P5, bnxt_p5_ipsec
	CFN_BODY_gda_pool_bnxt_p5_ipsec
	GDA_POOL_END POOL_BNXT_P5

	GDA_POOL POOL_BNXT_P7, bnxt_p7_ipsec
	CFN_BODY_gda_pool_bnxt_p7_ipsec
	GDA_POOL_END POOL_BNXT_P7
