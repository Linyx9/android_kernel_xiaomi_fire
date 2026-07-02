/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 MediaTek Inc.
 * Author: Huijuan Xie <huijuan.xie@mediatek.com>
 */

#ifndef _OCP2131_I2C_H_
#define _OCP2131_I2C_H_

struct OCP2131_SETTING_TABLE {
	unsigned char cmd;
	unsigned char data;
};

int ocp2131_i2c_write_byte(unsigned char addr, unsigned char value);

#endif
