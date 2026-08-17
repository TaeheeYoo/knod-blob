// SPDX-License-Identifier: GPL-2.0-or-later
//
// The IPsec fused shader for gfx10, as the driver generator built it.
//
// Brought across mechanically and checked instruction for instruction against
// that generator.  Names, macros and the reasons behind any of it come next;
// what is here is only known to be the same shader.


	// Phase 0 - Parse ESP header + SA table lookup
	s_dcache_inv
	s_waitcnt lgkmcnt(0)
	s_mov_b32 s[SR_BSWAP], 0x10203
	v_mov_b32_e32 v1, s16
	v_lshlrev_b32_e32 v1, 5, v1
	v_add_nc_u32_e32 v1, 40, v1
	v_mov_b32_e32 v2, s9
	v_add_co_u32 v3, vcc, s8, v1
	v_add_co_ci_u32_e32 v4, vcc, 0, v2, vcc
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
	v_mov_b32_e32 v[VR_SAVE_ESP_OFF], 54
	s_branch .L002
.L001:
	v_mov_b32_e32 v[VR_SAVE_ESP_OFF], 34
.L002:
	v_add_co_u32 v11, vcc, v[VR_SAVE_ESP_OFF], v9
	v_add_co_ci_u32_e32 v12, vcc, 0, v10, vcc
	global_load_dwordx2 v[20:21], v[11:12], off
	s_waitcnt vmcnt(0)
	v_alignbit_b32 v13, v21, v20, 16
	v_perm_b32 v13, v13, v13, s[SR_BSWAP]
	v_mov_b32_e32 v16, s8
	v_mov_b32_e32 v17, s9
	global_load_dwordx2 v[18:19], v[16:17], off
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s24, v18
	v_readfirstlane_b32 s25, v[VR_J0_0]
	v_readfirstlane_b32 s22, v13
	s_mov_b32 s23, 0
	s_mov_b32 s26, -1
	v_mov_b32_e32 v16, s24
	v_mov_b32_e32 v17, s25
