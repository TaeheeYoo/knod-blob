/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AES-GCM's per-block math for the IPsec engine: AES encryption by T-tables
 * and the GHASH multiply, in plain C so the host can check it against a
 * reference.  How a wave splits a packet among its lanes is the engine's.
 *
 * AES works on the state as four little-endian words, with the round keys as
 * the kernel's aes_expandkey() lays them out in crypto_aes_ctx.key_enc:
 * 4 * (rounds + 1) words.  The tables are aes_enc_tab rotated left by 0, 8,
 * 16 and 24 bits, T0 to T3 back to back; the last round's S-box is byte 1 of
 * T0, as lib/crypto/aes.c takes it.  With AES_TAB_STRIDE the four tables'
 * entry i is at word i * AES_TAB_STRIDE; with AES_TAB_T0 too there is T0
 * alone, the others its rotations.
 *
 * GHASH keeps a 128-bit value as four 32-bit words, w[0] the most significant
 * of the big-endian block: the block's bytes read as big-endian words.  The
 * multiply has no carry-less instruction to use: as in BearSSL's
 * ghash_ctmul32, carry-less 32x32 products come out of integer multiplies of
 * operands with holes in them, Karatsuba takes them to 128x128, and the
 * product, or a sum of them, is reduced.
 *
 * AES_TAB and AES_RK say where the tables and the round keys are; the GPU puts
 * the tables in LDS and reads the round keys from constant memory.
 */
#ifndef KNOD_BLOB_AES_GCM_H
#define KNOD_BLOB_AES_GCM_H

#include <stdint.h>

#ifndef AES_TAB
#define AES_TAB
#endif
#ifndef AES_RK
#define AES_RK
#endif
#ifndef AES_INLINE
#define AES_INLINE	static inline __attribute__((always_inline))
#endif

#define AES_TABLE_WORDS	(4 * 256)

/* Entry @v of table @n. */
#ifndef AES_TAB_STRIDE
#define AES_TAB_STRIDE	1
#endif

/* A byte of state, pinned as the byte it is: the compiler would make each
 * index a shift, a mask and an add, where an extract and a shift-and-add
 * do.
 */
#ifdef __AMDGPU__
#define AES_IDX(x)	({ uint32_t __b = (x); __asm__("" : "+v"(__b)); __b; })
#else
#define AES_IDX(x)	(x)
#endif

#ifdef AES_TAB_T0
#define AES_T(n, v)	aes_rol32(t[AES_IDX(v) * AES_TAB_STRIDE], 8 * (n))
#else
#define AES_T(n, v)	t[(256 * (n) + AES_IDX(v)) * AES_TAB_STRIDE]
#endif

static inline uint32_t aes_rol32(uint32_t v, unsigned int n)
{
	return n ? v << n | v >> (32 - n) : v;
}

static inline uint32_t aes_bswap32(uint32_t v)
{
	return v >> 24 | (v >> 8 & 0xff00) | (v << 8 & 0xff0000) | v << 24;
}

/* An AES round of state @w into @x, round keys @k. */
#define AES_ROUND(x, w, k) do {						\
	(x)[0] = (k)[0] ^ AES_T(0, (w)[0] & 0xff) ^			\
		 AES_T(1, (w)[1] >> 8 & 0xff) ^				\
		 AES_T(2, (w)[2] >> 16 & 0xff) ^ AES_T(3, (w)[3] >> 24);	\
	(x)[1] = (k)[1] ^ AES_T(0, (w)[1] & 0xff) ^			\
		 AES_T(1, (w)[2] >> 8 & 0xff) ^				\
		 AES_T(2, (w)[3] >> 16 & 0xff) ^ AES_T(3, (w)[0] >> 24);	\
	(x)[2] = (k)[2] ^ AES_T(0, (w)[2] & 0xff) ^			\
		 AES_T(1, (w)[3] >> 8 & 0xff) ^				\
		 AES_T(2, (w)[0] >> 16 & 0xff) ^ AES_T(3, (w)[1] >> 24);	\
	(x)[3] = (k)[3] ^ AES_T(0, (w)[3] & 0xff) ^			\
		 AES_T(1, (w)[0] >> 8 & 0xff) ^				\
		 AES_T(2, (w)[1] >> 16 & 0xff) ^ AES_T(3, (w)[2] >> 24);	\
} while (0)

