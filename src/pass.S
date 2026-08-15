// SPDX-License-Identifier: GPL-2.0-or-later
//
// The kernel that runs while the BPF feature is selected but no program is
// attached, and the one detach flips back to.
//
// It is the same prologue and the same epilogue every translated program gets,
// with a fixed verdict where the program would be.  Nothing about it follows a
// program, so unlike those it is one routine spliced whole rather than pieces
// the JIT fills between.

#include "common.inc"
#include "prologue.inc"
#include "epilogue.inc"

	.text

	.globl knod_pass_kernel
	.p2align 2
knod_pass_kernel:
	KNOD_PROLOGUE_BODY

	// Every lane that had a packet is finished, and finished with PASS.
	// Saying so here is what lets the epilogue below be the ordinary one:
	// it turns lanes back on by the done mask, and this claims them all.
	s_mov_b64 s[DONE_MASK:DONE_MASK+1], s[INITIAL_EXEC:INITIAL_EXEC+1]
	v_mov_b32 v[BPF_R0], XDP_PASS

	// No packet cache to write back, so the two halves meet directly.
	KNOD_EPILOGUE_PRE_BODY
	KNOD_EPILOGUE_POST_BODY

	.size knod_pass_kernel, . - knod_pass_kernel
