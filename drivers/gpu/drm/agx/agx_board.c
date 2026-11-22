// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/byteorder.h>

#include "agx_board.h"

#define AGX_T8132_CORES		GENMASK(9, 0)
#define AGX_MCC_BASE		0x2201c4000ULL
#define AGX_MCC_SHIFT		25
#define AGX_PAGE		0x4000
#define AGX_SGX_MIN_SIZE	0x1008000
#define AGX_MAX_POWER_MW	80000
#define AGX_MHZ			1000000

struct agx_props {
	agx_prop_fn get;
	const void *ctx;
	const char **reason;
};

struct agx_le_state {
	__le32 freq;
	__le32 volt;
};

struct agx_tunable {
	const char *name;
	u16 offset;
	u32 fallback;
};

#define TUNE(prop, field, value) \
	{ "apple,sgx-gpu-" prop, offsetof(struct agx_policy, field), value }

static const struct agx_tunable agx_tunables[] = {
	TUNE("pwr-min-duty-cycle", pwr_min_duty, 40),
	TUNE("pwr-integral-min-clamp", pwr_integral_min, 0),
	TUNE("pwr-filter-time-constant", pwr_filter, 313),
	TUNE("pwr-integral-gain", pwr_ki, 0x3ca59586),
	TUNE("pwr-proportional-gain", pwr_kp, 0x40a90fdb),
	TUNE("ppm-filter-time-constant-ms", ppm_filter_ms, 16),
	TUNE("ppm-ki", ppm_ki, 0x43160000),
	TUNE("ppm-kp", ppm_kp, 0x3e800000),
	TUNE("perf-base-pstate", perf_base, 1),
	TUNE("perf-tgt-utilization", perf_utilization, 85),
	TUNE("perf-dz", perf_dead_zone, 0),
	TUNE("perf-boost-min-util", perf_boost_util, 95),
	TUNE("perf-boost-ce-step", perf_boost_step, 50),
	TUNE("perf-reset-iters", perf_reset_iters, 6),
	TUNE("perf-filter-drop-threshold", perf_drop, 0),
	TUNE("perf-integral-min-clamp", perf_integral_min, 0),
	TUNE("perf-filter-time-constant", perf_filter[0], 5),
	TUNE("perf-filter-time-constant2", perf_filter[1], 200),
	TUNE("perf-integral-gain", perf_ki[0], 0x3f4c28f6),
	TUNE("perf-integral-gain2", perf_ki[1], 0x3f4c28f6),
	TUNE("perf-proportional-gain", perf_kp[0], 0x40accccd),
	TUNE("perf-proportional-gain2", perf_kp[1], 0x40accccd),
	TUNE("se-filter-time-constant", se_filter[0], 9),
	TUNE("se-filter-time-constant-1", se_filter[1], 3),
	TUNE("se-ki", se_ki[0], 0xc2480000),
	TUNE("se-ki-1", se_ki[1], 0xc2c80000),
	TUNE("se-kp", se_kp[0], 0xc0a00000),
	TUNE("se-kp-1", se_kp[1], 0xc0a00000),
	TUNE("se-inactive-threshold", se_inactive, 2500),
	TUNE("se-engagement-criteria", se_engage_criteria, 600),
	TUNE("se-engagement-tolerance", se_engage_tolerance, 2),
	TUNE("se-reset-tolerance", se_reset_tolerance, 4),
	TUNE("se-reset-criteria", se_reset_criteria, 50),
	TUNE("avg-power-target-filter-tc", avg_target_filter, 1),
	TUNE("avg-power-min-duty-cycle", avg_min_duty, 40),
	TUNE("avg-power-kp", avg_kp, 0x401ae148),
	TUNE("fast-die0-release-temp", fast_die_release_c, 80),
	TUNE("fast-die0-prop-tgt-delta", fast_die_delta, 0),
	TUNE("fast-die0-integral-gain", fast_die_ki, 0x43480000),
	TUNE("fast-die0-proportional-gain", fast_die_kp, 0x42f00000),
};

static int agx_bad(const struct agx_props *p, const char *reason)
{
	*p->reason = reason;
	return -EINVAL;
}

static int agx_le32_prop(const struct agx_props *p, const char *name, u32 *out)
{
	__le32 v;
	int len;
	const void *data = p->get(p->ctx, name, &len);

	if (!data)
		return -ENOENT;
	if (len != sizeof(v))
		return agx_bad(p, name);
	memcpy(&v, data, sizeof(v));
	*out = le32_to_cpu(v);
	return 0;
}

