/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef CHOP_CHOP_H
#define CHOP_CHOP_H

#include <linux/ioctl.h>
//TN Begin modified by bingtai.zou/860558 20220823 EKFOGO4G-1550 BEGIN
int __init chop_chop_hub_init(void);
void __exit chop_chop_hub_exit(void);
//TN Begin modified by bingtai.zou/860558 20220823 EKFOGO4G-1550 END
#endif
