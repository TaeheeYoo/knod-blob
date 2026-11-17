// SPDX-License-Identifier: GPL-2.0-or-later
//
// The features' claims, KNOD_BLOB_GDA_CLAIM_*: csrc/claim.c's functions as
// they are, which the engine calls through the control block's gda_claim
// wherever the host put one.  Named in a gda_engine*.S, so the Makefile
// leaves them out of the routines a program calls.

	.text
	.globl knod_gda_claim_ipsec
	.p2align 2
knod_gda_claim_ipsec:
	CFN_BODY_gda_claim_ipsec
	.size knod_gda_claim_ipsec, . - knod_gda_claim_ipsec
