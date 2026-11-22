// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "fw_27_0_1.h"

enum fw27_window_base {
	FW27_WIN_IRQ,
	FW27_WIN_MCC,
	FW27_WIN_SGX,
	FW27_WIN_SGX_ABS,
	FW27_WIN_SOC,
};

static const struct {
	u16 slot;
	u8 base;
	u8 access;
	u32 size;
	u64 addr;
} fw27_window_table[] = {
	{  0, FW27_WIN_IRQ, AGX_WINDOW_RW, 0x4000, 0 },
	{  3, FW27_WIN_MCC, AGX_WINDOW_RW, FW27_MCC_SIZE, 0 },
	{  9, FW27_WIN_SOC, AGX_WINDOW_RW, 0x1000, 0x3803d0000 },
	{ 10, FW27_WIN_SOC, AGX_WINDOW_RO, 0x2000, 0x3803c0000 },
	{ 12, FW27_WIN_SOC, AGX_WINDOW_RW, 0x4000, 0x50165c000 },
	{ 14, FW27_WIN_SOC, AGX_WINDOW_RO, 0x8000, 0x380280000 },
	{ 15, FW27_WIN_SOC, AGX_WINDOW_RO, 0x24000, 0x38c840000 },
	{ 17, FW27_WIN_SGX, AGX_WINDOW_RW, 0x20000, 0 },
	{ 22, FW27_WIN_SGX_ABS, AGX_WINDOW_RW, 0x8000, 0x1000000 },
	{ 31, FW27_WIN_SGX, AGX_WINDOW_RW, 0x8000, 0xd04000 },
	{ 32, FW27_WIN_SGX, AGX_WINDOW_RW, 0x1000, 0xd0d000 },
	{ 33, FW27_WIN_SGX, AGX_WINDOW_RW, 0x8000, 0xd58000 },
	{ 34, FW27_WIN_SGX, AGX_WINDOW_RW, 0x4000, 0xd10000 },
	{ 36, FW27_WIN_SGX, AGX_WINDOW_RW, 0x4000, 0xd40000 },
	{ 37, FW27_WIN_SGX, AGX_WINDOW_RW, 0x8000, 0xd60000 },
	{ 41, FW27_WIN_SGX, AGX_WINDOW_RO, 0x4000, 0xe00000 },
	{ 44, FW27_WIN_SGX_ABS, AGX_WINDOW_RW, 0x8000, 0xe08000 },
	{ 45, FW27_WIN_SGX, AGX_WINDOW_RW, 0x4000, 0xe1c000 },
	{ 46, FW27_WIN_SGX_ABS, AGX_WINDOW_RW, 0x4000, 0xe1f800 },
};

static_assert(ARRAY_SIZE(fw27_window_table) == AGX_NR_WINDOWS);

int fw27_windows(const struct agx_board *board, u64 base, struct agx_window *out)
{
	u64 cursor = base;
	int i;

	for (i = 0; i < AGX_NR_WINDOWS; i++) {
		struct agx_window *w = &out[i];
		u64 addr = fw27_window_table[i].addr;
		u64 skew;

		w->slot = fw27_window_table[i].slot;
		w->access = fw27_window_table[i].access;
		w->size = fw27_window_table[i].size;
		w->stride = w->size;
		w->offset = 0;

		switch (fw27_window_table[i].base) {
		case FW27_WIN_IRQ:
			w->phys = board->irq_page;
			break;
		case FW27_WIN_MCC:
			w->phys = board->mcc[0];
			w->size *= board->nr_mcc;
			break;
		case FW27_WIN_SGX:
			w->offset = addr;
			fallthrough;
		case FW27_WIN_SGX_ABS:
			w->phys = board->sgx_base + addr;
			break;
		case FW27_WIN_SOC:
			w->phys = addr;
			break;
		}

		skew = w->phys & (FW27_PAGE - 1);
		w->va = cursor + skew;
		cursor += (skew + w->size + FW27_PAGE - 1) & ~(u64)(FW27_PAGE - 1);
		cursor += FW27_PAGE;
		if (cursor - base > FW27_VA_WINDOW_LIMIT)
			return -ENOSPC;
	}
	return 0;
}

