// SPDX-License-Identifier: GPL-2.0-only

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "fw_27_0_1.h"

#define FW27_TA_REGS_COUNTED		65
#define FW27_FRAGMENT_REGS_COUNTED	81
#define FW27_TA_MSQ_RESTART		0x190
#define FW27_TA_MSQ_BACK		0x268
#define FW27_FRAGMENT_MSQ_RESTART	0x1b0
#define FW27_FRAGMENT_MSQ_BACK		0x288
#define FW27_COMPUTE_MSQ_RESTART	0x160
#define FW27_COMPUTE_MSQ_BACK		0x258
#define FW27_MERGE_NUMERATOR		0x3fddb3d9
#define FW27_NR_SCENES			80
#define FW27_MAX_TIB_BLOCKS		128

struct fw27_geometry {
	u64 tiles_x;
	u64 tiles_y;
	u64 size1;
	u64 size2;
	u64 size3;
	u64 x_blocks;
	u64 y_blocks;
	u64 screen;
	u64 pixels;
	u64 macrotiles;
};

struct fw27_render_values {
	u64 tilemap;
	u64 tilemap_offset;
	u64 tilemap_size;
	u64 tpc_offset;
	u64 heapmeta;
	u64 heapmeta_offset;
	u64 layermeta;
	u64 layermeta_offset;
	u64 deflake1;
	u64 deflake2;
	u64 deflake3;
	u64 encoder_offset;
	u64 size1;
	u64 size2;
	u64 size3;
	u64 x_blocks;
	u64 y_blocks;
	u64 screen;
	u64 pixels;
	u64 macrotiles;
	u64 target_layers;
	u64 target_layers_low;
	u64 record_index;
	u64 tile_state;
	u64 tib;
	u64 size;
	u64 utile_config;
	u64 tile_config;
	u64 multisample_control;
	u64 ppp_control;
	u64 completion_control;
	u64 cycle;
	u64 load_pipeline;
	u64 load_pipeline_bind;
	u64 store_pipeline;
	u64 store_pipeline_bind;
	u64 merge_x;
	u64 merge_y;
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
};

struct fw27_compute_values {
	u64 save_area;
	u64 save_1480;
	u64 save_1488;
	u64 save_1490;
	u64 save_1498;
	u64 entry;
	u64 identity;
};

#define FW27_REG_CONST	U16_MAX

struct fw27_reg {
	u32 number;
	u16 source;
	u64 constant;
};

#define VAL(reg, value)	{ .number = (reg), .source = FW27_REG_CONST, .constant = (value) }
#define TA(reg, field)	{ .number = (reg), .source = offsetof(struct fw27_render_values, field) }
#define FRAG(reg, field)	TA(reg, field)
#define CDM(reg, field)	{ .number = (reg), .source = offsetof(struct fw27_compute_values, field) }

static const struct fw27_reg fw27_ta_regs[] = {
	VAL(0x01748, 1),
	VAL(0x10141, 512),
	TA(0x1c039, tilemap_offset),
	TA(0x1c9c8, tilemap_offset),
	TA(0x1c0a1, tpc_offset),
	TA(0x1c031, heapmeta_offset),
	TA(0x1c9c0, heapmeta_offset),
	VAL(0x1c051, 0x3a0012006b0003),
	VAL(0x1c061, 1),
	TA(0x10149, utile_config),
	TA(0x10139, multisample_control),
	TA(0x10111, deflake1),
	TA(0x1c9b0, deflake1),
	TA(0x10119, deflake2),
	TA(0x1c9b8, deflake2),
	VAL(0x1c958, 1),
	TA(0x1c950, deflake3),
	VAL(0x1c930, 0),
	TA(0x1c880, encoder_offset),
	TA(0x1c079, layermeta_offset),
	TA(0x1c9d8, layermeta_offset),
	VAL(0x10151, 0),
	VAL(0x1c199, 0),
	VAL(0x1c1a1, 0),
	VAL(0x1c1a9, 0),
	VAL(0x1c1b1, 0),
	VAL(0x1c1b9, 0),
	VAL(0x1c8f8, 0x8860),
	TA(0x1c0b1, size1),
	TA(0x1c850, size1),
	VAL(0x10131, 0x88),
	TA(0x10121, ppp_control),
	TA(0x10129, pixels),
	TA(0x101b9, screen),
	TA(0x1c069, x_blocks),
	TA(0x1c071, y_blocks),
	TA(0x1c081, size2),
	TA(0x1c0a9, size3),
	VAL(0x10171, 256),
	TA(0x10169, target_layers),
	VAL(0x0a309, 0),
	VAL(0x1c8e0, U64_MAX),
	VAL(0x1c8e8, U64_MAX),
	VAL(0x1c898, 0),
	VAL(0x101e1, 28),
	TA(0x1c9e8, target_layers_low),
	VAL(0x1a099, 0),
	VAL(0x1a0a1, 0),
	VAL(0x1a069, 0),
	VAL(0x1a071, 0),
	VAL(0x1a0c9, 0),
	VAL(0x1a0d1, 0),
	VAL(0x101c9, 0),
	VAL(0x0d471, 0),
	VAL(0x1a0f1, 8),
	VAL(0x10799, 0xff0000),
	TA(0x1c830, ta_admission),
	TA(0x1ca30, cycle),
	TA(0x16c39, cycle),
	TA(0x1c910, record_index),
	TA(0x0a5a1, ta_gate),
	VAL(0x0d419, 0x200000001),
	TA(0x1ca10, ta_identity),
	TA(0x014a1, ta_identity),
	TA(0x0a349, ta_identity),
	TA(0x10209, ta_work_stamp),
	TA(0x1c9f0, ta_work_stamp),
	TA(0x14320, ta_work_stamp),
	TA(0x14308, completion_control),
	TA(0x14318, ta_status),
	VAL(0x01740, 1),
};
static_assert(ARRAY_SIZE(fw27_ta_regs) == 71);

