// SPDX-License-Identifier: GPL-2.0-or-later
//
// One stage of the pipeline on its own, for measuring what it costs.
//
// Brought across the same way the pipeline was, and checked the same way.

	s_dcache_inv
	s_waitcnt lgkmcnt(0)
	s_mov_b32 s44, 0x10203
	v_mov_b32_e32 v16, s8
	v_mov_b32_e32 v17, s9
	GLOBAL_LOAD_DWORDX2 v[18:19], v[16:17], off
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s24, v18
	v_readfirstlane_b32 s25, v19
	v_mov_b32_e32 v12, s24
	v_mov_b32_e32 v13, s25
	GLOBAL_LOAD_DWORDX4 v[1:4], v[12:13], off offset:16
	GLOBAL_LOAD_DWORDX4 v[5:8], v[12:13], off offset:32
	GLOBAL_LOAD_DWORDX2 v[14:15], v[12:13], off offset:48
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s24, v1
	v_readfirstlane_b32 s25, v2
	v_readfirstlane_b32 s52, v3
	v_readfirstlane_b32 s53, v4
	v_readfirstlane_b32 s34, v5
	v_readfirstlane_b32 s35, v6
	v_readfirstlane_b32 s36, v7
	v_readfirstlane_b32 s39, v14
	v_mov_b32_e32 v1, s16
	v_lshlrev_b32_e32 v1, 5, v1
	v_add_nc_u32_e32 v1, 40, v1
	v_mov_b32_e32 v2, s9
	v_add_co_u32 v3, vcc, s8, v1
	v_add_co_ci_u32_e32 v4, vcc, 0, v2, vcc
	GLOBAL_LOAD_DWORDX2 v[30:31], v[3:4], off
	GLOBAL_LOAD_DWORDX2 v[32:33], v[3:4], off offset:8
	GLOBAL_LOAD_DWORD v34, v[3:4], off offset:24
	s_waitcnt vmcnt(0)
	GLOBAL_LOAD_DWORDX4 v[14:17], v[30:31], off offset:40
	s_waitcnt vmcnt(0)
	v_alignbit_b32 v14, v15, v14, 16
	v_alignbit_b32 v15, v16, v15, 16
	v_readfirstlane_b32 s37, v14
	v_readfirstlane_b32 s38, v15
	v_readfirstlane_b32 s50, v34
	s_sub_u32 s50, s50, 0x42
	s_add_u32 s51, 15, s50
	s_lshr_b32 s51, s51, 4
	s_add_u32 s54, 2, s51
	s_mov_b32 s32, 0xff
	v_lshlrev_b32_e32 v9, 2, v0
	v_mov_b32_e32 v12, s34
	v_mov_b32_e32 v13, s35
	v_add_co_u32 v12, vcc, v12, v9
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	GLOBAL_LOAD_DWORD v14, v[12:13], off
	GLOBAL_LOAD_DWORD v15, v[12:13], off offset:1024
	v_add_co_u32 v12, vcc, 0x800, v12
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	GLOBAL_LOAD_DWORD v16, v[12:13], off
	GLOBAL_LOAD_DWORD v17, v[12:13], off offset:1024
	s_waitcnt vmcnt(0)
	DS_WRITE_B32 v9, v14
	v_add_nc_u32_e32 v10, 0x400, v9
	DS_WRITE_B32 v10, v15
	v_add_nc_u32_e32 v10, 0x800, v9
	DS_WRITE_B32 v10, v16
	v_add_nc_u32_e32 v10, 0xc00, v9
	DS_WRITE_B32 v10, v17
	s_waitcnt lgkmcnt(0)
	s_barrier
	v_cmp_gt_u32_e32 vcc, s51, v0
	s_and_saveexec_b64 s[48:49], vcc
	s_cbranch_execz .Lb1001
	v_lshlrev_b32_e32 v11, 4, v0
	v_add_co_u32 v12, vcc, 50, v30
	v_add_co_ci_u32_e32 v13, vcc, 0, v31, vcc
	v_add_co_u32 v12, vcc, v12, v11
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	GLOBAL_LOAD_DWORDX4 v[23:26], v[12:13], off offset:-2
	GLOBAL_LOAD_DWORD v27, v[12:13], off offset:14
	v_mov_b32_e32 v1, s36
	v_mov_b32_e32 v2, s37
	v_mov_b32_e32 v3, s38
	v_add_nc_u32_e32 v4, 2, v0
	v_perm_b32 v4, v4, v4, s44
	s_mov_b32 s34, s24
	s_mov_b32 s35, s25
	S_LOAD_DWORDX4 s[28:31], s[24:25], null
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
	S_LOAD_DWORDX4 s[28:31], s[24:25], null
