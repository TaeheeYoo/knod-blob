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

static uint32_t crypto_do(GLOBAL uint32_t *src,
			  GLOBAL const struct knod_crypto_key *k,
			  LDS uint32_t *fr, uint32_t op, uint32_t alen,
			  uint32_t clen)
{
	uint32_t key[8], aad[4], i, lane = lane_id(), in, out, seal, fail;

	seal = op == KNOD_CRYPTO_OP_ENCRYPT;
	in = alen + KNOD_CRYPTO_IV + clen + (seal ? 0 : KNOD_CRYPTO_TAG);
	out = alen + KNOD_CRYPTO_IV + clen + (seal ? KNOD_CRYPTO_TAG : 0);
	for (i = lane; 4 * i < in; i += 64)
		fr[i] = get32(src + i);
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
	for (i = 0; i < 8; i++)
		key[i] = uniform32(get32(&k->key[i]));
	for (i = 0; i < 4; i++)
		aad[i] = 4 * i < alen ? uniform32(fr[i]) : 0;
	fail = gda_chacha_aead(fr, (alen + KNOD_CRYPTO_IV) / 4, alen, key,
			       uniform32(get32(&k->salt)), aad, alen, clen,
			       lane, (GLOBAL uint32_t *)0, seal);
	if (fail)
		return KNOD_CRYPTO_ST_BADTAG;
	for (i = lane; 4 * i < out; i += 64)
		put32(src + i, fr[i]);
	return KNOD_CRYPTO_ST_OK;
}

void cfn_crypto_kernel(args_t a, uint32_t wg, uint32_t lds)
{
	GLOBAL const struct knod_crypto_key *k;
	GLOBAL struct knod_crypto_desc *d;
	uint32_t slot, op, alen, clen, st;
	GLOBAL uint32_t *src;

	a = (args_t)uniform64((uint64_t)a);
	wg = uniform32(wg);
	if (wg >= uniform32(load((gptr)&a->n)))
		return;
	slot = uniform32(load((gptr)uniform64(get64(&a->slots)) + wg));
	d = (GLOBAL struct knod_crypto_desc *)uniform64(get64(&a->descs)) +
	    slot;
	k = (GLOBAL const struct knod_crypto_key *)uniform64(get64(&a->keys)) +
	    uniform32(get32(&d->key));
	src = (GLOBAL uint32_t *)(uniform64(get64(&a->work)) +
				  (uint64_t)slot * uniform32(get32(&a->slot_bytes)));
	op = uniform32(get32(&d->op));
	clen = uniform32(get32(&d->cryptlen));
	alen = uniform32(get32(&d->assoclen)) - KNOD_CRYPTO_IV;

	st = KNOD_CRYPTO_ST_UNSUPPORTED;
	if (uniform32(get32(&k->rounds)) == 0 &&
	    alen <= KNOD_CRYPTO_AAD && !(alen & 3) &&
	    uniform32(get32(&d->authsize)) == KNOD_CRYPTO_TAG &&
	    clen <= KNOD_CRYPTO_LDS_TEXT && op <= KNOD_CRYPTO_OP_DECRYPT)
		st = crypto_do(src, k, (LDS uint32_t *)(uintptr_t)uniform32(lds),
			       op, alen, clen);
	/* The text and tag before the verdict the host goes by. */
	__builtin_amdgcn_fence(__ATOMIC_RELEASE, "agent");
	if (!lane_id())
		store((gptr)&d->status, st);
}
