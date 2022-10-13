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
int mot_atoi(const char *src);

// BEGIN Motorola zhanggb, add refreshrate group, IKSWT-18219
/**
 * enum dsi_te_mode - dsi te source
 * @RRGSFlag_Unrestricted:    when no qcom,mdss-dsi-panel-framerate_group set in timming device tree
 * @RRGSFlag_All_No_Duplicated:      If you want this mode in the refresh rate list when brightness high enough
 * @RRGSFlag_120HzBased:      120Hz based refresh rates, used when limit brightness zone
 * @RRGSFlag_90HzBased:        90Hz based refresh rates, used when limit brightness zone
 * @RRGSFlag_Special_Idle_1Hz:   1Hz LTPO refreshrate, which should report 90/120 fps to uplayer, but actually 1hz in panel hardware
 * @RRGSFlag_Special_Idle_10Hz: 10Hz LTPO refreshrate, which should report 90/120 fps to uplayer, but actually 10hz in panel hardware
 */
enum dsi_display_mode_group_flag {
        RRGSFlag_Unrestricted = 0,
        RRGSFlag_All_No_Duplicated =  1 << 0, //The default refreshrates without duplicated except special idle ones
        RRGSFlag_120HzBased =  1 << 1,    // Only can switch between 120hz based refreshrates
        RRGSFlag_90HzBased =  1 << 2,     // Only can switch between 90hz based refreshrates
        RRGSFlag_Special_Idle_1Hz =  1 << 3,     // Only can selected when idle and brightness high enough
        RRGSFlag_Special_Idle_10Hz =  1 << 4,    // Only can selected when idle and brightness high enough
};

static inline u32 dsi_display_mode_actual_rr(struct dsi_mode_info *timing)
{
	if (timing->refresh_rate_group_flag & RRGSFlag_Special_Idle_1Hz)
		return 1;
	else if (timing->refresh_rate_group_flag & RRGSFlag_Special_Idle_10Hz)
		return 10;
	else
		return timing->refresh_rate;
}
// END Motorola zhanggb, IKSWT-18219

#endif /* _DSI_DISPLAY_MOT_EXT_H_ */
