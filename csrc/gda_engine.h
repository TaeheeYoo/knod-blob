/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The GDA engine: a queue's rings, run on the GPU.
 *
 * One workgroup per queue, and up to KNOD_PERSIST_GDA_WAVES_MAX of its waves
 * take packets, 64 completions each a round: wave w the ones at ci + 64w.
 * Wave 0 alone keeps the rings - the RQ and CQ producers and consumers, the
 * SQ, the TX completions, the control block - and the waves meet in a few
 * bytes of LDS past the program's stack, four barriers a round.
 *
 * RQ entry i always holds RX page i.  The NIC consumes the RQ in order and
 * completes the SQ in order, so no page ever needs to move between entries:
 * an entry whose packet went out on the SQ is held until the NIC says it was
 * sent, and the RQ producer stops at the oldest such entry.
 *
 * What a NIC's rings look like is its nic_*.h's: a gda_<format>.c includes
 * one, names the functions with GDA_FN() and includes this, and the result is
 * that format's engine.  src/gda_engine.S calls gda_init() once, then every
 * round gda_round_begin(), the program and gda_round_end().  What lasts from
 * one call to the next is in three VGPRs it keeps across the program: the
 * lanes of one hold the ring state, one value each (GL_*), and the other two
 * the packet count.  Each function takes them and hands them back.  They are
 * read a lane at a time where needed rather than unpacked, which would want
 * more scalars at once than a function gets without a stack.
 *
 * The hooks a nic_*.h has:
 *   NIC_SQ_SPARE	SQ entries kept empty
 *   struct nic_rx	what nic_rx_poll() leaves nic_rx_read()
 *   nic_load()		its state lanes, from the queue's entry
 *   nic_rx_post()	post every RQ entry, fresh rings
 *   nic_rx_poll()	whether the lane's completion is done
 *   nic_rx_read()	its length, flow and page, and lanes out of step
 *   nic_rx_csum()	what the NIC found of a passed packet's checksums
 *   nic_rx_release()	wave 0: CQ and RQ entries back to the NIC
 *   nic_tx_reap()	what the NIC sent, off the TX CQ
 *   nic_tx_post()	one packet's send descriptor
 *   nic_tx_ring()	wave 0: the SQ's doorbell
 */
#ifndef GDA_FN
# error "a gda_<format>.c names the functions"
#endif

/* What outlives the shader, and what the host reads, to the queue's entry. */
static INLINE void gda_save(struct gda *g)
{
	entry_t e = g->entry;

	if (lane_id())
		return;
	put32(&e->ci, GL(g, CI));
	put64(&e->packets, g->packets);
	put64(&e->rounds, GL64(g, ROUNDS));
	put128(&e->tx_packets,
	       (v4u){ GL(g, TX_PKTS), __builtin_amdgcn_readlane(g->s, GL_TX_PKTS + 1),
		      GL(g, TX_FULL), __builtin_amdgcn_readlane(g->s, GL_TX_FULL + 1) });
	put128(&e->sq_pc, (v4u){ GL(g, SQ_PC), GL(g, SQ_CC), GL(g, TX_CI),
				 GL(g, TX_GEN) });
	put32(&e->rq_bound, GL(g, LAST_BOUND));
	put32(&e->tx_dead, GL(g, TX_DEAD));
	put64(&e->pass_full, GL64(g, PASS_FULL));
}

/* Wave 0 leaving: the others are told at the barrier they wait at, then where
 * the rings got to goes to the entry for the next shader.
 */
static INLINE __attribute__((noreturn)) void gda_stop(struct gda *g)
{
	if (!lane_id())
		g->meet->cmd = 1;
	barrier();
	gda_save(g);
	stores_out();
	__builtin_amdgcn_endpgm();
}

static INLINE void gda_regress_dbg(struct gda *g, uint32_t bound, uint32_t prev,
			    uint32_t pass_cc, uint32_t cand)
{
	GLOBAL uint32_t *d = g->entry->regress_dbg;

	if (lane_id())
		return;
	put32(&d[KNOD_PERSIST_REGRESS_DELTA], bound - prev);
	add(&d[KNOD_PERSIST_REGRESS_HITS], 1);
	put32(&d[KNOD_PERSIST_REGRESS_S2], GL(g, CI));
	put32(&d[KNOD_PERSIST_REGRESS_BOUND], bound);
	put32(&d[KNOD_PERSIST_REGRESS_SQ_PC], GL(g, SQ_PC));
	put32(&d[KNOD_PERSIST_REGRESS_SQ_CC], GL(g, SQ_CC));
	put32(&d[KNOD_PERSIST_REGRESS_PASS_CC], pass_cc);
	put32(&d[KNOD_PERSIST_REGRESS_CAND], cand);
}

/*
 * Hand the CQ entries back up to CI, and the RQ entries up to the oldest one
 * whose packet is still waiting to be sent.  What passed to the host was
 * copied out in its round.
 */
static INLINE void gda_rq_release(struct gda *g)
{
	uint32_t ci = GL(g, CI), pc = GL(g, SQ_PC), cc = GL(g, SQ_CC);
	uint32_t bound = ci, prev;

	if (pc != cc)
		bound = get32(g->ring + KNOD_PERSIST_RING_RQPOS_OFF +
			      4 * (cc & GL(g, SQ_MASK)));

	/* Debug: the bound only ever moves on; count one that goes back. */
	prev = GL(g, LAST_BOUND);
	SET(g, LAST_BOUND, bound);
	if ((int32_t)(bound - prev) < 0)
		gda_regress_dbg(g, bound, prev, 0, 0);

	nic_rx_release(g, ci, bound);
}

/* Wave 0: park while the host changes maps, or leave if it wants the shader
 * gone.  The other waves wait at the next barrier either way.
 */
static INLINE void gda_park_or_stop(struct gda *g)
{
	GLOBAL struct knod_persistent_control *ctl = g->ctl;
	uint32_t pause;

	if (get32(&ctl->version) != KNOD_PERSIST_VERSION || get32(&ctl->stop))
		gda_stop(g);
	pause = get32(&ctl->pause);
	if (!pause)
		return;

	/* Where the rings got to, then that this queue is parked: the host may
	 * read anything once it sees the ack.
	 */
	gda_save(g);
	stores_out();
	if (!lane_id())
		put32(&g->entry->pause_ack, pause);
	stores_out();
	do {
		__builtin_amdgcn_s_sleep(8);
		if (get32(&ctl->version) != KNOD_PERSIST_VERSION ||
		    get32(&ctl->stop))
			gda_stop(g);
	} while (get32(&ctl->pause));
}

/* Post every RQ entry: a round only ever gives an entry back as it was.  Then
 * this build of the rings is posted.
 */
static INLINE void gda_post_rq(struct gda *g)
{
	entry_t e = g->entry;

	nic_rx_post(g);
	if (lane_id())
		return;
	put64(&e->ci, (uint64_t)get32(&e->gen) << 32);
	put32(&e->rq_bound, 0);
	stores_out();
}

#ifdef GDA_IPSEC
/* The T-tables' copies the engine makes and reads: LDS has room for
 * KNOD_PERSIST_GDA_IPSEC_TTAB_COPIES of the four; -DGDA_TTAB_COPIES=n to
 * measure others against, and -DGDA_TTAB_T0 for four times as many of T0
 * alone, the others its rotations.
 */
#ifdef GDA_TTAB_T0
#define AES_TAB_T0
#define GDA_TTAB_WORDS	256
#else
#define GDA_TTAB_WORDS	1024
#endif
#ifndef GDA_TTAB_COPIES
#define GDA_TTAB_COPIES	KNOD_PERSIST_GDA_IPSEC_TTAB_COPIES
#endif
_Static_assert(4 * GDA_TTAB_WORDS * GDA_TTAB_COPIES <=
	       KNOD_PERSIST_GDA_IPSEC_TTAB_LDS_BYTES &&
	       !(GDA_TTAB_COPIES & (GDA_TTAB_COPIES - 1)),
	       "the T-tables' copies: a power of two LDS has room for");
#define AES_TAB		LDS
#define AES_TAB_STRIDE	GDA_TTAB_COPIES
#define AES_RK		CONSTANT
#define AES_INLINE	static INLINE
#include "aes_gcm.h"
#include "chacha_poly.h"

/* An ESP packet's offsets in its frame: Ethernet, an IPv4 header with no
 * options, the SPI and sequence, the IV; the ICV after the payload.
 */
#define ESP_SPI		34
#define ESP_IV		42
#define ESP_DATA	50
#define ESP_ICV		16

/* The AES T-tables' copies in LDS, past where the waves meet. */
static INLINE LDS uint32_t *gda_ipsec_ttab(struct gda *g)
{
	return (LDS uint32_t *)(uintptr_t)
		(uniform32(GL(g, LDS)) + KNOD_PERSIST_GDA_LDS_BYTES);
}

/* The lane's copy of them. */
static INLINE LDS const uint32_t *gda_aes_tab(struct gda *g)
{
	return gda_ipsec_ttab(g) + (lane_id() & (GDA_TTAB_COPIES - 1));
}

/* Every wave's share of the tables' copies into LDS, once for the shader. */
static INLINE void gda_ipsec_load(struct gda *g, ctl_t ctl)
{
	GLOBAL const uint32_t *src =
		(GLOBAL const uint32_t *)get64(&ctl->ipsec_ttab);
	LDS uint32_t *t = gda_ipsec_ttab(g);
	uint32_t i;

	SET64(g, IPSEC_SA, get64(&ctl->ipsec_sa));
	SET(g, IPSEC_SA_MASK, get32(&ctl->ipsec_sa_mask));
	for (i = 64 * g->wave + lane_id();
	     i < GDA_TTAB_WORDS * GDA_TTAB_COPIES; i += 64 * GL(g, WAVES))
		t[i] = src[i / GDA_TTAB_COPIES];
}

/* Four bytes of a packet at any offset, as a little-endian word: gfx10 drops
 * an unaligned dword address's low bits, so two aligned ones and a shift.
 */
static INLINE uint32_t gda_ld32u(GLOBAL const uint8_t *p)
{
	uintptr_t a = (uintptr_t)p;
	GLOBAL const uint32_t *w = (GLOBAL const uint32_t *)(a & ~(uintptr_t)3);
	uint32_t lo = get32(w), hi = get32(w + 1);

	return a & 3 ? __builtin_amdgcn_alignbyte(hi, lo, a & 3) : lo;
}

static INLINE uint32_t gda_be16(GLOBAL const uint8_t *p)
{
	uint32_t w = gda_ld32u(p);

	return (w & 0xff) << 8 | (w >> 8 & 0xff);
}

/* @x * @h into @z unreduced, @h a power of H from the SA's table. */
static INLINE void gda_ghash_add(uint32_t z[8], const uint32_t x[4],
				 GLOBAL const uint8_t *hpow, uint32_t power)
{
	v4u v = *(GLOBAL const v4u *)(hpow + 16 * (power - 1));
	uint32_t h[4] = { aes_bswap32(v.x), aes_bswap32(v.y),
			  aes_bswap32(v.z), aes_bswap32(v.w) };

	gcm_mul_add(z, x, h);
}

#include "chacha_lds.h"

