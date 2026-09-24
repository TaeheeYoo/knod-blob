// SPDX-License-Identifier: GPL-2.0-or-later
//
// GDA stage 2: what wraps a program when the shader runs the NIC's rings
// itself.  The prologue sets up exactly the registers KNOD_PROLOGUE_BODY sets
// up - the program cannot tell the difference - but its packets come from the
// NIC's CQ rather than a batch the host built.  The epilogue sends what the
// program sent on the queue's XDP SQ, and gives the rest back to the RQ.
//
// One wave per queue, KNOD_GDA_LANES packets a round.  The rest of the
// workgroup ends at once.
//
// RQ entry i always holds RX page i.  The NIC consumes the RQ in order and
// completes the SQ in order, so no page ever needs to move between entries:
// an entry whose packet went out on the SQ is held until the NIC says it was
// sent, and the RQ producer stops at the oldest such entry.  Every round's
// first WQE records the RQ position it came from, which is all that takes.
//
// Across a program only s0, s2, s[6:7], s[8:9], s[10:11] and the named
// registers survive; here that is s2 = the CQ consumer index and s[10:11] =
// the ring buffer, and v73-v75 (KNOD_BLOB_PRO_GDA_VREG), which no program
// reaches: v[74:75] lane 0 the packet count, and v73 one value per lane.

#ifndef GDA_INC
#define GDA_INC
#include <linux/knod_persistent.h>
#include "epilogue.inc"

#define GS		KNOD_BLOB_PRO_GDA_VREG
#define GDA_PKTS	(KNOD_BLOB_PRO_GDA_VREG + 1)	// v[74:75]
#define KNOD_GDA_LANES	64
#define GT		(KNOD_BLOB_PRO_TMP_SREG + 10)	// s[24:25]

// v[GS], by lane.
#define GL_RQ_SIZE	0
#define GL_SQ_PC	1	// WQEs posted, free-running
#define GL_SQ_CC	2	// WQEs the NIC completed
#define GL_TX_CI	3	// TX CQ consumer index
#define GL_SQ_MASK	4	// zero: no SQ, XDP_TX drops
#define GL_SQN		5
#define GL_TX_MKEY	6
#define GL_TX_CQ_LOG	7
#define GL_SQ		8	// and 9
#define GL_DB		10	// and 11: the NIC's doorbell
#define GL_RX_DMA	12	// and 13: page index -> the NIC's address
#define GL_TX_PKTS	14	// and 15
#define GL_TX_FULL	16	// and 17
#define GL_TX_GEN	18

// The epilogue's scalars.
#define E_N		14	// packets this round
#define E_CI		15	// consumer index the round started at
#define E_PC		16	// SQ producer
#define E_CC		17	// SQ consumer
#define E_TXCI		18
#define E_MASK		19
#define E_K		20	// WQEs this round
#define E_T		21
#define E_TX		22	// s[22:23] lanes sending

// v[P0:P1] = this queue's control entry.  Scalar temp: PS0.
	.macro GDA_ENTRY_ADDR
	s_mul_i32 s[PS0], s[WG_Y], KNOD_PERSIST_GDA_BYTES
	s_add_u32 s[PS0], s[PS0], KNOD_PERSIST_GDA
	v_mov_b32 v[P0], s8
	v_mov_b32 v[P1], s9
	v_add_co_u32 v[P0], vcc, s[PS0], v[P0]
	V_ADD_CI_U32 v[P1], vcc, 0, v[P1], vcc
	.endm

// What outlives the shader, and what the host reads, to the queue's entry.
// Lane 0, v[P0:P1] = the entry.  Clobbers s[24:25] and v[P24:P31].
	.macro GDA_SAVE
	v_mov_b32 v[P24], s2
	global_store_dword v[P0:P1], v[P24], off \
			   offset:KNOD_PERSIST_GDA_CI glc slc
	global_store_dwordx2 v[P0:P1], v[GDA_PKTS:GDA_PKTS+1], off \
			     offset:KNOD_PERSIST_GDA_PACKETS glc slc
	v_readlane_b32 s[GT], v[GS], GL_TX_PKTS
	v_readlane_b32 s[GT+1], v[GS], GL_TX_PKTS + 1
	v_mov_b32 v[P24], s[GT]
	v_mov_b32 v[P24+1], s[GT+1]
	v_readlane_b32 s[GT], v[GS], GL_TX_FULL
	v_readlane_b32 s[GT+1], v[GS], GL_TX_FULL + 1
	v_mov_b32 v[P24+2], s[GT]
	v_mov_b32 v[P24+3], s[GT+1]
	global_store_dwordx4 v[P0:P1], v[P24:P24+3], off \
			     offset:KNOD_PERSIST_GDA_TX_PACKETS glc slc
	v_readlane_b32 s[GT], v[GS], GL_SQ_PC
	v_readlane_b32 s[GT+1], v[GS], GL_SQ_CC
	v_mov_b32 v[P28], s[GT]
	v_mov_b32 v[P28+1], s[GT+1]
	v_readlane_b32 s[GT], v[GS], GL_TX_CI
	v_readlane_b32 s[GT+1], v[GS], GL_TX_GEN
	v_mov_b32 v[P28+2], s[GT]
	v_mov_b32 v[P28+3], s[GT+1]
	global_store_dwordx4 v[P0:P1], v[P28:P28+3], off \
			     offset:KNOD_PERSIST_GDA_SQ_PC glc slc
	.endm

