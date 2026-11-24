/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __AGX_FW_27_0_1_H__
#define __AGX_FW_27_0_1_H__

#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#include "../agx_fw.h"

#define AT(off, member) struct { u8 __UNIQUE_ID(pad)[off]; member; } __packed

struct fw27_le56 {
	u8 b[7];
} __packed;

static inline struct fw27_le56 fw27_le56(u64 v)
{
	struct fw27_le56 r;
	int i;

	for (i = 0; i < 7; i++)
		r.b[i] = v >> (8 * i);
	return r;
}

#define FW27_ROOT_TAG		0x0c8a636f039204b8ULL
#define FW27_HANDOFF_MAGIC	0x4b1d000000000002ULL
#define FW27_HANDOFF_NO_SLOT	0x0000ffff
#define FW27_HANDOFF_SLOTS	65

#define FW27_PAGE		0x4000
#define FW27_VA_WINDOW_LIMIT	0xe00000
#define FW27_MCC_SIZE		0x18000

#define FW27_CTRL_DESTROY	0x13
#define FW27_CTRL_INIT		0x15
#define FW27_CTRL_OPENING	0x20
#define FW27_CTRL_ENTRIES	256

#define FW27_MSG_INIT		0x81ULL
#define FW27_MSG_WORK		0x83ULL
#define FW27_MSG_CONTROL	0x84ULL

#define FW27_WORK_TA		0
#define FW27_WORK_FRAGMENT	1
#define FW27_WORK_COMPUTE	3
#define FW27_ITEM_WAIT		4
#define FW27_ITEM_TA_PRELUDE	6

#define FW27_OP_WAIT_IDLE	0x01
#define FW27_OP_TIMESTAMP	0x03
#define FW27_OP_START_TA	0x05
#define FW27_OP_FINISH_TA	0x06
#define FW27_OP_START_3D	0x07
#define FW27_OP_FINISH_3D	0x08
#define FW27_OP_START_CDM	0x0b
#define FW27_OP_FINISH_CDM	0x0c
#define FW27_OP_FLAG_START	0x80000000
#define FW27_OP_END		0x40000002

struct fw27_handoff_flush {
	__le32 state;
	__le32 pad;
	__le64 addr;
	__le64 size;
} __packed;
static_assert(sizeof(struct fw27_handoff_flush) == 0x18);

struct fw27_handoff {
	union {
		AT(0x000, __le64 magic_ap);
		AT(0x008, __le64 magic_fw);
		AT(0x018, __le32 cur_slot);
		AT(0x020, struct fw27_handoff_flush flush[FW27_HANDOFF_SLOTS]);
		AT(0x638, u8 unk_638);
		AT(0x640, __le64 unk_640);
		u8 raw[0x4000];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_handoff) == 0x4000);
static_assert(offsetof(struct fw27_handoff, unk_638) ==
	      0x20 + FW27_HANDOFF_SLOTS * sizeof(struct fw27_handoff_flush));

struct fw27_level {
	u8 entry_size;
	u8 page_shift;
	u8 table_shift;
	u8 shift;
	__le16 entries;
	__le16 page_size;
	__le64 valid;
	__le64 addr_mask;
	__le64 index_mask;
} __packed;
static_assert(sizeof(struct fw27_level) == 0x20);

struct fw27_root {
	union {
		AT(0x00, __le64 tag);
		AT(0x08, __le64 region_a);
		AT(0x18, __le64 main);
		AT(0x20, __le64 region_c);
		AT(0x2c, __le32 present);
		AT(0x30, __le16 page_size);
		AT(0x32, u8 page_shift);
		AT(0x33, u8 nr_levels);
		AT(0x34, struct fw27_level level[3]);
		AT(0xa8, __le64 status_a);
		AT(0xb0, __le64 status_b);
		AT(0xb8, __le64 extra0);
		AT(0xc0, __le64 extra1);
		u8 raw[0xc8];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_root) == 0xc8);

struct fw27_channel {
	__le64 read;
	__le64 done;
	__le64 tail;
	__le64 ring;
} __packed;

struct fw27_ring_pair {
	__le64 state;
	__le64 ring;
} __packed;

struct fw27_main {
	union {
		AT(0x000, __le64 hwdata);
		AT(0x008, __le64 pipe[2]);
		AT(0x018, __le64 cursor_header);
		AT(0x020, struct fw27_channel work[AGX_NR_WORK_CHANNELS]);
		AT(0x1a0, struct fw27_channel control);
		AT(0x1c0, struct fw27_ring_pair event);
		AT(0x1d0, struct fw27_ring_pair log);
		AT(0x1e0, struct fw27_ring_pair trace);
		AT(0x1f0, struct fw27_ring_pair stats);
		AT(0x200, __le64 channel15);
		AT(0x254, __le64 stats_obj[4]);
		AT(0x2c8, __le64 region_2c8);
		AT(0x2d0, __le64 slot_table_gpu);
		AT(0x2d8, __le64 slot_table);
		AT(0x2e0, __le64 shared_gpu);
		AT(0x2e8, __le64 shared);
		AT(0x2f8, __le32 unk_2f8);
		AT(0x3d8, __le32 unk_3d8);
		AT(0x479, __le64 power);
		u8 raw[0x500];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_main) == 0x500);

