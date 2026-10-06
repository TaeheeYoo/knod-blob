// SPDX-License-Identifier: GPL-2.0-or-later
//
// A program's two ends, which the JIT splices around it.  The engine calls it
// with s_swappc_b64 s[6:7], having left its packets in the registers
// KNOD_BLOB_PRO_* names, EXEC and INITIAL_EXEC the lanes that have one and
// DONE_MASK clear.

#ifndef GDA_PROGRAM_INC
#define GDA_PROGRAM_INC

	.macro KNOD_GDA_PROGRAM_ENTRY
	v_mov_b32 v[BPF_R10], KNOD_BLOB_BPF_STACK_SIZE
	v_mov_b32 v[BPF_R10+1], 0
	s_mov_b32 s[FRAME], 0
	.endm

	.macro KNOD_GDA_PROGRAM_EXIT
	// A lane that did not return drops.
	s_andn2_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1], \
		    s[DONE_MASK:DONE_MASK+1]
	v_mov_b32 v[BPF_R0], XDP_DROP
	s_mov_b64 exec, s[INITIAL_EXEC:INITIAL_EXEC+1]

	// Whatever the program wrote - maps above all - is out before the round
	// ends: parking is where the host reads and changes them.
	s_waitcnt vmcnt(0) lgkmcnt(0)
	s_waitcnt_vscnt null, 0
	s_setpc_b64 s[6:7]
	.endm

#endif