void fw27_handoff(void *page)
{
	struct fw27_handoff *h = page;

	memset(h, 0, sizeof(*h));
	h->magic_ap = cpu_to_le64(FW27_HANDOFF_MAGIC);
	h->cur_slot = cpu_to_le32(FW27_HANDOFF_NO_SLOT);
	h->unk_638 = 1;
}

static void fw27_root(struct fw27_root *r, const struct agx_graph_va *va)
{
	static const struct {
		u8 shift;
		u16 entries;
	} levels[] = { { 36, 64 }, { 25, 2048 }, { 14, 2048 } };
	int i;

	memset(r, 0, sizeof(*r));
	r->tag = cpu_to_le64(FW27_ROOT_TAG);
	r->region_a = cpu_to_le64(va->region_a);
	r->main = cpu_to_le64(va->main);
	r->region_c = cpu_to_le64(va->region_c);
	r->present = cpu_to_le32(1);
	r->page_size = cpu_to_le16(FW27_PAGE);
	r->page_shift = 14;
	r->nr_levels = ARRAY_SIZE(levels);

	for (i = 0; i < ARRAY_SIZE(levels); i++) {
		struct fw27_level *l = &r->level[i];

		l->entry_size = 8;
		l->page_shift = 14;
		l->table_shift = 14;
		l->shift = levels[i].shift;
		l->entries = cpu_to_le16(levels[i].entries);
		l->page_size = cpu_to_le16(FW27_PAGE);
		l->valid = cpu_to_le64(1);
		l->addr_mask = cpu_to_le64(GENMASK_ULL(41, 14));
		l->index_mask = cpu_to_le64((u64)(levels[i].entries - 1) << levels[i].shift);
	}

	r->status_a = cpu_to_le64(va->status_a);
	r->status_b = cpu_to_le64(va->status_b);
	r->extra0 = cpu_to_le64(va->extra0);
	r->extra1 = cpu_to_le64(va->extra1);
}

static void fw27_channel(struct fw27_channel *ch, const struct agx_graph_va *va,
			 u32 done, u32 tail, u64 ring)
{
	ch->read = cpu_to_le64(va->done_cursors + done);
	ch->done = cpu_to_le64(va->done_cursors + done + 8);
	ch->tail = cpu_to_le64(va->tail_cursors + tail);
	ch->ring = cpu_to_le64(ring);
}

static void fw27_pair(struct fw27_ring_pair *pair, u64 state, u64 ring)
{
	pair->state = cpu_to_le64(state);
	pair->ring = cpu_to_le64(ring);
}

static void fw27_main(struct fw27_main *m, const struct agx_graph_va *va)
{
	int prio, stage, i;

	memset(m, 0, sizeof(*m));
	m->hwdata = cpu_to_le64(va->hwdata);
	m->pipe[0] = cpu_to_le64(va->pipe);
	m->pipe[1] = cpu_to_le64(va->pipe);
	m->cursor_header = cpu_to_le64(va->tail_cursors + 0x68);

	for (prio = 0; prio < AGX_NR_PRIORITIES; prio++) {
		for (stage = 0; stage < AGX_NR_STAGES; stage++) {
			i = prio * AGX_NR_STAGES + stage;
			fw27_channel(&m->work[i], va, stage * 0x40 + prio * 0x10,
				     stage * 0x20 + prio * 8, va->work_ring[i]);
		}
	}
	fw27_channel(&m->control, va, 0xc0, 0x60, va->control_ring);
	fw27_pair(&m->event, va->event_state, va->event_ring);
	fw27_pair(&m->log, va->log_state, va->log_ring);
	fw27_pair(&m->trace, va->trace_state, va->trace_ring);
	fw27_pair(&m->stats, va->stats_state, va->stats_ring);
	m->channel15 = cpu_to_le64(va->channel15);

	for (i = 0; i < ARRAY_SIZE(m->stats_obj); i++)
		m->stats_obj[i] = cpu_to_le64(va->stats[i]);

	m->region_2c8 = cpu_to_le64(va->region_2c8);
	m->slot_table_gpu = cpu_to_le64(va->slot_table_gpu);
	m->slot_table = cpu_to_le64(va->slot_table);
	m->shared_gpu = cpu_to_le64(va->shared_gpu);
	m->shared = cpu_to_le64(va->shared);
	m->unk_2f8 = cpu_to_le32(4);
	m->unk_3d8 = cpu_to_le32(0xff);
	m->power = cpu_to_le64(va->power);
}