.L003:
	global_load_dword v20, v[16:17], off
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s27, v20
	v_add_co_u32 v16, vcc, 0x68, v16
	v_add_co_ci_u32_e32 v17, vcc, 0, v17, vcc
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
	v_mov_b32_e32 v[VR_SAVE_SLOT], s26
	v_mov_b32_e32 v[VR_SAVE_BD_LO], v5
	v_mov_b32_e32 v[VR_SAVE_BD_HI], v6
	v_mov_b32_e32 v[VR_SAVE_PKT_LO], v9
	v_mov_b32_e32 v[VR_SAVE_PKT_HI], v10
	v_mov_b32_e32 v[VR_SAVE_SPI], v13
	global_load_dwordx2 v[VR_SAVE_OUT_LO:VR_SAVE_OUT_HI], v[3:4], off offset:8
	global_load_dword v[VR_SAVE_PKTLEN], v[3:4], off offset:24
	s_waitcnt vmcnt(0)
	global_load_dwordx2 v[20:21], v[11:12], off offset:4
	s_waitcnt vmcnt(0)
	v_alignbit_b32 v[VR_SAVE_SEQ], v21, v20, 16
	v_perm_b32 v[VR_SAVE_SEQ], v[VR_SAVE_SEQ], v[VR_SAVE_SEQ], s[SR_BSWAP]
	global_store_dword v[3:4], v[VR_SAVE_SEQ], off offset:28 glc slc

	// Phase 2 - Branch on miss/bypass - skip crypto entirely
	v_readfirstlane_b32 s26, v[VR_SAVE_SLOT]
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
	v_readfirstlane_b32 s[SR_HTABLE_LO], v3
	v_readfirstlane_b32 s[SR_HTABLE_HI], v4
	v_readfirstlane_b32 s[SR_T_ADDR], v5
	v_readfirstlane_b32 s35, v6
	v_readfirstlane_b32 s[SR_IV0], v7
	v_readfirstlane_b32 s[SR_NR_ROUNDS], v14
	v_readfirstlane_b32 s[SR_SA_MODE], v15

	// Phase 4 - Build AES-GCM nonce
	v_add_co_u32 v12, vcc, v[VR_SAVE_ESP_OFF], v[VR_SAVE_PKT_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_PKT_HI], vcc
	global_load_dwordx4 v[14:17], v[12:13], off offset:8
	s_waitcnt vmcnt(0)
	v_alignbit_b32 v14, v15, v14, 16
	v_alignbit_b32 v15, v16, v15, 16
	v_readfirstlane_b32 s[SR_IV1], v14
	v_readfirstlane_b32 s[SR_IV2], v15

	// Phase 5 - Compute ciphertext bounds
	v_readfirstlane_b32 s28, v[VR_SAVE_ESP_OFF]
	s_add_u32 s28, 32, s28
	v_readfirstlane_b32 s[SR_CTEXT_LEN], v[VR_SAVE_PKTLEN]
	s_sub_u32 s[SR_CTEXT_LEN], s[SR_CTEXT_LEN], s28
	s_add_u32 s[SR_NBLOCKS_GCM], 15, s[SR_CTEXT_LEN]
	s_lshr_b32 s[SR_NBLOCKS_GCM], s[SR_NBLOCKS_GCM], 4
	s_add_u32 s[SR_TOTAL_GHASH_BLK], 2, s[SR_NBLOCKS_GCM]

	// Phase 6 - Cooperative T-table load (VRAM -> LDS)
	s_mov_b32 s[SR_MASK], 0xff
	v_lshlrev_b32_e32 v9, 2, v[VR_TID]
	v_mov_b32_e32 v12, s[SR_T_ADDR]
	v_mov_b32_e32 v13, s35
	v_add_co_u32 v12, vcc, v12, v9
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	global_load_dword v14, v[12:13], off
	global_load_dword v15, v[12:13], off offset:1024
	v_add_co_u32 v12, vcc, 0x800, v12
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	global_load_dword v16, v[12:13], off
	global_load_dword v17, v[12:13], off offset:1024
	s_waitcnt vmcnt(0)
	ds_write_b32 v9, v14
	v_add_nc_u32_e32 v10, 0x400, v9
	ds_write_b32 v10, v15
	v_add_nc_u32_e32 v10, 0x800, v9
	ds_write_b32 v10, v16
	v_add_nc_u32_e32 v10, 0xc00, v9
	ds_write_b32 v10, v17
	s_waitcnt lgkmcnt(0)
	s_barrier

	// Phase 7 - AES-CTR decrypt
	v_cmp_gt_u32_e32 vcc, s[SR_NBLOCKS_GCM], v[VR_TID]
	s_and_saveexec_b64 s[48:49], vcc
	s_cbranch_execz .L007
	s_mov_b32 s[SR_T_ADDR], s24
	s_mov_b32 s35, s25
	v_lshlrev_b32_e32 v11, 4, v[VR_TID]
	v_add_co_u32 v12, vcc, v[VR_SAVE_ESP_OFF], v[VR_SAVE_PKT_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_PKT_HI], vcc
	v_add_co_u32 v12, vcc, 16, v12
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	v_add_co_u32 v12, vcc, v12, v11
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	global_load_dwordx4 v[VR_PREFETCH0:VR_PREFETCH3], v[12:13], off
	global_load_dword v[VR_PREFETCH4], v[12:13], off offset:16
	v_mov_b32_e32 v1, s[SR_IV0]
	v_mov_b32_e32 v2, s[SR_IV1]
	v_mov_b32_e32 v3, s[SR_IV2]
	v_add_nc_u32_e32 v4, 2, v[VR_TID]
	v_perm_b32 v4, v4, v4, s[SR_BSWAP]
	s_load_dwordx4 s[28:31], s[24:25], null
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, s28, v1
	v_xor_b32_e32 v2, s29, v2
	v_xor_b32_e32 v3, s30, v3
	v_xor_b32_e32 v4, s31, v4
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_lshr_b32 s27, s[SR_NR_ROUNDS], 1
	s_add_u32 s27, -1, s27
	s_mov_b32 s[SR_LOOP_CTR], 0
	s_load_dwordx4 s[28:31], s[24:25], null