struct fw27_io_window {
	__le64 phys;
	__le64 va;
	__le32 size;
	__le32 stride;
	__le64 offset;
	__le32 flags;
	__le32 pad;
} __packed;
static_assert(sizeof(struct fw27_io_window) == 0x28);

#define FW27_IO_RW		2
#define FW27_NR_IO		60

struct fw27_state_group {
	__le32 counters[4];
} __packed;

struct fw27_second_domain {
	__le32 top_state;
	__le32 freq[AGX_MAX_STATES];
	__le32 volt_core[AGX_MAX_STATES][16];
	__le32 volt_sram[AGX_MAX_STATES][16];
} __packed;

struct fw27_hwdata {
	union {
		AT(0x0028, __le64 timestamps);
		AT(0x0640, struct fw27_io_window io[FW27_NR_IO]);
		AT(0x0fb0, __le32 chip_id);
		AT(0x0fb4, __le32 rev_major);
		AT(0x0fb8, __le32 rev_minor);
		AT(0x0fc0, __le32 gpc);
		AT(0x0fd8, __le32 unk_fd8);
		AT(0x0fe4, __le32 unk_fe4);
		AT(0x0fec, __le32 clocks_per_ms);
		AT(0x0ff4, __le32 period_ms);
		AT(0x0ffc, __le32 unk_ffc[3]);
		AT(0x10e0, __le32 top_state);
		AT(0x10e4, __le32 freq_high[AGX_MAX_STATES]);
		AT(0x1124, __le32 volt_core[AGX_MAX_STATES][16]);
		AT(0x1524, __le32 volt_sram[AGX_MAX_STATES][16]);
		AT(0x1924, __le32 freq_low[AGX_MAX_STATES]);
		AT(0x1964, __le32 state_scale[AGX_MAX_STATES]);
		AT(0x19e4, __le32 power_high[AGX_MAX_STATES]);
		AT(0x1a24, __le32 power_low[AGX_MAX_STATES]);
		AT(0x1ae4, __le32 row_high[AGX_MAX_STATES]);
		AT(0x1b24, __le32 row_low[AGX_MAX_STATES]);
		AT(0x1df4, struct fw27_second_domain second);
		AT(0x26aa, __le16 unk_26aa);
		AT(0x26ac, struct fw27_state_group group[2]);
		AT(0x26d0, __le32 unk_26d0[12]);
		AT(0x2724, __le32 unk_2724);
		u8 raw[0x4000];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_hwdata) == 0x4000);
static_assert(offsetof(struct fw27_hwdata, io[FW27_NR_IO]) == 0xfa0);
static_assert(offsetof(struct fw27_hwdata, second.volt_sram) == 0x1df4 + 0x444);

struct fw27_region_c {
	union {
		AT(0x000, __le32 trace_mask);
		AT(0x024, __le32 unk_24);
		AT(0x030, __le32 idle_enable);
		AT(0x058, __le16 unk_58);
		AT(0x05a, __le16 unk_5a);
		AT(0x05c, __le16 unk_5c);
		AT(0x07c, __le32 unk_7c);
		AT(0x088, __le32 max_power);
		AT(0x08c, __le32 unk_8c);
		AT(0x090, __le32 unk_90);
		AT(0x09c, __le32 unk_9c);
		AT(0x7f0, __le32 unk_7f0);
		AT(0x7f4, __le32 unk_7f4);
		AT(0x99c, __le32 unk_99c[4]);
		AT(0x9c0, __le32 off_delay_ms);
		AT(0x9cc, __le32 unk_9cc[3]);
		AT(0xe2c, __le32 fault_control);
		AT(0xe30, __le32 unk_e30);
		u8 raw[0x4000];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_region_c) == 0x4000);

struct fw27_status_a {
	union {
		AT(0x04, __le32 present);
		AT(0x10, __le32 clear_10);
		AT(0x14, __le32 init_ack);
		AT(0x18, __le32 clear_18);
		AT(0x1c, __le32 opening_ack);
		u8 raw[0x80];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_status_a) == 0x80);

struct fw27_status_b {
	union {
		AT(0x0004, __le32 present);
		AT(0x5168, __le64 map_state);
		AT(0x5170, __le64 map_ring);
		AT(0x51b4, __le32 map_armed);
		u8 raw[0x10000];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_status_b) == 0x10000);

struct fw27_extra0 {
	union {
		AT(0x010, __le32 avg_filter);
		AT(0x014, __le32 avg_ki);
		AT(0x018, __le32 avg_kp);
		AT(0x01c, __le32 avg_min_duty);
		AT(0x020, __le32 avg_enable);
		AT(0x0c8, __le32 fast_die_limit);
		AT(0x0cc, __le32 fast_die_release);
		AT(0x0d8, __le32 fast_die_kp);
		AT(0x0dc, __le32 fast_die_ki);
		AT(0x0f0, __le32 turbo_target);
		AT(0x0f4, __le32 turbo_filter);
		AT(0x108, __le32 turbo_kp);
		AT(0x10c, __le32 turbo_ki);
		AT(0x110, __le32 unk_110);
		AT(0x128, __le32 ppm_enable);
		AT(0x12c, __le32 ppm_max_power);
		AT(0x130, __le32 ppm_kp);
		AT(0x134, __le32 unk_134);
		AT(0x61c, __le32 last_kp);
		AT(0x620, __le32 last_ki);
		AT(0x624, __le32 last_filter);
		AT(0x628, __le32 last_max);
		AT(0x638, __le32 last_below_max);
		u8 raw[0x700];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_extra0) == 0x700);

