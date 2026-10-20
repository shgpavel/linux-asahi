// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2023 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modes.h>

#include "afk.h"
#include "connector.h"
#include "dcp.h"
#include "dcp-internal.h"
#include "ibootep.h"
#include "iomfb_internal.h"
#include "plane.h"

#define IBOOT_CMD_TYPE 0xc0
#define IBOOT_RXBUF_LEN 0x4000
#define IBOOT_HPD_POLL_MS 250
#define IBOOT_HPD_TRIES 40
#define IBOOT_SET_MODE_TRIES 5

enum iboot_op {
	IBOOT_SET_SURFACE = 1,
	IBOOT_SET_POWER = 2,
	IBOOT_GET_HPD = 3,
	IBOOT_GET_TIMING_MODES = 4,
	IBOOT_GET_COLOR_MODES = 5,
	IBOOT_SET_MODE = 6,
};

enum iboot_surface_fmt {
	IBOOT_FMT_BGRA = 1,
	IBOOT_FMT_RGBA = 3,
	IBOOT_FMT_W30R = 9,
};

struct iboot_cmd_hdr {
	__le32 op;
	__le32 len;
	__le32 unk1;
	__le32 unk2;
} __packed;

struct iboot_resp_hdr {
	__le32 op;
	__le32 len;
} __packed;

struct iboot_hpd_resp {
	u8 hpd;
	u8 pad[3];
	__le32 timing_cnt;
	__le32 color_cnt;
} __packed;

struct iboot_plane {
	__le32 unk1;
	__le64 addr;
	__le32 tile_size;
	__le32 stride;
	__le32 unk2[4];
	__le32 addr_format;
	__le32 unk3;
} __packed;

struct iboot_layer {
	struct iboot_plane planes[3];
	__le32 unk;
	__le32 plane_cnt;
	__le32 width;
	__le32 height;
	__le32 surface_fmt;
	__le32 colorspace;
	__le32 eotf;
	u8 transform;
	u8 padding[3];
} __packed;

struct iboot_set_mode_cmd {
	struct iboot_timing_mode tmode;
	struct iboot_color_mode cmode;
} __packed;

static int iboot_cmd(struct apple_dcp *dcp, u32 op, const void *in,
		     size_t in_len, void *out, size_t out_len)
{
	struct iboot_cmd_hdr *hdr;
	size_t tx_len = sizeof(*hdr) + in_len;
	void *tx, *rx;
	u32 retcode;
	int ret;

	if (!dcp->iboot_service)
		return -ENODEV;

	tx = kzalloc(tx_len, GFP_KERNEL);
	rx = kzalloc(IBOOT_RXBUF_LEN, GFP_KERNEL);
	if (!tx || !rx) {
		ret = -ENOMEM;
		goto out;
	}

	hdr = tx;
	hdr->op = cpu_to_le32(op);
	hdr->len = cpu_to_le32(tx_len);
	if (in_len)
		memcpy(tx + sizeof(*hdr), in, in_len);

	mutex_lock(&dcp->iboot_mutex);
	ret = afk_send_command(dcp->iboot_service, IBOOT_CMD_TYPE, tx, tx_len,
			       rx, IBOOT_RXBUF_LEN, &retcode);
	mutex_unlock(&dcp->iboot_mutex);
	if (ret) {
		dev_warn(dcp->dev, "iboot: op %u failed: %d\n", op, ret);
		goto out;
	}
	if (retcode) {
		dev_warn(dcp->dev, "iboot: op %u returned 0x%x\n", op, retcode);
		ret = -EIO;
		goto out;
	}

	if (out && out_len)
		memcpy(out, rx + sizeof(struct iboot_resp_hdr),
		       min(out_len, IBOOT_RXBUF_LEN - sizeof(struct iboot_resp_hdr)));

out:
	kfree(rx);
	kfree(tx);
	return ret;
}

static int iboot_set_power(struct apple_dcp *dcp, bool power)
{
	u32 pwr = power;

	return iboot_cmd(dcp, IBOOT_SET_POWER, &pwr, 1, NULL, 0);
}

