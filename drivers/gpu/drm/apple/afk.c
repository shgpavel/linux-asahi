// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2022 Sven Peter <sven@svenpeter.dev> */

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/kconfig.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/soc/apple/rtkit.h>

#include <linux/unaligned.h>

#include "afk.h"
#include "trace.h"

struct afk_receive_message_work {
	struct apple_dcp_afkep *ep;
	u64 message;
	struct work_struct work;
};

#define RBEP_TYPE GENMASK(63, 48)

enum rbep_msg_type {
	RBEP_INIT = 0x80,
	RBEP_INIT_ACK = 0xa0,
	RBEP_GETBUF = 0x89,
	RBEP_GETBUF_ACK = 0xa1,
	RBEP_INIT_TX = 0x8a,
	RBEP_INIT_RX = 0x8b,
	RBEP_START = 0xa3,
	RBEP_START_ACK = 0x86,
	RBEP_SEND = 0xa2,
	RBEP_RECV = 0x85,
	RBEP_SHUTDOWN = 0xc0,
	RBEP_SHUTDOWN_ACK = 0xc1,
};

#define BLOCK_SHIFT 6

#define GETBUF_SIZE GENMASK(31, 16)
#define GETBUF_TAG GENMASK(15, 0)
#define GETBUF_ACK_DVA GENMASK(47, 0)

#define INITRB_OFFSET GENMASK(47, 32)
#define INITRB_SIZE GENMASK(31, 16)
#define INITRB_TAG GENMASK(15, 0)

#define SEND_WPTR GENMASK(31, 0)

static __le32 *afk_rb_rptr(struct afk_ringbuffer *bfr)
{
	return bfr->base + bfr->blksz;
}

static __le32 *afk_rb_wptr(struct afk_ringbuffer *bfr)
{
	return bfr->base + 2 * bfr->blksz;
}

static void afk_send(struct apple_dcp_afkep *ep, u64 message)
{
	dcp_send_message(ep->dcp, ep->endpoint, message);
}

struct apple_dcp_afkep *afk_init(struct apple_dcp *dcp, u32 endpoint,
				 const struct apple_epic_service_ops *ops)
{
	struct apple_dcp_afkep *afkep;
	int ret;

	afkep = devm_kzalloc(dcp->dev, sizeof(*afkep), GFP_KERNEL);
	if (!afkep)
		return ERR_PTR(-ENOMEM);

	afkep->ops = ops;
	afkep->dcp = dcp;
	afkep->endpoint = endpoint;
	afkep->wq = alloc_ordered_workqueue("apple-dcp-afkep%02x",
					    WQ_MEM_RECLAIM, endpoint);
	if (!afkep->wq) {
		ret = -ENOMEM;
		goto out_free_afkep;
	}

	// TODO: devm_ for wq

	init_completion(&afkep->started);
	init_completion(&afkep->stopped);
	spin_lock_init(&afkep->lock);

	return afkep;

out_free_afkep:
	devm_kfree(dcp->dev, afkep);
	return ERR_PTR(ret);
}

void afk_shutdown(struct apple_dcp_afkep *afkep)
{
	afk_send(afkep, FIELD_PREP(RBEP_TYPE, RBEP_SHUTDOWN));
	int ret, i;

	ret = wait_for_completion_timeout(&afkep->stopped, msecs_to_jiffies(1000));
	if (ret <= 0) {
		dev_err(afkep->dcp->dev, "Timed out shutting down AFK endpoint %02x", afkep->endpoint);
	}

	destroy_workqueue(afkep->wq);

	for (i = 0; i < AFK_MAX_CHANNEL; i++) {
		kvfree(afkep->frag[i].buf);
		afkep->frag[i].buf = NULL;
		afkep->frag[i].active = false;
	}
}

int afk_start(struct apple_dcp_afkep *ep)
{
	int ret;

	reinit_completion(&ep->started);
	apple_rtkit_start_ep(ep->dcp->rtk, ep->endpoint);
	afk_send(ep, FIELD_PREP(RBEP_TYPE, RBEP_INIT));

	ret = wait_for_completion_timeout(&ep->started, msecs_to_jiffies(1000));
	if (ret <= 0)
		return -ETIMEDOUT;
	else
		return 0;
}

static void afk_getbuf(struct apple_dcp_afkep *ep, u64 message)
{
	u32 size = FIELD_GET(GETBUF_SIZE, message) << BLOCK_SHIFT;
	u16 tag = FIELD_GET(GETBUF_TAG, message);
	u64 reply;

	trace_afk_getbuf(ep, size, tag);

	if (ep->bfr) {
		dev_err(ep->dcp->dev,
			"Got GETBUF message but buffer already exists\n");
		return;
	}

	ep->bfr = dmam_alloc_coherent(ep->dcp->dev, size, &ep->bfr_dma,
				      GFP_KERNEL);
	if (!ep->bfr) {
		dev_err(ep->dcp->dev, "Failed to allocate %u bytes buffer\n",
			size);
		return;
	}

	ep->bfr_size = size;
	ep->bfr_tag = tag;

	reply = FIELD_PREP(RBEP_TYPE, RBEP_GETBUF_ACK);
	reply |= FIELD_PREP(GETBUF_ACK_DVA, ep->bfr_dma);
	afk_send(ep, reply);
}

static void afk_init_rxtx(struct apple_dcp_afkep *ep, u64 message,
			  struct afk_ringbuffer *bfr)
{
	u32 base = FIELD_GET(INITRB_OFFSET, message) << BLOCK_SHIFT;
	u32 size = FIELD_GET(INITRB_SIZE, message) << BLOCK_SHIFT;
	u16 tag = FIELD_GET(INITRB_TAG, message);
	u32 bufsz, end;

	if (tag != ep->bfr_tag) {
		dev_err(ep->dcp->dev, "AFK[ep:%02x]: expected tag 0x%x but got 0x%x\n",
			ep->endpoint, ep->bfr_tag, tag);
		return;
	}

	if (bfr->ready) {
		dev_err(ep->dcp->dev, "AFK[ep:%02x]: buffer is already initialized\n",
			ep->endpoint);
		return;
	}

	if (base >= ep->bfr_size) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: requested base 0x%x >= max size 0x%lx\n",
			ep->endpoint, base, ep->bfr_size);
		return;
	}

	end = base + size;
	if (end > ep->bfr_size) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: requested end 0x%x > max size 0x%lx\n",
			ep->endpoint, end, ep->bfr_size);
		return;
	}

	bfr->base = ep->bfr + base;
	bufsz = le32_to_cpu(*(__le32 *)bfr->base);
	if (bufsz >= size || (size - bufsz) % 3 ||
	    ((size - bufsz) / 3) % (1 << BLOCK_SHIFT)) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: ring buffer size 0x%x does not fit 0x%x\n",
			ep->endpoint, bufsz, size);
		return;
	}

	bfr->blksz = (size - bufsz) / 3;
	bfr->buf = bfr->base + 3 * bfr->blksz;
	bfr->bufsz = bufsz;
	bfr->ready = true;

	if (ep->rxbfr.ready && ep->txbfr.ready)
		afk_send(ep, FIELD_PREP(RBEP_TYPE, RBEP_START));
}

#if IS_ENABLED(CONFIG_DRM_APPLE_DEBUG)
static void afk_populate_service_debugfs(struct apple_epic_service *srv);
static void afk_remove_service_debugfs(struct apple_epic_service *srv);
#else
static void afk_populate_service_debugfs(struct apple_epic_service *srv)
{
}
static void afk_remove_service_debugfs(struct apple_epic_service *srv)
{
}
#endif

