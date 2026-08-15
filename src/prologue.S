// SPDX-License-Identifier: GPL-2.0-or-later
//
// What runs before the translated BPF program.
//
// It works out which packet this lane is for and hands the program a context,
// the packet's bounds, and the address of the buffer descriptor the epilogue
// will write back to.  Lanes with no packet are switched off and stay off.
//
// Two things follow the program rather than the hardware and are emitted by
// the JIT after this: zeroing the EXEC-save scalars, one per pair the control
// flow needs, and preloading the packet cache, one load per sixteen bytes the
// program reaches into.

#include "common.inc"
#include "prologue.inc"

	.text

	.globl knod_prologue
	.p2align 2
knod_prologue:
	KNOD_PROLOGUE_BODY
	.size knod_prologue, . - knod_prologue