.L006:
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[SR_RK2:SR_RK2+3], s[24:25], null
	v_and_b32_e32 v9, s[SR_MASK], v1
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s[SR_MASK], v2
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s[SR_MASK], v3
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s[SR_MASK], v4
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[28:31], s[24:25], null
	v_and_b32_e32 v9, s[SR_MASK], v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v14, v15
	v_xor_b32_e32 v1, v1, v16
	v_xor_b32_e32 v1, v1, v17
	v_xor_b32_e32 v1, s[SR_RK2], v1
	v_and_b32_e32 v9, s[SR_MASK], v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v2, v14, v15
	v_xor_b32_e32 v2, v2, v16
	v_xor_b32_e32 v2, v2, v17
	v_xor_b32_e32 v2, s57, v2
	v_and_b32_e32 v9, s[SR_MASK], v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v3, v14, v15
	v_xor_b32_e32 v3, v3, v16
	v_xor_b32_e32 v3, v3, v17
	v_xor_b32_e32 v3, s58, v3
	v_and_b32_e32 v9, s[SR_MASK], v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v4, v14, v15
	v_xor_b32_e32 v4, v4, v16
	v_xor_b32_e32 v4, v4, v17
	v_xor_b32_e32 v4, s59, v4
	s_add_u32 s[SR_LOOP_CTR], 1, s[SR_LOOP_CTR]
	s_cmp_lt_u32 s[SR_LOOP_CTR], s27
	s_cbranch_scc1 .L006
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[SR_RK2:SR_RK2+3], s[24:25], null
	v_and_b32_e32 v9, s[SR_MASK], v1
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s[SR_MASK], v2
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s[SR_MASK], v3
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s[SR_MASK], v4
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	v_and_b32_e32 v9, s[SR_MASK], v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s[SR_MASK], v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v1, v14, v15
	v_or_b32_e32 v1, v1, v16
	v_or_b32_e32 v1, v1, v17
	v_xor_b32_e32 v1, s[SR_RK2], v1
	v_and_b32_e32 v9, s[SR_MASK], v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s[SR_MASK], v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v2, v14, v15
	v_or_b32_e32 v2, v2, v16
	v_or_b32_e32 v2, v2, v17
	v_xor_b32_e32 v2, s57, v2
	v_and_b32_e32 v9, s[SR_MASK], v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s[SR_MASK], v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v3, v14, v15
	v_or_b32_e32 v3, v3, v16
	v_or_b32_e32 v3, v3, v17
	v_xor_b32_e32 v3, s58, v3
	v_and_b32_e32 v9, s[SR_MASK], v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s[SR_MASK], v14
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
	v_alignbit_b32 v[VR_PREFETCH0], v[VR_PREFETCH1], v[VR_PREFETCH0], 16
	v_alignbit_b32 v[VR_PREFETCH1], v[VR_PREFETCH2], v[VR_PREFETCH1], 16
	v_alignbit_b32 v[VR_PREFETCH2], v[VR_PREFETCH3], v[VR_PREFETCH2], 16
	v_alignbit_b32 v[VR_PREFETCH3], v[VR_PREFETCH4], v[VR_PREFETCH3], 16
	v_xor_b32_e32 v[VR_PREFETCH0], v1, v[VR_PREFETCH0]
	v_xor_b32_e32 v[VR_PREFETCH1], v2, v[VR_PREFETCH1]
	v_xor_b32_e32 v[VR_PREFETCH2], v3, v[VR_PREFETCH2]
	v_xor_b32_e32 v[VR_PREFETCH3], v4, v[VR_PREFETCH3]
	v_add_co_u32 v12, vcc, v11, v[VR_SAVE_OUT_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_OUT_HI], vcc
	global_store_dwordx4 v[12:13], v[VR_PREFETCH0:VR_PREFETCH3], off glc slc
