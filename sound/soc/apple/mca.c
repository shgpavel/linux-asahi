// SPDX-License-Identifier: GPL-2.0-only
//
// Apple SoCs MCA driver
//
// Copyright (C) The Asahi Linux Contributors
//
// The MCA peripheral is made up of a number of identical units called clusters.
// Each cluster has its separate clock parent, SYNC signal generator, carries
// four SERDES units and has a dedicated I2S port on the SoC's periphery.
//
// The clusters can operate independently, or can be combined together in a
// configurable manner. We mostly treat them as self-contained independent
// units and don't configure any cross-cluster connections except for the I2S
// ports. The I2S ports can be routed to any of the clusters (irrespective
// of their native cluster). We map this onto ASoC's (DPCM) notion of backend
// and frontend DAIs. The 'cluster guts' are frontends which are dynamically
// routed to backend I2S ports.
//
// DAI references in devicetree are resolved to backends. The routing between
// frontends and backends is determined by the machine driver in the DAPM paths
// it supplies.

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#define USE_RXB_FOR_CAPTURE

/* Relative to cluster base */
#define REG_STATUS		0x0
#define STATUS_MCLK_EN		BIT(0)
#define REG_MCLK_CONF		0x4
#define MCLK_CONF_DIV		GENMASK(11, 8)

#define CLUSTER_SYNCGEN_OFF	0x100
#define REG_SYNCGEN_STATUS	0x0
#define SYNCGEN_STATUS_EN	BIT(0)
#define REG_SYNCGEN_MCLK_SEL	0x4
#define SYNCGEN_MCLK_SEL	GENMASK(3, 0)
#define REG_SYNCGEN_HI_PERIOD	0x8
#define REG_SYNCGEN_LO_PERIOD	0xc

#define CLUSTER_PORT_OFF	0x600
#define REG_PORT_ENABLES	0x0
#define PORT_ENABLES_CLOCKS	GENMASK(2, 1)
#define PORT_ENABLES_TX_DATA	BIT(3)
#define REG_PORT_CLOCK_SEL	0x4
#define PORT_CLOCK_SEL		GENMASK(11, 8)
#define REG_PORT_DATA_SEL	0x8
#define PORT_DATA_SEL_TXA(cl)	(1 << ((cl)*2))
#define PORT_DATA_SEL_TXB(cl)	(2 << ((cl)*2))

#define REG_INTSTATE		0x700
#define REG_INTMASK		0x704

/* Bases of serdes units (relative to cluster) */
#define CLUSTER_RXA_OFF	0x200
#define CLUSTER_TXA_OFF	0x300
#define CLUSTER_RXB_OFF	0x400
#define CLUSTER_TXB_OFF	0x500

#define CLUSTER_TX_OFF	CLUSTER_TXA_OFF

#ifndef USE_RXB_FOR_CAPTURE
#define CLUSTER_RX_OFF	CLUSTER_RXA_OFF
#else
#define CLUSTER_RX_OFF	CLUSTER_RXB_OFF
#endif

/* Relative to serdes unit base */
#define REG_SERDES_STATUS	0x00
#define SERDES_STATUS_EN	BIT(0)
#define SERDES_STATUS_RST	BIT(1)
#define REG_TX_SERDES_CONF	0x04
#define REG_RX_SERDES_CONF	0x08
#define SERDES_CONF_NCHANS	GENMASK(3, 0)
#define SERDES_CONF_WIDTH_MASK	GENMASK(8, 4)
#define SERDES_CONF_WIDTH_16BIT 0x40
#define SERDES_CONF_WIDTH_20BIT 0x80
#define SERDES_CONF_WIDTH_24BIT 0xc0
#define SERDES_CONF_WIDTH_32BIT 0x100
#define SERDES_CONF_BCLK_POL	0x400
#define SERDES_CONF_LSB_FIRST	0x800
#define SERDES_CONF_UNK1	BIT(12)
#define SERDES_CONF_UNK2	BIT(13)
#define SERDES_CONF_UNK3	BIT(14)
#define SERDES_CONF_NO_DATA_FEEDBACK	BIT(15)
#define SERDES_CONF_SYNC_SEL	GENMASK(18, 16)
#define REG_TX_SERDES_BITSTART	0x08
#define REG_RX_SERDES_BITSTART	0x0c
#define REG_TX_SERDES_SLOTMASK	0x0c
#define REG_RX_SERDES_SLOTMASK	0x10
#define REG_RX_SERDES_PORT	0x04

/* Relative to switch base */
#define REG_DMA_ADAPTER_A(cl)	(0x8000 * (cl))
#define REG_DMA_ADAPTER_B(cl)	(0x8000 * (cl) + 0x4000)
#define DMA_ADAPTER_TX_LSB_PAD	GENMASK(4, 0)
#define DMA_ADAPTER_TX_NCHANS	GENMASK(6, 5)
#define DMA_ADAPTER_RX_MSB_PAD	GENMASK(12, 8)
#define DMA_ADAPTER_RX_NCHANS	GENMASK(14, 13)
#define DMA_ADAPTER_NCHANS	GENMASK(22, 20)

#define SWITCH_STRIDE	0x8000
#define CLUSTER_STRIDE	0x4000

#define T8132_NCLUSTERS		6
#define T8132_UNIT_STRIDE	0x100
#define T8132_CLK_OFF		0x2000
#define T8132_RX_SERDES_OFF	0x4000
#define T8132_TX_SERDES_OFF	0x6000
#define T8132_ADAPTER_OFF	0x8000
#define T8132_SYNCGEN_OFF	0x20
#define T8132_PORT_OFF		0x60
#define T8132_MCLK_CONF_DIV	GENMASK(12, 8)

#define T8132_REG_ADAPTER_EN	0x4
#define T8132_ADAPTER_TX_EN	BIT(0)
#define T8132_ADAPTER_RX_EN	BIT(4)
#define T8132_ADAPTER_THRESHOLD	GENMASK(22, 16)

#define T8132_REG_GRST_SYNC(g)	(0x100 + 4 * (g))
#define T8132_REG_GRST_START	0x140
#define T8132_REG_GRST_STOP	0x144
#define T8132_REG_GRST_RX_START	0x150
#define T8132_REG_GRST_RX_STOP	0x154
#define T8132_REG_GRST_TX_START	0x160
#define T8132_REG_GRST_TX_STOP	0x164
#define T8132_REG_GRST_COMMIT	0x170

#define T8132_MCLK_MUX_SEL	GENMASK(27, 24)
#define T8132_MCLK_MUX_BUSY	BIT(30)

#define MAX_NCLUSTERS	6