static const struct fw27_reg fw27_fragment_regs[] = {
	VAL(0x01739, 1),
	FRAG(0x10009, utile_config),
	FRAG(0x15379, store_pipeline_bind),
	FRAG(0x15381, store_pipeline),
	FRAG(0x15369, load_pipeline_bind),
	FRAG(0x15371, load_pipeline),
	FRAG(0x15131, merge_x),
	FRAG(0x15139, merge_y),
	VAL(0x100a1, 0),
	VAL(0x15069, 0),
	VAL(0x15071, 0),
	VAL(0x16058, 0),
	FRAG(0x10019, multisample_control),
	FRAG(0x100b1, macrotiles),
	FRAG(0x16030, macrotiles),
	FRAG(0x100d9, screen),
	VAL(0x0a301, 0),
	VAL(0x10791, 0xff0200),
	FRAG(0x16098, heapmeta),
	FRAG(0x15109, scissor_array),
	FRAG(0x15101, depth_bias_array),
	FRAG(0x15021, aux_fb_flags),
	FRAG(0x15211, size),
	FRAG(0x15049, aux_fb_pages),
	FRAG(0x10051, tib),
	FRAG(0x15321, depth_dimensions),
	FRAG(0x15301, depth_clear),
	FRAG(0x15309, stencil_clear),
	FRAG(0x15311, occlusion_query_base),
	FRAG(0x15319, depth_flags),
	VAL(0x15349, 0x4040404),
	VAL(0x15351, 0),
	FRAG(0x15329, depth_buffer),
	FRAG(0x15331, depth_buffer),
	FRAG(0x15339, stencil_buffer),
	FRAG(0x15341, stencil_buffer),
	VAL(0x15231, 0),
	VAL(0x15221, 0),
	VAL(0x15239, 0),
	VAL(0x15229, 0),
	FRAG(0x15401, depth_stride),
	FRAG(0x15421, depth_stride),
	FRAG(0x15409, stencil_stride),
	FRAG(0x15429, stencil_stride),
	FRAG(0x153c1, depth_aux_buffer),
	FRAG(0x15411, depth_aux_stride),
	FRAG(0x153c9, depth_aux_buffer),
	FRAG(0x15431, depth_aux_stride),
	FRAG(0x153d1, stencil_aux_buffer),
	FRAG(0x15419, stencil_aux_stride),
	FRAG(0x153d9, stencil_aux_buffer),
	FRAG(0x15439, stencil_aux_stride),
	FRAG(0x16429, tilemap),
	FRAG(0x16060, layermeta),
	FRAG(0x16431, tilemap_size),
	FRAG(0x10039, tile_config),
	VAL(0x16020, 0),
	VAL(0x16451, 0),
	VAL(0x15359, 0),
	VAL(0x100b8, 0x8860),
	FRAG(0x16461, aux_fb),
	FRAG(0x16090, aux_fb),
	VAL(0x101e9, 28),
	VAL(0x160a8, 0),
	FRAG(0x16068, tile_state),
	VAL(0x1a0a9, 0),
	VAL(0x1a0b1, 0),
	VAL(0x1a079, 0),
	VAL(0x1a081, 0),
	VAL(0x1a0d9, 0),
	VAL(0x1a0e1, 0),
	VAL(0x101c1, 0),
	VAL(0x0d469, 0),
	VAL(0x1a0f9, 8),
	FRAG(0x0a5a9, fragment_gate),
	VAL(0x0d429, 0x200000001),
	FRAG(0x160e0, fragment_identity),
	FRAG(0x01499, fragment_identity),
	FRAG(0x0a341, fragment_identity),
	FRAG(0x1c838, fragment_admission),
	FRAG(0x1ca28, cycle),
	FRAG(0x10211, fragment_work_stamp),
	FRAG(0x10420, fragment_work_stamp),
	FRAG(0x14048, completion_control),
	FRAG(0x14080, fragment_status),
	VAL(0x01731, 1),
	VAL(0x01731, 1),
};
static_assert(ARRAY_SIZE(fw27_fragment_regs) == 87);

static const struct fw27_reg fw27_compute_regs[] = {
	CDM(0x1a510, save_area),
	CDM(0x1a420, entry),
	CDM(0x1a4d0, save_1480),
	CDM(0x1a4d8, save_1488),
	CDM(0x1a4e0, save_1490),
	CDM(0x1a4e8, save_1498),
	VAL(0x1a440, 0x154024201),
	VAL(0x1a458, 0x10c08ae0),
	VAL(0x101d9, 0x1c),
	VAL(0x1a089, 0),
	VAL(0x1a091, 0),
	VAL(0x1a059, 0),
	VAL(0x1a061, 0),
	VAL(0x1a0b9, 0),
	VAL(0x1a0c1, 0),
	VAL(0x101d1, 0),
	VAL(0x0d479, 0),
	VAL(0x1a0e9, 8),
	VAL(0x107a1, 0xff0000),
	VAL(0x0a599, 0x20000400020),
	VAL(0x0d411, 0x200000001),
	CDM(0x1a540, identity),
	CDM(0x014a9, identity),
	CDM(0x0a351, identity),
};
static_assert(ARRAY_SIZE(fw27_compute_regs) == FW27_CDM_REGS);