static void fw27_hwdata(struct fw27_hwdata *h, const struct agx_board *board,
			const struct agx_graph_va *va, const struct agx_window *windows)
{
	u32 top = agx_board_top_state(board);
	int i, j;

	memset(h, 0, sizeof(*h));
	h->timestamps = cpu_to_le64(va->timestamps);

	for (i = 0; i < AGX_NR_WINDOWS; i++) {
		const struct agx_window *w = &windows[i];
		struct fw27_io_window *io = &h->io[w->slot];

		io->phys = cpu_to_le64(w->phys);
		io->va = cpu_to_le64(w->va);
		io->size = cpu_to_le32(w->size);
		io->stride = cpu_to_le32(w->stride);
		io->offset = cpu_to_le64(w->offset);
		io->flags = cpu_to_le32(w->access == AGX_WINDOW_RW ? FW27_IO_RW : 0);
	}

	h->chip_id = cpu_to_le32(0x8132);
	h->rev_major = cpu_to_le32(board->revision >> 4);
	h->rev_minor = cpu_to_le32(board->revision & 7);
	h->gpc = cpu_to_le32(board->gpc);
	h->unk_fd8 = cpu_to_le32(1);
	h->unk_fe4 = cpu_to_le32(1);
	h->clocks_per_ms = cpu_to_le32(AGX_CLOCKS_PER_MS);
	h->period_ms = cpu_to_le32(board->period_ms);
	for (i = 0; i < ARRAY_SIZE(h->unk_ffc); i++)
		h->unk_ffc[i] = cpu_to_le32(1);

	h->top_state = cpu_to_le32(top);
	h->second.top_state = cpu_to_le32(top);
	for (i = 0; i < board->nr_states; i++) {
		__le32 core = cpu_to_le32(board->volt_core[i]);
		__le32 sram = cpu_to_le32(board->volt_sram[i]);

		h->freq_high[i] = cpu_to_le32(board->freq_high[i]);
		h->freq_low[i] = cpu_to_le32(board->freq_low[i]);
		h->state_scale[i] = cpu_to_le32(AGX_F32_1_02.bits);
		h->power_high[i] = cpu_to_le32(agx_f32_from_u32(board->power_high[i]).bits);
		h->power_low[i] = cpu_to_le32(agx_f32_from_u32(board->power_low[i]).bits);
		h->row_high[i] = cpu_to_le32(board->row_high[i]);
		h->row_low[i] = cpu_to_le32(board->row_low[i]);
		for (j = 0; j < 16; j++) {
			h->volt_core[i][j] = core;
			h->volt_sram[i][j] = sram;
		}

		h->second.freq[i] = cpu_to_le32(board->freq_low[i]);
		h->second.volt_core[i][0] = core;
		h->second.volt_sram[i][0] = sram;
	}

	h->unk_26aa = cpu_to_le16(22);
	for (i = 0; i < ARRAY_SIZE(h->group); i++)
		for (j = 1; j < 4; j++)
			h->group[i].counters[j] = cpu_to_le32(1);
	for (i = 0; i < ARRAY_SIZE(h->unk_26d0); i++)
		h->unk_26d0[i] = cpu_to_le32(U32_MAX);
	h->unk_2724 = cpu_to_le32(1);
}

