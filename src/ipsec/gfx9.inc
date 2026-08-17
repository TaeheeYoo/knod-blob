// SPDX-License-Identifier: GPL-2.0-or-later
//
// The IPsec fused shader for gfx9, as the driver generator built it.
//
// Brought across mechanically and checked instruction for instruction against
// that generator.  Names, macros and the reasons behind any of it come next;
// what is here is only known to be the same shader.


	// Phase 0 - Parse ESP header + SA table lookup
	s_dcache_inv
	s_waitcnt lgkmcnt(0)
	s_mov_b32 s44, 0x10203
	v_mov_b32_e32 v1, s16
	v_lshlrev_b32_e32 v1, 5, v1
	v_add_u32_e32 v1, 40, v1
	v_mov_b32_e32 v2, s9
	v_add_co_u32_e32 v3, vcc, s8, v1
	v_addc_co_u32_e32 v4, vcc, 0, v2, vcc
	global_load_dwordx2 v[9:10], v[3:4], off
	global_load_dwordx2 v[5:6], v[3:4], off offset:16
	s_waitcnt vmcnt(0)
	v_mov_b32_e32 v11, v9
	v_mov_b32_e32 v12, v10
	global_load_dword v15, v[9:10], off offset:12
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s28, v15
	s_lshr_b32 s28, s28, 20
	s_and_b32 s28, 15, s28
	s_cmp_eq_u32 s28, 4
	s_cbranch_scc1 .L001
	s_cmp_eq_u32 s28, 6
	s_cbranch_scc1 .L000
	s_mov_b32 s26, -2
	s_branch .L005
.L000:
	v_mov_b32_e32 v42, 54
	s_branch .L002
.L001:
	v_mov_b32_e32 v42, 34
.L002:
	v_add_co_u32_e32 v11, vcc, v42, v9
	v_addc_co_u32_e32 v12, vcc, 0, v10, vcc
	global_load_dword v13, v[11:12], off
	s_waitcnt vmcnt(0)
	v_perm_b32 v13, v13, v13, s44
	v_mov_b32_e32 v16, s8
	v_mov_b32_e32 v17, s9
	global_load_dwordx2 v[18:19], v[16:17], off
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s24, v18
	v_readfirstlane_b32 s25, v19
	v_readfirstlane_b32 s22, v13
	s_mov_b32 s23, 0
	s_mov_b32 s26, -1
	v_mov_b32_e32 v16, s24
	v_mov_b32_e32 v17, s25
.L003:
	global_load_dword v20, v[16:17], off
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s27, v20
	v_add_co_u32_e32 v16, vcc, 0x68, v16
	v_addc_co_u32_e32 v17, vcc, 0, v17, vcc
	s_add_u32 s23, 1, s23
	s_nop 0
	s_cmp_eq_u32 s27, s22
	s_cbranch_scc1 .L004
	s_cmp_lt_u32 s23, 0x100
	s_cbranch_scc1 .L003
	s_branch .L005
.L004:
	s_sub_u32 s26, s23, 1