static const struct apple_epic_service_ops *
afk_match_service(struct apple_dcp_afkep *ep, const char *name)
{
	const struct apple_epic_service_ops *ops;

	if (!name[0])
		return NULL;
	if (!ep->ops)
		return NULL;

	for (ops = ep->ops; ops->name[0]; ops++) {
		if (strcmp(ops->name, name))
			continue;

		return ops;
	}

	return NULL;
}

static struct apple_epic_service *afk_epic_find_service(struct apple_dcp_afkep *ep,
						 u32 channel)
{
    for (u32 i = 0; i < ep->num_channels; i++)
        if (ep->services[i].enabled && ep->services[i].channel == channel)
            return &ep->services[i];

    return NULL;
}

static struct apple_epic_service *afk_epic_find_channel(struct apple_dcp_afkep *ep,
							u32 channel)
{
	for (u32 i = 0; i < ep->num_channels; i++)
		if (ep->services[i].channel == channel)
			return &ep->services[i];

	return NULL;
}

static int afk_recv_handle_init(struct apple_dcp_afkep *ep, u32 channel,
				u8 *payload, size_t payload_size)
{
	char name[32];
	s64 epic_unit = -1;
	u32 ch_idx;
	const char *service_name = name;
	const char *epic_name = NULL, *epic_class = NULL;
	const struct apple_epic_service_ops *ops;
	struct apple_epic_service *service = NULL;
	struct dcp_parse_ctx ctx;
	u8 *props = payload + sizeof(name);
	size_t props_size = payload_size - sizeof(name);
	int ret;

	if (ep->v2)
		service = afk_epic_find_channel(ep, channel);
	else
		WARN_ON(afk_epic_find_service(ep, channel));

	if (payload_size < sizeof(name)) {
		dev_err(ep->dcp->dev, "AFK[ep:%02x]: payload too small: %lx\n",
			ep->endpoint, payload_size);
		return -EINVAL;
	}

	if (!service && ep->num_channels >= AFK_MAX_CHANNEL) {
		dev_err(ep->dcp->dev, "AFK[ep:%02x]: too many enabled services!\n",
			ep->endpoint);
		return -ENOSPC;
	}

	strscpy(name, payload, sizeof(name));

	/*
	 * in DCP firmware 13.2 DCP reports interface-name as name which starts
	 * with "dispext%d" using -1 s ID for "dcp". In the 12.3 firmware
	 * EPICProviderClass was used. If the init call has props parse them and
	 * use EPICProviderClass to match the service.
	 */
	if (props_size > 36) {
		ret = parse(props, props_size, &ctx);
		if (ret) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: Failed to parse service init props for %s\n",
				ep->endpoint, name);
			return ret;
		}
		ret = parse_epic_service_init(&ctx, &epic_name, &epic_class, &epic_unit);
		if (ret && !ep->v2) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: failed to extract init props: %d\n",
				ep->endpoint, ret);
			return ret;
		}
		if (ret) {
			epic_name = NULL;
			epic_class = NULL;
		} else {
			service_name = epic_class;
		}
	} else {
            service_name = name;
        }

	if (ep->match_epic_name && epic_name)
		ops = afk_match_service(ep, epic_name);
	else
		ops = afk_match_service(ep, service_name);

	if (!ops) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: unable to match service %s on channel %d\n",
			ep->endpoint, service_name, channel);
		ret = -ENOENT;
		goto free;
	}

	if (!service) {
		ch_idx = ep->num_channels++;
		service = &ep->services[ch_idx];
		spin_lock_init(&service->lock);
		service->cmd_tag = 0;
	}
	service->enabled = true;
	service->torndown = false;
	service->ops = ops;
	service->ep = ep;
	service->channel = channel;
	ops->init(service, epic_name, epic_class, epic_unit);
	dev_info(ep->dcp->dev, "AFK[ep:%02x]: new service %s on channel %d\n",
		 ep->endpoint, service_name, channel);

	afk_populate_service_debugfs(service);
	ret = 0;

free:
	kfree(epic_name);
	kfree(epic_class);
	return ret;
}

static void afk_recv_handle_teardown(struct apple_dcp_afkep *ep, u32 channel)
{
	struct apple_epic_service *service;
	const struct apple_epic_service_ops *ops;
	unsigned long flags;

	service = afk_epic_find_service(ep, channel);
	if (!service) {
		dev_warn(ep->dcp->dev, "AFK[ep:%02x]: teardown for disabled channel %u\n",
			 ep->endpoint, channel);
		return;
	}

	afk_remove_service_debugfs(service);

	// TODO: think through what locking is necessary
	spin_lock_irqsave(&service->lock, flags);
	/*
	 * teardown must not disable the service since since it may be sent as
	 * side effect of a COMMAND which for which a reply is expected.
	 * Seen with DCP's "av" endpoint during the close afk_service_call.
	 */
	service->torndown = true;
	ops = service->ops;
	spin_unlock_irqrestore(&service->lock, flags);

	if (ops->teardown)
		ops->teardown(service);
}

static void afk_complete_cmd(struct apple_dcp_afkep *ep,
			     struct apple_epic_service *service, u8 idx,
			     u32 retcode, unsigned long flags)
	__releases(&service->lock)
{
	void *rxbuf, *txbuf;
	dma_addr_t rxbuf_dma, txbuf_dma;
	size_t rxlen, txlen;

	service->cmds[idx].done = true;
	service->cmds[idx].retcode = retcode;
	if (service->cmds[idx].free_on_ack) {
		/* defer freeing until we're no longer in atomic context */
		rxbuf = service->cmds[idx].rxbuf;
		txbuf = service->cmds[idx].txbuf;
		rxlen = service->cmds[idx].rxlen;
		txlen = service->cmds[idx].txlen;
		rxbuf_dma = service->cmds[idx].rxbuf_dma;
		txbuf_dma = service->cmds[idx].txbuf_dma;
		bitmap_release_region(service->cmd_map, idx, 0);
	} else {
		rxbuf = txbuf = NULL;
		rxlen = txlen = 0;
	}
	if (service->cmds[idx].completion)
		complete(service->cmds[idx].completion);

	spin_unlock_irqrestore(&service->lock, flags);

	if (rxbuf && rxlen)
		dma_free_coherent(ep->dcp->dev, rxlen, rxbuf, rxbuf_dma);
	if (txbuf && txlen)
		dma_free_coherent(ep->dcp->dev, txlen, txbuf, txbuf_dma);
}

static void afk_recv_handle_reply(struct apple_dcp_afkep *ep, u32 channel,
				  u16 tag, void *payload, size_t payload_size)
{
	struct epic_cmd *cmd = payload;
	struct apple_epic_service *service;
	unsigned long flags;
	u8 idx = tag & 0xff;

	service = afk_epic_find_service(ep, channel);
	if (!service) {
		dev_warn(ep->dcp->dev, "AFK[ep:%02x]: command reply on disabled channel %u\n",
			 ep->endpoint, channel);
		return;
	}

	if (payload_size < sizeof(*cmd)) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: command reply on channel %d too small: %ld\n",
			ep->endpoint, channel, payload_size);
		return;
	}

	if (idx >= MAX_PENDING_CMDS) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: command reply on channel %d out of range: %d\n",
			ep->endpoint, channel, idx);
		return;
	}

	spin_lock_irqsave(&service->lock, flags);
	if (service->cmds[idx].done) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: command reply on channel %d already handled\n",
			ep->endpoint, channel);
		spin_unlock_irqrestore(&service->lock, flags);
		return;
	}

	if (tag != service->cmds[idx].tag) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: command reply on channel %d has invalid tag: expected 0x%04x != 0x%04x\n",
			ep->endpoint, channel, tag, service->cmds[idx].tag);
		spin_unlock_irqrestore(&service->lock, flags);
		return;
	}

	afk_complete_cmd(ep, service, idx, le32_to_cpu(cmd->retcode), flags);
}

