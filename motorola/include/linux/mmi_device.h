/* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MMI_DEVICE_H_INCLUDED
#define __MMI_DEVICE_H_INCLUDED

bool mmi_device_is_available(struct device_node *np);
bool mmi_check_dynamic_device_node(char *dev_name);

#endif		/* __MMI_DEVICE_H_INCLUDED */