static bool iboot_color_better(const struct iboot_color_mode *a,
			       const struct iboot_color_mode *b)
{
	u32 abpp = le32_to_cpu(a->bpp), bbpp = le32_to_cpu(b->bpp);

	if (!!a->valid != !!b->valid)
		return a->valid;
	if ((abpp <= 32) != (bbpp <= 32))
		return abpp <= 32;
	if (abpp != bbpp)
		return abpp > bbpp;
	if (a->colorimetry != b->colorimetry)
		return le32_to_cpu(a->colorimetry) < le32_to_cpu(b->colorimetry);
	if (a->encoding != b->encoding)
		return le32_to_cpu(a->encoding) < le32_to_cpu(b->encoding);
	return le32_to_cpu(a->eotf) < le32_to_cpu(b->eotf);
}

static bool iboot_timing_better(const struct iboot_timing_mode *a,
				const struct iboot_timing_mode *b)
{
	u32 afps = le32_to_cpu(a->fps), bfps = le32_to_cpu(b->fps);
	u64 aarea = (u64)le32_to_cpu(a->width) * le32_to_cpu(a->height);
	u64 barea = (u64)le32_to_cpu(b->width) * le32_to_cpu(b->height);

	if ((afps <= 60 << 16) != (bfps <= 60 << 16))
		return afps <= 60 << 16;
	if (aarea != barea)
		return aarea > barea;
	return afps > bfps;
}