struct epic_std_service_ap_call {
	__le32 unk0;
	__le32 unk1;
	__le32 type;
	__le32 len;
	__le32 magic;
	u8 _unk[48];
} __attribute__((packed));

static void afk_recv_handle_std_service(struct apple_dcp_afkep *ep, u32 channel,
					u32 type, struct epic_hdr *ehdr,
					struct epic_sub_hdr *eshdr,
					void *payload, size_t payload_size)
{
	struct apple_epic_service *service = afk_epic_find_service(ep, channel);

	if (!service) {
		dev_warn(ep->dcp->dev,
			 "AFK[ep:%02x]: std service notify on disabled channel %u\n",
			 ep->endpoint, channel);
		return;
	}
	if (service->torndown) {
		dev_warn(ep->dcp->dev,
			 "AFK[ep:%02x]: std service notify on torn down service "
			 "(chan:%u)\n", ep->endpoint, channel);
		return;
	}

	if (type == EPIC_TYPE_NOTIFY && eshdr->category == EPIC_CAT_NOTIFY) {
		struct epic_std_service_ap_call *call = payload;
		size_t call_size;
		void *reply;
		int ret;

		if (payload_size < sizeof(*call))
			return;

		call_size = le32_to_cpu(call->len);
		if (payload_size < sizeof(*call) + call_size)
			return;

		if (!service->ops->call)
			return;
		reply = kzalloc(payload_size, GFP_KERNEL);
		if (!reply)
			return;

		ret = service->ops->call(service, le32_to_cpu(call->type),
					 payload + sizeof(*call), call_size,
					 reply + sizeof(*call), call_size);
		if (ret) {
			kfree(reply);
			return;
		}

		memcpy(reply, call, sizeof(*call));
		afk_send_epic(ep, channel, le16_to_cpu(eshdr->tag),
			      EPIC_TYPE_NOTIFY_ACK, EPIC_CAT_REPLY,
			      EPIC_SUBTYPE_STD_SERVICE, reply, payload_size);
		kfree(reply);

		return;
	}

	if (type == EPIC_TYPE_NOTIFY && eshdr->category == EPIC_CAT_REPORT) {
		if (service->ops->report)
			service->ops->report(service, le16_to_cpu(eshdr->type),
					     payload, payload_size);
		return;
	}

	dev_err(ep->dcp->dev,
		"AFK[ep:%02x]: channel %d received unhandled standard service message: %x / %x\n",
		ep->endpoint, channel, type, eshdr->category);
	print_hex_dump(KERN_INFO, "AFK: ", DUMP_PREFIX_NONE, 16, 1, payload,
				   payload_size, true);
}

static int afk_send_v2_msg(struct apple_dcp_afkep *ep, u16 iid,
			   enum epic_v2_kind kind, u8 type, u64 tag, u8 flags,
			   const void *payload, size_t payload_len);
static int afk_send_v2_internal(struct apple_dcp_afkep *ep, u32 code);

static int afk_v2_iocopy(struct apple_dcp_afkep *ep, u64 dva, void *buf,
			 size_t size, bool write)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(ep->dcp->dev);

	if (!domain)
		return -ENODEV;

	while (size) {
		size_t chunk = min_t(size_t, size, SZ_16K - (dva & (SZ_16K - 1)));
		phys_addr_t phys = iommu_iova_to_phys(domain, dva);
		void *p;

		if (!phys) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: failed to translate dva 0x%llx\n",
				ep->endpoint, dva);
			return -EFAULT;
		}

		p = memremap(phys, chunk, MEMREMAP_WB);
		if (!p)
			return -ENOMEM;

		if (write)
			memcpy(p, buf, chunk);
		else
			memcpy(buf, p, chunk);
		memunmap(p);

		dva += chunk;
		buf += chunk;
		size -= chunk;
	}

	return 0;
}

static void afk_recv_handle_v2_report(struct apple_dcp_afkep *ep, u16 iid,
				      const struct epic_v2_msg_hdr *mh,
				      void *payload, size_t payload_size)
{
	struct apple_epic_service *service;
	int ret;

	switch (mh->type) {
	case EPIC_V2_REPORT_PUBLISH:
		if (afk_recv_handle_init(ep, iid, payload, payload_size))
			return;

		ret = afk_send_v2_msg(ep, iid, EPIC_V2_KIND_REPORT,
				      EPIC_V2_REPORT_OPEN, 0, 1, NULL, 0);
		if (ret)
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: failed to open interface %u: %d\n",
				ep->endpoint, iid, ret);
		return;

	case EPIC_V2_REPORT_TERMINATE:
		if (!afk_epic_find_service(ep, iid)) {
			dev_dbg(ep->dcp->dev,
				"AFK[ep:%02x]: terminate without service on interface %u\n",
				ep->endpoint, iid);
			return;
		}
		afk_recv_handle_teardown(ep, iid);
		return;
	}

	service = afk_epic_find_service(ep, iid);
	if (service && !service->torndown && service->ops->report) {
		service->ops->report(service, mh->type, payload, payload_size);
		return;
	}

	dev_dbg(ep->dcp->dev, "AFK[ep:%02x]: ignoring report 0x%x on interface %u\n",
		ep->endpoint, mh->type, iid);
}

static void afk_recv_handle_v2_response(struct apple_dcp_afkep *ep, u16 iid,
					void *payload, size_t payload_size)
{
	struct epic_v2_cmd_hdr *rh = payload;
	struct apple_epic_service *service;
	struct epic_v2_oob *oob;
	unsigned long flags;
	u32 retcode, rsp_len;
	bool copy = true;
	int idx;

	service = afk_epic_find_service(ep, iid);
	if (!service) {
		dev_warn(ep->dcp->dev,
			 "AFK[ep:%02x]: command response on disabled interface %u\n",
			 ep->endpoint, iid);
		return;
	}

	if (payload_size < sizeof(*rh)) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: command response on interface %u too small: %zu\n",
			ep->endpoint, iid, payload_size);
		return;
	}
	payload += sizeof(*rh);
	payload_size -= sizeof(*rh);
	retcode = le32_to_cpu(rh->value);

	spin_lock_irqsave(&service->lock, flags);
	for_each_set_bit(idx, service->cmd_map, MAX_PENDING_CMDS)
		if (!service->cmds[idx].done && service->cmds[idx].tag == rh->seq)
			break;

	if (idx >= MAX_PENDING_CMDS) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: command response on interface %u for unknown seq %u\n",
			ep->endpoint, iid, rh->seq);
		spin_unlock_irqrestore(&service->lock, flags);
		return;
	}

	if (!retcode && (rh->flags & EPIC_V2_CMD_OOB)) {
		oob = payload;
		if (payload_size < sizeof(*oob)) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: command response on interface %u has no descriptor\n",
				ep->endpoint, iid);
			copy = false;
		} else {
			payload += sizeof(*oob);
			payload_size -= sizeof(*oob);
			rsp_len = le32_to_cpu(oob->rsp_len);
			if (rsp_len) {
				if (le64_to_cpu(oob->rsp_addr) != service->cmds[idx].rxbuf_dma ||
				    rsp_len > service->cmds[idx].rxlen)
					dev_err(ep->dcp->dev,
						"AFK[ep:%02x]: unexpected response buffer 0x%llx (0x%x)\n",
						ep->endpoint, le64_to_cpu(oob->rsp_addr),
						rsp_len);
				copy = false;
			}
		}
	}

	if (!retcode && copy && payload_size) {
		if (payload_size > service->cmds[idx].rxlen)
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: command response on interface %u too large: 0x%zx > 0x%zx\n",
				ep->endpoint, iid, payload_size,
				service->cmds[idx].rxlen);
		else
			memcpy(service->cmds[idx].rxbuf, payload, payload_size);
	}

	afk_complete_cmd(ep, service, idx, retcode, flags);
}

