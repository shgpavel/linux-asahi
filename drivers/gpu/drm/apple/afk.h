// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * AFK (Apple Firmware Kit) EPIC (EndPoint Interface Client) support
 */
/* Copyright 2022 Sven Peter <sven@svenpeter.dev> */

#ifndef _DRM_APPLE_DCP_AFK_H
#define _DRM_APPLE_DCP_AFK_H

#include <linux/completion.h>
#include <linux/kconfig.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include "dcp.h"

#define AFK_MAX_CHANNEL 16
#define MAX_PENDING_CMDS 16

struct apple_epic_service_ops;
struct apple_dcp_afkep;

struct epic_cmd_info {
	u16 tag;

	void *rxbuf;
	void *txbuf;
	dma_addr_t rxbuf_dma;
	dma_addr_t txbuf_dma;
	size_t rxlen;
	size_t txlen;

	u32 retcode;
	bool done;
	bool free_on_ack;
	struct completion *completion;
};

struct apple_epic_service {
	const struct apple_epic_service_ops *ops;
	struct apple_dcp_afkep *ep;

	struct epic_cmd_info cmds[MAX_PENDING_CMDS];
	DECLARE_BITMAP(cmd_map, MAX_PENDING_CMDS);
	u8 cmd_tag;
	spinlock_t lock;

	u32 channel;
	bool enabled;
	bool torndown;

	void *cookie;

    struct {
        struct dentry *entry;
        u8 *scratch;
    } debugfs;
};

enum epic_subtype;

struct apple_epic_service_ops {
	const char name[32];

	void (*init)(struct apple_epic_service *service, const char *name,
			      const char *class, s64 unit);
	int (*call)(struct apple_epic_service *service, u32 idx,
		    const void *data, size_t data_size, void *reply,
		    size_t reply_size);
	int (*report)(struct apple_epic_service *service, enum epic_subtype type,
		      const void *data, size_t data_size);
	void (*teardown)(struct apple_epic_service *service);
};

struct afk_qe {
#define QE_MAGIC 0x20504f49 // ' POI'
	__le32 magic;
	__le32 size;
	__le32 channel;
	__le32 type;
	u8 data[];
};

struct epic_hdr {
	u8 version;
	__le16 seq;
	u8 _pad;
	__le32 unk;
	__le64 timestamp;
} __attribute__((packed));

struct epic_sub_hdr {
	__le32 length;
	u8 version;
	u8 category;
	__le16 type;
	__le64 timestamp;
	__le16 tag;
	__le16 unk;
	__le32 inline_len;
} __attribute__((packed));

struct epic_cmd {
	__le32 retcode;
	__le64 rxbuf;
	__le64 txbuf;
	__le32 rxlen;
	__le32 txlen;
	u8 rxcookie;
	u8 txcookie;
} __attribute__((packed));

struct epic_service_call {
	u8 _pad0[2];
	__le16 group;
	__le32 command;
	__le32 data_len;
#define EPIC_SERVICE_CALL_MAGIC 0x69706378
	__le32 magic;
	u8 _pad1[48];
} __attribute__((packed));
static_assert(sizeof(struct epic_service_call) == 64);

enum epic_type {
	EPIC_TYPE_NOTIFY = 0,
	EPIC_TYPE_COMMAND = 3,
	EPIC_TYPE_REPLY = 4,
	EPIC_TYPE_NOTIFY_ACK = 8,
};

enum epic_category {
	EPIC_CAT_REPORT = 0x00,
	EPIC_CAT_NOTIFY = 0x10,
	EPIC_CAT_REPLY = 0x20,
	EPIC_CAT_COMMAND = 0x30,
};

enum epic_subtype {
	EPIC_SUBTYPE_ANNOUNCE = 0x30,
	EPIC_SUBTYPE_TEARDOWN = 0x32,
	EPIC_SUBTYPE_STD_SERVICE = 0xc0,
};

struct epic_v2_hdr {
	u8 seq;
	u8 _pad;
	__le16 iid;
	__le32 length;
} __packed;

struct epic_v2_msg_hdr {
	__le64 tag;
	u8 type;
	u8 kind;
	u8 flags;
	u8 _pad[5];
} __packed;

struct epic_v2_cmd_hdr {
#define EPIC_V2_CMD_OOB BIT(0)
	u8 flags;
	u8 seq;
	__le16 _pad;
	__le32 value;
} __packed;

struct epic_v2_oob {
	__le64 rsp_addr;
	__le64 cmd_addr;
	__le32 rsp_len;
	__le32 cmd_len;
} __packed;

#define EPIC_V2_SPLIT 0x404
#define EPIC_V2_MAX_LENGTH SZ_1M

enum epic_v2_kind {
	EPIC_V2_KIND_REPORT = 0,
	EPIC_V2_KIND_COMMAND = 1,
	EPIC_V2_KIND_RESPONSE = 2,
};

enum epic_v2_report {
	EPIC_V2_REPORT_PUBLISH = 0x11,
	EPIC_V2_REPORT_OPEN = 0x12,
	EPIC_V2_REPORT_CLOSE = 0x13,
	EPIC_V2_REPORT_TERMINATE = 0x14,
};

enum epic_v2_internal {
	EPIC_V2_INTERNAL_PAUSE = 2,
	EPIC_V2_INTERNAL_PAUSE_DONE = 3,
	EPIC_V2_INTERNAL_RESUME = 4,
	EPIC_V2_INTERNAL_RESUME_DONE = 5,
};

struct afk_v2_frag {
	bool active;
	u16 iid;
	size_t len;
	size_t total;
	u8 *buf;
};

struct afk_ringbuffer {
	bool ready;
	void *base;
	u32 blksz;
	u32 rptr;
	void *buf;
	size_t bufsz;
};

struct apple_dcp_afkep {
	struct apple_dcp *dcp;

	u32 endpoint;
	struct workqueue_struct *wq;

	struct completion started;
	struct completion stopped;

	void *bfr;
	u16 bfr_tag;
	size_t bfr_size;
	dma_addr_t bfr_dma;

	struct afk_ringbuffer txbfr;
	struct afk_ringbuffer rxbfr;

	spinlock_t lock;
	u16 qe_seq;

	const struct apple_epic_service_ops *ops;
	struct apple_epic_service services[AFK_MAX_CHANNEL];
	u32 num_channels;

	struct dentry *debugfs_entry;

	bool match_epic_name;

	bool framing_known;
	bool v2;
	struct afk_v2_frag frag[AFK_MAX_CHANNEL];
};

struct apple_dcp_afkep *afk_init(struct apple_dcp *dcp, u32 endpoint,
				 const struct apple_epic_service_ops *ops);
int afk_start(struct apple_dcp_afkep *ep);
void afk_shutdown(struct apple_dcp_afkep *ep);
int afk_receive_message(struct apple_dcp_afkep *ep, u64 message);
int afk_send_epic(struct apple_dcp_afkep *ep, u32 channel, u16 tag,
		  enum epic_type etype, enum epic_category ecat, u8 stype,
		  const void *payload, size_t payload_len);
int afk_send_command(struct apple_epic_service *service, u8 type,
		     const void *payload, size_t payload_len, void *output,
		     size_t output_len, u32 *retcode);
int afk_service_call(struct apple_epic_service *service, u16 group, u32 command,
		     const void *data, size_t data_len, size_t data_pad,
		     void *output, size_t output_len, size_t output_pad);
#endif
