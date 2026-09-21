// SPDX-License-Identifier: GPL-2.0-or-later
//
// What the core dispatches before a feature has claimed the slot.
//
// It ends the wave and nothing else.  Unlike everything else here it is not
// spliced into a program, so nothing follows it and nothing pads it - the
// padding has to be its own, because the instruction prefetcher reads past the
// end of a wave and faults on whatever it finds there.

#include "common.inc"

	.text

	.globl knod_default_kernel
	.p2align 2
knod_default_kernel:
	s_endpgm

	// Far enough that the prefetcher lands on something valid.  RDNA has an
	// encoding that means exactly this.
	.rept 191
	s_code_end
	.endr
	.size knod_default_kernel, . - knod_default_kernel