static void afk_recv_handle_v2_command(struct apple_dcp_afkep *ep, u16 iid,
				       const struct epic_v2_msg_hdr *mh,
				       void *payload, size_t payload_size)
{
	struct apple_epic_service *service = afk_epic_find_service(ep, iid);
	struct epic_v2_cmd_hdr *ch = payload, *rh;
	struct epic_v2_oob *oob = NULL, *roob;
	struct epic_service_call *call;
	void *data, *cmd_data = NULL, *rsp, *reply;
	size_t data_size, call_size = 0, reply_size = 0, rsp_size;
	u32 cmd_len, rsp_len;
	int ret;

	if (!service || service->torndown) {
		dev_warn(ep->dcp->dev,
			 "AFK[ep:%02x]: command 0x%x on disabled interface %u\n",
			 ep->endpoint, mh->type, iid);
		return;
	}

	if (payload_size < sizeof(*ch)) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: command on interface %u too small: %zu\n",
			ep->endpoint, iid, payload_size);
		return;
	}

	data = payload + sizeof(*ch);
	data_size = payload_size - sizeof(*ch);

	if (ch->flags & EPIC_V2_CMD_OOB) {
		if (data_size < sizeof(*oob)) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: command on interface %u has no descriptor\n",
				ep->endpoint, iid);
			return;
		}
		oob = data;
		data += sizeof(*oob);
		data_size -= sizeof(*oob);

		cmd_len = le32_to_cpu(oob->cmd_len);
		if (cmd_len) {
			if (cmd_len > EPIC_V2_MAX_LENGTH)
				return;
			cmd_data = kvzalloc(cmd_len, GFP_KERNEL);
			if (!cmd_data)
				return;
			ret = afk_v2_iocopy(ep, le64_to_cpu(oob->cmd_addr),
					    cmd_data, cmd_len, false);
			if (ret)
				goto out;
			data = cmd_data;
			data_size = cmd_len;
		}
	}

	call = data;
	if (mh->type == EPIC_SUBTYPE_STD_SERVICE && service->ops->call &&
	    data_size >= sizeof(*call) &&
	    le32_to_cpu(call->magic) == EPIC_SERVICE_CALL_MAGIC &&
	    data_size - sizeof(*call) >= le32_to_cpu(call->data_len)) {
		call_size = le32_to_cpu(call->data_len);
		reply_size = sizeof(*call) + call_size;
	} else {
		dev_warn(ep->dcp->dev,
			 "AFK[ep:%02x]: unhandled command 0x%x on interface %u\n",
			 ep->endpoint, mh->type, iid);
	}

	rsp_size = sizeof(*ch) + (oob ? sizeof(*oob) : 0);
	rsp = kzalloc(rsp_size + reply_size, GFP_KERNEL);
	if (!rsp)
		goto out;
	reply = rsp + rsp_size;

	if (reply_size) {
		ret = service->ops->call(service, le32_to_cpu(call->command),
					 data + sizeof(*call), call_size,
					 reply + sizeof(*call), call_size);
		if (ret)
			goto out_free_rsp;
		memcpy(reply, call, sizeof(*call));
	}

	rh = rsp;
	rh->flags = ch->flags;
	rh->seq = ch->seq;
	rh->value = cpu_to_le32(0);

	if (oob) {
		roob = rsp + sizeof(*rh);
		*roob = *oob;
		rsp_len = le32_to_cpu(oob->rsp_len);
		if (le64_to_cpu(oob->rsp_addr) && rsp_len) {
			reply_size = min_t(size_t, reply_size, rsp_len);
			ret = afk_v2_iocopy(ep, le64_to_cpu(oob->rsp_addr),
					    reply, reply_size, true);
			if (ret)
				goto out_free_rsp;
			roob->rsp_len = cpu_to_le32(reply_size);
			reply_size = 0;
		} else {
			roob->rsp_len = cpu_to_le32(0);
		}
	}

	ret = afk_send_v2_msg(ep, iid, EPIC_V2_KIND_RESPONSE, mh->type,
			      le64_to_cpu(mh->tag), 0, rsp, rsp_size + reply_size);
	if (ret)
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: failed to answer command 0x%x on interface %u: %d\n",
			ep->endpoint, mh->type, iid, ret);

out_free_rsp:
	kfree(rsp);
out:
	kvfree(cmd_data);
}

static void afk_recv_handle_v2_internal(struct apple_dcp_afkep *ep,
					const u8 *payload, size_t payload_size)
{
	u32 code;

	if (payload_size < sizeof(code)) {
		dev_err(ep->dcp->dev, "AFK[ep:%02x]: internal packet too small: %zu\n",
			ep->endpoint, payload_size);
		return;
	}

	code = get_unaligned_le32(payload);
	dev_dbg(ep->dcp->dev, "AFK[ep:%02x]: internal packet %u\n",
		ep->endpoint, code);

	if (code == EPIC_V2_INTERNAL_RESUME)
		afk_send_v2_internal(ep, EPIC_V2_INTERNAL_RESUME_DONE);
	else if (code == EPIC_V2_INTERNAL_PAUSE)
		afk_send_v2_internal(ep, EPIC_V2_INTERNAL_PAUSE_DONE);
}

static int afk_v2_frag(struct apple_dcp_afkep *ep, u16 iid, u32 total,
		       const u8 *data, size_t size, u8 **out)
{
	struct afk_v2_frag *frag = NULL;
	int i;

	for (i = 0; i < AFK_MAX_CHANNEL; i++) {
		if (ep->frag[i].active && ep->frag[i].iid == iid) {
			frag = &ep->frag[i];
			break;
		}
	}

	if (!frag) {
		if (total > EPIC_V2_MAX_LENGTH) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: message on interface %u too large: 0x%x\n",
				ep->endpoint, iid, total);
			return -EMSGSIZE;
		}

		for (i = 0; i < AFK_MAX_CHANNEL; i++) {
			if (!ep->frag[i].active) {
				frag = &ep->frag[i];
				break;
			}
		}
		if (!frag) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: out of fragment slots for interface %u\n",
				ep->endpoint, iid);
			return -ENOSPC;
		}

		frag->buf = kvzalloc(max(total, 1U), GFP_KERNEL);
		if (!frag->buf)
			return -ENOMEM;
		frag->active = true;
		frag->iid = iid;
		frag->len = 0;
		frag->total = total;
	}

	size = min(size, frag->total - frag->len);
	memcpy(frag->buf + frag->len, data, size);
	frag->len += size;

	if (frag->len < frag->total)
		return 0;

	*out = frag->buf;
	frag->buf = NULL;
	frag->active = false;

	return 1;
}