/* The wave's copy of the frame it is on. */
static INLINE LDS uint32_t *gda_ipsec_frame(struct gda *g)
{
	return (LDS uint32_t *)(uintptr_t)
		(uniform32(GL(g, LDS)) + KNOD_PERSIST_GDA_LDS_BYTES +
		 KNOD_PERSIST_GDA_IPSEC_TTAB_LDS_BYTES +
		 KNOD_PERSIST_GDA_IPSEC_FRAME_BYTES * g->wave);
}

/* The round's PASS packets, which the waves take one at a time. */
struct gda_ipsec_work {
	uint32_t next;		/* the next one to take */
	uint32_t pad[3];
	uint32_t page[256];	/* by slot from the round's first */
	uint32_t offlen[256];	/* off | len << 16 */
	uint32_t meta[256];	/* KNOD_PERSIST_PASS_IPSEC* bits */
};
_Static_assert(sizeof(struct gda_ipsec_work) <=
	       KNOD_PERSIST_GDA_IPSEC_WORK_BYTES, "the IPsec work area");

static INLINE LDS struct gda_ipsec_work *gda_ipsec_work(struct gda *g)
{
	return (LDS struct gda_ipsec_work *)(uintptr_t)
		(uniform32(GL(g, LDS)) + KNOD_PERSIST_GDA_LDS_BYTES +
		 KNOD_PERSIST_GDA_IPSEC_TTAB_LDS_BYTES +
		 4 * KNOD_PERSIST_GDA_IPSEC_FRAME_BYTES);
}

static INLINE uint32_t gda_lds_be16(LDS const uint32_t *fr, uint32_t b)
{
	return bswap32(gda_lds32u(fr, b)) >> 16;
}

/*
 * Four bytes of what goes out at @raw's place, @q bytes into the payload the
 * wave's window holds, which has @lim of them: the plaintext where it covers
 * them, @raw, the frame's own bytes, where it does not.
 */

/* The SA of an ESP packet's SPI, or -1. */
static INLINE int gda_ipsec_sa(struct gda *g, uint32_t spi)
{
	GLOBAL const struct knod_persistent_ipsec_sa *t =
		(GLOBAL const struct knod_persistent_ipsec_sa *)GL64(g, IPSEC_SA);
	uint32_t mask = GL(g, IPSEC_SA_MASK), h, i, k;

	if (!t)
		return -1;
	h = KNOD_PERSIST_IPSEC_SA_HASH(spi);
	for (i = 0; i < KNOD_PERSIST_IPSEC_SA_PROBE; i++) {
		k = (h + i) & mask;
		if (get32(&t[k].spi) == spi && get32(&t[k].active))
			return k;
	}
	return -1;
}

/*
 * The page's bytes from @src - 4 + @sh into the wave's frame copy, its word
 * i the four from there plus 4i, through @end bytes of it: a lane sixteen
 * bytes and the word past them, two kilobytes' loads out before each wait:
 * four would want more registers than the call has.
 */
static INLINE void gda_ipsec_fetch(LDS uint32_t *fr, GLOBAL const uint8_t *src,
				   uint32_t sh, uint32_t end, uint32_t lane)
{
	GLOBAL const uint32_t *w = (GLOBAL const uint32_t *)(src - 4);
	uint32_t q, i, j, hi, nx[2];
	v4u v[2];

	for (q = lane; 16 * q < end; q += 128) {
#pragma unroll
		for (j = 0; j < 2; j++)
			if (16 * (q + 64 * j) < end) {
				v[j] = get128(w + 4 * (q + 64 * j));
				nx[j] = get32(w + 4 * (q + 64 * j) + 4);
			}
#pragma unroll
		for (j = 0; j < 2; j++)
			if (16 * (q + 64 * j) < end) {
				uint32_t o[4];

				for (i = 0; i < 4; i++) {
					hi = i < 3 ? v[j][i + 1] : nx[j];
					o[i] = sh ? __builtin_amdgcn_alignbyte(
							hi, v[j][i], sh) :
						    v[j][i];
				}
				*(LDS v4u *)(fr + 4 * (q + 64 * j)) =
					(v4u){ o[0], o[1], o[2], o[3] };
			}
	}
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
}

/*
 * The SA the round's last ESP packet had, which the next most likely has
 * too: what decrypting with it takes, from the table and its keys' BO.  SAs
 * come and go between rounds, the queues parked, so a round may keep it.
 */
struct gda_sa_cache {
	uint32_t spi, slot, rounds, salt;	/* slot ~0 for none */
	uint64_t rk, hpow;
	uint32_t key[8];			/* ChaCha20's */
};

/* Every lane's sum of words added up and folded to sixteen bits. */
static INLINE uint32_t gda_wave_fold(uint64_t sum)
{
	uint64_t all = 0;
	uint32_t i;

	/* readlane gives an int: unsigned before it widens. */
	for (i = 0; i < 64; i++)
		all += (uint32_t)__builtin_amdgcn_readlane((uint32_t)sum, i) +
		       ((uint64_t)(uint32_t)__builtin_amdgcn_readlane(
				(uint32_t)(sum >> 32), i) << 32);
	while (all >> 16)
		all = (all & 0xffff) + (all >> 16);
	return all;
}

/* Whether every lane's sum of words, folded, is a good checksum. */
static INLINE uint32_t gda_wave_csum_ok(uint64_t sum)
{
	return gda_wave_fold(sum) == 0xffff;
}

/*
 * Whether the inner TCP or UDP checksum of a tunnel-mode ESP packet the
 * wave decrypted in its LDS copy is good: the plaintext from byte @pt, its
 * @clen bytes an inner IPv4 packet, padding and the trailer.  Every lane
 * sums its words of the segment, the pseudo-header the inner IP header's,
 * and the lanes' sums are added up.  Anything else - a fragment, options
 * past what is there, another protocol - is not found good, for the stack
 * to check.
 */
static INLINE uint32_t gda_ipsec_inner_csum(LDS const uint32_t *fr,
					    uint32_t pt, uint32_t clen)
{
	uint32_t lane = lane_id(), vihl, ihl, tot, proto, l4, n, i, b, w;
	uint64_t sum = 0;

	if (clen < 2 || (gda_lds32u(fr, pt + clen - 2) >> 8 & 0xff) != 4)
		return 0;		/* next header: not IPv4 in IPv4 */
	vihl = gda_lds32u(fr, pt) & 0xff;
	ihl = 4 * (vihl & 15);
	tot = gda_lds_be16(fr, pt + 2);
	proto = gda_lds32u(fr, pt + 8) >> 8 & 0xff;
	if ((vihl >> 4) != 4 || ihl < 20 || tot < ihl + 8 || tot > clen ||
	    (gda_lds_be16(fr, pt + 6) & 0x3fff) ||
	    (proto != 6 && proto != 17))
		return 0;
	l4 = tot - ihl;
	/* A UDP packet with no checksum has none to check. */
	if (proto == 17 && !(gda_lds32u(fr, pt + ihl + 4) >> 16))
		return 1;
	n = (l4 + 3) / 4;
	for (i = lane; i < n; i += 64) {
		b = 4 * i;
		w = gda_lds32u(fr, pt + ihl + b);
		if (b + 4 > l4)
			w &= (1u << (8 * (l4 - b))) - 1;
		sum += w;
	}
	/* The pseudo-header, in memory order as the rest: addresses, then a
	 * zero byte, the protocol and the segment's length.
	 */
	if (!lane)
		sum += (uint64_t)gda_lds32u(fr, pt + 12) +
		       gda_lds32u(fr, pt + 16) +
		       (proto << 8 | (l4 & 0xff) << 24 | (l4 >> 8) << 16);
	return gda_wave_csum_ok(sum);
}

/*
 * The counter blocks over the @clen bytes of payload in @fr at word @pw,
 * block j with counter j + 1: a lane two blocks at a time, 64 apart.
 */
static INLINE void gda_gcm_ctr(LDS uint32_t *fr, uint32_t pw,
			       LDS const uint32_t *t,
			       const CONSTANT uint32_t *rk, uint32_t rounds,
			       uint32_t salt, uint32_t iv_lo, uint32_t iv_hi,
			       uint32_t clen)
{
	uint32_t n = (clen + 15) / 16, x[8], k[8], j, h, b, i;

#pragma clang loop unroll(disable)
	for (j = 1 + lane_id(); j <= n; j += 128) {
		gcm_counter(x, salt, iv_lo, iv_hi, j + 1);
		gcm_counter(x + 4, salt, iv_lo, iv_hi, j + 65);
		aes_encrypt_block2(t, rk, rounds, k, x);
		for (h = 0; h < 2; h++)
			for (i = 0; i < 4; i++) {
				b = 16 * (j + 64 * h - 1) + 4 * i;
				if (b < clen)
					fr[pw + b / 4] ^= b + 4 <= clen ?
						k[4 * h + i] :
						k[4 * h + i] &
						((1u << (8 * (clen - b))) - 1);
			}
	}
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
}

/*
 * AES-GCM opening the payload in @fr at word @pw, as gda_gcm_seal() seals
 * it: GHASH over the ciphertext, a lane every 64th block, the AAD - SPI and
 * sequence - and the lengths, the lanes' sums added up and the tag checked
 * against the one past the payload; then the counter blocks over it, a lane
 * a block.  Whether the tag failed.
 */
static INLINE uint32_t gda_gcm_open(struct gda *g, LDS uint32_t *fr,
				    uint32_t pw, uint32_t iv,
				    const CONSTANT uint32_t *rk, uint32_t rounds,
				    uint32_t salt, GLOBAL const uint8_t *hpow,
				    uint32_t spi, uint32_t seq, uint32_t clen,
				    GLOBAL uint32_t *dbg)
{
	LDS const uint32_t *t = gda_aes_tab(g);
	uint32_t t0 = gda_cyc();
	uint32_t n = (clen + 15) / 16, m = n + 2, lane = lane_id();
	uint32_t iv_lo = uniform32(gda_lds32u(fr, iv));
	uint32_t iv_hi = uniform32(gda_lds32u(fr, iv + 4));
	uint32_t z[8] = { 0 }, acc[4], x[4], k[4], j, b, i, w, fail = 0;

	GCM_HERE(lane);
#pragma clang loop unroll(disable)
	for (j = 1 + lane; j <= n; j += 64) {
		GCM_HERE(j);
		for (i = 0; i < 4; i++) {
			b = 16 * (j - 1) + 4 * i;
			w = fr[pw + b / 4];
			x[i] = aes_bswap32(b >= clen ? 0 : b + 4 <= clen ? w :
					   w & ((1u << (8 * (clen - b))) - 1));
		}
		gda_ghash_add(z, x, hpow, m - j);
	}
	/* The AAD, SPI and sequence, at the furthest power; the lengths in
	 * bits at the nearest.
	 */
	if (lane < 2) {
		x[0] = lane ? 0 : aes_bswap32(spi);
		x[1] = lane ? 64 : aes_bswap32(seq);
		x[2] = 0;
		x[3] = lane ? 8 * clen : 0;
		gda_ghash_add(z, x, hpow, lane ? 1 : m);
	}

	/* Every lane's sum: each half's in each of its lanes, then the two. */
	gcm_reduce(acc, z);
#pragma unroll
	for (w = 0; w < 4; w++) {
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(16));
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(8));
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(4));
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(2));
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(1));
		acc[w] = __builtin_amdgcn_readlane(acc[w], 0) ^
			 __builtin_amdgcn_readlane(acc[w], 32);
	}
	/* The tag: the sum, masked with the counter's first block. */
	gcm_counter(x, salt, iv_lo, iv_hi, 1);
	aes_encrypt_block(t, rk, rounds, k, x);
	gcm_to_ghash(x, k);