static void fw27_emit_regs(struct fw27_register *out, const struct fw27_reg *table, int count,
			   const void *values)
{
	int i;

	for (i = 0; i < count; i++) {
		u64 value = table[i].constant;

		if (table[i].source != FW27_REG_CONST)
			value = *(const u64 *)(values + table[i].source);
		out[i].number = cpu_to_le32(table[i].number);
		out[i].value = cpu_to_le64(value);
	}
}

static u64 fw27_layer_metadata(const struct agx_render *job)
{
	return job->layermeta ?: job->heapmeta;
}

static u64 fw27_tib(const struct agx_render *job)
{
	u32 blocks = job->tib_blocks & 0x3f;
	u32 banks = 1;

	if (job->samples == 1)
		return blocks;
	if (job->tib_blocks > 64)
		banks = 4;
	else if (job->tib_blocks > 32)
		banks = 2;
	return (u64)(job->samples * banks) << 16 | blocks;
}

static struct fw27_geometry fw27_geometry(const struct agx_render *job)
{
	u64 tx = DIV_ROUND_UP(job->width, 32);
	u64 ty = DIV_ROUND_UP(job->height, 32);
	u64 mx = ALIGN(DIV_ROUND_UP(tx, 4), 4);
	u64 my = ALIGN(DIV_ROUND_UP(ty, 4), 4);
	u64 ux = 32 / job->utile_width;
	u64 uy = 32 / job->utile_height;

	return (struct fw27_geometry) {
		.tiles_x = tx,
		.tiles_y = ty,
		.size1 = DIV_ROUND_UP(5 * mx * my * ux * uy, 4),
		.size2 = mx * my,
		.size3 = 2 * mx * my * ux * uy,
		.x_blocks = 3 * mx | (2 * mx) << 9 | mx << 18,
		.y_blocks = 3 * my | (2 * my) << 9 | my << 18,
		.screen = (ty - 1) << 12 | (tx - 1),
		.pixels = (u64)(job->width - 1) | (u64)(job->height - 1) << 16,
		.macrotiles = my * uy | (mx * ux) << 16,
	};
}

static struct fw27_render_values fw27_render_values(const struct agx_render *job,
						    const struct fw27_geometry *g)
{
	u64 base = job->context_base;
	u64 layermeta = fw27_layer_metadata(job);
	u64 layers = job->layers > 1 ? 0xe000 | (job->layers - 1) : 0x8000;

	return (struct fw27_render_values) {
		.tilemap = job->tilemap,
		.tilemap_offset = job->tilemap - base,
		.tilemap_size = (4 * g->size1) << 24,
		.tpc_offset = job->tpc - base,
		.heapmeta = job->heapmeta,
		.heapmeta_offset = (job->heapmeta - base) | BIT_ULL(63),
		.layermeta = layermeta,
		.layermeta_offset = layermeta - base,
		.deflake1 = job->deflake[0] - base,
		.deflake2 = job->deflake[1] - base,
		.deflake3 = (job->deflake[2] - base) | BIT_ULL(50),
		.encoder_offset = job->encoder - base,
		.size1 = g->size1,
		.size2 = g->size2,
		.size3 = g->size3,
		.x_blocks = g->x_blocks,
		.y_blocks = g->y_blocks,
		.screen = g->screen,
		.pixels = g->pixels,
		.macrotiles = g->macrotiles,
		.target_layers = layers,
		.target_layers_low = layers & 0x4fff,
		.record_index = 0x80001 + (u64)job->scene * 4,
		.tile_state = 0x3717f | (g->tiles_x - 1) << 44 | (g->tiles_y - 1) << 53 |
			      BIT_ULL(37) | (job->layers > 1 ? BIT_ULL(32) : 0) |
			      (job->utile_config & 0xf000) << 28,
		.tib = fw27_tib(job),
		.size = (u64)job->height << 32 | job->width,
		.utile_config = job->utile_config,
		.tile_config = job->tile_config,
		.multisample_control = job->multisample_control,
		.ppp_control = job->ppp_control,
		.completion_control = job->completion_control,
		.cycle = job->cycle,
		.load_pipeline = job->load_pipeline,
		.load_pipeline_bind = job->load_pipeline_bind,
		.store_pipeline = job->store_pipeline,
		.store_pipeline_bind = job->store_pipeline_bind,
		.merge_x = job->merge_upper_x,
		.merge_y = job->merge_upper_y,
		.scissor_array = job->scissor_array,
		.depth_bias_array = job->depth_bias_array,
		.occlusion_query_base = job->occlusion_query_base,
		.aux_fb = job->aux_fb,
		.aux_fb_flags = job->aux_fb_flags,
		.aux_fb_pages = job->aux_fb_pages,
		.depth_flags = job->depth_flags,
		.depth_dimensions = job->depth_dimensions,
		.depth_clear = job->depth_clear,
		.depth_buffer = job->depth_buffer,
		.depth_stride = job->depth_stride,
		.depth_aux_buffer = job->depth_aux_buffer,
		.depth_aux_stride = job->depth_aux_stride,
		.stencil_clear = job->stencil_clear | 0x300,
		.stencil_buffer = job->stencil_buffer,
		.stencil_stride = job->stencil_stride,
		.stencil_aux_buffer = job->stencil_aux_buffer,
		.stencil_aux_stride = job->stencil_aux_stride,
		.ta_admission = job->ta_admission,
		.ta_gate = job->ta_gate,
		.ta_identity = job->ta_identity,
		.ta_work_stamp = job->ta_work_stamp,
		.ta_status = job->ta_status | 1,
		.fragment_admission = job->fragment_admission,
		.fragment_gate = job->fragment_gate,
		.fragment_identity = job->fragment_identity,
		.fragment_work_stamp = job->fragment_work_stamp,
		.fragment_status = job->fragment_status | 1,
	};
}

