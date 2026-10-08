// SPDX-License-Identifier: GPL-2.0-or-later
//
// Hash map lookup, update and delete: csrc/map_hash.c.  One routine per key
// length, because the key arrives in registers and the length decides how
// many.

#include "common.inc"
#include "cfn.inc"

	.text

.irp n, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
	ROUTINE lookup_hash_k\n
	ROUTINE lookup_percpu_hash_k\n
	ROUTINE update_hash_k\n
	ROUTINE update_percpu_hash_k\n
	ROUTINE delete_hash_k\n
.endr
