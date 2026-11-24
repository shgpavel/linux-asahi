// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/string.h>

#include "fw_27_0_1.h"

static u64 fw27_init_message(u64 root)
{
	return FW27_MSG_INIT << 48 | (root & GENMASK_ULL(43, 0));
}

static u64 fw27_work_kick(u32 priority, enum agx_stage stage)
{
	return FW27_MSG_WORK << 48 | (priority & 7) << 2 | stage;
}

static u64 fw27_control_kick(void)
{
	return FW27_MSG_CONTROL << 48 | 0x11;
}

static const struct agx_fw_sizes fw27_sizes = {
	.root = sizeof(struct fw27_root),
	.main = sizeof(struct fw27_main),
	.hwdata = sizeof(struct fw27_hwdata),
	.power = sizeof(struct fw27_power),
	.region_c = sizeof(struct fw27_region_c),
	.status_a = sizeof(struct fw27_status_a),
	.status_b = sizeof(struct fw27_status_b),
	.extra0 = sizeof(struct fw27_extra0),
	.control_ring = FW27_CTRL_ENTRIES * sizeof(struct fw27_control),
	.handoff = sizeof(struct fw27_handoff),
	.queue = sizeof(struct fw27_queue),
	.queue_pointers = sizeof(struct fw27_queue_pointers),
	.queue_context = sizeof(struct fw27_queue_context),
	.queue_jobs = sizeof(struct fw27_queue_jobs),
	.channel_msg = sizeof(struct fw27_channel_msg),
	.control_msg = sizeof(struct fw27_control),
	.event_msg = sizeof(struct fw27_event),
	.item = sizeof(struct fw27_barrier),
	.notifier = sizeof(struct fw27_notifier),
	.flist = sizeof(struct fw27_flist),
	.compute_entry = sizeof(struct fw27_compute_entry),
	.ta_work = sizeof(struct fw27_ta_work),
	.fragment_work = sizeof(struct fw27_fragment_work),
	.compute_work = sizeof(struct fw27_compute_work),
	.ta_microsequence = sizeof(struct fw27_ta_msq),
	.fragment_microsequence = sizeof(struct fw27_fragment_msq),
	.compute_microsequence = sizeof(struct fw27_compute_msq),
};

static const struct agx_fw_ops fw27_ops = {
	.windows = fw27_windows,
	.handoff = fw27_handoff,
	.handoff_ready = fw27_handoff_ready,
	.graph = fw27_graph,
	.map_notify = fw27_map_notify,
	.init_message = fw27_init_message,
	.work_kick = fw27_work_kick,
	.control_kick = fw27_control_kick,
	.opening_flist = fw27_opening_flist,
	.opening_control = fw27_opening_control,
	.destroy_context = fw27_destroy_context,
	.queue = fw27_queue,
	.queue_pointers = fw27_queue_pointers,
	.queue_context = fw27_queue_context,
	.queue_jobs = fw27_queue_jobs,
	.barrier = fw27_barrier,
	.dependency = fw27_dependency,
	.ta_prelude = fw27_ta_prelude,
	.fragment_prelude = fw27_fragment_prelude,
	.channel_msg = fw27_channel_msg,
	.notifier = fw27_notifier,
	.render_check = fw27_render_check,
	.ta_work = fw27_ta_work,
	.fragment_work = fw27_fragment_work,
	.ta_microsequence = fw27_ta_microsequence,
	.fragment_microsequence = fw27_fragment_microsequence,
	.compute_check = fw27_compute_check,
	.compute_entry = fw27_compute_entry,
	.compute_work = fw27_compute_work,
	.compute_microsequence = fw27_compute_microsequence,
};

static const struct agx_fw fw27_b000 = {
	.name = "27.0.1 b000",
	.uuid = "F0160C04-0B3C-326C-AAE7-09530AB8A7C0",
	.build = "mBoot-20457.1.29",
	.private_va = 0xfffffc0000000000,
	.private_size = 0x2000000000,
	.sizes = &fw27_sizes,
	.ops = &fw27_ops,
};

const struct agx_fw *agx_fw_find(const char *uuid)
{
	return strcmp(uuid, fw27_b000.uuid) ? NULL : &fw27_b000;
}