int fw27_render_check(const struct agx_render *job, const struct agx_render_hw *hw)
{
	const u64 based[] = {
		job->tilemap, job->heapmeta, fw27_layer_metadata(job), job->tpc,
		job->deflake[0], job->deflake[1], job->deflake[2], job->encoder,
	};
	int i;

	if (!job->width || job->width > 16384 || !job->height || job->height > 16384)
		return -EINVAL;
	if (!job->layers || job->layers > 2048)
		return -EINVAL;
	if (!(job->utile_width == 32 && (job->utile_height == 32 || job->utile_height == 16)) &&
	    !(job->utile_width == 16 && job->utile_height == 16))
		return -EINVAL;
	if (job->samples != 1 && job->samples != 2 && job->samples != 4)
		return -EINVAL;
	if (job->tib_blocks > FW27_MAX_TIB_BLOCKS || job->scene >= FW27_NR_SCENES)
		return -EINVAL;
	if (!job->sampler_array != !job->nr_samplers || job->sampler_array & 7 ||
	    job->nr_samplers == U32_MAX)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(based); i++)
		if (based[i] < job->context_base || based[i] - job->context_base > U32_MAX)
			return -EINVAL;
	if ((hw->support | hw->ta.shared_tail | hw->fragment.shared_tail) & 0xff)
		return -EINVAL;
	return 0;
}

void fw27_ta_work(void *out, const struct agx_render *job, const struct agx_render_hw *hw)
{
	const struct agx_stage_va *s = &hw->ta;
	struct fw27_geometry g = fw27_geometry(job);
	struct fw27_render_values v = fw27_render_values(job, &g);
	struct fw27_ta_work *w = out;

	memset(w, 0, sizeof(*w));
	w->kind = cpu_to_le32(FW27_WORK_TA);
	w->counter = cpu_to_le64(s->counter);
	w->context = cpu_to_le32(hw->context);
	w->notifier = cpu_to_le64(hw->notifier);
	w->pool_slot = cpu_to_le64(hw->pool_slot);
	w->pool = cpu_to_le64(hw->pool);
	w->scene = cpu_to_le64(hw->scene);
	w->empty = cpu_to_le64(hw->empty);

	fw27_emit_regs(w->regs, fw27_ta_regs, ARRAY_SIZE(fw27_ta_regs), &v);
	w->register_alias = cpu_to_le64(s->register_alias);
	w->nr_regs = cpu_to_le16(FW27_TA_REGS_COUNTED);
	w->regs_bytes = cpu_to_le16(FW27_TA_REGS_COUNTED * sizeof(struct fw27_register));

	w->tpc = cpu_to_le64(job->tpc);
	w->tpc_size = cpu_to_le64((g.size3 << 6) * job->layers);
	w->microsequence = cpu_to_le64(s->microsequence);
	w->microsequence_size = cpu_to_le32(sizeof(struct fw27_ta_msq));
	w->partial_event = cpu_to_le32(hw->fragment.event);
	w->partial_stamp = cpu_to_le32(hw->fragment.stamp);
	w->deflake = cpu_to_le64(v.deflake1);

	w->identity = cpu_to_le64(job->ta_identity >> 32);
	w->unk_848 = cpu_to_le32(U32_MAX);
	w->sampler_array = cpu_to_le64(job->sampler_array);
	w->nr_samplers = cpu_to_le32(job->nr_samplers);
	w->max_samplers = cpu_to_le32(job->nr_samplers ? job->nr_samplers + 1 : 0);
	w->request_growth = job->request_growth;

	w->driver_stamp = cpu_to_le64(s->driver_stamp);
	w->fw_stamp = cpu_to_le64(s->fw_stamp);
	w->stamp = cpu_to_le32(s->stamp);
	w->event = cpu_to_le32(s->event);
	w->uuid = cpu_to_le32(s->uuid);
	w->last_layer = cpu_to_le64(job->layers - 1);
	w->time_start = cpu_to_le64(hw->time_start);
	w->user_time_start = cpu_to_le64(s->user_time_start);
	w->user_time_end = cpu_to_le64(s->user_time_end);

	w->support = fw27_le56(hw->support >> 8);
	w->support_valid = 1;
	w->size_flags = cpu_to_le64(0xdaa0 | (job->layers > 1 ? BIT_ULL(62) : 0));
	w->shared_tail = fw27_le56(s->shared_tail >> 8);
	w->fired_value = cpu_to_le64(1);
}

