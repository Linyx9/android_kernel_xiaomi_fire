/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef TAP_TAP_H
#define TAP_TAP_H

#include <linux/ioctl.h>
int __init tap_tap_hub_init(void);
void __exit tap_tap_hub_exit(void);
#endif
