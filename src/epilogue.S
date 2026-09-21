// SPDX-License-Identifier: GPL-2.0-or-later
//
// What runs after the translated BPF program.
//
// It tells the host what the program decided and where the packet ended up,
// by writing both into the buffer descriptor the prologue found. The host
// retires completed batches from those descriptors in FIFO order.

#include "common.inc"
#include "epilogue.inc"

	.text

// Lanes that never had a packet, and lanes that left through an early exit,
// were switched off along the way.  Turn the ones that exited back on so their
// verdict is written too, then restore the mask the prologue left.  Everything
// then writes the buffer descriptor the prologue put in v[SLOT].
//
// The workgroup then publishes this queue's sequence in the mailbox. Empty
// physical waves participate in the barrier even though they write no packet.
	.globl knod_epilogue
	.p2align 2
knod_epilogue:
	KNOD_EPILOGUE_PRE_BODY
	KNOD_EPILOGUE_POST_BODY
	.size knod_epilogue, . - knod_epilogue
