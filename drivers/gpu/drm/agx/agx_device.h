/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __AGX_DEVICE_H__
#define __AGX_DEVICE_H__

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/ioport.h>
#include <linux/types.h>

#include "agx_board.h"

#define AGX_ASC_CPU_CONTROL	0x44
#define AGX_ASC_CPU_RUN		BIT(4)
#define AGX_ASC_CPU_STATUS	0x48

struct agx_fw;
struct apple_rtkit;
struct device;

enum agx_region {
	AGX_REGION_TTBS,
	AGX_REGION_PAGETABLES,
	AGX_REGION_L2,
	AGX_REGION_HANDOFF,
	AGX_REGION_FW_TEXT,
	AGX_REGION_FW_DATA,
	AGX_REGION_FW_SHARED_RO,
	AGX_NR_REGIONS,
};

struct agx_device {
	struct device *dev;
	const struct agx_fw *fw;
	struct agx_board board;
	struct resource region[AGX_NR_REGIONS];
	void __iomem *asc;
	void __iomem *sgx;
	u32 asc_control;
	struct apple_rtkit *rtk;
	void *handoff;
	atomic_t messages;
	bool crashed;
	bool wedged;
};

extern const char * const agx_region_names[AGX_NR_REGIONS];

int agx_boot(struct agx_device *adev);

#endif