#pragma unroll
	for (i = 0; i < 4; i++)
		fail |= (acc[i] ^ x[i]) !=
			aes_bswap32(gda_lds32u(fr, 4 * pw + clen + 4 * i));
	gda_prof(dbg, GDA_PROF_POLY, t0);
	/* A packet the tag fails is dropped: nothing to decrypt. */
	if (uniform32(fail))
		return 1;
	t0 = gda_cyc();

	gda_gcm_ctr(fr, pw, t, rk, rounds, salt, iv_lo, iv_hi, clen);
	gda_prof(dbg, GDA_PROF_DECRYPT, t0);
	return 0;
}

/*
 * A passed packet the wave is on, decrypted in its LDS copy if it is ESP of
 * an SA in the table: @src at the sixteen bytes it starts in, the packet @off
 * past them and @len long.  The frame comes into LDS in one go, ChaCha20-
 * Poly1305 or AES-GCM opens its payload there, and the copy is what goes
 * out; everything but the payload stays as it came - the stack's crypto
 * offload takes the ESP header and trailer off.  Returns its PASS entry's
 * KNOD_PERSIST_PASS_IPSEC* bits, zero for one left alone; *@in_lds whether
 * the copy is there to go out.
 */
static INLINE uint32_t gda_ipsec_rx(struct gda *g, GLOBAL uint8_t *src,
				    uint32_t off, uint32_t len,
				    struct gda_sa_cache *sc, uint32_t *in_lds)
{
	GLOBAL const struct knod_persistent_ipsec_sa *sa;
	const CONSTANT uint32_t *rk;
	GLOBAL const uint8_t *hpow;
	uint32_t tot, clen, n, m, rounds, salt, spi, seq, i, fail;
	uint32_t lane = lane_id();
	LDS uint32_t *fr = gda_ipsec_frame(g);
	uint32_t sh = (off + ESP_DATA) & 3, fo = off + 4 - sh;
	GLOBAL uint32_t *dbg = g->entry->regress_dbg;
	uint32_t t0 = gda_cyc();
	int slot;

	/* The frame into LDS in one go, its payload on a word: copy byte fo
	 * is the frame's first.  From then on the copy is what goes out.
	 */
	*in_lds = 0;
	if (len < ESP_DATA + ESP_ICV ||
	    fo + len + 4 + 16 > KNOD_PERSIST_GDA_IPSEC_FRAME_BYTES)
		return 0;
	gda_ipsec_fetch(fr, src, sh, fo + len + 4, lane);
	gda_prof(dbg, GDA_PROF_FETCH, t0);
	*in_lds = 1;
	if (gda_lds_be16(fr, fo + 12) != 0x0800 ||
	    (gda_lds32u(fr, fo + 14) & 0xff) != 0x45 ||
	    (gda_lds_be16(fr, fo + 20) & 0x3fff) ||
	    (gda_lds32u(fr, fo + 20) >> 24) != 50)
		return 0;
	tot = uniform32(gda_lds_be16(fr, fo + 16));
	if (tot < ESP_DATA - 14 + ESP_ICV || 14 + tot > len)
		return 0;
	clen = 14 + tot - ESP_DATA - ESP_ICV;
	n = (clen + 15) / 16;
	m = n + 2;
	spi = uniform32(gda_lds32u(fr, fo + ESP_SPI));
	if (sc->slot == ~0u || sc->spi != spi) {
		slot = (int)uniform32(gda_ipsec_sa(g, spi));
		if (slot < 0)
			return 0;
		sa = (GLOBAL const struct knod_persistent_ipsec_sa *)
		     GL64(g, IPSEC_SA) + slot;
		sc->spi = spi;
		sc->slot = slot;
		sc->rounds = uniform32(get32(&sa->rounds));
		sc->salt = uniform32(get32(&sa->salt));
		sc->rk = uniform64(get64(&sa->rk));
		sc->hpow = uniform64(get64(&sa->hpow));
		if (!sc->rounds)
			for (i = 0; i < 8; i++)
				sc->key[i] = uniform32(
					((const CONSTANT uint32_t *)sc->rk)[i]);
	}
	slot = sc->slot;
	rounds = sc->rounds;
	salt = sc->salt;
	rk = (const CONSTANT uint32_t *)sc->rk;

	if (!rounds) {
		seq = uniform32(gda_lds32u(fr, fo + ESP_SPI + 4));
		fail = gda_chacha_rx(fr, (fo + ESP_DATA) / 4,
				     fo + ESP_IV, sc->key, salt, spi, seq, clen,
				     lane, dbg, 0);
#ifdef KNOD_IPSEC_PROF
		if (!lane)
			add((gptr)&dbg[GDA_PROF_PKTS], 1);
#endif
		/* The stack need not sum the plaintext again: it is here. */
		return KNOD_PERSIST_PASS_IPSEC |
		       slot << KNOD_PERSIST_PASS_IPSEC_SA_SHIFT |
		       (fail ? KNOD_PERSIST_PASS_IPSEC_FAIL :
			uniform32(gda_ipsec_inner_csum(fr, fo + ESP_DATA,
						       clen)) ?
			KNOD_PERSIST_PASS_IPSEC_CSUM : 0);
	}
	/* GHASH's powers of H: the SA's table has so many. */
	if (m > KNOD_PERSIST_IPSEC_HPOW)
		return 0;
	hpow = (GLOBAL const uint8_t *)sc->hpow;
	seq = uniform32(gda_lds32u(fr, fo + ESP_SPI + 4));
	fail = gda_gcm_open(g, fr, (fo + ESP_DATA) / 4, fo + ESP_IV, rk, rounds,
			    salt, hpow, spi, seq, clen, dbg);
#ifdef KNOD_IPSEC_PROF
	if (!lane)
		add((gptr)&dbg[GDA_PROF_PKTS], 1);
#endif
	return KNOD_PERSIST_PASS_IPSEC | slot << KNOD_PERSIST_PASS_IPSEC_SA_SHIFT |
	       (fail ? KNOD_PERSIST_PASS_IPSEC_FAIL :
		uniform32(gda_ipsec_inner_csum(fr, fo + ESP_DATA, clen)) ?
		KNOD_PERSIST_PASS_IPSEC_CSUM : 0);
}

static INLINE void gda_ipsec_out(GLOBAL uint8_t *dst, LDS const uint32_t *fr,
				 uint32_t off, uint32_t n);

/*
 * A round's PASS packet, decrypted in LDS if it is ESP of an SA, out to
 * @dst: @src the 16 bytes it starts in, the packet @off past them and @len
 * long, @copy bytes from there to go; one gda_ipsec_rx() may not open,
 * @open zero, goes as it is.  Its PASS entry's KNOD_PERSIST_PASS_IPSEC*
 * bits.
 */
static INLINE uint32_t gda_ipsec_job(struct gda *g, struct gda_sa_cache *sc,
				     GLOBAL uint8_t *src, GLOBAL uint8_t *dst,
				     uint32_t off, uint32_t len, uint32_t copy,
				     uint32_t open)
{
	uint32_t b = 0, in_lds = 0, i;

	if (open)
		b = gda_ipsec_rx(g, src, off, len, sc, &in_lds);
	if (in_lds)
		gda_ipsec_out(dst, gda_ipsec_frame(g), off, copy);
	else
		/* Rare here: a kilobyte at a time keeps the registers for
		 * the decrypting.
		 */
		for (i = 16 * lane_id(); i < copy; i += 1024)
			put128(dst + i, get128(src + i));
	return b;
}

typedef GLOBAL struct knod_persistent_gda_jobs *jobs_t;

/* One of a round's jobs from table @jt, its number in *@k: whether there
 * was one left.  What the queue wrote of it is there to read after.
 */
static INLINE uint32_t gda_job_take(jobs_t jt, uint32_t *k)
{
	GLOBAL uint64_t *p = (GLOBAL uint64_t *)&jt->state;
	uint32_t got = 0, n = 0;
	uint64_t st;

	if (!lane_id()) {
		st = __scoped_atomic_load_n(p, __ATOMIC_RELAXED,
					    __MEMORY_SCOPE_DEVICE);
		while ((st >> 16 & 0xffff) > (st & 0xffff)) {
			if (__scoped_atomic_compare_exchange_n(
				    p, &st, st + 1, 0, __ATOMIC_ACQUIRE,
				    __ATOMIC_RELAXED, __MEMORY_SCOPE_DEVICE)) {
				got = 1;
				n = st & 0xffff;
				break;
			}
		}
	}
	*k = __builtin_amdgcn_readfirstlane(n);
	return __builtin_amdgcn_readfirstlane(got);
}

/* Job @k of table @jt done by this wave: its result out, then counted. */
static INLINE void gda_job_run(struct gda *g, struct gda_sa_cache *sc,
			       jobs_t jt, uint32_t k)
{
	GLOBAL struct knod_persistent_gda_job *jb = &jt->job[k];
	uint32_t ol = uniform32(get32(&jb->offlen)), b;

	b = gda_ipsec_job(g, sc, (GLOBAL uint8_t *)uniform64(get64(&jb->src)),
			  (GLOBAL uint8_t *)uniform64(get64(&jb->dst)),
			  ol & 0xffff, ol >> 16, uniform32(get32(&jb->copy)),
			  uniform32(get32(&jb->open)));
	if (!lane_id())
		store((gptr)&jb->meta, b);
	stores_out();
	if (!lane_id())
		add((gptr)&jt->done, 1);
}

/*
 * The crypto pool's workgroup @j: a wave at a time, jobs from whichever
 * queue has some, from the queue after the last one's on, until the shader
 * stops.  Wave 0 parks the workgroup with the queues, between jobs.
 */
static INLINE __attribute__((noreturn)) void gda_pool_run(ctl_t ctl,
							   uint32_t j,
							   uint32_t wave)
{
	entry_t e = &ctl->gda[KNOD_PERSIST_MAX_QUEUES + j];
	uint32_t nq = get32(&ctl->gda_queues), waves = get32(&ctl->gda_waves);
	struct gda_sa_cache sc = { .slot = ~0u };
	struct gda g = { .s = 0 };
	uint32_t q = j, i, k, any, cmd, pause;
	jobs_t jt;

	if (!get32(&e->live) || wave >= waves)
		__builtin_amdgcn_endpgm();
	g.ctl = ctl;
	g.entry = e;
	g.queue = KNOD_PERSIST_MAX_QUEUES + j;
	g.wave = wave;
	SET(&g, LDS, get32(&ctl->gda_lds));
	SET(&g, WAVES, waves);
	g.meet = (LDS struct gda_meet *)(uintptr_t)GL(&g, LDS);
	gda_ipsec_load(&g, ctl);
	barrier();

	for (;;) {
		any = 0;
		for (i = 0; i < nq; i++) {
			q = q + 1 < nq ? q + 1 : 0;
			jt = (jobs_t)uniform64(get64(&ctl->gda[q].jobs));
			if (jt && gda_job_take(jt, &k)) {
				gda_job_run(&g, &sc, jt, k);
				any = 1;
				break;
			}
		}

		/* Between jobs, every wave's: stop or park with the queues. */
		barrier();
		if (!wave && !lane_id()) {
			cmd = get32(&ctl->version) != KNOD_PERSIST_VERSION ||
			      get32(&ctl->stop);
			pause = get32(&ctl->pause);
			if (!cmd && pause) {
				put32(&e->pause_ack, pause);
				do {
					__builtin_amdgcn_s_sleep(8);
					cmd = get32(&ctl->stop);
				} while (!cmd && get32(&ctl->pause));
				cmd = cmd ? 1 : 2;
			}
			g.meet->cmd = cmd;
		}
		barrier();
		cmd = uniform32(g.meet->cmd);
		if (cmd == 1) {
			stores_out();
			__builtin_amdgcn_endpgm();
		}
		/* Parked: SAs may have come and gone, keys and all. */
		if (cmd == 2)
			sc.slot = ~0u;
		/* Nothing anywhere: a microsecond or so, against a round's
		 * tens of them, before the queues are read again.
		 */
		if (!any)
			__builtin_amdgcn_s_sleep(32);
	}
}
#endif