.L005:

	// Phase 1 - Save pre-crypto state + load extra sub[] fields
	v_mov_b32_e32 v30, s26
	v_mov_b32_e32 v31, v5
	v_mov_b32_e32 v32, v6
	v_mov_b32_e32 v33, v9
	v_mov_b32_e32 v34, v10
	v_mov_b32_e32 v39, v13
	global_load_dwordx2 v[36:37], v[3:4], off offset:8
	global_load_dword v35, v[3:4], off offset:24
	s_waitcnt vmcnt(0)
	global_load_dword v38, v[11:12], off offset:4
	s_waitcnt vmcnt(0)
	v_perm_b32 v38, v38, v38, s44
	global_store_dword v[3:4], v38, off offset:28

	// Phase 2 - Branch on miss/bypass - skip crypto entirely
	v_readfirstlane_b32 s26, v30
	s_cmp_ge_u32 s26, 0x100
	s_cbranch_scc1 .L025

	// Phase 3 - Load SA entry fields for the matched slot
	s_mul_i32 s27, s26, 0x68
	s_add_u32 s24, s24, s27
	s_addc_u32 s25, s25, 0
	v_mov_b32_e32 v12, s24
	v_mov_b32_e32 v13, s25
	global_load_dwordx4 v[1:4], v[12:13], off offset:16
	global_load_dwordx4 v[5:8], v[12:13], off offset:32
	global_load_dwordx2 v[14:15], v[12:13], off offset:48
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s24, v1
	v_readfirstlane_b32 s25, v2
	v_readfirstlane_b32 s52, v3
	v_readfirstlane_b32 s53, v4
	v_readfirstlane_b32 s34, v5
	v_readfirstlane_b32 s35, v6
	v_readfirstlane_b32 s36, v7
	v_readfirstlane_b32 s39, v14
	v_readfirstlane_b32 s55, v15

	// Phase 4 - Build AES-GCM nonce
	v_add_co_u32_e32 v12, vcc, v42, v33
	v_addc_co_u32_e32 v13, vcc, 0, v34, vcc
	global_load_dwordx2 v[14:15], v[12:13], off offset:8
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s37, v14
	v_readfirstlane_b32 s38, v15

	// Phase 5 - Compute ciphertext bounds
	v_readfirstlane_b32 s28, v42
	s_add_u32 s28, 32, s28
	v_readfirstlane_b32 s50, v35
	s_sub_u32 s50, s50, s28
	s_add_u32 s51, 15, s50
	s_lshr_b32 s51, s51, 4
	s_add_u32 s54, 2, s51

	// Phase 6 - Cooperative T-table load (VRAM -> LDS)
	s_mov_b32 s32, 0xff
	v_lshlrev_b32_e32 v9, 2, v0
	v_mov_b32_e32 v12, s34
	v_mov_b32_e32 v13, s35
	v_add_co_u32_e32 v12, vcc, v12, v9
	v_addc_co_u32_e32 v13, vcc, 0, v13, vcc
	global_load_dword v14, v[12:13], off
	global_load_dword v15, v[12:13], off offset:1024
	global_load_dword v16, v[12:13], off offset:2048
	global_load_dword v17, v[12:13], off offset:3072
	s_waitcnt vmcnt(0)
	ds_write_b32 v9, v14
	v_add_u32_e32 v10, 0x400, v9
	ds_write_b32 v10, v15
	v_add_u32_e32 v10, 0x800, v9
	ds_write_b32 v10, v16
	v_add_u32_e32 v10, 0xc00, v9
	ds_write_b32 v10, v17
	s_waitcnt lgkmcnt(0)
	s_barrier

	// Phase 7 - AES-CTR decrypt
	v_cmp_gt_u32_e32 vcc, s51, v0
	s_and_saveexec_b64 s[48:49], vcc
	s_cbranch_execz .L007
	s_mov_b32 s34, s24
	s_mov_b32 s35, s25
	v_lshlrev_b32_e32 v11, 4, v0
	v_add_co_u32_e32 v12, vcc, v42, v33
	v_addc_co_u32_e32 v13, vcc, 0, v34, vcc
	v_add_co_u32_e32 v12, vcc, 16, v12
	v_addc_co_u32_e32 v13, vcc, 0, v13, vcc
	v_add_co_u32_e32 v12, vcc, v12, v11
	v_addc_co_u32_e32 v13, vcc, 0, v13, vcc
	global_load_dwordx4 v[23:26], v[12:13], off
	v_mov_b32_e32 v1, s36
	v_mov_b32_e32 v2, s37
	v_mov_b32_e32 v3, s38
	v_add_u32_e32 v4, 2, v0
	v_perm_b32 v4, v4, v4, s44
	s_load_dwordx4 s[28:31], s[24:25], 0x0
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, s28, v1
	v_xor_b32_e32 v2, s29, v2
	v_xor_b32_e32 v3, s30, v3
	v_xor_b32_e32 v4, s31, v4
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_lshr_b32 s27, s39, 1
	s_add_u32 s27, -1, s27
	s_mov_b32 s33, 0
	s_load_dwordx4 s[28:31], s[24:25], 0x0
