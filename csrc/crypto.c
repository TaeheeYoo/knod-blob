// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * knod_crypto's kernel: workgroup @wg of a batch takes the request in work
 * slot slots[wg], as include/uapi/linux/knod_crypto.h says, and seals or
 * opens it in a copy in LDS at @lds.  ChaCha20-Poly1305 (rfc7539esp) with
 * up to 16 bytes of AAD in whole words and up to KNOD_CRYPTO_LDS_TEXT bytes
 * of text, a 64-byte block a lane; anything else goes back unsupported, for
 * the host's fallback.
 */
#include "gda.h"
#include "chacha_lds.h"
#include <linux/knod_crypto.h>

#define KNOD_CRYPTO_AAD		16	/* at most */
#define KNOD_CRYPTO_IV		8
#define KNOD_CRYPTO_TAG		16
#define KNOD_CRYPTO_LDS_TEXT	(64 * 64)
/* AAD, IV, text and tag, and the word past them gda_lds32u() reads. */
_Static_assert(KNOD_CRYPTO_AAD + KNOD_CRYPTO_IV + KNOD_CRYPTO_LDS_TEXT +
	       KNOD_CRYPTO_TAG + 4 <= KNOD_CRYPTO_LDS_BYTES, "the LDS copy");

typedef GLOBAL const struct knod_crypto_args *args_t;
_Static_assert(KNOD_CRYPTO_SCRATCH_BYTES >= KNOD_BLOB_CALL_STACK_BYTES,
	       "the call stack in the scratch the host gives");
_Static_assert(__builtin_offsetof(struct knod_crypto_args, flags) ==
	       KNOD_CRYPTO_ARGS_FLAGS_OFF, "the entry reads the flags there");

/* Debug: the time now into one of @t's fields, if there is @t. */
static void crypto_time(GLOBAL struct knod_crypto_time *t,
			GLOBAL uint32_t *field)
{
	if (t && !lane_id())
		put32(field, gda_cyc());
}

/* Sixteen bytes a lane, a kilobyte a round: the request at most. */
#define CRYPTO_ROUNDS	5
_Static_assert(CRYPTO_ROUNDS * 1024 >= KNOD_CRYPTO_AAD + KNOD_CRYPTO_IV +
	       KNOD_CRYPTO_LDS_TEXT + KNOD_CRYPTO_TAG, "the copy's rounds");
_Static_assert(CRYPTO_ROUNDS * 1024 <= KNOD_CRYPTO_LDS_BYTES + 1024,
	       "the copy's last round in LDS");

static uint32_t crypto_do(GLOBAL uint32_t *src,
			  const struct knod_crypto_key *k,
			  LDS uint32_t *fr, uint32_t op, uint32_t alen,
			  uint32_t clen, GLOBAL struct knod_crypto_time *t)
{
	uint32_t aad[4], i, j, lane = lane_id(), in, out, seal, fail;
	v4u r[CRYPTO_ROUNDS];

	seal = op == KNOD_CRYPTO_OP_ENCRYPT;
	in = alen + KNOD_CRYPTO_IV + clen + (seal ? 0 : KNOD_CRYPTO_TAG);
	out = alen + KNOD_CRYPTO_IV + clen + (seal ? KNOD_CRYPTO_TAG : 0);
	/* Every round's loads out before the first is waited for. */
#pragma unroll
	for (j = 0; j < CRYPTO_ROUNDS; j++) {
		i = lane + 64 * j;
		if (16 * i < in)
			r[j] = ((GLOBAL const v4u *)src)[i];
	}
#pragma unroll
	for (j = 0; j < CRYPTO_ROUNDS; j++) {
		i = lane + 64 * j;
		if (16 * i < in)
			((LDS v4u *)fr)[i] = r[j];
	}
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
	crypto_time(t, &t->fetched);
	for (i = 0; i < 4; i++)
		aad[i] = 4 * i < alen ? uniform32(fr[i]) : 0;
	fail = gda_chacha_aead(fr, (alen + KNOD_CRYPTO_IV) / 4, alen, k->key,
			       k->salt, aad, alen, clen, lane,
			       (GLOBAL uint32_t *)0, seal);
	crypto_time(t, &t->sealed);
	if (fail)
		return KNOD_CRYPTO_ST_BADTAG;
#pragma unroll
	for (j = 0; j < CRYPTO_ROUNDS; j++) {
		i = lane + 64 * j;
		if (16 * i < out)
			((GLOBAL v4u *)src)[i] = ((LDS const v4u *)fr)[i];
	}
	return KNOD_CRYPTO_ST_OK;
}

/* The unit the wave runs on: HW_ID1, gfx10.3 and gfx11 alike. */
static uint32_t crypto_hw_id(void)
{
	return __builtin_amdgcn_s_getreg(23 | 31 << 11);
}