/*
 * @n bytes from @src to @dst, sixteen a lane: every lane's loads out first,
 * up to four kilobytes of them, and one wait before the stores, not a wait
 * a kilobyte.
 */
static INLINE void gda_copy(GLOBAL uint8_t *dst, GLOBAL const uint8_t *src,
			    uint32_t n)
{
	uint32_t b, i;
	v4u r[4];

	for (b = 16 * lane_id(); b < n; b += 4096) {
#pragma unroll
		for (i = 0; i < 4; i++)
			if (b + 1024 * i < n)
				r[i] = get128(src + b + 1024 * i);
#pragma unroll
		for (i = 0; i < 4; i++)
			if (b + 1024 * i < n)
				put128(dst + b + 1024 * i, r[i]);
	}
}

/* Whether the host has frames waiting to go. */
static INLINE uint32_t gda_htx_pending(struct gda *g)
{
	if (!GL64(g, HTX) || GL(g, TX_DEAD))
		return 0;
	return get32(&g->entry->htx_pc) != get32(&g->entry->htx_cc);
}

#ifdef GDA_IPSEC
static INLINE void gda_ipsec_out(GLOBAL uint8_t *dst, LDS const uint32_t *fr,
				 uint32_t off, uint32_t n);

/*
 * AES-GCM over the payload in @fr at word @pw: the counter blocks over it, a
 * lane a block, then GHASH over what that made and the tag past it.
 */
static INLINE void gda_gcm_seal(struct gda *g, LDS uint32_t *fr, uint32_t pw,
				uint32_t iv, const CONSTANT uint32_t *rk,
				uint32_t rounds, uint32_t salt,
				GLOBAL const uint8_t *hpow, uint32_t spi,
				uint32_t seq, uint32_t clen)
{
	LDS const uint32_t *t = gda_aes_tab(g);
	uint32_t n = (clen + 15) / 16, m = n + 2, lane = lane_id();
	uint32_t iv_lo = uniform32(gda_lds32u(fr, iv));
	uint32_t iv_hi = uniform32(gda_lds32u(fr, iv + 4));
	uint32_t z[8] = { 0 }, acc[4], x[4], k[4], j, b, i, w;

	GCM_HERE(lane);
	gda_gcm_ctr(fr, pw, t, rk, rounds, salt, iv_lo, iv_hi, clen);

#pragma clang loop unroll(disable)
	for (j = 1 + lane; j <= n; j += 64) {
		GCM_HERE(j);
		for (i = 0; i < 4; i++) {
			b = 16 * (j - 1) + 4 * i;
			w = fr[pw + b / 4];
			x[i] = aes_bswap32(b >= clen ? 0 : b + 4 <= clen ? w :
					   w & ((1u << (8 * (clen - b))) - 1));
		}
		gda_ghash_add(z, x, hpow, m - j);
	}
	/* The AAD, SPI and sequence, at the furthest power; the lengths in
	 * bits at the nearest.
	 */
	if (lane < 2) {
		x[0] = lane ? 0 : aes_bswap32(spi);
		x[1] = lane ? 64 : aes_bswap32(seq);
		x[2] = 0;
		x[3] = lane ? 8 * clen : 0;
		gda_ghash_add(z, x, hpow, lane ? 1 : m);
	}

	gcm_reduce(acc, z);
#pragma unroll
	for (w = 0; w < 4; w++) {
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(16));
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(8));
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(4));
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(2));
		acc[w] ^= __builtin_amdgcn_ds_swizzle(acc[w], GDA_SWIZZLE_XOR(1));
		acc[w] = __builtin_amdgcn_readlane(acc[w], 0) ^
			 __builtin_amdgcn_readlane(acc[w], 32);
	}
	gcm_counter(x, salt, iv_lo, iv_hi, 1);
	aes_encrypt_block(t, rk, rounds, k, x);
	gcm_to_ghash(x, k);
	/* A byte a lane, back in wire order: the tag need not start on a
	 * word.
	 */
	w = lane < 4 ? acc[0] ^ x[0] : lane < 8 ? acc[1] ^ x[1] :
	    lane < 12 ? acc[2] ^ x[2] : acc[3] ^ x[3];
	if (lane < 16)
		((LDS uint8_t *)fr)[4 * pw + clen + lane] =
			aes_bswap32(w) >> (8 * (lane & 3));
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
}

static INLINE uint32_t gda_lds8(LDS const uint32_t *fr, uint32_t b)
{
	return gda_lds32u(fr, b) & 0xff;
}

static INLINE void gda_lds_put8(LDS uint32_t *fr, uint32_t b, uint32_t v)
{
	((LDS uint8_t *)fr)[b] = v;
}

static INLINE void gda_lds_put_be16(LDS uint32_t *fr, uint32_t b, uint32_t v)
{
	gda_lds_put8(fr, b, v >> 8);
	gda_lds_put8(fr, b + 1, v);
}

static INLINE uint32_t gda_lds_be32(LDS const uint32_t *fr, uint32_t b)
{
	return bswap32(gda_lds32u(fr, b));
}

static INLINE void gda_lds_put_be32(LDS uint32_t *fr, uint32_t b, uint32_t v)
{
	gda_lds_put_be16(fr, b, v >> 16);
	gda_lds_put_be16(fr, b + 2, v);
}

/* The checksum of the IPv4 header at byte @b, by one lane. */
static INLINE void gda_lds_ip_csum(LDS uint32_t *fr, uint32_t b)
{
	uint32_t ihl = 4 * (gda_lds8(fr, b) & 15), i, v;
	uint64_t sum = 0;

	gda_lds_put_be16(fr, b + 10, 0);
	for (i = 0; i < ihl; i += 4)
		sum += gda_lds32u(fr, b + i);
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	v = ~sum & 0xffff;
	gda_lds_put8(fr, b + 10, v);
	gda_lds_put8(fr, b + 11, v >> 8);
}

/*
 * What a NIC does to a host frame before it seals it, by entry @e and its
 * second word @x, in the wave's copy @fr of it, @fo bytes in, @len bytes
 * long with the trailer and the ICV: a GSO segment's headers made its own
 * and its trailer written; the inner TCP or UDP checksum filled in.
 * Whether the frame is one it can do that to.
 */
static INLINE uint32_t gda_htx_prep(LDS uint32_t *fr, uint32_t fo,
				    uint32_t len, uint64_t e, uint64_t x)
{
	uint32_t next = KNOD_PERSIST_HTX_NEXT(x), l4 = KNOD_PERSIST_HTX_L4(x);
	uint32_t seg = KNOD_PERSIST_HTX_SEG(x), pad = KNOD_PERSIST_HTX_PAD(x);
	uint32_t lane = lane_id(), ip, proto, clen, end, n, i, b, w, c, v;
	uint32_t gso = (e & KNOD_PERSIST_HTX_GSO) != 0;
	uint64_t sum = 0;

	if (len < ESP_DATA + 2 + ESP_ICV)
		return 0;
	clen = len - ESP_DATA - ESP_ICV;
	if (!gso)
		pad = gda_lds8(fr, fo + ESP_DATA + clen - 2);
	if (pad + 2 > clen)
		return 0;
	/* The plaintext's end: the trailer's start. */
	end = ESP_DATA + clen - 2 - pad;
	ip = next == 4 ? ESP_DATA : 14;
	if (l4 < ip + 20 || l4 + 8 > end)
		return 0;
	proto = next == 4 ? gda_lds8(fr, fo + ip + 9) : next;

	if (gso && !lane) {
		uint32_t lo, hi, f;

		for (i = 0; i < pad; i++)
			gda_lds_put8(fr, fo + end + i, i + 1);
		gda_lds_put8(fr, fo + end + pad, pad);
		gda_lds_put8(fr, fo + end + pad + 1, next);
		gda_lds_put_be16(fr, fo + 16, len - 14);
		gda_lds_put_be16(fr, fo + 18, gda_lds_be16(fr, fo + 18) + seg);
		gda_lds_ip_csum(fr, fo + 14);
		gda_lds_put_be32(fr, fo + ESP_SPI + 4,
				 gda_lds_be32(fr, fo + ESP_SPI + 4) + seg);
		/* The IV is the 64-bit sequence. */
		hi = gda_lds_be32(fr, fo + ESP_IV);
		lo = gda_lds_be32(fr, fo + ESP_IV + 4);
		hi += lo + seg < lo;
		gda_lds_put_be32(fr, fo + ESP_IV, hi);
		gda_lds_put_be32(fr, fo + ESP_IV + 4, lo + seg);
		if (next == 4) {
			gda_lds_put_be16(fr, fo + ip + 2, end - ip);
			gda_lds_put_be16(fr, fo + ip + 4,
					 gda_lds_be16(fr, fo + ip + 4) + seg);
			gda_lds_ip_csum(fr, fo + ip);
		}
		if (proto == 6) {
			gda_lds_put_be32(fr, fo + l4 + 4,
					 gda_lds_be32(fr, fo + l4 + 4) +
					 seg * KNOD_PERSIST_HTX_MSS(x));
			/* FIN and PSH on the last segment only, CWR on the
			 * first.
			 */
			f = gda_lds8(fr, fo + l4 + 13);
			if (!(e & KNOD_PERSIST_HTX_LAST))
				f &= ~0x09u;
			if (seg)
				f &= ~0x80u;
			gda_lds_put8(fr, fo + l4 + 13, f);
		}
	}
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");

	if (!(e & KNOD_PERSIST_HTX_CSUM) || (proto != 6 && proto != 17))
		return 1;
	c = l4 + (proto == 6 ? 16 : 6);
	if (!lane)
		gda_lds_put_be16(fr, fo + c, 0);
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
	/* As gda_ipsec_inner_csum() checks it: words in memory order, the
	 * pseudo-header's too.
	 */
	n = end - l4;
	for (i = lane; 4 * i < n; i += 64) {
		b = 4 * i;
		w = gda_lds32u(fr, fo + l4 + b);
		if (b + 4 > n)
			w &= (1u << (8 * (n - b))) - 1;
		sum += w;
	}
	if (!lane)
		sum += (uint64_t)gda_lds32u(fr, fo + ip + 12) +
		       gda_lds32u(fr, fo + ip + 16) +
		       (proto << 8 | (n & 0xff) << 24 | (n >> 8) << 16);
	v = ~gda_wave_fold(sum) & 0xffff;
	if (proto == 17 && !v)
		v = 0xffff;
	if (!lane) {
		gda_lds_put8(fr, fo + c, v);
		gda_lds_put8(fr, fo + c + 1, v >> 8);
	}
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
	return 1;
}