static int agx_le32_default(const struct agx_props *p, const char *name, u32 fallback, u32 *out)
{
	int ret = agx_le32_prop(p, name, out);

	if (ret != -ENOENT)
		return ret;
	*out = fallback;
	return 0;
}

static int agx_cells(const struct agx_props *p, const char *name, u32 *out, u32 count)
{
	__be32 v;
	int len;
	const void *data = p->get(p->ctx, name, &len);
	u32 i;

	if (!data || len != count * sizeof(v))
		return agx_bad(p, name);
	for (i = 0; i < count; i++) {
		memcpy(&v, data + i * sizeof(v), sizeof(v));
		out[i] = be32_to_cpu(v);
	}
	return 0;
}

static int agx_string(const struct agx_props *p, const char *name, const char **out)
{
	int len;
	const char *data = p->get(p->ctx, name, &len);

	if (!data || len < 2 || strnlen(data, len) != len - 1)
		return agx_bad(p, name);
	*out = data;
	return 0;
}

static int agx_parse_identity(struct agx_board *b, const struct agx_props *p)
{
	u32 vm[4];
	int ret;

	ret = agx_string(p, "apple,firmware-uuid", &b->fw_uuid);
	if (ret)
		return ret;
	ret = agx_string(p, "apple,firmware-build", &b->fw_build);
	if (ret)
		return ret;
	ret = agx_cells(p, "apple,rtkit-private-vm-region", vm, ARRAY_SIZE(vm));
	if (ret)
		return ret;
	b->private_va = (u64)vm[0] << 32 | vm[1];
	b->private_size = (u64)vm[2] << 32 | vm[3];
	if (!b->private_size)
		return agx_bad(p, "empty private VM region");
	return agx_cells(p, "apple,chip-revision", &b->revision, 1);
}

static int agx_parse_limits(struct agx_board *b, const struct agx_props *p)
{
	u32 uw, no_gpc;
	int ret;

	ret = agx_cells(p, "apple,core-mask", &b->core_mask, 1);
	if (ret)
		return ret;
	if (!b->core_mask || b->core_mask & ~AGX_T8132_CORES)
		return agx_bad(p, "core mask out of range");

	ret = agx_cells(p, "apple,max-power-microwatt", &uw, 1);
	if (ret)
		return ret;
	b->max_power_mw = uw / 1000;
	if (!b->max_power_mw || b->max_power_mw > AGX_MAX_POWER_MW)
		return agx_bad(p, "maximum power out of range");

	ret = agx_cells(p, "apple,thermal-sensor-mask", &b->sensor_mask, 1);
	if (ret)
		return ret;
	ret = agx_cells(p, "apple,fast-die-sensor-mask", &b->fast_die_mask, 1);
	if (ret)
		return ret;
	if (!b->fast_die_mask || b->fast_die_mask & ~b->sensor_mask)
		return agx_bad(p, "fast-die sensors outside the sensor chain");

	ret = agx_le32_default(p, "apple,sgx-no-gpc", 0, &no_gpc);
	if (ret)
		return ret;
	b->gpc = !no_gpc;
	return 0;
}