.L007:

	// Phase 7.5 - Compute AES(K, J0) for ICV finalization
	s_mov_b32 s24, s[SR_T_ADDR]
	s_mov_b32 s25, s35
	s_or_b64 exec, s[48:49], s[48:49]
	v_mov_b32_e32 v1, s[SR_IV0]
	v_mov_b32_e32 v2, s[SR_IV1]
	v_mov_b32_e32 v3, s[SR_IV2]
	v_mov_b32_e32 v4, 0x1000000
	s_load_dwordx4 s[28:31], s[24:25], null
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, s28, v1
	v_xor_b32_e32 v2, s29, v2
	v_xor_b32_e32 v3, s30, v3
	v_xor_b32_e32 v4, s31, v4
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_lshr_b32 s27, s[SR_NR_ROUNDS], 1
	s_add_u32 s27, -1, s27
	s_mov_b32 s[SR_LOOP_CTR], 0
	s_load_dwordx4 s[28:31], s[24:25], null
.L008:
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[SR_RK2:SR_RK2+3], s[24:25], null
	v_and_b32_e32 v9, s[SR_MASK], v1
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s[SR_MASK], v2
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s[SR_MASK], v3
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s[SR_MASK], v4
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[28:31], s[24:25], null
	v_and_b32_e32 v9, s[SR_MASK], v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v14, v15
	v_xor_b32_e32 v1, v1, v16
	v_xor_b32_e32 v1, v1, v17
	v_xor_b32_e32 v1, s[SR_RK2], v1
	v_and_b32_e32 v9, s[SR_MASK], v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v2, v14, v15
	v_xor_b32_e32 v2, v2, v16
	v_xor_b32_e32 v2, v2, v17
	v_xor_b32_e32 v2, s57, v2
	v_and_b32_e32 v9, s[SR_MASK], v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v3, v14, v15
	v_xor_b32_e32 v3, v3, v16
	v_xor_b32_e32 v3, v3, v17
	v_xor_b32_e32 v3, s58, v3
	v_and_b32_e32 v9, s[SR_MASK], v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v4, v14, v15
	v_xor_b32_e32 v4, v4, v16
	v_xor_b32_e32 v4, v4, v17
	v_xor_b32_e32 v4, s59, v4
	s_add_u32 s[SR_LOOP_CTR], 1, s[SR_LOOP_CTR]
	s_cmp_lt_u32 s[SR_LOOP_CTR], s27
	s_cbranch_scc1 .L008
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	s_load_dwordx4 s[SR_RK2:SR_RK2+3], s[24:25], null
	v_and_b32_e32 v9, s[SR_MASK], v1
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s[SR_MASK], v2
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s[SR_MASK], v3
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s[SR_MASK], v4
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	v_and_b32_e32 v9, s[SR_MASK], v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s[SR_MASK], v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v1, v14, v15
	v_or_b32_e32 v1, v1, v16
	v_or_b32_e32 v1, v1, v17
	v_xor_b32_e32 v1, s[SR_RK2], v1
	v_and_b32_e32 v9, s[SR_MASK], v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s[SR_MASK], v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v2, v14, v15
	v_or_b32_e32 v2, v2, v16
	v_or_b32_e32 v2, v2, v17
	v_xor_b32_e32 v2, s57, v2
	v_and_b32_e32 v9, s[SR_MASK], v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s[SR_MASK], v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v3, v14, v15
	v_or_b32_e32 v3, v3, v16
	v_or_b32_e32 v3, v3, v17
	v_xor_b32_e32 v3, s58, v3
	v_and_b32_e32 v9, s[SR_MASK], v8
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s[SR_MASK], v9
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	ds_read_b32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_lshrrev_b32_e32 v14, 8, v14
	v_and_b32_e32 v14, s[SR_MASK], v14
	v_and_b32_e32 v15, 0xff00, v15
	v_lshlrev_b32_e32 v16, 8, v16
	v_and_b32_e32 v16, 0xff0000, v16
	v_lshlrev_b32_e32 v17, 16, v17
	v_and_b32_e32 v17, 0xff000000, v17
	v_or_b32_e32 v4, v14, v15
	v_or_b32_e32 v4, v4, v16
	v_or_b32_e32 v4, v4, v17
	v_xor_b32_e32 v4, s59, v4
	v_mov_b32_e32 v[VR_J0_0], v1
	v_mov_b32_e32 v20, v2
	v_mov_b32_e32 v21, v3
	v_mov_b32_e32 v[VR_J0_3], v4
	s_barrier

	// Phase 8 - Parallel GHASH
	v_sub_nc_u32_e32 v9, s[SR_TOTAL_GHASH_BLK], v[VR_TID]
	v_add_nc_u32_e32 v9, -1, v9
	v_max_i32_e32 v9, 0, v9
	v_lshlrev_b32_e32 v9, 4, v9
	v_mov_b32_e32 v12, s[SR_HTABLE_LO]
	v_mov_b32_e32 v13, s[SR_HTABLE_HI]
	v_add_co_u32 v12, vcc, v12, v9
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	global_load_dwordx4 v[5:8], v[12:13], off
	v_mov_b32_e32 v14, 0
	v_mov_b32_e32 v15, 0
	v_mov_b32_e32 v16, 0
	v_mov_b32_e32 v17, 0
	v_cmp_eq_u32_e32 vcc, 0, v[VR_TID]
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L009
	v_mov_b32_e32 v14, v[VR_SAVE_SPI]
	v_mov_b32_e32 v15, v[VR_SAVE_SEQ]
