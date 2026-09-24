/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _KNOD_PERSISTENT_H
#define _KNOD_PERSISTENT_H
/* One workgroup per RX queue; no X dimension multiplier. */
#define KNOD_PERSIST_VERSION 0x4b500008
#define KNOD_PERSIST_SLOTS 3
#define KNOD_PERSIST_SLOT_BASE 64
#define KNOD_PERSIST_SLOT_BYTES 512
#define KNOD_PERSIST_READY 0
#define KNOD_PERSIST_PARAM 8
#define KNOD_PERSIST_DONE 64
#define KNOD_PERSIST_MAX_QUEUES 32
#define KNOD_PERSIST_STOP 4
/* GDA stage 2: nonzero asks every queue to park at its next round boundary
 * and ack with the same value; see KNOD_PERSIST_GDA_PAUSE_ACK.
 */
#define KNOD_PERSIST_PAUSE 8
/* GDA stage 2: the parameter block a program runs against, fixed for the
 * shader's lifetime rather than one per batch.
 */
#define KNOD_PERSIST_GDA_PARAM 16
/* u64 per queue: where this queue's NIC TX doorbell is in the GPU's address
 * space.  Written once when a shader lifetime starts, zero when the NIC
 * publishes none.  Sits past the slots, so no slot offset moves.
 */
#define KNOD_PERSIST_TX_DB 1600
/* 16 bytes per queue: the doorbell value the host left for the shader to
 * ring, then the last one the shader rang.  Each field has one writer, so
 * no atomics: the shader rings whenever the two differ.
 */
#define KNOD_PERSIST_TX_KICK 1856
#define KNOD_PERSIST_KICK_BYTES 16
/* GDA stage 2: what the receive kernel needs to run a queue's rings, one
 * entry per queue, and the two counts it reports back in the same entry.
 * live zero means the queue's rings are not the accel's; its wave ends.
 */
#define KNOD_PERSIST_GDA 2368
#define KNOD_PERSIST_GDA_BYTES 128
#define KNOD_PERSIST_GDA_RING 0
#define KNOD_PERSIST_GDA_RX_DMA 8
#define KNOD_PERSIST_GDA_PACKETS 16
#define KNOD_PERSIST_GDA_ERRORS 24
#define KNOD_PERSIST_GDA_RQ_LOG 32
#define KNOD_PERSIST_GDA_RQ_LOG_STRIDE 36
#define KNOD_PERSIST_GDA_CQ_LOG 40
#define KNOD_PERSIST_GDA_FRAG 44
#define KNOD_PERSIST_GDA_HEADROOM 48
#define KNOD_PERSIST_GDA_MKEY 52
#define KNOD_PERSIST_GDA_LIVE 56
/* Which build of the rings this is; the NIC moves it every time it builds
 * them.  The ring state below outlives a shader, and only rings of the same
 * generation can pick it up.
 */
#define KNOD_PERSIST_GDA_GEN 60
#define KNOD_PERSIST_GDA_RX_BASE 64	/* the RX buffer, page 0 */
#define KNOD_PERSIST_GDA_BDS 72		/* per-lane descriptors for the program */
#define KNOD_PERSIST_GDA_CI 80		/* CQ consumer index, carried over */
#define KNOD_PERSIST_GDA_POSTED_GEN 84	/* the generation the RQ was posted for */
#define KNOD_PERSIST_GDA_PAUSE_ACK 88
#define KNOD_PERSIST_BYTES 8192
/* The ring buffer's layout, as net/knod.h lays it out for the NIC (the kernel
 * checks the two agree): doorbell records, then the RQ, then its CQ, then the
 * per-lane descriptors a program's bounds are read from.
 */
#define KNOD_PERSIST_RING_RQ_DB 0
#define KNOD_PERSIST_RING_CQ_DB 64
#define KNOD_PERSIST_RING_RQ_OFF 4096
#define KNOD_PERSIST_RING_CQ_OFF (4096 + 8192 * 64)
#define KNOD_PERSIST_RING_BDS_OFF (KNOD_PERSIST_RING_CQ_OFF + 8192 * 64)
#ifndef __ASSEMBLY__
#include <linux/types.h>
struct knod_persistent_slot {
	u64 ready;
	u64 param;
	u8 reserved[48];
	u64 done[KNOD_PERSIST_MAX_QUEUES];
	u8 pad[192];
};

struct knod_persistent_kick {
	u64 pending;
	u64 rung;
};

struct knod_persistent_gda {
	u64 ring;
	u64 rx_dma;
	u64 packets;
	u64 errors;
	u32 rq_log;
	u32 rq_log_stride;
	u32 cq_log;
	u32 frag;
	u32 headroom;
	u32 mkey_be;
	u32 live;
	u32 gen;
	u64 rx_base;
	u64 bds;
	u32 ci;
	u32 posted_gen;
	u32 pause_ack;
	u32 pad;
	u8 reserved[32];
};

struct knod_persistent_control {
	u32 version;
	u32 stop;
	u32 pause;
	u32 pad0;
	u64 gda_param;
	u8 reserved[40];
	struct knod_persistent_slot slots[KNOD_PERSIST_SLOTS];
	u64 tx_db[KNOD_PERSIST_MAX_QUEUES];
	struct knod_persistent_kick tx_kick[KNOD_PERSIST_MAX_QUEUES];
	struct knod_persistent_gda gda[KNOD_PERSIST_MAX_QUEUES];
};
#endif
#endif