.L006:
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[56:59], s[24:25], 0x0
	v_and_b32_e32 v9, s32, v1
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s32, v2
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s32, v3
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s32, v4
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[28:31], s[24:25], 0x0
	v_and_b32_e32 v9, s32, v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v1, v14, v15
	v_xor_b32_e32 v1, v1, v16
	v_xor_b32_e32 v1, v1, v17
	v_xor_b32_e32 v1, s56, v1
	v_and_b32_e32 v9, s32, v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v2, v14, v15
	v_xor_b32_e32 v2, v2, v16
	v_xor_b32_e32 v2, v2, v17
	v_xor_b32_e32 v2, s57, v2
	v_and_b32_e32 v9, s32, v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v3, v14, v15
	v_xor_b32_e32 v3, v3, v16
	v_xor_b32_e32 v3, v3, v17
	v_xor_b32_e32 v3, s58, v3
	v_and_b32_e32 v9, s32, v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v4, v14, v15
	v_xor_b32_e32 v4, v4, v16
	v_xor_b32_e32 v4, v4, v17
	v_xor_b32_e32 v4, s59, v4
	s_add_u32 s33, 1, s33
	s_cmp_lt_u32 s33, s27
	s_cbranch_scc1 .L006
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[56:59], s[24:25], 0x0
	v_and_b32_e32 v9, s32, v1
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s32, v2
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s32, v3
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s32, v4
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	v_and_b32_e32 v9, s32, v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s32, v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v1, v14, v15
	v_or_b32_e32 v1, v1, v16
	v_or_b32_e32 v1, v1, v17
	v_xor_b32_e32 v1, s56, v1
	v_and_b32_e32 v9, s32, v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s32, v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v2, v14, v15
	v_or_b32_e32 v2, v2, v16
	v_or_b32_e32 v2, v2, v17
	v_xor_b32_e32 v2, s57, v2
	v_and_b32_e32 v9, s32, v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s32, v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v3, v14, v15
	v_or_b32_e32 v3, v3, v16
	v_or_b32_e32 v3, v3, v17
	v_xor_b32_e32 v3, s58, v3
	v_and_b32_e32 v9, s32, v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s32, v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v4, v14, v15
	v_or_b32_e32 v4, v4, v16
	v_or_b32_e32 v4, v4, v17
	v_xor_b32_e32 v4, s59, v4
	s_waitcnt vmcnt(0)
	v_xor_b32_e32 v23, v1, v23
	v_xor_b32_e32 v24, v2, v24
	v_xor_b32_e32 v25, v3, v25
	v_xor_b32_e32 v26, v4, v26
	v_add_co_u32_e32 v12, vcc, v11, v36
	v_addc_co_u32_e32 v13, vcc, 0, v37, vcc
	global_store_dwordx4 v[12:13], v[23:26], off
.L007:

	// Phase 7.5 - Compute AES(K, J0) for ICV finalization
	s_mov_b32 s24, s34
	s_mov_b32 s25, s35
	s_or_b64 exec, s[48:49], s[48:49]
	v_mov_b32_e32 v1, s36
	v_mov_b32_e32 v2, s37
	v_mov_b32_e32 v3, s38
	v_mov_b32_e32 v4, 0x1000000
	s_load_dwordx4 s[28:31], s[24:25], 0x0
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, s28, v1
	v_xor_b32_e32 v2, s29, v2
	v_xor_b32_e32 v3, s30, v3
	v_xor_b32_e32 v4, s31, v4
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_lshr_b32 s27, s39, 1
	s_add_u32 s27, -1, s27
	s_mov_b32 s33, 0
	s_load_dwordx4 s[28:31], s[24:25], 0x0
