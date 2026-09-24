// SPDX-License-Identifier: GPL-2.0-or-later
//
// GDA stage 2, M1: the receive kernel.  One wave per queue runs the queue's
// receive rings itself - polls the NIC's CQ and keeps its RQ posted - and
// drops everything, with no program and no host in the loop.
//
// The rings live in accel memory the NIC reads and writes directly (the
// KNOD_PERSIST_RING_* layout), and the NIC has set them up: doorbell records
// zeroed, every CQE invalid and owned by hardware.  This posts every RQ entry
// once, entry i to RX page i, and then only moves two counters: a dropped
// packet leaves its page where it was, so the entry it came in on can go
// straight back up as it stands.

#include "common.inc"
#include <linux/knod_persistent.h>

// This queue's configuration, from its control block entry.
#define GR_RING		16	// s[16:17] the ring buffer
#define GR_RXDMA	18	// s[18:19] page index -> the NIC's address
#define GR_RQ_LOG	20
#define GR_STRIDE	21	// log2 bytes per RQ entry
#define GR_CQ_LOG	22
#define GR_FRAG		23	// byte count an RQ entry offers
#define GR_HEADROOM	24
#define GR_MKEY		25	// already big endian
#define GR_LIVE		26

// State.
#define GS_CI		32	// CQ consumer index, free-running
#define GS_PC		33	// RQ producer count, free-running
#define GS_PKTS		34	// s[34:35]
#define GS_ERRS		36	// s[36:37]
#define GS_CQ_MASK	38
#define GS_RQ_SIZE	39
#define GS_I		40	// post cursor, then pass counter
#define GS_ENTRY	41	// this queue's entry in the control block
#define GS_T		42	// s[42:53] scratch

// An unused scatter entry: nothing to offer, and a key that says so.
#define MLX5_INVALID_LKEY_BE	0x00010000

	.text
	.globl knod_gda_rx_kernel
	.p2align 2
knod_gda_rx_kernel:
#if !defined(KNOD_NO_ICACHE_INV)
	s_icache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
#endif
	// One wave per queue does all of it; the rest of the group ends.
	v_readfirstlane_b32 s0, v[TID]
	s_and_b32 s0, s0, 1023
	s_cmp_eq_u32 s0, 0
	s_cbranch_scc0 .Lgda_rx_end

	s_mov_b64 exec, -1
	v_mbcnt_lo_u32_b32 v1, -1, 0
	v_mbcnt_hi_u32_b32 v1, -1, v1

	// This queue's entry, from lane 0.
	s_mul_i32 s[GS_ENTRY], s[WG_Y], KNOD_PERSIST_GDA_BYTES
	s_add_u32 s[GS_ENTRY], s[GS_ENTRY], KNOD_PERSIST_GDA
	s_mov_b64 exec, 1
	v_mov_b32 v2, s8
	v_mov_b32 v3, s9
	v_add_co_u32 v2, vcc, s[GS_ENTRY], v2
	V_ADD_CI_U32 v3, vcc, 0, v3, vcc
	global_load_dwordx4 v[4:7], v[2:3], off glc dlc
	global_load_dwordx4 v[8:11], v[2:3], off \
			    offset:KNOD_PERSIST_GDA_RQ_LOG glc dlc
	global_load_dwordx4 v[12:15], v[2:3], off \
			    offset:KNOD_PERSIST_GDA_HEADROOM glc dlc
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s[GR_RING], v4
	v_readfirstlane_b32 s[GR_RING+1], v5
	v_readfirstlane_b32 s[GR_RXDMA], v6
	v_readfirstlane_b32 s[GR_RXDMA+1], v7
	v_readfirstlane_b32 s[GR_RQ_LOG], v8
	v_readfirstlane_b32 s[GR_STRIDE], v9
	v_readfirstlane_b32 s[GR_CQ_LOG], v10
	v_readfirstlane_b32 s[GR_FRAG], v11
	v_readfirstlane_b32 s[GR_HEADROOM], v12
	v_readfirstlane_b32 s[GR_MKEY], v13
	v_readfirstlane_b32 s[GR_LIVE], v14
	// Not a queue whose rings the NIC gave us.
	s_cmp_eq_u32 s[GR_LIVE], 0
	s_cbranch_scc1 .Lgda_rx_end

	s_lshl_b32 s[GS_RQ_SIZE], 1, s[GR_RQ_LOG]
	s_lshl_b32 s[GS_CQ_MASK], 1, s[GR_CQ_LOG]
	s_sub_u32 s[GS_CQ_MASK], s[GS_CQ_MASK], 1

	// Post every RQ entry, 64 at a time: entry i to RX page i, past the
	// headroom.  Nothing here ever takes a page from its entry, so this is
	// the only time an entry is written.
	s_mov_b64 exec, -1
	s_mov_b32 s[GS_I], 0
