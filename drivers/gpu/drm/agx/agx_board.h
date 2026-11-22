/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __AGX_BOARD_H__
#define __AGX_BOARD_H__

#include <linux/types.h>

#include "agx_f32.h"

#define AGX_MAX_STATES		16
#define AGX_MAX_MCC		4
#define AGX_MAX_PERIODS_MS	178956
#define AGX_CLOCKS_PER_MS	24000
#define AGX_FAST_DIE_LIMIT_C	110

struct agx_policy {
	u32 pwr_min_duty;
	u32 pwr_integral_min;
	u32 pwr_filter;
	agx_f32 pwr_ki;
	agx_f32 pwr_kp;

	u32 ppm_filter_ms;
	agx_f32 ppm_ki;
	agx_f32 ppm_kp;

	u32 perf_base;
	u32 perf_utilization;
	u32 perf_dead_zone;
	u32 perf_boost_util;
	u32 perf_boost_step;
	u32 perf_reset_iters;
	u32 perf_drop;
	u32 perf_integral_min;
	u32 perf_filter[2];
	agx_f32 perf_ki[2];
	agx_f32 perf_kp[2];

	u32 se_filter[2];
	agx_f32 se_ki[2];
	agx_f32 se_kp[2];
	u32 se_inactive;
	u32 se_engage_criteria;
	u32 se_engage_tolerance;
	u32 se_reset_tolerance;
	u32 se_reset_criteria;

	u32 avg_target_filter;
	u32 avg_filter;
	u32 avg_filter_ms;
	u32 avg_min_duty;
	agx_f32 avg_ki;
	bool avg_ki_per_second;
	agx_f32 avg_kp;

	u32 fast_die_release_c;
	u32 fast_die_delta;
	agx_f32 fast_die_ki;
	agx_f32 fast_die_kp;

	bool has_turbo_input;
	u32 turbo_input;
};

struct agx_board {
	const char *fw_uuid;
	const char *fw_build;
	u64 private_va;
	u64 private_size;

	u32 revision;
	u32 core_mask;
	u32 sensor_mask;
	u32 fast_die_mask;
	u32 max_power_mw;
	bool gpc;

	u32 nr_states;
	u32 freq_high[AGX_MAX_STATES];
	u32 freq_low[AGX_MAX_STATES];
	u32 row_high[AGX_MAX_STATES];
	u32 row_low[AGX_MAX_STATES];
	u32 volt_core[AGX_MAX_STATES];
	u32 volt_sram[AGX_MAX_STATES];
	u32 power_high[AGX_MAX_STATES];
	u32 power_low[AGX_MAX_STATES];

	u32 period_clocks;
	u32 period_ms;

	u64 sgx_base;
	u64 irq_page;
	u64 mcc[AGX_MAX_MCC];
	u32 nr_mcc;

	struct agx_policy policy;
};

typedef const void *(*agx_prop_fn)(const void *ctx, const char *name, int *len);

int agx_board_parse(struct agx_board *board, agx_prop_fn prop, const void *ctx,
		    u64 sgx_base, u64 sgx_size, const char **reason);

static inline u32 agx_board_top_state(const struct agx_board *board)
{
	return board->nr_states - 1;
}

#endif