struct fw27_power {
	union {
		AT(0x0004, __le32 sample_clocks[2]);
		AT(0x0010, __le32 util_a_count);
		AT(0x0014, __le32 util_a_scale);
		AT(0x002c, __le32 util_a_flags[2]);
		AT(0x003c, __le32 util_a_range[4]);
		AT(0x0050, __le32 util_a_percent);
		AT(0x0060, __le32 util_b_count);
		AT(0x0064, __le32 util_b_scale);
		AT(0x006c, __le32 util_b_flags[2]);
		AT(0x007c, __le32 util_b_range[4]);
		AT(0x0090, __le32 util_b_percent);
		AT(0x00d0, __le32 state_scale[AGX_MAX_STATES]);
		AT(0x09c8, __le32 unk_9c8);

		AT(0x09d4, __le32 pwr_alpha);
		AT(0x09dc, __le32 pwr_inverse);
		AT(0x09e4, __le32 pwr_ki);
		AT(0x09ec, __le32 pwr_integral_min);
		AT(0x09f0, __le32 pwr_integral_max);
		AT(0x09f4, __le32 pwr_kp);
		AT(0x09fc, __le32 pwr_slope);
		AT(0x0a00, __le32 pwr_duty_min);
		AT(0x0a04, __le32 pwr_duty_max);
		AT(0x0a08, __le32 pwr_duty_now);
		AT(0x0a14, __le32 pwr_limit);
		AT(0x0a48, __le32 pwr_output);

		AT(0x0a80, __le32 ppm_count);
		AT(0x0a8c, __le32 ppm_alpha);
		AT(0x0a94, __le32 ppm_inverse);
		AT(0x0a9c, __le32 ppm_ki);
		AT(0x0aa4, __le32 ppm_integral_min);
		AT(0x0aa8, __le32 ppm_integral_max);
		AT(0x0aac, __le32 ppm_kp);
		AT(0x0ab8, __le32 ppm_duty_min);
		AT(0x0abc, __le32 ppm_duty_max);
		AT(0x0acc, __le32 ppm_limit);
		AT(0x0ad8, __le32 ppm_duration);
		AT(0x0ae0, __le64 ppm_clocks);

		AT(0x0b38, __le32 perf_target);
		AT(0x0b40, __le32 perf_boost);
		AT(0x0b44, __le32 perf_boost_step);
		AT(0x0b48, __le32 perf_reset_iters);
		AT(0x0b50, __le32 unk_b50[2]);
		AT(0x0b58, __le32 perf_drop);
		AT(0x0b5c, __le32 perf_alpha[2]);
		AT(0x0b64, __le32 perf_inverse[2]);
		AT(0x0b6c, __le32 perf_ki[2]);
		AT(0x0b74, __le32 perf_integral_min);
		AT(0x0b78, __le32 perf_integral_max);
		AT(0x0b7c, __le32 perf_kp[2]);
		AT(0x0b84, __le32 perf_slope);
		AT(0x0b88, __le32 perf_base);
		AT(0x0b8c, __le32 perf_max);
		AT(0x0b90, __le32 perf_now);
		AT(0x0b9c, __le32 perf_target_now);
		AT(0x0ba4, __le32 perf_dead_zone);
		AT(0x0bd0, __le32 perf_output);

		AT(0x1bff, __le16 power_slots[AGX_MAX_STATES][32]);

		AT(0x2200, __le32 c2200_integral_max);
		AT(0x2204, __le32 c2200_min);
		AT(0x2208, __le32 c2200_max);
		AT(0x220c, __le32 c2200_max_freq);
		AT(0x2220, __le32 c2200_duty_min);
		AT(0x2228, __le32 c2200_percent);
		AT(0x222c, __le32 c2200_duty_max);
		AT(0x2234, __le32 c2200_alpha);
		AT(0x2238, __le32 c2200_inverse);

		AT(0x2278, __le64 fast_die_sensors);
		AT(0x2280, __le32 fast_die_release);
		AT(0x2288, __le32 fast_die_count);
		AT(0x2294, __le32 fast_die_alpha);
		AT(0x229c, __le32 fast_die_inverse);
		AT(0x22a4, __le32 fast_die_ki);
		AT(0x22ac, __le32 fast_die_integral_min);
		AT(0x22b0, __le32 fast_die_integral_max);
		AT(0x22b4, __le32 fast_die_kp);
		AT(0x22bc, __le32 fast_die_slope);
		AT(0x22c0, __le32 fast_die_duty_min);
		AT(0x22c4, __le32 fast_die_duty_max);
		AT(0x22c8, __le32 fast_die_duty_now);
		AT(0x22cc, __le32 fast_die_delta);
		AT(0x22d4, __le32 fast_die_limit);
		AT(0x2308, __le32 fast_die_output);