.Lgda_rx_post:
	V_ADD_U32 v2, s[GS_I], v1			// entry
	v_lshlrev_b32 v3, 3, v2
	v_add_co_u32 v4, vcc, s[GR_RXDMA], v3
	v_mov_b32 v5, s[GR_RXDMA+1]
	V_ADD_CI_U32 v5, vcc, 0, v5, vcc
	global_load_dwordx2 v[6:7], v[4:5], off
	// ring + RQ + (entry << stride)
	v_lshlrev_b32 v3, s[GR_STRIDE], v2
	v_add_co_u32 v3, vcc, KNOD_PERSIST_RING_RQ_OFF, v3
	v_add_co_u32 v4, vcc, s[GR_RING], v3
	v_mov_b32 v5, s[GR_RING+1]
	V_ADD_CI_U32 v5, vcc, 0, v5, vcc
	s_waitcnt vmcnt(0)
	v_add_co_u32 v6, vcc, s[GR_HEADROOM], v6
	V_ADD_CI_U32 v7, vcc, 0, v7, vcc
	// The scatter entry: byte count, key, address, big endian but the key.
	v_mov_b32 v8, s[GR_FRAG]
	v_perm_b32 v8, v8, v8, 0x00010203
	v_mov_b32 v9, s[GR_MKEY]
	v_perm_b32 v10, v7, v7, 0x00010203
	v_perm_b32 v11, v6, v6, 0x00010203
	global_store_dwordx4 v[4:5], v[8:11], off glc slc
	// An entry wider than one scatter entry: the rest offer nothing.
	s_cmp_le_u32 s[GR_STRIDE], 4
	s_cbranch_scc1 .Lgda_rx_posted
	s_lshl_b32 s[GS_T], 1, s[GR_STRIDE]
	s_mov_b32 s[GS_T+1], 16
	v_mov_b32 v12, 0
	v_mov_b32 v13, MLX5_INVALID_LKEY_BE
	v_mov_b32 v14, 0
	v_mov_b32 v15, 0
.Lgda_rx_pad:
	v_add_co_u32 v2, vcc, s[GS_T+1], v4
	V_ADD_CI_U32 v3, vcc, 0, v5, vcc
	global_store_dwordx4 v[2:3], v[12:15], off glc slc
	s_add_u32 s[GS_T+1], s[GS_T+1], 16
	s_cmp_lt_u32 s[GS_T+1], s[GS_T]
	s_cbranch_scc1 .Lgda_rx_pad
.Lgda_rx_posted:
	s_add_u32 s[GS_I], s[GS_I], 64
	s_cmp_lt_u32 s[GS_I], s[GS_RQ_SIZE]
	s_cbranch_scc1 .Lgda_rx_post

	// Every entry is in memory before the NIC is told it may use them.
	s_waitcnt_vscnt null, 0
	s_mov_b32 s[GS_CI], 0
	s_mov_b32 s[GS_PC], s[GS_RQ_SIZE]
	s_mov_b64 s[GS_PKTS:GS_PKTS+1], 0
	s_mov_b64 s[GS_ERRS:GS_ERRS+1], 0
	s_mov_b32 s[GS_I], 0
	s_mov_b64 exec, 1
	v_mov_b32 v2, s[GS_PC]
	v_and_b32 v2, 0xffff, v2
	v_perm_b32 v2, v2, v2, 0x00010203
	v_mov_b32 v4, s[GR_RING]
	v_mov_b32 v5, s[GR_RING+1]
	global_store_dword v[4:5], v2, off offset:KNOD_PERSIST_RING_RQ_DB glc slc
	s_waitcnt_vscnt null, 0

