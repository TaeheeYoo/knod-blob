// SPDX-License-Identifier: GPL-2.0-or-later
//
// Array map lookup, update and delete: csrc/map_array.c.  The key is one
// dword, the index.

#include "common.inc"
#include "cfn.inc"

	.text

	ROUTINE lookup_array, 1
	ROUTINE update_array, 1, 1
	ROUTINE delete_array, 1
	ROUTINE lookup_percpu_array, 1
	ROUTINE update_percpu_array, 1, 1
	ROUTINE delete_percpu_array, 1