/*
 * A host frame of @len bytes at @src, ESP for SA @slot, sealed on its way to
 * @dst with AES-GCM if @gcm, else ChaCha20-Poly1305: whether it was.  Entry
 * @e and its second word @x say what is to be done to it first.  One the
 * engine cannot seal is not sent at all.
 */
static INLINE int gda_htx_seal(struct gda *g, GLOBAL uint8_t *dst,
			       GLOBAL const uint8_t *src, uint32_t len,
			       uint32_t slot, int gcm, uint64_t e, uint64_t x)
{
	GLOBAL const struct knod_persistent_ipsec_sa *sa;
	LDS uint32_t *fr = gda_ipsec_frame(g);
	uint32_t fo = 4 - (ESP_DATA & 3), key[8], tot, clen, spi, seq, i;
	uint32_t rounds, salt;
	const CONSTANT uint32_t *rk;

	if (len < ESP_DATA + ESP_ICV ||
	    fo + len + 4 + 16 > KNOD_PERSIST_GDA_IPSEC_FRAME_BYTES)
		return 0;
	sa = (GLOBAL const struct knod_persistent_ipsec_sa *)
	     GL64(g, IPSEC_SA) + slot;
	if (!uniform32(get32(&sa->active)))
		return 0;
	gda_ipsec_fetch(fr, src, ESP_DATA & 3, fo + len + 4, lane_id());
	if ((e & (KNOD_PERSIST_HTX_CSUM | KNOD_PERSIST_HTX_GSO)) &&
	    !gda_htx_prep(fr, fo, len, e, x))
		return 0;
	spi = uniform32(gda_lds32u(fr, fo + ESP_SPI));
	tot = uniform32(gda_lds_be16(fr, fo + 16));
	if (spi != uniform32(get32(&sa->spi)) ||
	    tot < ESP_DATA - 14 + ESP_ICV || 14 + tot > len)
		return 0;
	clen = 14 + tot - ESP_DATA - ESP_ICV;
	rk = (const CONSTANT uint32_t *)uniform64(get64(&sa->rk));
	rounds = uniform32(get32(&sa->rounds));
	salt = uniform32(get32(&sa->salt));
	seq = uniform32(gda_lds32u(fr, fo + ESP_SPI + 4));
	if (!!rounds != !!gcm)
		return 0;
	/* GHASH's powers of H: the SA's table has so many. */
	if (gcm && (clen + 15) / 16 + 2 > KNOD_PERSIST_IPSEC_HPOW)
		return 0;
	if (gcm) {
		gda_gcm_seal(g, fr, (fo + ESP_DATA) / 4, fo + ESP_IV, rk,
			     rounds, salt,
			     (GLOBAL const uint8_t *)uniform64(get64(&sa->hpow)),
			     spi, seq, clen);
	} else {
		for (i = 0; i < 8; i++)
			key[i] = uniform32(rk[i]);
		gda_chacha_rx(fr, (fo + ESP_DATA) / 4, fo + ESP_IV, key, salt,
			      spi, seq, clen, lane_id(),
			      g->entry->regress_dbg, 1);
	}
	gda_ipsec_out(dst, fr, 0, (len + 15) & ~15u);
	return 1;
}
#endif

/* The host's TX buffer, and the frames in it that go this round. */
struct gda_htx {
	GLOBAL uint8_t *htx, *ring, *ctl;
	uint32_t mask, pool, pmask, cc, n;
};

/* Up to @room of the frames waiting, sixty-four at most. */
static INLINE void gda_htx_take(struct gda *g, struct gda_htx *h,
				uint32_t room)
{
	h->mask = get32(&g->entry->htx_mask);
	h->pool = get32(&g->entry->htx_pool);
	h->pmask = get32(&g->entry->htx_pool_mask);
	h->htx = (GLOBAL uint8_t *)GL64(g, HTX);
	h->ring = h->htx + ((uint64_t)(h->mask + 1) << GL(g, PAGE_SHIFT));
	h->ctl = h->ring + KNOD_PERSIST_HTX_ENTRY * (h->mask + 1);
	h->cc = get32(&g->entry->htx_cc);
	h->n = get32(&g->entry->htx_pc) - h->cc;
	h->n = h->n < room ? h->n : room;
	h->n = h->n < 64 ? h->n : 64;
}

/* Frame @l's ring entry, and its second word. */
static INLINE uint64_t gda_htx_entry(struct gda_htx *h, uint32_t l)
{
	return get64(h->ring + KNOD_PERSIST_HTX_ENTRY * ((h->cc + l) & h->mask));
}

static INLINE uint64_t gda_htx_entry2(struct gda_htx *h, uint32_t l)
{
	return get64(h->ring + KNOD_PERSIST_HTX_ENTRY * ((h->cc + l) & h->mask) +
		     8);
}

/* Frame @l's entry, where it is and where it goes. */
static INLINE uint64_t gda_htx_frame(struct gda *g, struct gda_htx *h,
				     uint32_t l, GLOBAL const uint8_t **src,
				     GLOBAL uint8_t **dst)
{
	uint32_t shift = GL(g, PAGE_SHIFT);

	*src = h->htx + ((uint64_t)((h->cc + l) & h->mask) << shift) +
	       KNOD_PERSIST_HTX_HEAD;
	*dst = (GLOBAL uint8_t *)GL64(g, RX_BASE) +
	       ((uint64_t)(h->pool + ((h->cc + l) & h->pmask)) << shift);
	return uniform64(gda_htx_entry(h, l));
}

/* The SQ's room for the host's frames once the round's own are on it. */
static INLINE uint32_t gda_htx_room(struct gda *g)
{
	if (!GL64(g, HTX) || !GL(g, SQ_MASK) || GL(g, TX_DEAD))
		return 0;
	return GL(g, SQ_MASK) + 1 - NIC_SQ_SPARE -
	       (GL(g, SQ_PC) - GL(g, SQ_CC));
}

#ifdef GDA_IPSEC
/* Whether frame entry @e is ESP of an AES-GCM SA, for gda_host_gcm(). */
static INLINE int gda_htx_is_gcm(struct gda *g, uint64_t e)
{
	GLOBAL const struct knod_persistent_ipsec_sa *sa;

	if (!(e & KNOD_PERSIST_HTX_ESP))
		return 0;
	sa = (GLOBAL const struct knod_persistent_ipsec_sa *)
	     GL64(g, IPSEC_SA) + KNOD_PERSIST_HTX_SA(e);
	return uniform32(get32(&sa->rounds)) != 0;
}
#endif

/*
 * Pick up the rings where the last shader left them, or post them afresh for
 * a new build.  A wave the queue does not need, or a queue whose rings the NIC
 * did not give us, ends here.
 */
v4u GDA_FN(gda_init)(ctl_t ctl, uint32_t queue, uint32_t wave)
{
	struct gda g = { .s = 0 };
	entry_t e;
	uint64_t sq, db;
	uint32_t mask, waves;
	int fresh;

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	queue = uniform32(queue);
	wave = uniform32(wave);
#ifdef GDA_IPSEC
	/* Past the queues: the crypto pool's. */
	if (get32(&ctl->gda_queues) && queue >= get32(&ctl->gda_queues))
		gda_pool_run(ctl, queue - get32(&ctl->gda_queues), wave);
#endif
	e = &ctl->gda[queue];
	waves = get32(&ctl->gda_waves);
	if (!get32(&e->live) || wave >= waves)
		__builtin_amdgcn_endpgm();

	nic_load(&g, e);
	SET(&g, RQ_LOG, get32(&e->rq_log));
	SET(&g, RQ_SIZE, 1u << get32(&e->rq_log));
	SET(&g, CQ_LOG, get32(&e->cq_log));
	SET(&g, HEADROOM, get32(&e->headroom));
	SET64(&g, RX_DMA, get64(&e->rx_dma));
	SET64(&g, TX_PKTS, get64(&e->tx_packets));
	SET64(&g, TX_FULL, get64(&e->tx_full));
	g.packets = get64(&e->packets);

	/* The SQ, if the NIC gave us that too and its doorbell is mapped. */
	sq = get64(&e->sq);
	db = get64(&ctl->tx_db[queue]);
	mask = sq && db ? get32(&e->sq_mask) : 0;
	SET(&g, SQ_MASK, mask);
	SET64(&g, SQ, sq);
	SET64(&g, DB, db);
	SET(&g, TX_CQ_LOG, get32(&e->tx_cq_log));
	SET(&g, TX_GEN, get32(&e->tx_gen));
	/* Where the last shader left this build of the SQ, or its start. */
	if (mask && get32(&e->tx_gen) == get32(&e->tx_posted_gen)) {
		SET(&g, SQ_PC, get32(&e->sq_pc));
		SET(&g, SQ_CC, get32(&e->sq_cc));
		SET(&g, TX_CI, get32(&e->tx_ci));
	}

	SET64(&g, PASS_RING, get64(&e->pass_ring));
	SET64(&g, PASS_STAGE, get64(&e->pass_stage));
	SET64(&g, PASS_FULL, get64(&e->pass_full));
	SET(&g, PASS_MASK, get32(&e->pass_mask));
	SET(&g, PASS_PC, get32(&e->pass_pc));
	SET(&g, PASS_CC, get32(&e->pass_pc) - get32(&e->pass_mask) - 1);
	SET(&g, STAGGER, get32(&e->stagger));
	SET(&g, STAGGER_MASK, get32(&e->stagger_mask));
	SET64(&g, ROUNDS, get64(&e->rounds));
	SET(&g, WAVES, waves);
	SET64(&g, PARAM, get64(&ctl->gda_param));
	SET(&g, LDS, get32(&ctl->gda_lds));
	SET64(&g, RX_BASE, get64(&e->rx_base));
	SET(&g, PAGE_SHIFT,
	    load((gptr)(GL64(&g, PARAM) + KNOD_BLOB_PARAM_PAGE_SHIFT)));
	SET(&g, CI, get32(&e->ci));
	SET64(&g, RING, get64(&e->ring));
	SET64(&g, HTX, get64(&e->htx));
	fresh = get32(&e->posted_gen) != get32(&e->gen);
	/* Where the last shader left the RQ, so the bound is seen to move on
	 * from there.
	 */
	SET(&g, LAST_BOUND, fresh ? 0 : get32(&e->rq_bound));
	gda_load(&g, ctl, queue, wave);
#ifdef GDA_IPSEC
	gda_ipsec_load(&g, ctl);
#endif

	/* LDS starts undefined: no stop until wave 0 says so.  Every wave has
	 * read the entry before wave 0 writes it.
	 */
	if (!wave && !lane_id())
		g.meet->cmd = 0;
	barrier();

	/* Posted for this build of the rings already: pick up where the last
	 * shader left the CQ.  Otherwise wave 0 posts every RQ entry.
	 */
	if (fresh) {
		SET(&g, CI, 0);
		if (!wave)
			gda_post_rq(&g);
	}

	return (v4u){ g.s, g.packets, g.packets >> 32, 0 };
}

/*
 * For the ordered engine: each packet's flow - the NIC's RSS hash, folded to
 * 16 bits - where gda_gate() and gda_parked() find it.  Two flows on one key
 * only wait for each other; a packet without a hash shares key 0 with the
 * rest of those.  Its rank waits until a lane is parked: most rounds park
 * none.
 */