		AT(0x2528, u8 turbo_present);
		AT(0x2529, u8 turbo_mode);
		AT(0x273a, __le32 turbo_alpha);
		AT(0x2742, __le32 turbo_inverse);
		AT(0x274a, __le32 turbo_ki);
		AT(0x2752, __le32 turbo_integral_min);
		AT(0x2756, __le32 turbo_integral_max);
		AT(0x275a, __le32 turbo_kp);
		AT(0x2762, __le32 turbo_slope);
		AT(0x2766, __le32 turbo_min);
		AT(0x276a, __le32 turbo_max);
		AT(0x276e, __le32 turbo_now);
		AT(0x2776, __le32 turbo_range);
		AT(0x277a, __le32 turbo_target);
		AT(0x2782, __le32 turbo_delta);
		AT(0x27ae, __le32 turbo_output);
		AT(0x27ca, __le32 turbo_filter);
		AT(0x27e2, __le32 turbo_threshold);
		AT(0x27e6, __le32 turbo_unk_27e6);
		AT(0x27ea, __le32 turbo_unk_27ea);
		AT(0x2880, __le32 top_2880);

		AT(0x28f0, __le32 avg_limits[3]);
		AT(0x2904, __le32 avg_target_alpha);
		AT(0x2908, __le32 avg_target_inverse);
		AT(0x290c, __le32 avg_target_count);
		AT(0x2910, __le32 avg_target_duration);
		AT(0x2914, __le64 avg_target_clocks);
		AT(0x29e4, __le32 avg_count);
		AT(0x29f0, __le32 avg_alpha);
		AT(0x29f8, __le32 avg_inverse);
		AT(0x2a00, __le32 avg_ki);
		AT(0x2a08, __le32 avg_integral_min);
		AT(0x2a0c, __le32 avg_integral_max);
		AT(0x2a10, __le32 avg_kp);
		AT(0x2a18, __le32 avg_slope);
		AT(0x2a1c, __le32 avg_duty_min);
		AT(0x2a20, __le32 avg_duty_max);
		AT(0x2a24, __le32 avg_duty_now);
		AT(0x2a2c, __le32 avg_max);
		AT(0x2a30, __le32 avg_limit);
		AT(0x2a3c, __le32 avg_duration);
		AT(0x2a44, __le64 avg_clocks);
		AT(0x2a64, __le32 avg_output);

		AT(0x2b74, __le32 se_unk_2b74);
		AT(0x2b78, __le32 se_enable);
		AT(0x2b80, __le32 se_alpha[2]);
		AT(0x2b88, __le32 se_inverse[2]);
		AT(0x2b90, __le32 se_ki[2]);
		AT(0x2b98, __le32 se_integral_min);
		AT(0x2b9c, __le32 se_integral_max);
		AT(0x2ba0, __le32 se_kp[2]);
		AT(0x2bac, __le32 se_percent);
		AT(0x2bb0, __le32 se_max);
		AT(0x2bb4, __le32 se_base);
		AT(0x2bbc, __le32 se_period);
		AT(0x2bc0, __le32 se_unk_2bc0);
		AT(0x2bcc, __le32 se_duration[2]);
		AT(0x2bd4, __le64 se_clocks[2]);
		AT(0x2be8, __le32 se_scale);
		AT(0x2c10, __le32 se_period_us);
		AT(0x2c14, __le32 se_periods);
		AT(0x2c18, __le32 se_inactive);
		AT(0x2c1c, __le32 se_engage_criteria);
		AT(0x2c20, __le32 se_engage_tolerance);
		AT(0x2c24, __le32 se_reset_tolerance);
		AT(0x2c28, __le32 se_reset_criteria);
		AT(0x2c58, __le32 c2c58_integral_max);
		AT(0x2c5c, __le32 c2c58_integral_min);
		AT(0x2c68, __le32 c2c58_duty_min);
		AT(0x2c6c, __le32 c2c58_duty_max);

		AT(0x2e34, __le32 last_count);
		AT(0x2e40, __le32 last_alpha);
		AT(0x2e48, __le32 last_inverse);
		AT(0x2e50, __le32 last_ki);
		AT(0x2e58, __le32 last_integral_min);
		AT(0x2e5c, __le32 last_integral_max);
		AT(0x2e60, __le32 last_kp);
		AT(0x2e6c, __le32 last_below_max);
		AT(0x2e70, __le32 last_max[2]);
		AT(0x2e80, __le32 last_now);
		AT(0x2eb4, __le32 last_output);

		AT(0x3b80, __le64 metrology);
		AT(0x3b88, __le64 metrology_sensors);
		AT(0x6328, __le64 aux);
		AT(0x6330, __le32 key_rail);
		AT(0x6398, __le32 key_sensor[4]);
		AT(0x6a64, __le32 entropy);
		u8 raw[0x8000];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_power) == 0x8000);

struct fw27_queue {
	union {
		AT(0x00, __le64 pointers);
		AT(0x08, __le64 ring);
		AT(0x10, __le64 jobs);
		AT(0x18, __le64 private);
		AT(0x2c, __le32 unk_2c);
		AT(0x30, __le32 priority[2]);
		AT(0x38, __le64 priority_mask);
		AT(0x40, __le32 priority_first);
		AT(0x44, __le32 priority_last);
		AT(0x48, __le32 unk_48);
		AT(0x4c, __le32 uuid);
		AT(0x78, __le64 context);
		u8 raw[0x80];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_queue) == 0x80);

struct fw27_queue_pointers {
	union {
		AT(0x00, __le32 done);
		AT(0x30, __le32 read);
		AT(0x40, __le32 write);
		AT(0x50, __le32 capacity);
		u8 raw[0x60];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_queue_pointers) == 0x60);

