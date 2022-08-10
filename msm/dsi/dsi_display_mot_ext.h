/*
 * Copyright (c) 2015-2019, The Linux Foundation.All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DSI_DISPLAY_MOT_EXT_H_
#define _DSI_DISPLAY_MOT_EXT_H_

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/of_device.h>
#include <linux/firmware.h>
#include <drm/drm_crtc.h>

#include "msm_drv.h"
#include "dsi_defs.h"
#include "dsi_ctrl.h"
#include "dsi_phy.h"
#include "dsi_panel.h"
#include "sde_connector.h"
#include "sde_motUtil.h"
#include "dsi_display.h"
#include <linux/alarmtimer.h>

#define LCD_PARA_LINE_LEN 2*1024
#define LCD_PARA_TEM_BUF_LEN 64

/**
 * dsi_display_ext_init() - Moto extend code init
 * @display:          Handle to display.
 *
 * Add sysfs node for Moto extend feature.
 *
 * Return: error code.
 */
int dsi_display_ext_init(struct dsi_display *display);
bool dsi_panel_mot_parse_timing_from_file(struct dsi_display *display, int index);
int dsi_display_read_8s(struct dsi_display *display);

#endif /* _DSI_DISPLAY_MOT_EXT_H_ */