static INLINE void gda_rank(struct gda *g, uint32_t rss, uint32_t mine,
			    uint32_t n, uint32_t *rank)
{
	uint32_t me = 64 * g->wave + lane_id();
	uint64_t have = lanes_below(mine);

	if ((have >> lane_id()) & 1)
		g->meet->key[me] = rss ^ rss >> 16;
	/* What gda_gate() will set, clear before any wave can. */
	if (!g->wave && lane_id() < KNOD_PERSIST_GDA_WAVES_MAX * 2) {
		g->meet->part[lane_id()] = 0;
		g->meet->act[lane_id()] = 0;
	}
	if (!g->wave && !lane_id())
		g->meet->count = n;
	barrier();

	*rank = (have >> lane_id()) & 1 ? 0 : KNOD_BLOB_RANK_NONE;
}

static INLINE void gda_sync_dbg(struct gda *g, uint64_t off, uint32_t pos,
				uint32_t entry, uint32_t kind)
{
	uint32_t l = __builtin_ctzll(off);
	GLOBAL uint32_t *d = g->entry->sync_dbg;

	if (lane_id())
		return;
	add(&d[KNOD_PERSIST_SYNC_HITS], 1);
	put32(&d[KNOD_PERSIST_SYNC_ENTRIES],
	      (__builtin_amdgcn_readlane(pos, l) & 0xffff) |
	      __builtin_amdgcn_readlane(entry, l) << 16);
	put32(&d[KNOD_PERSIST_SYNC_KIND], __builtin_amdgcn_readlane(kind, l));
}

/*
 * Wait for a round's packets and hand this wave's to the program.  Wave 0
 * reads the control block and parks or stops for all of them; then every wave
 * reads its 64 CQEs and the round is the runs of every wave up to the first
 * that was short.  While there is none, what was sent still frees RQ entries,
 * and nothing else would.
 *
 * Returns, per lane, what the program's registers want: the page, where the
 * packet is in it and how long, the lane's index in the queue and in all
 * queues, the page's address, the lane's xdp_md, then which lanes have one,
 * the parameter block, and the state back.
 */
static INLINE v16u gda_round_begin(ctl_t ctl, uint32_t queue, uint32_t wave,
				   uint32_t s, uint32_t pk_lo, uint32_t pk_hi,
				   int ordered)
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };
	uint32_t pos, run, n, full, i, waves, idle, len, page, off, idx;
	uint32_t rank = 0, rss = 0, kind, htx;
	uint64_t mine, page_base, param, ctx, bad;
	struct nic_rx rx;
	v16u r;

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_load(&g, ctl, uniform32(queue), uniform32(wave));
	waves = GL(&g, WAVES);

	for (;;) {
		if (!g.wave) {
			gda_park_or_stop(&g);
			SET(&g, WAKE, get32(&ctl->pass_wake));
		}
		barrier();
		if (g.meet->cmd)
			__builtin_amdgcn_endpgm();

		/* Lane i polls completion ci + 64w + i. */
		pos = GL(&g, CI) + 64 * g.wave + lane_id();
		run = run_of(ballot(nic_rx_poll(&g, pos, &rx)));
		if (!lane_id())
			g.meet->n[g.wave] = run;
		htx = 0;
		if (!g.wave) {
			htx = gda_htx_pending(&g);
			if (!lane_id())
				g.meet->htx = htx;
		}
		/* Maps the host changed while we were parked, and anything
		 * else a program reads that someone else writes; once, for the
		 * workgroup, before any wave's program runs.
		 */
		if (!g.wave && (run || htx)) {
			__builtin_amdgcn_fence(__ATOMIC_ACQUIRE, "agent");
			__builtin_amdgcn_s_dcache_inv();
		}
		barrier();

		n = 0;
		mine = 0;
		full = 1;
		for (i = 0; i < KNOD_PERSIST_GDA_WAVES_MAX; i++) {
			uint32_t r = i < waves ? g.meet->n[i] * full : 0;

			n += r;
			if (i == g.wave)
				mine = r;
			if (r != 64)
				full = 0;
		}
		SET(&g, N, n);
		SET(&g, RUN, mine);
		if (n || g.meet->htx)
			break;

		if (!g.wave && GL(&g, SQ_PC) != GL(&g, SQ_CC)) {
			nic_tx_reap(&g, GL(&g, SQ_PC));
			gda_rq_release(&g);
		}
		/* A queue the host sends on looks for its frames often. */
		idle = GL(&g, IDLE_ROUNDS);
		if (idle < 64 || GL64(&g, HTX)) {
			SET(&g, IDLE_ROUNDS, idle + 1);
			__builtin_amdgcn_s_sleep(1);
		} else {
#pragma clang loop unroll(disable)
			for (i = 0; i < 1024; i++)
				__builtin_amdgcn_s_sleep(15);
		}
	}
	SET(&g, IDLE_ROUNDS, 0);

	/* The CQE is ours through its owner word before its payload is read:
	 * both together can take a new owner with the previous byte count.
	 */
	ORDER();
	bad = nic_rx_read(&g, &rx, pos, ordered, &len, &rss, &page, &kind) &
	      lanes_below(mine);
	if (ordered)
		gda_rank(&g, rss, mine, n, &rank);
	if (bad)
		gda_sync_dbg(&g, bad, pos, page, kind);
	page &= GL(&g, RQ_SIZE) - 1;

	/* page base = rx_base + (page << page_shift); data past the headroom
	 * and the page's stagger.
	 */
	page_base = GL64(&g, RX_BASE) + ((uint64_t)page << GL(&g, PAGE_SHIFT));
	off = gda_page_off(&g, page);
	idx = g.queue * waves * 64 + 64 * g.wave + lane_id();
	param = GL64(&g, PARAM);
	ctx = param + KNOD_BLOB_PARAM_SUB + (uint64_t)idx * KNOD_BLOB_SUB_SIZE;

	/* The ordered engine's: the packet's rank among its flow's in the
	 * page's top half.
	 */
	r.s0 = page | rank << 16;
	r.s1 = off;
	r.s2 = len;
	r.s3 = 64 * g.wave + lane_id();
	r.s4 = idx;
	r.s5 = page_base;
	r.s6 = page_base >> 32;
	r.s7 = ctx;
	r.s8 = ctx >> 32;
	r.s9 = lanes_below(mine);
	r.sa = lanes_below(mine) >> 32;
	r.sb = param;
	r.sc = param >> 32;
	r.sd = g.s;
	r.se = g.packets;
	r.sf = g.packets >> 32;
	return r;
}

v16u GDA_FN(gda_round_begin)(ctl_t ctl, uint32_t queue, uint32_t wave,
			      uint32_t s, uint32_t pk_lo, uint32_t pk_hi)
{
#ifdef GDA_IPSEC
	uint32_t t0 = gda_cyc();
	v16u r = gda_round_begin(ctl, queue, wave, s, pk_lo, pk_hi, 0);

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_prof(ctl->gda[uniform32(queue)].regress_dbg, GDA_PROF_BEGIN, t0);
	return r;
#else
	return gda_round_begin(ctl, queue, wave, s, pk_lo, pk_hi, 0);
#endif
}

#ifndef GDA_IPSEC
v16u GDA_FN(gda_round_begin_ordered)(ctl_t ctl, uint32_t queue,
				      uint32_t wave, uint32_t s,
				      uint32_t pk_lo, uint32_t pk_hi)
{
	return gda_round_begin(ctl, queue, wave, s, pk_lo, pk_hi, 1);
}

/*
 * The ordered engine's, after the first pass: each parked lane's rank among
 * its flow's packets, into its flags, and the highest of them in every wave -
 * the passes the round still wants.  Only gda_gate() parks, and only where a
 * lane was about to write: in a round with none there is nothing to count,
 * and every wave knows it.
 */
v2u GDA_FN(gda_parked)(ctl_t ctl, uint32_t queue, uint32_t wave, uint32_t s,
		       uint32_t flags)
{
	LDS struct gda_meet *meet = meet_at(__builtin_amdgcn_readlane(s, GL_LDS));
	uint32_t waves = __builtin_amdgcn_readlane(s, GL_WAVES), top = 0, i, j;
	uint32_t me = 64 * uniform32(wave) + lane_id(), k = 0;
	int parked = (flags & KNOD_BLOB_RANK_MASK) != KNOD_BLOB_RANK_NONE &&
		     (flags & KNOD_BLOB_RANK_PARKED);
	uint16_t key;
	v2u r;

	r.x = flags;
	r.y = 0;
	if (!any_active(meet))
		return r;

	if (ballot(parked)) {
		key = meet->key[me];
		/* Every packet before this wave's last lane, so the bound is
		 * the wave's and the loop the whole wave's.
		 */
#pragma clang loop unroll_count(4)
		for (j = 0; j < me - lane_id() + 64; j++)
			k += j < me && meet->key[j] == key;
		if (parked)
			r.x = (flags & ~KNOD_BLOB_RANK_MASK) | k;
	}

	top = wave_max8(parked ? k : 0, ~0ull);
	if (!lane_id())
		meet->top[uniform32(wave)] = top;
	barrier();
	top = 0;
	for (i = 0; i < KNOD_PERSIST_GDA_WAVES_MAX; i++)
		if (i < waves && meet->top[i] > top)
			top = meet->top[i];
	r.y = top;
	return r;
}

#endif

/* Fold each wave's XDP_TX into the SQ's room, in wave order, so the WQEs
 * follow the packets' order on the RQ.
 */
struct gda_share {
	uint32_t asked;		/* XDP_TX all waves asked for */
	uint32_t given;		/* WQEs all waves got */
	uint32_t base;		/* this wave's first, from the round's */
	uint32_t mine;		/* this wave's */
	uint32_t last;		/* the last wave with one */
};

static INLINE struct gda_share gda_share(struct gda *g, uint32_t left)
{
	struct gda_share x = { 0 };
	uint32_t i, k;

	for (i = 0; i < KNOD_PERSIST_GDA_WAVES_MAX; i++) {
		k = i < GL(g, WAVES) ? g->meet->k[i] : 0;
		x.asked += k;
		k = k < left ? k : left;
		left -= k;
		if (i == g->wave) {
			x.base = x.given;
			x.mine = k;
		}
		if (k)
			x.last = i;
		x.given += k;
	}
	return x;
}

#ifdef GDA_IPSEC
/*
 * The frame copy in @fr to the host: @n bytes from its page's sixteen
 * aligned ones the frame starts in, @off bytes in, a lane sixteen bytes.
 */
static INLINE void gda_ipsec_out(GLOBAL uint8_t *dst, LDS const uint32_t *fr,
				 uint32_t off, uint32_t n)
{
	uint32_t b, at, sh = (off + ESP_DATA) & 3;

	for (b = 16 * lane_id(); b < n; b += 1024) {
		at = b + 4 - sh;
		put128(dst + b, (v4u){ gda_lds32u(fr, at), gda_lds32u(fr, at + 4),
				       gda_lds32u(fr, at + 8),
				       gda_lds32u(fr, at + 12) });
	}
}

/*
 * After the program, before gda_round_end(): the packets that pass, to the
 * host, decrypted on the way if they are ESP of an SA in the table.  The
 * PASS slots are reserved here, as gda_round_end() does for an engine
 * without IPsec, and the round's packets go into LDS for the waves to take
 * one at a time, whichever wave they came in on: a round fills the waves in
 * turn, and decrypting one wave's 64 while another has none left the rest
 * waiting.  A packet goes from its LDS copy - nothing is read back from the
 * page it was decrypted in - but for AES-GCM's, decrypted in the page.  The
 * entries are written by the lanes the packets came in on; gda_round_end()
 * publishes them.
 */