struct fw27_queue_context {
	union {
		AT(0x00, __le16 slot);
		AT(0x05, u8 unk_5);
		AT(0x26, u8 unk_26);
		AT(0x33, u8 unk_33);
		u8 raw[0x40];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_queue_context) == 0x40);

struct fw27_queue_jobs {
	union {
		AT(0x08, __le64 self);
		u8 raw[0x18];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_queue_jobs) == 0x18);

struct fw27_barrier {
	union {
		AT(0x00, __le32 kind);
		AT(0x04, __le64 stamp);
		AT(0x0c, __le32 value);
		AT(0x10, __le32 event);
		AT(0x14, __le32 stamp_self);
		AT(0x18, __le32 uuid);
		u8 raw[0x40];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_barrier) == 0x40);

struct fw27_dependency {
	union {
		AT(0x00, __le32 kind);
		AT(0x04, __le64 stamp[2]);
		AT(0x14, __le32 value);
		AT(0x20, __le32 event);
		AT(0x24, __le32 stamp_self);
		AT(0x28, __le32 uuid);
		AT(0x30, __le32 wait_kind);
		u8 raw[0x40];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_dependency) == 0x40);

struct fw27_ta_prelude {
	union {
		AT(0x00, __le32 kind);
		AT(0x04, __le32 context);
		AT(0x08, __le32 pool_slot);
		AT(0x10, __le32 blocks);
		AT(0x14, __le64 pool);
		AT(0x1c, __le32 stamp);
		u8 raw[0x40];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_ta_prelude) == 0x40);

struct fw27_channel_msg {
	__le64 tag;
	__le64 queue;
	__le32 stage;
	__le16 head;
	u8 event;
	u8 first;
} __packed;
static_assert(sizeof(struct fw27_channel_msg) == 0x18);

struct fw27_notifier {
	union {
		AT(0x00, __le64 threshold);
		AT(0x08, __le32 generation);
		AT(0x0c, __le32 count);
		AT(0x10, __le32 unk_10);
		AT(0x24, __le32 context);
		u8 raw[0x100];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_notifier) == 0x100);

struct fw27_control {
	union {
		AT(0x00, __le32 opcode);
		AT(0x04, __le32 open_context);
		AT(0x08, __le32 open_mask);
		AT(0x14, __le64 open_flist);
		AT(0x1c, __le64 open_table[2]);
		AT(0x2c, __le32 open_count);
		AT(0x34, __le32 open_enable);
		AT(0x08, u8 destroy_state[AGX_NR_CONTEXT_STATE]);
		AT(0x0c, __le64 destroy_context);
		u8 raw[0x40];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_control) == 0x40);

struct fw27_event {
	union {
		AT(0x00, __le32 kind);
		AT(0x04, __le32 arg[4]);
		AT(0x14, __le64 halt_count);
		u8 raw[0x48];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_event) == 0x48);

struct fw27_flist {
	union {
		AT(0x00, __le32 unk_0);
		AT(0x14, __le64 directory);
		AT(0x1c, __le32 entries);
		AT(0x28, __le32 unk_28);
		AT(0x30, __le64 table);
		AT(0x48, __le32 cursor);
		AT(0x4c, __le64 aux);
		u8 raw[0x80];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_flist) == 0x80);

struct fw27_compute_entry {
	__le32 barrier;
	__le32 link_hi;
	__le32 link_lo;
} __packed;
static_assert(sizeof(struct fw27_compute_entry) == 0xc);

struct fw27_register {
	__le32 number;
	__le64 value;
} __packed;
static_assert(sizeof(struct fw27_register) == 0xc);

#define FW27_REG_SLOTS		128

struct fw27_ta_work {
	union {
		AT(0x000, __le32 kind);
		AT(0x004, __le64 counter);
		AT(0x00c, __le32 context);
		AT(0x010, __le64 notifier);
		AT(0x018, __le64 pool_slot);
		AT(0x020, __le64 pool);
		AT(0x028, __le64 scene);
		AT(0x030, __le64 empty);
		AT(0x040, struct fw27_register regs[FW27_REG_SLOTS]);
		AT(0x740, __le64 register_alias);
		AT(0x748, __le16 nr_regs);
		AT(0x74a, __le16 regs_bytes);
		AT(0x760, __le64 tpc);
		AT(0x768, __le64 tpc_size);
		AT(0x770, __le64 microsequence);
		AT(0x778, __le32 microsequence_size);
		AT(0x77c, __le32 partial_event);
		AT(0x780, __le32 partial_stamp);
		AT(0x784, u8 fw_784);
		AT(0x7a8, __le64 deflake);
		AT(0x83c, u8 fw_83c);
		AT(0x840, __le64 identity);
		AT(0x848, __le32 unk_848);
		AT(0x84c, __le64 sampler_array);
		AT(0x854, __le32 nr_samplers);
		AT(0x858, __le32 max_samplers);
		AT(0x85c, u8 fw_85c);
		AT(0x864, u8 request_growth);
		AT(0x878, __le64 driver_stamp);
		AT(0x880, __le64 fw_stamp);
		AT(0x888, __le32 stamp);
		AT(0x88c, __le32 event);
		AT(0x898, __le32 uuid);
		AT(0x8a0, __le64 last_layer);
		AT(0x8a8, u8 fw_8a8);
		AT(0x8c0, __le64 time_0);
		AT(0x8c8, __le64 time_start);
		AT(0x8d0, __le64 time_end);
		AT(0x8d8, __le64 user_time_start);
		AT(0x8e0, __le64 user_time_end);
		AT(0x8ff, struct fw27_le56 support);
		AT(0x906, u8 support_valid);
		AT(0x908, __le64 size_flags);
		AT(0x910, struct fw27_le56 shared_tail);
		AT(0x917, __le64 fired_value);
		AT(0x91f, u8 fw_91f);
		AT(0x92f, u8 fw_92f);
		u8 raw[0x940];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_ta_work) == 0x940);

