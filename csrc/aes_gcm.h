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
 * product is reduced.
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

/* Carry-less 32x32 -> 64 multiply, from integer multiplies of operands with
 * every fourth bit kept: no carry crosses into a kept bit.
 */
AES_INLINE uint64_t gcm_bmul32(uint32_t x, uint32_t y)
{
	uint32_t x0 = x & 0x11111111, x1 = x & 0x22222222;
	uint32_t x2 = x & 0x44444444, x3 = x & 0x88888888;
	uint32_t y0 = y & 0x11111111, y1 = y & 0x22222222;
	uint32_t y2 = y & 0x44444444, y3 = y & 0x88888888;
	uint64_t z0, z1, z2, z3;

#define M(a, b)	((uint64_t)(a) * (b))
	z0 = M(x0, y0) ^ M(x1, y3) ^ M(x2, y2) ^ M(x3, y1);
	z1 = M(x0, y1) ^ M(x1, y0) ^ M(x2, y3) ^ M(x3, y2);
	z2 = M(x0, y2) ^ M(x1, y1) ^ M(x2, y0) ^ M(x3, y3);
	z3 = M(x0, y3) ^ M(x1, y2) ^ M(x2, y1) ^ M(x3, y0);
#undef M
	z0 &= 0x1111111111111111ull;
	z1 &= 0x2222222222222222ull;
	z2 &= 0x4444444444444444ull;
	z3 &= 0x8888888888888888ull;
	return z0 | z1 | z2 | z3;
}

/* The 32-bit word with its bits in reverse order. */
AES_INLINE uint32_t gcm_rev32(uint32_t x)
{
	x = (x & 0x55555555) << 1 | (x >> 1 & 0x55555555);
	x = (x & 0x33333333) << 2 | (x >> 2 & 0x33333333);
	x = (x & 0x0f0f0f0f) << 4 | (x >> 4 & 0x0f0f0f0f);
	x = (x & 0x00ff00ff) << 8 | (x >> 8 & 0x00ff00ff);
	return x << 16 | x >> 16;
}

/* Carry-less 64x64 -> 128, Karatsuba over gcm_bmul32(): @a and @b low word
 * first, @z four words low first.
 */
AES_INLINE void gcm_clmul64(uint32_t z[4], uint32_t a0, uint32_t a1,
			    uint32_t b0, uint32_t b1)
{
	uint64_t lo = gcm_bmul32(a0, b0), hi = gcm_bmul32(a1, b1);
	uint64_t mid = gcm_bmul32(a0 ^ a1, b0 ^ b1) ^ lo ^ hi;

	z[0] = (uint32_t)lo;
	z[1] = (uint32_t)(lo >> 32) ^ (uint32_t)mid;
	z[2] = (uint32_t)hi ^ (uint32_t)(mid >> 32);
	z[3] = (uint32_t)(hi >> 32);
}

/*
 * @x * @h in GF(2^128) with GCM's bit order, to @out.  GCM puts x^0 in the
 * top bit of a block's first byte, so a big-endian word bit-reversed holds
 * x^(32i)..x^(32i+31) from its bit 0: in that order it is a plain carry-less
 * product - Karatsuba twice, nine 32x32 multiplies - reduced by
 * x^128 + x^7 + x^2 + x + 1, and reversed back.
 */
AES_INLINE void gcm_mul(uint32_t out[4], const uint32_t x[4],
			const uint32_t h[4])
{
	uint32_t a[4], b[4], l[4], m[4], u[4], z[8], t;
	int i;

	for (i = 0; i < 4; i++) {
		a[i] = gcm_rev32(x[i]);
		b[i] = gcm_rev32(h[i]);
	}

	gcm_clmul64(l, a[0], a[1], b[0], b[1]);
	gcm_clmul64(u, a[2], a[3], b[2], b[3]);
	gcm_clmul64(m, a[0] ^ a[2], a[1] ^ a[3], b[0] ^ b[2], b[1] ^ b[3]);
	for (i = 0; i < 4; i++)
		m[i] ^= l[i] ^ u[i];

	z[0] = l[0];
	z[1] = l[1];
	z[2] = l[2] ^ m[0];
	z[3] = l[3] ^ m[1];
	z[4] = u[0] ^ m[2];
	z[5] = u[1] ^ m[3];
	z[6] = u[2];
	z[7] = u[3];

	/* x^128 = x^7 + x^2 + x + 1: fold the top words down, from the top,
	 * so what one fold carries into a top word is folded in turn.
	 */
	for (i = 7; i >= 4; i--) {
		t = z[i];
		z[i - 4] ^= t ^ t << 1 ^ t << 2 ^ t << 7;
		z[i - 3] ^= t >> 31 ^ t >> 30 ^ t >> 25;
	}

	for (i = 0; i < 4; i++)
		out[i] = gcm_rev32(z[i]);
}

#endif /* KNOD_BLOB_AES_GCM_H */