.L009:
	s_mov_b64 exec, s[40:41]
	v_add_nc_u32_e32 v9, -1, v[VR_TID]
	v_cmp_gt_u32_e32 vcc, s[SR_NBLOCKS_GCM], v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L010
	v_lshlrev_b32_e32 v9, 4, v9
	v_add_co_u32 v12, vcc, v[VR_SAVE_ESP_OFF], v[VR_SAVE_PKT_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_PKT_HI], vcc
	v_add_co_u32 v12, vcc, 16, v12
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	v_add_co_u32 v12, vcc, v12, v9
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	global_load_dwordx4 v[14:17], v[12:13], off
	global_load_dword v11, v[12:13], off offset:16
	s_waitcnt vmcnt(0)
	v_alignbit_b32 v14, v15, v14, 16
	v_alignbit_b32 v15, v16, v15, 16
	v_alignbit_b32 v16, v17, v16, 16
	v_alignbit_b32 v17, v11, v17, 16
	v_sub_nc_u32_e32 v9, s[SR_CTEXT_LEN], v9
	v_cmp_lt_u32_e32 vcc, 12, v9
	v_cndmask_b32_e32 v17, 0, v17, vcc
	v_cmp_lt_u32_e32 vcc, 8, v9
	v_cndmask_b32_e32 v16, 0, v16, vcc
	v_cmp_lt_u32_e32 vcc, 4, v9
	v_cndmask_b32_e32 v15, 0, v15, vcc
	v_perm_b32 v14, v14, v14, s[SR_BSWAP]
	v_perm_b32 v15, v15, v15, s[SR_BSWAP]
	v_perm_b32 v16, v16, v16, s[SR_BSWAP]
	v_perm_b32 v17, v17, v17, s[SR_BSWAP]
.L010:
	s_mov_b64 exec, s[40:41]
	s_add_u32 s42, 1, s[SR_NBLOCKS_GCM]
	v_cmp_eq_u32_e32 vcc, s42, v[VR_TID]
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L011
	v_mov_b32_e32 v15, 64
	v_mov_b32_e32 v17, s[SR_CTEXT_LEN]
	v_lshlrev_b32_e32 v17, 3, v17