/* Block @bl's sixteen words at @p, as many as are within @clen. */
static void crypto_big_load(GLOBAL const uint32_t *p, uint32_t bl,
			    uint32_t clen, uint32_t d[16])
{
	uint32_t i;

	for (i = 0; i < 16; i++)
		if (64 * bl + 4 * i < clen)
			d[i] = p[16 * bl + i];
}

/*
 * The keystream over @clen bytes at @p, a lane a 64-byte block at a time:
 * the next round's block loaded before this one's keystream is made, so
 * the load is out while it is.
 */
static void crypto_big_xor(GLOBAL uint32_t *p, const uint32_t key[8],
			   const uint32_t nonce[3], uint32_t clen,
			   uint32_t lane)
{
	uint32_t blk[16], d[16], cur[16], i, b, bl;

	if (64 * lane < clen)
		crypto_big_load(p, lane, clen, d);
	for (bl = lane; 64 * bl < clen; bl += 64) {
		for (i = 0; i < 16; i++)
			cur[i] = d[i];
		if (64 * (bl + 64) < clen)
			crypto_big_load(p, bl + 64, clen, d);
		chacha20_block(blk, key, 1 + bl, nonce);
		for (i = 0; i < 16; i++) {
			b = 64 * bl + 4 * i;
			if (b < clen)
				p[b / 4] = cur[i] ^ (b + 4 <= clen ? blk[i] :
					blk[i] & ((1u << (8 * (clen - b))) - 1));
		}
	}
	/* Every lane's words out before the others read them. */
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "workgroup");
}

/*
 * The four words Poly1305 takes at position @c of what it runs over: the
 * AAD's block if there is one, the text's sixteen bytes, zero past them,
 * or the lengths last.
 */
static void crypto_big_words(GLOBAL const uint32_t *p, const uint32_t aad[4],
			     uint32_t ab, uint32_t alen, uint32_t clen,
			     uint32_t nb, uint32_t c, uint32_t w[4])
{
	uint32_t i, b;

	for (i = 0; i < 4; i++) {
		b = 16 * (c - ab) + 4 * i;
		w[i] = ab && !c ? (i == 0 ? aad[0] : i == 1 ? aad[1] :
				   i == 2 ? aad[2] : aad[3]) :
		       c == nb - 1 ? (i == 0 ? alen : i == 2 ? clen : 0) :
		       b >= clen ? 0 :
		       b + 4 <= clen ? p[b / 4] :
		       p[b / 4] & ((1u << (8 * (clen - b))) - 1);
	}
}

/*
 * A request past the LDS copy's reach, sealed or opened where it is in its
 * slots: gda_chacha_aead() over memory instead of LDS.  Whether the tag
 * failed.
 */
static uint32_t crypto_big(GLOBAL uint32_t *src,
			   const struct knod_crypto_key *k, uint32_t seal,
			   uint32_t alen, uint32_t clen)
{
	GLOBAL uint32_t *p = src + (alen + KNOD_CRYPTO_IV) / 4;
	uint32_t n = (clen + 15) / 16, ab = alen ? 1 : 0, nb = n + 1 + ab;
	uint32_t nonce[3], blk[16], s[4], w[4], cw[4], tag[4], aad[4];
	uint32_t lane = lane_id(), i, j, kk, pad, x;
	GLOBAL uint8_t *pb = (GLOBAL uint8_t *)p;
	struct poly r, cur, h, q, cv;

	nonce[0] = k->salt;
	nonce[1] = uniform32(src[alen / 4]);
	nonce[2] = uniform32(src[alen / 4 + 1]);
	for (i = 0; i < 4; i++)
		aad[i] = 4 * i < alen ? uniform32(src[i]) : 0;
	chacha20_block(blk, k->key, 0, nonce);
	poly_r(&r, blk);
	for (i = 0; i < 4; i++)
		s[i] = blk[4 + i];
	if (seal)
		crypto_big_xor(p, k->key, nonce, clen, lane);

	cur = r;
	for (i = 0; i < 6; i++)
		poly_mul(&cur, &cur, &cur);
	kk = (nb + 63) / 64;
	pad = 64 * kk - nb;
	for (i = 0; i < 5; i++)
		h.l[i] = 0;
	/* The next position's words loaded before this one's are taken in. */
	crypto_big_words(p, aad, ab, alen, clen, nb, lane - pad, w);
#pragma clang loop unroll(disable)
	for (j = lane; j < 64 * kk; j += 64) {
		for (i = 0; i < 4; i++)
			cw[i] = w[i];
		if (j + 64 < 64 * kk)
			crypto_big_words(p, aad, ab, alen, clen, nb,
					 j + 64 - pad, w);
		poly_from_words(&cv, cw, j >= pad);
		if (j < pad)
			for (i = 0; i < 5; i++)
				cv.l[i] = 0;
		poly_mul(&h, &h, &cur);
		poly_add(&h, &cv);
		poly_carry(&h);
	}
	cur = r;
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(1));
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(2));
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(4));
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(8));
	gda_poly_tree(&h, &cur, GDA_SWIZZLE_XOR(16));
	for (i = 0; i < 5; i++)
		q.l[i] = __builtin_amdgcn_readlane(h.l[i], 32);
	poly_mul(&h, &h, &cur);
	for (i = 0; i < 5; i++)
		h.l[i] = __builtin_amdgcn_readlane(h.l[i], 0) + q.l[i];
	poly_carry(&h);
	poly_mul(&h, &h, &r);
	poly_tag(tag, &h, s);

	/* A byte a lane: the tag need not start on a word. */
	x = lane < 4 ? tag[0] : lane < 8 ? tag[1] : lane < 12 ? tag[2] : tag[3];
	x = x >> (8 * (lane & 3)) & 0xff;
	if (seal) {
		if (lane < 16)
			pb[clen + lane] = x;
		return 0;
	}
	if (__builtin_amdgcn_ballot_w64(lane < 16 && pb[clen + lane] != x))
		return 1;
	crypto_big_xor(p, k->key, nonce, clen, lane);
	return 0;
}