static int agx_parse_states(struct agx_board *b, const struct agx_props *p)
{
	const void *core_data, *sram_data;
	struct agx_le_state core[AGX_MAX_STATES], sram[AGX_MAX_STATES];
	int core_len, sram_len, ret;
	u32 rows, tables, first, last, row, low, high, n = 0;

	ret = agx_le32_prop(p, "apple,sgx-perf-state-count", &rows);
	if (ret)
		return agx_bad(p, "no performance state count");
	ret = agx_le32_prop(p, "apple,sgx-perf-state-table-count", &tables);
	if (ret || tables != 1 || !rows || rows > AGX_MAX_STATES)
		return agx_bad(p, "unsupported performance table shape");

	core_data = p->get(p->ctx, "apple,sgx-perf-states", &core_len);
	sram_data = p->get(p->ctx, "apple,sgx-perf-states-sram", &sram_len);
	if (!core_data || !sram_data || core_len != rows * sizeof(core[0]) ||
	    sram_len != core_len)
		return agx_bad(p, "incomplete performance tables");
	memcpy(core, core_data, core_len);
	memcpy(sram, sram_data, sram_len);
	if (core[0].freq)
		return agx_bad(p, "no off state");

	for (first = 0; first < rows; first = last + 1) {
		u32 volt = le32_to_cpu(core[first].volt);

		last = first;
		while (last + 1 < rows && core[last + 1].volt == core[first].volt)
			last++;

		low = first;
		high = first;
		for (row = first; row <= last; row++) {
			u32 freq = le32_to_cpu(core[row].freq);

			if (sram[row].freq != core[row].freq || sram[row].volt != sram[first].volt)
				return agx_bad(p, "core and SRAM tables disagree");
			if (freq % AGX_MHZ)
				return agx_bad(p, "frequency not in whole MHz");
			if (freq < le32_to_cpu(core[low].freq))
				low = row;
			if (freq > le32_to_cpu(core[high].freq))
				high = row;
		}

		b->freq_high[n] = le32_to_cpu(core[high].freq) / AGX_MHZ;
		b->freq_low[n] = le32_to_cpu(core[low].freq) / AGX_MHZ;
		b->row_high[n] = high;
		b->row_low[n] = low;
		b->volt_core[n] = volt;
		b->volt_sram[n] = le32_to_cpu(sram[first].volt);

		if (n && (volt <= b->volt_core[n - 1] ||
			  b->freq_high[n] <= b->freq_high[n - 1] ||
			  b->freq_low[n] <= b->freq_low[n - 1]))
			return agx_bad(p, "unordered performance states");
		n++;
	}
	if (n < 2)
		return agx_bad(p, "no active performance state");
	b->nr_states = n;

	ret = agx_cells(p, "apple,relative-power-high", b->power_high, n);
	if (ret)
		return ret;
	ret = agx_cells(p, "apple,relative-power-low", b->power_low, n);
	if (ret)
		return ret;
	for (row = 0; row < n; row++)
		if (b->power_high[row] > 100 || b->power_low[row] > 100)
			return agx_bad(p, "relative power above 100%");
	return 0;
}

static int agx_parse_sampling(struct agx_board *b, const struct agx_props *p)
{
	u32 periods;
	int ret;

	ret = agx_le32_prop(p, "apple,sgx-gpu-pwr-sample-period-aic-clks", &b->period_clocks);
	if (ret == -ENOENT) {
		ret = agx_le32_prop(p, "apple,sgx-gpu-power-sample-period", &periods);
		if (ret)
			return agx_bad(p, "no power sample period");
		if (periods > AGX_MAX_PERIODS_MS)
			return agx_bad(p, "power sample period too long");
		b->period_clocks = periods * AGX_CLOCKS_PER_MS;
	} else if (ret) {
		return ret;
	}
	b->period_ms = b->period_clocks / AGX_CLOCKS_PER_MS;
	if (!b->period_ms)
		return agx_bad(p, "power sample period below 1 ms");
	return 0;
}

static int agx_parse_windows(struct agx_board *b, const struct agx_props *p, u64 base, u64 size)
{
	const void *irq;
	__le64 addr;
	u32 mask, bit;
	int len, ret;

	if (size < AGX_SGX_MIN_SIZE)
		return agx_bad(p, "sgx window too small");
	b->sgx_base = base;

	irq = p->get(p->ctx, "apple,sgx-meta-sw-interrupt", &len);
	if (!irq || (len != 20 && len != 40))
		return agx_bad(p, "apple,sgx-meta-sw-interrupt");
	memcpy(&addr, irq + len - 20, sizeof(addr));
	b->irq_page = le64_to_cpu(addr) & ~(u64)(AGX_PAGE - 1);
	if (!b->irq_page || b->irq_page >> 42)
		return agx_bad(p, "interrupt window out of range");

	ret = agx_le32_default(p, "apple,sgx-active-mcc-bit-vector", 3, &mask);
	if (ret)
		return ret;
	if (!mask || mask >> AGX_MAX_MCC || mask & (mask + 1))
		return agx_bad(p, "unsupported MCC mask");
	for (bit = 0; mask >> bit; bit++)
		b->mcc[bit] = AGX_MCC_BASE + ((u64)bit << AGX_MCC_SHIFT);
	b->nr_mcc = bit;
	return 0;
}

static bool agx_filter_ok(const struct agx_board *b, u32 count)
{
	return count && count <= AGX_MAX_PERIODS_MS / b->period_ms;
}