.L011:
	s_mov_b64 exec, s[40:41]
	s_waitcnt vmcnt(0)
	v_perm_b32 v5, v5, v5, s[SR_BSWAP]
	v_perm_b32 v6, v6, v6, s[SR_BSWAP]
	v_perm_b32 v7, v7, v7, s[SR_BSWAP]
	v_perm_b32 v8, v8, v8, s[SR_BSWAP]
	v_mov_b32_e32 v1, 0
	v_mov_b32_e32 v2, 0
	v_mov_b32_e32 v3, 0
	v_mov_b32_e32 v4, 0
	v_cmp_gt_u32_e32 vcc, s[SR_TOTAL_GHASH_BLK], v[VR_TID]
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L013
	v_mov_b32_e32 v1, 0
	v_mov_b32_e32 v2, 0
	v_mov_b32_e32 v3, 0
	v_mov_b32_e32 v4, 0
	v_mov_b32_e32 v10, 0xe1000000
	s_mov_b32 s[SR_LOOP_CTR], 0
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
	s_add_u32 s[SR_LOOP_CTR], 1, s[SR_LOOP_CTR]
	s_cmp_lt_u32 s[SR_LOOP_CTR], 0x80
	s_cbranch_scc1 .L012
.L013:
	s_mov_b64 exec, s[40:41]
	v_lshlrev_b32_e32 v10, 4, v[VR_TID]
	ds_write_b128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
	s_barrier
	v_and_b32_e32 v9, 1, v[VR_TID]
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L014
	v_add_nc_u32_e32 v9, 1, v[VR_TID]
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v[VR_TID]
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
	v_and_b32_e32 v9, 2, v[VR_TID]
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L015
	v_add_nc_u32_e32 v9, 2, v[VR_TID]
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v[VR_TID]
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
	v_and_b32_e32 v9, 4, v[VR_TID]
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L016
	v_add_nc_u32_e32 v9, 4, v[VR_TID]
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v[VR_TID]
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
	v_and_b32_e32 v9, 8, v[VR_TID]
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L017
	v_add_nc_u32_e32 v9, 8, v[VR_TID]
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v[VR_TID]
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
	v_and_b32_e32 v9, 16, v[VR_TID]
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L018
	v_add_nc_u32_e32 v9, 16, v[VR_TID]
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v[VR_TID]
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
	v_and_b32_e32 v9, 32, v[VR_TID]
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L019
	v_add_nc_u32_e32 v9, 32, v[VR_TID]
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v[VR_TID]
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
	v_and_b32_e32 v9, 64, v[VR_TID]
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L020
	v_add_nc_u32_e32 v9, 64, v[VR_TID]
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v[VR_TID]
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
	v_and_b32_e32 v9, 0x80, v[VR_TID]
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .L021
	v_add_nc_u32_e32 v9, 0x80, v[VR_TID]
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v[VR_TID]
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
	v_cmp_eq_u32_e32 vcc, 0, v[VR_TID]
	s_and_saveexec_b64 s[48:49], vcc
	s_cbranch_execz .L024
	v_perm_b32 v1, v1, v1, s[SR_BSWAP]
	v_perm_b32 v2, v2, v2, s[SR_BSWAP]
	v_perm_b32 v3, v3, v3, s[SR_BSWAP]
	v_perm_b32 v4, v4, v4, s[SR_BSWAP]
	v_xor_b32_e32 v1, v1, v[VR_J0_0]
	v_xor_b32_e32 v2, v2, v20
	v_xor_b32_e32 v3, v3, v21
	v_xor_b32_e32 v4, v4, v[VR_J0_3]
	v_add_nc_u32_e32 v9, -16, v[VR_SAVE_PKTLEN]
	v_add_co_u32 v12, vcc, v9, v[VR_SAVE_PKT_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_PKT_HI], vcc
	global_load_dwordx4 v[14:17], v[12:13], off
	global_load_dword v11, v[12:13], off offset:16
	s_waitcnt vmcnt(0)
	v_alignbit_b32 v14, v15, v14, 16
	v_alignbit_b32 v15, v16, v15, 16
	v_alignbit_b32 v16, v17, v16, 16
	v_alignbit_b32 v17, v11, v17, 16
	v_xor_b32_e32 v14, v14, v1
	v_xor_b32_e32 v15, v15, v2
	v_xor_b32_e32 v16, v16, v3
	v_xor_b32_e32 v17, v17, v4
	v_or_b32_e32 v14, v14, v15
	v_or_b32_e32 v14, v14, v16
	v_or_b32_e32 v14, v14, v17
	v_cmp_ne_u32_e32 vcc, 0, v14
	s_cbranch_vccz .L022
	v_mov_b32_e32 v[VR_SAVE_SLOT], -3