static void afk_recv_handle_v2(struct apple_dcp_afkep *ep, u8 *data,
			       size_t data_size)
{
	struct epic_v2_hdr *hdr = (struct epic_v2_hdr *)data;
	struct epic_v2_msg_hdr *mh;
	u8 *body, *buf = NULL;
	size_t size;
	u32 length;
	u16 iid;

	if (data_size < sizeof(*hdr)) {
		dev_err(ep->dcp->dev, "AFK[ep:%02x]: payload too small: %zx\n",
			ep->endpoint, data_size);
		return;
	}

	iid = le16_to_cpu(hdr->iid);
	length = le32_to_cpu(hdr->length);
	body = data + sizeof(*hdr);
	size = data_size - sizeof(*hdr);

	if (size != length) {
		if (afk_v2_frag(ep, iid, length, body, size, &buf) <= 0)
			return;
		body = buf;
		size = length;
	}

	if (!iid) {
		afk_recv_handle_v2_internal(ep, body, size);
		goto out;
	}

	if (size < sizeof(*mh)) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: message on interface %u too small: %zu\n",
			ep->endpoint, iid, size);
		goto out;
	}

	mh = (struct epic_v2_msg_hdr *)body;
	body += sizeof(*mh);
	size -= sizeof(*mh);

	switch (mh->kind) {
	case EPIC_V2_KIND_REPORT:
		afk_recv_handle_v2_report(ep, iid, mh, body, size);
		break;
	case EPIC_V2_KIND_COMMAND:
		afk_recv_handle_v2_command(ep, iid, mh, body, size);
		break;
	case EPIC_V2_KIND_RESPONSE:
		afk_recv_handle_v2_response(ep, iid, body, size);
		break;
	default:
		dev_warn(ep->dcp->dev,
			 "AFK[ep:%02x]: unknown message kind %u on interface %u\n",
			 ep->endpoint, mh->kind, iid);
	}

out:
	kvfree(buf);
}

static void afk_recv_handle(struct apple_dcp_afkep *ep, u32 channel, u32 type,
			    u8 *data, size_t data_size)
{
	struct apple_epic_service *service;
	struct epic_hdr *ehdr = (struct epic_hdr *)data;
	struct epic_sub_hdr *eshdr =
		(struct epic_sub_hdr *)(data + sizeof(*ehdr));
	u16 subtype = le16_to_cpu(eshdr->type);
	u8 *payload = data + sizeof(*ehdr) + sizeof(*eshdr);
	size_t payload_size;

	if (!ep->framing_known) {
		struct epic_v2_hdr *v2 = (struct epic_v2_hdr *)data;

		ep->v2 = !channel && !type &&
			 data_size >= sizeof(*v2) + sizeof(struct epic_v2_msg_hdr) &&
			 le32_to_cpu(v2->length) == data_size - sizeof(*v2);
		ep->framing_known = true;
		dev_dbg(ep->dcp->dev, "AFK[ep:%02x]: %s framing\n", ep->endpoint,
			ep->v2 ? "v2" : "v1");
	}

	if (ep->v2)
		return afk_recv_handle_v2(ep, data, data_size);

	if (data_size < sizeof(*ehdr) + sizeof(*eshdr)) {
		dev_err(ep->dcp->dev, "AFK[ep:%02x]: payload too small: %lx\n",
			ep->endpoint, data_size);
		return;
	}
	payload_size = data_size - sizeof(*ehdr) - sizeof(*eshdr);

	trace_afk_recv_handle(ep, channel, type, data_size, ehdr, eshdr);

	service = afk_epic_find_service(ep, channel);

	if (!service) {
		if (type != EPIC_TYPE_NOTIFY && type != EPIC_TYPE_REPLY) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: expected notify but got 0x%x on channel %d\n",
				ep->endpoint, type, channel);
			return;
		}
		if (eshdr->category != EPIC_CAT_REPORT) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: expected report but got 0x%x on channel %d\n",
				ep->endpoint, eshdr->category, channel);
			return;
		}
		if (subtype == EPIC_SUBTYPE_TEARDOWN) {
			dev_dbg(ep->dcp->dev,
				"AFK[ep:%02x]: teardown without service on channel %d\n",
				ep->endpoint, channel);
			return;
		}
		if (subtype != EPIC_SUBTYPE_ANNOUNCE) {
			dev_err(ep->dcp->dev,
				"AFK[ep:%02x]: expected announce but got 0x%x on channel %d\n",
				ep->endpoint, subtype, channel);
			return;
		}

		afk_recv_handle_init(ep, channel, payload, payload_size);
		return;
	}

	if (!service) {
		dev_err(ep->dcp->dev, "AFK[ep:%02x]: channel %d has no service\n",
			ep->endpoint, channel);
		return;
	}

	if (type == EPIC_TYPE_NOTIFY && eshdr->category == EPIC_CAT_REPORT &&
	    subtype == EPIC_SUBTYPE_TEARDOWN)
		return afk_recv_handle_teardown(ep, channel);

	if (type == EPIC_TYPE_REPLY && eshdr->category == EPIC_CAT_REPLY)
		return afk_recv_handle_reply(ep, channel,
					     le16_to_cpu(eshdr->tag), payload,
					     payload_size);

	if (subtype == EPIC_SUBTYPE_STD_SERVICE)
		return afk_recv_handle_std_service(
			ep, channel, type, ehdr, eshdr, payload, payload_size);

	dev_err(ep->dcp->dev, "AFK[ep:%02x]: channel %d received unhandled message "
		"(type %x subtype %x)\n", ep->endpoint, channel, type, subtype);
	print_hex_dump(KERN_INFO, "AFK: ", DUMP_PREFIX_NONE, 16, 1, payload,
				   payload_size, true);
}

