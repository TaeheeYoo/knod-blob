// SPDX-License-Identifier: GPL-2.0-or-later
//
// What runs before the translated BPF program.
//
// It works out which packet this lane is for and hands the program a context,
// the packet's bounds, and the address of the buffer descriptor the epilogue
// will write back to.  Lanes with no packet are switched off and stay off.
//
// The JIT follows this by zeroing the EXEC-save scalars, one per pair the
// program's control flow needs.

#include "common.inc"
#include "prologue.inc"

	.text

	.globl knod_prologue
	.p2align 2
knod_prologue:
	KNOD_PROLOGUE_BODY
	.size knod_prologue, . - knod_prologue