.L022:

	// Phase 10 - ESP trailer strip + write verdict (thread 0)
	v_readfirstlane_b32 s27, v[VR_SAVE_SLOT]
	s_cmp_ge_u32 s27, 0x100
	s_cbranch_scc1 .L023
	v_mov_b32_e32 v9, s[SR_CTEXT_LEN]
	v_add_nc_u32_e32 v9, -4, v9
	v_add_co_u32 v12, vcc, v9, v[VR_SAVE_OUT_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_OUT_HI], vcc
	global_load_dword v14, v[12:13], off
	s_waitcnt vmcnt(0)
	v_lshrrev_b32_e32 v9, 16, v14
	v_and_b32_e32 v9, 0xff, v9
	v_sub_nc_u32_e32 v15, s[SR_CTEXT_LEN], v9
	v_add_nc_u32_e32 v15, -2, v15
	v_add_co_u32 v12, vcc, 18, v[VR_SAVE_BD_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_BD_HI], vcc
	global_store_short v[12:13], v15, off glc slc
	v_lshrrev_b32_e32 v9, 24, v14
	v_lshlrev_b32_e32 v9, 8, v9
	v_mov_b32_e32 v16, s[SR_SA_MODE]
	v_or_b32_e32 v9, v16, v9
	v_add_co_u32 v12, vcc, 16, v[VR_SAVE_BD_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_BD_HI], vcc
	global_store_short v[12:13], v9, off glc slc
	s_cmp_eq_u32 s[SR_SA_MODE], 0
	s_cbranch_scc0 .L023
	v_add_co_u32 v12, vcc, 14, v[VR_SAVE_PKT_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_PKT_HI], vcc
	global_load_ushort v14, v[12:13], off
	global_load_ushort v15, v[12:13], off offset:2
	global_load_ushort v16, v[12:13], off offset:4
	global_load_ushort v17, v[12:13], off offset:6
	global_load_ushort v18, v[12:13], off offset:8
	global_load_ushort v[VR_J0_0], v[12:13], off offset:10
	global_load_ushort v20, v[12:13], off offset:12
	global_load_ushort v21, v[12:13], off offset:14
	global_load_ushort v[VR_J0_3], v[12:13], off offset:16
	global_load_ushort v[VR_PREFETCH0], v[12:13], off offset:18
	s_waitcnt vmcnt(0)
	v_mov_b32_e32 v9, 0xffffffec
	v_mov_b32_e32 v18, -1
	v_add_co_u32 v12, vcc, v9, v[VR_SAVE_OUT_LO]
	v_add_co_ci_u32_e32 v13, vcc, v18, v[VR_SAVE_OUT_HI], vcc
	global_store_short v[12:13], v14, off glc slc
	global_store_short v[12:13], v15, off offset:2 glc slc
	global_store_short v[12:13], v16, off offset:4 glc slc
	global_store_short v[12:13], v17, off offset:6 glc slc
	global_store_short v[12:13], v18, off offset:8 glc slc
	global_store_short v[12:13], v[VR_J0_0], off offset:10 glc slc
	global_store_short v[12:13], v20, off offset:12 glc slc
	global_store_short v[12:13], v21, off offset:14 glc slc
	global_store_short v[12:13], v[VR_J0_3], off offset:16 glc slc
	global_store_short v[12:13], v[VR_PREFETCH0], off offset:18 glc slc
.L023:
	v_add_co_u32 v12, vcc, 8, v[VR_SAVE_BD_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_BD_HI], vcc
	v_mov_b32_e32 v14, 0x100
	v_mov_b32_e32 v15, v[VR_SAVE_SLOT]
	global_store_dwordx2 v[12:13], v[14:15], off glc slc
	s_waitcnt vmcnt(0)