static bool afk_recv(struct apple_dcp_afkep *ep)
{
	struct afk_qe *hdr;
	u32 rptr, wptr;
	u32 magic, size, channel, type;

	if (!ep->rxbfr.ready) {
		dev_err(ep->dcp->dev, "AFK[ep:%02x]: got RECV but not ready\n",
			ep->endpoint);
		return false;
	}

	rptr = le32_to_cpu(*afk_rb_rptr(&ep->rxbfr));
	wptr = le32_to_cpu(*afk_rb_wptr(&ep->rxbfr));
	trace_afk_recv_rwptr_pre(ep, rptr, wptr);

	if (rptr == wptr)
		return false;

	if (rptr > (ep->rxbfr.bufsz - sizeof(*hdr))) {
		dev_warn(ep->dcp->dev,
			 "AFK[ep:%02x]: rptr out of bounds: 0x%x > 0x%lx\n",
			 ep->endpoint, rptr, ep->rxbfr.bufsz - sizeof(*hdr));
		return false;
	}

	dma_rmb();

	hdr = ep->rxbfr.buf + rptr;
	magic = le32_to_cpu(hdr->magic);
	size = le32_to_cpu(hdr->size);
	trace_afk_recv_qe(ep, rptr, magic, size);

	if (magic != QE_MAGIC) {
		dev_warn(ep->dcp->dev, "AFK[ep:%02x]: invalid queue entry magic: 0x%x\n",
			 ep->endpoint, magic);
		return false;
	}

	/*
	 * If there's not enough space for the payload the co-processor inserted
	 * the current dummy queue entry and we have to advance to the next one
	 * which will contain the real data.
	*/
	if (rptr + size + sizeof(*hdr) > ep->rxbfr.bufsz) {
		rptr = 0;
		hdr = ep->rxbfr.buf + rptr;
		magic = le32_to_cpu(hdr->magic);
		size = le32_to_cpu(hdr->size);
		trace_afk_recv_qe(ep, rptr, magic, size);

		if (magic != QE_MAGIC) {
			dev_warn(ep->dcp->dev,
				 "AFK[ep:%02x]: invalid next queue entry magic: 0x%x\n",
				 ep->endpoint, magic);
			return false;
		}

		*afk_rb_rptr(&ep->rxbfr) = cpu_to_le32(rptr);
	}

	if (rptr + size + sizeof(*hdr) > ep->rxbfr.bufsz) {
		dev_warn(ep->dcp->dev,
			 "AFK[ep:%02x]: queue entry out of bounds: 0x%lx > 0x%lx\n",
			 ep->endpoint, rptr + size + sizeof(*hdr), ep->rxbfr.bufsz);
		return false;
	}

	channel = le32_to_cpu(hdr->channel);
	type = le32_to_cpu(hdr->type);

	rptr = roundup(rptr + sizeof(*hdr) + size, ep->rxbfr.blksz);
	if (WARN_ON(rptr > ep->rxbfr.bufsz))
		rptr = 0;
	if (rptr == ep->rxbfr.bufsz)
		rptr = 0;

	dma_mb();

	*afk_rb_rptr(&ep->rxbfr) = cpu_to_le32(rptr);
	trace_afk_recv_rwptr_post(ep, rptr, wptr);

	/*
	 * TODO: this is theoretically unsafe since DCP could overwrite data
	 *       after the read pointer was updated above. Do it anyway since
	 *       it avoids 2 problems in the DCP tracer:
	 *       1. the tracer sees replies before the notifies from dcp
	 *       2. the tracer tries to read buffers after they are unmapped.
	 */
	afk_recv_handle(ep, channel, type, hdr->data, size);

	return true;
}

static void afk_receive_message_worker(struct work_struct *work_)
{
	struct afk_receive_message_work *work;
	u16 type;

	work = container_of(work_, struct afk_receive_message_work, work);

	type = FIELD_GET(RBEP_TYPE, work->message);
	switch (type) {
	case RBEP_INIT_ACK:
		break;

	case RBEP_START_ACK:
		complete_all(&work->ep->started);
		break;

	case RBEP_SHUTDOWN_ACK:
		complete_all(&work->ep->stopped);
		break;

	case RBEP_GETBUF:
		afk_getbuf(work->ep, work->message);
		break;

	case RBEP_INIT_TX:
		afk_init_rxtx(work->ep, work->message, &work->ep->txbfr);
		break;

	case RBEP_INIT_RX:
		afk_init_rxtx(work->ep, work->message, &work->ep->rxbfr);
		break;

	case RBEP_RECV:
		while (afk_recv(work->ep))
			;
		break;

	default:
		dev_err(work->ep->dcp->dev,
			"Received unknown AFK message type: 0x%x (0x%016llx)\n",
			type, work->message);
	}

	kfree(work);
}

int afk_receive_message(struct apple_dcp_afkep *ep, u64 message)
{
	struct afk_receive_message_work *work;

	// TODO: comment why decoupling from rtkit thread is required here
	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	work->ep = ep;
	work->message = message;
	INIT_WORK(&work->work, afk_receive_message_worker);
	queue_work(ep->wq, &work->work);

	return 0;
}

static int afk_send_qe(struct apple_dcp_afkep *ep, u32 channel, u32 etype,
		       const void *epic_hdr, size_t epic_hdr_len,
		       const void *payload, size_t payload_len)
{
	u32 rptr, wptr;
	struct afk_qe *hdr, *hdr2;
	size_t total_epic_size, total_size;

	lockdep_assert_held(&ep->lock);

	dma_rmb();
	rptr = le32_to_cpu(*afk_rb_rptr(&ep->txbfr));
	wptr = le32_to_cpu(*afk_rb_wptr(&ep->txbfr));
	trace_afk_send_rwptr_pre(ep, rptr, wptr);
	total_epic_size = epic_hdr_len + payload_len;
	total_size = sizeof(*hdr) + total_epic_size;

	hdr = hdr2 = NULL;

	/*
	 * We need to figure out how to place the entire headers and payload
	 * into the ring buffer:
	 * - If the write pointer is in front of the read pointer we just need
	 *   enough space inbetween to store everything.
	 * - If the read pointer has already wrapper around the end of the
	 *   buffer we can
	 *    a) either store the entire payload at the writer pointer if
	 *       there's enough space until the end,
	 *    b) or just store the queue entry at the write pointer to indicate
	 *       that we need to wrap to the start and then store the headers
	 *       and the payload at the beginning of the buffer. The queue
	 *       header has to be store twice in this case.
	 * In either case we have to ensure that there's always enough space
	 * so that we don't accidentally overwrite other buffers.
	 */
	if (wptr < rptr) {
		/*
		 * If wptr < rptr we can't wrap around and only have to make
		 * sure that there's enough space for the entire payload.
		 */
		if (wptr + total_size > rptr)
			return -ENOMEM;

		hdr = ep->txbfr.buf + wptr;
		wptr += sizeof(*hdr);
	} else {
		/* We need enough space to place at least a queue entry */
		if (wptr + sizeof(*hdr) > ep->txbfr.bufsz)
			return -ENOMEM;

		/*
		 * If we can place a single queue entry but not the full payload
		 * we need to place one queue entry at the end of the ring
		 * buffer and then another one together with the entire
		 * payload at the beginning.
		 */
		if (wptr + total_size > ep->txbfr.bufsz) {
			/*
			 * Ensure there's space for the  queue entry at the
			 * beginning
			 */
			if (sizeof(*hdr) > rptr)
				return -ENOMEM;

			/*
			 * Place two queue entries to indicate we want to wrap
			 * around to the firmware.
			 */
			hdr = ep->txbfr.buf + wptr;
			hdr2 = ep->txbfr.buf;
			wptr = sizeof(*hdr);

			/* Ensure there's enough space for the entire payload */
			if (wptr + total_epic_size > rptr)
				return -ENOMEM;
		} else {
			/* We have enough space to place the entire payload */
			hdr = ep->txbfr.buf + wptr;
			wptr += sizeof(*hdr);
		}
	}
	/*
	 * At this point we're guaranteed that hdr (and possibly hdr2) point
	 * to a buffer large enough to fit the queue entry and that we have
	 * enough space at wptr to store the payload.
	 */

	hdr->magic = cpu_to_le32(QE_MAGIC);
	hdr->size = cpu_to_le32(total_epic_size);
	hdr->channel = cpu_to_le32(channel);
	hdr->type = cpu_to_le32(etype);
	if (hdr2)
		memcpy(hdr2, hdr, sizeof(*hdr));

	memcpy(ep->txbfr.buf + wptr, epic_hdr, epic_hdr_len);
	wptr += epic_hdr_len;

	if (payload_len)
		memcpy(ep->txbfr.buf + wptr, payload, payload_len);
	wptr += payload_len;
	wptr = roundup(wptr, ep->txbfr.blksz);
	if (wptr == ep->txbfr.bufsz)
		wptr = 0;
	trace_afk_send_rwptr_post(ep, rptr, wptr);

	*afk_rb_wptr(&ep->txbfr) = cpu_to_le32(wptr);
	afk_send(ep, FIELD_PREP(RBEP_TYPE, RBEP_SEND) |
			     FIELD_PREP(SEND_WPTR, wptr));

	return 0;
}