// Take what the NIC has sent off the TX CQ: E_CC and E_TXCI move.  Expects
// E_PC, E_CC, E_TXCI and a nonzero E_MASK.  One CQE per round that sent, on
// its last WQE, so the run of new ones ends on a round boundary.
// Clobbers EXEC, E_T, s[22:25], v[P2:P8].
	.macro GDA_TX_REAP
	s_mov_b64 exec, -1
	v_mbcnt_lo_u32_b32 v[P2], -1, 0
	v_mbcnt_hi_u32_b32 v[P2], -1, v[P2]
	V_ADD_U32 v[P3], s[E_TXCI], v[P2]
	v_readlane_b32 s[E_T], v[GS], GL_TX_CQ_LOG
	s_lshl_b32 s[GT], 1, s[E_T]
	s_sub_u32 s[GT], s[GT], 1
	v_and_b32 v[P4], s[GT], v[P3]
	v_lshlrev_b32 v[P4], 6, v[P4]
	v_add_co_u32 v[P4], vcc, (KNOD_PERSIST_RING_TX_CQ_OFF + 60), v[P4]
	v_add_co_u32 v[P6], vcc, s10, v[P4]
	v_mov_b32 v[P6+1], s11
	V_ADD_CI_U32 v[P6+1], vcc, 0, v[P6+1], vcc
	global_load_dword v[P5], v[P6:P6+1], off glc dlc
	s_waitcnt vmcnt(0)
	v_lshrrev_b32 v[P4], s[E_T], v[P3]
	v_and_b32 v[P4], 1, v[P4]
	v_lshrrev_b32 v[P8], 24, v[P5]
	v_and_b32 v[P3], 1, v[P8]
	v_cmp_eq_u32_e64 s[22:23], v[P4], v[P3]
	v_lshrrev_b32 v[P8], 4, v[P8]
	v_cmp_ne_u32_e64 s[GT:GT+1], 15, v[P8]
	s_and_b64 s[22:23], s[22:23], s[GT:GT+1]
	s_not_b64 s[GT:GT+1], s[22:23]
	s_ff1_i32_b64 s[E_T], s[GT:GT+1]
	s_cmp_lt_i32 s[E_T], 0
	s_cselect_b32 s[E_T], 64, s[E_T]
	s_cmp_eq_u32 s[E_T], 0
	s_cbranch_scc1 .Lgda_reaped\@
	s_add_u32 s[E_TXCI], s[E_TXCI], s[E_T]
	// The last one's WQE counter, big endian 16 bits at byte 60.
	s_sub_u32 s[E_T], s[E_T], 1
	v_readlane_b32 s[GT+1], v[P5], s[E_T]
	s_and_b32 s[GT], s[GT+1], 0xff
	s_lshl_b32 s[GT], s[GT], 8
	s_bfe_u32 s[GT+1], s[GT+1], 0x80008
	s_or_b32 s[GT], s[GT], s[GT+1]
	// Everything up to and including it is sent: widen to the free-running
	// producer, which is never more than an SQ ahead.
	s_add_u32 s[GT], s[GT], 1
	s_sub_u32 s[GT+1], s[E_PC], s[GT]
	s_and_b32 s[GT+1], s[GT+1], 0xffff
	s_sub_u32 s[E_CC], s[E_PC], s[GT+1]
	s_mov_b64 exec, 1
	s_and_b32 s[GT], s[E_TXCI], 0xffffff
	v_mov_b32 v[P2], s[GT]
	v_perm_b32 v[P2], v[P2], v[P2], 0x00010203
	v_mov_b32 v[P6], s10
	v_mov_b32 v[P6+1], s11
	global_store_dword v[P6:P6+1], v[P2], off \
			   offset:KNOD_PERSIST_RING_TX_CQ_DB glc slc
.Lgda_reaped\@:
	.endm

// Hand the CQ entries back up to s2, and the RQ entries up to the oldest one
// whose packet is still waiting to be sent.  Expects E_PC, E_CC, E_MASK.
// Leaves EXEC at lane 0.  Clobbers E_T, s[24:25], v[P2:P4].
	.macro GDA_RQ_RELEASE
	s_mov_b64 exec, 1
	s_mov_b32 s[E_T], s2
	s_cmp_eq_u32 s[E_PC], s[E_CC]
	s_cbranch_scc1 .Lgda_bound\@
	s_and_b32 s[GT], s[E_CC], s[E_MASK]
	s_lshl_b32 s[GT], s[GT], 2
	s_add_u32 s[GT], s[GT], KNOD_PERSIST_RING_RQPOS_OFF
	s_add_u32 s[GT], s10, s[GT]
	s_addc_u32 s[GT+1], s11, 0
	v_mov_b32 v[P2], s[GT]
	v_mov_b32 v[P3], s[GT+1]
	global_load_dword v[P4], v[P2:P3], off glc dlc
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s[E_T], v[P4]
.Lgda_bound\@:
	v_readlane_b32 s[GT], v[GS], GL_RQ_SIZE
	s_add_u32 s[E_T], s[E_T], s[GT]
	s_and_b32 s[E_T], s[E_T], 0xffff
	v_mov_b32 v[P3], s[E_T]
	v_perm_b32 v[P3], v[P3], v[P3], 0x00010203
	s_and_b32 s[GT], s2, 0xffffff
	v_mov_b32 v[P2], s[GT]
	v_perm_b32 v[P2], v[P2], v[P2], 0x00010203
	v_mov_b32 v[P4], s10
	v_mov_b32 v[P4+1], s11
	global_store_dword v[P4:P4+1], v[P2], off \
			   offset:KNOD_PERSIST_RING_CQ_DB glc slc
	global_store_dword v[P4:P4+1], v[P3], off \
			   offset:KNOD_PERSIST_RING_RQ_DB glc slc
	.endm