static int iboot_update_modes(struct apple_dcp *dcp)
{
	struct drm_device *drm = dcp->crtc->base.dev;
	struct iboot_hpd_resp hpd;
	struct iboot_timing_mode *tmodes = NULL;
	struct iboot_color_mode *cmodes;
	struct dcp_display_mode *modes;
	unsigned int ntm, ncm, i, n = 0;
	int best_c = -1, pref = -1;
	void *buf;
	int ret;

	ret = iboot_cmd(dcp, IBOOT_GET_HPD, NULL, 0, &hpd, sizeof(hpd));
	if (ret)
		return ret;
	if (!hpd.hpd || !hpd.timing_cnt || !hpd.color_cnt)
		return 0;

	buf = kzalloc(IBOOT_RXBUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = iboot_cmd(dcp, IBOOT_GET_COLOR_MODES, NULL, 0, buf, IBOOT_RXBUF_LEN);
	if (ret)
		goto out;
	ncm = min_t(u32, le32_to_cpup(buf),
		    (IBOOT_RXBUF_LEN - 12) / sizeof(*cmodes));
	cmodes = buf + 4;
	for (i = 0; i < ncm; i++)
		if (best_c < 0 || iboot_color_better(&cmodes[i], &cmodes[best_c]))
			best_c = i;
	if (best_c < 0 || !cmodes[best_c].valid) {
		ret = -ENODEV;
		goto out;
	}
	dcp->iboot_cmode = cmodes[best_c];

	ret = iboot_cmd(dcp, IBOOT_GET_TIMING_MODES, NULL, 0, buf, IBOOT_RXBUF_LEN);
	if (ret)
		goto out;
	ntm = min_t(u32, le32_to_cpup(buf),
		    (IBOOT_RXBUF_LEN - 12) / sizeof(*tmodes));
	tmodes = kmemdup(buf + 4, ntm * sizeof(*tmodes), GFP_KERNEL);
	modes = kcalloc(ntm, sizeof(*modes), GFP_KERNEL);
	if (!tmodes || !modes) {
		kfree(modes);
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < ntm; i++) {
		u32 w = le32_to_cpu(tmodes[i].width);
		u32 h = le32_to_cpu(tmodes[i].height);
		u32 fps = le32_to_cpu(tmodes[i].fps);
		struct drm_display_mode *m;

		if (!tmodes[i].valid || !w || !h || !fps)
			continue;

		m = drm_cvt_mode(drm, w, h, DIV_ROUND_CLOSEST(fps, 1 << 16),
				 false, false, false);
		if (!m)
			continue;

		modes[n].mode = *m;
		drm_mode_destroy(drm, m);
		modes[n].mode.type = DRM_MODE_TYPE_DRIVER;
		modes[n].timing_mode_id = i;
		modes[n].color_mode_id = best_c;

		if (pref < 0 || iboot_timing_better(&tmodes[i],
				&tmodes[modes[pref].timing_mode_id]))
			pref = n;
		n++;
	}
	if (pref >= 0)
		modes[pref].mode.type |= DRM_MODE_TYPE_PREFERRED;

	kfree(dcp->iboot_tmodes);
	dcp->iboot_tmodes = tmodes;
	dcp->iboot_nr_tmodes = ntm;
	tmodes = NULL;

	kfree(dcp->modes);
	dcp->modes = modes;
	dcp->nr_modes = n;
	ret = n;

out:
	kfree(tmodes);
	kfree(buf);
	return ret;
}

static void iboot_hpd_work(struct work_struct *work)
{
	struct apple_dcp *dcp = container_of(to_delayed_work(work),
					     struct apple_dcp, iboot_hpd_work);
	int ret;

	ret = iboot_update_modes(dcp);
	if (ret <= 0 && --dcp->iboot_hpd_tries) {
		queue_delayed_work(system_dfl_wq, &dcp->iboot_hpd_work,
				   msecs_to_jiffies(IBOOT_HPD_POLL_MS));
		return;
	}

	dev_dbg(dcp->dev, "iboot: %d display modes\n", ret);
	if (ret > 0 && dcp->connector) {
		dcp->connector->connected = true;
		schedule_work(&dcp->connector->hotplug_wq);
	}
}

void iboot_hpd_changed(struct apple_dcp *dcp)
{
	dcp->iboot_hpd_tries = IBOOT_HPD_TRIES;
	mod_delayed_work(system_dfl_wq, &dcp->iboot_hpd_work, 0);
}

void iboot_unplugged(struct apple_dcp *dcp)
{
	cancel_delayed_work_sync(&dcp->iboot_hpd_work);
	dcp->valid_mode = false;
	dcp->nr_modes = 0;
	dev_dbg(dcp->dev, "iboot: display unplugged\n");
	if (dcp->connector && dcp->connector->connected) {
		dcp->connector->connected = false;
		schedule_work(&dcp->connector->hotplug_wq);
	}
}

int iboot_start(struct apple_dcp *dcp)
{
	int ret;

	ret = wait_for_completion_timeout(&dcp->iboot_ready,
					  msecs_to_jiffies(2000));
	if (!ret) {
		dev_err(dcp->dev, "iboot: disp0-service did not appear\n");
		return -ETIMEDOUT;
	}

	ret = iboot_set_power(dcp, true);
	if (ret)
		dev_warn(dcp->dev, "iboot: set_power failed: %d\n", ret);

	dcp->active = true;
	complete(&dcp->start_done);
	return 0;
}

int iboot_modeset(struct apple_dcp *dcp, struct drm_crtc_state *crtc_state)
{
	struct iboot_set_mode_cmd cmd;
	struct dcp_display_mode *mode;
	int i, ret = 0;

	mode = lookup_mode(dcp, &crtc_state->mode);
	if (!mode || mode->timing_mode_id >= dcp->iboot_nr_tmodes) {
		dev_err(dcp->dev, "iboot: no mode for " DRM_MODE_FMT "\n",
			DRM_MODE_ARG(&crtc_state->mode));
		return -EINVAL;
	}

	cmd.tmode = dcp->iboot_tmodes[mode->timing_mode_id];
	cmd.cmode = dcp->iboot_cmode;

	for (i = 0; i < IBOOT_SET_MODE_TRIES; i++) {
		ret = iboot_cmd(dcp, IBOOT_SET_MODE, &cmd, sizeof(cmd), NULL, 0);
		if (!ret)
			break;
		msleep(200);
	}
	if (ret)
		return ret;

	dcp->valid_mode = true;
	return 0;
}

static int iboot_surface_fmt(u32 drm_fmt)
{
	switch (drm_fmt) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return IBOOT_FMT_BGRA;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return IBOOT_FMT_RGBA;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		return IBOOT_FMT_W30R;
	default:
		return -EINVAL;
	}
}

static enum hrtimer_restart iboot_vblank(struct hrtimer *timer)
{
	struct apple_dcp *dcp = container_of(timer, struct apple_dcp,
					     iboot_vblank_timer);

	dcp->iboot_last_vblank = ktime_get();
	dcp_drm_crtc_vblank(dcp->crtc);
	return HRTIMER_NORESTART;
}

