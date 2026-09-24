// SPDX-License-Identifier: GPL-2.0-or-later
//
// GDA stage 2: the receive kernel, what runs in a program's place while there
// is none.  It is the GDA prologue and epilogue with every verdict a drop, so
// it takes up the rings - and whatever is still being sent - exactly where a
// program left them, and leaves them for the next one the same way.

#include "common.inc"
#include "gda.inc"

	.text
	.globl knod_gda_rx_kernel
	.p2align 2
knod_gda_rx_kernel:
	KNOD_GDA_PROLOGUE_BODY
	v_mov_b32 v[BPF_R0], XDP_DROP
	s_mov_b64 s[DONE_MASK:DONE_MASK+1], exec
	KNOD_GDA_EPILOGUE_BODY
	.size knod_gda_rx_kernel, . - knod_gda_rx_kernel
