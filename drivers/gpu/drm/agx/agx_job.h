/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __AGX_JOB_H__
#define __AGX_JOB_H__

#include <linux/types.h>

struct agx_render {
	u64 context_base;
	u64 encoder;
	u64 tilemap;
	u64 tpc;
	u64 heapmeta;
	u64 layermeta;
	u64 deflake[3];

	u32 width;
	u32 height;
	u32 layers;
	u32 samples;
	u32 utile_width;
	u32 utile_height;
	u32 tib_blocks;
	u32 scene;

	u64 utile_config;
	u64 tile_config;
	u64 multisample_control;
	u64 ppp_control;
	u64 completion_control;
	u64 cycle;

	u64 sampler_array;
	u32 nr_samplers;
	u64 sample_size;

	u64 load_pipeline;
	u64 load_pipeline_bind;
	u64 store_pipeline;
	u64 store_pipeline_bind;
	u64 partial_load_pipeline;
	u64 partial_load_pipeline_bind;
	u64 partial_store_pipeline;
	u64 partial_store_pipeline_bind;

	u64 merge_upper_x;
	u64 merge_upper_y;
	u64 scissor_array;
	u64 depth_bias_array;
	u64 occlusion_query_base;
	u64 aux_fb;
	u64 aux_fb_flags;
	u64 aux_fb_pages;

	u64 depth_flags;
	u64 depth_dimensions;
	u64 depth_clear;
	u64 depth_buffer;
	u64 depth_stride;
	u64 depth_aux_buffer;
	u64 depth_aux_stride;
	u64 stencil_clear;
	u64 stencil_buffer;
	u64 stencil_stride;
	u64 stencil_aux_buffer;
	u64 stencil_aux_stride;

	u64 ta_admission;
	u64 ta_gate;
	u64 ta_identity;
	u64 ta_work_stamp;
	u64 ta_status;
	u64 fragment_admission;
	u64 fragment_gate;
	u64 fragment_identity;
	u64 fragment_work_stamp;
	u64 fragment_status;

	bool request_growth;
	bool sync_grow;
	bool process_empty_tiles;
};

struct agx_stage_va {
	u64 work;
	u64 microsequence;
	u64 register_alias;
	u64 queue;
	u64 stats;
	u64 driver_stamp;
	u64 fw_stamp;
	u64 status_page;
	u64 shared_tail;
	u64 user_time_start;
	u64 user_time_end;
	u64 counter;
	u32 stamp;
	u32 event;
	u32 uuid;
};

struct agx_render_hw {
	struct agx_stage_va ta;
	struct agx_stage_va fragment;
	u64 notifier;
	u64 time_start;
	u64 time_end;
	u64 pool;
	u64 scene;
	u64 empty;
	u64 support;
	u32 pool_slot;
	u32 context;
};

struct agx_compute {
	u64 stream;
	u64 stream_end;
	u64 sampler_array;
	u32 nr_samplers;
	u64 scratch;
	u64 marker;
	u64 save_area;
};

struct agx_compute_hw {
	u64 work;
	u64 microsequence;
	u64 register_alias;
	u64 entry;
	u64 queue;
	u64 stats;
	u64 support;
	u64 notifier;
	u64 driver_stamp;
	u64 fw_stamp;
	u64 time_start;
	u64 time_end;
	u64 identity;
	u64 register_identity;
	u64 counter;
	u32 generation;
	u32 context;
	u32 stamp;
	u32 event;
	u32 ordinal;
};

#endif
