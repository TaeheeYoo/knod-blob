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
#include <stdint.h>

typedef uint32_t __u32;
typedef uint64_t __u64;
#include <linux/knod_blob.h>

#define GLOBAL		__attribute__((address_space(1)))
/* Unchanged while a program runs, so read with scalar loads. */
#define CONSTANT	__attribute__((address_space(4)))

#define NEXT_END	0x7fffffffu
#define NEXT_MASK	0x7fffffffu
#define NEXT_DELETED	0x80000000u

typedef const CONSTANT struct knod_blob_map_desc *desc_t;
typedef GLOBAL uint32_t *gptr;

static inline uint32_t load(gptr p)
{
	return __scoped_atomic_load_n(p, __ATOMIC_RELAXED,
				      __MEMORY_SCOPE_DEVICE);
}

static inline void store(gptr p, uint32_t v)
{
	__scoped_atomic_store_n(p, v, __ATOMIC_RELAXED, __MEMORY_SCOPE_DEVICE);
}

static inline uint32_t add(gptr p, uint32_t v)
{
	return __scoped_atomic_fetch_add(p, v, __ATOMIC_RELAXED,
					 __MEMORY_SCOPE_DEVICE);
}

/* Every store before it lands before any store after it. */
static inline void release(void)
{
	__builtin_amdgcn_fence(__ATOMIC_RELEASE, "agent");
}

static inline uint32_t uniform32(uint32_t v)
{
	return __builtin_amdgcn_readfirstlane(v);
}

static inline uint64_t uniform64(uint64_t v)
{
	return (uint64_t)uniform32(v >> 32) << 32 | uniform32(v);
}

/* Every lane is handed the same descriptor. */
static inline desc_t uniform_desc(desc_t d)
{
	return (desc_t)uniform64((uint64_t)d);
}

static inline uint32_t lane_id(void)
{
	return __builtin_amdgcn_mbcnt_hi(~0u, __builtin_amdgcn_mbcnt_lo(~0u, 0));
}


static inline uint64_t readlane64(uint64_t v, uint32_t lane)
{
	return (uint64_t)__builtin_amdgcn_readlane(v >> 32, lane) << 32 |
	       (uint32_t)__builtin_amdgcn_readlane(v, lane);
}

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
		uint32_t link = load((gptr)e);

		if (same_key(e, key, n) && !(link & NEXT_DELETED))
			return (uint64_t)value_of(d, e, n, percpu);
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
 * Replace the key's value, or take an element off the free queue and link it
 * in at the head.  The element is filled before the head points at it, so a
 * walker never reaches one whose contents are not there yet.  A full map
 * inserts nothing.
 */
static inline void update_one(desc_t d, gptr cell, const uint32_t *key, int n,
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
			return;
		}
		next = load((gptr)e) & NEXT_MASK;
	}

	cur = (gptr)d->free_cur_gaddr;
	slot = (int32_t)add(cur, -1) - 1;
	if (slot < 0) {
		add(cur, 1);
		return;
	}
	queue = (gptr)d->queue_gaddr;
	id = load(&queue[slot]);
	e = elem_at(d, id);

	store((gptr)e, load(cell));
	for (int i = 0; i < n; i++)
		store(&kv_of(e)[i], key[i]);
	/* The other instances' copies were zeroed when the host freed it. */
	store_value(d, value_of(d, e, n, percpu), val);
	release();
	store(cell, id);
}

/*
 * Unlink the key's element, mark it deleted for a walker already on it, and
 * hand its index to the GC list for the host to reclaim.
 */
static inline void delete_one(desc_t d, gptr cell, const uint32_t *key, int n)
{
	uint32_t next = load(cell) & NEXT_MASK;
	gptr prev = cell, list;

	while (next != NEXT_END) {
		GLOBAL uint8_t *e = elem_at(d, next);
		uint32_t link = load((gptr)e);

		if (same_key(e, key, n)) {
			store(prev, link);
			store((gptr)e, link | NEXT_DELETED);
			release();
			list = (gptr)d->gc_list_gaddr;
			store(&list[add((gptr)d->gc_count_gaddr, 1)], next);
			return;
		}
		prev = (gptr)e;
		next = link & NEXT_MASK;
	}
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
static inline void write(desc_t dv, const uint32_t *key, int n,
			 const uint32_t *val, int percpu)
{
	desc_t d = uniform_desc(dv);
	gptr cell = bucket_of(d, key, n);
	uint64_t todo = __builtin_amdgcn_ballot_w64(1);

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
				update_one(d, cell, key, n, val, percpu);
			else
				delete_one(d, cell, key, n);
			/* The next lane, or holder, may read what it wrote. */
			release();
		}
		if (lane_id() == first)
			store(lock, 0);
		todo &= ~group;
	}
}

/*
 * The key and the value are passed as that many dwords, each its own
 * argument: an aggregate or an array would be passed through a stack.
 */
#define A1(t)	t##_0
#define A2(t)	A1(t), t##_1
#define A3(t)	A2(t), t##_2
#define A4(t)	A3(t), t##_3
#define A5(t)	A4(t), t##_4
#define A6(t)	A5(t), t##_5
#define A7(t)	A6(t), t##_6
#define A8(t)	A7(t), t##_7
#define A9(t)	A8(t), t##_8
#define A10(t)	A9(t), t##_9
#define A11(t)	A10(t), t##_10
#define A12(t)	A11(t), t##_11
#define A13(t)	A12(t), t##_12
#define A14(t)	A13(t), t##_13

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
	write(d, k, n, v, 0);						\
	return 0;							\
}									\
uint64_t cfn_update_percpu_hash_k##n(desc_t d, KEY(n), VAL)		\
{									\
	uint32_t k[n] = { A##n(k) }, v[] = { A14(v) };			\
									\
	write(d, k, n, v, 1);						\
	return 0;							\
}									\
uint64_t cfn_delete_hash_k##n(desc_t d, KEY(n))				\
{									\
	uint32_t k[n] = { A##n(k) };					\
									\
	write(d, k, n, 0, 0);						\
	return 0;							\
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