/* Where PASS packet @k of the round, at @off in RX page @page, @len long, is
 * and goes: the 16 bytes it starts in, and the bytes to copy from there.
 */
static INLINE void gda_ipsec_where(struct gda *g, uint32_t k, uint32_t page,
				   uint32_t off, uint32_t len, uint32_t ppc,
				   GLOBAL uint8_t **src, GLOBAL uint8_t **dst,
				   uint32_t *copy, uint32_t *open)
{
	uint32_t from = off & ~15u, to = (off + len + 15) & ~15u;

	*src = (GLOBAL uint8_t *)GL64(g, RX_BASE) +
	       ((uint64_t)page << GL(g, PAGE_SHIFT)) + from;
	*dst = (GLOBAL uint8_t *)GL64(g, PASS_STAGE) +
	       ((uint64_t)((ppc + k) & GL(g, PASS_MASK)) << GL(g, PAGE_SHIFT)) +
	       from;
	*copy = to - from;
	/* Its LDS copy reads a little past it: not past its page. */
	*open = off + len + 32 <= 1u << GL(g, PAGE_SHIFT);
}

/* The round's @ptotal PASS packets, the queue's waves alone taking them. */
static INLINE void gda_ipsec_own(struct gda *g, struct gda_sa_cache *sc,
				 LDS struct gda_ipsec_work *w, int ok,
				 uint32_t k, uint32_t page, uint32_t off,
				 uint32_t len, uint32_t ppc, uint32_t ptotal)
{
	uint32_t kk, pg, ol, copy, open;
	GLOBAL uint8_t *src, *dst;

	if (ok) {
		w->page[k] = page;
		w->offlen[k] = off | len << 16;
	}
	barrier();
	for (;;) {
		kk = 0;
		if (!lane_id())
			kk = __atomic_fetch_add(&w->next, 1, __ATOMIC_RELAXED);
		kk = __builtin_amdgcn_readfirstlane(kk);
		if (kk >= ptotal)
			break;
		pg = uniform32(w->page[kk]);
		ol = uniform32(w->offlen[kk]);
		gda_ipsec_where(g, kk, pg, ol & 0xffff, ol >> 16, ppc, &src,
				&dst, &copy, &open);
		ol = gda_ipsec_job(g, sc, src, dst, (ol & 0xffff) & 15,
				   ol >> 16, copy, open);
		if (!lane_id())
			w->meta[kk] = ol;
	}
	barrier();
}

/*
 * ...and with the crypto pool: the jobs in the queue's table @jt, which the
 * pool's workgroups take as the queue's waves do, then the round waits for
 * the last of them.
 */
static INLINE void gda_ipsec_pool(struct gda *g, struct gda_sa_cache *sc,
				  jobs_t jt, LDS struct gda_ipsec_work *w,
				  int ok, uint32_t k, uint32_t page,
				  uint32_t off, uint32_t len, uint32_t ppc,
				  uint32_t ptotal)
{
	GLOBAL struct knod_persistent_gda_job *jb = &jt->job[k];
	GLOBAL uint64_t *st = (GLOBAL uint64_t *)&jt->state;
	uint32_t copy, open, kk;
	GLOBAL uint8_t *src, *dst;
	uint64_t gen;

	if (ok) {
		gda_ipsec_where(g, k, page, off, len, ppc, &src, &dst, &copy,
				&open);
		put64(&jb->src, (uint64_t)src);
		put64(&jb->dst, (uint64_t)dst);
		put32(&jb->offlen, (off & 15) | len << 16);
		put32(&jb->copy, copy);
		put32(&jb->open, open);
	}
	stores_out();
	barrier();
	/* The jobs, then done, out before the word that hands them out. */
	if (!g->wave && !lane_id()) {
		store((gptr)&jt->done, 0);
		stores_out();
		gen = (__scoped_atomic_load_n(st, __ATOMIC_RELAXED,
					      __MEMORY_SCOPE_DEVICE) >> 32) + 1;
		__scoped_atomic_store_n(st, gen << 32 | (uint64_t)ptotal << 16,
					__ATOMIC_RELEASE,
					__MEMORY_SCOPE_DEVICE);
	}
	barrier();
	while (gda_job_take(jt, &kk))
		gda_job_run(g, sc, jt, kk);
	/* The pool's last ones. */
	if (!lane_id())
		while (load((gptr)&jt->done) < ptotal)
			__builtin_amdgcn_s_sleep(1);
	__builtin_amdgcn_fence(__ATOMIC_ACQUIRE, "agent");
	if (ok)
		w->meta[k] = get32(&jb->meta);
	barrier();
}

v4u GDA_FN(gda_ipsec)(ctl_t ctl, uint32_t queue, uint32_t wave, uint32_t s,
		      uint32_t pk_lo, uint32_t pk_hi, uint32_t verdict,
		      uint32_t page, uint32_t off, uint32_t len, uint32_t lo,
		      uint32_t hi)
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };
	uint64_t live = (uint64_t)uniform32(hi) << 32 | uniform32(lo);
	struct gda_sa_cache sc = { .slot = ~0u };
	uint32_t t0 = gda_cyc(), ppc, pcc, room, pbase = 0, ptotal = 0, k, i;
	LDS struct gda_ipsec_work *w;
	uint64_t pass;
	jobs_t jt;
	int ok;

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_load(&g, ctl, uniform32(queue), uniform32(wave));
	w = gda_ipsec_work(&g);
	pass = ballot(((live >> lane_id()) & 1) && verdict == XDP_PASS);

	/* The PASS slots, as gda_round_end() would have them. */
	if (!lane_id())
		g.meet->p[g.wave] = __builtin_popcountll(pass);
	if (!g.wave) {
		ppc = GL(&g, PASS_PC);
		pcc = GL(&g, PASS_CC);
		if (GL(&g, PASS_MASK) + 1 - (ppc - pcc) < 64 * GL(&g, WAVES))
			pcc = get32((GLOBAL uint8_t *)GL64(&g, PASS_RING) +
				    8 * (GL(&g, PASS_MASK) + 1) +
				    KNOD_PERSIST_PASS_CC);
		if (!lane_id()) {
			g.meet->pass_pc = ppc;
			g.meet->pass_room = GL(&g, PASS_MASK) + 1 - (ppc - pcc);
			w->next = 0;
		}
	}
	barrier();
	ppc = g.meet->pass_pc;
	room = g.meet->pass_room;
	for (i = 0; i < KNOD_PERSIST_GDA_WAVES_MAX; i++) {
		uint32_t p = i < GL(&g, WAVES) ? g.meet->p[i] : 0;

		ptotal += p;
		if (i < g.wave)
			pbase += p;
	}
	if (ptotal > room)
		ptotal = room;
	k = pbase + rank(pass);
	ok = ((pass >> lane_id()) & 1) && k < room;
	jt = (jobs_t)uniform64(get64(&g.entry->jobs));
	if (jt && ptotal)
		gda_ipsec_pool(&g, &sc, jt, w, ok, k, page, off, len, ppc,
			       ptotal);
	else
		gda_ipsec_own(&g, &sc, w, ok, k, page, off, len, ppc, ptotal);

	if (ok)
		put64((GLOBAL uint64_t *)GL64(&g, PASS_RING) +
		      ((ppc + k) & GL(&g, PASS_MASK)),
		      (uint64_t)(off | len << 16) << 32 |
		      nic_rx_csum(&g, GL(&g, CI) + 64 * g.wave + lane_id()) |
		      w->meta[k]);
	stores_out();
	gda_prof(g.entry->regress_dbg, GDA_PROF_IPSEC, t0);
	return (v4u){ 0, 0, 0, 0 };
}
#endif

/*
 * Carry out the program's verdicts: what it sent goes on the SQ, what it
 * passed to the host's PASS ring, and wave 0 tells the NIC and the host and
 * gives the CQ and RQ entries back - every verdict but a send as a drop does.
 *
 * @verdict, @page, @off and @len per lane, of the lanes in @lo:@hi.
 */
v4u GDA_FN(gda_round_end)(ctl_t ctl, uint32_t queue, uint32_t wave,
			  uint32_t s, uint32_t pk_lo, uint32_t pk_hi, uint32_t verdict,
		      uint32_t page, uint32_t off, uint32_t len, uint32_t lo,
		      uint32_t hi
#ifdef GDA_IPSEC
		      , uint32_t ipsec
#endif
		      )
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };
	uint64_t live = (uint64_t)uniform32(hi) << 32 | uniform32(lo);
	uint32_t pc, cc, left, ppc, pbase = 0, ptotal = 0, i, p, n, pos;
	uint32_t room, pcc, slot, meta = 0;
#ifdef GDA_IPSEC
	uint32_t t0 = gda_cyc();
#endif
#ifdef GDA_IPSEC
	meta = ipsec;
#endif
	int in = (live >> lane_id()) & 1;
	uint64_t tx, pass, copy;
	struct gda_share x;

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_load(&g, ctl, uniform32(queue), uniform32(wave));
	n = GL(&g, N);

	/* What this wave sends and hands to the host, for the others to see.
	 * With no SQ an XDP_TX drops.  Wave 0 takes what the NIC has sent since
	 * the last look, and says where the SQ and the PASS ring stand.
	 */
	tx = GL(&g, SQ_MASK) ? ballot(in && verdict == XDP_TX) : 0;
	pass = ballot(in && verdict == XDP_PASS);
	if (!lane_id()) {
		g.meet->k[g.wave] = __builtin_popcountll(tx);
#ifndef GDA_IPSEC
		g.meet->p[g.wave] = __builtin_popcountll(pass);
#endif
	}
	if (!g.wave) {
		pc = GL(&g, SQ_PC);
		if (GL(&g, SQ_MASK) && pc != GL(&g, SQ_CC))
			nic_tx_reap(&g, pc);
		cc = GL(&g, SQ_CC);
		/* A dead SQ has room for none: the waves see it full. */
		if (GL(&g, TX_DEAD))
			cc = pc - GL(&g, SQ_MASK) - 1;
#ifndef GDA_IPSEC
		/* The PASS slots free: what the host had finished with when
		 * last looked, and a look past the ring for more only when
		 * that might not do for a whole round.  gda_ipsec() has done
		 * so for an IPsec engine.
		 */
		ppc = GL(&g, PASS_PC);
		pcc = GL(&g, PASS_CC);
		if (GL(&g, PASS_MASK) + 1 - (ppc - pcc) < 64 * GL(&g, WAVES)) {
			pcc = get32((GLOBAL uint8_t *)GL64(&g, PASS_RING) +
				    8 * (GL(&g, PASS_MASK) + 1) +
				    KNOD_PERSIST_PASS_CC);
			SET(&g, PASS_CC, pcc);
		}
#endif
		if (!lane_id()) {
			g.meet->sq_pc = pc;
			g.meet->sq_cc = cc;
#ifndef GDA_IPSEC
			g.meet->pass_pc = ppc;
			g.meet->pass_room = GL(&g, PASS_MASK) + 1 - (ppc - pcc);
#endif
		}
	}
	barrier();

	pc = g.meet->sq_pc;
	cc = g.meet->sq_cc;
	ppc = g.meet->pass_pc;
	room = g.meet->pass_room;
	ptotal = 0;
	for (i = 0; i < KNOD_PERSIST_GDA_WAVES_MAX; i++) {
		p = i < GL(&g, WAVES) ? g.meet->p[i] : 0;
		ptotal += p;
		if (i < g.wave)
			pbase += p;
	}
	left = GL(&g, SQ_MASK) + 1 - NIC_SQ_SPARE - (pc - cc);
	x = gda_share(&g, left);

	/* Each WQE says which RQ position its packet came in on, so that
	 * whatever WQE the NIC's completions stop at, the RQ is held from the
	 * right place.  Only the round's last WQE asks for a completion.
	 */
	pos = GL(&g, CI) + 64 * g.wave + lane_id();
	if (x.mine && ((tx >> lane_id()) & 1) && rank(tx) < x.mine) {
		uint32_t counter = pc + x.base + rank(tx);
		int last = g.wave == x.last && rank(tx) == x.mine - 1;

		nic_tx_post(&g, counter, page, off, len, last);
		put32(g.ring + KNOD_PERSIST_RING_RQPOS_OFF +
		      4 * (counter & GL(&g, SQ_MASK)), pos);
	}

	/* The packets that pass and find a slot go to it, at their offset in
	 * the RX page, one at a time with the whole wave on each: a lane per
	 * sixteen bytes, so each access is the packet's next kilobyte in one
	 * piece rather than sixty-four pieces of sixty-four packets.  Then
	 * each lane's entry.  The RX pages are free once this round ends.
	 */
