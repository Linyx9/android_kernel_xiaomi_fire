/*
 *  upm2388_switching.c
 *  Umpower UPM2388 Switching IC driver
 *
 *  Copyright (C) 2021 Umpower Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt)	"[upm2388]:%s: " fmt, __func__

#include "upm2388_switch.h"
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include "upm2388_algo.h"

static int upm2388_write_reg(struct i2c_client *client, int reg, u8 data)
{
	struct upm2388 *upm = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&upm->i2c_lock);
	ret = i2c_smbus_write_byte_data(client, reg, data);
	mutex_unlock(&upm->i2c_lock);
	if (ret < 0) {
		pr_info("[%s] : reg(0x%x), ret(%d)\n",
				limiter_type_name[upm->mtd.type], reg, ret);
	}
	return 0;
}

static int upm2388_read_reg(struct i2c_client *client, int reg, void *data)
{
	struct upm2388 *upm = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&upm->i2c_lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&upm->i2c_lock);
	if (ret < 0) {
		pr_info("[%s] : reg(0x%x), ret(%d)\n",
				limiter_type_name[upm->mtd.type], reg, ret);
		return ret;
	}
	ret &= 0xff;
	*(u8 *)data = (u8)ret;

	return 0;
}

static int upm2388_update_bits(struct i2c_client *client, u8 reg, u8 val, u8 mask)
{
	struct upm2388 *upm = i2c_get_clientdata(client);
	int ret = 0;
	u8 old_val = 0, new_val = 0;

	mutex_lock(&upm->i2c_lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret >= 0) {
		old_val = ret & 0xff;
		new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(client, reg, new_val);
	} else {
		pr_err("[%s] : reg(0x%x), ret(%d)\n",
				limiter_type_name[upm->mtd.type], reg, ret);
	}
	mutex_unlock(&upm->i2c_lock);
	return ret;
}

void upm2388_dump_regs(struct i2c_client *client)
{
	struct upm2388 *upm = i2c_get_clientdata(client);
	u8 data = 0;
	int i = 0;

	/* address 0x00 ~ 0x1f */
	for (i = 0x0; i <= 0x1F; i++) {
		upm2388_read_reg(upm->client, i, &data);
		pr_info("[%s]: REG[0x%X]0x%X\n", limiter_type_name[upm->mtd.type], i, data);
	}
}

static int upm2388_tsd_int_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en = %d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_INT1_MASK,
			en << UPM2388_TSD_IM_SHIFT, UPM2388_TSD_IM_MASK);
}

static int upm2388_fcc_2_trickle_int_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en = %d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_INT1_MASK,
			en << UPM2388_FCC_2_TRICKLE_BAT_IM_SHIFT, UPM2388_FCC_2_TRICKLE_BAT_IM_MASK);
}

static int upm2388_trickle_2_fcc_int_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en = %d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_INT1_MASK,
			en << UPM2388_TRICKLE_2_FCC_BAT_IM_SHIFT, UPM2388_TRICKLE_2_FCC_BAT_IM_MASK);
}

static int upm2388_trickle_2_pre_int_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en = %d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_INT1_MASK,
			en << UPM2388_TRICKLE_2_PRE_BAT_IM_SHIFT, UPM2388_TRICKLE_2_PRE_BAT_IM_MASK);
}

static int upm2388_pre_2_trickle_int_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en = %d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_INT1_MASK,
			en << UPM2388_PRE_2_TRICKLE_BAT_IM_SHIFT, UPM2388_PRE_2_TRICKLE_BAT_IM_MASK);
}

static int upm2388_restart_int_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en = %d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_INT2_MASK,
			en << UPM2388_RESTART_IM_SHIFT, UPM2388_RESTART_IM_MASK);
}

static int upm2388_eoc_int_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en = %d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_INT2_MASK,
			en << UPM2388_EOC_IM_SHIFT, UPM2388_EOC_IM_MASK);
}

static int upm2388_rr_req_done_int_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en = %d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_INT_MASK,
			en << UPM2388_RR_REQ_DONE_IM_SHIFT, UPM2388_RR_REQ_DONE_IM_MASK);
}

static int upm2388_co_req_first_done_int_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en = %d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_INT_MASK,
			en << UPM2388_CO_REQ_FIRST_DONE_IM_SHIFT, UPM2388_CO_REQ_FIRST_DONE_IM_MASK);
}

int upm2388_set_supllement_mode_en(struct upm2388 *upm, bool en)
{
	u8 val = 0;

	pr_info("en:%d\n", en);
	val = en;

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL3,
			val << UPM2388_SUPLLEMENT_MODE_SHIFT, UPM2388_SUPLLEMENT_MODE_MASK);
}

int upm2388_get_supllement_mode(struct upm2388 *upm, int *en)
{
	int ret = 0;
	int val = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_CORE_CTRL3, &val);
	if (ret < 0) {
		pr_err("read UPM2388_SW_CORE_CTRL3 failed, ret:%d\n", ret);
		return ret;
	}

	*en = (val & UPM2388_SUPLLEMENT_MODE_MASK) >> UPM2388_SUPLLEMENT_MODE_SHIFT;
	pr_info("supllement mode:0x%x, reg:0x%x\n", *en, UPM2388_SW_CORE_CTRL3);

	return ret;
}


static int __maybe_unused upm2388_get_sw_core_status(struct upm2388 *upm, u8 *status)
{
	u8 data = 0;
	int ret = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_CORE_STATUS1, &data);
	if (ret < 0) {
		pr_err("read UPM2388_SW_CORE_STATUS1 failed, ret:%d\n", ret);
		return ret;
	}

	*status = (data & UPM2388_SW_CORE_STATUS_MASK) >> UPM2388_SW_CORE_STATUS_SHIFT;

	return ret;
}

static int __maybe_unused upm2388_is_fast_chg(struct upm2388 *upm, bool *result)
{
	u8 data = 0;
	int ret = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_CORE_STATUS1, &data);
	if (ret < 0) {
		pr_err("read UPM2388_SW_CORE_STATUS1 failed, ret:%d\n", ret);
		return ret;
	}

	*result = (data & UPM2388_FAST_CHG_MASK) ? true : false;

	return ret;
}

