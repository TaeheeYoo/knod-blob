// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ChaCha20-Poly1305 (rfc7539esp) on a wave's copy of a payload in LDS, a
 * lane per 64-byte block: what the GDA IPsec engine and knod_crypto's
 * kernel both seal and open with.
 */
#ifndef KNOD_CHACHA_LDS_H
#define KNOD_CHACHA_LDS_H

#include "chacha_poly.h"

/*
 * Where an IPsec engine's time goes, built with -DKNOD_IPSEC_PROF: into its
 * queue's regress_dbg, ticks of the GPU's 100 MHz clock, summed by lane 0 of
 * each wave.  /srv/share/bin/ipsec_prof.sh reads them.
 */
#define GDA_PROF_PKTS		0
#define GDA_PROF_FETCH		1
#define GDA_PROF_POLY		2
#define GDA_PROF_DECRYPT	3
#define GDA_PROF_IPSEC		4	/* gda_ipsec() whole */
#define GDA_PROF_END_CALLS	5
#define GDA_PROF_END		6	/* gda_round_end() whole */
#define GDA_PROF_BEGIN		7	/* gda_round_begin(), idle too */

static INLINE uint32_t gda_cyc(void)
{
	return (uint32_t)__builtin_readsteadycounter();
}

static INLINE void gda_prof(GLOBAL uint32_t *dbg, int i, uint32_t from)
{
#ifdef KNOD_IPSEC_PROF
	if (!lane_id())
		add((gptr)&dbg[i], gda_cyc() - from);
#endif
}

/* Four bytes from byte @b of an LDS copy, wherever they start. */
static INLINE uint32_t gda_lds32u(LDS const uint32_t *fr, uint32_t b)
{
	uint32_t a = b >> 2;

	return b & 3 ? __builtin_amdgcn_alignbyte(fr[a + 1], fr[a], b & 3) :
		       fr[a];
}

/* ds_swizzle's pattern for the lane @x away in each 32: and 0x1f, or 0. */
#define GDA_SWIZZLE_XOR(x)	((x) << 10 | 0x1f)

/* @h times @r plus the lane @pattern names's, then @r squared. */
#define gda_poly_tree(h, r, pattern)	do {				\
	uint32_t __i, __o[5];						\
									\
	for (__i = 0; __i < 5; __i++)					\
		__o[__i] = __builtin_amdgcn_ds_swizzle((h)->l[__i],	\
						       pattern);	\
	poly_mul(h, h, r);						\
	for (__i = 0; __i < 5; __i++)					\
		(h)->l[__i] += __o[__i];				\
	poly_carry(h);							\
	poly_mul(r, r, r);						\
} while (0)

/*
 * ChaCha20-Poly1305 (rfc7539esp) for gda_ipsec_rx(), on the frame's copy in
 * LDS: @fr, its payload the words from @pw.  @key is the SA's, the nonce its
 * salt and the packet's IV.  Poly1305's one-time key is block 0, so its
 * powers of r are the wave's to make: a lane takes every 64th sixteen bytes
 * the tag runs over, by Horner with r^64, times the power of r its last is
 * from the end, and the lanes' sums are added up.  Then a lane decrypts a
 * 64-byte block of the copy, which goes to the host from there.
 */
static INLINE void gda_chacha_xor(LDS uint32_t *fr, uint32_t pw,
				  const uint32_t key[8],
				  const uint32_t nonce[3], uint32_t clen,
				  uint32_t lane)
{
	uint32_t blk[16], i, b, k;

	/* Block 1 + k of the keystream over the copy's payload, a lane every
	 * 64th: past four kilobytes a lane takes more than one.
	 */
#pragma clang loop unroll(disable)
	for (k = lane; 64 * k < clen; k += 64) {
		chacha20_block(blk, key, 1 + k, nonce);
		for (i = 0; i < 16; i++) {
			b = 64 * k + 4 * i;
			if (b < clen)
				fr[pw + b / 4] ^= b + 4 <= clen ? blk[i] :
					blk[i] & ((1u << (8 * (clen - b))) - 1);
		}
	}
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
}

/*
 * Open the payload in @fr, or with @seal, seal it: the keystream over it
 * after the tag is checked, or before the tag is made and put past it.
 * The tag covers @alen bytes of AAD first, up to 16, @aad's words zero past
 * them.  Whether the tag failed.
 */
