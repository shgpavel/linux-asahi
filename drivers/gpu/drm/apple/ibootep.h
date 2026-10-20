// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright The Asahi Linux Contributors */

#ifndef __APPLE_IBOOTEP_H__
#define __APPLE_IBOOTEP_H__

#include <linux/types.h>

#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>

struct apple_dcp;

struct iboot_timing_mode {
	__le32 valid;
	__le32 width;
	__le32 height;
	__le32 fps;
	u8 pad[8];
} __packed;

struct iboot_color_mode {
	__le32 valid;
	__le32 colorimetry;
	__le32 eotf;
	__le32 encoding;
	__le32 bpp;
	u8 pad[4];
} __packed;

int iboot_start(struct apple_dcp *dcp);
void iboot_hpd_changed(struct apple_dcp *dcp);
void iboot_unplugged(struct apple_dcp *dcp);
int iboot_modeset(struct apple_dcp *dcp, struct drm_crtc_state *crtc_state);
void iboot_flush(struct apple_dcp *dcp, struct drm_crtc *crtc,
		 struct drm_atomic_commit *commit);
void iboot_poweron(struct apple_dcp *dcp);
void iboot_poweroff(struct apple_dcp *dcp);
void iboot_release_fbs(struct apple_dcp *dcp);

#endif