int afk_send_epic(struct apple_dcp_afkep *ep, u32 channel, u16 tag,
		  enum epic_type etype, enum epic_category ecat, u8 stype,
		  const void *payload, size_t payload_len)
{
	struct {
		struct epic_hdr ehdr;
		struct epic_sub_hdr eshdr;
	} __packed hdr;
	unsigned long flags;
	int ret;

	memset(&hdr, 0, sizeof(hdr));
	hdr.ehdr.version = 2;
	hdr.ehdr.timestamp = cpu_to_le64(0);

	hdr.eshdr.length = cpu_to_le32(payload_len);
	hdr.eshdr.version = 4;
	hdr.eshdr.category = ecat;
	hdr.eshdr.type = cpu_to_le16(stype);
	hdr.eshdr.timestamp = cpu_to_le64(0);
	hdr.eshdr.tag = cpu_to_le16(tag);
	if (ecat == EPIC_CAT_REPLY)
		hdr.eshdr.inline_len = cpu_to_le32(payload_len - 4);
	else
		hdr.eshdr.inline_len = cpu_to_le32(0);

	spin_lock_irqsave(&ep->lock, flags);
	hdr.ehdr.seq = cpu_to_le16(ep->qe_seq);
	ret = afk_send_qe(ep, channel, etype, &hdr, sizeof(hdr), payload,
			  payload_len);
	if (!ret)
		ep->qe_seq++;
	spin_unlock_irqrestore(&ep->lock, flags);

	return ret;
}

