/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef SIG_MOVE_H
#define SIG_MOVE_H

#include <linux/ioctl.h>

//TN Begin modified by bingtai.zou/860558 20220823 EKFOGO4G-1550 BEGIN
int __init sig_move_hub_init(void);
void __exit sig_move_hub_exit(void);
//TN Begin modified by bingtai.zou/860558 20220823 EKFOGO4G-1550 END
#endif
