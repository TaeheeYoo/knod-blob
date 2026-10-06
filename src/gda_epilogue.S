// SPDX-License-Identifier: GPL-2.0-or-later
//
// What runs after the translated BPF program when the shader runs the NIC's
// receive rings: every entry the round took goes back to the RQ.

#include "common.inc"
#include "gda.inc"

	.section .text.knod_gda_epilogue, "ax", @progbits
	.globl knod_gda_epilogue
	.p2align 2
knod_gda_epilogue:
	KNOD_GDA_EPILOGUE_BODY knod_gda_epilogue
	GDA_ROUTINE_END knod_gda_epilogue