static int agx_parse_policy(struct agx_board *b, const struct agx_props *p)
{
	struct agx_policy *c = &b->policy;
	u32 max = 100 * agx_board_top_state(b);
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(agx_tunables); i++) {
		ret = agx_le32_default(p, agx_tunables[i].name, agx_tunables[i].fallback,
				       (u32 *)((u8 *)c + agx_tunables[i].offset));
		if (ret)
			return ret;
	}

	c->avg_ki_per_second = true;
	ret = agx_le32_prop(p, "apple,sgx-gpu-avg-power-ki-only", &c->avg_ki.bits);
	if (ret == -ENOENT) {
		ret = agx_le32_prop(p, "apple,sgx-gpu-avg-power-ki", &c->avg_ki.bits);
		if (!ret)
			c->avg_ki_per_second = false;
		else if (ret == -ENOENT)
			c->avg_ki = AGX_F32(0x41870000);
	}
	if (ret && ret != -ENOENT)
		return ret;

	ret = agx_le32_prop(p, "apple,sgx-gpu-avg-power-filter-tc-ms", &c->avg_filter_ms);
	if (!ret) {
		c->avg_filter = c->avg_filter_ms / b->period_ms;
	} else if (ret == -ENOENT) {
		ret = agx_le32_default(p, "apple,sgx-gpu-avg-power-input-filter-tc",
				       248 / b->period_ms, &c->avg_filter);
		if (ret)
			return ret;
	} else {
		return ret;
	}

	ret = agx_le32_prop(p, "apple,sgx-gpu-turbo-controller-input", &c->turbo_input);
	if (!ret)
		c->has_turbo_input = true;
	else if (ret == -ENOENT)
		c->turbo_input = 2;
	else
		return ret;

	if (c->pwr_min_duty > max || c->avg_min_duty > max)
		return agx_bad(p, "minimum duty above the top state");
	if (c->pwr_integral_min > b->max_power_mw)
		return agx_bad(p, "power clamp above the limit");
	if (!c->perf_base || c->perf_base > agx_board_top_state(b))
		return agx_bad(p, "base performance state out of range");
	if (c->perf_utilization > 100 || c->perf_dead_zone > c->perf_utilization ||
	    c->perf_boost_util > 100 || c->perf_integral_min > 95)
		return agx_bad(p, "performance policy out of range");
	if (c->fast_die_release_c < 30 || c->fast_die_release_c >= AGX_FAST_DIE_LIMIT_C ||
	    c->fast_die_delta > AGX_FAST_DIE_LIMIT_C)
		return agx_bad(p, "thermal policy out of range");

	if (!agx_filter_ok(b, c->pwr_filter) ||
	    !agx_filter_ok(b, c->ppm_filter_ms / b->period_ms) ||
	    !agx_filter_ok(b, c->perf_filter[0]) || !agx_filter_ok(b, c->perf_filter[1]) ||
	    !agx_filter_ok(b, c->se_filter[0]) || !agx_filter_ok(b, c->se_filter[1]) ||
	    !agx_filter_ok(b, c->avg_target_filter) || !agx_filter_ok(b, c->avg_filter))
		return agx_bad(p, "filter length out of range");

	if (!agx_f32_finite(c->pwr_ki) || !agx_f32_finite(c->pwr_kp) ||
	    !agx_f32_finite(c->ppm_ki) || !agx_f32_finite(c->ppm_kp) ||
	    !agx_f32_finite(c->perf_ki[0]) || !agx_f32_finite(c->perf_ki[1]) ||
	    !agx_f32_finite(c->perf_kp[0]) || !agx_f32_finite(c->perf_kp[1]) ||
	    !agx_f32_finite(c->se_ki[0]) || !agx_f32_finite(c->se_ki[1]) ||
	    !agx_f32_finite(c->se_kp[0]) || !agx_f32_finite(c->se_kp[1]) ||
	    !agx_f32_finite(c->avg_ki) || !agx_f32_finite(c->avg_kp) ||
	    !agx_f32_finite(c->fast_die_ki) || !agx_f32_finite(c->fast_die_kp))
		return agx_bad(p, "controller gain is not finite");
	return 0;
}

int agx_board_parse(struct agx_board *board, agx_prop_fn prop, const void *ctx,
		    u64 sgx_base, u64 sgx_size, const char **reason)
{
	const struct agx_props p = { .get = prop, .ctx = ctx, .reason = reason };
	int ret;

	memset(board, 0, sizeof(*board));
	*reason = "";

	ret = agx_parse_identity(board, &p);
	if (ret)
		return ret;
	ret = agx_parse_limits(board, &p);
	if (ret)
		return ret;
	ret = agx_parse_states(board, &p);
	if (ret)
		return ret;
	ret = agx_parse_sampling(board, &p);
	if (ret)
		return ret;
	ret = agx_parse_windows(board, &p, sgx_base, sgx_size);
	if (ret)
		return ret;
	return agx_parse_policy(board, &p);
}