static int __maybe_unused upm2388_is_trickle_chg(struct upm2388 *upm, bool *result)
{
	u8 data = 0;
	int ret = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_CORE_STATUS1, &data);
	if (ret < 0) {
		pr_err("read UPM2388_SW_CORE_STATUS1 failed, ret:%d\n", ret);
		return ret;
	}

	*result = (data & UPM2388_TRICKLE_CHG_MASK) ? true : false;

	return ret;
}

static int __maybe_unused upm2388_is_pre_chg(struct upm2388 *upm, bool *result)
{
	u8 data = 0;
	int ret = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_CORE_STATUS1, &data);
	if (ret < 0) {
		pr_err("read UPM2388_SW_CORE_STATUS1 failed, ret:%d\n", ret);
		return ret;
	}

	*result = (data & UPM2388_PRE_CHG_MASK) ? true : false;

	return ret;
}

static int upm2388_get_vchg_voltage(struct upm2388 *upm, int *uv)
{
	u8 val1 = 0, val2 = 0;
	int data = 0;
	int ret = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_VAL1_VCHG, &val1);
	if (ret < 0) {
		pr_err("read UPM2388_SW_VAL1_VCHG failed, ret:%d\n", ret);
		return ret;
	}

	ret = upm2388_read_reg(upm->client, UPM2388_SW_VAL2_VCHG, &val2);
	if (ret < 0) {
		pr_err("read UPM2388_SW_VAL2_VCHG failed, ret:%d\n", ret);
		return ret;
	}

	data = (val1 << 4) | (val2 >> 4);
	*uv = data * UPM2388_VCHG_LSB + UPM2388_VCHG_BASE + UPM2388_VCHG_OFFSET;

	return ret;
}

static int upm2388_get_vbat_voltage(struct upm2388 *upm, int *uv)
{
	u8 val1 = 0, val2 = 0;
	int data = 0;
	int ret = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_VAL1_VBAT, &val1);
	if (ret < 0) {
		pr_err("read UPM2388_SW_VAL1_VBAT failed, ret:%d\n", ret);
		return ret;
	}

	ret = upm2388_read_reg(upm->client, UPM2388_SW_VAL2_VBAT, &val2);
	if (ret < 0) {
		pr_err("read UPM2388_SW_VAL2_VBAT failed, ret:%d\n", ret);
		return ret;
	}

	data = (val1 << 4) | (val2 >> 4);
	*uv = data * UPM2388_VBAT_LSB + UPM2388_VBAT_BASE + UPM2388_VBAT_OFFSET;

	return ret;
}

static int upm2388_get_ichg_current(struct upm2388 *upm, int *ua)
{
	u8 val1 = 0, val2 = 0;
	int data = 0;
	int ret = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_VAL1_ICHG, &val1);
	if (ret < 0) {
		pr_err("read UPM2388_SW_VAL1_ICHG failed, ret:%d\n", ret);
		return ret;
	}

	ret = upm2388_read_reg(upm->client, UPM2388_SW_VAL2_ICHG, &val2);
	if (ret < 0) {
		pr_err("read UPM2388_SW_VAL2_ICHG failed, ret:%d\n", ret);
		return ret;
	}

	data = (val1 << 4) | (val2 >> 4);
	*ua = data * UPM2388_ICHG_LSB / UPM2388_ICHG_LSB_DIV +
			UPM2388_ICHG_BASE + UPM2388_ICHG_OFFSET;

	return ret;
}

static int upm2388_get_idischg_current(struct upm2388 *upm, int *ua)
{
	u8 val1 = 0, val2 = 0;
	int data = 0;
	int ret = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_VAL1_IDISCHG, &val1);
	if (ret < 0) {
		pr_err("read UPM2388_SW_VAL1_IDISCHG failed, ret:%d\n", ret);
		return ret;
	}

	ret = upm2388_read_reg(upm->client, UPM2388_SW_VAL2_IDISCHG, &val2);
	if (ret < 0) {
		pr_err("read UPM2388_SW_VAL2_IDISCHG failed, ret:%d\n", ret);
		return ret;
	}

	data = (val1 << 4) | (val2 >> 4);
	*ua = data * UPM2388_IDISCHG_LSB / UPM2388_IDISCHG_LSB_DIV +
			UPM2388_IDISCHG_BASE + UPM2388_IDISCHG_OFFSET;

	return ret;
}

static int upm2388_recharge_eval_start(struct upm2388 *upm)
{
	pr_info("[%s]: start\n", limiter_type_name[upm->mtd.type]);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL1,
			1 << UPM2388_RECHG_EVAL_START_SHIFT, UPM2388_RECHG_EVAL_START_MASK);
}

static int upm2388_eoc_eval_start(struct upm2388 *upm)
{
	pr_info("[%s]: start\n", limiter_type_name[upm->mtd.type]);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL1,
			1 << UPM2388_EOC_EVAL_START_SHIFT, UPM2388_EOC_EVAL_START_MASK);
}

static int upm2388_set_in_ok(struct upm2388 *upm, bool onoff)
{
	pr_info("[%s]: INOK = %d\n", limiter_type_name[upm->mtd.type], onoff);

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL3,
			onoff << UPM2388_IN_OK_SHIFT, UPM2388_IN_OK_MASK);
}

static int upm2388_set_supllement_mode(struct upm2388 *upm, bool onoff)
{
	int ret = 0;

	pr_info("[%s]: onoff:%d\n", limiter_type_name[upm->mtd.type], onoff);

	ret = upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL3,
		onoff << UPM2388_SUPLLEMENT_MODE_SHIFT, UPM2388_SUPLLEMENT_MODE_MASK);
	if (ret < 0) {
		pr_err("set UPM2388_SW_CORE_CTRL3 fail, ret:T%d\n", ret);
		return ret;
	}

	upm->supllement_mode = onoff;

	return ret;
}

