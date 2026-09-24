// SPDX-License-Identifier: GPL-2.0-or-later
//
// What runs after the translated BPF program when the shader runs the NIC's
// receive rings: every entry the round took goes back to the RQ.

#include "common.inc"
#include "gda.inc"

	.text

	.globl knod_gda_epilogue
	.p2align 2
knod_gda_epilogue:
	KNOD_GDA_EPILOGUE_BODY
	.size knod_gda_epilogue, . - knod_gda_epilogue
