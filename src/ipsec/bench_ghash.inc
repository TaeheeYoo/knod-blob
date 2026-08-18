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
	v_sub_nc_u32_e32 v9, s54, v0
	v_add_nc_u32_e32 v9, -1, v9
	v_max_i32_e32 v9, 0, v9
	v_lshlrev_b32_e32 v9, 4, v9
	v_mov_b32_e32 v12, s52
	v_mov_b32_e32 v13, s53
	v_add_co_u32 v12, vcc, v12, v9
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	GLOBAL_LOAD_DWORDX4 v[5:8], v[12:13], off
	s_sub_u32 s33, s51, 1
	v_min_i32_e32 v18, s33, v0
	v_lshlrev_b32_e32 v11, 4, v18
	v_add_co_u32 v12, vcc, 50, v30
	v_add_co_ci_u32_e32 v13, vcc, 0, v31, vcc
	v_add_co_u32 v12, vcc, v12, v11
	v_add_co_ci_u32_e32 v13, vcc, 0, v13, vcc
	GLOBAL_LOAD_DWORDX4 v[23:26], v[12:13], off offset:-2
	GLOBAL_LOAD_DWORD v27, v[12:13], off offset:14
	s_waitcnt vmcnt(0)
	v_alignbit_b32 v14, v24, v23, 16
	v_alignbit_b32 v15, v25, v24, 16
	v_alignbit_b32 v16, v26, v25, 16
	v_alignbit_b32 v17, v27, v26, 16
	v_perm_b32 v14, v14, v14, s44
	v_perm_b32 v15, v15, v15, s44
	v_perm_b32 v16, v16, v16, s44
	v_perm_b32 v17, v17, v17, s44
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
	s_cbranch_execz .Lb2001
	v_mov_b32_e32 v1, 0
	v_mov_b32_e32 v2, 0
	v_mov_b32_e32 v3, 0
	v_mov_b32_e32 v4, 0
	v_mov_b32_e32 v10, 0xe1000000
	s_mov_b32 s33, 0
.Lb2000:
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
	s_cbranch_scc1 .Lb2000
.Lb2001:
	s_mov_b64 exec, s[40:41]
	v_lshlrev_b32_e32 v10, 4, v0
	DS_WRITE_B128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
	s_barrier
	v_and_b32_e32 v9, 1, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .Lb2002
	v_add_nc_u32_e32 v9, 1, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	DS_READ_B128 v[5:8], v9
	DS_READ_B128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	DS_WRITE_B128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.Lb2002:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 2, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .Lb2003
	v_add_nc_u32_e32 v9, 2, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	DS_READ_B128 v[5:8], v9
	DS_READ_B128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	DS_WRITE_B128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.Lb2003:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 4, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .Lb2004
	v_add_nc_u32_e32 v9, 4, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	DS_READ_B128 v[5:8], v9
	DS_READ_B128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	DS_WRITE_B128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.Lb2004:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 8, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .Lb2005
	v_add_nc_u32_e32 v9, 8, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	DS_READ_B128 v[5:8], v9
	DS_READ_B128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	DS_WRITE_B128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.Lb2005:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 16, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .Lb2006
	v_add_nc_u32_e32 v9, 16, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	DS_READ_B128 v[5:8], v9
	DS_READ_B128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	DS_WRITE_B128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.Lb2006:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 32, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .Lb2007
	v_add_nc_u32_e32 v9, 32, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	DS_READ_B128 v[5:8], v9
	DS_READ_B128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	DS_WRITE_B128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.Lb2007:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 64, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .Lb2008
	v_add_nc_u32_e32 v9, 64, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	DS_READ_B128 v[5:8], v9
	DS_READ_B128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	DS_WRITE_B128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.Lb2008:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_and_b32_e32 v9, 0x80, v0
	v_cmp_eq_u32_e32 vcc, 0, v9
	s_and_saveexec_b64 s[40:41], vcc
	s_cbranch_execz .Lb2009
	v_add_nc_u32_e32 v9, 0x80, v0
	v_lshlrev_b32_e32 v9, 4, v9
	v_lshlrev_b32_e32 v10, 4, v0
	DS_READ_B128 v[5:8], v9
	DS_READ_B128 v[1:4], v10
	s_waitcnt lgkmcnt(0)
	v_xor_b32_e32 v1, v1, v5
	v_xor_b32_e32 v2, v2, v6
	v_xor_b32_e32 v3, v3, v7
	v_xor_b32_e32 v4, v4, v8
	DS_WRITE_B128 v10, v[1:4]
	s_waitcnt lgkmcnt(0)
.Lb2009:
	s_mov_b64 exec, s[40:41]
	s_barrier
	v_cmp_eq_u32_e32 vcc, 0, v0
	s_and_saveexec_b64 s[48:49], vcc
	s_cbranch_execz .Lb2010
	v_mov_b32_e32 v9, 0
	DS_READ_B128 v[1:4], v9
	s_waitcnt lgkmcnt(0)
	GLOBAL_STORE_DWORDX4 v[32:33], v[1:4], off glc slc
.Lb2010:
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