struct fw27_fragment_work {
	union {
		AT(0x000, __le32 kind);
		AT(0x004, __le64 counter);
		AT(0x00c, __le32 context);
		AT(0x014, __le64 microsequence);
		AT(0x01c, __le32 microsequence_size);
		AT(0x020, __le64 notifier);
		AT(0x028, __le64 pool);
		AT(0x030, __le64 scene);
		AT(0x038, __le64 empty);
		AT(0x040, __le64 tilemap);
		AT(0x048, __le64 multisample_control);
		AT(0x050, __le32 samples);
		AT(0x054, __le32 macrotiles);
		AT(0x068, __le32 merge_x);
		AT(0x06c, __le32 merge_y);
		AT(0x078, __le64 nr_tiles);
		AT(0x080, struct fw27_register regs[FW27_REG_SLOTS]);
		AT(0x680, __le64 unk_680[4]);
		AT(0x780, __le64 register_alias);
		AT(0x788, __le16 nr_regs);
		AT(0x78a, __le16 regs_bytes);
		AT(0x7a0, __le64 depth_bias_array);
		AT(0x7b0, __le64 scissor_array);
		AT(0x7c0, __le64 occlusion_query_base);
		AT(0x8f8, __le64 load_pipeline_bind);
		AT(0x900, __le64 load_pipeline);
		AT(0x928, __le64 partial_load_pipeline_bind);
		AT(0x930, __le64 partial_load_pipeline);
		AT(0x938, __le64 depth_flags);
		AT(0x940, __le32 unk_940);
		AT(0x948, __le64 depth_buffer_load);
		AT(0x950, __le64 depth_stride);
		AT(0x958, __le64 depth_aux_stride);
		AT(0x960, __le64 depth_buffer_store[2]);
		AT(0x970, __le64 depth_aux_buffer);
		AT(0x978, __le64 stencil_buffer_load);
		AT(0x980, __le64 stencil_stride);
		AT(0x988, __le64 stencil_aux_stride);
		AT(0x990, __le64 stencil_buffer_store[2]);
		AT(0x9a0, __le64 stencil_aux_buffer);
		AT(0x9b8, __le32 tib);
		AT(0x9c0, __le64 aux_fb_flags);
		AT(0x9c8, __le64 size);
		AT(0x9d0, __le64 aux_fb_pages);
		AT(0x9d8, __le64 tile_config);
		AT(0x9f4, __le32 store_pipeline_bind);
		AT(0x9fc, __le64 store_pipeline);
		AT(0xa14, __le32 partial_store_pipeline_bind);
		AT(0xa1c, __le64 partial_store_pipeline);
		AT(0xa28, __le32 depth_clear);
		AT(0xa2c, __le32 stencil_clear);
		AT(0xa30, __le64 sample_size);
		AT(0xa50, __le64 depth_dimensions);
		AT(0xa58, u8 fw_a58);
		AT(0xb70, u8 fw_b70);
		AT(0xb80, __le32 sync_grow);
		AT(0xb84, u8 fw_b84);
		AT(0xb88, __le64 identity);
		AT(0xb90, __le32 unk_b90);
		AT(0xb94, __le64 sampler_array);
		AT(0xb9c, __le32 nr_samplers);
		AT(0xba0, __le32 max_samplers);
		AT(0xba4, __le32 process_empty_tiles);
		AT(0xba8, __le32 unk_ba8);
		AT(0xbac, __le32 multisampled);
		AT(0xbb0, u8 fw_bb0);
		AT(0xbb4, u8 fw_bb4);
		AT(0xbc0, __le64 driver_stamp);
		AT(0xbc8, __le64 fw_stamp);
		AT(0xbd0, __le32 stamp);
		AT(0xbd4, __le32 event);
		AT(0xbdc, __le32 flush_stamps);
		AT(0xbe0, __le32 uuid);
		AT(0xbe8, __le64 last_layer);
		AT(0xbf0, u8 fw_bf0);
		AT(0xc08, __le64 time_0);
		AT(0xc10, __le64 time_start);
		AT(0xc18, __le64 time_end);
		AT(0xc20, __le64 user_time_start);
		AT(0xc28, __le64 user_time_end);
		AT(0xc47, struct fw27_le56 support);
		AT(0xc4e, u8 support_valid);
		AT(0xc50, __le64 size_flags);
		AT(0xc58, struct fw27_le56 shared_tail);
		AT(0xc5f, __le64 fired_value);
		AT(0xc67, u8 fw_c67);
		AT(0xc77, u8 fw_c77);
		AT(0xc87, __le64 unk_c87);
		u8 raw[0xcc0];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_fragment_work) == 0xcc0);

