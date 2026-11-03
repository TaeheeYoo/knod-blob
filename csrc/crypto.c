// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * knod_crypto's kernel: workgroup @wg of a batch takes the request in work
 * slot slots[wg], as include/uapi/linux/knod_crypto.h says.  For now it
 * seals and opens nothing: every request goes back unsupported, for the
 * host's fallback.
 */
#include "gda.h"
#include <linux/knod_crypto.h>

typedef GLOBAL const struct knod_crypto_args *args_t;

void cfn_crypto_kernel(args_t a, uint32_t wg)
{
	GLOBAL struct knod_crypto_desc *d;
	uint32_t slot;

	a = (args_t)uniform64((uint64_t)a);
	wg = uniform32(wg);
	if (wg >= uniform32(load((gptr)&a->n)))
		return;
	slot = uniform32(load((gptr)uniform64(get64(&a->slots)) + wg));
	d = (GLOBAL struct knod_crypto_desc *)uniform64(get64(&a->descs)) +
	    slot;
	if (!lane_id())
		store((gptr)&d->status, KNOD_CRYPTO_ST_UNSUPPORTED);
}