#define APPLE_MCA_FMTBITS (SNDRV_PCM_FMTBIT_S16_LE | \
			   SNDRV_PCM_FMTBIT_S24_LE | \
			   SNDRV_PCM_FMTBIT_S32_LE)

struct mca_serdes_regs {
	int conf;
	int bitstart;
	int slotmask;
	int port;
};

static const struct mca_serdes_regs mca_serdes_tx = {
	.conf = REG_TX_SERDES_CONF,
	.bitstart = REG_TX_SERDES_BITSTART,
	.slotmask = REG_TX_SERDES_SLOTMASK,
	.port = -1,
};

static const struct mca_serdes_regs mca_serdes_rx = {
	.conf = REG_RX_SERDES_CONF,
	.bitstart = REG_RX_SERDES_BITSTART,
	.slotmask = REG_RX_SERDES_SLOTMASK,
	.port = REG_RX_SERDES_PORT,
};

static const struct mca_serdes_regs mca_t8132_serdes_rx = {
	.conf = REG_TX_SERDES_CONF,
	.bitstart = REG_TX_SERDES_BITSTART,
	.slotmask = REG_TX_SERDES_SLOTMASK,
	.port = 0x14,
};

struct mca_cluster {
	int no;
	__iomem void *base;
	__iomem void *syncgen;
	__iomem void *port;
	__iomem void *serdes[SNDRV_PCM_STREAM_LAST + 1];
	__iomem void *adapter[SNDRV_PCM_STREAM_LAST + 1];
	int running;
	struct mca_data *host;
	struct device *pd_dev;
	struct clk *clk_parent;
	struct dma_chan *dma_chans[SNDRV_PCM_STREAM_LAST + 1];

	bool clk_provider;

	bool port_clk_started[SNDRV_PCM_STREAM_LAST + 1];
	int port_clk_driver; /* The cluster driving this cluster's port */

	bool clocks_in_use[SNDRV_PCM_STREAM_LAST + 1];
	struct device_link *pd_link;

	/* In case of clock consumer FE */
	int syncgen_in_use;

	unsigned int bclk_ratio;

	/* Masks etc. picked up via the set_tdm_slot method */
	int tdm_slots;
	int tdm_slot_width;
	unsigned int tdm_tx_mask;
	unsigned int tdm_rx_mask;
};

struct mca_data {
	struct device *dev;

	__iomem void *switch_base;

	bool t8132;
	__iomem void *grst_base;
	__iomem void *mclk_base;
	const struct mca_serdes_regs *serdes_regs[SNDRV_PCM_STREAM_LAST + 1];

	struct device *pd_dev;
	struct reset_control *rstc;
	struct device_link *pd_link;

	/* Mutex for accessing port_clk_driver of foreign clusters */
	struct mutex port_mutex;

	int nclusters;
	struct mca_cluster clusters[] __counted_by(nclusters);
};

static void mca_modify_reg(__iomem void *ptr, u32 mask, u32 val)
{
	u32 newval;

	newval = (val & mask) | (readl_relaxed(ptr) & ~mask);
	writel_relaxed(newval, ptr);
}

static void mca_modify(struct mca_cluster *cl, int regoffset, u32 mask, u32 val)
{
	mca_modify_reg(cl->base + regoffset, mask, val);
}

static void mca_t8132_group_trigger(struct mca_cluster *cl, int stream,
				    bool start)
{
	__iomem void *grst = cl->host->grst_base;
	u32 mask = BIT(cl->no);
	bool tx = stream == SNDRV_PCM_STREAM_PLAYBACK;
	int others = cl->running & ~BIT(stream);

	writel_relaxed(cl->no + 1, grst + T8132_REG_GRST_SYNC(cl->no));
	if (start) {
		writel_relaxed(mask, grst + T8132_REG_GRST_START);
		writel_relaxed(tx ? 0 : mask, grst + T8132_REG_GRST_RX_START);
		writel_relaxed(tx ? mask : 0, grst + T8132_REG_GRST_TX_START);
		cl->running |= BIT(stream);
	} else {
		writel_relaxed(others ? 0 : mask, grst + T8132_REG_GRST_STOP);
		writel_relaxed(tx ? 0 : mask, grst + T8132_REG_GRST_RX_STOP);
		writel_relaxed(tx ? mask : 0, grst + T8132_REG_GRST_TX_STOP);
		cl->running &= ~BIT(stream);
	}
	writel_relaxed(1, grst + T8132_REG_GRST_COMMIT);
}

/*
 * Get the cluster of FE or BE DAI
 */
static struct mca_cluster *mca_dai_to_cluster(struct snd_soc_dai *dai)
{
	struct mca_data *mca = snd_soc_dai_get_drvdata(dai);
	/*
	 * FE DAIs are         0 ... nclusters - 1
	 * BE DAIs are nclusters ... 2*nclusters - 1
	 */
	int cluster_no = dai->id % mca->nclusters;

	return &mca->clusters[cluster_no];
}

/* called before PCM trigger */
static void mca_fe_early_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	struct mca_cluster *cl = mca_dai_to_cluster(dai);
	__iomem void *serdes = cl->serdes[substream->stream];
	__iomem void *serdes_conf =
		serdes + cl->host->serdes_regs[substream->stream]->conf;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mca_modify_reg(serdes_conf, SERDES_CONF_SYNC_SEL,
			       FIELD_PREP(SERDES_CONF_SYNC_SEL, 0));
		mca_modify_reg(serdes_conf, SERDES_CONF_SYNC_SEL,
			       FIELD_PREP(SERDES_CONF_SYNC_SEL, 7));
		mca_modify_reg(serdes + REG_SERDES_STATUS,
			       SERDES_STATUS_EN | SERDES_STATUS_RST,
			       SERDES_STATUS_RST);
		/*
		 * Experiments suggest that it takes at most ~1 us
		 * for the bit to clear, so wait 5 us for good measure.
		 */
		udelay(50);
		WARN_ON(readl_relaxed(serdes + REG_SERDES_STATUS) &
			SERDES_STATUS_RST);
		mca_modify_reg(serdes_conf, SERDES_CONF_SYNC_SEL,
			       FIELD_PREP(SERDES_CONF_SYNC_SEL, 0));
		mca_modify_reg(serdes_conf, SERDES_CONF_SYNC_SEL,
			       FIELD_PREP(SERDES_CONF_SYNC_SEL, cl->no + 1));
		/*
		 * ADMAC gets started right after this. This delay seems
		 * to be needed for that to be reliable, e.g. ensure the
		 * clock is stable?
		 */
		udelay(100);
		break;
	default:
		break;
	}
}