static INLINE uint32_t gda_chacha_aead(LDS uint32_t *fr, uint32_t pw,
				       uint32_t iv,
				       const uint32_t key[8], uint32_t salt,
				       const uint32_t aad[4], uint32_t alen,
				       uint32_t clen, uint32_t lane,
				       GLOBAL uint32_t *dbg, int seal)
{
	uint32_t nonce[3], blk[16], s[4], w[4], tag[4], t = gda_cyc();
	uint32_t n = (clen + 15) / 16, ab = alen ? 1 : 0, nb = n + 1 + ab;
	uint32_t i, k, kk, pad, c, fail, b;
	struct poly r, cur, h, p, cv;

	nonce[0] = salt;
	nonce[1] = uniform32(gda_lds32u(fr, iv));
	nonce[2] = uniform32(gda_lds32u(fr, iv + 4));

	chacha20_block(blk, key, 0, nonce);
	poly_r(&r, blk);
	for (i = 0; i < 4; i++)
		s[i] = blk[4 + i];
	if (seal)
		gda_chacha_xor(fr, pw, key, nonce, clen, lane);

	/* r^64, for each lane's Horner. */
	cur = r;
	for (i = 0; i < 6; i++)
		poly_mul(&cur, &cur, &cur);

	/*
	 * AAD first, the ciphertext, the lengths last, after as many zero
	 * pieces as give every lane the same number: lane l takes positions
	 * l + 64k.
	 */
	kk = (nb + 63) / 64;
	pad = 64 * kk - nb;
	for (i = 0; i < 5; i++)
		h.l[i] = 0;
#pragma clang loop unroll(disable)
	for (k = lane; k < 64 * kk; k += 64) {
		c = k - pad;
		for (i = 0; i < 4; i++) {
			b = 16 * (c - ab) + 4 * i;
			w[i] = ab && !c ? (i == 0 ? aad[0] : i == 1 ? aad[1] :
					   i == 2 ? aad[2] : aad[3]) :
			       c == nb - 1 ? (i == 0 ? alen : i == 2 ? clen : 0) :
			       b >= clen ? 0 :
			       b + 4 <= clen ? fr[pw + b / 4] :
			       fr[pw + b / 4] & ((1u << (8 * (clen - b))) - 1);
		}
		poly_from_words(&cv, w, k >= pad);
		if (k < pad)
			for (i = 0; i < 5; i++)
				cv.l[i] = 0;
		poly_mul(&h, &h, &cur);
		poly_add(&h, &cv);
		poly_carry(&h);
	}

	/* The lanes' sums, each times r^(63 - lane): pairs, each lower one
	 * times r^j and its partner's added, j = 1 .. 16 in each half, then
	 * the halves.  Times r once more, the last piece's power being 1.
	 */
	cur = r;
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(1));
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(2));
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(4));
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(8));
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(16));
	for (i = 0; i < 5; i++)
		p.l[i] = __builtin_amdgcn_readlane(h.l[i], 32);
	poly_mul(&h, &h, &cur);
	for (i = 0; i < 5; i++)
		h.l[i] = __builtin_amdgcn_readlane(h.l[i], 0) + p.l[i];
	poly_carry(&h);
	poly_mul(&h, &h, &r);
	poly_tag(tag, &h, s);
	if (seal) {
		/* A byte a lane: the tag need not start on a word. */
		w[0] = lane < 4 ? tag[0] : lane < 8 ? tag[1] :
		       lane < 12 ? tag[2] : tag[3];
		if (lane < 16)
			((LDS uint8_t *)fr)[4 * pw + clen + lane] =
				w[0] >> (8 * (lane & 3));
		__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
		return 0;
	}
	fail = 0;
	for (i = 0; i < 4; i++)
		fail |= tag[i] != gda_lds32u(fr, 4 * pw + clen + 4 * i);
	gda_prof(dbg, GDA_PROF_POLY, t);
	t = gda_cyc();

	gda_chacha_xor(fr, pw, key, nonce, clen, lane);

	gda_prof(dbg, GDA_PROF_DECRYPT, t);
	return uniform32(fail);
}

/* gda_chacha_aead() on an ESP packet's: its SPI and sequence the AAD. */
static INLINE uint32_t gda_chacha_rx(LDS uint32_t *fr, uint32_t pw,
				     uint32_t iv,
				     const uint32_t key[8], uint32_t salt,
				     uint32_t spi, uint32_t seq, uint32_t clen,
				     uint32_t lane, GLOBAL uint32_t *dbg,
				     int seal)
{
	const uint32_t aad[4] = { spi, seq, 0, 0 };

	return gda_chacha_aead(fr, pw, iv, key, salt, aad, 8, clen, lane, dbg,
			       seal);
}

#endif /* KNOD_CHACHA_LDS_H */