static void iboot_queue_vblank(struct apple_dcp *dcp, struct drm_crtc *crtc)
{
	int refresh = drm_mode_vrefresh(&crtc->state->mode);
	ktime_t now = ktime_get(), next;

	if (refresh <= 0)
		refresh = 60;
	next = ktime_add_ns(dcp->iboot_last_vblank, NSEC_PER_SEC / refresh);
	if (ktime_before(next, now))
		next = now;

	hrtimer_start(&dcp->iboot_vblank_timer, next, HRTIMER_MODE_ABS);
}

static void iboot_hold_fb(struct apple_dcp *dcp, struct drm_framebuffer *fb)
{
	if (dcp->iboot_fbs[0] == fb)
		return;
	drm_framebuffer_get(fb);
	if (dcp->iboot_fbs[1])
		drm_framebuffer_put(dcp->iboot_fbs[1]);
	dcp->iboot_fbs[1] = dcp->iboot_fbs[0];
	dcp->iboot_fbs[0] = fb;
}

void iboot_release_fbs(struct apple_dcp *dcp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dcp->iboot_fbs); i++) {
		if (dcp->iboot_fbs[i])
			drm_framebuffer_put(dcp->iboot_fbs[i]);
		dcp->iboot_fbs[i] = NULL;
	}
}

void iboot_flush(struct apple_dcp *dcp, struct drm_crtc *crtc,
		 struct drm_atomic_commit *commit)
{
	struct drm_plane_state *ps;
	struct iboot_layer layer;
	struct drm_framebuffer *fb;
	int fmt;

	ps = drm_atomic_get_new_plane_state(commit, crtc->primary);
	if (ps && ps->fb && dcp->valid_mode) {
		fb = ps->fb;
		fmt = iboot_surface_fmt(fb->format->format);
		if (fmt < 0) {
			dev_warn(dcp->dev, "iboot: unsupported format %p4cc\n",
				 &fb->format->format);
		} else {
			memset(&layer, 0, sizeof(layer));
			layer.planes[0].addr = cpu_to_le64(to_apple_plane_state(ps)->iova);
			layer.planes[0].stride = cpu_to_le32(fb->pitches[0]);
			layer.planes[0].addr_format = cpu_to_le32(1);
			layer.plane_cnt = cpu_to_le32(1);
			layer.width = cpu_to_le32(fb->width);
			layer.height = cpu_to_le32(fb->height);
			layer.surface_fmt = cpu_to_le32(fmt);
			layer.colorspace = cpu_to_le32(2);
			layer.eotf = cpu_to_le32(1);
			if (!iboot_cmd(dcp, IBOOT_SET_SURFACE, &layer,
				       sizeof(layer), NULL, 0))
				iboot_hold_fb(dcp, fb);
		}
	}

	iboot_queue_vblank(dcp, crtc);
}

void iboot_poweron(struct apple_dcp *dcp)
{
	iboot_set_power(dcp, true);
}

void iboot_poweroff(struct apple_dcp *dcp)
{
	iboot_set_power(dcp, false);
	dcp->valid_mode = false;
	iboot_release_fbs(dcp);
}

static void disp_service_init(struct apple_epic_service *service, const char *name,
			const char *class, s64 unit)
{
	struct apple_dcp *dcp = service->ep->dcp;

	dcp->iboot_service = service;
	complete(&dcp->iboot_ready);
}

static void disp_service_teardown(struct apple_epic_service *service)
{
	struct apple_dcp *dcp = service->ep->dcp;

	if (dcp->iboot_service == service)
		dcp->iboot_service = NULL;
}

static const struct apple_epic_service_ops ibootep_ops[] = {
	{
		.name = "disp0-service",
		.init = disp_service_init,
		.teardown = disp_service_teardown,
	},
	{}
};

int ibootep_init(struct apple_dcp *dcp)
{
	init_completion(&dcp->iboot_ready);
	mutex_init(&dcp->iboot_mutex);
	INIT_DELAYED_WORK(&dcp->iboot_hpd_work, iboot_hpd_work);
	hrtimer_setup(&dcp->iboot_vblank_timer, iboot_vblank, CLOCK_MONOTONIC,
		      HRTIMER_MODE_ABS);

	dcp->ibootep = afk_init(dcp, DISP0_ENDPOINT, ibootep_ops);
	afk_start(dcp->ibootep);

	return 0;
}