#define FW27_CDM_REGS		24

struct fw27_compute_work {
	union {
		AT(0x000, __le32 kind);
		AT(0x004, __le64 counter);
		AT(0x00c, __le32 context);
		AT(0x010, __le64 notifier);
		AT(0x020, struct fw27_register regs[FW27_CDM_REGS]);
		AT(0x720, __le64 register_alias);
		AT(0x728, __le16 nr_regs);
		AT(0x72a, __le16 regs_bytes);
		AT(0x760, __le64 microsequence);
		AT(0x768, __le32 microsequence_size);
		AT(0x76c, u8 fw_76c);
		AT(0x798, __le64 save_area);
		AT(0x7a0, __le64 stream_last);
		AT(0x7c8, __le32 unk_7c8);
		AT(0x7cc, __le32 unk_7cc);
		AT(0x7dc, u8 fw_7dc);
		AT(0x7e0, __le32 identity_hi);
		AT(0x7e8, __le64 sampler_array);
		AT(0x7f0, __le32 nr_samplers);
		AT(0x7f4, __le32 max_samplers);
		AT(0x800, __le64 driver_stamp);
		AT(0x808, __le64 fw_stamp);
		AT(0x810, __le32 stamp);
		AT(0x814, __le32 event);
		AT(0x81c, __le32 flush_stamps);
		AT(0x820, __le32 identity_lo);
		AT(0x828, __le32 ordinal);
		AT(0x82c, __le64 time_0);
		AT(0x834, __le64 time_start);
		AT(0x83c, __le64 time_end);
		AT(0x86a, __le64 support);
		AT(0x872, u8 support_valid);
		AT(0x874, __le32 unk_874);
		AT(0x87c, __le32 unk_87c);
		AT(0x88b, __le64 fired_value);
		AT(0x893, u8 fw_893);
		AT(0x8a3, u8 fw_8a3);
		AT(0x8b3, u8 fw_8b3);
		u8 raw[0x8c0];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_compute_work) == 0x8c0);

struct fw27_msq_time {
	union {
		AT(0x00, __le32 opcode);
		AT(0x04, __le64 base);
		AT(0x0c, __le64 start);
		AT(0x14, __le64 update);
		AT(0x1c, __le64 queue);
		AT(0x24, __le64 user);
		AT(0x2c, __le64 tail);
		AT(0x34, __le64 owner);
		AT(0x44, __le32 uuid);
		u8 raw[0x4c];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_msq_time) == 0x4c);

struct fw27_ta_msq {
	union {
		AT(0x000, __le32 start_op);
		AT(0x014, __le64 regs);
		AT(0x01c, __le64 start_pool);
		AT(0x024, __le64 start_scene);
		AT(0x02c, __le64 start_stats);
		AT(0x034, __le64 start_queue);
		AT(0x03c, __le64 start_85c);
		AT(0x044, __le32 start_context);
		AT(0x048, __le32 counter);
		AT(0x050, __le64 pool_slot);
		AT(0x064, __le64 start_784);
		AT(0x06c, __le64 start_8a8);
		AT(0x07c, __le32 start_uuid);
		AT(0x190, __le64 support);
		AT(0x1a8, __le64 status_page);
		AT(0x1b8, __le64 unk_1b8);
		AT(0x1c0, __le32 event);
		AT(0x1cc, struct fw27_msq_time begin);
		AT(0x218, __le32 wait_op);
		AT(0x21c, struct fw27_msq_time end);
		AT(0x268, __le32 finish_op);
		AT(0x26c, __le64 finish_scene);
		AT(0x274, __le64 finish_pool);
		AT(0x27c, __le64 finish_stats);
		AT(0x284, __le64 finish_queue);
		AT(0x28c, __le64 finish_85c);
		AT(0x294, __le32 finish_context);
		AT(0x29c, __le64 finish_784);
		AT(0x2a8, __le32 finish_uuid);
		AT(0x2b0, __le64 fw_stamp);
		AT(0x2b8, __le32 stamp);
		AT(0x2e0, __le64 restart);
		AT(0x2e8, __le32 restart_offset);
		AT(0x2ed, __le64 tail);
		AT(0x2f5, u8 unk_2f5);
		AT(0x2fc, __le32 end_op);
		u8 raw[0x300];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_ta_msq) == 0x300);

