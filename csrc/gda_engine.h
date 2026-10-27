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
#define AES_TAB		LDS
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

/* The AES T-tables in LDS, past where the waves meet. */
static INLINE LDS uint32_t *gda_ipsec_ttab(struct gda *g)
{
	return (LDS uint32_t *)(uintptr_t)
		(uniform32(GL(g, LDS)) + KNOD_PERSIST_GDA_LDS_BYTES);
}

/* The wave's window over the payload it is on, past the T-tables. */
static INLINE LDS uint8_t *gda_ipsec_buf(struct gda *g)
{
	return (LDS uint8_t *)(uintptr_t)
		(uniform32(GL(g, LDS)) + KNOD_PERSIST_GDA_LDS_BYTES +
		 KNOD_PERSIST_GDA_IPSEC_TTAB_BYTES +
		 KNOD_PERSIST_GDA_IPSEC_BUF_BYTES * g->wave);
}

/* Every wave's share of the T-tables into LDS, once for the shader. */
static INLINE void gda_ipsec_load(struct gda *g, ctl_t ctl)
{
	GLOBAL const uint32_t *src =
		(GLOBAL const uint32_t *)get64(&ctl->ipsec_ttab);
	LDS uint32_t *t = gda_ipsec_ttab(g);
	uint32_t i;

	SET64(g, IPSEC_SA, get64(&ctl->ipsec_sa));
	SET(g, IPSEC_SA_MASK, get32(&ctl->ipsec_sa_mask));
	for (i = 64 * g->wave + lane_id();
	     i < KNOD_PERSIST_GDA_IPSEC_TTAB_BYTES / 4; i += 64 * GL(g, WAVES))
		t[i] = src[i];
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

/*
 * Where an IPsec engine's time goes, into its queue's regress_dbg: cycles of
 * the CU the wave is on (twenty bits of them on gfx11), summed by lane 0.
 */
#define GDA_PROF_PKTS		0
#define GDA_PROF_FETCH		1
#define GDA_PROF_POLY		2
#define GDA_PROF_DECRYPT	3
#define GDA_PROF_PACKET		4
#define GDA_PROF_END_CALLS	5
#define GDA_PROF_END		6
#define GDA_PROF_ITER		7

static INLINE uint32_t gda_cyc(void)
{
	return (uint32_t)__builtin_readcyclecounter();
}

static INLINE void gda_prof(GLOBAL uint32_t *dbg, int i, uint32_t from)
{
	if (!lane_id())
		add((gptr)&dbg[i], (gda_cyc() - from) & 0xfffff);
}

/* The wave's copy of the frame it is on. */
static INLINE LDS uint32_t *gda_ipsec_frame(struct gda *g)
{
	return (LDS uint32_t *)(uintptr_t)
		(uniform32(GL(g, LDS)) + KNOD_PERSIST_GDA_LDS_BYTES +
		 KNOD_PERSIST_GDA_IPSEC_TTAB_BYTES +
		 4 * KNOD_PERSIST_GDA_IPSEC_BUF_BYTES +
		 KNOD_PERSIST_GDA_IPSEC_FRAME_BYTES * g->wave);
}

/* Four bytes from byte @b of an LDS copy, wherever they start. */
static INLINE uint32_t gda_lds32u(LDS const uint32_t *fr, uint32_t b)
{
	uint32_t a = b >> 2;

	return b & 3 ? __builtin_amdgcn_alignbyte(fr[a + 1], fr[a], b & 3) :
		       fr[a];
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

static INLINE uint32_t gda_ipsec_take(LDS const uint8_t *buf, uint32_t raw,
				      int q, int lim)
{
	LDS const uint32_t *w = (LDS const uint32_t *)buf;
	int a = q >> 2;
	uint32_t lo = w[a < 0 ? 0 : a], hi = w[a + 1 < 0 ? 0 : a + 1];
	uint32_t v = __builtin_amdgcn_alignbyte(hi, lo, q & 3);
	uint32_t m = ~0u;

	if (q < 0)
		m = q <= -4 ? 0 : m << (8 * -q);
	if (lim - q < 4)
		m &= lim - q <= 0 ? 0 : (1u << (8 * (lim - q))) - 1;
	return (v & m) | (raw & ~m);
}

/* ds_swizzle's pattern for the lane @x away in each 32: and 0x1f, or 0. */
#define GDA_SWIZZLE_XOR(x)	((x) << 10 | 0x1f)

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
 * bytes and the word past them, a kilobyte an instruction, one wait.
 */
static INLINE void gda_ipsec_fetch(LDS uint32_t *fr, GLOBAL const uint8_t *src,
				   uint32_t sh, uint32_t end, uint32_t lane)
{
	uint32_t q, i, hi, o[4];

	for (q = lane; 16 * q < end; q += 64) {
		GLOBAL const uint32_t *w =
			(GLOBAL const uint32_t *)(src - 4) + 4 * q;
		v4u v = get128(w);
		uint32_t nx = get32(w + 4);

		for (i = 0; i < 4; i++) {
			hi = i < 3 ? v[i + 1] : nx;
			o[i] = sh ? __builtin_amdgcn_alignbyte(hi, v[i], sh) :
				    v[i];
		}
		*(LDS v4u *)(fr + 4 * q) = (v4u){ o[0], o[1], o[2], o[3] };
	}
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
}

/*
 * The wave's window over @lim bytes of payload back to the page, sixteen
 * aligned bytes a lane from @from to @to, shifted by @d into place and the
 * frame's own bytes around it.
 */
static INLINE void gda_ipsec_put(GLOBAL uint8_t *src, LDS const uint8_t *buf,
				 uint32_t from, uint32_t to, int d, int lim,
				 uint32_t lane)
{
	uint32_t b, i, w;

	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
	for (b = 16 * lane; b < to - from; b += 1024) {
		v4u r = get128(src + from + b);
		int q = b - d;

		/* A word at a time, as gcm_mul()'s products. */
#pragma unroll
		for (i = 0; i < 4; i++, q += 4) {
			w = gda_ipsec_take(buf, r[i], q, lim);
			GCM_HERE(w);
			GCM_HERE(q);
			r[i] = w;
		}
		put128(src + from + b, r);
	}
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");
}

/* Sixteen bytes of payload from @b, zero past @clen, words as they come. */
static INLINE void gda_ipsec_chunk(uint32_t c[4], GLOBAL const uint8_t *f,
				   uint32_t b, uint32_t clen)
{
	uint32_t nb = clen - b < 16 ? clen - b : 16, i, w;

#pragma unroll
	for (i = 0; i < 4; i++) {
		w = gda_ld32u(f + ESP_DATA + b + 4 * i);
		c[i] = 4 * i >= nb ? 0 : 4 * i + 4 <= nb ? w :
		       w & ((1u << (8 * (nb - 4 * i))) - 1);
	}
}

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
 * 64-byte block of the copy, and the page takes the payload back sixteen
 * aligned bytes a lane: @pg the page's address of the copy's byte 0.
 */
static INLINE uint32_t gda_chacha_rx(LDS uint32_t *fr, uint32_t pw,
				     GLOBAL uint8_t *pg, uint32_t iv,
				     const uint32_t key[8], uint32_t salt,
				     uint32_t spi, uint32_t seq, uint32_t clen,
				     uint32_t lane, GLOBAL uint32_t *dbg)
{
	uint32_t nonce[3], blk[16], s[4], w[4], tag[4], t = gda_cyc();
	uint32_t n = (clen + 15) / 16, nb = n + 2, i, k, kk, pad, c, fail, b;
	GLOBAL uint8_t *c0, *c1;
	struct poly r, cur, h, p, cv;

	nonce[0] = salt;
	nonce[1] = uniform32(gda_lds32u(fr, iv));
	nonce[2] = uniform32(gda_lds32u(fr, iv + 4));

	chacha20_block(blk, key, 0, nonce);
	poly_r(&r, blk);
	for (i = 0; i < 4; i++)
		s[i] = blk[4 + i];

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
			b = 16 * (c - 1) + 4 * i;
			w[i] = !c ? (i == 0 ? spi : i == 1 ? seq : 0) :
			       c == nb - 1 ? (i == 0 ? 8 : i == 2 ? clen : 0) :
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
	fail = 0;
	for (i = 0; i < 4; i++)
		fail |= tag[i] != gda_lds32u(fr, 4 * pw + clen + 4 * i);
	gda_prof(dbg, GDA_PROF_POLY, t);
	t = gda_cyc();

	/* Block 1 + lane of the keystream over the copy's payload. */
	if (64 * lane < clen) {
		chacha20_block(blk, key, 1 + lane, nonce);
		for (i = 0; i < 16; i++) {
			b = 64 * lane + 4 * i;
			if (b < clen)
				fr[pw + b / 4] ^= b + 4 <= clen ? blk[i] :
					blk[i] & ((1u << (8 * (clen - b))) - 1);
		}
	}
	__builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "wavefront");

	/* The page's sixteen-byte pieces over the payload, out of the copy. */
	c0 = (GLOBAL uint8_t *)((uintptr_t)(pg + 4 * pw) & ~(uintptr_t)15);
	c1 = (GLOBAL uint8_t *)(((uintptr_t)(pg + 4 * pw + clen) + 15) &
				~(uintptr_t)15);
	for (b = 16 * lane; c0 + b < c1; b += 1024) {
		uint32_t at = c0 + b - pg;

		put128(c0 + b, (v4u){ gda_lds32u(fr, at), gda_lds32u(fr, at + 4),
				      gda_lds32u(fr, at + 8),
				      gda_lds32u(fr, at + 12) });
	}
	gda_prof(dbg, GDA_PROF_DECRYPT, t);
	return uniform32(fail);
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

/*
 * A passed packet the wave is on, decrypted in its RX page if it is ESP of an
 * SA in the table: @src at the sixteen bytes it starts in, the packet @off
 * past them and @len long.  Everything but the payload stays as it came - the
 * stack's crypto offload takes the ESP header and trailer off.  Returns its
 * PASS entry's KNOD_PERSIST_PASS_IPSEC* bits, zero for one left alone.
 *
 * GHASH first, over the ciphertext: the sum of each block times the power of
 * H it is from the end, a lane per block, every lane's added up at the end.
 * Then a lane per block again, a kilobyte of payload at a time: each lane
 * decrypts its block into the wave's LDS window over that kilobyte, and the
 * page takes it back sixteen aligned bytes a lane, shifted into place and the
 * frame's own bytes around it.
 */
static INLINE uint32_t gda_ipsec_rx(struct gda *g, GLOBAL uint8_t *src,
				    uint32_t off, uint32_t len,
				    struct gda_sa_cache *sc)
{
	GLOBAL const uint8_t *f = src + off;
	GLOBAL const struct knod_persistent_ipsec_sa *sa;
	LDS uint8_t *buf = gda_ipsec_buf(g);
	LDS const uint32_t *t = gda_ipsec_ttab(g);
	const CONSTANT uint32_t *rk;
	GLOBAL const uint8_t *hpow;
	uint32_t tot, clen, n, m, rounds, salt, iv_lo, iv_hi, spi, seq;
	uint32_t z[8] = { 0 }, acc[4], x[4], c[4], k[4], j, b, i, w, fail;
	uint32_t base, from, to, nb, lane = lane_id();
	LDS uint32_t *fr = gda_ipsec_frame(g);
	uint32_t sh = (off + ESP_DATA) & 3, fo = off + 4 - sh;
	GLOBAL uint32_t *dbg = g->entry->regress_dbg;
	uint32_t t0 = gda_cyc();
	int slot, d, lim;

	/* The frame into LDS in one go, its payload on a word: copy byte fo
	 * is the frame's first.
	 */
	if (len < ESP_DATA + ESP_ICV ||
	    fo + len + 4 + 16 > KNOD_PERSIST_GDA_IPSEC_FRAME_BYTES)
		return 0;
	gda_ipsec_fetch(fr, src, sh, fo + len + 4, lane);
	gda_prof(dbg, GDA_PROF_FETCH, t0);
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
	if (m > 256)
		return 0;
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
	/* Made afresh for each packet: what comes of the lane is cheap, and
	 * kept from one packet to the next it takes registers the multiply
	 * wants.
	 */
	GCM_HERE(lane);

	if (!rounds) {
		seq = uniform32(gda_lds32u(fr, fo + ESP_SPI + 4));
		fail = gda_chacha_rx(fr, (fo + ESP_DATA) / 4, src - 4 + sh,
				     fo + ESP_IV, sc->key, salt, spi, seq, clen,
				     lane, dbg);
		gda_prof(dbg, GDA_PROF_PACKET, t0);
		if (!lane)
			add((gptr)&dbg[GDA_PROF_PKTS], 1);
		return KNOD_PERSIST_PASS_IPSEC |
		       slot << KNOD_PERSIST_PASS_IPSEC_SA_SHIFT |
		       (fail ? KNOD_PERSIST_PASS_IPSEC_FAIL : 0);
	}
	hpow = (GLOBAL const uint8_t *)sc->hpow;
	seq = uniform32(gda_ld32u(f + ESP_SPI + 4));
	iv_lo = uniform32(gda_ld32u(f + ESP_IV));
	iv_hi = uniform32(gda_ld32u(f + ESP_IV + 4));

#pragma clang loop unroll(disable)
	for (j = 1 + lane; j <= n; j += 64) {
		GCM_HERE(j);
		b = 16 * (j - 1);
		nb = clen - b < 16 ? clen - b : 16;
#pragma unroll
		for (i = 0; i < 4; i++) {
			w = gda_ld32u(f + ESP_DATA + b + 4 * i);
			x[i] = aes_bswap32(4 * i >= nb ? 0 :
					   4 * i + 4 <= nb ? w :
					   w & ((1u << (8 * (nb - 4 * i))) - 1));
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

#pragma clang loop unroll(disable)
	for (base = 0; base < clen; base += 1024) {
		lim = clen - base < 1024 ? clen - base : 1024;
		from = (off + ESP_DATA + base) & ~15u;
		to = (off + ESP_DATA + base + lim + 15) & ~15u;
		d = off + ESP_DATA + base - from;

		/* Payload block j, counter j + 1. */
		j = 1 + base / 16 + lane;
		if (j <= n) {
			b = 16 * (j - 1);
			nb = clen - b < 16 ? clen - b : 16;
#pragma unroll
			for (i = 0; i < 4; i++) {
				w = gda_ld32u(f + ESP_DATA + b + 4 * i);
				c[i] = 4 * i >= nb ? 0 :
				       4 * i + 4 <= nb ? w :
				       w & ((1u << (8 * (nb - 4 * i))) - 1);
			}
			gcm_counter(x, salt, iv_lo, iv_hi, j + 1);
			aes_encrypt_block(t, rk, rounds, k, x);
			*(LDS v4u *)(buf + 16 * lane) =
				(v4u){ c[0] ^ k[0], c[1] ^ k[1], c[2] ^ k[2],
				       c[3] ^ k[3] };
		}
		gda_ipsec_put(src, buf, from, to, d, lim, lane);
	}
	stores_out();

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
	fail = 0;
#pragma unroll
	for (i = 0; i < 4; i++)
		fail |= (acc[i] ^ x[i]) !=
			aes_bswap32(gda_ld32u(f + ESP_DATA + clen + 4 * i));

	return KNOD_PERSIST_PASS_IPSEC | slot << KNOD_PERSIST_PASS_IPSEC_SA_SHIFT |
	       (uniform32(fail) ? KNOD_PERSIST_PASS_IPSEC_FAIL : 0);
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
	uint32_t rank = 0, rss = 0, kind;
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
		/* Maps the host changed while we were parked, and anything
		 * else a program reads that someone else writes; once, for the
		 * workgroup, before any wave's program runs.
		 */
		if (!g.wave && run) {
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
		if (n)
			break;

		if (!g.wave && GL(&g, SQ_PC) != GL(&g, SQ_CC)) {
			nic_tx_reap(&g, GL(&g, SQ_PC));
			gda_rq_release(&g);
		}
		idle = GL(&g, IDLE_ROUNDS);
		if (idle < 64) {
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
	return gda_round_begin(ctl, queue, wave, s, pk_lo, pk_hi, 0);
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
 * After the program, before gda_round_end(): the packets that pass decrypted
 * where they are, for gda_round_end() to copy as any other.  Returns each
 * lane's KNOD_PERSIST_PASS_IPSEC* bits, for gda_round_end() to put in its
 * entry.  Apart from it, since the two together want more VGPRs than a call
 * has.
 */
v4u GDA_FN(gda_ipsec)(ctl_t ctl, uint32_t queue, uint32_t wave, uint32_t s,
		      uint32_t pk_lo, uint32_t pk_hi, uint32_t verdict,
		      uint32_t page, uint32_t off, uint32_t len, uint32_t lo,
		      uint32_t hi)
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };
	uint64_t live = (uint64_t)uniform32(hi) << 32 | uniform32(lo);
	uint64_t todo;
	struct gda_sa_cache sc = { .slot = ~0u };
	uint32_t meta = 0;

	ctl = (ctl_t)uniform64((uint64_t)ctl);
	gda_load(&g, ctl, uniform32(queue), uniform32(wave));
	todo = ballot(((live >> lane_id()) & 1) && verdict == XDP_PASS);
	while (todo) {
		uint32_t l = __builtin_ctzll(todo), o, b, n, t = gda_cyc();

		todo &= todo - 1;
		o = __builtin_amdgcn_readlane(off, l);
		n = __builtin_amdgcn_readlane(len, l);
		/* Its LDS copy reads a little past it: not past its page. */
		if (o + n + 32 > 1u << GL(&g, PAGE_SHIFT))
			continue;
		b = gda_ipsec_rx(&g, (GLOBAL uint8_t *)GL64(&g, RX_BASE) +
				 ((uint64_t)__builtin_amdgcn_readlane(page, l) <<
				  GL(&g, PAGE_SHIFT)) + (o & ~15u),
				 o & 15, n, &sc);
		if (lane_id() == l)
			meta = b;
		gda_prof(g.entry->regress_dbg, GDA_PROF_ITER, t);
	}
	stores_out();
	return (v4u){ meta, 0, 0, 0 };
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
		g.meet->p[g.wave] = __builtin_popcountll(pass);
	}
	if (!g.wave) {
		pc = GL(&g, SQ_PC);
		if (GL(&g, SQ_MASK) && pc != GL(&g, SQ_CC))
			nic_tx_reap(&g, pc);
		cc = GL(&g, SQ_CC);
		/* A dead SQ has room for none: the waves see it full. */
		if (GL(&g, TX_DEAD))
			cc = pc - GL(&g, SQ_MASK) - 1;
		/* The PASS slots free: what the host had finished with when
		 * last looked, and a look past the ring for more only when
		 * that might not do for a whole round.
		 */
		ppc = GL(&g, PASS_PC);
		pcc = GL(&g, PASS_CC);
		if (GL(&g, PASS_MASK) + 1 - (ppc - pcc) < 64 * GL(&g, WAVES)) {
			pcc = get32((GLOBAL uint8_t *)GL64(&g, PASS_RING) +
				    8 * (GL(&g, PASS_MASK) + 1) +
				    KNOD_PERSIST_PASS_CC);
			SET(&g, PASS_CC, pcc);
		}
		if (!lane_id()) {
			g.meet->sq_pc = pc;
			g.meet->sq_cc = cc;
			g.meet->pass_pc = ppc;
			g.meet->pass_room = GL(&g, PASS_MASK) + 1 - (ppc - pcc);
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
	slot = (ppc + pbase + rank(pass)) & GL(&g, PASS_MASK);
	copy = ballot(((pass >> lane_id()) & 1) && pbase + rank(pass) < room);
	while (copy) {
		uint32_t l = __builtin_ctzll(copy), from, to, b;
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
		for (b = 16 * lane_id(); b < to - from; b += 1024)
			put128(dst + b, get128(src + b));
	}
	if (((pass >> lane_id()) & 1) && pbase + rank(pass) < room)
		put64((GLOBAL uint64_t *)GL64(&g, PASS_RING) + slot,
		      (uint64_t)(off | len << 16) << 32 | nic_rx_csum(&g, pos) |
		      meta);

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
		if (x.given) {
			SET64(&g, TX_PKTS, GL64(&g, TX_PKTS) + x.given);
			pc += x.given;
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
	gda_prof(g.entry->regress_dbg, GDA_PROF_END, t0);
	if (!lane_id())
		add((gptr)&g.entry->regress_dbg[GDA_PROF_END_CALLS], 1);
#endif
	return (v4u){ g.s, g.packets, g.packets >> 32, 0 };
}