.L008:
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[56:59], s[24:25], 0x0
	v_and_b32_e32 v9, s32, v1
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s32, v2
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s32, v3
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s32, v4
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[28:31], s[24:25], 0x0
	v_and_b32_e32 v9, s32, v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v1, v14, v15
	v_xor_b32_e32 v1, v1, v16
	v_xor_b32_e32 v1, v1, v17
	v_xor_b32_e32 v1, s56, v1
	v_and_b32_e32 v9, s32, v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v2, v14, v15
	v_xor_b32_e32 v2, v2, v16
	v_xor_b32_e32 v2, v2, v17
	v_xor_b32_e32 v2, s57, v2
	v_and_b32_e32 v9, s32, v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v3, v14, v15
	v_xor_b32_e32 v3, v3, v16
	v_xor_b32_e32 v3, v3, v17
	v_xor_b32_e32 v3, s58, v3
	v_and_b32_e32 v9, s32, v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v4, v14, v15
	v_xor_b32_e32 v4, v4, v16
	v_xor_b32_e32 v4, v4, v17
	v_xor_b32_e32 v4, s59, v4
	s_add_u32 s33, 1, s33
	s_cmp_lt_u32 s33, s27
	s_cbranch_scc1 .L008
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[56:59], s[24:25], 0x0
	v_and_b32_e32 v9, s32, v1
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s32, v2
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s32, v3
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s32, v4
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	v_and_b32_e32 v9, s32, v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s32, v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v1, v14, v15
	v_or_b32_e32 v1, v1, v16
	v_or_b32_e32 v1, v1, v17
	v_xor_b32_e32 v1, s56, v1
	v_and_b32_e32 v9, s32, v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s32, v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v2, v14, v15
	v_or_b32_e32 v2, v2, v16
	v_or_b32_e32 v2, v2, v17
	v_xor_b32_e32 v2, s57, v2
	v_and_b32_e32 v9, s32, v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s32, v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v3, v14, v15
	v_or_b32_e32 v3, v3, v16
	v_or_b32_e32 v3, v3, v17
	v_xor_b32_e32 v3, s58, v3
	v_and_b32_e32 v9, s32, v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt vmcnt(15) lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s32, v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v4, v14, v15
	v_or_b32_e32 v4, v4, v16
	v_or_b32_e32 v4, v4, v17
	v_xor_b32_e32 v4, s59, v4
	v_mov_b32_e32 v19, v1
	v_mov_b32_e32 v20, v2
	v_mov_b32_e32 v21, v3
	v_mov_b32_e32 v22, v4
	s_barrier

	// Phase 8 - Parallel GHASH
	v_sub_u32_e32 v9, s54, v0
	v_add_u32_e32 v9, -1, v9
	v_max_i32_e32 v9, 0, v9
	v_lshlrev_b32_e32 v9, 4, v9
	v_mov_b32_e32 v12, s52
	v_mov_b32_e32 v13, s53
	v_add_co_u32_e32 v12, vcc, v12, v9
	v_addc_co_u32_e32 v13, vcc, 0, v13, vcc
	global_load_dwordx4 v[5:8], v[12:13], off
	v_mov_b32_e32 v14, 0
	v_mov_b32_e32 v15, 0
	v_mov_b32_e32 v16, 0
	v_mov_b32_e32 v17, 0
	v_cmp_eq_u32_e32 vcc, 0, v0
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L009
	v_mov_b32_e32 v14, v39
	v_mov_b32_e32 v15, v38
.L009:
	s_mov_b64 exec, s[40:41]
	v_add_u32_e32 v9, -1, v0
	v_cmp_gt_u32_e32 vcc, s51, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L010
	v_lshlrev_b32_e32 v9, 4, v9
	v_add_co_u32_e32 v12, vcc, v42, v33
	v_addc_co_u32_e32 v13, vcc, 0, v34, vcc
	v_add_co_u32_e32 v12, vcc, 16, v12
	v_addc_co_u32_e32 v13, vcc, 0, v13, vcc
	v_add_co_u32_e32 v12, vcc, v12, v9
	v_addc_co_u32_e32 v13, vcc, 0, v13, vcc
	global_load_dwordx4 v[14:17], v[12:13], off
	s_waitcnt vmcnt(0)
	v_sub_u32_e32 v9, s50, v9
	v_cmp_lt_u32_e32 vcc, 12, v9
	v_cndmask_b32_e32 v17, 0, v17, vcc
	v_cmp_lt_u32_e32 vcc, 8, v9
	v_cndmask_b32_e32 v16, 0, v16, vcc
	v_cmp_lt_u32_e32 vcc, 4, v9
	v_cndmask_b32_e32 v15, 0, v15, vcc
	v_perm_b32 v14, v14, v14, s44
	v_perm_b32 v15, v15, v15, s44
	v_perm_b32 v16, v16, v16, s44
	v_perm_b32 v17, v17, v17, s44
