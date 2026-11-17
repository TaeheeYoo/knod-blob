// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The features' claims on the packets a program passes: which the crypto
 * pool is to take, and as what.  The engine knows no protocol; it calls the
 * one the host put in the control block (src/gda_engine.inc) and puts out
 * as jobs the packets it claims.
 */
#include "gda.h"

/* Four bytes at any offset: gfx10 drops an unaligned dword address's low
 * bits, so two aligned ones and a shift.
 */
static INLINE uint32_t claim_ld32u(GLOBAL const uint8_t *p)
{
	uintptr_t a = (uintptr_t)p;
	GLOBAL const uint32_t *w = (GLOBAL const uint32_t *)(a & ~(uintptr_t)3);
	uint32_t lo = get32(w), hi = get32(w + 1);

	return a & 3 ? __builtin_amdgcn_alignbyte(hi, lo, a & 3) : lo;
}

/*
 * KNOD_BLOB_GDA_CLAIM_IPSEC: an ESP packet that passed, over IPv4 with no
 * options and not a fragment - the stack puts those back together before
 * it decrypts them - to open.
 */
uint32_t cfn_gda_claim_ipsec(uint32_t verdict, uint32_t lo, uint32_t hi,
			     uint32_t len)
{
	GLOBAL const uint8_t *d =
		(GLOBAL const uint8_t *)((uint64_t)hi << 32 | lo);
	uint32_t type_ihl, frag_proto;

	/* Ethernet, IPv4, the SPI and sequence, an IV and an ICV. */
	if (verdict != XDP_PASS || len < 14 + 20 + 8 + 8 + 16)
		return 0;
	type_ihl = claim_ld32u(d + 12);
	frag_proto = claim_ld32u(d + 20);
	if ((type_ihl & 0xffffff) != 0x450008 || (frag_proto & 0xff3f) ||
	    frag_proto >> 24 != 50)
		return 0;
	return KNOD_PERSIST_GDA_JOB_OPEN;
}