/* The last round, S-box alone. */
#define AES_LAST(o, w, k) do {						\
	(o)[0] = (k)[0] ^ AES_SB((w)[0]) ^ AES_SB((w)[1] >> 8) << 8 ^	\
		 AES_SB((w)[2] >> 16) << 16 ^ AES_SB((w)[3] >> 24) << 24;	\
	(o)[1] = (k)[1] ^ AES_SB((w)[1]) ^ AES_SB((w)[2] >> 8) << 8 ^	\
		 AES_SB((w)[3] >> 16) << 16 ^ AES_SB((w)[0] >> 24) << 24;	\
	(o)[2] = (k)[2] ^ AES_SB((w)[2]) ^ AES_SB((w)[3] >> 8) << 8 ^	\
		 AES_SB((w)[0] >> 16) << 16 ^ AES_SB((w)[1] >> 24) << 24;	\
	(o)[3] = (k)[3] ^ AES_SB((w)[3]) ^ AES_SB((w)[0] >> 8) << 8 ^	\
		 AES_SB((w)[1] >> 16) << 16 ^ AES_SB((w)[2] >> 24) << 24;	\
} while (0)
#define AES_SB(v)	(AES_T(0, (v) & 0xff) >> 8 & 0xff)

/* One AES block: @in to @out, four little-endian words each. */
AES_INLINE void aes_encrypt_block(const AES_TAB uint32_t *t,
				  const AES_RK uint32_t *rk,
				  unsigned int rounds, uint32_t out[4],
				  const uint32_t in[4])
{
	uint32_t w[4], x[4];
	unsigned int r, i;

	for (i = 0; i < 4; i++)
		w[i] = in[i] ^ rk[i];
	for (r = 1; r < rounds; r++) {
		AES_ROUND(x, w, rk + 4 * r);
		for (i = 0; i < 4; i++)
			w[i] = x[i];
	}
	AES_LAST(out, w, rk + 4 * rounds);
}

/*
 * Two AES blocks at once, @in to @out, eight words each: each round's
 * lookups for both go out before either's are waited for, so a block's
 * wait on LDS is the other's work.
 */
AES_INLINE void aes_encrypt_block2(const AES_TAB uint32_t *t,
				   const AES_RK uint32_t *rk,
				   unsigned int rounds, uint32_t out[8],
				   const uint32_t in[8])
{
	uint32_t w[8], x[8], k[4];
	unsigned int r, i;

	for (i = 0; i < 8; i++)
		w[i] = in[i] ^ rk[i & 3];
	for (r = 1; r < rounds; r++) {
		for (i = 0; i < 4; i++)
			k[i] = rk[4 * r + i];
		AES_ROUND(x, w, k);
		AES_ROUND(x + 4, w + 4, k);
		for (i = 0; i < 8; i++)
			w[i] = x[i];
	}
	for (i = 0; i < 4; i++)
		k[i] = rk[4 * rounds + i];
	AES_LAST(out, w, k);
	AES_LAST(out + 4, w + 4, k);
}

/*
 * GCM's counter block for an ESP packet, as AES takes it: salt, the packet's
 * 8-byte IV and the 32-bit big-endian counter - 1 for the tag's mask, 2 for
 * the first block of payload.  @salt and @iv as the bytes read little-endian.
 */
AES_INLINE void gcm_counter(uint32_t out[4], uint32_t salt, uint32_t iv_lo,
			    uint32_t iv_hi, uint32_t ctr)
{
	out[0] = salt;
	out[1] = iv_lo;
	out[2] = iv_hi;
	out[3] = aes_bswap32(ctr);
}