.L010:
	s_mov_b64 exec, s[40:41]
	s_add_u32 s42, 1, s51
	v_cmp_eq_u32_e32 vcc, s42, v0
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L011
	v_mov_b32_e32 v15, 64
	v_mov_b32_e32 v17, s50
	v_lshlrev_b32_e32 v17, 3, v17
.L011:
	s_mov_b64 exec, s[40:41]
	s_waitcnt vmcnt(0)
	v_perm_b32 v5, v5, v5, s44
	v_perm_b32 v6, v6, v6, s44
	v_perm_b32 v7, v7, v7, s44
	v_perm_b32 v8, v8, v8, s44
	v_mov_b32_e32 v1, 0
	v_mov_b32_e32 v2, 0
	v_mov_b32_e32 v3, 0
	v_mov_b32_e32 v4, 0
	v_cmp_gt_u32_e32 vcc, s54, v0
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L013
	v_mov_b32_e32 v1, 0
	v_mov_b32_e32 v2, 0
	v_mov_b32_e32 v3, 0
	v_mov_b32_e32 v4, 0
	v_mov_b32_e32 v10, 0xe1000000
	s_mov_b32 s33, 0
.L012:
	v_cmp_gt_i32_e32 vcc, 0, v14
	v_cndmask_b32_e32 v18, 0, v5, vcc
	v_xor_b32_e32 v1, v1, v18
	v_cndmask_b32_e32 v18, 0, v6, vcc
	v_xor_b32_e32 v2, v2, v18
	v_cndmask_b32_e32 v18, 0, v7, vcc
	v_xor_b32_e32 v3, v3, v18
	v_cndmask_b32_e32 v18, 0, v8, vcc
	v_xor_b32_e32 v4, v4, v18
	v_and_b32_e32 v9, 1, v8
	v_alignbit_b32 v8, v7, v8, 1
	v_alignbit_b32 v7, v6, v7, 1
	v_alignbit_b32 v6, v5, v6, 1
	v_lshrrev_b32_e32 v5, 1, v5
	v_cmp_ne_u32_e32 vcc, 0, v9
	v_cndmask_b32_e32 v18, 0, v10, vcc
	v_xor_b32_e32 v5, v5, v18
	v_alignbit_b32 v14, v14, v15, 31
	v_alignbit_b32 v15, v15, v16, 31
	v_alignbit_b32 v16, v16, v17, 31
	v_lshlrev_b32_e32 v17, 1, v17
	s_add_u32 s33, 1, s33
	s_cmp_lt_u32 s33, 0x80
	s_cbranch_scc1 .L012
