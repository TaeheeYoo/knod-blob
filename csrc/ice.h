/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * What of the Intel E810 (ice) hardware formats the GDA shader reads and
 * writes: the RX descriptor it posts and the NIC writes back in place, in the
 * flex NIC profile (RXDID 2), and the TX data descriptor.  The parts of
 * drivers/net/ethernet/intel/ice/ice_lan_tx_rx.h the shader touches; the
 * hardware fixes them, not the kernel.
 *
 * Every field is little endian, as the hardware has it.
 */

#ifndef KNOD_BLOB_ICE_H
#define KNOD_BLOB_ICE_H

/* 32-byte RX descriptors, 16-byte TX ones. */
#define KNOD_ICE_RX_DESC_SHIFT		5
#define KNOD_ICE_TX_DESC_SHIFT		4

/* status_error0 */
#define KNOD_ICE_RX_DD			0x1
#define KNOD_ICE_RX_EOF			0x2
#define KNOD_ICE_RX_LEN_MASK		0x3fff

/* The RX tail moves in eights: the hardware ignores its low three bits. */
#define KNOD_ICE_RX_TAIL_ALIGN		8

/* cmd_type_offset_bsz: the type low, the command from bit 4, the buffer's
 * size from bit 34.  A descriptor that asked for one (RS) gets DONE written
 * back as its type once sent.
 */
#define KNOD_ICE_TX_CMD_SHIFT		4
#define KNOD_ICE_TX_CMD_EOP		0x1
#define KNOD_ICE_TX_CMD_RS		0x2
#define KNOD_ICE_TX_LEN_SHIFT		34
#define KNOD_ICE_TX_DTYPE		0xf
#define KNOD_ICE_TX_DTYPE_DONE		0xf

#ifndef __ASSEMBLY__
#include <linux/types.h>

/* union ice_32byte_rx_desc, as posted. */
struct knod_ice_rx_desc {
	__le64	pkt_addr;
	__le64	hdr_addr;	/* zero: no header split */
	__le64	rsvd[2];
};

/* struct ice_32b_rx_flex_desc_nic: the first half, as written back. */
struct knod_ice_rx_wb {
	__u8	rxdid;
	__u8	mir_id_umb_cast;
	__le16	ptype_flexi_flags0;
	__le16	pkt_len;
	__le16	hdr_len_sph_flex_flags1;
	__le16	status_error0;
	__le16	l2tag1;
	__le32	rss_hash;
};

/* struct ice_tx_desc */
struct knod_ice_tx_desc {
	__le64	buf_addr;
	__le64	cmd_type_offset_bsz;
};
#endif /* !__ASSEMBLY__ */

#endif /* KNOD_BLOB_ICE_H */