AES_INLINE void gcm_xor(uint32_t out[4], const uint32_t a[4],
			const uint32_t b[4])
{
	out[0] = a[0] ^ b[0];
	out[1] = a[1] ^ b[1];
	out[2] = a[2] ^ b[2];
	out[3] = a[3] ^ b[3];
}

/* A block as AES's little-endian words, as GHASH's big-endian ones. */
AES_INLINE void gcm_to_ghash(uint32_t out[4], const uint32_t in[4])
{
	out[0] = aes_bswap32(in[0]);
	out[1] = aes_bswap32(in[1]);
	out[2] = aes_bswap32(in[2]);
	out[3] = aes_bswap32(in[3]);
}

/* A value the compiler must take as made here, in order with the others: it
 * would run every product at once, for the latency, and want more registers
 * than a wave with the round in its others has.
 */
#ifdef __AMDGPU__
#define GCM_HERE(v)	__asm__ volatile("" : "+v"(v))
#else
#define GCM_HERE(v)	do { } while (0)
#endif

/* The low half of a carry-less 32x32 multiply, from integer multiplies of
 * operands with every fourth bit kept: no carry crosses into a kept bit.
 */
AES_INLINE uint32_t gcm_bmul32(uint32_t x, uint32_t y)
{
	uint32_t x0 = x & 0x11111111, x1 = x & 0x22222222;
	uint32_t x2 = x & 0x44444444, x3 = x & 0x88888888;
	uint32_t y0 = y & 0x11111111, y1 = y & 0x22222222;
	uint32_t y2 = y & 0x44444444, y3 = y & 0x88888888;
	uint32_t z0, z1, z2, z3;

	z0 = x0 * y0 ^ x1 * y3 ^ x2 * y2 ^ x3 * y1;
	GCM_HERE(z0);
	GCM_HERE(y0);
	z1 = x0 * y1 ^ x1 * y0 ^ x2 * y3 ^ x3 * y2;
	GCM_HERE(z1);
	GCM_HERE(y1);
	z2 = x0 * y2 ^ x1 * y1 ^ x2 * y0 ^ x3 * y3;
	GCM_HERE(z2);
	GCM_HERE(y2);
	z3 = x0 * y3 ^ x1 * y2 ^ x2 * y1 ^ x3 * y0;
	return (z0 & 0x11111111) | (z1 & 0x22222222) |
	       (z2 & 0x44444444) | (z3 & 0x88888888);
}

/* The 32-bit word with its bits in reverse order. */
AES_INLINE uint32_t gcm_rev32(uint32_t x)
{
#if __has_builtin(__builtin_bitreverse32)
	return __builtin_bitreverse32(x);
#else
	x = (x & 0x55555555) << 1 | (x >> 1 & 0x55555555);
	x = (x & 0x33333333) << 2 | (x >> 2 & 0x33333333);
	x = (x & 0x0f0f0f0f) << 4 | (x >> 4 & 0x0f0f0f0f);
	x = (x & 0x00ff00ff) << 8 | (x >> 8 & 0x00ff00ff);
	return x << 16 | x >> 16;
#endif
}


/*
 * GHASH's multiply, in GF(2^128) with GCM's bit order.  GCM puts x^0 in the
 * top bit of a block's first byte, so a big-endian word bit-reversed holds
 * x^(32i)..x^(32i+31) from its bit 0: in that order it is a plain carry-less
 * product - Karatsuba twice, nine 32x32 multiplies - reduced by
 * x^128 + x^7 + x^2 + x + 1, and reversed back.  Reducing is linear, so a sum
 * of products is reduced once: gcm_mul_add() adds @x * @h unreduced to @z,
 * eight words low first, and gcm_reduce() takes the sum to a block.
 *
 * Each 32x32 product is the reversed operands' whole carry-less product, its
 * two 32-bit halves (gcm_bmul64()).  Karatsuba's sums are
 * linear too, so each product goes straight into the words it ends up in,
 * two landings on one word cancelling.  Written out: a table of them is one a
 * compiler may leave in .rodata, which the blob does not carry.
 */
