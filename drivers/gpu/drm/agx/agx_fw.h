/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __AGX_FW_H__
#define __AGX_FW_H__

#include <linux/types.h>

#include "agx_board.h"
#include "agx_job.h"

#define AGX_NR_PRIORITIES	4
#define AGX_NR_STAGES		3
#define AGX_NR_WORK_CHANNELS	(AGX_NR_PRIORITIES * AGX_NR_STAGES)
#define AGX_NR_CONTEXT_STATE	4
#define AGX_NR_WINDOWS		19

enum agx_stage {
	AGX_STAGE_TA,
	AGX_STAGE_FRAGMENT,
	AGX_STAGE_COMPUTE,
};

enum agx_window_access {
	AGX_WINDOW_RO,
	AGX_WINDOW_RW,
};

struct agx_window {
	u64 phys;
	u64 va;
	u32 size;
	u32 stride;
	u32 offset;
	u16 slot;
	u16 access;
};

struct agx_graph_va {
	u64 root;
	u64 main;
	u64 region_a;
	u64 region_c;
	u64 status_a;
	u64 status_b;
	u64 extra0;
	u64 extra1;
	u64 hwdata;
	u64 pipe;
	u64 power;
	u64 power_aux;
	u64 stats[4];

	u64 tail_cursors;
	u64 done_cursors;
	u64 work_ring[AGX_NR_WORK_CHANNELS];
	u64 control_ring;
	u64 event_state;
	u64 event_ring;
	u64 log_state;
	u64 log_ring;
	u64 trace_state;
	u64 trace_ring;
	u64 stats_state;
	u64 stats_ring;
	u64 channel15;

	u64 region_2c8;
	u64 slot_table;
	u64 slot_table_gpu;
	u64 shared;
	u64 shared_gpu;

	u64 timestamps;
	u64 windows;
};

struct agx_graph_mem {
	void *root;
	void *main;
	void *hwdata;
	void *power;
	void *region_c;
	void *status_a;
	void *status_b;
	void *extra0;
	void *control_ring;
};

struct agx_fw_sizes {
	u32 root;
	u32 main;
	u32 hwdata;
	u32 power;
	u32 region_c;
	u32 status_a;
	u32 status_b;
	u32 extra0;
	u32 control_ring;
	u32 handoff;
	u32 queue;
	u32 queue_pointers;
	u32 queue_context;
	u32 queue_jobs;
	u32 channel_msg;
	u32 control_msg;
	u32 event_msg;
	u32 item;
	u32 notifier;
	u32 flist;
	u32 compute_entry;
	u32 ta_work;
	u32 fragment_work;
	u32 compute_work;
	u32 ta_microsequence;
	u32 fragment_microsequence;
	u32 compute_microsequence;
};

struct agx_queue_va {
	u64 pointers;
	u64 ring;
	u64 jobs;
	u64 private;
	u64 context;
	u32 uuid;
	u32 priority;
};

struct agx_wait {
	u64 stamp;
	u32 value;
	u32 event;
	u32 stamp_self;
	u32 uuid;
	u32 kind;
};

struct agx_channel_kick {
	u64 queue;
	u64 tag;
	u16 head;
	u8 event;
	u8 stage;
	bool first;
};

struct agx_flist_va {
	u64 flist;
	u64 aux;
	u64 directory;
	u64 table;
	u32 entries;
	u32 pages;
};

struct agx_fw_ops {
	int (*windows)(const struct agx_board *board, u64 base, struct agx_window *out);
	void (*handoff)(void *page);
	int (*graph)(const struct agx_board *board, const struct agx_graph_va *va,
		     u32 entropy, const struct agx_graph_mem *mem);
	void (*map_notify)(void *status_b, u64 state, u64 ring);
	u64 (*init_message)(u64 root);
	u64 (*work_kick)(u32 priority, enum agx_stage stage);
	u64 (*control_kick)(void);

	void (*opening_flist)(void *out, const struct agx_flist_va *va);
	void (*opening_control)(void *out, const struct agx_flist_va *va);
	void (*destroy_context)(void *out, u64 context, const u8 state[AGX_NR_CONTEXT_STATE]);

	int (*queue)(void *out, const struct agx_queue_va *va);
	void (*queue_pointers)(void *out, u32 capacity, u32 write);
	void (*queue_context)(void *out);
	void (*queue_jobs)(void *out, u64 self);
	void (*barrier)(void *out, const struct agx_wait *wait);
	void (*dependency)(void *out, const struct agx_wait *wait);
	void (*ta_prelude)(void *out, const struct agx_render_hw *hw, u32 blocks);
	void (*fragment_prelude)(void *out, const struct agx_render_hw *hw);
	void (*channel_msg)(void *out, const struct agx_channel_kick *kick);
	void (*notifier)(void *out, u64 threshold, u32 generation, u32 count, u32 context);

	int (*render_check)(const struct agx_render *job, const struct agx_render_hw *hw);
	void (*ta_work)(void *out, const struct agx_render *job, const struct agx_render_hw *hw);
	void (*fragment_work)(void *out, const struct agx_render *job,
			      const struct agx_render_hw *hw);
	void (*ta_microsequence)(void *out, const struct agx_render_hw *hw);
	void (*fragment_microsequence)(void *out, const struct agx_render_hw *hw);

	int (*compute_check)(const struct agx_compute *job, const struct agx_compute_hw *hw);
	void (*compute_entry)(void *out, u64 stream);
	void (*compute_work)(void *out, const struct agx_compute *job,
			     const struct agx_compute_hw *hw);
	void (*compute_microsequence)(void *out, const struct agx_compute *job,
				      const struct agx_compute_hw *hw);
};

struct agx_fw {
	const char *name;
	const char *uuid;
	const char *build;
	u64 private_va;
	u64 private_size;
	const struct agx_fw_sizes *sizes;
	const struct agx_fw_ops *ops;
};

const struct agx_fw *agx_fw_find(const char *uuid);

#endif