static int mca_fe_trigger(struct snd_pcm_substream *substream, int cmd,
			  struct snd_soc_dai *dai)
{
	struct mca_cluster *cl = mca_dai_to_cluster(dai);
	__iomem void *serdes = cl->serdes[substream->stream];

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (cl->host->t8132) {
			mca_t8132_group_trigger(cl, substream->stream, true);
			break;
		}
		mca_modify_reg(serdes + REG_SERDES_STATUS,
			       SERDES_STATUS_EN | SERDES_STATUS_RST,
			       SERDES_STATUS_EN);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (cl->host->t8132) {
			mca_t8132_group_trigger(cl, substream->stream, false);
			break;
		}
		mca_modify_reg(serdes + REG_SERDES_STATUS,
			       SERDES_STATUS_EN, 0);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int mca_fe_get_portmask(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dpcm *dpcm;
	int mask = 0;

	for_each_dpcm_be(fe, substream->stream, dpcm) {
		int no = mca_dai_to_cluster(snd_soc_rtd_to_cpu(dpcm->be, 0))->no;
		mask |= 1 << no;
	}

	return mask;
}

static int mca_fe_enable_clocks(struct mca_cluster *cl)
{
	struct mca_data *mca = cl->host;
	int ret;

	if (!cl->clk_provider)
		return -EINVAL;

	ret = clk_prepare_enable(cl->clk_parent);
	if (ret) {
		dev_err(mca->dev,
			"cluster %d: unable to enable clock parent: %d\n",
			cl->no, ret);
		return ret;
	}

	/*
	 * We can't power up the device earlier than this because
	 * the power state driver would error out on seeing the device
	 * as clock-gated.
	 */
	WARN_ON(cl->pd_link);
	cl->pd_link = device_link_add(mca->dev, cl->pd_dev,
				      DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME |
					      DL_FLAG_RPM_ACTIVE);
	if (!cl->pd_link) {
		dev_err(mca->dev,
			"cluster %d: unable to prop-up power domain\n", cl->no);
		clk_disable_unprepare(cl->clk_parent);
		return -EINVAL;
	}

	writel_relaxed(cl->no + 1, cl->syncgen + REG_SYNCGEN_MCLK_SEL);
	mca_modify_reg(cl->syncgen + REG_SYNCGEN_STATUS, SYNCGEN_STATUS_EN,
		       SYNCGEN_STATUS_EN);
	mca_modify(cl, REG_STATUS, STATUS_MCLK_EN, STATUS_MCLK_EN);

	return 0;
}

static void mca_fe_disable_clocks(struct mca_cluster *cl)
{
	mca_modify_reg(cl->syncgen + REG_SYNCGEN_STATUS, SYNCGEN_STATUS_EN, 0);
	mca_modify(cl, REG_STATUS, STATUS_MCLK_EN, 0);

	if (cl->pd_link) {
		device_link_del(cl->pd_link);
		cl->pd_link = NULL;
	}

	clk_disable_unprepare(cl->clk_parent);
}

static bool mca_fe_clocks_in_use(struct mca_cluster *cl)
{
	struct mca_data *mca = cl->host;
	struct mca_cluster *be_cl;
	int stream, i;

	guard(mutex)(&mca->port_mutex);
	for (i = 0; i < mca->nclusters; i++) {
		be_cl = &mca->clusters[i];

		if (be_cl->port_clk_driver != cl->no)
			continue;

		for_each_pcm_streams(stream) {
			if (be_cl->clocks_in_use[stream]) {
				return true;
			}
		}
	}
	return false;
}

static int mca_fe_prepare(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct mca_cluster *cl = mca_dai_to_cluster(dai);
	struct mca_data *mca = cl->host;

	if (cl->clk_provider)
		return 0;

	if (!cl->syncgen_in_use) {
		int port = ffs(mca_fe_get_portmask(substream)) - 1;

		WARN_ON(cl->pd_link);
		cl->pd_link = device_link_add(mca->dev, cl->pd_dev,
					      DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME |
						DL_FLAG_RPM_ACTIVE);
		if (!cl->pd_link) {
			dev_err(mca->dev,
				"cluster %d: unable to prop-up power domain\n", cl->no);
			return -EINVAL;
		}

		writel_relaxed(port + 6 + 1,
			       cl->syncgen + REG_SYNCGEN_MCLK_SEL);
		mca_modify_reg(cl->syncgen + REG_SYNCGEN_STATUS,
			       SYNCGEN_STATUS_EN, SYNCGEN_STATUS_EN);
	}
	cl->syncgen_in_use |= 1 << substream->stream;

	return 0;
}

static int mca_fe_hw_free(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct mca_cluster *cl = mca_dai_to_cluster(dai);

	if (cl->host->t8132)
		mca_modify_reg(cl->adapter[substream->stream] + T8132_REG_ADAPTER_EN,
			       substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			       T8132_ADAPTER_TX_EN : T8132_ADAPTER_RX_EN, 0);

	if (cl->clk_provider)
		return 0;

	cl->syncgen_in_use &= ~(1 << substream->stream);
	if (cl->syncgen_in_use)
		return 0;

	mca_modify_reg(cl->syncgen + REG_SYNCGEN_STATUS, SYNCGEN_STATUS_EN, 0);
	if (cl->pd_link) {
		device_link_del(cl->pd_link);
		cl->pd_link = NULL;
	}

	return 0;
}

static unsigned int mca_crop_mask(unsigned int mask, int nchans)
{
	while (hweight32(mask) > nchans)
		mask &= ~(1 << __fls(mask));

	return mask;
}

static int mca_configure_serdes(struct mca_cluster *cl, int stream,
				unsigned int mask, int slots, int nchans,
				int slot_width, int portmask)
{
	__iomem void *serdes_base = cl->serdes[stream];
	const struct mca_serdes_regs *regs = cl->host->serdes_regs[stream];
	bool is_tx = stream == SNDRV_PCM_STREAM_PLAYBACK;
	u32 serdes_conf, serdes_conf_mask;

	serdes_conf_mask = SERDES_CONF_WIDTH_MASK | SERDES_CONF_NCHANS;
	serdes_conf = FIELD_PREP(SERDES_CONF_NCHANS, max(slots, 1) - 1);
	switch (slot_width) {
	case 16:
		serdes_conf |= SERDES_CONF_WIDTH_16BIT;
		break;
	case 20:
		serdes_conf |= SERDES_CONF_WIDTH_20BIT;
		break;
	case 24:
		serdes_conf |= SERDES_CONF_WIDTH_24BIT;
		break;
	case 32:
		serdes_conf |= SERDES_CONF_WIDTH_32BIT;
		break;
	default:
		goto err;
	}

	serdes_conf_mask |= SERDES_CONF_SYNC_SEL;
	serdes_conf |= FIELD_PREP(SERDES_CONF_SYNC_SEL, cl->no + 1);

	if (is_tx) {
		serdes_conf_mask |= SERDES_CONF_UNK1 | SERDES_CONF_UNK2 |
				    SERDES_CONF_UNK3;
		serdes_conf |= SERDES_CONF_UNK1 | SERDES_CONF_UNK2 |
			       SERDES_CONF_UNK3;
	} else {
		serdes_conf_mask |= SERDES_CONF_UNK1 | SERDES_CONF_UNK2 |
				    SERDES_CONF_UNK3 |
				    SERDES_CONF_NO_DATA_FEEDBACK;
		serdes_conf |= SERDES_CONF_UNK1 | SERDES_CONF_UNK2 |
			       SERDES_CONF_NO_DATA_FEEDBACK;
	}

	mca_modify_reg(serdes_base + regs->conf, serdes_conf_mask, serdes_conf);

	if (is_tx) {
		writel_relaxed(0xffffffff,
			       serdes_base + regs->slotmask);
		writel_relaxed(~((u32)mca_crop_mask(mask, nchans)),
			       serdes_base + regs->slotmask + 0x4);
		writel_relaxed(0xffffffff,
			       serdes_base + regs->slotmask + 0x8);
		writel_relaxed(~((u32)mask),
			       serdes_base + regs->slotmask + 0xc);
	} else {
		writel_relaxed(0xffffffff,
			       serdes_base + regs->slotmask);
		writel_relaxed(~((u32)mca_crop_mask(mask, nchans)),
			       serdes_base + regs->slotmask + 0x4);
		writel_relaxed(portmask,
			       serdes_base + regs->port);
	}

	return 0;

err:
	dev_err(cl->host->dev,
		"unsupported SERDES configuration requested (mask=0x%x slots=%d slot_width=%d)\n",
		mask, slots, slot_width);
	return -EINVAL;
}

static int mca_fe_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct mca_cluster *cl = mca_dai_to_cluster(dai);
	unsigned int mask, nchannels;

