// SPDX-License-Identifier: GPL-2.0-only

#include <linux/limits.h>
#include <linux/string.h>

#include "fw_27_0_1.h"

struct fw27_filter {
	agx_f32 alpha;
	agx_f32 inverse;
	u32 count_x4;
	u32 duration;
	u64 clocks;
};

static __le32 fw27_f32(agx_f32 v)
{
	return cpu_to_le32(v.bits);
}

static struct fw27_filter fw27_filter(u32 count, u32 period_ms)
{
	agx_f32 n = agx_f32_from_u32(count);

	return (struct fw27_filter) {
		.alpha = agx_f32_div(agx_f32_from_u32(count - 1), n),
		.inverse = agx_f32_div(AGX_F32_1, n),
		.count_x4 = count * 4,
		.duration = count * period_ms,
		.clocks = (u64)count * period_ms * AGX_CLOCKS_PER_MS,
	};
}

static void fw27_power_loop(struct fw27_power *p, const struct agx_board *b, u32 max)
{
	const struct agx_policy *c = &b->policy;
	agx_f32 power = agx_f32_from_u32(b->max_power_mw);
	struct fw27_filter f = fw27_filter(c->pwr_filter, b->period_ms);

	p->pwr_alpha = fw27_f32(f.alpha);
	p->pwr_inverse = fw27_f32(f.inverse);
	p->pwr_ki = fw27_f32(c->pwr_ki);
	p->pwr_integral_min = fw27_f32(agx_f32_from_u32(c->pwr_integral_min));
	p->pwr_integral_max = fw27_f32(power);
	p->pwr_kp = fw27_f32(c->pwr_kp);
	p->pwr_slope = fw27_f32(agx_f32_div(agx_f32_neg(agx_f32_from_u32(max)), power));
	p->pwr_duty_min = cpu_to_le32(c->pwr_min_duty);
	p->pwr_duty_max = cpu_to_le32(max);
	p->pwr_duty_now = cpu_to_le32(max);
	p->pwr_limit = cpu_to_le32(b->max_power_mw);
	p->pwr_output = cpu_to_le32(max);

	f = fw27_filter(5, b->period_ms);
	p->c2200_integral_max = fw27_f32(AGX_F32_65536);
	p->c2200_min = fw27_f32(agx_f32_from_u32(c->pwr_min_duty));
	p->c2200_max = fw27_f32(agx_f32_from_u32(max));
	p->c2200_max_freq = cpu_to_le32(b->freq_high[agx_board_top_state(b)]);
	p->c2200_duty_min = cpu_to_le32(c->pwr_min_duty);
	p->c2200_percent = fw27_f32(AGX_F32_100);
	p->c2200_duty_max = cpu_to_le32(max);
	p->c2200_alpha = fw27_f32(f.alpha);
	p->c2200_inverse = fw27_f32(f.inverse);
	p->top_2880 = cpu_to_le32(max);

	p->c2c58_integral_max = fw27_f32(AGX_F32_65536);
	p->c2c58_integral_min = fw27_f32(AGX_F32_ZERO);
	p->c2c58_duty_min = cpu_to_le32(c->pwr_min_duty);
	p->c2c58_duty_max = cpu_to_le32(max);
}

static void fw27_ppm_loop(struct fw27_power *p, const struct agx_board *b, u32 max, agx_f32 dt)
{
	const struct agx_policy *c = &b->policy;
	struct fw27_filter f = fw27_filter(c->ppm_filter_ms / b->period_ms, b->period_ms);

	p->ppm_count = cpu_to_le32(f.count_x4);
	p->ppm_alpha = fw27_f32(f.alpha);
	p->ppm_inverse = fw27_f32(f.inverse);
	p->ppm_ki = fw27_f32(agx_f32_mul(dt, c->ppm_ki));
	p->ppm_integral_min = fw27_f32(agx_f32_from_u32(c->pwr_integral_min));
	p->ppm_integral_max = fw27_f32(AGX_F32_65536);
	p->ppm_kp = fw27_f32(c->ppm_kp);
	p->ppm_duty_min = cpu_to_le32(c->pwr_min_duty);
	p->ppm_duty_max = cpu_to_le32(max);
	p->ppm_limit = cpu_to_le32(b->max_power_mw);
	p->ppm_duration = cpu_to_le32(c->ppm_filter_ms);
	p->ppm_clocks = cpu_to_le64(f.clocks);
}

