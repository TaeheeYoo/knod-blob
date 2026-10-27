/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * ChaCha20 and Poly1305 (RFC 8439) a block at a time, for an IPsec engine
 * that decrypts rfc7539esp(chacha20,poly1305) by the wave: plain C, no
 * tables, the same on the GPU and on the host.
 *
 * ChaCha20 is 32-bit adds, xors and rotates.  Poly1305's numbers mod
 * 2^130 - 5 are five 26-bit limbs; a product's partial sums fit 64 bits.
 */
#ifndef KNOD_BLOB_CHACHA_POLY_H
#define KNOD_BLOB_CHACHA_POLY_H

#include <stdint.h>

#ifndef CP_INLINE
#define CP_INLINE	static inline __attribute__((always_inline))
#endif

CP_INLINE uint32_t cp_rol32(uint32_t v, unsigned int n)
{
	return v << n | v >> (32 - n);
}

#define CP_QR(a, b, c, d)	do {				\
	a += b; d ^= a; d = cp_rol32(d, 16);			\
	c += d; b ^= c; b = cp_rol32(b, 12);			\
	a += b; d ^= a; d = cp_rol32(d, 8);			\
	c += d; b ^= c; b = cp_rol32(b, 7);			\
} while (0)

/* Block @ctr of the keystream for @key (eight words) and @nonce (three),
 * every word as the bytes have it little-endian.
 */
CP_INLINE void chacha20_block(uint32_t out[16], const uint32_t key[8],
			      uint32_t ctr, const uint32_t nonce[3])
{
	uint32_t x[16], i;

	x[0] = 0x61707865;
	x[1] = 0x3320646e;
	x[2] = 0x79622d32;
	x[3] = 0x6b206574;
	for (i = 0; i < 8; i++)
		x[4 + i] = key[i];
	x[12] = ctr;
	x[13] = nonce[0];
	x[14] = nonce[1];
	x[15] = nonce[2];
	for (i = 0; i < 16; i++)
		out[i] = x[i];
	for (i = 0; i < 10; i++) {
		CP_QR(out[0], out[4], out[8], out[12]);
		CP_QR(out[1], out[5], out[9], out[13]);
		CP_QR(out[2], out[6], out[10], out[14]);
		CP_QR(out[3], out[7], out[11], out[15]);
		CP_QR(out[0], out[5], out[10], out[15]);
		CP_QR(out[1], out[6], out[11], out[12]);
		CP_QR(out[2], out[7], out[8], out[13]);
		CP_QR(out[3], out[4], out[9], out[14]);
	}
	for (i = 0; i < 16; i++)
		out[i] += x[i];
}

/* A number mod 2^130 - 5, five 26-bit limbs, low first. */
struct poly {
	uint32_t l[5];
};

/* Sixteen bytes as four little-endian words, and 2^128 above them when
 * @hibit: a message chunk, or with no bit r clamped.
 */
CP_INLINE void poly_from_words(struct poly *a, const uint32_t w[4],
			       uint32_t hibit)
{
	a->l[0] = w[0] & 0x3ffffff;
	a->l[1] = (w[0] >> 26 | w[1] << 6) & 0x3ffffff;
	a->l[2] = (w[1] >> 20 | w[2] << 12) & 0x3ffffff;
	a->l[3] = (w[2] >> 14 | w[3] << 18) & 0x3ffffff;
	a->l[4] = w[3] >> 8 | hibit << 24;
}

/* The key's r: its first sixteen bytes, clamped. */
CP_INLINE void poly_r(struct poly *r, const uint32_t key[4])
{
	uint32_t w[4] = { key[0] & 0x0fffffff, key[1] & 0x0ffffffc,
			  key[2] & 0x0ffffffc, key[3] & 0x0ffffffc };

	poly_from_words(r, w, 0);
}

CP_INLINE void poly_add(struct poly *a, const struct poly *b)
{
	uint32_t i;

	for (i = 0; i < 5; i++)
		a->l[i] += b->l[i];
}

/* Limbs back below 2^26 but for a small carry into the first two, so sums
 * of many stay within 32 bits.
 */
CP_INLINE void poly_carry(struct poly *a)
{
	uint32_t c;

	c = a->l[0] >> 26; a->l[0] &= 0x3ffffff; a->l[1] += c;
	c = a->l[1] >> 26; a->l[1] &= 0x3ffffff; a->l[2] += c;
	c = a->l[2] >> 26; a->l[2] &= 0x3ffffff; a->l[3] += c;
	c = a->l[3] >> 26; a->l[3] &= 0x3ffffff; a->l[4] += c;
	c = a->l[4] >> 26; a->l[4] &= 0x3ffffff; a->l[0] += c * 5;
	c = a->l[0] >> 26; a->l[0] &= 0x3ffffff; a->l[1] += c;
}