.Lgda_rx_poll:
	// Lane i looks at CQE ci + i - its last byte, the opcode and the owner
	// bit, is all a drop needs.
	s_mov_b64 exec, -1
	V_ADD_U32 v2, s[GS_CI], v1
	v_and_b32 v3, s[GS_CQ_MASK], v2
	v_lshlrev_b32 v3, 6, v3
	v_add_co_u32 v3, vcc, (KNOD_PERSIST_RING_CQ_OFF + 63), v3
	v_add_co_u32 v4, vcc, s[GR_RING], v3
	v_mov_b32 v5, s[GR_RING+1]
	V_ADD_CI_U32 v5, vcc, 0, v5, vcc
	global_load_ubyte v6, v[4:5], off glc dlc
	s_waitcnt vmcnt(0)
	// Ours when the owner bit matches the pass round the ring the position
	// is on and the opcode is not the invalid one a fresh ring holds.
	v_lshrrev_b32 v7, s[GR_CQ_LOG], v2
	v_and_b32 v7, 1, v7
	v_and_b32 v8, 1, v6
	v_cmp_eq_u32_e64 s[GS_T:GS_T+1], v7, v8
	v_lshrrev_b32 v9, 4, v6
	v_cmp_ne_u32_e64 s[GS_T+2:GS_T+3], 15, v9
	s_and_b64 s[GS_T:GS_T+1], s[GS_T:GS_T+1], s[GS_T+2:GS_T+3]
	// Only an unbroken run from the head is ours to take.
	s_not_b64 s[GS_T+2:GS_T+3], s[GS_T:GS_T+1]
	s_ff1_i32_b64 s[GS_T+4], s[GS_T+2:GS_T+3]
	s_cmp_lt_i32 s[GS_T+4], 0
	s_cselect_b32 s[GS_T+4], 64, s[GS_T+4]
	s_cmp_eq_u32 s[GS_T+4], 0
	s_cbranch_scc1 .Lgda_rx_idle

	// Error completions among those: opcodes 0xd and 0xe.
	v_cmp_le_u32_e64 s[GS_T+6:GS_T+7], 13, v9
	s_bfm_b64 s[GS_T+8:GS_T+9], s[GS_T+4], 0
	s_cmp_eq_u32 s[GS_T+4], 64
	s_cselect_b64 s[GS_T+8:GS_T+9], -1, s[GS_T+8:GS_T+9]
	s_and_b64 s[GS_T+6:GS_T+7], s[GS_T+6:GS_T+7], s[GS_T+8:GS_T+9]
	s_and_b64 s[GS_T+6:GS_T+7], s[GS_T+6:GS_T+7], s[GS_T:GS_T+1]
	s_bcnt1_i32_b64 s[GS_T+10], s[GS_T+6:GS_T+7]

	s_add_u32 s[GS_CI], s[GS_CI], s[GS_T+4]
	s_add_u32 s[GS_PC], s[GS_PC], s[GS_T+4]
	s_add_u32 s[GS_PKTS], s[GS_PKTS], s[GS_T+4]
	s_addc_u32 s[GS_PKTS+1], s[GS_PKTS+1], 0
	s_add_u32 s[GS_ERRS], s[GS_ERRS], s[GS_T+10]
	s_addc_u32 s[GS_ERRS+1], s[GS_ERRS+1], 0

	// Hand the CQ entries back, then the same RQ entries: the records are
	// all the NIC needs to see.
	s_mov_b64 exec, 1
	v_mov_b32 v2, s[GS_CI]
	v_and_b32 v2, 0xffffff, v2
	v_perm_b32 v2, v2, v2, 0x00010203
	v_mov_b32 v3, s[GS_PC]
	v_and_b32 v3, 0xffff, v3
	v_perm_b32 v3, v3, v3, 0x00010203
	v_mov_b32 v4, s[GR_RING]
	v_mov_b32 v5, s[GR_RING+1]
	global_store_dword v[4:5], v2, off offset:KNOD_PERSIST_RING_CQ_DB glc slc
	global_store_dword v[4:5], v3, off offset:KNOD_PERSIST_RING_RQ_DB glc slc

	// Now and then, the counts and whether to stop.
	s_add_u32 s[GS_I], s[GS_I], 1
	s_and_b32 s[GS_T], s[GS_I], 255
	s_cmp_eq_u32 s[GS_T], 0
	s_cbranch_scc1 .Lgda_rx_report
	s_branch .Lgda_rx_poll

.Lgda_rx_idle:
	s_sleep 1
.Lgda_rx_report:
	s_mov_b64 exec, 1
	v_mov_b32 v2, s8
	v_mov_b32 v3, s9
	v_add_co_u32 v4, vcc, s[GS_ENTRY], v2
	V_ADD_CI_U32 v5, vcc, 0, v3, vcc
	v_mov_b32 v6, s[GS_PKTS]
	v_mov_b32 v7, s[GS_PKTS+1]
	v_mov_b32 v8, s[GS_ERRS]
	v_mov_b32 v9, s[GS_ERRS+1]
	global_store_dwordx4 v[4:5], v[6:9], off \
			     offset:KNOD_PERSIST_GDA_PACKETS glc slc
	global_load_dwordx2 v[10:11], v[2:3], off glc dlc
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s[GS_T], v10
	s_cmp_eq_u32 s[GS_T], KNOD_PERSIST_VERSION
	s_cbranch_scc0 .Lgda_rx_stop
	v_readfirstlane_b32 s[GS_T], v11
	s_cmp_eq_u32 s[GS_T], 0
	s_cbranch_scc1 .Lgda_rx_poll
.Lgda_rx_stop:
	s_waitcnt_vscnt null, 0
.Lgda_rx_end:
	s_endpgm
	.size knod_gda_rx_kernel, . - knod_gda_rx_kernel