.L013:
	s_mov_b64 exec, s[40:41]
	v_lshlrev_b32_e32 v10, 4, v0
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
	s_barrier
	v_and_b32_e32 v9, 1, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L014
	v_add_u32_e32 v9, 1, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	ds_read_b128 v[5:8], v9
	ds_read_b128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.L014:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 2, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L015
	v_add_u32_e32 v9, 2, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	ds_read_b128 v[5:8], v9
	ds_read_b128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.L015:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 4, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L016
	v_add_u32_e32 v9, 4, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	ds_read_b128 v[5:8], v9
	ds_read_b128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.L016:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 8, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L017
	v_add_u32_e32 v9, 8, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	ds_read_b128 v[5:8], v9
	ds_read_b128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.L017:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 16, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L018
	v_add_u32_e32 v9, 16, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	ds_read_b128 v[5:8], v9
	ds_read_b128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.L018:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 32, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L019
	v_add_u32_e32 v9, 32, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	ds_read_b128 v[5:8], v9
	ds_read_b128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.L019:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 64, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L020
	v_add_u32_e32 v9, 64, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	ds_read_b128 v[5:8], v9
	ds_read_b128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.L020:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 0x80, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L021
	v_add_u32_e32 v9, 0x80, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	ds_read_b128 v[5:8], v9
	ds_read_b128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.L021:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_mov_b32_e32 v9, 0
	ds_read_b128 v[1:4], v9
	s_waitcnt lgkmcnt(0)

	// Phase 9 - ICV verify (thread 0 only)
	v_cmp_eq_u32_e32 vcc, 0, v0
	s_and_saveexec_b64 s[48:49], vcc
	s_cbranch_execz .L024
	v_perm_b32 v1, v1, v1, s44
	v_perm_b32 v2, v2, v2, s44
	v_perm_b32 v3, v3, v3, s44
	v_perm_b32 v4, v4, v4, s44
	v_xor_b32_e32 v1, v1, v19
	v_xor_b32_e32 v2, v2, v20
	v_xor_b32_e32 v3, v3, v21
	v_xor_b32_e32 v4, v4, v22
	v_add_u32_e32 v9, -16, v35
	v_add_co_u32_e32 v12, vcc, v9, v33
	v_addc_co_u32_e32 v13, vcc, 0, v34, vcc
	global_load_dwordx4 v[14:17], v[12:13], off
	s_waitcnt vmcnt(0)
	v_xor_b32_e32 v14, v14, v1
	v_xor_b32_e32 v15, v15, v2
	v_xor_b32_e32 v16, v16, v3
	v_xor_b32_e32 v17, v17, v4
	v_or_b32_e32 v14, v14, v15
	v_or_b32_e32 v14, v14, v16
	v_or_b32_e32 v14, v14, v17
	v_cmp_ne_u32_e32 vcc, 0, v14
	s_cbranch_vccz .L022
	v_mov_b32_e32 v30, -3
.L022:

	// Phase 10 - ESP trailer strip + write verdict (thread 0)
	v_readfirstlane_b32 s27, v30
	s_cmp_ge_u32 s27, 0x100
	s_cbranch_scc1 .L023
	v_mov_b32_e32 v9, s50
	v_add_u32_e32 v9, -4, v9
	v_add_co_u32_e32 v12, vcc, v9, v36
	v_addc_co_u32_e32 v13, vcc, 0, v37, vcc
	global_load_dword v14, v[12:13], off
	s_waitcnt vmcnt(0)
	v_lshrrev_b32_e32 v9, 16, v14
	v_and_b32_e32 v9, 0xff, v9
	v_sub_u32_e32 v15, s50, v9
	v_add_u32_e32 v15, -2, v15
	v_add_co_u32_e32 v12, vcc, 18, v31
	v_addc_co_u32_e32 v13, vcc, 0, v32, vcc
	global_store_short v[12:13], v15, off
	v_lshrrev_b32_e32 v9, 24, v14
	v_lshlrev_b32_e32 v9, 8, v9
	v_mov_b32_e32 v16, s55
	v_or_b32_e32 v9, v16, v9
	v_add_co_u32_e32 v12, vcc, 16, v31
	v_addc_co_u32_e32 v13, vcc, 0, v32, vcc
	global_store_short v[12:13], v9, off
	s_cmp_eq_u32 s55, 0
	s_cbranch_scc0 .L023
	v_add_co_u32_e32 v12, vcc, 14, v33
	v_addc_co_u32_e32 v13, vcc, 0, v34, vcc
	global_load_ushort v14, v[12:13], off
	global_load_ushort v15, v[12:13], off offset:2
	global_load_ushort v16, v[12:13], off offset:4
	global_load_ushort v17, v[12:13], off offset:6
	global_load_ushort v18, v[12:13], off offset:8
	global_load_ushort v19, v[12:13], off offset:10
	global_load_ushort v20, v[12:13], off offset:12
	global_load_ushort v21, v[12:13], off offset:14
	global_load_ushort v22, v[12:13], off offset:16
	global_load_ushort v23, v[12:13], off offset:18
	s_waitcnt vmcnt(0)
	v_mov_b32_e32 v9, 0xffffffec
	v_mov_b32_e32 v18, -1
	v_add_co_u32_e32 v12, vcc, v9, v36
	v_addc_co_u32_e32 v13, vcc, v18, v37, vcc
	global_store_short v[12:13], v14, off
	global_store_short v[12:13], v15, off offset:2
	global_store_short v[12:13], v16, off offset:4
	global_store_short v[12:13], v17, off offset:6
	global_store_short v[12:13], v18, off offset:8
	global_store_short v[12:13], v19, off offset:10
	global_store_short v[12:13], v20, off offset:12
	global_store_short v[12:13], v21, off offset:14
	global_store_short v[12:13], v22, off offset:16
	global_store_short v[12:13], v23, off offset:18
