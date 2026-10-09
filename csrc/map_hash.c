// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hash map lookup, update and delete.
 *
 * Compiled to the AMDGPU calling convention: the descriptor in v[0:1], the
 * key's dwords after it, for an update the value's after those, the result
 * back in v[0:1].  The routine in map_hash.S around each moves the arguments
 * in and keeps what the call may destroy.
 *
 * Map memory other CUs write is read past L0 and L1 (glc dlc), with device
 * scope relaxed atomics.  One routine per key length, because the key arrives
 * in registers and the length decides how many.
 */
#include "map.h"

#define NEXT_END	0x7fffffffu
#define NEXT_MASK	0x7fffffffu
#define NEXT_DELETED	0x80000000u

#define E2BIG		7
#define ENOENT		2
#define ENOMEM		12

static inline uint32_t rol32(uint32_t x, int n)
{
	return (x << n) | (x >> (32 - n));
}

#define JHASH_MIX(a, b, c) do {				\
	a -= c; a ^= rol32(c, 4);  c += b;		\
	b -= a; b ^= rol32(a, 6);  a += c;		\
	c -= b; c ^= rol32(b, 8);  b += a;		\
	a -= c; a ^= rol32(c, 16); c += b;		\
	b -= a; b ^= rol32(a, 19); a += c;		\
	c -= b; c ^= rol32(b, 4);  b += a;		\
} while (0)

#define JHASH_FINAL(a, b, c) do {			\
	c ^= b; c -= rol32(b, 14);			\
	a ^= c; a -= rol32(c, 11);			\
	b ^= a; b -= rol32(a, 25);			\
	c ^= b; c -= rol32(b, 16);			\
	a ^= c; a -= rol32(c, 4);			\
	b ^= a; b -= rol32(a, 14);			\
	c ^= b; c -= rol32(b, 24);			\
} while (0)

/*
 * lib/jhash.h jhash() over the key's bytes, so that a key hashes to the
 * bucket the kernel put it in.  The key is zero padded to whole dwords, which
 * makes jhash()'s byte tail the same as adding the last dwords.
 */
static inline uint32_t jhash(desc_t d, const uint32_t *k, int n)
{
	uint32_t a, b, c;

	a = b = c = 0xdeadbeef + d->key_size + d->hashrnd;
	while (n > 3) {
		a += k[0];
		b += k[1];
		c += k[2];
		JHASH_MIX(a, b, c);
		n -= 3;
		k += 3;
	}
	switch (n) {
	case 3:
		c += k[2];
		/* fallthrough */
	case 2:
		b += k[1];
		/* fallthrough */
	case 1:
		a += k[0];
		JHASH_FINAL(a, b, c);
	}
	return c;
}

static inline gptr bucket_of(desc_t d, const uint32_t *key, int n)
{
	gptr bucket = (gptr)d->bucket_gaddr;

	return &bucket[jhash(d, key, n) & (d->n_buckets - 1)];
}

static inline GLOBAL uint8_t *elem_at(desc_t d, uint32_t id)
{
	return (GLOBAL uint8_t *)d->elems_gaddr + (uint64_t)id * d->elem_size;
}

static inline gptr kv_of(GLOBAL uint8_t *e)
{
	return (gptr)(e + KNOD_BLOB_ELEM_KV_OFF);
}

static inline gptr lru_of(GLOBAL uint8_t *e)
{
	return (gptr)(e + KNOD_BLOB_ELEM_LRU_OFF);
}

static inline int is_lru(desc_t d)
{
	return d->flags & KNOD_BLOB_MAP_LRU;
}

/* As the kernel's LRU hash: used, so eviction passes it over once more. */
static inline void mark_used(GLOBAL uint8_t *e, uint32_t lru)
{
	if (!(lru & KNOD_BLOB_ELEM_REF))
		__scoped_atomic_fetch_or(lru_of(e), KNOD_BLOB_ELEM_REF,
					 __ATOMIC_RELAXED,
					 __MEMORY_SCOPE_DEVICE);
}

static inline int same_key(GLOBAL uint8_t *e, const uint32_t *key, int n)
{
	int same = 1;

	for (int i = 0; i < n; i++)
		same &= load(&kv_of(e)[i]) == key[i];
	return same;
}

/* This queue's copy of the value: one per workgroup row. */
static inline gptr value_of(desc_t d, GLOBAL uint8_t *e, int n, int percpu)
{
	uint32_t off = KNOD_BLOB_ELEM_VALUE_OFF(n);

	if (percpu)
		off += d->per_instance_size * __builtin_amdgcn_workgroup_id_y();
	return (gptr)(e + off);
}

/*
 * A lane whose chain ends drops out of the walk, so lanes at different depths
 * share it and it runs as long as any lane is still looking.  A deleted
 * element can still be on a chain a walk reached it through: walk past it.
 */