/*
 * The arguments, the batch's slot list, the descriptor and the key are the
 * host's and do not change while the kernel runs: scalar loads, a few in
 * a chain, each a whole structure, instead of a load a field.
 */
void cfn_crypto_kernel(args_t a, uint32_t wg, uint32_t lds)
{
	uint32_t slot, alen, st, t0 = gda_cyc(), tl;
	GLOBAL struct knod_crypto_time *t;
	struct knod_crypto_args ar;
	struct knod_crypto_desc d;
	struct knod_crypto_key k;
	GLOBAL uint32_t *src;

	ar = *(CONSTANT const struct knod_crypto_args *)uniform64((uint64_t)a);
	wg = uniform32(wg);
	if (wg >= ar.n)
		return;
	t = (GLOBAL struct knod_crypto_time *)ar.times;
	if (t)
		t += wg;
	slot = ((CONSTANT const uint32_t *)ar.slots)[wg];
	d = ((CONSTANT const struct knod_crypto_desc *)ar.descs)[slot];
	k = ((CONSTANT const struct knod_crypto_key *)ar.keys)[d.key];
	src = (GLOBAL uint32_t *)(ar.work + (uint64_t)slot * ar.slot_bytes);
	alen = d.assoclen - KNOD_CRYPTO_IV;

	tl = gda_cyc();
	if (t && !lane_id())
		put32(&t->loaded, tl);
	st = KNOD_CRYPTO_ST_UNSUPPORTED;
	if (!k.rounds && alen <= KNOD_CRYPTO_AAD && !(alen & 3) &&
	    d.authsize == KNOD_CRYPTO_TAG && d.op <= KNOD_CRYPTO_OP_DECRYPT &&
	    d.cryptlen <= KNOD_CRYPTO_LDS_TEXT)
		st = crypto_do(src, &k, (LDS uint32_t *)(uintptr_t)uniform32(lds),
			       d.op, alen, d.cryptlen, t);
	else if (!k.rounds && alen <= KNOD_CRYPTO_AAD && !(alen & 3) &&
		 d.authsize == KNOD_CRYPTO_TAG &&
		 d.op <= KNOD_CRYPTO_OP_DECRYPT &&
		 d.cryptlen <= KNOD_CRYPTO_TEXT_MAX) {
		if (t && !lane_id())
			put32(&t->fetched, tl);
		st = crypto_big(src, &k, d.op == KNOD_CRYPTO_OP_ENCRYPT, alen,
				d.cryptlen) ? KNOD_CRYPTO_ST_BADTAG :
					      KNOD_CRYPTO_ST_OK;
		crypto_time(t, &t->sealed);
	} else if (t && !lane_id()) {
		put32(&t->fetched, tl);
		put32(&t->sealed, tl);
	}
	/* The text and tag before the verdict the host goes by. */
	__builtin_amdgcn_fence(__ATOMIC_RELEASE, "agent");
	if (!lane_id())
		store((gptr)&((GLOBAL struct knod_crypto_desc *)ar.descs)[slot].status,
		      st);
	if (t && !lane_id()) {
		put32(&t->start, t0);
		put32(&t->end, gda_cyc());
		put32(&t->hw_id, crypto_hw_id());
	}
}