void fw27_fragment_work(void *out, const struct agx_render *job, const struct agx_render_hw *hw)
{
	static const u64 unk_680[] = {
		0x8c020001dddc00, 0xa0001de1800, 0x8c020200029800, 0x220200039520,
	};
	const struct agx_stage_va *s = &hw->fragment;
	struct fw27_geometry g = fw27_geometry(job);
	struct fw27_render_values v = fw27_render_values(job, &g);
	agx_f32 numerator = AGX_F32(FW27_MERGE_NUMERATOR);
	u32 merge_x = job->merge_upper_x;
	u32 merge_y = job->merge_upper_y;
	struct fw27_fragment_work *w = out;
	int i;

	if (!job->merge_upper_x)
		merge_x = agx_f32_div(numerator, agx_f32_from_u32(job->width)).bits;
	if (!job->merge_upper_y)
		merge_y = agx_f32_div(numerator, agx_f32_from_u32(job->height)).bits;

	memset(w, 0, sizeof(*w));
	w->kind = cpu_to_le32(FW27_WORK_FRAGMENT);
	w->counter = cpu_to_le64(s->counter);
	w->context = cpu_to_le32(hw->context);
	w->microsequence = cpu_to_le64(s->microsequence);
	w->microsequence_size = cpu_to_le32(sizeof(struct fw27_fragment_msq));
	w->notifier = cpu_to_le64(hw->notifier);
	w->pool = cpu_to_le64(hw->pool);
	w->scene = cpu_to_le64(hw->scene);
	w->empty = cpu_to_le64(hw->empty);
	w->tilemap = cpu_to_le64(job->tilemap);
	w->multisample_control = cpu_to_le64(job->multisample_control);
	w->samples = cpu_to_le32(job->samples);
	w->macrotiles = cpu_to_le32(g.macrotiles);
	w->merge_x = cpu_to_le32(merge_x);
	w->merge_y = cpu_to_le32(merge_y);
	w->nr_tiles = cpu_to_le64(g.tiles_x * g.tiles_y);

	fw27_emit_regs(w->regs, fw27_fragment_regs, ARRAY_SIZE(fw27_fragment_regs), &v);
	for (i = 0; i < ARRAY_SIZE(unk_680); i++)
		w->unk_680[i] = cpu_to_le64(unk_680[i]);
	w->register_alias = cpu_to_le64(s->register_alias);
	w->nr_regs = cpu_to_le16(FW27_FRAGMENT_REGS_COUNTED);
	w->regs_bytes = cpu_to_le16(FW27_FRAGMENT_REGS_COUNTED * sizeof(struct fw27_register));

	w->depth_bias_array = cpu_to_le64(job->depth_bias_array);
	w->scissor_array = cpu_to_le64(job->scissor_array);
	w->occlusion_query_base = cpu_to_le64(job->occlusion_query_base);
	w->load_pipeline_bind = cpu_to_le64(job->load_pipeline_bind);
	w->load_pipeline = cpu_to_le64(job->load_pipeline);
	w->partial_load_pipeline_bind = cpu_to_le64(job->partial_load_pipeline_bind);
	w->partial_load_pipeline = cpu_to_le64(job->partial_load_pipeline);

	w->depth_flags = cpu_to_le64(job->depth_flags);
	w->unk_940 = cpu_to_le32(0x4040404);
	w->depth_buffer_load = cpu_to_le64(job->depth_buffer);
	w->depth_stride = cpu_to_le64(job->depth_stride);
	w->depth_aux_stride = cpu_to_le64(job->depth_aux_stride);
	w->depth_buffer_store[0] = cpu_to_le64(job->depth_buffer);
	w->depth_buffer_store[1] = cpu_to_le64(job->depth_buffer);
	w->depth_aux_buffer = cpu_to_le64(job->depth_aux_buffer);
	w->stencil_buffer_load = cpu_to_le64(job->stencil_buffer);
	w->stencil_stride = cpu_to_le64(job->stencil_stride);
	w->stencil_aux_stride = cpu_to_le64(job->stencil_aux_stride);
	w->stencil_buffer_store[0] = cpu_to_le64(job->stencil_buffer);
	w->stencil_buffer_store[1] = cpu_to_le64(job->stencil_buffer);
	w->stencil_aux_buffer = cpu_to_le64(job->stencil_aux_buffer);

	w->tib = cpu_to_le32(v.tib);
	w->aux_fb_flags = cpu_to_le64(job->aux_fb_flags);
	w->size = cpu_to_le64(v.size);
	w->aux_fb_pages = cpu_to_le64(job->aux_fb_pages);
	w->tile_config = cpu_to_le64(job->tile_config);
	w->store_pipeline_bind = cpu_to_le32(job->store_pipeline_bind);
	w->store_pipeline = cpu_to_le64(job->store_pipeline);
	w->partial_store_pipeline_bind = cpu_to_le32(job->partial_store_pipeline_bind);
	w->partial_store_pipeline = cpu_to_le64(job->partial_store_pipeline);
	w->depth_clear = cpu_to_le32(job->depth_clear);
	w->stencil_clear = cpu_to_le32((job->stencil_clear & 0xff) | 0x300);
	w->sample_size = cpu_to_le64(job->sample_size);
	w->depth_dimensions = cpu_to_le64(job->depth_dimensions);

	w->sync_grow = cpu_to_le32(job->sync_grow);
	w->identity = cpu_to_le64(job->fragment_identity >> 32);
	w->unk_b90 = cpu_to_le32(U32_MAX);
	w->sampler_array = cpu_to_le64(job->sampler_array);
	w->nr_samplers = cpu_to_le32(job->nr_samplers);
	w->max_samplers = cpu_to_le32(job->nr_samplers ? job->nr_samplers + 1 : 0);
	w->process_empty_tiles = cpu_to_le32(job->process_empty_tiles);
	w->unk_ba8 = cpu_to_le32(1);
	w->multisampled = cpu_to_le32(job->samples > 1);

	w->driver_stamp = cpu_to_le64(s->driver_stamp);
	w->fw_stamp = cpu_to_le64(s->fw_stamp);
	w->stamp = cpu_to_le32(s->stamp);
	w->event = cpu_to_le32(s->event);
	w->flush_stamps = cpu_to_le32(1);
	w->uuid = cpu_to_le32(s->uuid);
	w->last_layer = cpu_to_le64(job->layers - 1);
	w->time_start = cpu_to_le64(hw->time_start);
	w->time_end = cpu_to_le64(hw->time_end);
	w->user_time_start = cpu_to_le64(s->user_time_start);
	w->user_time_end = cpu_to_le64(s->user_time_end);

	w->support = fw27_le56(hw->support >> 8);
	w->support_valid = 1;
	w->size_flags = cpu_to_le64(0x113a0 | (job->layers > 1 ? BIT_ULL(62) : 0));
	w->shared_tail = fw27_le56(s->shared_tail >> 8);
	w->fired_value = cpu_to_le64(1);
	w->unk_c87 = cpu_to_le64(BIT_ULL(32));
}

