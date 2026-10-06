// SPDX-License-Identifier: GPL-2.0-or-later
//
// The kernel every GDA shader starts in.  The program goes right after it,
// and the call's offset to it is knod_gda_engine_call's to patch.

#include "common.inc"
#include "gda.inc"

	.text
	.globl knod_gda_engine
	.p2align 2
knod_gda_engine:
	KNOD_GDA_ROUND_BEGIN
	s_getpc_b64 s[GT:GT+1]
.Lgda_pc:
	s_add_u32 s[GT], s[GT], .Lgda_program - .Lgda_pc
	s_addc_u32 s[GT+1], s[GT+1], 0
	s_swappc_b64 s[6:7], s[GT:GT+1]
	KNOD_GDA_ROUND_END
.Lgda_program:
	.size knod_gda_engine, . - knod_gda_engine
	.globl knod_gda_engine_call
	.set knod_gda_engine_call, .Lgda_pc + 4 - knod_gda_engine