.L023:
	v_add_co_u32_e32 v12, vcc, 8, v31
	v_addc_co_u32_e32 v13, vcc, 0, v32, vcc
	v_mov_b32_e32 v14, 0x100
	v_mov_b32_e32 v15, v30
	global_store_dwordx2 v[12:13], v[14:15], off
	s_waitcnt vmcnt(0)
.L024:

	// Phase 11 - Miss/bypass path verdict - thread 0 only
	s_branch .L026
.L025:
	v_cmp_eq_u32_e32 vcc, 0, v0
	s_and_saveexec_b64 s[48:49], vcc
	s_cbranch_execz .L026
	v_add_co_u32_e32 v12, vcc, 8, v31
	v_addc_co_u32_e32 v13, vcc, 0, v32, vcc
	v_mov_b32_e32 v14, 0x100
	v_mov_b32_e32 v15, v30
	global_store_dwordx2 v[12:13], v[14:15], off
	s_waitcnt vmcnt(0)
.L026:

	// Phase 12 - GPU-initiated SDMA dispatch (thread 0 only)
	s_load_dwordx2 s[24:25], s[8:9], 0x10
	s_load_dwordx2 s[26:27], s[8:9], 0x20
	s_waitcnt lgkmcnt(0)
	s_or_b32 s28, s26, s27
	s_cmp_eq_u32 s28, 0
	s_cbranch_scc1 .L028
	v_mov_b32_e32 v1, s26
	v_mov_b32_e32 v2, s27
	global_load_dwordx4 v[3:6], v[1:2], off offset:28
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s28, v5
	v_readfirstlane_b32 s29, v30
	s_cmp_ge_u32 s29, -2
	s_cbranch_scc0 .L027
	v_mov_b32_e32 v7, 1
	global_atomic_add v7, v[1:2], v7, off glc
	s_waitcnt vmcnt(0)
	v_lshlrev_b32_e32 v8, 3, v7
	v_sub_u32_e32 v8, v8, v7
	v_add_u32_e32 v8, v3, v8
	v_and_b32_e32 v8, v4, v8
	v_lshlrev_b32_e32 v8, 2, v8
	v_mov_b32_e32 v14, s25
	v_add_co_u32_e32 v9, vcc, s24, v8
	v_addc_co_u32_e32 v10, vcc, 0, v14, vcc
	global_store_dword v[9:10], v6, off
	v_add_u32_e32 v14, -1, v35
	global_store_dword v[9:10], v14, off offset:4
	v_mov_b32_e32 v14, 0
	global_store_dword v[9:10], v14, off offset:8
	global_store_dword v[9:10], v33, off offset:12
	global_store_dword v[9:10], v34, off offset:16
	v_mov_b32_e32 v14, 0xffffffec
	v_add_co_u32_e32 v15, vcc, v14, v36
	v_mov_b32_e32 v14, -1
	v_addc_co_u32_e32 v16, vcc, v14, v37, vcc
	global_store_dword v[9:10], v15, off offset:20
	global_store_dword v[9:10], v16, off offset:24
	s_waitcnt vmcnt(0)
.L027:
	v_mov_b32_e32 v7, 1
	global_atomic_add v7, v[1:2], v7, off offset:4 glc
	s_waitcnt vmcnt(0)
	v_add_u32_e32 v7, 1, v7
	v_readfirstlane_b32 s29, v7
	s_cmp_eq_u32 s29, s28
	s_cbranch_scc0 .L028
	v_mov_b32_e32 v7, 0
	global_atomic_add v7, v[1:2], v7, off glc
	s_waitcnt vmcnt(0)
	global_store_dword v[1:2], v7, off offset:52
	v_mov_b32_e32 v7, 1
	global_store_dword v[1:2], v7, off offset:48
	s_waitcnt vmcnt(0)
.L028:
	s_endpgm