static inline uint64_t lookup(desc_t dv, const uint32_t *key, int n,
			      int percpu)
{
	desc_t d = uniform_desc(dv);
	uint32_t next = load(bucket_of(d, key, n)) & NEXT_MASK;

	while (next != NEXT_END) {
		GLOBAL uint8_t *e = elem_at(d, next);
		/* The link and the LRU word together, one request. */
		uint64_t w = __scoped_atomic_load_n((GLOBAL uint64_t *)e,
						    __ATOMIC_RELAXED,
						    __MEMORY_SCOPE_DEVICE);
		uint32_t link = w;

		if (same_key(e, key, n) && !(link & NEXT_DELETED)) {
			if (is_lru(d))
				mark_used(e, w >> 32);
			return (uint64_t)value_of(d, e, n, percpu);
		}
		next = link & NEXT_MASK;
	}
	return 0;
}

static inline void store_value(desc_t d, gptr v, const uint32_t *val)
{
	uint32_t words = d->value_size >> 2;

	/* Unrolled, so the value stays in registers rather than a stack. */
#pragma clang loop unroll(full)
	for (int i = 0; i < KNOD_BLOB_VALUE_CHUNKS_MAX; i++)
		if (i < words)
			store(&v[i], val[i]);
}

/*
 * Take element @id off the chain at @cell, whose lock the caller holds, and
 * mark it deleted for a walker already on it.  False if it is not there.
 */
static inline int unlink(gptr cell, desc_t d, uint32_t id)
{
	uint32_t next = load(cell) & NEXT_MASK;
	gptr prev = cell;

	while (next != NEXT_END) {
		GLOBAL uint8_t *e = elem_at(d, next);
		uint32_t link = load((gptr)e);

		if (next == id) {
			store(prev, link);
			store((gptr)e, link | NEXT_DELETED);
			store(lru_of(e), 0);
			return 1;
		}
		prev = (gptr)e;
		next = link & NEXT_MASK;
	}
	return 0;
}

/*
 * For an LRU hash with no free element: an element to reuse, by the clock -
 * one used since the hand last passed is passed over once more, and the
 * first that was not is taken off its chain.  Its bucket's lock is taken
 * only if it is free: the caller holds @cell's already, and waiting on
 * another while holding one could wait forever.  NEXT_END if a whole turn
 * found nothing it could take.
 */
static inline uint32_t evict(desc_t d, gptr cell)
{
	gptr hand = (gptr)d->clock_gaddr, vcell, lock;
	GLOBAL uint8_t *e;
	uint32_t id, lru;
	int got;

	for (uint32_t i = 0; i < 2 * d->max_entries; i++) {
		id = add(hand, 1) & (d->max_entries - 1);
		e = elem_at(d, id);
		lru = load(lru_of(e));
		if (!(lru & KNOD_BLOB_ELEM_LIVE))
			continue;
		if (lru & KNOD_BLOB_ELEM_REF) {
			__scoped_atomic_fetch_and(lru_of(e),
						  ~KNOD_BLOB_ELEM_REF,
						  __ATOMIC_RELAXED,
						  __MEMORY_SCOPE_DEVICE);
			continue;
		}
		vcell = &((gptr)d->bucket_gaddr)[lru >>
						   KNOD_BLOB_ELEM_BUCKET_SHIFT];
		lock = (gptr)((uint64_t)vcell + d->lock_offset);
		if (vcell != cell &&
		    __scoped_atomic_exchange_n(lock, 1, __ATOMIC_RELAXED,
					       __MEMORY_SCOPE_DEVICE))
			continue;
		got = unlink(vcell, d, id);
		if (vcell != cell) {
			release();
			store(lock, 0);
		}
		if (got)
			return id;
	}
	return NEXT_END;
}

/* Every instance's copy of a reused element's value, which an insert into a
 * percpu map otherwise leaves to the host to have zeroed.
 */
static inline void zero_values(desc_t d, GLOBAL uint8_t *e, int n)
{
	uint32_t words = d->value_size >> 2;
	gptr v = (gptr)(e + KNOD_BLOB_ELEM_VALUE_OFF(n));

	for (uint32_t c = 0; c < d->n_instances; c++)
		for (uint32_t i = 0; i < words; i++)
			store(&v[c * (d->per_instance_size >> 2) + i], 0);
}

/*
 * Replace the key's value, or take an element off the free queue and link it
 * in at the head.  The element is filled before the head points at it, so a
 * walker never reaches one whose contents are not there yet.  A full map
 * inserts nothing and says so, as the kernel's does - unless it is an LRU
 * one, which evicts an element to reuse.
 */
