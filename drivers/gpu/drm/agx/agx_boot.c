// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/soc/apple/rtkit.h>

#include "agx_device.h"
#include "agx_fw.h"

#define AGX_SGX_BOOT_ENABLE0	0x1000104
#define AGX_SGX_BOOT_ENABLE1	0x1000108
#define AGX_SGX_BOOT_MODE	0xd06030
#define AGX_SGX_BOOT_MODE_VALUE	6
#define AGX_EP_FIRMWARE		0x20
#define AGX_EP_DOORBELL		0x21
#define AGX_NR_EPS		256
#define AGX_SHOWN_WORDS		8
#define AGX_SETTLE_MS		500

static void agx_rtkit_crashed(void *cookie, const void *log, size_t size)
{
	struct agx_device *adev = cookie;

	WRITE_ONCE(adev->crashed, true);
	dev_err(adev->dev, "GPU firmware crashed, %zu-byte crash log\n", size);
}

static void agx_rtkit_message(void *cookie, u8 ep, u64 msg)
{
	struct agx_device *adev = cookie;

	atomic_inc(&adev->messages);
	dev_info_ratelimited(adev->dev, "endpoint %#x sent %#018llx\n", ep, msg);
}

static bool agx_in_region(struct agx_device *adev, enum agx_region region,
			  dma_addr_t iova, size_t size)
{
	struct resource *r = &adev->region[region];
	u64 start = r->start, len = resource_size(r);

	return size && iova >= start && size <= len && iova - start <= len - size;
}

static int agx_rtkit_shmem_setup(void *cookie, struct apple_rtkit_shmem *bfr)
{
	struct agx_device *adev = cookie;

	if (!bfr->iova ||
	    (!agx_in_region(adev, AGX_REGION_FW_DATA, bfr->iova, bfr->size) &&
	     !agx_in_region(adev, AGX_REGION_FW_SHARED_RO, bfr->iova, bfr->size))) {
		dev_err(adev->dev, "refused firmware buffer %pad+%#zx outside the firmware's data\n",
			&bfr->iova, bfr->size);
		return -EINVAL;
	}
	bfr->buffer = memremap(bfr->iova, bfr->size, MEMREMAP_WB);
	if (!bfr->buffer)
		return -ENOMEM;
	bfr->is_mapped = true;
	dev_info(adev->dev, "firmware buffer %pad+%#zx\n", &bfr->iova, bfr->size);
	return 0;
}

static void agx_rtkit_shmem_destroy(void *cookie, struct apple_rtkit_shmem *bfr)
{
	if (bfr->buffer)
		memunmap(bfr->buffer);
}

static const struct apple_rtkit_ops agx_rtkit_ops = {
	.quirks = APPLE_RTKIT_QUIRK_EARLY_CRASHLOG,
	.crashed = agx_rtkit_crashed,
	.recv_message = agx_rtkit_message,
	.shmem_setup = agx_rtkit_shmem_setup,
	.shmem_destroy = agx_rtkit_shmem_destroy,
};

static void agx_log_endpoints(struct agx_device *adev)
{
	char list[3 * AGX_NR_EPS + 1] = "";
	int ep, len = 0;

	for (ep = 0; ep < AGX_NR_EPS; ep++)
		if (apple_rtkit_has_endpoint(adev->rtk, ep))
			len += scnprintf(list + len, sizeof(list) - len, " %02x", ep);
	dev_info(adev->dev, "endpoints:%s\n", list);
}

static void agx_observe_region(struct agx_device *adev, enum agx_region region)
{
	struct resource *r = &adev->region[region];
	size_t words = resource_size(r) / sizeof(u64), nonzero = 0, n;
	const u64 *map;

	if (region == AGX_REGION_HANDOFF)
		map = adev->handoff;
	else
		map = memremap(r->start, resource_size(r), MEMREMAP_WC);
	if (!map) {
		dev_warn(adev->dev, "cannot map %s\n", agx_region_names[region]);
		return;
	}
	for (n = 0; n < words; n++) {
		u64 v = READ_ONCE(map[n]);

		if (!v)
			continue;
		if (nonzero++ < AGX_SHOWN_WORDS)
			dev_info(adev->dev, "%s +%#zx: %#018llx\n",
				 agx_region_names[region], n * sizeof(u64), v);
	}
	dev_info(adev->dev, "%s: %zu of %zu words nonzero\n",
		 agx_region_names[region], nonzero, words);
	if (map != adev->handoff)
		memunmap((void *)map);
}