/* Product halves @lo and @hi into @z from word @k up. */
#define GCM_ADD(z, k, lo, hi)	do {				\
	(z)[k] ^= (lo);						\
	(z)[(k) + 1] ^= (hi);					\
} while (0)

/* The whole 63-bit carry-less product of @x and @y, as gcm_bmul32() makes the
 * low half: a column sums eight terms at most, which a nibble holds with no
 * carry into the next kept bit.  A 32x32 multiply to 64 bits is one
 * instruction, v_mad_u64_u32, where the two halves took two.
 */
AES_INLINE uint64_t gcm_bmul64(uint32_t x, uint32_t y)
{
	uint32_t x0 = x & 0x11111111, x1 = x & 0x22222222;
	uint32_t x2 = x & 0x44444444, x3 = x & 0x88888888;
	uint32_t y0 = y & 0x11111111, y1 = y & 0x22222222;
	uint32_t y2 = y & 0x44444444, y3 = y & 0x88888888;
	uint64_t z0, z1, z2, z3;

	z0 = (uint64_t)x0 * y0 ^ (uint64_t)x1 * y3 ^ (uint64_t)x2 * y2 ^
	     (uint64_t)x3 * y1;
	z1 = (uint64_t)x0 * y1 ^ (uint64_t)x1 * y0 ^ (uint64_t)x2 * y3 ^
	     (uint64_t)x3 * y2;
	z2 = (uint64_t)x0 * y2 ^ (uint64_t)x1 * y1 ^ (uint64_t)x2 * y0 ^
	     (uint64_t)x3 * y3;
	z3 = (uint64_t)x0 * y3 ^ (uint64_t)x1 * y2 ^ (uint64_t)x2 * y1 ^
	     (uint64_t)x3 * y0;
	return (z0 & 0x1111111111111111ull) | (z1 & 0x2222222222222222ull) |
	       (z2 & 0x4444444444444444ull) | (z3 & 0x8888888888888888ull);
}

/* @a * @b carry-less, in two halves: the product of the reversed operands,
 * whose low half is the low one and whose high half the high one.  Each
 * product is added in and its halves pinned before the next starts, which
 * keeps the registers down.
 */
AES_INLINE void gcm_clmul32(uint32_t a, uint32_t b, uint32_t *lo,
			    uint32_t *hi)
{
	uint64_t p;

	GCM_HERE(a);
	GCM_HERE(b);
	p = gcm_bmul64(gcm_rev32(a), gcm_rev32(b));
	*lo = (uint32_t)p;
	*hi = (uint32_t)(p >> 32);
}

