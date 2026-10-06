// SPDX-License-Identifier: GPL-2.0-or-later
//
// A program's way in: its frame.  The engine has its packets in place.

#include "common.inc"
#include "gda_program.inc"

	.text

	.globl knod_gda_prologue
	.p2align 2
knod_gda_prologue:
	KNOD_GDA_PROGRAM_ENTRY
	.size knod_gda_prologue, . - knod_gda_prologue
