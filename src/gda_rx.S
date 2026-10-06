// SPDX-License-Identifier: GPL-2.0-or-later
//
// The program that runs while there is none: every packet goes to the host,
// as with no XDP program at all.  The engine around it takes up the rings -
// and whatever is still being sent - exactly where a program left them, and
// leaves them for the next one the same way.

#include "common.inc"
#include "gda_program.inc"

	.text
	.globl knod_gda_rx_kernel
	.p2align 2
knod_gda_rx_kernel:
	KNOD_GDA_PROGRAM_ENTRY
	v_mov_b32 v[BPF_R0], XDP_PASS
	s_mov_b64 s[DONE_MASK:DONE_MASK+1], exec
	KNOD_GDA_PROGRAM_EXIT
	.size knod_gda_rx_kernel, . - knod_gda_rx_kernel