.L024:

	// Phase 11 - Miss/bypass path verdict - thread 0 only
	s_branch .L026
.L025:
	v_cmp_eq_u32_e32 vcc, 0, v[VR_TID]
	s_and_saveexec_b64 s[48:49], vcc
	s_cbranch_execz .L026
	v_add_co_u32 v12, vcc, 8, v[VR_SAVE_BD_LO]
	v_add_co_ci_u32_e32 v13, vcc, 0, v[VR_SAVE_BD_HI], vcc
	v_mov_b32_e32 v14, 0x100
	v_mov_b32_e32 v15, v[VR_SAVE_SLOT]
	global_store_dwordx2 v[12:13], v[14:15], off glc slc
	s_waitcnt vmcnt(0)
.L026:

	// Phase 12 - GPU-initiated SDMA dispatch (thread 0 only)
	s_load_dwordx2 s[24:25], s[8:9], 0x10
	s_load_dwordx2 s[26:27], s[8:9], 0x20
	s_waitcnt lgkmcnt(0)
	s_or_b64 s[28:29], s[26:27], s[26:27]
	s_cbranch_scc0 .L028
	v_mov_b32_e32 v1, s26
	v_mov_b32_e32 v2, s27
	global_load_dwordx4 v[3:6], v[1:2], off offset:28
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s28, v5
	v_readfirstlane_b32 s29, v[VR_SAVE_SLOT]
	s_cmp_ge_u32 s29, -2
	s_cbranch_scc0 .L027
	v_mov_b32_e32 v7, 1
	global_atomic_add v7, v[1:2], v7, off glc
	s_waitcnt vmcnt(0)
	v_lshlrev_b32_e32 v8, 3, v7
	v_sub_nc_u32_e32 v8, v8, v7
	v_add_nc_u32_e32 v8, v3, v8
	v_and_b32_e32 v8, v4, v8
	v_lshlrev_b32_e32 v8, 2, v8
	v_mov_b32_e32 v14, s25
	v_add_co_u32 v9, vcc, s24, v8
	v_add_co_ci_u32_e32 v10, vcc, 0, v14, vcc
	global_store_dword v[9:10], v6, off glc slc
	v_add_nc_u32_e32 v14, -1, v[VR_SAVE_PKTLEN]
	global_store_dword v[9:10], v14, off offset:4 glc slc
	v_mov_b32_e32 v14, 0
	global_store_dword v[9:10], v14, off offset:8 glc slc
	global_store_dword v[9:10], v[VR_SAVE_PKT_LO], off offset:12 glc slc
	global_store_dword v[9:10], v[VR_SAVE_PKT_HI], off offset:16 glc slc
	v_mov_b32_e32 v14, 0xffffffec
	v_add_co_u32 v15, vcc, v14, v[VR_SAVE_OUT_LO]
	v_mov_b32_e32 v14, -1
	v_add_co_ci_u32_e32 v16, vcc, v14, v[VR_SAVE_OUT_HI], vcc
	global_store_dword v[9:10], v15, off offset:20 glc slc
	global_store_dword v[9:10], v16, off offset:24 glc slc
	s_waitcnt vmcnt(0)
.L027:
	v_mov_b32_e32 v7, 1
	global_atomic_add v7, v[1:2], v7, off offset:4 glc
	s_waitcnt vmcnt(0)
	v_add_nc_u32_e32 v7, 1, v7
	v_readfirstlane_b32 s29, v7
	s_cmp_eq_u32 s29, s28
	s_cbranch_scc0 .L028
	v_mov_b32_e32 v7, 0
	global_atomic_add v7, v[1:2], v7, off glc
	s_waitcnt vmcnt(0)
	global_store_dword v[1:2], v7, off offset:52 glc slc
	v_mov_b32_e32 v7, 1
	global_store_dword v[1:2], v7, off offset:48 glc slc
	s_waitcnt vmcnt(0)
.L028:
	s_endpgm
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
	s_code_end