// Park while the host changes maps, or leave if it wants the shader gone.
// Expects v[P0:P1] = the entry and v[P4:P6] = the control block's version,
// stop and pause, as read this round.  Lane 0.
	.macro GDA_PARK_OR_STOP
	v_readfirstlane_b32 s[PS0], v[P4]
	s_cmp_eq_u32 s[PS0], KNOD_PERSIST_VERSION
	s_cbranch_scc0 .Lgda_stop\@
	v_readfirstlane_b32 s[PS0], v[P5]
	s_cmp_eq_u32 s[PS0], 0
	s_cbranch_scc0 .Lgda_stop\@
	v_readfirstlane_b32 s[PS1], v[P6]
	s_cmp_eq_u32 s[PS1], 0
	s_cbranch_scc1 .Lgda_parked\@
	// Where the rings got to, then that this queue is parked: the host may
	// read anything once it sees the ack.
	GDA_SAVE
	s_waitcnt_vscnt null, 0
	global_store_dword v[P0:P1], v[P6], off \
			   offset:KNOD_PERSIST_GDA_PAUSE_ACK glc slc
	s_waitcnt_vscnt null, 0
.Lgda_wait\@:
	s_sleep 8
	v_mov_b32 v[P2], s8
	v_mov_b32 v[P3], s9
	global_load_dwordx4 v[P4:P4+3], v[P2:P3], off glc dlc
	s_waitcnt vmcnt(0)
	v_readfirstlane_b32 s[PS0], v[P4]
	s_cmp_eq_u32 s[PS0], KNOD_PERSIST_VERSION
	s_cbranch_scc0 .Lgda_stop\@
	v_readfirstlane_b32 s[PS0], v[P5]
	s_cmp_eq_u32 s[PS0], 0
	s_cbranch_scc0 .Lgda_stop\@
	v_readfirstlane_b32 s[PS1], v[P6]
	s_cmp_eq_u32 s[PS1], 0
	s_cbranch_scc0 .Lgda_wait\@
	s_branch .Lgda_parked\@
.Lgda_stop\@:
	// Where the rings got to, for whichever shader comes next.
	GDA_SAVE
	s_waitcnt_vscnt null, 0
	s_endpgm
.Lgda_parked\@:
	.endm

	.macro KNOD_GDA_PROLOGUE_BODY
#if !defined(KNOD_NO_ICACHE_INV)
	s_icache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
