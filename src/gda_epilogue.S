// SPDX-License-Identifier: GPL-2.0-or-later
//
// A program's way out: lanes that did not return drop, what it wrote is
// out, and back to the engine.

#include "common.inc"
#include "gda.inc"

	.text

	.globl knod_gda_epilogue
	.p2align 2
knod_gda_epilogue:
	KNOD_GDA_PROGRAM_EXIT
	.size knod_gda_epilogue, . - knod_gda_epilogue