#define WORK(type, base, field)	cpu_to_le64((base) + offsetof(struct type, field))

void fw27_ta_microsequence(void *out, const struct agx_render_hw *hw)
{
	const struct agx_stage_va *s = &hw->ta;
	u64 work = s->work;
	struct fw27_ta_msq *m = out;
	__le64 user = 0;

	if (s->user_time_start || s->user_time_end)
		user = WORK(fw27_ta_work, work, user_time_start);

	memset(m, 0, sizeof(*m));
	m->start_op = cpu_to_le32(FW27_OP_START_TA);
	m->regs = WORK(fw27_ta_work, work, regs);
	m->start_pool = cpu_to_le64(hw->pool);
	m->start_scene = cpu_to_le64(hw->scene);
	m->start_stats = cpu_to_le64(s->stats);
	m->start_queue = cpu_to_le64(s->queue);
	m->start_85c = WORK(fw27_ta_work, work, fw_85c);
	m->start_context = cpu_to_le32(hw->context);
	m->counter = cpu_to_le32(s->counter);
	m->pool_slot = cpu_to_le64(hw->pool_slot);
	m->start_784 = WORK(fw27_ta_work, work, fw_784);
	m->start_8a8 = WORK(fw27_ta_work, work, fw_8a8);
	m->start_uuid = cpu_to_le32(s->uuid);
	m->support = cpu_to_le64(hw->support);
	m->status_page = cpu_to_le64(s->status_page);
	m->unk_1b8 = cpu_to_le64(1);
	m->event = cpu_to_le32(s->event);

	m->begin.opcode = cpu_to_le32(FW27_OP_FLAG_START | FW27_OP_TIMESTAMP);
	m->begin.base = WORK(fw27_ta_work, work, time_0);
	m->begin.start = WORK(fw27_ta_work, work, time_start);
	m->begin.update = WORK(fw27_ta_work, work, time_start);
	m->begin.queue = cpu_to_le64(s->queue);
	m->begin.user = user;
	m->begin.tail = WORK(fw27_ta_work, work, fw_92f);
	m->begin.owner = WORK(fw27_ta_work, work, fw_83c);
	m->begin.uuid = cpu_to_le32(s->uuid);
	m->wait_op = cpu_to_le32(FW27_OP_WAIT_IDLE);
	m->end = m->begin;
	m->end.opcode = cpu_to_le32(FW27_OP_TIMESTAMP);
	m->end.update = WORK(fw27_ta_work, work, time_end);

	m->finish_op = cpu_to_le32(FW27_OP_FINISH_TA);
	m->finish_scene = m->start_scene;
	m->finish_pool = m->start_pool;
	m->finish_stats = m->start_stats;
	m->finish_queue = m->start_queue;
	m->finish_85c = m->start_85c;
	m->finish_context = m->start_context;
	m->finish_784 = m->start_784;
	m->finish_uuid = m->start_uuid;
	m->fw_stamp = cpu_to_le64(s->fw_stamp);
	m->stamp = cpu_to_le32(s->stamp);
	m->restart = cpu_to_le64(s->microsequence + FW27_TA_MSQ_RESTART);
	m->restart_offset = cpu_to_le32(-FW27_TA_MSQ_BACK);
	m->tail = WORK(fw27_ta_work, work, fw_91f);
	m->unk_2f5 = 1;
	m->end_op = cpu_to_le32(FW27_OP_END);
}