	if (cl->tdm_slots) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			mask = cl->tdm_tx_mask;
		else
			mask = cl->tdm_rx_mask;

		nchannels = hweight32(mask);
	} else {
		nchannels = 2;
	}

	return snd_pcm_hw_constraint_minmax(substream->runtime,
					    SNDRV_PCM_HW_PARAM_CHANNELS,
					    1, nchannels);
}

static int mca_fe_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			       unsigned int rx_mask, int slots, int slot_width)
{
	struct mca_cluster *cl = mca_dai_to_cluster(dai);

	cl->tdm_slots = slots;
	cl->tdm_slot_width = slot_width;
	cl->tdm_tx_mask = tx_mask;
	cl->tdm_rx_mask = rx_mask;

	return 0;
}

static int mca_fe_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mca_cluster *cl = mca_dai_to_cluster(dai);
	struct mca_data *mca = cl->host;
	bool fpol_inv = false;
	u32 serdes_conf = 0;
	u32 bitstart;
	int stream;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		cl->clk_provider = true;
		break;

	case SND_SOC_DAIFMT_BC_FC:
		cl->clk_provider = false;
		break;

	default:
		goto err;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		fpol_inv = 0;
		bitstart = 1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		fpol_inv = 1;
		bitstart = 0;
		break;
	default:
		goto err;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
	case SND_SOC_DAIFMT_IB_IF:
		fpol_inv ^= 1;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
	case SND_SOC_DAIFMT_NB_IF:
		serdes_conf |= SERDES_CONF_BCLK_POL;
		break;
	}

	if (!fpol_inv)
		goto err;

	for_each_pcm_streams(stream) {
		const struct mca_serdes_regs *regs = mca->serdes_regs[stream];

		mca_modify_reg(cl->serdes[stream] + regs->conf,
			       SERDES_CONF_BCLK_POL, serdes_conf);
		writel_relaxed(bitstart, cl->serdes[stream] + regs->bitstart);
	}

	return 0;

err:
	dev_err(mca->dev, "unsupported DAI format (0x%x) requested\n", fmt);
	return -EINVAL;
}

static int mca_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct mca_cluster *cl = mca_dai_to_cluster(dai);

	cl->bclk_ratio = ratio;

	return 0;
}

static const struct {
	unsigned long rate;
	u32 sel;
} mca_t8132_mclk_srcs[] = {
	{ 24000000, 0 },
	{ 12000000, 5 },
	{ 6000000, 6 },
	{ 12288000, 7 },
	{ 6144000, 8 },
	{ 5644800, 9 },
	{ 3072000, 10 },
	{ 2822400, 11 },
};

static int mca_t8132_set_mclk(struct mca_cluster *cl, unsigned long rate)
{
	__iomem void *mux = cl->host->mclk_base + 4 * cl->no;
	unsigned long src, div;
	u32 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(mca_t8132_mclk_srcs); i++) {
		src = mca_t8132_mclk_srcs[i].rate;
		if (!rate || src % rate)
			continue;
		div = src / rate;
		if (div > FIELD_MAX(T8132_MCLK_CONF_DIV))
			continue;

		writel_relaxed(FIELD_PREP(T8132_MCLK_CONF_DIV, div),
			       cl->base + REG_MCLK_CONF);
		mca_modify_reg(mux, T8132_MCLK_MUX_SEL,
			       FIELD_PREP(T8132_MCLK_MUX_SEL,
					  mca_t8132_mclk_srcs[i].sel));
		return readl_poll_timeout_atomic(mux, val,
						 !(val & T8132_MCLK_MUX_BUSY),
						 1, 1000);
	}

	dev_err(cl->host->dev, "cluster %d: no MCLK source for %lu Hz\n",
		cl->no, rate);
	return -EINVAL;
}