static void fw27_perf_loop(struct fw27_power *p, const struct agx_board *b, u32 max, u32 base)
{
	const struct agx_policy *c = &b->policy;
	const u32 range[] = { 0, base, 1, max };
	u32 target = c->perf_utilization - c->perf_dead_zone;
	struct fw27_filter f;
	int i;

	p->util_a_count = cpu_to_le32(4);
	p->util_a_scale = fw27_f32(AGX_F32_1);
	p->util_a_percent = cpu_to_le32(100);
	p->util_b_count = cpu_to_le32(4);
	p->util_b_scale = fw27_f32(AGX_F32_1);
	p->util_b_percent = cpu_to_le32(100);
	for (i = 0; i < 2; i++) {
		p->util_a_flags[i] = cpu_to_le32(1);
		p->util_b_flags[i] = cpu_to_le32(1);

		f = fw27_filter(c->perf_filter[i], b->period_ms);
		p->perf_alpha[i] = fw27_f32(f.alpha);
		p->perf_inverse[i] = fw27_f32(f.inverse);
		p->perf_ki[i] = fw27_f32(c->perf_ki[i]);
		p->perf_kp[i] = fw27_f32(c->perf_kp[i]);
	}
	for (i = 0; i < ARRAY_SIZE(range); i++) {
		p->util_a_range[i] = cpu_to_le32(range[i]);
		p->util_b_range[i] = cpu_to_le32(range[i]);
	}

	p->perf_target = cpu_to_le32(target);
	p->perf_boost = cpu_to_le32(c->perf_boost_util);
	p->perf_boost_step = cpu_to_le32(c->perf_boost_step);
	p->perf_reset_iters = cpu_to_le32(c->perf_reset_iters);
	p->unk_b50[0] = cpu_to_le32(6);
	p->unk_b50[1] = cpu_to_le32(1);
	p->perf_drop = cpu_to_le32(c->perf_drop);
	p->perf_integral_min = fw27_f32(agx_f32_from_u32(c->perf_integral_min));
	p->perf_integral_max = fw27_f32(AGX_F32_95);
	p->perf_slope = fw27_f32(agx_f32_div(agx_f32_from_u32(max - base), AGX_F32_95));
	p->perf_base = cpu_to_le32(base);
	p->perf_max = cpu_to_le32(max);
	p->perf_now = cpu_to_le32(base);
	p->perf_target_now = cpu_to_le32(target);
	p->perf_dead_zone = cpu_to_le32(c->perf_dead_zone);
	p->perf_output = cpu_to_le32(base);
}

static void fw27_se_loop(struct fw27_power *p, const struct agx_board *b, u32 max, u32 base,
			 agx_f32 dt)
{
	const struct agx_policy *c = &b->policy;
	u32 us = b->period_ms * 1000;
	struct fw27_filter f;
	int i;

	for (i = 0; i < 2; i++) {
		f = fw27_filter(c->se_filter[i], b->period_ms);
		p->se_alpha[i] = fw27_f32(f.alpha);
		p->se_inverse[i] = fw27_f32(f.inverse);
		p->se_ki[i] = fw27_f32(agx_f32_mul(c->se_ki[i], dt));
		p->se_kp[i] = fw27_f32(c->se_kp[i]);
		p->se_duration[i] = cpu_to_le32(f.duration);
		p->se_clocks[i] = cpu_to_le64(f.clocks);
	}
	p->se_unk_2b74 = cpu_to_le32(50);
	p->se_enable = cpu_to_le32(1);
	p->se_integral_min = fw27_f32(AGX_F32_ZERO);
	p->se_integral_max = fw27_f32(AGX_F32_65536);
	p->se_percent = cpu_to_le32(100);
	p->se_max = cpu_to_le32(max);
	p->se_base = cpu_to_le32(base);
	p->se_period = fw27_f32(agx_f32_from_u32(us));
	p->se_unk_2bc0 = cpu_to_le32(1400);
	p->se_scale = fw27_f32(AGX_F32_65536);
	p->se_period_us = cpu_to_le32(us);
	p->se_periods = cpu_to_le32((33 + b->period_ms / 2) / b->period_ms);
	p->se_inactive = cpu_to_le32(c->se_inactive);
	p->se_engage_criteria = cpu_to_le32(c->se_engage_criteria);
	p->se_engage_tolerance = cpu_to_le32(c->se_engage_tolerance);
	p->se_reset_tolerance = cpu_to_le32(c->se_reset_tolerance);
	p->se_reset_criteria = cpu_to_le32(c->se_reset_criteria);
}