static int upm2388_set_dischg_mode(struct upm2388 *upm, int mode)
{
	u8 val = 0;

	pr_info("[%s]: mode:%d\n", limiter_type_name[upm->mtd.type], mode);

	switch(mode) {
		case UPM2388_DIS_MODE_CURR_REGUL_ONLY:
		case UPM2388_DIS_MODE_NO_REGUL_FULL_ON:
			val = mode;
			break;

	default :
		pr_err("wrong input(%d)\n", mode);
		return -EINVAL;
	}

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL3,
			val << UPM2388_SW_CORE_DIS_MODE_REG_SHIFT, UPM2388_SW_CORE_DIS_MODE_REG_MASK);
}


static int __maybe_unused upm2388_get_dischg_mode(struct upm2388 *upm, int *mode)
{
	u8 val = 0;
	int ret = 0;
	int tmp = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_CORE_CTRL3, &val);
	if (ret < 0) {
		*mode = -1;
		pr_err("read UPM2388_SW_CORE_CTRL3 failed, ret:%d\n", ret);
		return ret;
	}
	tmp = (val & UPM2388_SW_CORE_DIS_MODE_REG_MASK) >> UPM2388_SW_CORE_DIS_MODE_REG_SHIFT;

	switch(tmp) {
		case UPM2388_DIS_MODE_CURR_REGUL_ONLY :
		case UPM2388_DIS_MODE_NO_REGUL_FULL_ON :
			*mode = tmp;
			break;

		default :
			*mode = -1;
			return -EINVAL;
	}

	return ret;
}

static int upm2388_set_chg_mode(struct upm2388 *upm, int mode)
{
	u8 val = 0;

	pr_info("[%s]: mode:%d\n", limiter_type_name[upm->mtd.type], mode);

	switch(mode) {
		case UPM2388_CHG_MODE_CURR_REGUL_ONLY:
		case UPM2388_CHG_MODE_NO_REGUL_FULL_ON:
			val = mode;
			break;

	default :
		pr_err("wrong input(%d)\n", mode);
		return -EINVAL;
	}

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL3,
			val << UPM2388_SW_CORE_CHG_MODE_REG_SHIFT, UPM2388_SW_CORE_CHG_MODE_REG_MASK);
}

static int __maybe_unused upm2388_get_chg_mode(struct upm2388 *upm, int *mode)
{
	u8 val = 0;
	int ret = 0;
	int tmp = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_CORE_CTRL3, &val);
	if (ret < 0) {
		*mode = -1;
		pr_err("read UPM2388_SW_CORE_CTRL3 failed, ret:%d\n", ret);
		return ret;
	}
	tmp = (val & UPM2388_SW_CORE_CHG_MODE_REG_MASK) >> UPM2388_SW_CORE_CHG_MODE_REG_SHIFT;

	switch(tmp) {
		case UPM2388_CHG_MODE_CURR_REGUL_ONLY :
		case UPM2388_CHG_MODE_NO_REGUL_FULL_ON :
			*mode = tmp;
			break;

		default :
			*mode = -1;
			return -EINVAL;
	}

	return ret;
}

int upm2388_set_fcc_chg_current_limit(struct upm2388 *upm, int ma)
{
	int val = 0;

	pr_info("[%s]: ma:%d\n", limiter_type_name[upm->mtd.type], ma);

	ma = ma * 10;
	if (ma > 83980) {
		val = 0x7f;
	} else if (ma > 4522) {
		val = (ma - 4522) / 646 + 0x04;
	} else if (ma > 646) {
		val = (ma - 646) / 969;
	} else {
		val = 0x0;
	}

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL4,
			val << UPM2388_FCC_CHG_CURRENT_LIMIT_SHIFT, UPM2388_FCC_CHG_CURRENT_LIMIT_MASK);
}

int upm2388_set_trickle_chg_current_limit(struct upm2388 *upm, int ma)
{
	int val = 0;

	pr_info("[%s]: ma:%d\n", limiter_type_name[upm->mtd.type], ma);

	ma = ma * 10;
	if (ma > 7100) {
		val = 0x08;
	} else if (ma > 4522) {
		val = (ma - 4522) / 646 + 0x04;
	} else if (ma > 646) {
		val = (ma - 646) / 969;
	} else {
		val = 0x0;
	}

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL5,
			val << UPM2388_TRICKLE_CHG_CURRENT_LIMIT_SHIFT, UPM2388_TRICKLE_CHG_CURRENT_LIMIT_MASK);
}

int upm2388_set_dischg_current_limit(struct upm2388 *upm, int ma)
{
	int val = 0;

	pr_info("[%s]: ma:%d\n", limiter_type_name[upm->mtd.type], ma);

	ma = ma * 10;
	if (ma > 83980) {
		val = 0x7f;
	} else if (ma > 4522) {
		val = (ma - 4522) / 646 + 0x04;
	} else if (ma > 646) {
		val = (ma - 646) / 969;
	} else {
		val = 0x0;
	}

	return upm2388_update_bits(upm->client, UPM2388_SW_CORE_CTRL6,
			val << UPM2388_DISCHG_CURRENT_LIMIT_SHIFT, UPM2388_DISCHG_CURRENT_LIMIT_MASK);
}

int upm2388_set_recharge_voltage(struct upm2388 *upm, int mv)
{
	int val = 0;

	pr_info("[%s]: mv:%d\n", limiter_type_name[upm->mtd.type], mv);

	if (mv < UPM2388_RECHG_VOLTAGE_MIN)
		mv = UPM2388_RECHG_VOLTAGE_MIN;
	if (mv > UPM2388_RECHG_VOLTAGE_MAX)
		mv = UPM2388_RECHG_VOLTAGE_MAX;

	val = (mv - UPM2388_RECHG_VOLTAGE_BASE) / UPM2388_RECHG_VOLTAGE_LSB;
	val = val + UPM2388_RECHG_VOLTAGE_OFFSET;

	return upm2388_update_bits(upm->client, UPM2388_SW_TOP_RECHG_CTRL1,
			val << UPM2388_RECHG_VOLTAGE_SHIFT, UPM2388_RECHG_VOLTAGE_MASK);
}