static int mca_fe_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct mca_cluster *cl = mca_dai_to_cluster(dai);
	struct mca_data *mca = cl->host;
	struct device *dev = mca->dev;
	unsigned int samp_rate = params_rate(params);
	bool is_tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	bool refine_tdm = false;
	unsigned long bclk_ratio;
	unsigned int tdm_slots, tdm_slot_width, tdm_mask;
	u32 regval, pad;
	int ret, portmask, nchans_ceiled;

	if (!cl->tdm_slot_width) {
		/*
		 * We were not given TDM settings from above, set initial
		 * guesses which will later be refined.
		 */
		tdm_slot_width = params_width(params);
		tdm_slots = params_channels(params);
		refine_tdm = true;
	} else {
		tdm_slot_width = cl->tdm_slot_width;
		tdm_slots = cl->tdm_slots;
		tdm_mask = is_tx ? cl->tdm_tx_mask : cl->tdm_rx_mask;
	}

	if (cl->bclk_ratio)
		bclk_ratio = cl->bclk_ratio;
	else
		bclk_ratio = tdm_slot_width * tdm_slots;

	if (refine_tdm) {
		int nchannels = params_channels(params);

		if (nchannels > 2) {
			dev_err(dev, "missing TDM for stream with two or more channels\n");
			return -EINVAL;
		}

		if ((bclk_ratio % nchannels) != 0) {
			dev_err(dev, "BCLK ratio (%ld) not divisible by no. of channels (%d)\n",
				bclk_ratio, nchannels);
			return -EINVAL;
		}

		tdm_slot_width = bclk_ratio / nchannels;

		if (tdm_slot_width > 32 && nchannels == 1)
			tdm_slot_width = 32;

		if (tdm_slot_width < params_width(params)) {
			dev_err(dev, "TDM slots too narrow (tdm=%u params=%d)\n",
				tdm_slot_width, params_width(params));
			return -EINVAL;
		}

		tdm_mask = (1 << tdm_slots) - 1;
	}

	portmask = mca_fe_get_portmask(substream);
	if (!portmask)
		return -EINVAL;

	ret = mca_configure_serdes(cl, substream->stream,
				   tdm_mask, tdm_slots, params_channels(params),
				   tdm_slot_width, portmask);
	if (ret)
		return ret;

	pad = 32 - params_width(params);

	if (mca->t8132) {
		__iomem void *adapter = cl->adapter[substream->stream];

		if (is_tx)
			mca_modify_reg(adapter, DMA_ADAPTER_TX_LSB_PAD |
				       DMA_ADAPTER_TX_NCHANS | T8132_ADAPTER_THRESHOLD,
				       FIELD_PREP(DMA_ADAPTER_TX_LSB_PAD, pad) |
				       FIELD_PREP(T8132_ADAPTER_THRESHOLD, 0x10));
		else
			mca_modify_reg(adapter, DMA_ADAPTER_RX_MSB_PAD |
				       DMA_ADAPTER_RX_NCHANS | T8132_ADAPTER_THRESHOLD,
				       FIELD_PREP(DMA_ADAPTER_RX_MSB_PAD, pad) |
				       FIELD_PREP(T8132_ADAPTER_THRESHOLD, 0x10));
		mca_modify_reg(adapter + T8132_REG_ADAPTER_EN,
			       is_tx ? T8132_ADAPTER_TX_EN : T8132_ADAPTER_RX_EN,
			       is_tx ? T8132_ADAPTER_TX_EN : T8132_ADAPTER_RX_EN);
		goto clocks;
	}

	/*
	 * TODO: Here the register semantics aren't clear.
	 */
	nchans_ceiled = min_t(int, params_channels(params), 4);
	regval = FIELD_PREP(DMA_ADAPTER_NCHANS, nchans_ceiled) |
		 FIELD_PREP(DMA_ADAPTER_TX_NCHANS, 0x2) |
		 FIELD_PREP(DMA_ADAPTER_RX_NCHANS, 0x2) |
		 FIELD_PREP(DMA_ADAPTER_TX_LSB_PAD, pad) |
		 FIELD_PREP(DMA_ADAPTER_RX_MSB_PAD, pad);

	writel_relaxed(regval, cl->adapter[substream->stream]);

clocks:
	if (!mca_fe_clocks_in_use(cl)) {
		/*
		 * Set up FSYNC duty cycle as even as possible.
		 */
		writel_relaxed((bclk_ratio / 2) - 1,
			       cl->syncgen + REG_SYNCGEN_HI_PERIOD);
		writel_relaxed(((bclk_ratio + 1) / 2) - 1,
			       cl->syncgen + REG_SYNCGEN_LO_PERIOD);

		if (mca->t8132) {
			ret = mca_t8132_set_mclk(cl, bclk_ratio * samp_rate);
		} else {
			writel_relaxed(FIELD_PREP(MCLK_CONF_DIV, 0x1),
				       cl->base + REG_MCLK_CONF);
			ret = clk_set_rate(cl->clk_parent, bclk_ratio * samp_rate);
		}
		if (ret) {
			dev_err(mca->dev, "cluster %d: unable to set clock parent: %d\n",
				cl->no, ret);
			return ret;
		}
	}

	return 0;
}

static const struct snd_soc_dai_ops mca_fe_ops = {
	.startup = mca_fe_startup,
	.set_fmt = mca_fe_set_fmt,
	.set_bclk_ratio = mca_set_bclk_ratio,
	.set_tdm_slot = mca_fe_set_tdm_slot,
	.hw_params = mca_fe_hw_params,
	.trigger = mca_fe_trigger,
	.prepare = mca_fe_prepare,
	.hw_free = mca_fe_hw_free,
};

/*
 * Is there a FE attached which will be feeding this port's clocks?
 */
static bool mca_be_clk_started(struct mca_cluster *cl)
{
	int stream;

	for_each_pcm_streams(stream)
		if (cl->port_clk_started[stream])
			return true;
	return false;
}

static struct snd_soc_pcm_runtime *mca_be_get_fe(struct snd_soc_pcm_runtime *be,
						 int stream)
{
	struct snd_soc_pcm_runtime *fe = NULL;
	struct snd_soc_dpcm *dpcm;

	for_each_dpcm_fe(be, stream, dpcm) {
		if (fe && dpcm->fe != fe) {
			dev_err(be->dev, "many FE per one BE unsupported\n");
			return NULL;
		}

		fe = dpcm->fe;
	}

	return fe;
}

static int mca_be_prepare(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *be = snd_soc_substream_to_rtd(substream);
	struct snd_soc_pcm_runtime *fe = mca_be_get_fe(be, substream->stream);
	struct mca_cluster *cl = mca_dai_to_cluster(dai);
	struct mca_data *mca = cl->host;
	struct mca_cluster *fe_cl, *fe_clk_cl;
	int ret;

	fe_cl = mca_dai_to_cluster(snd_soc_rtd_to_cpu(fe, 0));

	if (!fe_cl->clk_provider)
		return 0;

	if (cl->port_clk_driver < 0)
		return 0;

	fe_clk_cl = &mca->clusters[cl->port_clk_driver];

	/*
	 * Typically the CODECs we are paired with will require clocks
	 * to be present at time of unmute with the 'mute_stream' op
	 * or at time of DAPM widget power-up. We need to enable clocks
	 * here at the latest (frontend prepare would be too late).
	 */
	if (!mca_fe_clocks_in_use(fe_clk_cl)) {
		ret = mca_fe_enable_clocks(fe_clk_cl);
		if (ret < 0)
			return ret;
	}

	cl->clocks_in_use[substream->stream] = true;

	return 0;
}

