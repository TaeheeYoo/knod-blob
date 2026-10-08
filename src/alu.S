// SPDX-License-Identifier: GPL-2.0-or-later
//
// BPF_DIV and BPF_MOD the JIT calls for: csrc/alu.c.

#include "common.inc"
#include "cfn.inc"

	.text

	.irp fn, div32, div64, mod32, mod64, sdiv32, sdiv64, smod32, smod64
	ROUTINE \fn
	.endr
