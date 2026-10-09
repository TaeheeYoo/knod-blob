// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Array map lookup, update and delete.
 *
 * An array indexes straight into storage, so the key length never changes the
 * code and one routine covers every map of a type.  Elements sit value_size
 * apart with nothing between them, and only whole dwords of a value are
 * written; the JIT keeps other lengths away.  A percpu array is one instance
 * per queue, and only its own workgroup writes one, so nothing here goes past
 * the caches the way the hash routines do.
 */
#include "map.h"

static inline gptr slot(desc_t dv, uint32_t key, int percpu)
{
	desc_t d = uniform_desc(dv);
	GLOBAL uint8_t *base = (GLOBAL uint8_t *)d->elems_gaddr;

	if (key >= d->max_entries)
		return 0;
	if (percpu)
		base += d->per_instance_size * __builtin_amdgcn_workgroup_id_y();
	return (gptr)(base + (uint64_t)key * d->value_size);
}

static inline void fill(desc_t dv, gptr v, const uint32_t *val)
{
	uint32_t words = uniform_desc(dv)->value_size >> 2;

#pragma clang loop unroll(full)
	for (int i = 0; i < KNOD_BLOB_VALUE_CHUNKS_MAX; i++)
		if (i < words)
			v[i] = val[i];
}

#define E2BIG		7
#define EEXIST		17
#define EINVAL		22

#define BPF_NOEXIST	1
#define BPF_EXIST	2

/* As the kernel's: every element is always there, so BPF_NOEXIST is refused,
 * and none can be deleted.
 */
#define DEFINE_ARRAY(name, percpu)					\
uint64_t cfn_lookup_##name(desc_t d, uint32_t key)			\
{									\
	return (uint64_t)slot(d, key, percpu);				\
}									\
uint64_t cfn_update_##name(desc_t d, uint32_t key, A14(uint32_t v),	\
			   uint32_t flags)				\
{									\
	uint32_t v[] = { A14(v) };					\
	gptr s = slot(d, key, percpu);					\
									\
	if (flags > BPF_EXIST)						\
		return (uint64_t)-EINVAL;				\
	if (!s)								\
		return (uint64_t)-E2BIG;				\
	if (flags == BPF_NOEXIST)					\
		return (uint64_t)-EEXIST;				\
	fill(d, s, v);							\
	return 0;							\
}									\
uint64_t cfn_delete_##name(desc_t d, uint32_t key)			\
{									\
	return (uint64_t)-EINVAL;					\
}

DEFINE_ARRAY(array, 0)
DEFINE_ARRAY(percpu_array, 1)
