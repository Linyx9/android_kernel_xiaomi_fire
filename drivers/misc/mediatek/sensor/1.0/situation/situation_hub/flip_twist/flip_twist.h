/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef FLIP_TWIST_H
#define FLIP_TWIST_H

#include <linux/ioctl.h>
int __init flip_twist_hub_init(void);
void __exit flip_twist_hub_exit(void);
#endif