static void agx_observe(struct agx_device *adev)
{
	u64 receipt;
	bool ready;

	ready = adev->fw->ops->handoff_ready(adev->handoff, &receipt);
	dev_info(adev->dev, "ASC control %#x status %#x; handoff +8 %#llx%s\n",
		 readl(adev->asc + AGX_ASC_CPU_CONTROL), readl(adev->asc + AGX_ASC_CPU_STATUS),
		 receipt, ready ? ", the firmware's receipt" : "");
	agx_observe_region(adev, AGX_REGION_HANDOFF);
	agx_observe_region(adev, AGX_REGION_TTBS);
	agx_observe_region(adev, AGX_REGION_L2);
	agx_observe_region(adev, AGX_REGION_PAGETABLES);
}

static int agx_wedge(struct agx_device *adev, const char *step, int err)
{
	adev->wedged = true;
	dev_err(adev->dev, "stopped at %s: %pe%s; the GPU stays down until reboot\n",
		step, ERR_PTR(err), READ_ONCE(adev->crashed) ? ", firmware crashed" : "");
	agx_observe(adev);
	return 0;
}

int agx_boot(struct agx_device *adev)
{
	struct device *dev = adev->dev;
	struct resource *h = &adev->region[AGX_REGION_HANDOFF];
	ktime_t start;
	int ret;

	if (resource_size(h) < adev->fw->sizes->handoff)
		return dev_err_probe(dev, -EINVAL, "handoff region %pR too small\n", h);

	adev->rtk = apple_rtkit_init(dev, adev, NULL, 0, &agx_rtkit_ops);
	if (IS_ERR(adev->rtk))
		return dev_err_probe(dev, PTR_ERR(adev->rtk), "no RTKit\n");

	adev->handoff = memremap(h->start, resource_size(h), MEMREMAP_WC);
	if (!adev->handoff) {
		apple_rtkit_free(adev->rtk);
		return -ENOMEM;
	}
	adev->fw->ops->handoff(adev->handoff);
	wmb();

	writel(readl(adev->sgx + AGX_SGX_BOOT_ENABLE0) | BIT(0), adev->sgx + AGX_SGX_BOOT_ENABLE0);
	writel(readl(adev->sgx + AGX_SGX_BOOT_ENABLE1) | BIT(0), adev->sgx + AGX_SGX_BOOT_ENABLE1);
	writel(AGX_SGX_BOOT_MODE_VALUE, adev->sgx + AGX_SGX_BOOT_MODE);

	__module_get(THIS_MODULE);
	start = ktime_get();
	writel(adev->asc_control | AGX_ASC_CPU_RUN, adev->asc + AGX_ASC_CPU_CONTROL);

	ret = apple_rtkit_boot(adev->rtk);
	if (ret)
		return agx_wedge(adev, "RTKit boot", ret);
	ret = apple_rtkit_start_ep(adev->rtk, AGX_EP_FIRMWARE);
	if (ret)
		return agx_wedge(adev, "endpoint 0x20", ret);
	ret = apple_rtkit_start_ep(adev->rtk, AGX_EP_DOORBELL);
	if (ret)
		return agx_wedge(adev, "endpoint 0x21", ret);

	dev_info(dev, "transport up in %lld ms\n", ktime_ms_delta(ktime_get(), start));
	agx_log_endpoints(adev);
	msleep(AGX_SETTLE_MS);
	agx_observe(adev);
	dev_info(dev, "milestone transport reached, %d firmware messages; stopping here\n",
		 atomic_read(&adev->messages));
	return 0;
}
