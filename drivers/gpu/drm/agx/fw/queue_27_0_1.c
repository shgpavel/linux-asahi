// SPDX-License-Identifier: GPL-2.0-only

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "fw_27_0_1.h"

static const struct {
	u64 mask;
	u32 first;
	u32 last;
} fw27_priorities[AGX_NR_PRIORITIES] = {
	{ 0xffffffffffff0000, 1, 1 },
	{ 0xffffffff00000000, 0, 0 },
	{ 0xffff000000000000, 0, 2 },
	{ 0x0000000000000000, 0, 3 },
};

int fw27_queue(void *out, const struct agx_queue_va *va)
{
	struct fw27_queue *q = out;

	if (va->priority >= AGX_NR_PRIORITIES)
		return -EINVAL;

	memset(q, 0, sizeof(*q));
	q->pointers = cpu_to_le64(va->pointers);
	q->ring = cpu_to_le64(va->ring);
	q->jobs = cpu_to_le64(va->jobs);
	q->private = cpu_to_le64(va->private);
	q->unk_2c = cpu_to_le32(U32_MAX);
	q->priority[0] = cpu_to_le32(va->priority);
	q->priority[1] = cpu_to_le32(va->priority);
	q->priority_mask = cpu_to_le64(fw27_priorities[va->priority].mask);
	q->priority_first = cpu_to_le32(fw27_priorities[va->priority].first);
	q->priority_last = cpu_to_le32(fw27_priorities[va->priority].last);
	q->unk_48 = cpu_to_le32(U32_MAX);
	q->uuid = cpu_to_le32(va->uuid);
	q->context = cpu_to_le64(va->context);
	return 0;
}

void fw27_queue_pointers(void *out, u32 capacity, u32 write)
{
	struct fw27_queue_pointers *p = out;

	memset(p, 0, sizeof(*p));
	p->write = cpu_to_le32(write);
	p->capacity = cpu_to_le32(capacity);
}

void fw27_queue_context(void *out)
{
	struct fw27_queue_context *c = out;

	memset(c, 0, sizeof(*c));
	c->slot = cpu_to_le16(U16_MAX);
	c->unk_5 = 1;
	c->unk_26 = 2;
	c->unk_33 = 0xff;
}

void fw27_queue_jobs(void *out, u64 self)
{
	struct fw27_queue_jobs *j = out;

	memset(j, 0, sizeof(*j));
	j->self = cpu_to_le64(self);
}

void fw27_barrier(void *out, const struct agx_wait *wait)
{
	struct fw27_barrier *b = out;

	memset(b, 0, sizeof(*b));
	b->kind = cpu_to_le32(FW27_ITEM_WAIT);
	b->stamp = cpu_to_le64(wait->stamp);
	b->value = cpu_to_le32(wait->value);
	b->event = cpu_to_le32(wait->event);
	b->stamp_self = cpu_to_le32(wait->stamp_self);
	b->uuid = cpu_to_le32(wait->uuid);
}

void fw27_dependency(void *out, const struct agx_wait *wait)
{
	struct fw27_dependency *d = out;

	memset(d, 0, sizeof(*d));
	d->kind = cpu_to_le32(FW27_ITEM_WAIT);
	d->stamp[0] = cpu_to_le64(wait->stamp);
	d->stamp[1] = cpu_to_le64(wait->stamp);
	d->value = cpu_to_le32(wait->value);
	d->event = cpu_to_le32(wait->event);
	d->stamp_self = cpu_to_le32(wait->stamp_self);
	d->uuid = cpu_to_le32(wait->uuid);
	d->wait_kind = cpu_to_le32(wait->kind);
}

void fw27_ta_prelude(void *out, const struct agx_render_hw *hw, u32 blocks)
{
	struct fw27_ta_prelude *p = out;

	memset(p, 0, sizeof(*p));
	p->kind = cpu_to_le32(FW27_ITEM_TA_PRELUDE);
	p->context = cpu_to_le32(hw->context);
	p->pool_slot = cpu_to_le32(hw->pool_slot);
	p->blocks = cpu_to_le32(blocks);
	p->pool = cpu_to_le64(hw->pool);
	p->stamp = cpu_to_le32(hw->ta.stamp);
}

void fw27_fragment_prelude(void *out, const struct agx_render_hw *hw)
{
	struct agx_wait wait = {
		.stamp = hw->ta.fw_stamp,
		.value = hw->ta.stamp,
		.event = hw->ta.event,
		.stamp_self = hw->fragment.stamp,
		.uuid = hw->fragment.uuid,
	};

	fw27_dependency(out, &wait);
}

void fw27_channel_msg(void *out, const struct agx_channel_kick *kick)
{
	struct fw27_channel_msg *m = out;

	m->tag = cpu_to_le64(kick->tag);
	m->queue = cpu_to_le64(kick->queue);
	m->stage = cpu_to_le32(kick->stage);
	m->head = cpu_to_le16(kick->head);
	m->event = kick->event;
	m->first = kick->first;
}

void fw27_notifier(void *out, u64 threshold, u32 generation, u32 count, u32 context)
{
	struct fw27_notifier *n = out;

	memset(n, 0, sizeof(*n));
	n->threshold = cpu_to_le64(threshold);
	n->generation = cpu_to_le32(generation);
	n->count = cpu_to_le32(count);
	n->unk_10 = cpu_to_le32(0x50);
	n->context = cpu_to_le32(context);
}