static void fw27_avg_loop(struct fw27_power *p, const struct agx_board *b, u32 max, agx_f32 dt)
{
	const struct agx_policy *c = &b->policy;
	struct fw27_filter f = fw27_filter(c->avg_target_filter, b->period_ms);
	agx_f32 ki = c->avg_ki;
	int i;

	for (i = 0; i < ARRAY_SIZE(p->avg_limits); i++)
		p->avg_limits[i] = cpu_to_le32(b->max_power_mw);
	p->avg_target_alpha = fw27_f32(f.alpha);
	p->avg_target_inverse = fw27_f32(f.inverse);
	p->avg_target_count = cpu_to_le32(f.count_x4);
	p->avg_target_duration = cpu_to_le32(f.duration);
	p->avg_target_clocks = cpu_to_le64(f.clocks);

	if (c->avg_ki_per_second)
		ki = agx_f32_mul(ki, dt);

	f = fw27_filter(c->avg_filter, b->period_ms);
	p->avg_count = cpu_to_le32(f.count_x4);
	p->avg_alpha = fw27_f32(f.alpha);
	p->avg_inverse = fw27_f32(f.inverse);
	p->avg_ki = fw27_f32(ki);
	p->avg_integral_min = fw27_f32(AGX_F32_ZERO);
	p->avg_integral_max = fw27_f32(AGX_F32_65536);
	p->avg_kp = fw27_f32(c->avg_kp);
	p->avg_slope = fw27_f32(AGX_F32_ZERO);
	p->avg_duty_min = cpu_to_le32(c->avg_min_duty);
	p->avg_duty_max = cpu_to_le32(max);
	p->avg_duty_now = cpu_to_le32(max);
	p->avg_max = fw27_f32(agx_f32_from_u32(b->max_power_mw));
	p->avg_limit = cpu_to_le32(b->max_power_mw);
	p->avg_duration = cpu_to_le32(c->avg_filter_ms ?: f.duration);
	p->avg_clocks = cpu_to_le64(f.clocks);
	p->avg_output = cpu_to_le32(max);
}

static void fw27_fast_die_loop(struct fw27_power *p, const struct agx_board *b, u32 max)
{
	const struct agx_policy *c = &b->policy;
	agx_f32 dt = agx_f32_div(agx_f32_div(agx_f32_from_u32(b->period_clocks), AGX_F32_24000),
				 AGX_F32_1000);

	p->fast_die_sensors = cpu_to_le64(b->fast_die_mask);
	p->fast_die_release = cpu_to_le32(c->fast_die_release_c * 100);
	p->fast_die_count = cpu_to_le32(4);
	p->fast_die_alpha = fw27_f32(AGX_F32_ZERO);
	p->fast_die_inverse = fw27_f32(AGX_F32_1);
	p->fast_die_ki = fw27_f32(agx_f32_mul(c->fast_die_ki, dt));
	p->fast_die_integral_min = fw27_f32(AGX_F32_ZERO);
	p->fast_die_integral_max = fw27_f32(AGX_F32_65536);
	p->fast_die_kp = fw27_f32(c->fast_die_kp);
	p->fast_die_slope = fw27_f32(AGX_F32_ZERO);
	p->fast_die_duty_min = cpu_to_le32(c->pwr_min_duty);
	p->fast_die_duty_max = cpu_to_le32(max);
	p->fast_die_duty_now = cpu_to_le32(max);
	p->fast_die_delta = cpu_to_le32(c->fast_die_delta * 100);
	p->fast_die_limit = cpu_to_le32(AGX_FAST_DIE_LIMIT_C * 100);
	p->fast_die_output = cpu_to_le32(max);
}

static void fw27_turbo_loop(struct fw27_power *p, const struct agx_board *b)
{
	const struct agx_policy *c = &b->policy;
	struct fw27_filter f = fw27_filter(200, b->period_ms);
	agx_f32 kp = AGX_F32(0x437a0000), ki = AGX_F32(0x3f933333);
	u32 target = 10150, delta = 0, threshold = 700;

	if (c->turbo_input == 0) {
		kp = AGX_F32(0x43960000);
		ki = AGX_F32(0x3fa66666);
		target = 9600;
		delta = 300;
		threshold = 0;
	} else if (c->turbo_input == 1) {
		target = 0;
	}

	p->turbo_present = c->has_turbo_input;
	p->turbo_mode = c->turbo_input == 1;
	p->turbo_alpha = fw27_f32(f.alpha);
	p->turbo_inverse = fw27_f32(f.inverse);
	p->turbo_ki = fw27_f32(ki);
	p->turbo_integral_min = fw27_f32(AGX_F32_ZERO);
	p->turbo_integral_max = fw27_f32(AGX_F32_65536);
	p->turbo_kp = fw27_f32(kp);
	p->turbo_slope = fw27_f32(agx_f32_neg(agx_f32_div(AGX_F32_100, AGX_F32_65536)));
	p->turbo_min = cpu_to_le32(900);
	p->turbo_max = cpu_to_le32(1000);
	p->turbo_now = cpu_to_le32(1000);
	p->turbo_range = fw27_f32(AGX_F32_3000);
	p->turbo_target = cpu_to_le32(target);
	p->turbo_delta = cpu_to_le32(delta);
	p->turbo_output = cpu_to_le32(1000);
	p->turbo_filter = cpu_to_le32(200);
	p->turbo_threshold = cpu_to_le32(threshold);
	p->turbo_unk_27e6 = fw27_f32(AGX_F32_64);
	p->turbo_unk_27ea = fw27_f32(AGX_F32_65536);
}