static void fw27_region_c(struct fw27_region_c *c, const struct agx_board *board)
{
	memset(c, 0, sizeof(*c));
	c->unk_24 = cpu_to_le32(4096);
	c->idle_enable = cpu_to_le32(1);
	c->unk_58 = cpu_to_le16(U16_MAX);
	c->unk_5a = cpu_to_le16(40);
	c->unk_5c = cpu_to_le16(U16_MAX);
	c->unk_7c = cpu_to_le32(1);
	c->max_power = cpu_to_le32(board->max_power_mw);
	c->unk_8c = cpu_to_le32(1000);
	c->unk_90 = cpu_to_le32(1000);
	c->unk_9c = cpu_to_le32(1000);
	c->unk_7f0 = cpu_to_le32(1);
	c->unk_7f4 = cpu_to_le32(1);
	c->unk_99c[0] = cpu_to_le32(32);
	c->unk_99c[1] = cpu_to_le32(8);
	c->unk_99c[2] = cpu_to_le32(256);
	c->unk_99c[3] = cpu_to_le32(1);
	c->off_delay_ms = cpu_to_le32(40);
	c->unk_9cc[0] = cpu_to_le32(64);
	c->unk_9cc[1] = cpu_to_le32(64);
	c->unk_9cc[2] = cpu_to_le32(2);
	c->fault_control = cpu_to_le32(3);
	c->unk_e30 = cpu_to_le32(1);
}

int fw27_graph(const struct agx_board *board, const struct agx_graph_va *va, u32 entropy,
	       const struct agx_graph_mem *mem)
{
	struct agx_window windows[AGX_NR_WINDOWS];
	struct fw27_control *control = mem->control_ring;
	struct fw27_status_a *a = mem->status_a;
	struct fw27_status_b *b = mem->status_b;
	int ret;

	ret = fw27_windows(board, va->windows, windows);
	if (ret)
		return ret;

	fw27_root(mem->root, va);
	fw27_main(mem->main, va);
	fw27_hwdata(mem->hwdata, board, va, windows);
	fw27_power(mem->power, board, va, entropy);
	fw27_extra0(mem->extra0, mem->power);
	fw27_region_c(mem->region_c, board);

	memset(a, 0, sizeof(*a));
	a->present = cpu_to_le32(1);
	memset(b, 0, sizeof(*b));
	b->present = cpu_to_le32(1);

	memset(control, 0, FW27_CTRL_ENTRIES * sizeof(*control));
	control->opcode = cpu_to_le32(FW27_CTRL_INIT);
	return 0;
}

void fw27_map_notify(void *status_b, u64 state, u64 ring)
{
	struct fw27_status_b *b = status_b;

	b->map_state = cpu_to_le64(state);
	b->map_ring = cpu_to_le64(ring);
	b->map_armed = cpu_to_le32(1);
}

void fw27_opening_flist(void *out, const struct agx_flist_va *va)
{
	struct fw27_flist *f = out;

	memset(f, 0, sizeof(*f));
	f->unk_0 = cpu_to_le32(1);
	f->directory = cpu_to_le64(va->directory);
	f->entries = cpu_to_le32(va->entries);
	f->unk_28 = cpu_to_le32(1);
	f->table = cpu_to_le64(va->table);
	f->aux = cpu_to_le64(va->aux);
}

void fw27_opening_control(void *out, const struct agx_flist_va *va)
{
	struct fw27_control *c = out;

	memset(c, 0, 2 * sizeof(*c));
	c[0].opcode = cpu_to_le32(FW27_CTRL_INIT);
	c[1].opcode = cpu_to_le32(FW27_CTRL_OPENING);
	c[1].open_context = cpu_to_le32(1);
	c[1].open_mask = cpu_to_le32(0x3f);
	c[1].open_flist = cpu_to_le64(va->flist);
	c[1].open_table[0] = cpu_to_le64(va->table);
	c[1].open_table[1] = cpu_to_le64(va->table);
	c[1].open_count = cpu_to_le32(va->pages);
	c[1].open_enable = cpu_to_le32(1);
}

void fw27_destroy_context(void *out, u64 context, const u8 state[AGX_NR_CONTEXT_STATE])
{
	struct fw27_control *c = out;

	memset(c, 0, sizeof(*c));
	c->opcode = cpu_to_le32(FW27_CTRL_DESTROY);
	memcpy(c->destroy_state, state, AGX_NR_CONTEXT_STATE);
	c->destroy_context = cpu_to_le64(context);
}