.Lb1000:
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	S_LOAD_DWORDX4 s[56:59], s[24:25], null
	v_and_b32_e32 v9, s32, v1
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s32, v2
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s32, v3
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s32, v4
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	S_LOAD_DWORDX4 s[28:31], s[24:25], null
	v_and_b32_e32 v9, s32, v5
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v14, v15
	v_xor_b32_e32 v1, v1, v16
	v_xor_b32_e32 v1, v1, v17
	v_xor_b32_e32 v1, s56, v1
	v_and_b32_e32 v9, s32, v6
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v2, v14, v15
	v_xor_b32_e32 v2, v2, v16
	v_xor_b32_e32 v2, v2, v17
	v_xor_b32_e32 v2, s57, v2
	v_and_b32_e32 v9, s32, v7
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v3, v14, v15
	v_xor_b32_e32 v3, v3, v16
	v_xor_b32_e32 v3, v3, v17
	v_xor_b32_e32 v3, s58, v3
	v_and_b32_e32 v9, s32, v8
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v4, v14, v15
	v_xor_b32_e32 v4, v4, v16
	v_xor_b32_e32 v4, v4, v17
	v_xor_b32_e32 v4, s59, v4
	s_add_u32 s33, 1, s33
	s_cmp_lt_u32 s33, s27
	s_cbranch_scc1 .Lb1000
	s_waitcnt lgkmcnt(0)
	s_add_u32 s24, 16, s24
	s_addc_u32 s25, 0, s25
	S_LOAD_DWORDX4 s[56:59], s[24:25], null
	v_and_b32_e32 v9, s32, v1
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v4
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v5, v14, v15
	v_xor_b32_e32 v5, v5, v16
	v_xor_b32_e32 v5, v5, v17
	v_xor_b32_e32 v5, s28, v5
	v_and_b32_e32 v9, s32, v2
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v3
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v1
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v6, v14, v15
	v_xor_b32_e32 v6, v6, v16
	v_xor_b32_e32 v6, v6, v17
	v_xor_b32_e32 v6, s29, v6
	v_and_b32_e32 v9, s32, v3
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v4
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v2
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v7, v14, v15
	v_xor_b32_e32 v7, v7, v16
	v_xor_b32_e32 v7, v7, v17
	v_xor_b32_e32 v7, s30, v7
	v_and_b32_e32 v9, s32, v4
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v1
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x400, v10
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v2
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0x800, v10
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v3
	v_lshlrev_b32_e32 v10, 2, v9
	v_add_nc_u32_e32 v10, 0xc00, v10
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v8, v14, v15
	v_xor_b32_e32 v8, v8, v16
	v_xor_b32_e32 v8, v8, v17
	v_xor_b32_e32 v8, s31, v8
	s_waitcnt lgkmcnt(0)
	v_and_b32_e32 v9, s32, v5
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v8
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
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
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v7
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v5
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
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
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v8
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v6
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
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
	DS_READ_B32 v14, v10
	v_lshrrev_b32_e32 v9, 8, v5
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v15, v10
	v_lshrrev_b32_e32 v9, 16, v6
	v_and_b32_e32 v9, s32, v9
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v16, v10
	v_lshrrev_b32_e32 v9, 24, v7
	v_lshlrev_b32_e32 v10, 2, v9
	DS_READ_B32 v17, v10
	s_waitcnt lgkmcnt(0)
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
	s_mov_b32 s24, s34
	s_mov_b32 s25, s35
	s_waitcnt vmcnt(0)
	v_alignbit_b32 v14, v24, v23, 16
	v_alignbit_b32 v15, v25, v24, 16
	v_alignbit_b32 v16, v26, v25, 16
	v_alignbit_b32 v17, v27, v26, 16
	v_xor_b32_e32 v1, v1, v14
	v_xor_b32_e32 v2, v2, v15
	v_xor_b32_e32 v3, v3, v16
	v_xor_b32_e32 v4, v4, v17
	v_add_co_u32 v12, vcc, v32, v11
	v_add_co_ci_u32_e32 v13, vcc, 0, v33, vcc
	GLOBAL_STORE_DWORDX4 v[12:13], v[1:4], off glc slc
.Lb1001:
	s_mov_b64 exec, s[48:49]
	s_waitcnt vmcnt(0)
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