AES_INLINE void gcm_mul_add(uint32_t z[8], const uint32_t x[4],
			    const uint32_t h[4])
{
	uint32_t a[4], b[4], c0, c1, d0, d1, lo, hi;
	int i;

	for (i = 0; i < 4; i++) {
		a[i] = x[i];
		b[i] = h[i];
	}

	/* The low 64 bits of each times the low. */
	gcm_clmul32(a[0], b[0], &lo, &hi);
	GCM_ADD(z, 0, lo, hi);
	GCM_ADD(z, 1, lo, hi);
	GCM_ADD(z, 2, lo, hi);
	GCM_ADD(z, 3, lo, hi);
	GCM_HERE(lo);
	GCM_HERE(hi);
	gcm_clmul32(a[1], b[1], &lo, &hi);
	GCM_ADD(z, 1, lo, hi);
	GCM_ADD(z, 2, lo, hi);
	GCM_ADD(z, 3, lo, hi);
	GCM_ADD(z, 4, lo, hi);
	GCM_HERE(lo);
	GCM_HERE(hi);
	c0 = a[0];
	c1 = a[1];
	d0 = b[0];
	d1 = b[1];
	GCM_HERE(c0);
	GCM_HERE(c1);
	GCM_HERE(d0);
	GCM_HERE(d1);
	gcm_clmul32(c0 ^ c1, d0 ^ d1, &lo, &hi);
	GCM_ADD(z, 1, lo, hi);
	GCM_ADD(z, 3, lo, hi);
	GCM_HERE(lo);
	GCM_HERE(hi);

	/* The high times the high. */
	gcm_clmul32(a[2], b[2], &lo, &hi);
	GCM_ADD(z, 2, lo, hi);
	GCM_ADD(z, 3, lo, hi);
	GCM_ADD(z, 4, lo, hi);
	GCM_ADD(z, 5, lo, hi);
	GCM_HERE(lo);
	GCM_HERE(hi);
	gcm_clmul32(a[3], b[3], &lo, &hi);
	GCM_ADD(z, 3, lo, hi);
	GCM_ADD(z, 4, lo, hi);
	GCM_ADD(z, 5, lo, hi);
	GCM_ADD(z, 6, lo, hi);
	GCM_HERE(lo);
	GCM_HERE(hi);
	c0 = a[2];
	c1 = a[3];
	d0 = b[2];
	d1 = b[3];
	GCM_HERE(c0);
	GCM_HERE(c1);
	GCM_HERE(d0);
	GCM_HERE(d1);
	gcm_clmul32(c0 ^ c1, d0 ^ d1, &lo, &hi);
	GCM_ADD(z, 3, lo, hi);
	GCM_ADD(z, 5, lo, hi);
	GCM_HERE(lo);
	GCM_HERE(hi);

	/* The sums times the sums. */
	for (i = 0; i < 4; i++) {
		GCM_HERE(a[i]);
		GCM_HERE(b[i]);
	}
	c0 = a[0] ^ a[2];
	c1 = a[1] ^ a[3];
	d0 = b[0] ^ b[2];
	d1 = b[1] ^ b[3];
	gcm_clmul32(c0, d0, &lo, &hi);
	GCM_ADD(z, 2, lo, hi);
	GCM_ADD(z, 3, lo, hi);
	GCM_HERE(lo);
	GCM_HERE(hi);
	gcm_clmul32(c1, d1, &lo, &hi);
	GCM_ADD(z, 3, lo, hi);
	GCM_ADD(z, 4, lo, hi);
	GCM_HERE(lo);
	GCM_HERE(hi);
	GCM_HERE(c0);
	GCM_HERE(c1);
	GCM_HERE(d0);
	GCM_HERE(d1);
	gcm_clmul32(c0 ^ c1, d0 ^ d1, &lo, &hi);
	GCM_ADD(z, 3, lo, hi);
	GCM_HERE(lo);
	GCM_HERE(hi);
}

/* x^128 = x^7 + x^2 + x + 1: fold the top words down, from the top, so what
 * one fold carries into a top word is folded in turn.
 */
AES_INLINE void gcm_reduce(uint32_t out[4], uint32_t z[8])
{
	uint32_t t;
	int i;

	for (i = 7; i >= 4; i--) {
		t = z[i];
		z[i - 4] ^= t ^ t << 1 ^ t << 2 ^ t << 7;
		z[i - 3] ^= t >> 31 ^ t >> 30 ^ t >> 25;
	}

	for (i = 0; i < 4; i++)
		out[i] = gcm_rev32(z[i]);
}

/* @x * @h to @out. */
AES_INLINE void gcm_mul(uint32_t out[4], const uint32_t x[4],
			const uint32_t h[4])
{
	uint32_t z[8] = { 0 };

	gcm_mul_add(z, x, h);
	gcm_reduce(out, z);
}

#endif /* KNOD_BLOB_AES_GCM_H */
