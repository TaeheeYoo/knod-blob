// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hash map lookup in C, for a routine that calls it rather than being it.
 *
 * Compiled for the GPU with the default calling convention: the descriptor
 * in v[0:1], the key's dwords after it, the value's address back in v[0:1].
 * Map memory another CU may have written this round is read past L0 and L1,
 * as the assembly reads it with MAP_COHERENT.
 */
#include <stdint.h>

typedef uint32_t __u32;
typedef uint64_t __u64;
#include <linux/knod_blob.h>

#define GLOBAL		__attribute__((address_space(1)))
/* Fixed for as long as a program runs: scalar loads may read it. */
#define CONSTANT	__attribute__((address_space(4)))

#define NEXT_END	0x7fffffffu
#define NEXT_MASK	0x7fffffffu
#define NEXT_DELETED	0x80000000u

typedef const CONSTANT struct knod_blob_map_desc *desc_t;

static inline uint32_t map_load(const GLOBAL uint32_t *p)
{
	return __scoped_atomic_load_n(p, __ATOMIC_RELAXED,
				      __MEMORY_SCOPE_DEVICE);
}

/* Every lane is handed the same descriptor. */
static inline desc_t uniform(desc_t d)
{
	uint64_t a = (uint64_t)d;

	uint32_t lo = __builtin_amdgcn_readfirstlane((uint32_t)a);
	uint32_t hi = __builtin_amdgcn_readfirstlane((uint32_t)(a >> 32));

	return (desc_t)((uint64_t)hi << 32 | lo);
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

/* lib/jhash.h jhash2(), so a key hashes to the bucket the kernel put it in. */
static inline uint32_t jhash2(const uint32_t *k, int n, uint32_t initval)
{
	uint32_t a, b, c;

	a = b = c = 0xdeadbeef + ((uint32_t)n << 2) + initval;
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

static inline uint64_t lookup_hash(desc_t dv, const uint32_t *key, int n,
				   int percpu)
{
	desc_t d = uniform(dv);
	const GLOBAL uint32_t *bucket = (const GLOBAL uint32_t *)d->bucket_gaddr;
	const GLOBAL uint8_t *elems = (const GLOBAL uint8_t *)d->elems_gaddr;
	uint32_t h = jhash2(key, n, d->key_size + d->hashrnd - (n << 2)) &
		     (d->n_buckets - 1);
	uint32_t next = map_load(&bucket[h]) & NEXT_MASK;

	while (next != NEXT_END) {
		const GLOBAL uint8_t *e = elems + (uint64_t)next * d->elem_size;
		const GLOBAL uint32_t *kv =
			(const GLOBAL uint32_t *)(e + KNOD_BLOB_ELEM_KV_OFF);
		uint32_t link = map_load((const GLOBAL uint32_t *)e);
		int same = 1;

		for (int i = 0; i < n; i++)
			same &= map_load(&kv[i]) == key[i];
		/* A deleted element may still be on the chain: walk past it. */
		if (same && !(link & NEXT_DELETED)) {
			uint32_t off = KNOD_BLOB_ELEM_VALUE_OFF(n);

			/* This queue's instance: one per workgroup row. */
			if (percpu)
				off += d->per_instance_size *
				       __builtin_amdgcn_workgroup_id_y();
			return (uint64_t)(e + off);
		}
		next = link & NEXT_MASK;
	}
	return 0;
}

/* Past eight dwords a key no longer fits below the routine's window. */
#define DEFINE_LOOKUP(n)						\
struct key##n { uint32_t w[n]; };					\
uint64_t cfn_lookup_hash_k##n(desc_t d, struct key##n k)		\
{									\
	return lookup_hash(d, k.w, n, 0);				\
}									\
uint64_t cfn_lookup_percpu_hash_k##n(desc_t d, struct key##n k)	\
{									\
	return lookup_hash(d, k.w, n, 1);				\
}

DEFINE_LOOKUP(1)
DEFINE_LOOKUP(2)
DEFINE_LOOKUP(3)
DEFINE_LOOKUP(4)
DEFINE_LOOKUP(5)
DEFINE_LOOKUP(6)
DEFINE_LOOKUP(7)
DEFINE_LOOKUP(8)