int upm2388_set_eoc_voltage(struct upm2388 *upm, int mv)
{
	int val = 0;

	pr_info("[%s]: mv:%d\n", limiter_type_name[upm->mtd.type], mv);

	if (mv < UPM2388_EOC_VOLTAGE_MIN)
		mv = UPM2388_EOC_VOLTAGE_MIN;
	if (mv > UPM2388_EOC_VOLTAGE_MAX)
		mv = UPM2388_EOC_VOLTAGE_MAX;

	val = (mv - UPM2388_EOC_VOLTAGE_BASE) / UPM2388_EOC_VOLTAGE_LSB;
	val = val + UPM2388_EOC_VOLTAGE_OFFSET;

	return upm2388_update_bits(upm->client, UPM2388_SW_TOP_EOC_CTRL1,
			val << UPM2388_EOC_VOLTAGE_SHIFT, UPM2388_EOC_VOLTAGE_MASK);
}

int upm2388_set_eoc_current(struct upm2388 *upm, int ma)
{
	int val = 0;

	pr_info("[%s]: ma:%d\n", limiter_type_name[upm->mtd.type], ma);

	ma = ma * 10;
	if (ma < UPM2388_EOC_CURRENT_MIN)
		ma = UPM2388_EOC_CURRENT_MIN;
	if (ma > UPM2388_EOC_CURRENT_MAX)
		ma = UPM2388_EOC_CURRENT_MAX;

	val = (ma - UPM2388_EOC_CURRENT_BASE) / UPM2388_EOC_CURRENT_LSB;

	return upm2388_update_bits(upm->client, UPM2388_SW_TOP_EOC_CTRL2,
			val << UPM2388_EOC_CURRENT_SHIFT, UPM2388_EOC_CURRENT_MASK);
}

static int upm2388_vchg_continuous_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en:%d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_ENABLE,
			en << UPM2388_VCHG_CONTINUOUS_SHIFT, UPM2388_VCHG_CONTINUOUS_MASK);
}

static int upm2388_vbat_continuous_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en:%d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_ENABLE,
			en << UPM2388_VBAT_CONTINUOUS_SHIFT, UPM2388_VBAT_CONTINUOUS_MASK);
}

static int upm2388_ichg_continuous_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en:%d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_ENABLE,
			en << UPM2388_ICHG_CONTINUOUS_SHIFT, UPM2388_ICHG_CONTINUOUS_MASK);
}

static int upm2388_idischg_continuous_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en:%d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_ENABLE,
			en << UPM2388_IDISCHG_CONTINUOUS_SHIFT, UPM2388_IDISCHG_CONTINUOUS_MASK);
}

static int __maybe_unused upm2388_vchg_one_shot_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en:%d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_ENABLE,
			en << UPM2388_VCHG_1SHOT_SHIFT, UPM2388_VCHG_1SHOT_MASK);
}

static int __maybe_unused upm2388_vbat_one_shot_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en:%d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_ENABLE,
			en << UPM2388_VBAT_1SHOT_SHIFT, UPM2388_VBAT_1SHOT_MASK);
}

static int __maybe_unused upm2388_ichg_one_shot_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en:%d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_ENABLE,
			en << UPM2388_ICHG_1SHOT_SHIFT, UPM2388_ICHG_1SHOT_MASK);
}

static int __maybe_unused upm2388_idischg_one_shot_en(struct upm2388 *upm, bool en)
{
	pr_info("[%s]: en:%d\n", limiter_type_name[upm->mtd.type], en);

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_ENABLE,
			en << UPM2388_IDISCHG_1SHOT_SHIFT, UPM2388_IDISCHG_1SHOT_MASK);
}

static int upm2388_set_vchg_int_hysteresis(struct upm2388 *upm, int mv)
{
	int val = 0;

	pr_info("[%s]: mv:%d\n", limiter_type_name[upm->mtd.type], mv);

	if (mv < UPM2388_HYST_LEV_VCHG_MIN)
		mv = UPM2388_HYST_LEV_VCHG_MIN;
	if (mv > UPM2388_HYST_LEV_VCHG_MAX)
		mv = UPM2388_HYST_LEV_VCHG_MAX;

	val = (mv - UPM2388_HYST_LEV_VCHG_BASE) / UPM2388_HYST_LEV_VCHG_LSB;

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_HYST_LEVEL1,
			val << UPM2388_HYST_LEV_VCHG_SHIFT, UPM2388_HYST_LEV_VCHG_MASK);
}

static int upm2388_set_vbat_int_hysteresis(struct upm2388 *upm, int mv)
{
	int val = 0;

	pr_info("[%s]: mv:%d\n", limiter_type_name[upm->mtd.type], mv);

	if (mv < UPM2388_HYST_LEV_VBAT_MIN)
		mv = UPM2388_HYST_LEV_VBAT_MIN;
	if (mv > UPM2388_HYST_LEV_VBAT_MAX)
		mv = UPM2388_HYST_LEV_VBAT_MAX;

	val = (mv - UPM2388_HYST_LEV_VBAT_BASE) / UPM2388_HYST_LEV_VBAT_LSB;

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_HYST_LEVEL1,
			val << UPM2388_HYST_LEV_VBAT_SHIFT, UPM2388_HYST_LEV_VBAT_MASK);
}

static int upm2388_set_ichg_int_hysteresis(struct upm2388 *upm, int ma)
{
	int val = 0;

	pr_info("[%s]: ma:%d\n", limiter_type_name[upm->mtd.type], ma);

	if (ma >= 3900) {
		val = 7;
	} else if (ma >= 2700) {
		val = 6;
	} else if (ma >= 1300) {
		val = 5;
	} else if (ma >= 650) {
		val = 4;
	} else if (ma >= 520) {
		val = 3;
	} else if (ma >= 390) {
		val = 2;
	} else if (ma >= 260) {
		val = 1;
	} else {
		val = 0;
	}

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_HYST_LEVEL2,
			val << UPM2388_HYST_LEV_ICHG_SHIFT, UPM2388_HYST_LEV_ICHG_MASK);
}