#endif
	// One wave per queue.
	v_readfirstlane_b32 s0, v[TID]
	s_and_b32 s0, s0, 1023
	s_cmp_eq_u32 s0, 0
	s_cbranch_scc0 .Lgda_end\@

	// The queue's entry, and the NIC's TX doorbell, from lane 0.
	s_mov_b64 exec, 1
	GDA_ENTRY_ADDR
	global_load_dwordx4 v[P2:P2+3], v[P0:P1], off glc dlc	// ring, rx_dma
	global_load_dwordx4 v[P6:P6+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_RQ_LOG glc dlc
	global_load_dwordx4 v[P12:P12+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_HEADROOM glc dlc
	global_load_dwordx2 v[P16:P16+1], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_CI glc dlc
	global_load_dwordx2 v[P10:P10+1], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_PACKETS glc dlc
	global_load_dwordx4 v[P20:P20+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_SQ glc dlc	// sq, sqn, mask
	global_load_dwordx4 v[P24:P24+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_TX_MKEY glc dlc	// mkey, cq_log, gen
	global_load_dwordx4 v[P28:P28+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_TX_PACKETS glc dlc
	global_load_dwordx4 v[P32:P32+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_SQ_PC glc dlc
	s_lshl_b32 s[PS0], s[WG_Y], 3
	s_add_u32 s[PS0], s[PS0], KNOD_PERSIST_TX_DB
	v_mov_b32 v[P18], s8
	v_mov_b32 v[P18+1], s9
	v_add_co_u32 v[P18], vcc, s[PS0], v[P18]
	V_ADD_CI_U32 v[P18+1], vcc, 0, v[P18+1], vcc
	global_load_dwordx2 v[P18:P18+1], v[P18:P18+1], off glc dlc
	s_waitcnt vmcnt(0)
	// Not a queue whose rings the NIC gave us.
	v_readfirstlane_b32 s[PS0], v[P12+2]
	s_cmp_eq_u32 s[PS0], 0
	s_cbranch_scc1 .Lgda_end\@

	v_readfirstlane_b32 s10, v[P2]
	v_readfirstlane_b32 s11, v[P2+1]
	s_mov_b64 exec, -1
	v_mov_b32 v[GS], 0
	v_readfirstlane_b32 s[PS0], v[P6]		// rq_log
	s_lshl_b32 s[PS0], 1, s[PS0]
	v_writelane_b32 v[GS], s[PS0], GL_RQ_SIZE
	v_readfirstlane_b32 s[PS0], v[P10]
	v_readfirstlane_b32 s[PS1], v[P10+1]
	v_mov_b32 v[GDA_PKTS], s[PS0]
	v_mov_b32 v[GDA_PKTS+1], s[PS1]
	v_readfirstlane_b32 s[PS0], v[P2+2]
	v_readfirstlane_b32 s[PS1], v[P2+3]
	v_writelane_b32 v[GS], s[PS0], GL_RX_DMA
	v_writelane_b32 v[GS], s[PS1], GL_RX_DMA + 1
	v_readfirstlane_b32 s[PS0], v[P28]
	v_readfirstlane_b32 s[PS1], v[P28+1]
	v_writelane_b32 v[GS], s[PS0], GL_TX_PKTS
	v_writelane_b32 v[GS], s[PS1], GL_TX_PKTS + 1
	v_readfirstlane_b32 s[PS0], v[P28+2]
	v_readfirstlane_b32 s[PS1], v[P28+3]
	v_writelane_b32 v[GS], s[PS0], GL_TX_FULL
	v_writelane_b32 v[GS], s[PS1], GL_TX_FULL + 1

	// The SQ, if the NIC gave us that too and its doorbell is mapped.
	v_readfirstlane_b32 s[PS2], v[P20]
	v_readfirstlane_b32 s[PS2+1], v[P20+1]
	v_readfirstlane_b32 s[PS4], v[P18]
	v_readfirstlane_b32 s[PS4+1], v[P18+1]
	v_readfirstlane_b32 s[PS6], v[P20+3]		// mask
	s_cmp_eq_u64 s[PS2:PS2+1], 0
	s_cselect_b32 s[PS6], 0, s[PS6]
	s_cmp_eq_u64 s[PS4:PS4+1], 0
	s_cselect_b32 s[PS6], 0, s[PS6]
	v_writelane_b32 v[GS], s[PS6], GL_SQ_MASK
	v_writelane_b32 v[GS], s[PS2], GL_SQ
	v_writelane_b32 v[GS], s[PS2+1], GL_SQ + 1
	v_writelane_b32 v[GS], s[PS4], GL_DB
	v_writelane_b32 v[GS], s[PS4+1], GL_DB + 1
	v_readfirstlane_b32 s[PS0], v[P20+2]
	v_writelane_b32 v[GS], s[PS0], GL_SQN
	v_readfirstlane_b32 s[PS0], v[P24]
	v_writelane_b32 v[GS], s[PS0], GL_TX_MKEY
	v_readfirstlane_b32 s[PS0], v[P24+1]
	v_writelane_b32 v[GS], s[PS0], GL_TX_CQ_LOG
	v_readfirstlane_b32 s[PS0], v[P24+2]		// gen
	v_writelane_b32 v[GS], s[PS0], GL_TX_GEN
	// Where the last shader left this build of the SQ, or its start.
	v_readfirstlane_b32 s[PS1], v[P32+3]
	s_cmp_eq_u32 s[PS0], s[PS1]
	s_cbranch_scc0 .Lgda_tx_fresh\@
	s_cmp_eq_u32 s[PS6], 0
	s_cbranch_scc1 .Lgda_tx_fresh\@
	v_readfirstlane_b32 s[PS0], v[P32]
	v_writelane_b32 v[GS], s[PS0], GL_SQ_PC
	v_readfirstlane_b32 s[PS0], v[P32+1]
	v_writelane_b32 v[GS], s[PS0], GL_SQ_CC
	v_readfirstlane_b32 s[PS0], v[P32+2]
	v_writelane_b32 v[GS], s[PS0], GL_TX_CI
.Lgda_tx_fresh\@:

	// Posted for this build of the rings already: pick up where the last
	// shader left the CQ.  Otherwise post every RQ entry, entry i to RX
	// page i - a round only ever gives an entry back as it was.
	v_readfirstlane_b32 s2, v[P16]			// ci
	v_readfirstlane_b32 s[PS0], v[P16+1]		// posted_gen
	v_readfirstlane_b32 s[PS1], v[P12+3]		// gen
	s_cmp_eq_u32 s[PS0], s[PS1]
	s_cbranch_scc1 .Lgda_posted\@

	v_mbcnt_lo_u32_b32 v[P10], -1, 0
	v_mbcnt_hi_u32_b32 v[P10], -1, v[P10]
	v_readfirstlane_b32 s[PS2], v[P6+1]		// log2 entry bytes
	v_readfirstlane_b32 s[PS2+1], v[P6+3]		// byte count
	v_readfirstlane_b32 s[PS4], v[P12]		// headroom
	v_readfirstlane_b32 s[PS4+1], v[P12+1]		// key, big endian
	v_readfirstlane_b32 s[PS6], v[P2+2]		// rx_dma
	v_readfirstlane_b32 s[PS6+1], v[P2+3]
	v_readlane_b32 s[PS6+2], v[GS], GL_RQ_SIZE
	s_mov_b32 s[PS0], 0
.Lgda_post\@:
	V_ADD_U32 v[P8], s[PS0], v[P10]			// entry
	v_lshlrev_b32 v[P9], 3, v[P8]
	v_add_co_u32 v[P20], vcc, s[PS6], v[P9]
	v_mov_b32 v[P20+1], s[PS6+1]
	V_ADD_CI_U32 v[P20+1], vcc, 0, v[P20+1], vcc
	global_load_dwordx2 v[P22:P22+1], v[P20:P20+1], off
	v_lshlrev_b32 v[P9], s[PS2], v[P8]
	v_add_co_u32 v[P9], vcc, KNOD_PERSIST_RING_RQ_OFF, v[P9]
	v_add_co_u32 v[P20], vcc, s10, v[P9]
	v_mov_b32 v[P20+1], s11
	V_ADD_CI_U32 v[P20+1], vcc, 0, v[P20+1], vcc
	s_waitcnt vmcnt(0)
	v_add_co_u32 v[P22], vcc, s[PS4], v[P22]
	V_ADD_CI_U32 v[P22+1], vcc, 0, v[P22+1], vcc
	v_mov_b32 v[P24], s[PS2+1]
	v_perm_b32 v[P24], v[P24], v[P24], 0x00010203
	v_mov_b32 v[P24+1], s[PS4+1]
	v_perm_b32 v[P24+2], v[P22+1], v[P22+1], 0x00010203
	v_perm_b32 v[P24+3], v[P22], v[P22], 0x00010203
	global_store_dwordx4 v[P20:P20+1], v[P24:P24+3], off glc slc
	s_add_u32 s[PS0], s[PS0], 64
	s_cmp_lt_u32 s[PS0], s[PS6+2]
	s_cbranch_scc1 .Lgda_post\@
	s_waitcnt_vscnt null, 0
	// Up for the NIC, and recorded as posted for this build.
	s_mov_b32 s2, 0
	s_mov_b64 exec, 1
	v_perm_b32 v[P8], v[GS], v[GS], 0x00010203	// lane 0: RQ size
	v_mov_b32 v[P20], s10
	v_mov_b32 v[P20+1], s11
	global_store_dword v[P20:P20+1], v[P8], off \
			   offset:KNOD_PERSIST_RING_RQ_DB glc slc
	v_mov_b32 v[P8], 0
	v_mov_b32 v[P9], v[P12+3]
	global_store_dwordx2 v[P0:P1], v[P8:P9], off \
			     offset:KNOD_PERSIST_GDA_CI glc slc
	s_waitcnt_vscnt null, 0
.Lgda_posted\@:
	s_getpc_b64 s[6:7]

	// ---- a round ----
	// This round's view of the control block and the queue's entry, then
	// the CQ.  Lane 0 for the first two; the CQ is lane-parallel.
	s_mov_b64 exec, 1
	GDA_ENTRY_ADDR
	v_mov_b32 v[P2], s8
	v_mov_b32 v[P3], s9
	global_load_dwordx4 v[P4:P4+3], v[P2:P3], off glc dlc	// version, stop, pause
	global_load_dwordx2 v[P8:P9], v[P2:P3], off \
			    offset:KNOD_PERSIST_GDA_PARAM glc dlc
	global_load_dwordx4 v[P12:P12+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_RQ_LOG glc dlc	// rq_log, stride, cq_log, frag
	global_load_dwordx4 v[P16:P16+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_HEADROOM glc dlc	// headroom, ...
	global_load_dwordx4 v[P20:P20+3], v[P0:P1], off \
			    offset:KNOD_PERSIST_GDA_RX_BASE glc dlc	// rx_base, bds
	s_waitcnt vmcnt(0)
	GDA_PARK_OR_STOP
	v_readfirstlane_b32 s[PARAM], v[P8]
	v_readfirstlane_b32 s[PARAM+1], v[P9]
	v_readfirstlane_b32 s[PS2], v[P12]		// rq_log
	v_readfirstlane_b32 s[PS2+1], v[P12+2]		// cq_log
	v_readfirstlane_b32 s[PS4], v[P16]		// headroom
	v_readfirstlane_b32 s[PS4+1], v[P20]		// rx_base
	v_readfirstlane_b32 s[PS6], v[P20+1]
	v_readfirstlane_b32 s[PS6+1], v[P20+2]		// bds
	v_readfirstlane_b32 s[PS6+2], v[P20+3]

	// Lane i reads CQE ci + i: its byte count, and its last dword - the
	// RQ entry it completes, its opcode and its owner bit.
	s_mov_b64 exec, -1
	v_mbcnt_lo_u32_b32 v[P10], -1, 0
	v_mbcnt_hi_u32_b32 v[P10], -1, v[P10]
	V_ADD_U32 v[P10+1], s2, v[P10]			// position
	s_lshl_b32 s[PS0], 1, s[PS2+1]
	s_sub_u32 s[PS0], s[PS0], 1
	v_and_b32 v[P12], s[PS0], v[P10+1]
	v_lshlrev_b32 v[P12], 6, v[P12]
	v_add_co_u32 v[P12], vcc, KNOD_PERSIST_RING_CQ_OFF, v[P12]
	v_add_co_u32 v[P24], vcc, s10, v[P12]
	v_mov_b32 v[P24+1], s11
	V_ADD_CI_U32 v[P24+1], vcc, 0, v[P24+1], vcc
	global_load_dword v[P28], v[P24:P24+1], off offset:44 glc dlc
	global_load_dword v[P28+1], v[P24:P24+1], off offset:60 glc dlc
	s_waitcnt vmcnt(0)
	// Ours when the owner bit is the pass round the ring the position is
	// on and the opcode is not the invalid one a fresh ring holds.
	v_lshrrev_b32 v[P12+1], 24, v[P28+1]		// op_own
	v_lshrrev_b32 v[P14], s[PS2+1], v[P10+1]
	v_and_b32 v[P14], 1, v[P14]
	v_and_b32 v[P14+1], 1, v[P12+1]
	v_cmp_eq_u32_e64 s[PS0:PS1], v[P14], v[P14+1]
	v_lshrrev_b32 v[P12+1], 4, v[P12+1]
	v_cmp_ne_u32_e64 s[GT:GT+1], 15, v[P12+1]
	s_and_b64 s[PS0:PS1], s[PS0:PS1], s[GT:GT+1]
	// Only the unbroken run from the head is this round's.
	s_not_b64 s[GT:GT+1], s[PS0:PS1]
	s_ff1_i32_b64 s[PS6+3], s[GT:GT+1]
	s_cmp_lt_i32 s[PS6+3], 0
	s_cselect_b32 s[PS6+3], 64, s[PS6+3]
	s_cmp_eq_u32 s[PS6+3], 0
	s_cbranch_scc0 .Lgda_work\@
	// Nothing new.  What was sent still frees RQ entries, and nothing else
	// would: with every entry held the NIC has nowhere to put a packet.
	v_readlane_b32 s[E_PC], v[GS], GL_SQ_PC
	v_readlane_b32 s[E_CC], v[GS], GL_SQ_CC
	s_cmp_eq_u32 s[E_PC], s[E_CC]
	s_cbranch_scc1 .Lgda_idle\@
	v_readlane_b32 s[E_TXCI], v[GS], GL_TX_CI
	v_readlane_b32 s[E_MASK], v[GS], GL_SQ_MASK
	GDA_TX_REAP
	v_writelane_b32 v[GS], s[E_CC], GL_SQ_CC
	v_writelane_b32 v[GS], s[E_TXCI], GL_TX_CI
	GDA_RQ_RELEASE
.Lgda_idle\@:
	s_sleep 1
	s_setpc_b64 s[6:7]
.Lgda_work\@:
	// Maps the host changed while we were parked, and anything else a
	// program reads that someone else writes.
	buffer_gl1_inv
	buffer_gl0_inv
	s_dcache_inv
	s_waitcnt vmcnt(0) lgkmcnt(0)
	s_load_dword s[GT], s[PARAM:PARAM+1], KNOD_BLOB_PARAM_PAGE_SHIFT

	s_bfm_b64 s[PS0:PS1], s[PS6+3], 0
	s_cmp_eq_u32 s[PS6+3], 64
	s_cselect_b64 s[PS0:PS1], -1, s[PS0:PS1]
	s_mov_b64 exec, s[PS0:PS1]
	s_mov_b64 s[INITIAL_EXEC:INITIAL_EXEC+1], exec
	s_mov_b64 s[DONE_MASK:DONE_MASK+1], 0

	// The RQ entry this CQE completes, and so the page: entry i is page i.
	v_perm_b32 v[P12+1], v[P28+1], v[P28+1], 0x0c0c0001
	s_lshl_b32 s[PS0], 1, s[PS2]
	s_sub_u32 s[PS0], s[PS0], 1
	v_and_b32 v[PAGE_IDX], s[PS0], v[P12+1]
	v_perm_b32 v[P14], v[P28], v[P28], 0x00010203	// byte count

	// page base = rx_base + (page << page_shift); data past the headroom.
	s_waitcnt lgkmcnt(0)
	s_sub_u32 s[GT+1], 32, s[GT]
	v_lshlrev_b32 v[P14+1], s[GT], v[PAGE_IDX]
	v_lshrrev_b32 v[P16], s[GT+1], v[PAGE_IDX]
	v_add_co_u32 v[PAGE_BASE], vcc, s[PS4+1], v[P14+1]
	v_mov_b32 v[PAGE_BASE+1], s[PS6]
	V_ADD_CI_U32 v[PAGE_BASE+1], vcc, v[P16], v[PAGE_BASE+1], vcc
	v_add_co_u32 v[DATA], vcc, s[PS4], v[PAGE_BASE]
	V_ADD_CI_U32 v[DATA+1], vcc, 0, v[PAGE_BASE+1], vcc
	v_add_co_u32 v[DATA_END], vcc, v[DATA], v[P14]
	V_ADD_CI_U32 v[DATA_END+1], vcc, 0, v[DATA+1], vcc

	// The descriptor a program's bounds are read from: offset and length
	// sharing a dword, then the page.
	v_lshlrev_b32 v[P16+1], 6, v[P10]
	v_add_co_u32 v[SLOT], vcc, s[PS6+1], v[P16+1]
	v_mov_b32 v[SLOT+1], s[PS6+2]
	V_ADD_CI_U32 v[SLOT+1], vcc, 0, v[SLOT+1], vcc
	v_lshlrev_b32 v[P20], 16, v[P14]
	v_or_b32 v[P20], s[PS4], v[P20]
	v_mov_b32 v[P20+1], v[PAGE_IDX]
	global_store_dwordx2 v[SLOT:SLOT+1], v[P20:P20+1], off \
			     offset:KNOD_BLOB_BD_OFF glc slc
	s_waitcnt_vscnt null, 0

	// The rest of what KNOD_PROLOGUE_BODY leaves the program: which lane
	// of which queue, r1 = &param->sub[idx], r10 at the top of the frame.
	v_mov_b32 v[TID], v[P10]
	v_mov_b32 v[P18], v[P10]
	s_mul_i32 s[PS0], s[WG_Y], KNOD_GDA_LANES
	V_ADD_U32 v[IDX], s[PS0], v[P10]
	v_mad_u64_u32 v[BPF_R1:BPF_R1+1], s[PS0:PS0+1], KNOD_BLOB_SUB_SIZE, \
		      v[IDX], s[PARAM:PARAM+1]
	v_add_co_u32 v[BPF_R1], vcc, KNOD_BLOB_PARAM_SUB, v[BPF_R1]
	V_ADD_CI_U32 v[BPF_R1+1], vcc, 0, v[BPF_R1+1], vcc
	v_mov_b32 v[CTX], v[BPF_R1]
	v_mov_b32 v[CTX+1], v[BPF_R1+1]
	v_mov_b32 v[BPF_R10], KNOD_BLOB_BPF_STACK_SIZE
	v_mov_b32 v[BPF_R10+1], 0
	s_mov_b32 s[FRAME], 0
	s_branch .Lgda_go\@
.Lgda_end\@:
	s_endpgm
.Lgda_go\@:
	.endm

	.macro KNOD_GDA_EPILOGUE_BODY
	// A lane that did not return drops, as in KNOD_EPILOGUE_PRE_BODY.
	s_andn2_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1], \
		    s[DONE_MASK:DONE_MASK+1]
	v_mov_b32 v[BPF_R0], XDP_DROP
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]

	// Whatever the program wrote - maps above all - is out before the round
	// ends: parking is where the host reads and changes them.
	s_waitcnt vmcnt(0) lgkmcnt(0)
	s_waitcnt_vscnt null, 0

	s_bcnt1_i32_b64 s[E_N], s[INITIAL_EXEC:INITIAL_EXEC+1]
	s_mov_b32 s[E_CI], s2
	v_readlane_b32 s[E_PC], v[GS], GL_SQ_PC
	v_readlane_b32 s[E_CC], v[GS], GL_SQ_CC
	v_readlane_b32 s[E_TXCI], v[GS], GL_TX_CI
	v_readlane_b32 s[E_MASK], v[GS], GL_SQ_MASK
	s_cmp_eq_u32 s[E_MASK], 0
	s_cbranch_scc1 .Lgda_released\@

	// Room first: what the NIC has sent since the last look.
	s_cmp_eq_u32 s[E_PC], s[E_CC]
	s_cbranch_scc1 .Lgda_room\@
	GDA_TX_REAP
.Lgda_room\@:
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]
	v_cmp_eq_u32_e64 s[E_TX:E_TX+1], XDP_TX, v[BPF_R0]
	s_and_b64 s[E_TX:E_TX+1], s[E_TX:E_TX+1], exec
	s_bcnt1_i32_b64 s[E_K], s[E_TX:E_TX+1]
	s_cmp_eq_u32 s[E_K], 0
	s_cbranch_scc1 .Lgda_sent\@
	s_sub_u32 s[E_T], s[E_PC], s[E_CC]
	s_sub_u32 s[E_T], s[E_MASK], s[E_T]
	s_add_u32 s[E_T], s[E_T], 1			// room
	s_cmp_le_u32 s[E_K], s[E_T]
	s_cbranch_scc1 .Lgda_fits\@
	// More than the SQ has room for: the first ones go, the rest drop.
	s_sub_u32 s[GT], s[E_K], s[E_T]
	v_readlane_b32 s[GT+1], v[GS], GL_TX_FULL
	s_add_u32 s[GT+1], s[GT+1], s[GT]
	v_writelane_b32 v[GS], s[GT+1], GL_TX_FULL
	v_readlane_b32 s[GT+1], v[GS], GL_TX_FULL + 1
	s_addc_u32 s[GT+1], s[GT+1], 0
	v_writelane_b32 v[GS], s[GT+1], GL_TX_FULL + 1
	s_mov_b64 exec, s[E_TX:E_TX+1]
	v_mbcnt_lo_u32_b32 v[P2], s[E_TX], 0
	v_mbcnt_hi_u32_b32 v[P2], s[E_TX+1], v[P2]
	v_cmp_gt_u32_e64 s[E_TX:E_TX+1], s[E_T], v[P2]
	s_and_b64 s[E_TX:E_TX+1], s[E_TX:E_TX+1], exec
	s_mov_b32 s[E_K], s[E_T]
	s_cmp_eq_u32 s[E_K], 0
	s_cbranch_scc1 .Lgda_sent\@
.Lgda_fits\@:
	// One send WQE per lane that goes, at pc + its rank among them.
	s_mov_b64 exec, s[E_TX:E_TX+1]
	v_mbcnt_lo_u32_b32 v[P2], s[E_TX], 0
	v_mbcnt_hi_u32_b32 v[P2], s[E_TX+1], v[P2]
	V_ADD_U32 v[P3], s[E_PC], v[P2]			// WQE counter
	v_and_b32 v[P4], s[E_MASK], v[P3]
	v_lshlrev_b32 v[P4], 6, v[P4]
	v_readlane_b32 s[GT], v[GS], GL_SQ
	v_readlane_b32 s[GT+1], v[GS], GL_SQ + 1
	v_add_co_u32 v[P8], vcc, s[GT], v[P4]
	v_mov_b32 v[P8+1], s[GT+1]
	V_ADD_CI_U32 v[P8+1], vcc, 0, v[P8+1], vcc

	// The NIC's address for the packet: rx_dma[page_idx] + its offset.
	v_readlane_b32 s[GT], v[GS], GL_RX_DMA
	v_readlane_b32 s[GT+1], v[GS], GL_RX_DMA + 1
	v_lshlrev_b32 v[P4], 3, v[PAGE_IDX]
	v_add_co_u32 v[P20], vcc, s[GT], v[P4]
	v_mov_b32 v[P20+1], s[GT+1]
	V_ADD_CI_U32 v[P20+1], vcc, 0, v[P20+1], vcc
	global_load_dwordx2 v[P12:P12+1], v[P20:P20+1], off
	V_SUB_U32 v[P22], v[DATA], v[PAGE_BASE]		// offset
	V_SUB_U32 v[P22+1], v[DATA_END], v[DATA]		// length

	// ctrl: opmod_idx_opcode and qpn_ds, both big endian, then fm_ce_se:
	// a completion for the round's last WQE only.
	v_and_b32 v[P5], 0xffff, v[P3]
	v_lshlrev_b32 v[P5], 8, v[P5]
	v_or_b32 v[P5], KNOD_MLX5_OPCODE_SEND, v[P5]
	v_perm_b32 v[P0], v[P5], v[P5], 0x00010203
	v_readlane_b32 s[GT], v[GS], GL_SQN
	s_lshl_b32 s[GT], s[GT], 8
	s_or_b32 s[GT], s[GT], KNOD_MLX5_SEND_DS
	v_mov_b32 v[P5], s[GT]
	v_perm_b32 v[P0+1], v[P5], v[P5], 0x00010203
	s_sub_u32 s[GT], s[E_K], 1
	v_cmp_eq_u32_e64 s[GT:GT+1], s[GT], v[P2]
	v_mov_b32 v[P5], (KNOD_MLX5_CTRL_CQ_UPDATE << 24)
	v_cndmask_b32_e64 v[P0+2], 0, v[P5], s[GT:GT+1]
	v_mov_b32 v[P0+3], 0

	// eth: nothing inlined, no offloads.
	v_mov_b32 v[P4], 0
	v_mov_b32 v[P5], 0
	v_mov_b32 v[P6], 0
	v_mov_b32 v[P7], 0

	// data: byte count, lkey, address.  Big endian, but for the lkey,
	// which the NIC handed over already swapped.
	v_readlane_b32 s[GT], v[GS], GL_TX_MKEY
	s_waitcnt vmcnt(0)
	v_add_co_u32 v[P12], vcc, v[P12], v[P22]
	V_ADD_CI_U32 v[P12+1], vcc, 0, v[P12+1], vcc
	v_perm_b32 v[P16], v[P22+1], v[P22+1], 0x00010203
	v_mov_b32 v[P16+1], s[GT]
	v_perm_b32 v[P16+2], v[P12+1], v[P12+1], 0x00010203
	v_perm_b32 v[P16+3], v[P12], v[P12], 0x00010203

	global_store_dwordx4 v[P8:P8+1], v[P0:P0+3], off glc slc
	global_store_dwordx4 v[P8:P8+1], v[P4:P4+3], off offset:16 glc slc
	global_store_dwordx4 v[P8:P8+1], v[P16:P16+3], off offset:32 glc slc

	// The doorbell is the last WQE's first eight bytes.
	s_flbit_i32_b64 s[GT], s[E_TX:E_TX+1]
	s_sub_u32 s[GT], 63, s[GT]
	v_readlane_b32 s[E_T], v[P0], s[GT]
	v_readlane_b32 s[E_N], v[P0+1], s[GT]
	// Which RQ position this round's first WQE came in on.
	s_ff1_i32_b64 s[GT], s[E_TX:E_TX+1]
	s_add_u32 s[GT+1], s[E_CI], s[GT]
	s_mov_b64 exec, 1
	v_mov_b32 v[P2], s[GT+1]
	s_and_b32 s[GT], s[E_PC], s[E_MASK]
	s_lshl_b32 s[GT], s[GT], 2
	s_add_u32 s[GT], s[GT], KNOD_PERSIST_RING_RQPOS_OFF
	s_add_u32 s[GT], s10, s[GT]
	s_addc_u32 s[GT+1], s11, 0
	v_mov_b32 v[P4], s[GT]
	v_mov_b32 v[P5], s[GT+1]
	global_store_dword v[P4:P5], v[P2], off glc slc
	v_mov_b32 v[P6], s[E_T]
	v_mov_b32 v[P7], s[E_N]
	// The WQEs are in memory before the record says they are there, and the
	// record before the doorbell.
	s_add_u32 s[E_PC], s[E_PC], s[E_K]
	s_waitcnt_vscnt null, 0
	s_and_b32 s[GT], s[E_PC], 0xffff
	v_mov_b32 v[P2], s[GT]
	v_perm_b32 v[P2], v[P2], v[P2], 0x00010203
	v_mov_b32 v[P4], s10
	v_mov_b32 v[P5], s11
	global_store_dword v[P4:P5], v[P2], off \
			   offset:KNOD_PERSIST_RING_SQ_DB glc slc
	s_waitcnt_vscnt null, 0
	v_readlane_b32 s[GT], v[GS], GL_DB
	v_readlane_b32 s[GT+1], v[GS], GL_DB + 1
	v_mov_b32 v[P4], s[GT]
	v_mov_b32 v[P5], s[GT+1]
	global_store_dwordx2 v[P4:P5], v[P6:P7], off glc slc
	s_bcnt1_i32_b64 s[E_N], s[INITIAL_EXEC:INITIAL_EXEC+1]

	v_readlane_b32 s[GT], v[GS], GL_TX_PKTS
	v_readlane_b32 s[GT+1], v[GS], GL_TX_PKTS + 1
	s_add_u32 s[GT], s[GT], s[E_K]
	s_addc_u32 s[GT+1], s[GT+1], 0
	v_writelane_b32 v[GS], s[GT], GL_TX_PKTS
	v_writelane_b32 v[GS], s[GT+1], GL_TX_PKTS + 1
.Lgda_sent\@:
	v_writelane_b32 v[GS], s[E_PC], GL_SQ_PC
	v_writelane_b32 v[GS], s[E_CC], GL_SQ_CC
	v_writelane_b32 v[GS], s[E_TXCI], GL_TX_CI
.Lgda_released\@:
	// Every other verdict hands its RQ entry straight back, as a drop does.
	s_add_u32 s2, s2, s[E_N]
	GDA_RQ_RELEASE
	v_add_co_u32 v[GDA_PKTS], vcc, s[E_N], v[GDA_PKTS]
	V_ADD_CI_U32 v[GDA_PKTS+1], vcc, 0, v[GDA_PKTS+1], vcc
	GDA_ENTRY_ADDR
	GDA_SAVE
	s_setpc_b64 s[6:7]
	.endm

#endif /* GDA_INC */
