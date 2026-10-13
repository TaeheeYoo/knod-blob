// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The gate an ordered program parks its lanes at, whatever the NIC: a routine
 * the program calls, not the engine.
 */
#include "gda.h"

static INLINE int bit(LDS const uint32_t *m, uint32_t j)
{
	return (m[j >> 5] >> (j & 31)) & 1;
}

/*
 * Where an ordered program decides which lanes go on (KNOD_BLOB_GDA_GATE):
 * a lane that took part - read what the order is for, or is about to write
 * it - parks if it is not the first of its flow to take part and some lane of
 * its flow is about to write.  Until here nothing a lane did is seen outside
 * it, so a parked one can be run again from where the program resumes it,
 * after its flow's earlier packets; the rest go on together.
 */
uint32_t cfn_gda_gate(uint32_t lds, uint32_t wave, uint32_t flags)
{
	LDS struct gda_meet *meet = meet_at(lds);
	uint32_t me = 64 * uniform32(wave) + lane_id();
	uint32_t rank = flags & KNOD_BLOB_RANK_MASK, n, j, first = 1, any = 0;
	int part = rank != KNOD_BLOB_RANK_NONE &&
		   (flags & (KNOD_BLOB_RANK_REACHED | KNOD_BLOB_RANK_ACTIVE));
	int act = rank != KNOD_BLOB_RANK_NONE &&
		  (flags & KNOD_BLOB_RANK_ACTIVE);
	uint16_t key;

	if (part)
		__scoped_atomic_fetch_or(&meet->part[me >> 5], 1u << (me & 31),
					 __ATOMIC_RELAXED,
					 __MEMORY_SCOPE_WRKGRP);
	if (act)
		__scoped_atomic_fetch_or(&meet->act[me >> 5], 1u << (me & 31),
					 __ATOMIC_RELAXED,
					 __MEMORY_SCOPE_WRKGRP);
	barrier();
	/* Nobody about to write: nobody waits, and no need to look. */
	if (!any_active(meet))
		return 0;

	n = uniform32(meet->count);
	key = meet->key[me];
#pragma clang loop unroll_count(4)
	for (j = 0; j < n; j++) {
		int same = meet->key[j] == key;

		first &= !(same && j < me && bit(meet->part, j));
		any |= same && bit(meet->act, j);
	}
	return part && !first && any;
}