#ifndef GDA_IPSEC
	slot = (ppc + pbase + rank(pass)) & GL(&g, PASS_MASK);
	copy = ballot(((pass >> lane_id()) & 1) && pbase + rank(pass) < room);
	while (copy) {
		uint32_t l = __builtin_ctzll(copy), from, to;
		GLOBAL uint8_t *src, *dst;

		copy &= copy - 1;
		from = __builtin_amdgcn_readlane(off, l) & ~15u;
		to = (__builtin_amdgcn_readlane(off, l) +
		      __builtin_amdgcn_readlane(len, l) + 15) & ~15u;
		src = (GLOBAL uint8_t *)GL64(&g, RX_BASE) +
		      ((uint64_t)__builtin_amdgcn_readlane(page, l) <<
		       GL(&g, PAGE_SHIFT)) + from;
		dst = (GLOBAL uint8_t *)GL64(&g, PASS_STAGE) +
		      ((uint64_t)__builtin_amdgcn_readlane(slot, l) <<
		       GL(&g, PAGE_SHIFT)) + from;
		gda_copy(dst, src, to - from);
	}
	if (((pass >> lane_id()) & 1) && pbase + rank(pass) < room)
		put64((GLOBAL uint64_t *)GL64(&g, PASS_RING) + slot,
		      (uint64_t)(off | len << 16) << 32 | nic_rx_csum(&g, pos) |
		      meta);
#endif

	/* The host's frames for after the round, as many as the SQ has room
	 * for past the round's own.
	 */
	if (!g.wave) {
		struct gda_htx h = { 0 };

		if (gda_htx_room(&g))
			gda_htx_take(&g, &h, gda_htx_room(&g) - x.given);
		if (!lane_id()) {
			g.meet->htx_n = h.n;
			g.meet->htx_next[0] = 0;
			g.meet->htx_next[1] = 0;
			g.meet->htx_ok[0] = 0;
			g.meet->htx_ok[1] = 0;
		}
	}

	/* Every wave's entries are in memory before wave 0 says they are. */
	stores_out();
	barrier();

	if (!g.wave) {
		if (ptotal > room) {
			SET64(&g, PASS_FULL, GL64(&g, PASS_FULL) + ptotal - room);
			ptotal = room;
		}
		if (ptotal) {
			ppc += ptotal;
			SET(&g, PASS_PC, ppc);
			if (!lane_id()) {
				put32(&g.entry->pass_pc, ppc);
				put32((GLOBAL uint32_t *)
				      ((GLOBAL uint64_t *)GL64(&g, PASS_RING) +
				       GL(&g, PASS_MASK) + 1), ppc);
			}
			/* The host sleeps until PASS entries come: take the
			 * request, so the other rounds leave it be, and
			 * interrupt once they are in memory.
			 */
			if (GL(&g, WAKE)) {
				SET(&g, WAKE, 0);
				if (!lane_id())
					put32(&ctl->pass_wake, 0);
				stores_out();
				__builtin_amdgcn_s_sendmsg(1, KNOD_PERSIST_PASS_IRQ);
			}
		}
		SET64(&g, TX_FULL, GL64(&g, TX_FULL) + x.asked - x.given);
		SET64(&g, TX_PKTS, GL64(&g, TX_PKTS) + x.given);
		pc += x.given;
		if (pc != GL(&g, SQ_PC)) {
			SET(&g, SQ_PC, pc);
			nic_tx_ring(&g, pc);
		}
	}

	/* Every wave moves its consumer index; wave 0 gives the CQ and RQ
	 * entries back.
	 */
	SET(&g, CI, GL(&g, CI) + n);
	if (!g.wave) {
		gda_rq_release(&g);
		g.packets += n;
		SET64(&g, ROUNDS, GL64(&g, ROUNDS) + 1);
		gda_save(&g);
	}
#ifdef GDA_IPSEC
	/* gda_ipsec() took the PASS packets. */
	(void)pass;
	(void)copy;
	(void)pbase;
	(void)pcc;
	(void)slot;
	(void)meta;
	gda_prof(g.entry->regress_dbg, GDA_PROF_END, t0);
#ifdef KNOD_IPSEC_PROF
	if (!lane_id())
		add((gptr)&g.entry->regress_dbg[GDA_PROF_END_CALLS], 1);
#endif
#endif
	return (v4u){ g.s, g.packets, g.packets >> 32, 0 };
}

/*
 * Every wave: the round's host frames, AES-GCM ESP if @gcm or the rest, one
 * at a time, whichever wave is free: copied to their pool pages, ESP sealed
 * on the way.  Whether each may go, in LDS.  How many frames there are.
 */
static INLINE uint32_t gda_htx_work(struct gda *g, int gcm)
{
	uint32_t n = uniform32(g->meet->htx_n), k;
	GLOBAL const uint8_t *src;
	struct gda_htx h;
	GLOBAL uint8_t *dst;
	uint64_t e;
	int ok;

	if (!n)
		return 0;
	gda_htx_take(g, &h, n);
	for (;;) {
		k = 0;
		if (!lane_id())
			k = __atomic_fetch_add(&g->meet->htx_next[gcm], 1,
					       __ATOMIC_RELAXED);
		k = __builtin_amdgcn_readfirstlane(k);
		if (k >= n)
			break;
		e = gda_htx_frame(g, &h, k, &src, &dst);
		/* The kernel's SDMA puts a frame in its pool page: ESP is
		 * sealed there, the rest goes as it is.  An engine without
		 * IPsec still copies one from its slot.
		 */
		ok = 1;
#ifdef GDA_IPSEC
		if (gda_htx_is_gcm(g, e) != gcm)
			continue;
		if (e & KNOD_PERSIST_HTX_ESP)
			ok = (e & KNOD_PERSIST_HTX_INPLACE) &&
			     KNOD_PERSIST_HTX_ESP_OFF(e) == ESP_SPI &&
			     gda_htx_seal(g, dst, dst, KNOD_PERSIST_HTX_LEN(e),
					  KNOD_PERSIST_HTX_SA(e), gcm, e,
					  uniform64(gda_htx_entry2(&h, k)));
		else
			ok = (e & KNOD_PERSIST_HTX_INPLACE) != 0;
		(void)src;
#else
		if (e & KNOD_PERSIST_HTX_ESP)
			ok = 0;
		else if (!(e & KNOD_PERSIST_HTX_INPLACE))
			gda_copy(dst, src, (KNOD_PERSIST_HTX_LEN(e) + 15) & ~15u);
#endif
		if (ok && !lane_id())
			__atomic_fetch_or(&g->meet->htx_ok[k / 32], 1u << (k & 31),
					  __ATOMIC_RELAXED);
	}
	stores_out();
	return n;
}

#ifdef GDA_IPSEC
/*
 * After gda_round_end(): the host frames of AES-GCM SAs sealed, in a call of
 * their own for the registers it takes.
 */
v4u GDA_FN(gda_host_gcm)(ctl_t ctl, uint32_t queue, uint32_t wave,
			 uint32_t s, uint32_t pk_lo, uint32_t pk_hi)
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_load(&g, ctl, uniform32(queue), uniform32(wave));
	gda_htx_work(&g, 1);
	return (v4u){ g.s, g.packets, g.packets >> 32, 0 };
}

/* ...and then the rest, ChaCha20-Poly1305 and plain, likewise. */
v4u GDA_FN(gda_host_chacha)(ctl_t ctl, uint32_t queue, uint32_t wave,
			    uint32_t s, uint32_t pk_lo, uint32_t pk_hi)
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_load(&g, ctl, uniform32(queue), uniform32(wave));
	gda_htx_work(&g, 0);
	return (v4u){ g.s, g.packets, g.packets >> 32, 0 };
}
#endif

/*
 * After gda_round_end() - or an IPsec engine's gda_host_gcm() and
 * gda_host_chacha() - the rest of the host's frames, then wave 0 posts every
 * one that may go.  They hold no RQ entry, so their
 * WQEs say where the CQ stands.  A frame that could not be sealed is
 * skipped, its slot freed all the same.
 */
v4u GDA_FN(gda_host_tx)(ctl_t ctl, uint32_t queue, uint32_t wave,
			uint32_t s, uint32_t pk_lo, uint32_t pk_hi)
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };
	uint32_t n, len = 0, page, pc, sent;
	struct gda_htx h;
	uint64_t go;

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_load(&g, ctl, uniform32(queue), uniform32(wave));
#ifdef GDA_IPSEC
	n = uniform32(g.meet->htx_n);
#else
	n = gda_htx_work(&g, 0);
#endif
	if (!n)
		goto out;
	barrier();
	if (g.wave)
		goto out;

	gda_htx_take(&g, &h, n);
	go = (uint64_t)uniform32(g.meet->htx_ok[1]) << 32 |
	     uniform32(g.meet->htx_ok[0]);
	pc = GL(&g, SQ_PC);
	sent = __builtin_popcountll(go);
	if (lane_id() < n)
		len = KNOD_PERSIST_HTX_LEN(gda_htx_entry(&h, lane_id()));
	page = h.pool + ((h.cc + lane_id()) & h.pmask);
	if ((go >> lane_id()) & 1) {
		nic_tx_post(&g, pc + rank(go), page, 0, len,
			    rank(go) == sent - 1);
		put32(g.ring + KNOD_PERSIST_RING_RQPOS_OFF +
		      4 * ((pc + rank(go)) & GL(&g, SQ_MASK)), GL(&g, CI));
	}
	/* The slots are copied out before the host is told it has them. */
	stores_out();
	if (!lane_id()) {
		put32(h.ctl + KNOD_PERSIST_HTX_CC, h.cc + n);
		/* Read back with get32() next round: the same kind of
		 * access, so that round sees it.
		 */
		store((gptr)&g.entry->htx_cc, h.cc + n);
	}
	if (sent) {
		SET64(&g, TX_PKTS, GL64(&g, TX_PKTS) + sent);
		pc += sent;
		SET(&g, SQ_PC, pc);
		nic_tx_ring(&g, pc);
	}
	gda_save(&g);
out:
	return (v4u){ g.s, g.packets, g.packets >> 32, 0 };
}