void fw27_fragment_microsequence(void *out, const struct agx_render_hw *hw)
{
	const struct agx_stage_va *s = &hw->fragment;
	u64 work = s->work;
	struct fw27_fragment_msq *m = out;
	__le64 user = 0;

	if (s->user_time_start || s->user_time_end)
		user = WORK(fw27_fragment_work, work, user_time_start);

	memset(m, 0, sizeof(*m));
	m->start_op = cpu_to_le32(FW27_OP_START_3D);
	m->regs = WORK(fw27_fragment_work, work, regs);
	m->start_scene = cpu_to_le64(hw->scene);
	m->start_stats = cpu_to_le64(s->stats);
	m->start_b70 = WORK(fw27_fragment_work, work, fw_b70);
	m->scene_54 = cpu_to_le64(hw->scene + 0x54);
	m->start_bb0 = WORK(fw27_fragment_work, work, fw_bb0);
	m->start_bb4 = WORK(fw27_fragment_work, work, fw_bb4);
	m->start_queue = cpu_to_le64(s->queue);
	m->start_work = cpu_to_le64(work);
	m->start_context = cpu_to_le32(hw->context);
	m->counter = cpu_to_le32(hw->ta.counter);
	m->pool_slot = cpu_to_le64(hw->pool_slot);
	m->start_a58 = WORK(fw27_fragment_work, work, fw_a58);
	m->start_bf0 = WORK(fw27_fragment_work, work, fw_bf0);
	m->start_uuid = cpu_to_le32(s->uuid);
	m->support = cpu_to_le64(hw->support);
	m->status_page = cpu_to_le64(s->status_page);
	m->event = cpu_to_le32(s->event);
	m->unk_1e4 = cpu_to_le32(1);

	m->begin.opcode = cpu_to_le32(FW27_OP_FLAG_START | FW27_OP_TIMESTAMP);
	m->begin.base = WORK(fw27_fragment_work, work, time_0);
	m->begin.start = WORK(fw27_fragment_work, work, time_start);
	m->begin.update = WORK(fw27_fragment_work, work, time_start);
	m->begin.queue = cpu_to_le64(s->queue);
	m->begin.user = user;
	m->begin.tail = WORK(fw27_fragment_work, work, fw_c77);
	m->begin.owner = WORK(fw27_fragment_work, work, fw_b84);
	m->begin.uuid = cpu_to_le32(s->uuid);
	m->wait_op = cpu_to_le32(FW27_OP_WAIT_IDLE);
	m->end = m->begin;
	m->end.opcode = cpu_to_le32(FW27_OP_TIMESTAMP);
	m->end.update = WORK(fw27_fragment_work, work, time_end);

	m->finish_op = cpu_to_le32(FW27_OP_FINISH_3D);
	m->finish_uuid = m->start_uuid;
	m->fw_stamp = cpu_to_le64(s->fw_stamp);
	m->stamp = cpu_to_le32(s->stamp);
	m->finish_scene = m->start_scene;
	m->finish_pool = cpu_to_le64(hw->pool);
	m->unk_2b4 = cpu_to_le32(1);
	m->finish_stats = m->start_stats;
	m->finish_bb0 = m->start_bb0;
	m->finish_bb4 = m->start_bb4;
	m->finish_b70 = m->start_b70;
	m->finish_queue = m->start_queue;
	m->finish_work = m->start_work;
	m->finish_context = m->start_context;
	m->finish_a58 = m->start_a58;
	m->restart = cpu_to_le64(s->microsequence + FW27_FRAGMENT_MSQ_RESTART);
	m->restart_offset = cpu_to_le32(-FW27_FRAGMENT_MSQ_BACK);
	m->tail = WORK(fw27_fragment_work, work, fw_c67);
	m->unk_335 = cpu_to_le64(0x110100000001);
	m->end_op = cpu_to_le32(FW27_OP_END);
}

int fw27_compute_check(const struct agx_compute *job, const struct agx_compute_hw *hw)
{
	if (!job->stream || job->stream & 3)
		return -EINVAL;
	if (job->stream_end <= job->stream || job->stream_end & 3)
		return -EINVAL;
	if (!job->sampler_array != !job->nr_samplers || job->sampler_array & 7 ||
	    job->nr_samplers == U32_MAX)
		return -EINVAL;
	if (hw->entry & 3)
		return -EINVAL;
	return 0;
}

void fw27_compute_entry(void *out, u64 stream)
{
	struct fw27_compute_entry *e = out;

	e->barrier = cpu_to_le32(0x600fffff);
	e->link_hi = cpu_to_le32(0x20000000 | upper_32_bits(stream));
	e->link_lo = cpu_to_le32(lower_32_bits(stream));
}