static inline int update_one(desc_t d, gptr cell, const uint32_t *key, int n,
			     const uint32_t *val, int percpu)
{
	uint32_t next = load(cell) & NEXT_MASK;
	GLOBAL uint8_t *e;
	gptr cur, queue;
	int32_t slot;
	uint32_t id;

	while (next != NEXT_END) {
		e = elem_at(d, next);
		if (same_key(e, key, n)) {
			store_value(d, value_of(d, e, n, percpu), val);
			if (is_lru(d))
				mark_used(e, 0);
			return 0;
		}
		next = load((gptr)e) & NEXT_MASK;
	}

	cur = (gptr)d->free_cur_gaddr;
	slot = (int32_t)add(cur, -1) - 1;
	if (slot >= 0) {
		queue = (gptr)d->queue_gaddr;
		id = load(&queue[slot]);
		e = elem_at(d, id);
	} else {
		add(cur, 1);
		if (!is_lru(d))
			return -E2BIG;
		id = evict(d, cell);
		if (id == NEXT_END)
			return -ENOMEM;
		e = elem_at(d, id);
		if (percpu)
			zero_values(d, e, n);
	}

	store((gptr)e, load(cell));
	for (int i = 0; i < n; i++)
		store(&kv_of(e)[i], key[i]);
	/* A free element's other instances were zeroed when the host freed it. */
	store_value(d, value_of(d, e, n, percpu), val);
	store(lru_of(e), KNOD_BLOB_ELEM_LIVE |
			 (uint32_t)(cell - (gptr)d->bucket_gaddr) <<
			 KNOD_BLOB_ELEM_BUCKET_SHIFT);
	release();
	store(cell, id);
	return 0;
}

/*
 * Unlink the key's element and hand its index to the GC list for the host to
 * reclaim.
 */
static inline int delete_one(desc_t d, gptr cell, const uint32_t *key, int n)
{
	uint32_t next = load(cell) & NEXT_MASK;
	gptr list;

	while (next != NEXT_END) {
		GLOBAL uint8_t *e = elem_at(d, next);

		if (same_key(e, key, n)) {
			unlink(cell, d, next);
			release();
			list = (gptr)d->gc_list_gaddr;
			store(&list[add((gptr)d->gc_count_gaddr, 1)], next);
			return 0;
		}
		next = load((gptr)e) & NEXT_MASK;
	}
	return -ENOENT;
}

/*
 * Writers change the chain, so they hold the bucket's lock, and lanes of one
 * wave cannot all hold it at once.  Lanes are grouped by bucket so that
 * different buckets do not wait on each other; within a bucket one lane takes
 * the lock and the lanes are served one at a time.
 *
 * Every loop here is over a mask of lanes, the same in every lane, so the
 * whole wave runs it together and a lane does its part in the iteration that
 * names it.  A loop a lane leaves on its own condition would not do: the
 * compiler may run what follows such a loop for all the lanes at once.
 */
static inline int64_t write(desc_t dv, const uint32_t *key, int n,
			    const uint32_t *val, int percpu)
{
	desc_t d = uniform_desc(dv);
	gptr cell = bucket_of(d, key, n);
	uint64_t todo = __builtin_amdgcn_ballot_w64(1);
	int ret = 0;

	while (todo) {
		uint32_t first = __builtin_ctzll(todo);
		uint64_t c = readlane64((uint64_t)cell, first);
		uint64_t group = __builtin_amdgcn_ballot_w64((uint64_t)cell == c);
		gptr lock = (gptr)(c + d->lock_offset);

		if (lane_id() == first)
			while (__scoped_atomic_exchange_n(lock, 1,
							  __ATOMIC_RELAXED,
							  __MEMORY_SCOPE_DEVICE))
				;
		for (uint64_t g = group; g; g &= g - 1) {
			if (lane_id() != __builtin_ctzll(g))
				continue;
			if (val)
				ret = update_one(d, cell, key, n, val, percpu);
			else
				ret = delete_one(d, cell, key, n);
			/* The next lane, or holder, may read what it wrote. */
			release();
		}
		if (lane_id() == first)
			store(lock, 0);
		todo &= ~group;
	}
	return ret;
}

#define KEY(n)	A##n(uint32_t k)
#define VAL	A14(uint32_t v)

#define DEFINE_HASH(n)							\
uint64_t cfn_lookup_hash_k##n(desc_t d, KEY(n))				\
{									\
	uint32_t k[n] = { A##n(k) };					\
									\
	return lookup(d, k, n, 0);					\
}									\
uint64_t cfn_lookup_percpu_hash_k##n(desc_t d, KEY(n))			\
{									\
	uint32_t k[n] = { A##n(k) };					\
									\
	return lookup(d, k, n, 1);					\
}									\
uint64_t cfn_update_hash_k##n(desc_t d, KEY(n), VAL)			\
{									\
	uint32_t k[n] = { A##n(k) }, v[] = { A14(v) };			\
									\
	return write(d, k, n, v, 0);					\
}									\
uint64_t cfn_update_percpu_hash_k##n(desc_t d, KEY(n), VAL)		\
{									\
	uint32_t k[n] = { A##n(k) }, v[] = { A14(v) };			\
									\
	return write(d, k, n, v, 1);					\
}									\
uint64_t cfn_delete_hash_k##n(desc_t d, KEY(n))				\
{									\
	uint32_t k[n] = { A##n(k) };					\
									\
	return write(d, k, n, 0, 0);					\
}

DEFINE_HASH(1)
DEFINE_HASH(2)
DEFINE_HASH(3)
DEFINE_HASH(4)
DEFINE_HASH(5)
DEFINE_HASH(6)
DEFINE_HASH(7)
DEFINE_HASH(8)
DEFINE_HASH(9)
DEFINE_HASH(10)
DEFINE_HASH(11)
DEFINE_HASH(12)
DEFINE_HASH(13)
DEFINE_HASH(14)
