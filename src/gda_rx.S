// SPDX-License-Identifier: GPL-2.0-or-later
//
// The program that runs while there is none: every packet goes to the host,
// as with no XDP program at all.  The engine around it takes up the rings -
// and whatever is still being sent - exactly where a program left them, and
// leaves them for the next one the same way.

//
// Called like any program: its packets where KNOD_BLOB_PRO_* says, and back
// through KNOD_BLOB_PRO_RET_SREG.  Every lane returns, so the drop a JITed
// program ends with for the lanes that did not has nothing to do here.

#include "common.inc"

	.text
	.globl knod_gda_rx_kernel
	.p2align 2
knod_gda_rx_kernel:
	v_mov_b32 v[BPF_R0], XDP_PASS
	s_setpc_b64 s[KNOD_BLOB_PRO_RET_SREG:KNOD_BLOB_PRO_RET_SREG+1]
	.size knod_gda_rx_kernel, . - knod_gda_rx_kernel
