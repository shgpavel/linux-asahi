// SPDX-License-Identifier: GPL-2.0-only

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#include "agx_board.h"
#include "agx_fw.h"

#define AGX_ASC_CPU_CONTROL	0x44
#define AGX_ASC_CPU_RUN		BIT(4)
#define AGX_ASC_CPU_STATUS	0x48
#define AGX_ASC_STATUS_MASK	GENMASK(1, 0)
#define AGX_ASC_STATUS_COLD	2
#define AGX_SGX_ID		0xd04000
#define AGX_REGION_ALIGN	SZ_16K

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

static const char * const agx_region_names[AGX_NR_REGIONS] = {
	[AGX_REGION_TTBS] = "ttbs",
	[AGX_REGION_PAGETABLES] = "pagetables",
	[AGX_REGION_L2] = "l2",
	[AGX_REGION_HANDOFF] = "handoff",
	[AGX_REGION_FW_TEXT] = "fw-text",
	[AGX_REGION_FW_DATA] = "fw-data",
	[AGX_REGION_FW_SHARED_RO] = "fw-shared-ro",
};

struct agx_device {
	struct device *dev;
	const struct agx_fw *fw;
	struct agx_board board;
	struct resource region[AGX_NR_REGIONS];
	void __iomem *asc;
	void __iomem *sgx;
};

static const void *agx_of_prop(const void *ctx, const char *name, int *len)
{
	return of_get_property(ctx, name, len);
}

static int agx_get_regions(struct agx_device *adev)
{
	struct device *dev = adev->dev;
	int i, j, ret;

	for (i = 0; i < AGX_NR_REGIONS; i++) {
		struct resource *r = &adev->region[i];

		ret = of_reserved_mem_region_to_resource_byname(dev->of_node,
								agx_region_names[i], r);
		if (ret)
			return dev_err_probe(dev, ret, "no %s region\n", agx_region_names[i]);
		if (!r->start || !IS_ALIGNED(r->start | resource_size(r), AGX_REGION_ALIGN))
			return dev_err_probe(dev, -EINVAL, "bad %s region %pR\n",
					     agx_region_names[i], r);
		for (j = 0; j < i; j++)
			if (resource_overlaps(r, &adev->region[j]))
				return dev_err_probe(dev, -EINVAL, "%s overlaps %s\n",
						     agx_region_names[i], agx_region_names[j]);
		dev_dbg(dev, "%s %pR\n", agx_region_names[i], r);
	}
	return 0;
}

static int agx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct agx_device *adev;
	struct resource *asc, *sgx;
	const char *reason;
	u32 control, status, id;
	int ret;

	adev = devm_kzalloc(dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	adev->dev = dev;
	platform_set_drvdata(pdev, adev);

	asc = platform_get_resource_byname(pdev, IORESOURCE_MEM, "asc");
	sgx = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sgx");
	if (!asc || !sgx)
		return dev_err_probe(dev, -EINVAL, "missing registers\n");

	ret = agx_board_parse(&adev->board, agx_of_prop, dev->of_node, sgx->start,
			      resource_size(sgx), &reason);
	if (ret)
		return dev_err_probe(dev, ret, "bad loader data: %s\n", reason);

	adev->fw = agx_fw_find(adev->board.fw_uuid);
	if (!adev->fw)
		return dev_err_probe(dev, -ENODEV, "unknown GPU firmware %s (%s)\n",
				     adev->board.fw_uuid, adev->board.fw_build);
	if (adev->board.private_va != adev->fw->private_va ||
	    adev->board.private_size != adev->fw->private_size)
		return dev_err_probe(dev, -EINVAL, "unexpected private VM %#llx+%#llx\n",
				     adev->board.private_va, adev->board.private_size);

	ret = agx_get_regions(adev);
	if (ret)
		return ret;

	adev->asc = devm_ioremap(dev, asc->start, resource_size(asc));
	adev->sgx = devm_ioremap(dev, sgx->start, resource_size(sgx));
	if (!adev->asc || !adev->sgx)
		return -ENOMEM;

	control = readl(adev->asc + AGX_ASC_CPU_CONTROL);
	status = readl(adev->asc + AGX_ASC_CPU_STATUS);
	if (control & AGX_ASC_CPU_RUN ||
	    (status & AGX_ASC_STATUS_MASK) != AGX_ASC_STATUS_COLD)
		return dev_err_probe(dev, -EBUSY, "ASC is not cold: control %#x status %#x\n",
				     control, status);

	id = readl(adev->sgx + AGX_SGX_ID);
	if (!id || id == U32_MAX)
		return dev_err_probe(dev, -ENODEV, "invalid GPU ID %#x\n", id);

	dev_info(dev, "GPU %#x, firmware %s, %u states up to %u MHz; not started\n",
		 id, adev->fw->name, adev->board.nr_states,
		 adev->board.freq_high[agx_board_top_state(&adev->board)]);
	return 0;
}

static const struct of_device_id agx_of_match[] = {
	{ .compatible = "apple,agx-t8132" },
	{}
};
MODULE_DEVICE_TABLE(of, agx_of_match);

static struct platform_driver agx_driver = {
	.probe = agx_probe,
	.driver = {
		.name = "apple-agx",
		.of_match_table = agx_of_match,
	},
};
module_platform_driver(agx_driver);

MODULE_DESCRIPTION("Apple AGX G16 GPU driver");
MODULE_LICENSE("GPL");
