// SPDX-License-Identifier: GPL-2.0-or-later
//
// A routine compiled from csrc/: knod_<fn> is cfn_<fn>'s body, which the JIT
// calls by the calling convention knod_blob.h sets out.

#ifndef CFN_INC
#define CFN_INC

#if KNOD_ISA == 10
# include "cfn.10.inc"
#else
# include "cfn.11.inc"
#endif

.macro	ROUTINE fn
	.globl knod_\fn
	.p2align 2
knod_\fn:
	CFN_BODY_\fn
	.size knod_\fn, . - knod_\fn
.endm

#endif
