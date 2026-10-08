// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bpf_xdp_adjust_head() and bpf_xdp_adjust_tail().
 *
 * A packet may move its start or its end anywhere inside its frame: from the
 * headroom's start to ETH_HLEN short of its end, or from ETH_HLEN past its
 * start to the frame's end less the tailroom the stack needs.  The queue's
 * descriptor says where the frame is around the offset the packet arrived
 * at, and a move outside it, or a queue with none, fails and moves nothing.
 *
 * The JIT passes the parameter block, the page, the packet's start and end,
 * the offset it arrived at, the move and the tailroom, and takes back r0 and
 * the start or end.
 */
#include "map.h"

#define ETH_HLEN	14
#define EINVAL		22

typedef uint32_t v4u __attribute__((ext_vector_type(4)));

/* Where the frame starts, and through @frame its size; false if the queue
 * describes none, or the packet arrived where the frame cannot be.
 */
static inline int frame_start(uint64_t param, uint64_t page, uint32_t off,
			      uint64_t *start, uint32_t *frame)
{
	const CONSTANT uint32_t *q = (const CONSTANT uint32_t *)
		(uniform64(param) + KNOD_BLOB_PARAM_QUEUES +
		 KNOD_BLOB_QUEUE_RX_BOUNDS +
		 KNOD_BLOB_QUEUE_SIZE * __builtin_amdgcn_workgroup_id_y());
	uint32_t bounds = *q, headroom = bounds & 0xffff;

	*frame = bounds >> 16;
	*start = page + (uint32_t)(off - headroom);
	return bounds && off >= headroom;
}

static inline v4u result(int ok, uint64_t moved, uint64_t was)
{
	uint64_t r0 = ok ? 0 : (uint64_t)-EINVAL, p = ok ? moved : was;

	return (v4u){ r0, r0 >> 32, p, p >> 32 };
}

v4u cfn_xdp_adjust_head(uint64_t param, uint64_t page, uint64_t data,
			uint64_t end, uint32_t off, uint32_t delta,
			uint32_t tailroom)
{
	uint64_t start, moved = data + (int64_t)(int32_t)delta;
	uint32_t frame;
	int ok = frame_start(param, page, off, &start, &frame);

	ok = ok && moved >= start && moved <= end - ETH_HLEN;
	return result(ok, moved, data);
}

v4u cfn_xdp_adjust_tail(uint64_t param, uint64_t page, uint64_t data,
			uint64_t end, uint32_t off, uint32_t delta,
			uint32_t tailroom)
{
	uint64_t start, moved = end + (int64_t)(int32_t)delta;
	uint32_t frame;
	int ok = frame_start(param, page, off, &start, &frame);

	ok = ok && moved >= data + ETH_HLEN &&
	     moved <= start + (uint32_t)(frame - tailroom);
	return result(ok, moved, end);
}
