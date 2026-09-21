// SPDX-License-Identifier: GPL-2.0-or-later
//
// The inbound ESP pipeline: parse the header, resolve the SA, decrypt with
// AES-CTR, GHASH the ciphertext, verify the ICV, answer the host.
//
// One workgroup per packet, 256 threads, one AES block each.  Unlike the BPF
// routines this is not spliced into anything - it is a whole shader, and the
// driver copies it into the kernel slot as it stands.
// The two supported RDNA generations share one body. Their mnemonic and
// unaligned-read differences are selected by the RDNA include.

#include "common.inc"
#include "ipsec.inc"

	.text

	.globl knod_ipsec_fused
	.p2align 2
knod_ipsec_fused:

# include "ipsec/rdna.inc"
# include "ipsec/rdna_body.inc"

	.size knod_ipsec_fused, . - knod_ipsec_fused

/*
 * The stages on their own, for finding out where the time goes.  Only gfx11
 * has them; they were written to answer that question on RDNA3 and nobody has
 * needed them elsewhere.  One kind with the stage in key_chunks rather than
 * three kinds, since they are measurement rather than pipeline.
 */
#if KNOD_ISA >= 11

	.globl knod_ipsec_bench_k0
	.p2align 2
knod_ipsec_bench_k0:
# include "ipsec/bench_base.inc"
	.size knod_ipsec_bench_k0, . - knod_ipsec_bench_k0

	.globl knod_ipsec_bench_k1
	.p2align 2
knod_ipsec_bench_k1:
# include "ipsec/bench_ctr.inc"
	.size knod_ipsec_bench_k1, . - knod_ipsec_bench_k1

	.globl knod_ipsec_bench_k2
	.p2align 2
knod_ipsec_bench_k2:
# include "ipsec/bench_ghash.inc"
	.size knod_ipsec_bench_k2, . - knod_ipsec_bench_k2

#endif
