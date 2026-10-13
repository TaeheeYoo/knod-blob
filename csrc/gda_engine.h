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
 * whose packet is still waiting to be sent or copied out.
 */
static INLINE void gda_rq_release(struct gda *g)
{
	uint32_t ci = GL(g, CI), pc = GL(g, SQ_PC), cc = GL(g, SQ_CC);
	uint32_t bound = ci, ppc, pcc, floor, cand = 0, prev;

	if (pc != cc)
		bound = get32(g->ring + KNOD_PERSIST_RING_RQPOS_OFF +
			      4 * (cc & GL(g, SQ_MASK)));

	/* The oldest packet the host is still copying out, if that is older. */
	ppc = GL(g, PASS_PC);
	pcc = GL(g, PASS_CC);
	if (ppc != pcc) {
		pcc = get32(&g->entry->pass_cc);
		floor = GL(g, PASS_FLOOR);
		if ((int32_t)(pcc - floor) < 0)
			pcc = floor;
		SET(g, PASS_CC, pcc);
		if (ppc != pcc) {
			cand = get32(g->ring + KNOD_PERSIST_RING_PASS_RQPOS_OFF +
				     4 * (pcc & GL(g, PASS_MASK)));
			if (ci - cand > ci - bound)
				bound = cand;
		}
	}

	/* Debug: the bound only ever moves on; count one that goes back. */
	prev = GL(g, LAST_BOUND);
	SET(g, LAST_BOUND, bound);
	if ((int32_t)(bound - prev) < 0)
		gda_regress_dbg(g, bound, prev, pcc, cand);

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
	put32(&e->pass_floor, GL(g, PASS_FLOOR));
	put32(&e->rq_bound, 0);
	stores_out();
}

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
	uint32_t mask, pcc, floor, waves;
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
	SET(&g, PASS_MASK, get32(&e->pass_mask));
	SET(&g, PASS_PC, get32(&e->pass_pc));
	pcc = get32(&e->pass_cc);
	floor = get32(&e->pass_floor);
	SET(&g, PASS_FLOOR, floor);
	SET(&g, PASS_CC, (int32_t)(pcc - floor) < 0 ? floor : pcc);
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
		/* What the host still copies out came in on the last build's
		 * RQ.
		 */
		SET(&g, PASS_FLOOR, GL(&g, PASS_PC));
		SET(&g, PASS_CC, GL(&g, PASS_PC));
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

		if (!g.wave &&
		    (GL(&g, PASS_PC) != GL(&g, PASS_CC) ||
		     GL(&g, SQ_PC) != GL(&g, SQ_CC))) {
			if (GL(&g, SQ_PC) != GL(&g, SQ_CC))
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
		      uint32_t hi)
{
	struct gda g = { .s = s, .packets = (uint64_t)pk_hi << 32 | pk_lo };
	uint64_t live = (uint64_t)uniform32(hi) << 32 | uniform32(lo);
	uint32_t pc, cc, left, ppc, pbase = 0, ptotal = 0, i, p, n, pos;
	int in = (live >> lane_id()) & 1;
	uint64_t tx, pass;
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
		if (!lane_id()) {
			g.meet->sq_pc = pc;
			g.meet->sq_cc = cc;
			g.meet->pass_pc = GL(&g, PASS_PC);
		}
	}
	barrier();

	pc = g.meet->sq_pc;
	cc = g.meet->sq_cc;
	ppc = g.meet->pass_pc;
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

	/* One PASS entry per lane that passes: the page, where the packet is
	 * in it and how long; and the RQ position it holds until the host is
	 * done.
	 */
	if ((pass >> lane_id()) & 1) {
		uint32_t slot = (ppc + pbase + rank(pass)) & GL(&g, PASS_MASK);

		put64((GLOBAL uint64_t *)GL64(&g, PASS_RING) + slot,
		      (uint64_t)(off | len << 16) << 32 | page);
		put32(g.ring + KNOD_PERSIST_RING_PASS_RQPOS_OFF + 4 * slot,
		      pos);
	}

	/* Every wave's entries are in memory before wave 0 says they are. */
	stores_out();
	barrier();

	if (!g.wave) {
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
	return (v4u){ g.s, g.packets, g.packets >> 32, 0 };
}
