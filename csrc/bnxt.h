/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * What of the bnxt (P5, P7) hardware formats the GDA shader reads and writes: the
 * RX and TX completions it polls and the RX and TX descriptors it posts.
 * The parts of drivers/net/ethernet/broadcom/bnxt/bnxt.h the shader touches;
 * the hardware fixes them, not the kernel.
 *
 * Every field is little endian, as the hardware has it.
 */

#ifndef KNOD_BLOB_BNXT_H
#define KNOD_BLOB_BNXT_H

/* A completion ring entry, and a descriptor, are 1 << 4 bytes.  An RX
 * completion takes two entries and a TX completion one.
 */
#define KNOD_BNXT_CMP_SHIFT		4
#define KNOD_BNXT_BD_SHIFT		4

/* Every completion entry's third dword: set on the ring's even passes. */
#define KNOD_BNXT_CMP_V			0x1
/* rx_cmp_ext's: checksums the NIC checked, and found wrong. */
#define KNOD_BNXT_RX_L4_CS_CALC		(1 << 1)
#define KNOD_BNXT_RX_T_L4_CS_CALC	(1 << 3)
#define KNOD_BNXT_RX_L4_CS_ERROR	(1 << 5)
#define KNOD_BNXT_RX_T_L4_CS_ERROR	(1 << 7)
#define KNOD_BNXT_CMP_TYPE		0x3f
#define KNOD_BNXT_CMP_TYPE_RX_L2	17
#define KNOD_BNXT_CMP_TYPE_RX_L2_V3	23	/* P7's */
#define KNOD_BNXT_CMP_TYPE_TX_COAL	2	/* P7's: up to sq_cons_idx */
#define KNOD_BNXT_TX_CMP_SQ_CONS_MASK	0x00ffffff
#define KNOD_BNXT_LEN_SHIFT		16

#define KNOD_BNXT_RX_BD_PACKET		0x4
#define KNOD_BNXT_RX_BD_EOP		0x80

#define KNOD_BNXT_TX_BD_PACKET_END	0x40
#define KNOD_BNXT_TX_BD_NO_CMPL		0x80
#define KNOD_BNXT_TX_BD_CNT_SHIFT	8
#define KNOD_BNXT_TX_BD_LHINT_SHIFT	13

/* P7: a doorbell's index carries the pass of the ring it is on here. */
#define KNOD_BNXT_DB_EPOCH		(1u << 24)

#ifndef __ASSEMBLY__
#include <linux/types.h>

/* struct rx_cmp: the first half of an RX completion. */
struct knod_bnxt_rx_cmp {
	__le32	len_flags_type;		/* length << 16 | type */
	__u32	opaque;			/* the RX descriptor's */
	__le32	misc_v1;
	__le32	rss_hash;
};

/* struct rx_cmp_ext: its second half. */
struct knod_bnxt_rx_cmp_ext {
	__le32	flags2;
	__le32	meta_data;
	__le32	errors_v2;
	__le32	rsvd;
};

/* struct tx_cmp */
struct knod_bnxt_tx_cmp {
	__le32	flags_type;
	__u32	opaque;			/* the TX descriptor's */
	__le32	errors_v;
	__le32	sq_cons_idx;
};

/* struct rx_bd and struct tx_bd: one buffer, a whole packet. */
struct knod_bnxt_bd {
	__le32	len_flags_type;
	__u32	opaque;
	__le64	haddr;
};
#endif /* !__ASSEMBLY__ */

#endif /* KNOD_BLOB_BNXT_H */