struct fw27_fragment_msq {
	union {
		AT(0x000, __le32 start_op);
		AT(0x014, __le64 regs);
		AT(0x01c, __le64 start_scene);
		AT(0x024, __le64 start_stats);
		AT(0x02c, __le64 start_b70);
		AT(0x034, __le64 scene_54);
		AT(0x03c, __le64 start_bb0);
		AT(0x044, __le64 start_bb4);
		AT(0x04c, __le64 start_queue);
		AT(0x054, __le64 start_work);
		AT(0x05c, __le32 start_context);
		AT(0x060, __le32 counter);
		AT(0x068, __le64 pool_slot);
		AT(0x07c, __le64 start_a58);
		AT(0x084, __le64 start_bf0);
		AT(0x0a0, __le32 start_uuid);
		AT(0x1b0, __le64 support);
		AT(0x1c8, __le64 status_page);
		AT(0x1e0, __le32 event);
		AT(0x1e4, __le32 unk_1e4);
		AT(0x1ec, struct fw27_msq_time begin);
		AT(0x238, __le32 wait_op);
		AT(0x23c, struct fw27_msq_time end);
		AT(0x288, __le32 finish_op);
		AT(0x28c, __le32 finish_uuid);
		AT(0x294, __le64 fw_stamp);
		AT(0x29c, __le32 stamp);
		AT(0x2a4, __le64 finish_scene);
		AT(0x2ac, __le64 finish_pool);
		AT(0x2b4, __le32 unk_2b4);
		AT(0x2b8, __le64 finish_stats);
		AT(0x2c0, __le64 finish_bb0);
		AT(0x2c8, __le64 finish_bb4);
		AT(0x2d0, __le64 finish_b70);
		AT(0x2d8, __le64 finish_queue);
		AT(0x2e0, __le64 finish_work);
		AT(0x2e8, __le32 finish_context);
		AT(0x2ec, __le64 finish_a58);
		AT(0x320, __le64 restart);
		AT(0x328, __le32 restart_offset);
		AT(0x32d, __le64 tail);
		AT(0x335, __le64 unk_335);
		AT(0x344, __le32 end_op);
		u8 raw[0x380];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_fragment_msq) == 0x380);

struct fw27_compute_msq {
	union {
		AT(0x000, __le32 start_op);
		AT(0x014, __le64 regs);
		AT(0x01c, __le64 start_stats);
		AT(0x024, __le64 start_queue);
		AT(0x02c, __le32 start_context);
		AT(0x030, __le32 unk_30);
		AT(0x034, __le32 generation);
		AT(0x044, __le64 start_76c);
		AT(0x050, __le32 start_uuid);
		AT(0x160, __le64 support);
		AT(0x178, __le64 marker);
		AT(0x188, __le64 scratch[4]);
		AT(0x1a8, __le64 start_8b3);
		AT(0x1b0, __le32 counter);
		AT(0x1b8, __le32 event);
		AT(0x1bc, struct fw27_msq_time begin);
		AT(0x208, __le32 wait_op);
		AT(0x20c, struct fw27_msq_time end);
		AT(0x258, __le32 finish_op);
		AT(0x25c, __le64 finish_stats);
		AT(0x264, __le64 finish_queue);
		AT(0x26c, __le32 finish_context);
		AT(0x270, __le64 finish_76c);
		AT(0x27c, __le32 finish_uuid);
		AT(0x284, __le64 fw_stamp);
		AT(0x28c, __le32 stamp);
		AT(0x2b4, __le64 restart);
		AT(0x2bc, __le32 restart_offset);
		AT(0x2c1, __le64 finish_8b3);
		AT(0x2c9, __le64 finish_893);
		AT(0x2d4, __le32 end_op);
		u8 raw[0x300];
	} __packed;
} __packed;
static_assert(sizeof(struct fw27_compute_msq) == 0x300);

int fw27_windows(const struct agx_board *board, u64 base, struct agx_window *out);
void fw27_handoff(void *page);
bool fw27_handoff_ready(const void *page, u64 *receipt);
int fw27_graph(const struct agx_board *board, const struct agx_graph_va *va, u32 entropy,
	       const struct agx_graph_mem *mem);
void fw27_map_notify(void *status_b, u64 state, u64 ring);
void fw27_opening_flist(void *out, const struct agx_flist_va *va);
void fw27_opening_control(void *out, const struct agx_flist_va *va);
void fw27_destroy_context(void *out, u64 context, const u8 state[AGX_NR_CONTEXT_STATE]);

void fw27_power(struct fw27_power *out, const struct agx_board *board,
		const struct agx_graph_va *va, u32 entropy);
void fw27_extra0(struct fw27_extra0 *out, const struct fw27_power *power);

int fw27_queue(void *out, const struct agx_queue_va *va);
void fw27_queue_pointers(void *out, u32 capacity, u32 write);
void fw27_queue_context(void *out);
void fw27_queue_jobs(void *out, u64 self);
void fw27_barrier(void *out, const struct agx_wait *wait);
void fw27_dependency(void *out, const struct agx_wait *wait);
void fw27_ta_prelude(void *out, const struct agx_render_hw *hw, u32 blocks);
void fw27_fragment_prelude(void *out, const struct agx_render_hw *hw);
void fw27_channel_msg(void *out, const struct agx_channel_kick *kick);
void fw27_notifier(void *out, u64 threshold, u32 generation, u32 count, u32 context);

int fw27_render_check(const struct agx_render *job, const struct agx_render_hw *hw);
void fw27_ta_work(void *out, const struct agx_render *job, const struct agx_render_hw *hw);
void fw27_fragment_work(void *out, const struct agx_render *job, const struct agx_render_hw *hw);
void fw27_ta_microsequence(void *out, const struct agx_render_hw *hw);
void fw27_fragment_microsequence(void *out, const struct agx_render_hw *hw);

int fw27_compute_check(const struct agx_compute *job, const struct agx_compute_hw *hw);
void fw27_compute_entry(void *out, u64 stream);
void fw27_compute_work(void *out, const struct agx_compute *job,
		       const struct agx_compute_hw *hw);
void fw27_compute_microsequence(void *out, const struct agx_compute *job,
				const struct agx_compute_hw *hw);

#endif