static void fw27_last_loop(struct fw27_power *p, const struct agx_board *b, u32 max)
{
	struct fw27_filter f = fw27_filter(32, b->period_ms);

	p->last_count = cpu_to_le32(4);
	p->last_alpha = fw27_f32(f.alpha);
	p->last_inverse = fw27_f32(f.inverse);
	p->last_ki = fw27_f32(AGX_F32_5);
	p->last_integral_min = fw27_f32(AGX_F32_ZERO);
	p->last_integral_max = fw27_f32(AGX_F32_65536);
	p->last_kp = fw27_f32(AGX_F32_15);
	p->last_below_max = cpu_to_le32(max - 100);
	p->last_max[0] = cpu_to_le32(max);
	p->last_max[1] = cpu_to_le32(max);
	p->last_now = cpu_to_le32(max);
	p->last_output = cpu_to_le32(max);
}

static __le32 fw27_smc_key(const char key[4])
{
	return cpu_to_le32((u32)key[0] << 24 | (u32)key[1] << 16 | (u32)key[2] << 8 | key[3]);
}

void fw27_power(struct fw27_power *p, const struct agx_board *b, const struct agx_graph_va *va,
		u32 entropy)
{
	u32 max = 100 * agx_board_top_state(b);
	u32 base = 100 * b->policy.perf_base;
	agx_f32 dt = agx_f32_div(agx_f32_from_u32(b->period_ms), AGX_F32_1000);
	int i, j;

	memset(p, 0, sizeof(*p));
	p->sample_clocks[0] = cpu_to_le32(b->period_clocks);
	p->sample_clocks[1] = cpu_to_le32(b->period_clocks);
	p->unk_9c8 = cpu_to_le32(625);
	for (i = 0; i < b->nr_states; i++)
		p->state_scale[i] = fw27_f32(AGX_F32_1_02);
	for (i = 0; i < AGX_MAX_STATES; i++)
		for (j = 0; j < ARRAY_SIZE(p->power_slots[i]); j++)
			p->power_slots[i][j] = cpu_to_le16(S16_MAX);

	fw27_power_loop(p, b, max);
	fw27_ppm_loop(p, b, max, dt);
	fw27_perf_loop(p, b, max, base);
	fw27_se_loop(p, b, max, base, dt);
	fw27_avg_loop(p, b, max, dt);
	fw27_fast_die_loop(p, b, max);
	fw27_turbo_loop(p, b);
	fw27_last_loop(p, b, max);

	p->metrology = cpu_to_le64(va->extra1 + 0x80);
	p->metrology_sensors = cpu_to_le64(b->sensor_mask);
	p->aux = cpu_to_le64(va->power_aux);
	p->key_rail = fw27_smc_key("g0TR");
	p->key_sensor[0] = fw27_smc_key("g0t0");
	p->key_sensor[1] = fw27_smc_key("g0t1");
	p->key_sensor[2] = fw27_smc_key("g0EB");
	p->key_sensor[3] = fw27_smc_key("g0EL");
	p->entropy = cpu_to_le32(entropy);
}

void fw27_extra0(struct fw27_extra0 *x, const struct fw27_power *p)
{
	memset(x, 0, sizeof(*x));
	x->avg_filter = cpu_to_le32(le32_to_cpu(p->avg_count) / 4);
	x->avg_ki = p->avg_ki;
	x->avg_kp = p->avg_kp;
	x->avg_min_duty = p->avg_duty_min;
	x->avg_enable = cpu_to_le32(1);
	x->fast_die_limit = p->fast_die_limit;
	x->fast_die_release = p->fast_die_release;
	x->fast_die_kp = p->fast_die_kp;
	x->fast_die_ki = p->fast_die_ki;
	x->turbo_target = p->turbo_target;
	x->turbo_filter = p->turbo_filter;
	x->turbo_kp = p->turbo_kp;
	x->turbo_ki = p->turbo_ki;
	x->unk_110 = p->unk_9c8;
	x->ppm_enable = cpu_to_le32(1);
	x->ppm_max_power = p->ppm_limit;
	x->ppm_kp = p->ppm_kp;
	x->unk_134 = fw27_f32(AGX_F32_1_2);
	x->last_kp = p->last_kp;
	x->last_ki = p->last_ki;
	x->last_filter = cpu_to_le32(32);
	x->last_max = p->last_max[0];
	x->last_below_max = p->last_below_max;
}
