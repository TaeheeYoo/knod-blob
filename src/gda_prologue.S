// SPDX-License-Identifier: GPL-2.0-or-later
//
// What runs before the translated BPF program when the shader runs the NIC's
// receive rings: the program's packets come from the CQ, not a host batch.

#include "common.inc"
#include "gda.inc"

	.text

	.globl knod_gda_prologue
	.p2align 2
knod_gda_prologue:
	KNOD_GDA_PROLOGUE_BODY
	.size knod_gda_prologue, . - knod_gda_prologue
