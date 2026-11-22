/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __AGX_F32_H__
#define __AGX_F32_H__

#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/types.h>

typedef struct {
	u32 bits;
} agx_f32;

#define AGX_F32(b)		((agx_f32){ (b) })
#define AGX_F32_ZERO		AGX_F32(0x00000000)
#define AGX_F32_1		AGX_F32(0x3f800000)
#define AGX_F32_1_02		AGX_F32(0x3f828f5c)
#define AGX_F32_1_2		AGX_F32(0x3f99999a)
#define AGX_F32_5		AGX_F32(0x40a00000)
#define AGX_F32_15		AGX_F32(0x41700000)
#define AGX_F32_64		AGX_F32(0x42800000)
#define AGX_F32_95		AGX_F32(0x42be0000)
#define AGX_F32_100		AGX_F32(0x42c80000)
#define AGX_F32_1000		AGX_F32(0x447a0000)
#define AGX_F32_3000		AGX_F32(0x453b8000)
#define AGX_F32_24000		AGX_F32(0x46bb8000)
#define AGX_F32_65536		AGX_F32(0x47800000)
#define AGX_F32_INF		0x7f800000u

static inline bool agx_f32_finite(agx_f32 a)
{
	return (a.bits & AGX_F32_INF) != AGX_F32_INF;
}

static inline agx_f32 agx_f32_neg(agx_f32 a)
{
	return AGX_F32(a.bits ^ BIT(31));
}

static inline u32 agx_f32_split(agx_f32 a, int *exp)
{
	u32 m = a.bits & GENMASK(22, 0);
	int e = (a.bits >> 23) & 0xff;

	if (e) {
		m |= BIT(23);
	} else if (m) {
		e = 1;
		while (!(m & BIT(23))) {
			m <<= 1;
			e--;
		}
	}
	*exp = e - 150;
	return m;
}

static inline agx_f32 agx_f32_make(u32 sign, int exp, u64 m)
{
	int len, biased, shift;
	u64 q, rem, half;

	if (!m)
		return AGX_F32(sign);

	len = fls64(m);
	biased = exp + len + 126;
	shift = len - 24;
	if (biased <= 0) {
		shift += 1 - biased;
		biased = 0;
	}

	if (shift > 63) {
		q = 0;
	} else if (shift > 0) {
		q = m >> shift;
		rem = m & ((1ULL << shift) - 1);
		half = 1ULL << (shift - 1);
		if (rem > half || (rem == half && (q & 1)))
			q++;
	} else {
		q = m << -shift;
	}

	if (!biased)
		return AGX_F32(sign | (u32)q);
	if (q == BIT(24)) {
		q >>= 1;
		biased++;
	}
	if (biased >= 0xff)
		return AGX_F32(sign | AGX_F32_INF);
	return AGX_F32(sign | (u32)biased << 23 | ((u32)q & GENMASK(22, 0)));
}

static inline agx_f32 agx_f32_from_u32(u32 v)
{
	return agx_f32_make(0, 0, v);
}

static inline agx_f32 agx_f32_mul(agx_f32 a, agx_f32 b)
{
	u32 sign = (a.bits ^ b.bits) & BIT(31);
	int ea, eb;
	u32 ma = agx_f32_split(a, &ea);
	u32 mb = agx_f32_split(b, &eb);

	if (!agx_f32_finite(a) || !agx_f32_finite(b))
		return AGX_F32(sign | AGX_F32_INF);
	return agx_f32_make(sign, ea + eb, (u64)ma * mb);
}

static inline agx_f32 agx_f32_div(agx_f32 a, agx_f32 b)
{
	u32 sign = (a.bits ^ b.bits) & BIT(31);
	int ea, eb;
	u32 ma = agx_f32_split(a, &ea);
	u32 mb = agx_f32_split(b, &eb);
	u64 q, rem;

	if (!agx_f32_finite(a) || !agx_f32_finite(b) || !mb)
		return AGX_F32(sign | AGX_F32_INF);
	q = div64_u64_rem((u64)ma << 38, mb, &rem);
	return agx_f32_make(sign, ea - eb - 38, q | !!rem);
}

#endif