/*
 * @a * @b mod 2^130 - 5, partly reduced: limbs below 2^26 but for a small
 * carry into the first two.  Takes limbs up to 2^27 or so, as a sum of a
 * few partly reduced numbers has.
 */
CP_INLINE void poly_mul(struct poly *o, const struct poly *a,
			const struct poly *b)
{
	uint64_t d0, d1, d2, d3, d4, c;
	uint32_t a0 = a->l[0], a1 = a->l[1], a2 = a->l[2], a3 = a->l[3];
	uint32_t a4 = a->l[4];
	uint32_t b0 = b->l[0], b1 = b->l[1], b2 = b->l[2], b3 = b->l[3];
	uint32_t b4 = b->l[4];
	uint32_t s1 = b1 * 5, s2 = b2 * 5, s3 = b3 * 5, s4 = b4 * 5;

	d0 = (uint64_t)a0 * b0 + (uint64_t)a1 * s4 + (uint64_t)a2 * s3 +
	     (uint64_t)a3 * s2 + (uint64_t)a4 * s1;
	d1 = (uint64_t)a0 * b1 + (uint64_t)a1 * b0 + (uint64_t)a2 * s4 +
	     (uint64_t)a3 * s3 + (uint64_t)a4 * s2;
	d2 = (uint64_t)a0 * b2 + (uint64_t)a1 * b1 + (uint64_t)a2 * b0 +
	     (uint64_t)a3 * s4 + (uint64_t)a4 * s3;
	d3 = (uint64_t)a0 * b3 + (uint64_t)a1 * b2 + (uint64_t)a2 * b1 +
	     (uint64_t)a3 * b0 + (uint64_t)a4 * s4;
	d4 = (uint64_t)a0 * b4 + (uint64_t)a1 * b3 + (uint64_t)a2 * b2 +
	     (uint64_t)a3 * b1 + (uint64_t)a4 * b0;

	c = d0 >> 26; o->l[0] = (uint32_t)d0 & 0x3ffffff; d1 += c;
	c = d1 >> 26; o->l[1] = (uint32_t)d1 & 0x3ffffff; d2 += c;
	c = d2 >> 26; o->l[2] = (uint32_t)d2 & 0x3ffffff; d3 += c;
	c = d3 >> 26; o->l[3] = (uint32_t)d3 & 0x3ffffff; d4 += c;
	c = d4 >> 26; o->l[4] = (uint32_t)d4 & 0x3ffffff;
	o->l[0] += (uint32_t)c * 5;
	c = o->l[0] >> 26; o->l[0] &= 0x3ffffff;
	o->l[1] += (uint32_t)c;
}

/* (@a mod 2^130 - 5 + @s) mod 2^128, the tag as four little-endian words. */
CP_INLINE void poly_tag(uint32_t tag[4], const struct poly *a,
			const uint32_t s[4])
{
	uint32_t h0 = a->l[0], h1 = a->l[1], h2 = a->l[2], h3 = a->l[3];
	uint32_t h4 = a->l[4], g0, g1, g2, g3, g4, c, mask;
	uint64_t f;

	/* Carry fully, twice round for what the first pass wraps. */
	c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
	c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
	c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
	c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
	c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
	c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
	c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
	c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
	c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
	c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

	/* h - p if h >= p. */
	g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
	g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
	g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
	g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
	g4 = h4 + c - (1u << 26);
	mask = (g4 >> 31) - 1;
	h0 = (h0 & ~mask) | (g0 & mask);
	h1 = (h1 & ~mask) | (g1 & mask);
	h2 = (h2 & ~mask) | (g2 & mask);
	h3 = (h3 & ~mask) | (g3 & mask);
	h4 = (h4 & ~mask) | (g4 & mask);

	h0 = h0 | h1 << 26;
	h1 = h1 >> 6 | h2 << 20;
	h2 = h2 >> 12 | h3 << 14;
	h3 = h3 >> 18 | h4 << 8;

	f = (uint64_t)h0 + s[0]; tag[0] = (uint32_t)f;
	f = (uint64_t)h1 + s[1] + (f >> 32); tag[1] = (uint32_t)f;
	f = (uint64_t)h2 + s[2] + (f >> 32); tag[2] = (uint32_t)f;
	f = (uint64_t)h3 + s[3] + (f >> 32); tag[3] = (uint32_t)f;
}

#endif /* KNOD_BLOB_CHACHA_POLY_H */
