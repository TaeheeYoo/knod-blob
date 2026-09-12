// SPDX-License-Identifier: GPL-2.0-or-later
//
// What runs after the translated BPF program.
//
// It tells the host what the program decided and where the packet ended up,
// by writing both into the buffer descriptor the prologue found, and for a
// packet the program let through it also claims a delivery slot.

#include "common.inc"
#include "epilogue.inc"

	.text

// Lanes that never had a packet, and lanes that left through an early exit,
// were switched off along the way.  Turn the ones that exited back on so their
// verdict is written too, then restore the mask the prologue left.  Everything
// then writes the buffer descriptor the prologue put in v[SLOT].
//
// A packet the program passed needs somewhere for the host to find it: a slot
// in this queue's delivery buffer, claimed by bumping the queue's counter, and
// its index recorded so the host knows which descriptor the slot belongs to.
// Lanes whose verdict was anything else have nothing left to do, and if no
// lane passed that part is skipped.
	.globl knod_epilogue
	.p2align 2
knod_epilogue:
	KNOD_EPILOGUE_PRE_BODY
	KNOD_EPILOGUE_POST_BODY
	.size knod_epilogue, . - knod_epilogue
