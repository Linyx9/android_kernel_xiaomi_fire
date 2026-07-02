/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef FLIP_H
#define FLIP_H

#include <linux/ioctl.h>
int __init flip_hub_init(void);
void __exit flip_hub_exit(void);
#endif