static int upm2388_set_idischg_int_hysteresis(struct upm2388 *upm, int ma)
{
	int val = 0;

	pr_info("[%s]: ma:%d\n", limiter_type_name[upm->mtd.type], ma);

	if (ma >= 3900) {
		val = 7;
	} else if (ma >= 2700) {
		val = 6;
	} else if (ma >= 1300) {
		val = 5;
	} else if (ma >= 650) {
		val = 4;
	} else if (ma >= 520) {
		val = 3;
	} else if (ma >= 390) {
		val = 2;
	} else if (ma >= 260) {
		val = 1;
	} else {
		val = 0;
	}

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_HYST_LEVEL2,
			val << UPM2388_HYST_LEV_IDISCHG_SHIFT, UPM2388_HYST_LEV_IDISCHG_MASK);
}

static int __maybe_unused upm2388_sel_volt_refresh(struct upm2388 *upm, int ms)
{
	int val = 0;

	pr_info("[%s]: ms:%d\n", limiter_type_name[upm->mtd.type], ms);

	if (ms >= 4000) {
		val = UPM2388_VOLT_REFRESH_4000MS;
	} else if (ms >= 2000) {
		val = UPM2388_VOLT_REFRESH_2000MS;
	} else if (ms >= 1000) {
		val = UPM2388_VOLT_REFRESH_1000MS;
	} else {
		val = UPM2388_VOLT_REFRESH_500MS;
	}

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_V_OPTION,
			val << UPM2388_SEL_VOLT_REFRESH_SHIFT, UPM2388_SEL_VOLT_REFRESH_MASK);
}

static int __maybe_unused upm2388_sel_curr_refresh(struct upm2388 *upm, int ms)
{
	int val = 0;

	pr_info("[%s]: ms:%d\n", limiter_type_name[upm->mtd.type], ms);

	if (ms >= 500) {
		val = UPM2388_CURR_REFRESH_500MS;
	} else if (ms >= 250) {
		val = UPM2388_CURR_REFRESH_250MS;
	} else if (ms >= 125) {
		val = UPM2388_CURR_REFRESH_125MS;
	} else {
		val = UPM2388_CURR_REFRESH_62MS5;
	}

	return upm2388_update_bits(upm->client, UPM2388_SW_PM_I_OPTION,
			val << UPM2388_SEL_CURR_REFRESH_SHIFT, UPM2388_SEL_CURR_REFRESH_MASK);
}

static int upm2388_get_es_no(struct upm2388 *upm, int *no)
{
	int ret = 0;
	int val = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_ID, &val);
	if (ret < 0) {
		pr_err("read UPM2388_SW_ID failed, ret:%d\n", ret);
		return ret;
	}

	*no = (val & UPM2388_ES_NO_MASK) >> UPM2388_ES_NO_SHIFT;
	pr_info("[%s]: no:0x%x, reg:0x%x\n", limiter_type_name[upm->mtd.type], *no, val);

	return ret;
}

static int upm2388_get_rev_no(struct upm2388 *upm, int *no)
{
	int ret = 0;
	int val = 0;

	ret = upm2388_read_reg(upm->client, UPM2388_SW_ID, &val);
	if (ret < 0) {
		pr_err("read UPM2388_SW_ID failed, ret:%d\n", ret);
		return ret;
	}

	*no = (val & UPM2388_REV_NO_MASK) >> UPM2388_REV_NO_SHIFT;
	pr_info("[%s]: no:0x%x\n", limiter_type_name[upm->mtd.type], *no);

	return ret;
}

static int upm2388_cm_tsd_en(struct upm2388 *upm, bool onoff)
{
	int ret = 0;

	pr_info("[%s]: tsd = %d\n", limiter_type_name[upm->mtd.type], onoff);

	ret = upm2388_update_bits(upm->client, UPM2388_SW_COMMON1,
			onoff << UPM2388_CM_TSD_EN_SHIFT, UPM2388_CM_TSD_EN_MASK);
	if (ret) {
		pr_err("set tsd en fail, ret:%d\n", ret);
	} else {
		upm->tsd = onoff;
	}

	return ret;
}

static int upm2388_detect_device(struct upm2388 *upm)
{
	int val = 0;
	int ret = 0;

	ret = upm2388_get_es_no(upm, &val);
	if (ret < 0) {
		pr_err("upm2388_get_es_no fail, ret:%d\n", ret);
		return ret;
	}

	if (val == UPM2388_ES_NO) {
		ret = 0;
	} else {
		ret = -ENODEV;
		pr_err("device is not upm2388\n");
	}

	return ret;
}

static irqreturn_t upm2388_irq_handler(int irq, void *irq_data)
{
	struct upm2388 *upm = irq_data;
	int eoc_flag = 0;
	int recharge_flag = 0;
	u8 val;

	pr_info("%s-ch%d: irq", limiter_type_name[upm->mtd.type], upm->mtd.channel);

	upm2388_read_reg(upm->client, UPM2388_SW_CORE_INT2, &val);
	recharge_flag = !!(val & UPM2388_RESTART_INT_MASK);
	eoc_flag = !! (val & UPM2388_EOC_INT_MASK);

	if (recharge_flag == 1) {
		upm2388_set_supllement_mode(upm, UPM2388_SUPLLEMENT_MODE_OFF);
		upm2388_eoc_eval_start(upm);
		pr_err("irq_recharge, exit supllment mode");//when recharge irq,exit supllement mode and set eoc int
	}
	if (eoc_flag == 1) {
		upm2388_set_supllement_mode(upm, UPM2388_SUPLLEMENT_MODE_ON);
		upm2388_recharge_eval_start(upm);
		pr_err("irq_eoc handler, enter supllemnet mode");//when eoc irq,enter supllement mode and set eoc int.
	}

	upm2388_dump_regs(upm->client);

	return IRQ_HANDLED;
}

