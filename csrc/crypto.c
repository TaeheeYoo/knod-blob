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
	    d.authsize == KNOD_CRYPTO_TAG && d.cryptlen <= KNOD_CRYPTO_LDS_TEXT &&
	    d.op <= KNOD_CRYPTO_OP_DECRYPT)
		st = crypto_do(src, &k, (LDS uint32_t *)(uintptr_t)uniform32(lds),
			       d.op, alen, d.cryptlen, t);
	else if (t && !lane_id()) {
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