static int mca_be_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *be = snd_soc_substream_to_rtd(substream);
	struct snd_soc_pcm_runtime *fe = mca_be_get_fe(be, substream->stream);
	struct mca_cluster *cl = mca_dai_to_cluster(dai);
	struct mca_cluster *fe_cl;
	struct mca_data *mca = cl->host;

	if (!fe)
		return -EINVAL;
	fe_cl = mca_dai_to_cluster(snd_soc_rtd_to_cpu(fe, 0));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		writel_relaxed(mca->t8132 ? BIT(fe_cl->no) :
			       PORT_DATA_SEL_TXA(fe_cl->no),
			       cl->port + REG_PORT_DATA_SEL);
		mca_modify_reg(cl->port + REG_PORT_ENABLES, PORT_ENABLES_TX_DATA,
			       PORT_ENABLES_TX_DATA);
	}

	if (!fe_cl->clk_provider)
		return 0;

	if (mca_be_clk_started(cl)) {
		/*
		 * Port is already started in the other direction.
		 * Make sure there isn't a conflict with another cluster
		 * driving the port clocks.
		 */
		if (cl->port_clk_driver != fe_cl->no)
			return -EINVAL;

		cl->port_clk_started[substream->stream] = true;
		return 0;
	}

	writel_relaxed(FIELD_PREP(PORT_CLOCK_SEL, fe_cl->no + 1),
		       cl->port + REG_PORT_CLOCK_SEL);
	mca_modify_reg(cl->port + REG_PORT_ENABLES, PORT_ENABLES_CLOCKS,
		       PORT_ENABLES_CLOCKS);

	scoped_guard(mutex, &mca->port_mutex)
		cl->port_clk_driver = fe_cl->no;
	cl->port_clk_started[substream->stream] = true;

	return 0;
}

static void mca_be_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *be = snd_soc_substream_to_rtd(substream);
	struct snd_soc_pcm_runtime *fe = mca_be_get_fe(be, substream->stream);
	struct mca_cluster *cl = mca_dai_to_cluster(dai);
	struct mca_cluster *fe_cl;
	struct mca_data *mca = cl->host;

	if (cl->clocks_in_use[substream->stream] &&
		!WARN_ON(cl->port_clk_driver < 0)) {
		struct mca_cluster *fe_cl = &mca->clusters[cl->port_clk_driver];

		/*
		 * Typically the CODECs we are paired with will require clocks
		 * to be present at time of mute with the 'mute_stream' op.
		 * We need to disable the clocks here at the earliest (hw_free
		 * would be too early).
		 *
		 * We are operating on a foreign cluster here, but since we
		 * belong to the same PCM, accesses should have been
		 * synchronized at ASoC level.
		 */
		cl->clocks_in_use[substream->stream] = false;

		if (!mca_fe_clocks_in_use(fe_cl))
			mca_fe_disable_clocks(fe_cl);
	}

	if (!fe)
		return;
	fe_cl = mca_dai_to_cluster(snd_soc_rtd_to_cpu(fe, 0));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		mca_modify_reg(cl->port + REG_PORT_ENABLES, PORT_ENABLES_TX_DATA, 0);
		writel_relaxed(0, cl->port + REG_PORT_DATA_SEL);
	}

	if (!fe_cl->clk_provider)
		return;

	cl->port_clk_started[substream->stream] = false;
	if (!mca_be_clk_started(cl)) {
		/*
		 * Were we the last direction to shutdown?
		 * Turn off the lights (clocks).
		 */
		mca_modify_reg(cl->port + REG_PORT_ENABLES, PORT_ENABLES_CLOCKS, 0);
		writel_relaxed(0, cl->port + REG_PORT_CLOCK_SEL);
		scoped_guard(mutex, &mca->port_mutex)
			cl->port_clk_driver = -1;

	}
}

static const struct snd_soc_dai_ops mca_be_ops = {
	.prepare = mca_be_prepare,
	.startup = mca_be_startup,
	.shutdown = mca_be_shutdown,
};

static int mca_set_runtime_hwparams(struct snd_soc_component *component,
				    struct snd_pcm_substream *substream,
				    struct dma_chan *chan)
{
	struct device *dma_dev = chan->device->dev;
	struct snd_dmaengine_dai_dma_data dma_data = {};
	int ret;

	struct snd_pcm_hardware hw;

	memset(&hw, 0, sizeof(hw));

	hw.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		  SNDRV_PCM_INFO_INTERLEAVED;
	hw.periods_min = 2;
	hw.periods_max = UINT_MAX;
	hw.period_bytes_min = 256;
	hw.period_bytes_max = dma_get_max_seg_size(dma_dev);
	hw.buffer_bytes_max = SIZE_MAX;
	hw.fifo_size = 16;

	ret = snd_dmaengine_pcm_refine_runtime_hwparams(substream, &dma_data,
							&hw, chan);

	if (ret)
		return ret;

	return snd_soc_set_runtime_hwparams(substream, &hw);
}

static int mca_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mca_cluster *cl = mca_dai_to_cluster(snd_soc_rtd_to_cpu(rtd, 0));
	struct dma_chan *chan = cl->dma_chans[substream->stream];
	int ret;

	if (rtd->dai_link->no_pcm)
		return 0;

	ret = mca_set_runtime_hwparams(component, substream, chan);
	if (ret)
		return ret;

	return snd_dmaengine_pcm_open(substream, chan);
}

static int mca_hw_params(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
	struct dma_slave_config slave_config;
	int ret;

	if (rtd->dai_link->no_pcm)
		return 0;

	memset(&slave_config, 0, sizeof(slave_config));
	ret = snd_hwparams_to_dma_slave_config(substream, params,
					       &slave_config);
	if (ret < 0)
		return ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		slave_config.dst_port_window_size =
			min_t(u32, params_channels(params), 4);
	else
		slave_config.src_port_window_size =
			min_t(u32, params_channels(params), 4);

	return dmaengine_slave_config(chan, &slave_config);
}

static int mca_close(struct snd_soc_component *component,
		     struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	if (rtd->dai_link->no_pcm)
		return 0;

	return snd_dmaengine_pcm_close(substream);
}

static int mca_trigger(struct snd_soc_component *component,
		       struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	if (rtd->dai_link->no_pcm)
		return 0;

	/*
	 * Before we do the PCM trigger proper, insert an opportunity
	 * to reset the frontend's SERDES.
	 */
	mca_fe_early_trigger(substream, cmd, snd_soc_rtd_to_cpu(rtd, 0));

	return snd_dmaengine_pcm_trigger(substream, cmd);
}

