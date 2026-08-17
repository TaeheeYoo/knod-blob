// SPDX-License-Identifier: GPL-2.0-or-later
//
// The inbound ESP pipeline: parse the header, resolve the SA, decrypt with
// AES-CTR, GHASH the ciphertext, verify the ICV, answer the host.
//
// One workgroup per packet, 256 threads, one AES block each.  Unlike the BPF
// routines this is not spliced into anything - it is a whole shader, and the
// driver copies it into the kernel slot as it stands.
//
// It is here in three per-generation bodies rather than one because that is
// how it arrived: the driver built it from three generators, and bringing it
// across as three shaders that are known to match is a smaller step than
// bringing it across as one that has been rearranged at the same time.  The
// generations do not differ much - renamed mnemonics, the unaligned-read
// workaround RDNA needs, cache bits - and folding them together is the work
// that follows, one piece at a time, each checked against what is here now.

#include "common.inc"

	.text

	.globl knod_ipsec_fused
	.p2align 2
knod_ipsec_fused:

#if KNOD_ISA == 9
# include "ipsec/gfx9.inc"
#elif KNOD_ISA == 10
# include "ipsec/gfx10.inc"
#elif KNOD_ISA == 11
# include "ipsec/gfx11.inc"
#else
# error "no IPsec shader for this generation"
#endif

	.size knod_ipsec_fused, . - knod_ipsec_fused
