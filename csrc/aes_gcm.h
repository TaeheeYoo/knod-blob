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
 * T0, as lib/crypto/aes.c takes it.
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

static inline uint32_t aes_rol32(uint32_t v, unsigned int n)
{
	return n ? v << n | v >> (32 - n) : v;
}

static inline uint32_t aes_bswap32(uint32_t v)
{
	return v >> 24 | (v >> 8 & 0xff00) | (v << 8 & 0xff0000) | v << 24;
}

/* One AES block: @in to @out, four little-endian words each. */
AES_INLINE void aes_encrypt_block(const AES_TAB uint32_t *t,
				  const AES_RK uint32_t *rk,
				  unsigned int rounds, uint32_t out[4],
				  const uint32_t in[4])
{
	const AES_TAB uint32_t *t0 = t, *t1 = t + 256, *t2 = t + 512,
			       *t3 = t + 768;
	uint32_t w0 = in[0] ^ rk[0], w1 = in[1] ^ rk[1];
	uint32_t w2 = in[2] ^ rk[2], w3 = in[3] ^ rk[3];
	uint32_t x0, x1, x2, x3;
	unsigned int r;

	rk += 4;
	for (r = 1; r < rounds; r++, rk += 4) {
		x0 = rk[0] ^ t0[w0 & 0xff] ^ t1[w1 >> 8 & 0xff] ^
		     t2[w2 >> 16 & 0xff] ^ t3[w3 >> 24];
		x1 = rk[1] ^ t0[w1 & 0xff] ^ t1[w2 >> 8 & 0xff] ^
		     t2[w3 >> 16 & 0xff] ^ t3[w0 >> 24];
		x2 = rk[2] ^ t0[w2 & 0xff] ^ t1[w3 >> 8 & 0xff] ^
		     t2[w0 >> 16 & 0xff] ^ t3[w1 >> 24];
		x3 = rk[3] ^ t0[w3 & 0xff] ^ t1[w0 >> 8 & 0xff] ^
		     t2[w1 >> 16 & 0xff] ^ t3[w2 >> 24];
		w0 = x0;
		w1 = x1;
		w2 = x2;
		w3 = x3;
	}

#define AES_SB(v)	(t0[(v) & 0xff] >> 8 & 0xff)
	out[0] = rk[0] ^ AES_SB(w0) ^ AES_SB(w1 >> 8) << 8 ^
		 AES_SB(w2 >> 16) << 16 ^ AES_SB(w3 >> 24) << 24;
	out[1] = rk[1] ^ AES_SB(w1) ^ AES_SB(w2 >> 8) << 8 ^
		 AES_SB(w3 >> 16) << 16 ^ AES_SB(w0 >> 24) << 24;
	out[2] = rk[2] ^ AES_SB(w2) ^ AES_SB(w3 >> 8) << 8 ^
		 AES_SB(w0 >> 16) << 16 ^ AES_SB(w1 >> 24) << 24;
	out[3] = rk[3] ^ AES_SB(w3) ^ AES_SB(w0 >> 8) << 8 ^
		 AES_SB(w1 >> 16) << 16 ^ AES_SB(w2 >> 24) << 24;
#undef AES_SB
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
 * As in BearSSL's ghash_ctmul32, each 32x32 product is two halves of 32 bits:
 * the low one of the reversed operands, the high one the low one of the
 * operands as they are, reversed and shifted down one.  Karatsuba's sums are
 * linear too, so each product goes straight into the words it ends up in,
 * two landings on one word cancelling.  Written out: a table of them is one a
 * compiler may leave in .rodata, which the blob does not carry.
 */
/* Product halves @lo and @hi into @z from word @k up. */
#define GCM_ADD(z, k, lo, hi)	do {				\
	(z)[k] ^= (lo);						\
	(z)[(k) + 1] ^= (hi);					\
} while (0)

/* @a * @b carry-less, in two halves.  Each product is added in and its halves
 * pinned before the next starts, which keeps the registers down.
 */
AES_INLINE void gcm_clmul32(uint32_t a, uint32_t b, uint32_t *lo,
			    uint32_t *hi)
{
	GCM_HERE(a);
	GCM_HERE(b);
	*lo = gcm_bmul32(gcm_rev32(a), gcm_rev32(b));
	*hi = gcm_rev32(gcm_bmul32(a, b)) >> 1;
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