static snd_pcm_uframes_t mca_pointer(struct snd_soc_component *component,
				     struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	if (rtd->dai_link->no_pcm)
		return -ENOTSUPP;

	return snd_dmaengine_pcm_pointer(substream);
}

static struct dma_chan *mca_request_dma_channel(struct mca_cluster *cl, unsigned int stream)
{
	bool is_tx = (stream == SNDRV_PCM_STREAM_PLAYBACK);

	if (cl->host->t8132) {
		char *name = devm_kasprintf(cl->host->dev, GFP_KERNEL,
					    is_tx ? "tx%d" : "rx%d", cl->no);

		return of_dma_request_slave_channel(cl->host->dev->of_node, name);
	}
#ifndef USE_RXB_FOR_CAPTURE
	char *name = devm_kasprintf(cl->host->dev, GFP_KERNEL,
				    is_tx ? "tx%da" : "rx%da", cl->no);
#else
	char *name = devm_kasprintf(cl->host->dev, GFP_KERNEL,
				    is_tx ? "tx%da" : "rx%db", cl->no);
#endif
	return of_dma_request_slave_channel(cl->host->dev->of_node, name);

}

static void mca_pcm_free(struct snd_soc_component *component,
			 struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_chip(pcm);
	struct mca_cluster *cl = mca_dai_to_cluster(snd_soc_rtd_to_cpu(rtd, 0));
	unsigned int i;

	if (rtd->dai_link->no_pcm)
		return;

	for_each_pcm_streams(i) {
		struct snd_pcm_substream *substream =
			rtd->pcm->streams[i].substream;

		if (!substream || !cl->dma_chans[i])
			continue;

		dma_release_channel(cl->dma_chans[i]);
		cl->dma_chans[i] = NULL;
	}
}


static int mca_pcm_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd)
{
	struct mca_cluster *cl = mca_dai_to_cluster(snd_soc_rtd_to_cpu(rtd, 0));
	unsigned int i;

	if (rtd->dai_link->no_pcm)
		return 0;

	for_each_pcm_streams(i) {
		struct snd_pcm_substream *substream =
			rtd->pcm->streams[i].substream;
		struct dma_chan *chan;

		if (!substream)
			continue;

		chan = mca_request_dma_channel(cl, i);

		if (IS_ERR_OR_NULL(chan)) {
			mca_pcm_free(component, rtd->pcm);

			if (chan && PTR_ERR(chan) == -EPROBE_DEFER)
				return PTR_ERR(chan);

			dev_err(component->dev, "unable to obtain DMA channel (stream %d cluster %d): %pe\n",
				i, cl->no, chan);

			if (!chan)
				return -EINVAL;
			return PTR_ERR(chan);
		}

		cl->dma_chans[i] = chan;
		snd_pcm_set_managed_buffer(substream, SNDRV_DMA_TYPE_DEV_IRAM,
					   chan->device->dev, 512 * 1024 * 6,
					   SIZE_MAX);
	}

	return 0;
}

static const struct snd_soc_component_driver mca_component = {
	.name = "apple-mca",
	.open = mca_pcm_open,
	.close = mca_close,
	.hw_params = mca_hw_params,
	.trigger = mca_trigger,
	.pointer = mca_pointer,
	.pcm_new = mca_pcm_new,
	.pcm_free = mca_pcm_free,
};

static void apple_mca_release(struct mca_data *mca)
{
	int i;

	for (i = 0; i < mca->nclusters; i++) {
		struct mca_cluster *cl = &mca->clusters[i];

		if (!IS_ERR_OR_NULL(cl->clk_parent))
			clk_put(cl->clk_parent);

		if (!IS_ERR_OR_NULL(cl->pd_dev))
			dev_pm_domain_detach(cl->pd_dev, true);
	}

	if (mca->pd_link) {
		device_link_del(mca->pd_link);
		mca->pd_link = NULL;
	}

	if (!IS_ERR_OR_NULL(mca->pd_dev))
		dev_pm_domain_detach(mca->pd_dev, true);

	reset_control_rearm(mca->rstc);
}