static int afk_send_v2(struct apple_dcp_afkep *ep, u16 iid,
		       const struct epic_v2_msg_hdr *mh,
		       const void *payload, size_t payload_len)
{
	struct {
		struct epic_v2_hdr hdr;
		struct epic_v2_msg_hdr mh;
	} __packed hdr;
	size_t hdr_len = sizeof(hdr.hdr) + (mh ? sizeof(*mh) : 0);
	size_t length = hdr_len - sizeof(hdr.hdr) + payload_len;
	unsigned long flags;
	int ret;

	if (length > EPIC_V2_SPLIT) {
		dev_err(ep->dcp->dev,
			"AFK[ep:%02x]: message on interface %u too large: 0x%zx\n",
			ep->endpoint, iid, length);
		return -EMSGSIZE;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.hdr.iid = cpu_to_le16(iid);
	hdr.hdr.length = cpu_to_le32(length);
	if (mh)
		hdr.mh = *mh;

	spin_lock_irqsave(&ep->lock, flags);
	hdr.hdr.seq = ep->qe_seq & 0xff;
	ret = afk_send_qe(ep, 0, 0, &hdr, hdr_len, payload, payload_len);
	if (!ret)
		ep->qe_seq++;
	spin_unlock_irqrestore(&ep->lock, flags);

	return ret;
}

static int afk_send_v2_msg(struct apple_dcp_afkep *ep, u16 iid,
			   enum epic_v2_kind kind, u8 type, u64 tag, u8 flags,
			   const void *payload, size_t payload_len)
{
	struct epic_v2_msg_hdr mh;

	memset(&mh, 0, sizeof(mh));
	mh.tag = cpu_to_le64(tag);
	mh.type = type;
	mh.kind = kind;
	mh.flags = flags;

	return afk_send_v2(ep, iid, &mh, payload, payload_len);
}

static int afk_send_v2_internal(struct apple_dcp_afkep *ep, u32 code)
{
	__le32 payload = cpu_to_le32(code);

	return afk_send_v2(ep, 0, NULL, &payload, sizeof(payload));
}

static int afk_send_v2_command(struct apple_epic_service *service, u8 seq,
			       u8 type, dma_addr_t txbuf_dma, size_t txlen,
			       dma_addr_t rxbuf_dma, size_t rxlen)
{
	struct {
		struct epic_v2_cmd_hdr hdr;
		struct epic_v2_oob oob;
	} __packed cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.flags = EPIC_V2_CMD_OOB;
	cmd.hdr.seq = seq;
	cmd.hdr.value = cpu_to_le32(rxlen);
	cmd.oob.cmd_addr = cpu_to_le64(txlen ? txbuf_dma : 0);
	cmd.oob.cmd_len = cpu_to_le32(txlen);
	if (service->oob_rsp) {
		cmd.oob.rsp_addr = cpu_to_le64(rxbuf_dma);
		cmd.oob.rsp_len = cpu_to_le32(rxlen);
	}

	return afk_send_v2_msg(service->ep, service->channel,
			       EPIC_V2_KIND_COMMAND, type, seq, 0, &cmd,
			       sizeof(cmd));
}

int afk_send_command(struct apple_epic_service *service, u8 type,
		     const void *payload, size_t payload_len, void *output,
		     size_t output_len, u32 *retcode)
{
	struct epic_cmd cmd;
	void *rxbuf, *txbuf;
	dma_addr_t rxbuf_dma, txbuf_dma;
	unsigned long flags;
	int ret, idx;
	u16 tag;
	struct apple_dcp_afkep *ep = service->ep;
	DECLARE_COMPLETION_ONSTACK(completion);

	rxbuf = dma_alloc_coherent(ep->dcp->dev, output_len, &rxbuf_dma,
				   GFP_KERNEL);
	if (!rxbuf)
		return -ENOMEM;
	txbuf = dma_alloc_coherent(ep->dcp->dev, payload_len, &txbuf_dma,
				   GFP_KERNEL);
	if (!txbuf) {
		ret = -ENOMEM;
		goto err_free_rxbuf;
	}

	memcpy(txbuf, payload, payload_len);

	memset(&cmd, 0, sizeof(cmd));
	cmd.retcode = cpu_to_le32(0);
	cmd.rxbuf = cpu_to_le64(rxbuf_dma);
	cmd.rxlen = cpu_to_le32(output_len);
	cmd.txbuf = cpu_to_le64(txbuf_dma);
	cmd.txlen = cpu_to_le32(payload_len);

	spin_lock_irqsave(&service->lock, flags);
	idx = bitmap_find_free_region(service->cmd_map, MAX_PENDING_CMDS, 0);
	if (idx < 0) {
		ret = -ENOSPC;
		goto err_unlock;
	}

	if (ep->v2) {
		tag = service->cmd_tag;
	} else {
		tag = (service->cmd_tag & 0xff) << 8;
		tag |= idx & 0xff;
	}
	service->cmd_tag++;

	service->cmds[idx].tag = tag;
	service->cmds[idx].rxbuf = rxbuf;
	service->cmds[idx].txbuf = txbuf;
	service->cmds[idx].rxbuf_dma = rxbuf_dma;
	service->cmds[idx].txbuf_dma = txbuf_dma;
	service->cmds[idx].rxlen = output_len;
	service->cmds[idx].txlen = payload_len;
	service->cmds[idx].free_on_ack = false;
	service->cmds[idx].done = false;
	service->cmds[idx].completion = &completion;
	init_completion(&completion);

	spin_unlock_irqrestore(&service->lock, flags);

	if (ep->v2)
		ret = afk_send_v2_command(service, tag, type, txbuf_dma,
					  payload_len, rxbuf_dma, output_len);
	else
		ret = afk_send_epic(service->ep, service->channel, tag,
				    EPIC_TYPE_COMMAND, EPIC_CAT_COMMAND, type,
				    &cmd, sizeof(cmd));
	if (ret)
		goto err_free_cmd;

	ret = wait_for_completion_timeout(&completion,
					  msecs_to_jiffies(MSEC_PER_SEC));

	if (ret <= 0) {
		spin_lock_irqsave(&service->lock, flags);
		/*
		 * Check again while we're inside the lock to make sure
		 * the command wasn't completed just after
		 * wait_for_completion_timeout returned.
		 */
		if (!service->cmds[idx].done) {
			service->cmds[idx].completion = NULL;
			service->cmds[idx].free_on_ack = true;
			spin_unlock_irqrestore(&service->lock, flags);
			return -ETIMEDOUT;
		}
		spin_unlock_irqrestore(&service->lock, flags);
	}

	ret = 0;
	if (retcode)
		*retcode = service->cmds[idx].retcode;
	if (output && output_len)
		memcpy(output, rxbuf, output_len);

err_free_cmd:
	spin_lock_irqsave(&service->lock, flags);
	bitmap_release_region(service->cmd_map, idx, 0);
err_unlock:
	spin_unlock_irqrestore(&service->lock, flags);
	dma_free_coherent(ep->dcp->dev, payload_len, txbuf, txbuf_dma);
err_free_rxbuf:
	dma_free_coherent(ep->dcp->dev, output_len, rxbuf, rxbuf_dma);
	return ret;
}

int afk_service_call(struct apple_epic_service *service, u16 group, u32 command,
		     const void *data, size_t data_len, size_t data_pad,
		     void *output, size_t output_len, size_t output_pad)
{
	struct epic_service_call *call;
	void *bfr;
	size_t bfr_len = max(data_len + data_pad, output_len + output_pad) +
			 sizeof(*call);
	int ret;
	u32 retcode;
	u32 retlen;

	bfr = kzalloc(bfr_len, GFP_KERNEL);
	if (!bfr)
		return -ENOMEM;

	call = bfr;

	memset(call, 0, sizeof(*call));
	call->group = cpu_to_le16(group);
	call->command = cpu_to_le32(command);
	call->data_len = cpu_to_le32(data_len + data_pad);
	call->magic = cpu_to_le32(EPIC_SERVICE_CALL_MAGIC);

	memcpy(bfr + sizeof(*call), data, data_len);

	ret = afk_send_command(service, EPIC_SUBTYPE_STD_SERVICE, bfr, bfr_len,
			       bfr, bfr_len, &retcode);
	if (ret)
		goto out;
	if (retcode) {
		ret = -EINVAL;
		goto out;
	}
	if (le32_to_cpu(call->magic) != EPIC_SERVICE_CALL_MAGIC ||
	    le16_to_cpu(call->group) != group ||
	    le32_to_cpu(call->command) != command) {
		ret = -EINVAL;
		goto out;
	}

	retlen = le32_to_cpu(call->data_len);
	if (output_len < retlen)
		retlen = output_len;
	if (output && output_len) {
		memset(output, 0, output_len);
		memcpy(output, bfr + sizeof(*call), retlen);
	}

out:
	kfree(bfr);
	return ret;
}

#if IS_ENABLED(CONFIG_DRM_APPLE_DEBUG)

#define AFK_DEBUGFS_MAX_REPLY 8192

static ssize_t service_call_write_file(struct file *file, const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct apple_epic_service *srv = file->private_data;
	void *buf;
	int ret;
	struct {
		u32 group;
		u32 command;
	} call_info;

	if (count < sizeof(call_info))
		return -EINVAL;
	if (!srv->debugfs.scratch) {
		srv->debugfs.scratch = \
			devm_kzalloc(srv->ep->dcp->dev, AFK_DEBUGFS_MAX_REPLY, GFP_KERNEL);
		if (!srv->debugfs.scratch)
			return -ENOMEM;
	}

	ret = copy_from_user(&call_info, user_buf, sizeof(call_info));
	if (ret == sizeof(call_info))
		return -EFAULT;
	user_buf += sizeof(call_info);
	count -= sizeof(call_info);

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	ret = copy_from_user(buf, user_buf, count);
	if (ret == count) {
		kfree(buf);
		return -EFAULT;
	}

	memset(srv->debugfs.scratch, 0, AFK_DEBUGFS_MAX_REPLY);
	dma_mb();

	ret = afk_service_call(srv, call_info.group, call_info.command, buf, count, 0,
			       srv->debugfs.scratch, AFK_DEBUGFS_MAX_REPLY, 0);
	kfree(buf);

	if (ret < 0)
		return ret;

	return count + sizeof(call_info);
}

static ssize_t service_call_read_file(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct apple_epic_service *srv = file->private_data;

	if (!srv->debugfs.scratch)
		return -EINVAL;

	return simple_read_from_buffer(user_buf, count, ppos,
				       srv->debugfs.scratch, AFK_DEBUGFS_MAX_REPLY);
}

static const struct file_operations service_call_fops = {
	.open = simple_open,
	.write = service_call_write_file,
	.read = service_call_read_file,
};

static ssize_t service_raw_call_write_file(struct file *file, const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct apple_epic_service *srv = file->private_data;
	u32 retcode;
	int ret;

	if (!srv->debugfs.scratch) {
		srv->debugfs.scratch = \
			devm_kzalloc(srv->ep->dcp->dev, AFK_DEBUGFS_MAX_REPLY, GFP_KERNEL);
		if (!srv->debugfs.scratch)
			return -ENOMEM;
	}

	memset(srv->debugfs.scratch, 0, AFK_DEBUGFS_MAX_REPLY);
	ret = copy_from_user(srv->debugfs.scratch, user_buf, count);
	if (ret == count)
		return -EFAULT;

	ret = afk_send_command(srv, EPIC_SUBTYPE_STD_SERVICE, srv->debugfs.scratch, count,
			       srv->debugfs.scratch, AFK_DEBUGFS_MAX_REPLY, &retcode);
	if (ret < 0)
		return ret;
	if (retcode)
		return -EINVAL;

	return count;
}

static ssize_t service_raw_call_read_file(struct file *file, char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct apple_epic_service *srv = file->private_data;

	if (!srv->debugfs.scratch)
		return -EINVAL;

	return simple_read_from_buffer(user_buf, count, ppos,
				       srv->debugfs.scratch, AFK_DEBUGFS_MAX_REPLY);
}

static const struct file_operations service_raw_call_fops = {
	.open = simple_open,
	.write = service_raw_call_write_file,
	.read = service_raw_call_read_file,
};

static void afk_populate_service_debugfs(struct apple_epic_service *srv)
{
	if (!srv->ep->debugfs_entry || !srv->ops)
		return;

	if (strcmp(srv->ops->name, "DCPAVAudioInterface") == 0) {
		srv->debugfs.entry = debugfs_create_dir(srv->ops->name,
							srv->ep->debugfs_entry);
		debugfs_create_file("call", 0600, srv->debugfs.entry, srv,
				&service_call_fops);
		debugfs_create_file("raw_call", 0600, srv->debugfs.entry, srv,
				&service_raw_call_fops);
	}
}

static void afk_remove_service_debugfs(struct apple_epic_service *srv)
{
	if (srv->debugfs.entry) {
		debugfs_remove_recursive(srv->debugfs.entry);
		srv->debugfs.entry = NULL;
	}
}

#endif