static int upm2388_register_irq(struct upm2388 *upm)
{
	int ret = 0;

	upm->pdata->bat_int = of_get_named_gpio(upm->dev->of_node, "limiter,intr_gpio", 0);
	if (!gpio_is_valid(upm->pdata->bat_int)) {
		pr_err("fail to valid gpio : %d\n", upm->pdata->bat_int);
		return -EINVAL;
	}

	ret = gpio_request_one(upm->pdata->bat_int, GPIOF_DIR_IN,
		devm_kasprintf(upm->dev, GFP_KERNEL, "upm2388-%s-ch%d-irq-gpio",
			limiter_type_name[upm->mtd.type], upm->mtd.channel));
	if (ret) {
		pr_err("fail to request upm2388 irq\n");
		return EINVAL;
	}

	upm->irq =gpio_to_irq(upm->pdata->bat_int);
	if (upm->irq < 0) {
		pr_err("fail to gpio to irq\n");
		return EINVAL;
	}

	ret = devm_request_threaded_irq(&upm->client->dev, upm->irq, NULL,
		upm2388_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                devm_kasprintf(upm->dev, GFP_KERNEL, "upm2388-%s-ch%d-irq",
			limiter_type_name[upm->mtd.type], upm->mtd.channel), upm);

	enable_irq_wake(upm->irq);

	return ret;
}

static int upm2388_switching_parse_dt(struct upm2388 *upm, struct upm2388_platform_data *pdata)
{
	struct device_node *np = upm->dev->of_node;
	int ret = 0;

	pr_info("parsing start\n");

	if (np == NULL) {
		pr_err("np is NULL\n");
		return -ENODEV;
	}

	pdata->bat_enb = of_get_named_gpio(np, "limiter,en_gpio", 0);
	if (pdata->bat_enb < 0) {
		pr_err("get limiter,en_gpio fail, bat_enb:%d\n", pdata->bat_enb);
	} else {
		gpio_request(pdata->bat_enb,
			devm_kasprintf(upm->dev, GFP_KERNEL, "upm2388-%s-ch%d-en-gpio",
			limiter_type_name[upm->mtd.type], upm->mtd.channel));
		gpio_direction_output(pdata->bat_enb, 1);
		//gpio_set_value(pdata->bat_enb, 0);
	}

	ret = of_property_read_u32(np, "limiter,chg_current_limit",
				&pdata->chg_current_limit);
	if (ret < 0) {
		dev_err(upm->dev, "can't chg_current_limit\n");
		pdata->chg_current_limit = 600;
	}

	ret = of_property_read_u32(np, "limiter,dischg_current_limit",
				&pdata->dischg_current_limit);
	if (ret < 0) {
		dev_err(upm->dev, "can't dischg_current_limit\n");
		pdata->dischg_current_limit = 6500;
	}

	ret = of_property_read_u32(np, "limiter,recharge_volatge",
				&pdata->recharge_volatge);
	if (ret < 0) {
		dev_err(upm->dev, "can't recharge_volatge\n");
		pdata->recharge_volatge = 4250;
	}

	ret = of_property_read_u32(np, "limiter,float_voltage",
				&pdata->float_voltage);
	if (ret < 0) {
		pr_info("float voltage is empty\n");
		pdata->float_voltage = 4350; /* for interrupt setting, not used */
	}

	ret = of_property_read_u32(np, "limiter,eoc", &pdata->eoc);
	if (ret < 0) {
		pr_info("eoc is empty\n");
		pdata->eoc = 200; /* for interrupt setting, not used */
	}

	ret = of_property_read_u32(np, "limiter,hys_vchg",
				&pdata->hys_vchg);
	if (ret < 0) {
		pr_info("Hysteresis level is empty(vchg)\n");
		pdata->hys_vchg = 250; /* 250mV(default) */
	}

	ret = of_property_read_u32(np, "limiter,hys_vbat",
				&pdata->hys_vbat);
	if (ret < 0) {
		pr_info("Hysteresis level is empty(vbat)\n");
		pdata->hys_vbat = 250; /* 250mV(default) */
	}

	ret = of_property_read_u32(np, "limiter,hys_ichg",
				&pdata->hys_ichg);
	if (ret < 0) {
		pr_info("Hysteresis level is empty(ichg)\n");
		pdata->hys_ichg = 500; /* 500mA(default) */
	}

	ret = of_property_read_u32(np, "limiter,hys_idischg",
				&pdata->hys_idischg);
	if (ret < 0) {
		pr_info("Hysteresis level is empty(idischg)\n");
		pdata->hys_idischg = 500; /* 500mA(default) */
	}

	pdata->tsd_en = (of_find_property(np, "limiter,tsd-en", NULL))
					? true : false;

	return 0;
}

static void upm2388_init_regs(struct upm2388 *upm)
{
	pr_err("upm2388 switching initialize\n");

	upm2388_vchg_continuous_en(upm, true);
	upm2388_vbat_continuous_en(upm, true);
	upm2388_ichg_continuous_en(upm, true);
	upm2388_idischg_continuous_en(upm, true);

	upm2388_set_chg_mode(upm, UPM2388_CHG_MODE_CURR_REGUL_ONLY);
	upm2388_set_dischg_mode(upm, UPM2388_DIS_MODE_CURR_REGUL_ONLY);

	upm2388_set_dischg_current_limit(upm, upm->pdata->dischg_current_limit);
	upm2388_set_fcc_chg_current_limit(upm, upm->pdata->chg_current_limit);

	upm2388_set_eoc_current(upm, upm->pdata->eoc);
	upm2388_set_eoc_voltage(upm, upm->pdata->float_voltage);

	upm2388_set_recharge_voltage(upm, upm->pdata->recharge_volatge);
	upm2388_set_supllement_mode(upm, false);
	upm2388_eoc_eval_start(upm);

	upm2388_set_trickle_chg_current_limit(upm, 250);

	upm2388_set_vbat_int_hysteresis(upm, upm->pdata->hys_vbat);
	upm2388_set_vchg_int_hysteresis(upm, upm->pdata->hys_vchg);
	upm2388_set_ichg_int_hysteresis(upm, upm->pdata->hys_ichg);
	upm2388_set_idischg_int_hysteresis(upm, upm->pdata->hys_idischg);

	upm2388_tsd_int_en(upm, true);
	upm2388_fcc_2_trickle_int_en(upm, true);
	upm2388_trickle_2_fcc_int_en(upm, true);
	upm2388_trickle_2_pre_int_en(upm, true);
	upm2388_pre_2_trickle_int_en(upm, true);
	upm2388_restart_int_en(upm, false);
	upm2388_eoc_int_en(upm, false);
	upm2388_rr_req_done_int_en(upm, true);
	upm2388_co_req_first_done_int_en(upm, true);

	upm2388_set_chg_mode(upm, UPM2388_CHG_MODE_CURR_REGUL_ONLY);
	upm2388_set_dischg_mode(upm, UPM2388_CHG_MODE_CURR_REGUL_ONLY);

	upm2388_cm_tsd_en(upm, upm->pdata->tsd_en);

	upm2388_dump_regs(upm->client);
}