void fw27_compute_work(void *out, const struct agx_compute *job, const struct agx_compute_hw *hw)
{
	struct fw27_compute_values v = {
		.save_area = job->save_area,
		.save_1480 = job->save_area + 0x1480,
		.save_1488 = job->save_area + 0x1488,
		.save_1490 = job->save_area + 0x1490,
		.save_1498 = job->save_area + 0x1498,
		.entry = hw->entry ?: job->stream,
		.identity = hw->register_identity,
	};
	struct fw27_compute_work *w = out;

	memset(w, 0, sizeof(*w));
	w->kind = cpu_to_le32(FW27_WORK_COMPUTE);
	w->counter = cpu_to_le64(hw->counter);
	w->context = cpu_to_le32(hw->context);
	w->notifier = cpu_to_le64(hw->notifier);

	fw27_emit_regs(w->regs, fw27_compute_regs, ARRAY_SIZE(fw27_compute_regs), &v);
	w->register_alias = cpu_to_le64(hw->register_alias);
	w->nr_regs = cpu_to_le16(FW27_CDM_REGS);
	w->regs_bytes = cpu_to_le16(FW27_CDM_REGS * sizeof(struct fw27_register));

	w->microsequence = cpu_to_le64(hw->microsequence);
	w->microsequence_size = cpu_to_le32(sizeof(struct fw27_compute_msq));
	w->save_area = cpu_to_le64(job->save_area);
	w->stream_last = cpu_to_le64(job->stream_end - 4);
	w->unk_7c8 = cpu_to_le32(0x54024201);
	w->unk_7cc = cpu_to_le32(1);
	w->identity_hi = cpu_to_le32(upper_32_bits(hw->identity));
	w->sampler_array = cpu_to_le64(job->nr_samplers ? job->sampler_array : U32_MAX);
	w->nr_samplers = cpu_to_le32(job->nr_samplers);
	w->max_samplers = cpu_to_le32(job->nr_samplers ? job->nr_samplers + 1 : 0);
	w->driver_stamp = cpu_to_le64(hw->driver_stamp);
	w->fw_stamp = cpu_to_le64(hw->fw_stamp);
	w->stamp = cpu_to_le32(hw->stamp);
	w->event = cpu_to_le32(hw->event);
	w->flush_stamps = cpu_to_le32(1);
	w->identity_lo = cpu_to_le32(lower_32_bits(hw->identity));
	w->ordinal = cpu_to_le32(hw->ordinal);
	w->time_start = cpu_to_le64(hw->time_start);
	w->time_end = cpu_to_le64(hw->time_end);
	w->support = cpu_to_le64(hw->support);
	w->support_valid = 1;
	w->unk_874 = cpu_to_le32(0x21b00);
	w->unk_87c = cpu_to_le32(0x11700);
	w->fired_value = cpu_to_le64(1);
}

void fw27_compute_microsequence(void *out, const struct agx_compute *job,
				const struct agx_compute_hw *hw)
{
	static const u32 scratch[] = { 0, 0xf400, 0x1e800, 0x1f000 };
	u64 work = hw->work;
	u32 uuid = lower_32_bits(hw->identity);
	struct fw27_compute_msq *m = out;
	int i;

	memset(m, 0, sizeof(*m));
	m->start_op = cpu_to_le32(FW27_OP_START_CDM);
	m->regs = WORK(fw27_compute_work, work, regs);
	m->start_stats = cpu_to_le64(hw->stats);
	m->start_queue = cpu_to_le64(hw->queue);
	m->start_context = cpu_to_le32(hw->context);
	m->unk_30 = cpu_to_le32(1);
	m->generation = cpu_to_le32(hw->generation);
	m->start_76c = WORK(fw27_compute_work, work, fw_76c);
	m->start_uuid = cpu_to_le32(uuid);
	m->support = cpu_to_le64(hw->support);
	m->marker = cpu_to_le64(job->marker);
	for (i = 0; i < ARRAY_SIZE(scratch); i++)
		m->scratch[i] = cpu_to_le64(job->scratch + scratch[i]);
	m->start_8b3 = WORK(fw27_compute_work, work, fw_8b3);
	m->counter = cpu_to_le32(hw->counter);
	m->event = cpu_to_le32(hw->event);

	m->begin.opcode = cpu_to_le32(FW27_OP_FLAG_START | FW27_OP_TIMESTAMP);
	m->begin.base = WORK(fw27_compute_work, work, time_0);
	m->begin.start = WORK(fw27_compute_work, work, time_start);
	m->begin.update = WORK(fw27_compute_work, work, time_start);
	m->begin.queue = cpu_to_le64(hw->queue);
	m->begin.tail = WORK(fw27_compute_work, work, fw_8a3);
	m->begin.owner = WORK(fw27_compute_work, work, fw_7dc);
	m->begin.uuid = cpu_to_le32(uuid);
	m->wait_op = cpu_to_le32(FW27_OP_WAIT_IDLE);
	m->end = m->begin;
	m->end.opcode = cpu_to_le32(FW27_OP_TIMESTAMP);
	m->end.update = WORK(fw27_compute_work, work, time_end);

	m->finish_op = cpu_to_le32(FW27_OP_FINISH_CDM);
	m->finish_stats = m->start_stats;
	m->finish_queue = m->start_queue;
	m->finish_context = m->start_context;
	m->finish_76c = m->start_76c;
	m->finish_uuid = m->start_uuid;
	m->fw_stamp = cpu_to_le64(hw->fw_stamp);
	m->stamp = cpu_to_le32(hw->stamp);
	m->restart = cpu_to_le64(hw->microsequence + FW27_COMPUTE_MSQ_RESTART);
	m->restart_offset = cpu_to_le32(-FW27_COMPUTE_MSQ_BACK);
	m->finish_8b3 = m->start_8b3;
	m->finish_893 = WORK(fw27_compute_work, work, fw_893);
	m->end_op = cpu_to_le32(FW27_OP_END);
}
