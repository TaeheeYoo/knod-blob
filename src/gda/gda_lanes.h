/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * What each lane of v[GS] (KNOD_BLOB_PRO_GDA_VREG) holds.  Shared by the C
 * engine and the asm glue, so it has to stay plain #defines.
 *
 * Every wave has its own v[GS].  The configuration and GL_CI/GL_WAVE are kept
 * in all of them; the ring state counts only in wave 0's.  Pairs are low
 * dword first.
 */
#ifndef KNOD_GDA_LANES_H
#define KNOD_GDA_LANES_H

#define GL_RQ_SIZE	0
#define GL_SQ_PC	1	/* WQEs posted, free-running */
#define GL_SQ_CC	2	/* WQEs the NIC completed */
#define GL_TX_CI	3	/* TX CQ consumer index */
#define GL_SQ_MASK	4	/* zero: no SQ, XDP_TX drops */
#define GL_SQN		5
#define GL_TX_MKEY	6	/* big endian already */
#define GL_TX_CQ_LOG	7
#define GL_SQ		8	/* and 9 */
#define GL_DB		10	/* and 11: the NIC's TX doorbell */
#define GL_RX_DMA	12	/* and 13: page index -> the NIC's address */
#define GL_TX_PKTS	14	/* and 15 */
#define GL_TX_FULL	16	/* and 17 */
#define GL_TX_GEN	18
#define GL_RX_BASE	19	/* and 20 */
#define GL_HEADROOM	23
#define GL_RQ_LOG	24
#define GL_CQ_LOG	25
#define GL_PARAM	26	/* and 27 */
#define GL_LDS		28	/* where the waves meet */
#define GL_WAVES	29
#define GL_PAGE_SHIFT	30
#define GL_N		31	/* packets this round, all waves */
#define GL_ROUNDS	32	/* and 33: rounds that took packets */
#define GL_STAGGER	34
#define GL_STAGGER_MASK	35
#define GL_PASS_PC	36	/* PASS entries appended, free-running */
#define GL_PASS_CC	37	/* ...the host has finished with, or the floor */
#define GL_PASS_FLOOR	38	/* entries before it are an earlier build's */
#define GL_PASS_RING	39	/* and 40: in host memory */
#define GL_PASS_MASK	41
#define GL_WAKE		42	/* the host waits for PASS entries */
#define GL_TX_DEAD	43	/* the SQ went to error */
#define GL_LAST_BOUND	44	/* debug: the RQ bound last written */

/*
 * What the hand-written engine kept in SGPRs.  A call into C clobbers s0-s29,
 * so these live here instead and the glue puts back the ones the program
 * expects.
 */
#define GL_CI		45	/* RX CQ consumer index, every wave in step */
#define GL_WAVE		46	/* 64 x this wave */
#define GL_RING		47	/* and 48: the queue's ring buffer */
#define GL_PKTS		49	/* and 50: packets taken */
#define GL_HW_DISPATCH	51	/* and 52: s[4:5] as the hardware gave it */
#define GL_HW_KERNARG	53	/* and 54: s[8:9], the control block */
#define GL_HW_WG_X	55	/* s12 */
#define GL_HW_WG_Y	56	/* s13, the queue */
#define GL_ROUND_PC	57	/* and 58: where a round starts */

#endif