static struct match_table_data device_data[] = {
	{
		.index = 0,
		.type = LIMITER_UNI,
		.channel = 0,
	},
	{
		.index = 1,
		.type = LIMITER_MAIN,
		.channel = 0,
	},
	{
		.index = 2,
		.type = LIMITER_SUB,
		.channel = 0,
	},
};

static const struct of_device_id upm2388_switching_match_table[] = {
	{
		.compatible = "up,upm2388-switching-uni",
		.data = &device_data[0],
	},
	{
		.compatible = "up,upm2388-switching-main",
		.data = &device_data[1],
	},
	{
		.compatible = "up,upm2388-switching-sub",
		.data = &device_data[2],
	},
};

static ssize_t upm2388_show_registers(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct upm2388 *upm = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[200];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s-%s-ch%d:\n", "upm2388",
			limiter_type_name[upm->mtd.type], upm->mtd.channel);
	for (addr = 0x0; addr <= 0x1F; addr++) {
		ret = upm2388_read_reg(upm->client, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2x] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t upm2388_store_registers(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	struct upm2388 *upm = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg < 0x1F) {
		upm2388_write_reg(upm->client, (unsigned char) reg,
				   (unsigned char) val);
	}

	return count;
}

static DEVICE_ATTR(registers, S_IRUGO | S_IWUSR, upm2388_show_registers,
		   upm2388_store_registers);

static struct attribute *upm2388_attributes[] = {
	&dev_attr_registers.attr,
	NULL,
};

static const struct attribute_group upm2388_attr_group = {
	.attrs = upm2388_attributes,
};

static enum power_supply_property upm2388_charger_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_POWER_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_ENABLE_CONTROL,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
	POWER_SUPPLY_PROP_RECHARGE_VOLTAGE,
};

static int upm2388_limiter_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct upm2388 *upm = power_supply_get_drvdata(psy);
	int ret = 0;
	int data;
	val->intval = 0;

	pr_info("psp = %d\n", psp);
	switch (psp) {
		case POWER_SUPPLY_PROP_MANUFACTURER:
			val->strval = "UPM";
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			ret = upm2388_get_vbat_voltage(upm, &data);
			val->intval = data;
			break;
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
			ret = upm2388_get_vchg_voltage(upm, &data);
			val->intval = data;
			break;
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
			ret = upm2388_get_ichg_current(upm, &data);
			val->intval = data;
			break;
		case POWER_SUPPLY_PROP_CURRENT_NOW:
			//upm2388_dump_regs(upm->client);
			ret = upm2388_get_idischg_current(upm, &data);
			val->intval = data;
			break;
		default:
			pr_err("default prop, psp:%d", psp);
			ret = -EINVAL;
			break;
	}

	return ret;
}

static int upm2388_limiter_set_property(struct power_supply *psy,
		enum power_supply_property prop, const union power_supply_propval *val)
{
	struct upm2388 *upm = power_supply_get_drvdata(psy);
	int ret = 0;
	
	switch (prop) {
		case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
			ret = upm2388_set_fcc_chg_current_limit(upm, val->intval);
			break;
		case POWER_SUPPLY_PROP_INPUT_POWER_LIMIT:
			ret = upm2388_set_dischg_current_limit(upm, val->intval);
			break;
		case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD://when charging
			ret = upm2388_set_in_ok(upm, val->intval);
			if (val->intval == 0) {
				upm2388_set_supllement_mode(upm,UPM2388_SUPLLEMENT_MODE_OFF);
				pr_err("vbus out exit supllment mode");
			}
			break;
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
			ret = upm2388_set_eoc_voltage(upm, val->intval);
			break;
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
			ret = upm2388_set_eoc_current(upm, val->intval);
			break;
		case POWER_SUPPLY_PROP_CHARGE_ENABLE_CONTROL:
			ret = upm2388_set_supllement_mode_en(upm, val->intval);
			break;
		case POWER_SUPPLY_PROP_RECHARGE_VOLTAGE:
			ret = upm2388_set_recharge_voltage(upm, val->intval);
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static int upm2388_limiter_is_writeable(struct power_supply *psy,
                    enum power_supply_property prop)
{
	int ret = 0;

	switch (prop) {
	default:
		ret = 0;
		break;
	}

	return ret;
}

static int upm2388_psy_register(struct upm2388 *upm)
{
	struct power_supply_config psy_cfg = {};

	psy_cfg.of_node = upm->dev->of_node;
	psy_cfg.drv_data = upm;

	upm->limiter_psy_desc.name = devm_kasprintf(upm->dev, GFP_KERNEL,
		"upm2388_%s_ch%d", limiter_type_name[upm->mtd.type], upm->mtd.channel);
	upm->limiter_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	upm->limiter_psy_desc.properties = upm2388_charger_props;
	upm->limiter_psy_desc.num_properties = ARRAY_SIZE(upm2388_charger_props);
	upm->limiter_psy_desc.get_property = upm2388_limiter_get_property;
	upm->limiter_psy_desc.set_property = upm2388_limiter_set_property;
	upm->limiter_psy_desc.property_is_writeable = upm2388_limiter_is_writeable;
	upm->limiter_psy_desc.no_thermal = true;

	upm->limiter_psy = devm_power_supply_register(upm->dev, &upm->limiter_psy_desc, &psy_cfg);
	if (IS_ERR(upm->limiter_psy)) {
		pr_err("failed to register limiter_psy\n");
		return PTR_ERR(upm->limiter_psy);
	}

	pr_info("%s power supply register successfully\n", upm->limiter_psy_desc.name);

	return 0;
}

int upm2388_switch_enable(struct upm2388 *upm, bool enable)
{
	int rc = 0;

	if (upm->pdata->bat_enb) {
		pr_info("%s upm2388 switch\n", enable ? "enebale" : "disable");
		gpio_direction_output(upm->pdata->bat_enb, enable);
	} else {
		pr_err("can't control switch\n");
		rc = -1;
	}

	return rc;
}

static int upm2388_switching_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device_node *of_node = client->dev.of_node;
	struct upm2388 *upm;
	struct upm2388_platform_data *pdata = client->dev.platform_data;
	const struct of_device_id *match;
	int ret = 0;

	dev_info(&client->dev, "UPM2388 Switching Driver Loading\n");

	if (of_node) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}
	} else {
		pdata = client->dev.platform_data;
	}

	upm = kzalloc(sizeof(struct upm2388), GFP_KERNEL);
	if (upm == NULL) {
		dev_err(&client->dev, "Memory is not enough.\n");
		ret = -ENOMEM;
		goto err_limiter_nomem;
	}
	upm->dev = &client->dev;

	ret = i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK);
	if (!ret) {
		ret = i2c_get_functionality(client->adapter);
		dev_err(upm->dev, "I2C functionality is not supported.\n");
		ret = -ENOSYS;
		goto err_i2cfunc_not_support;
	}
	upm->client = client;
	upm->pdata = pdata;

	i2c_set_clientdata(client, upm);
	mutex_init(&upm->i2c_lock);

	match = of_match_node(upm2388_switching_match_table, of_node);
	if (match == NULL) {
        	pr_err("device tree match not found!\n");
        	goto err_no_dev;
	}
	upm->mtd.type = ((struct match_table_data *)(match->data))->type;
	upm->mtd.channel = ((struct match_table_data *)(match->data))->channel;

	ret = upm2388_detect_device(upm);
	if (ret < 0) {
		pr_err("No upm2388 device found!\n");
		goto err_no_dev;
	}

	ret = upm2388_switching_parse_dt(upm, pdata);
	if (ret < 0)
		goto err_parse_dt;

	upm->in_ok = false;
	upm->supllement_mode = false;
	upm->power_meter = false;
	upm->tsd = false;
	upm2388_get_es_no(upm, &upm->es_num);
	upm2388_get_rev_no(upm, &upm->rev_id);

	ret = upm2388_register_irq(upm);
	if (ret) {
		pr_err("register irq fail, ret:%d\n", ret);
	}

	upm2388_init_regs(upm);

	ret = upm2388_psy_register(upm);
	if (ret) {
		pr_err("register power supply fail, ret:%d\n", ret);
		goto err_register_psy;
	}

	ret = sysfs_create_group(&upm->dev->kobj, &upm2388_attr_group);
	if (ret)
		pr_err("failed to register sysfs. err: %d\n", ret);

