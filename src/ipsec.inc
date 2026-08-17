/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * What the IPsec shader keeps where.
 *
 * Only the registers that mean one thing for the whole shader are named.  The
 * rest - v1 through v21, s24 through s31 - are the AES and GHASH working set,
 * reused for something different in every phase, and a name on those would say
 * less than the number does.  The generator this came from drew the same line:
 * it referred to these through symbols and to the working set by number.
 */

#ifndef IPSEC_INC
#define IPSEC_INC

/* Scalars, uniform across the workgroup. */
#define SR_MASK			32	/* 0xFF, for pulling bytes out */
#define SR_LOOP_CTR		33
#define SR_T_ADDR		34	/* s[34:35] T-table being copied in */
#define SR_IV0			36	/* the ESP IV, three words of it */
#define SR_IV1			37
#define SR_IV2			38
#define SR_NR_ROUNDS		39	/* 10, 12 or 14, from the SA */
#define SR_BSWAP		44	/* v_perm selector that swaps a dword */
#define SR_CTEXT_LEN		50
#define SR_NBLOCKS_GCM		51	/* ceil(ctext_len / 16) */
#define SR_HTABLE_LO		52	/* s[52:53] the H-power table */
#define SR_HTABLE_HI		53
#define SR_TOTAL_GHASH_BLK	54	/* blocks plus AAD plus the length */
#define SR_SA_MODE		55	/* transport or tunnel */
#define SR_RK2			56	/* s[56:59] the round key not in use */

/* The thread id, and the counter block for the ICV. */
#define VR_TID			0
#define VR_J0_0			19
#define VR_J0_3			22

/* Packet bytes read before the crypto needs them. */
#define VR_PREFETCH0		23
#define VR_PREFETCH1		24
#define VR_PREFETCH2		25
#define VR_PREFETCH3		26
#define VR_PREFETCH4		27

/* What has to survive the crypto, which owns everything below v30. */
#define VR_SAVE_SLOT		30
#define VR_SAVE_BD_LO		31
#define VR_SAVE_BD_HI		32
#define VR_SAVE_PKT_LO		33
#define VR_SAVE_PKT_HI		34
#define VR_SAVE_PKTLEN		35
#define VR_SAVE_OUT_LO		36
#define VR_SAVE_OUT_HI		37
#define VR_SAVE_SEQ		38
#define VR_SAVE_SPI		39
#define VR_SAVE_ESP_OFF		42

#endif /* IPSEC_INC */