static int apple_mca_probe(struct platform_device *pdev)
{
	struct mca_data *mca;
	struct mca_cluster *clusters;
	struct snd_soc_dai_driver *dai_drivers;
	struct resource *res;
	void __iomem *base;
	int nclusters;
	bool t8132;
	int ret, i;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	t8132 = of_device_is_compatible(pdev->dev.of_node, "apple,t8132-mca");
	if (t8132) {
		if (resource_size(res) < T8132_ADAPTER_OFF +
		    T8132_UNIT_STRIDE * T8132_NCLUSTERS)
			return -EINVAL;
		nclusters = T8132_NCLUSTERS;
	} else {
		if (resource_size(res) < CLUSTER_STRIDE)
			return -EINVAL;
		nclusters = (resource_size(res) - CLUSTER_STRIDE) /
			    CLUSTER_STRIDE + 1;
	}

	mca = devm_kzalloc(&pdev->dev, struct_size(mca, clusters, nclusters),
			   GFP_KERNEL);
	if (!mca)
		return -ENOMEM;
	mca->dev = &pdev->dev;
	mca->nclusters = nclusters;
	mutex_init(&mca->port_mutex);
	platform_set_drvdata(pdev, mca);
	clusters = mca->clusters;

	mca->t8132 = t8132;
	mca->serdes_regs[SNDRV_PCM_STREAM_PLAYBACK] = &mca_serdes_tx;
	if (t8132) {
		mca->serdes_regs[SNDRV_PCM_STREAM_CAPTURE] = &mca_t8132_serdes_rx;
		mca->grst_base = base;
		mca->mclk_base = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(mca->mclk_base))
			return PTR_ERR(mca->mclk_base);
	} else {
		mca->serdes_regs[SNDRV_PCM_STREAM_CAPTURE] = &mca_serdes_rx;
		mca->switch_base = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(mca->switch_base))
			return PTR_ERR(mca->switch_base);
	}

	mca->rstc = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
	if (IS_ERR(mca->rstc))
		return PTR_ERR(mca->rstc);

	dai_drivers = devm_kzalloc(
		&pdev->dev, sizeof(*dai_drivers) * 2 * nclusters, GFP_KERNEL);
	if (!dai_drivers)
		return -ENOMEM;

	mca->pd_dev = dev_pm_domain_attach_by_id(&pdev->dev, 0);
	if (IS_ERR(mca->pd_dev))
		return -EINVAL;

	mca->pd_link = device_link_add(&pdev->dev, mca->pd_dev,
				       DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME |
					       DL_FLAG_RPM_ACTIVE);
	if (!mca->pd_link) {
		ret = -EINVAL;
		/* Prevent an unbalanced reset rearm */
		mca->rstc = NULL;
		goto err_release;
	}

	reset_control_reset(mca->rstc);

	for (i = 0; i < nclusters; i++) {
		struct mca_cluster *cl = &clusters[i];
		struct snd_soc_dai_driver *fe =
			&dai_drivers[mca->nclusters + i];
		struct snd_soc_dai_driver *be = &dai_drivers[i];

		cl->host = mca;
		cl->no = i;
		cl->port_clk_driver = -1;
		if (t8132) {
			cl->base = base + T8132_CLK_OFF + T8132_UNIT_STRIDE * i;
			cl->syncgen = cl->base + T8132_SYNCGEN_OFF;
			cl->port = cl->base + T8132_PORT_OFF;
			cl->serdes[SNDRV_PCM_STREAM_PLAYBACK] =
				base + T8132_TX_SERDES_OFF + T8132_UNIT_STRIDE * i;
			cl->serdes[SNDRV_PCM_STREAM_CAPTURE] =
				base + T8132_RX_SERDES_OFF + T8132_UNIT_STRIDE * i;
			cl->adapter[SNDRV_PCM_STREAM_PLAYBACK] =
				base + T8132_ADAPTER_OFF + T8132_UNIT_STRIDE * i;
			cl->adapter[SNDRV_PCM_STREAM_CAPTURE] =
				cl->adapter[SNDRV_PCM_STREAM_PLAYBACK];
		} else {
			cl->base = base + CLUSTER_STRIDE * i;
			cl->syncgen = cl->base + CLUSTER_SYNCGEN_OFF;
			cl->port = cl->base + CLUSTER_PORT_OFF;
			cl->serdes[SNDRV_PCM_STREAM_PLAYBACK] =
				cl->base + CLUSTER_TX_OFF;
			cl->serdes[SNDRV_PCM_STREAM_CAPTURE] =
				cl->base + CLUSTER_RX_OFF;
			cl->adapter[SNDRV_PCM_STREAM_PLAYBACK] =
				mca->switch_base + REG_DMA_ADAPTER_A(i);
#ifndef USE_RXB_FOR_CAPTURE
			cl->adapter[SNDRV_PCM_STREAM_CAPTURE] =
				mca->switch_base + REG_DMA_ADAPTER_A(i);
#else
			cl->adapter[SNDRV_PCM_STREAM_CAPTURE] =
				mca->switch_base + REG_DMA_ADAPTER_B(i);
#endif
			cl->clk_parent = of_clk_get(pdev->dev.of_node, i);
			if (IS_ERR(cl->clk_parent)) {
				dev_err(&pdev->dev, "unable to obtain clock %d: %ld\n",
					i, PTR_ERR(cl->clk_parent));
				ret = PTR_ERR(cl->clk_parent);
				goto err_release;
			}
		}
		cl->pd_dev = dev_pm_domain_attach_by_id(&pdev->dev, i + 1);
		if (IS_ERR(cl->pd_dev)) {
			dev_err(&pdev->dev,
				"unable to obtain cluster %d PD: %ld\n", i,
				PTR_ERR(cl->pd_dev));
			ret = PTR_ERR(cl->pd_dev);
			goto err_release;
		}

		fe->id = i;
		fe->name =
			devm_kasprintf(&pdev->dev, GFP_KERNEL, "mca-pcm-%d", i);
		if (!fe->name) {
			ret = -ENOMEM;
			goto err_release;
		}
		fe->ops = &mca_fe_ops;
		fe->playback.channels_min = 1;
		fe->playback.channels_max = 32;
		fe->playback.rates = SNDRV_PCM_RATE_8000_192000;
		fe->playback.formats = APPLE_MCA_FMTBITS;
		fe->capture.channels_min = 1;
		fe->capture.channels_max = 32;
		fe->capture.rates = SNDRV_PCM_RATE_8000_192000;
		fe->capture.formats = APPLE_MCA_FMTBITS;
		fe->symmetric_rate = 1;

		fe->playback.stream_name =
			devm_kasprintf(&pdev->dev, GFP_KERNEL, "PCM%d TX", i);
		fe->capture.stream_name =
			devm_kasprintf(&pdev->dev, GFP_KERNEL, "PCM%d RX", i);

		if (!fe->playback.stream_name || !fe->capture.stream_name) {
			ret = -ENOMEM;
			goto err_release;
		}

		be->id = i + nclusters;
		be->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "mca-i2s-%d", i);
		if (!be->name) {
			ret = -ENOMEM;
			goto err_release;
		}
		be->ops = &mca_be_ops;
		be->playback.channels_min = 1;
		be->playback.channels_max = 32;
		be->playback.rates = SNDRV_PCM_RATE_8000_192000;
		be->playback.formats = APPLE_MCA_FMTBITS;
		be->capture.channels_min = 1;
		be->capture.channels_max = 32;
		be->capture.rates = SNDRV_PCM_RATE_8000_192000;
		be->capture.formats = APPLE_MCA_FMTBITS;

		be->playback.stream_name =
			devm_kasprintf(&pdev->dev, GFP_KERNEL, "I2S%d TX", i);
		be->capture.stream_name =
			devm_kasprintf(&pdev->dev, GFP_KERNEL, "I2S%d RX", i);
		if (!be->playback.stream_name || !be->capture.stream_name) {
			ret = -ENOMEM;
			goto err_release;
		}
	}

	ret = snd_soc_register_component(&pdev->dev, &mca_component,
					 dai_drivers, nclusters * 2);
	if (ret) {
		dev_err(&pdev->dev, "unable to register ASoC component: %d\n",
			ret);
		goto err_release;
	}

	return 0;

err_release:
	apple_mca_release(mca);
	return ret;
}

static void apple_mca_remove(struct platform_device *pdev)
{
	struct mca_data *mca = platform_get_drvdata(pdev);

	snd_soc_unregister_component(&pdev->dev);
	apple_mca_release(mca);
}

static const struct of_device_id apple_mca_of_match[] = {
	{ .compatible = "apple,t8132-mca", },
	{ .compatible = "apple,t8103-mca", },
	{ .compatible = "apple,mca", },
	{}
};
MODULE_DEVICE_TABLE(of, apple_mca_of_match);

static struct platform_driver apple_mca_driver = {
	.driver = {
		.name = "apple-mca",
		.of_match_table = apple_mca_of_match,
	},
	.probe = apple_mca_probe,
	.remove = apple_mca_remove,
};
module_platform_driver(apple_mca_driver);

MODULE_AUTHOR("Martin Povišer <povik+lin@cutebit.org>");
MODULE_DESCRIPTION("ASoC Apple MCA driver");
MODULE_LICENSE("GPL");