#ifdef UPM2388_ALGO
	upm2388_algo_init(upm);
#endif

	dev_info(&client->dev, "UPM2388 Switching Driver Loaded\n");
	return 0;

err_register_psy:
err_parse_dt:
err_no_dev:
	mutex_destroy(&upm->i2c_lock);
err_i2cfunc_not_support:
	kfree(upm);
err_limiter_nomem:
	devm_kfree(&client->dev, pdata);

	return ret;
}

static void upm2388_switching_shutdown(struct i2c_client *client)
{
	pr_info("++\n");
}

static int upm2388_switching_remove(struct i2c_client *client)
{
	struct upm2388 *upm = i2c_get_clientdata(client);
	//power_supply_unregister(switching->psy_sw);
	mutex_destroy(&upm->i2c_lock);
#ifdef UPM2388_ALGO
	upm2388_algo_deinit(upm);
#endif
	return 0;
}

#if defined CONFIG_PM
static int upm2388_switching_suspend(struct device *dev)
{
#ifdef UPM2388_ALGO
	struct i2c_client *client = to_i2c_client(dev);
	struct upm2388 *upm = i2c_get_clientdata(client);
#endif
	int ret = 0;

	pr_info("enter\n");
#ifdef UPM2388_ALGO
	if (upm) {
		pr_info("get upm device and cancel works\n");
		cancel_delayed_work(&upm->switch_update_work);
		cancel_delayed_work(&upm->batt_status_update_work);
	} else {
		pr_err("failed to get upm device\n");
		ret = -ENODEV;
	}
#endif
	return ret;
}

static int upm2388_switching_resume(struct device *dev)
{
#ifdef UPM2388_ALGO
	struct i2c_client *client = to_i2c_client(dev);
	struct upm2388 *upm = i2c_get_clientdata(client);
#endif
	int ret = 0;

	pr_info("enter\n");
#ifdef UPM2388_ALGO
	if (upm) {
		pr_info("get upm device and schedule works\n");
		schedule_delayed_work(&upm->switch_update_work, msecs_to_jiffies(NORMAL_TEMP_UPDATE_MS));
		schedule_delayed_work(&upm->batt_status_update_work, msecs_to_jiffies(WAIT_FOR_CHECK_STATUS_MS));
	} else {
		pr_err("failed to get upm device\n");
		ret = -ENODEV;
	}
#endif
	return ret;
}

#else

#define upm2388_switching_suspend NULL
#define upm2388_switching_resume NULL
#endif

static SIMPLE_DEV_PM_OPS(upm2388_switching_pm_ops, upm2388_switching_suspend,
		upm2388_switching_resume);

static struct i2c_driver upm2388_switching_driver = {
	.driver = {
		.name = "upm2388-switching",
		.owner = THIS_MODULE,
		.pm = &upm2388_switching_pm_ops,
		.of_match_table = upm2388_switching_match_table,
	},
	.probe  = upm2388_switching_probe,
	.remove = upm2388_switching_remove,
	.shutdown   = upm2388_switching_shutdown,
};

static int __init upm2388_switching_init(void)
{
	pr_info("UPM2388 Switching Init\n");
	return i2c_add_driver(&upm2388_switching_driver);
}

static void __exit upm2388_switching_exit(void)
{
	i2c_del_driver(&upm2388_switching_driver);
}
module_init(upm2388_switching_init);
module_exit(upm2388_switching_exit);

MODULE_DESCRIPTION("UP UPM2388 Switching IC Driver");
MODULE_AUTHOR("Unisemipower Electronics");
MODULE_LICENSE("GPL");
