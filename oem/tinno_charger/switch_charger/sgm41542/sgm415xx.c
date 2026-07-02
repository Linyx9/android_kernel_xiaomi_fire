// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2021 MediaTek Inc.
 */
#define pr_fmt(fmt) "[sgm415xx] %s: " fmt, __func__

#include <linux/types.h>
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#endif
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/iio/consumer.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
#include <linux/phy/phy.h>
#define PHY_MODE_BC11_SET 1
#define PHY_MODE_BC11_CLR 2
#endif

#include "sgm415xx.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include "charger_class.h"
#include "mtk_charger.h"
#include "mtk_musb.h"
#else
#include <mt-plat/upmu_common.h>
#include <mt-plat/charger_class.h>
#include <mt-plat/mtk_charger.h>
#include <mt-plat/charger_type.h>
#include "mediatek/charger/mtk_charger_intf.h"
#endif /* LINUX_VERSION_CODE */

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include <dev_info.h>
#endif /* CONFIG_OEM_DEVINFO */

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#include <tinno_charger.h>
#endif /* CONFIG_OEM_TINNO_CHARGER */

/**********************************************************
 *
 *   [I2C Slave Setting]
 *
 *********************************************************/

#define SGM4154x_REG_NUM	(0xF)
#define SINGLE_DUMP_LEN		19
#define TOTAL_DUMP_LEN		(SINGLE_DUMP_LEN * (SGM4154x_REG_NUM))

#define R_VBUS_CHARGER_1   330
#define R_VBUS_CHARGER_2   39

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static struct proc_dir_entry *entry;
#endif

static bool dump_reg_enable;
static bool allow_set_dp_dm_vol = false;

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
extern bool turbo_charger_active;
extern bool ffc_batt_full;
#endif

enum vindpm_track {
	SGM4154x_TRACK_DIS,
	SGM4154x_TRACK_200,
	SGM4154x_TRACK_250,
	SGM4154x_TRACK_300,
};

/* SGM4154x REG06 BOOST_LIM[5:4], uV */
static const unsigned int BOOST_VOLT_LIMIT[] = {
	4850000, 5000000, 5150000, 5300000
};

/* SGM4154x REG02 BOOST_LIM[7:7], uA */
#if (defined(__SGM41542_CHIP_ID__) || defined(__SGM41541_CHIP_ID__)|| defined(__SGM41543_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
static const unsigned int BOOST_CURRENT_LIMIT[] = {
	1200000, 2000000
};
#else
static const unsigned int BOOST_CURRENT_LIMIT[] = {
	500000, 1200000
};
#endif

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))

static const unsigned int IPRECHG_CURRENT_STABLE[] = {
	5000, 10000, 15000, 20000, 30000, 40000, 50000, 60000,
	80000, 100000, 120000, 140000, 160000, 180000, 200000, 240000
};

static const unsigned int ITERM_CURRENT_STABLE[] = {
	5000, 10000, 15000, 20000, 30000, 40000, 50000, 60000,
	80000, 100000, 120000, 140000, 160000, 180000, 200000, 240000
};
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static enum power_supply_usb_type sgm4154x_usb_type[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
};
#endif

static const struct charger_properties sgm4154x_chg_props = {
	.alias_name = SGM4154x_NAME,
};

/**********************************************************
 *
 *   [Global Variable]
 *
 *********************************************************/
static struct power_supply_desc sgm4154x_power_supply_desc;
static struct charger_device *s_chg_dev_otg;

/**********************************************************
 *
 *   [I2C Function For Read/Write sgm4154x]
 *
 *********************************************************/
static int __sgm4154x_read_byte(struct sgm4154x_device *sgm, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(sgm->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8)ret;

	return 0;
}

static int __sgm4154x_write_byte(struct sgm4154x_device *sgm, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(sgm->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
			val, reg, ret);
		return ret;
	}
	return 0;
}

static int sgm4154x_read_reg(struct sgm4154x_device *sgm, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_read_byte(sgm, reg, data);
	mutex_unlock(&sgm->i2c_rw_lock);

	return ret;
}

__maybe_unused static int sgm4154x_write_reg(struct sgm4154x_device *sgm, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_write_byte(sgm, reg, val);
	mutex_unlock(&sgm->i2c_rw_lock);

	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

	return ret;
}

static int sgm4154x_update_bits(struct sgm4154x_device *sgm, u8 reg,
	u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&sgm->i2c_rw_lock);
	ret = __sgm4154x_read_byte(sgm, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __sgm4154x_write_byte(sgm, reg, tmp);
	if (ret)
		pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&sgm->i2c_rw_lock);
	return ret;
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/

static int sgm4154x_set_watchdog_timer(struct sgm4154x_device *sgm, int time)
{
	int ret;
	u8 reg_val;

	if (time == 0)
		reg_val = SGM4154x_WDT_TIMER_DISABLE;
	else if (time == 40)
		reg_val = SGM4154x_WDT_TIMER_40S;
	else if (time == 80)
		reg_val = SGM4154x_WDT_TIMER_80S;
	else
		reg_val = SGM4154x_WDT_TIMER_160S;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
			SGM4154x_WDT_TIMER_MASK, reg_val);

	return ret;
}

static int sgm4154x_set_vindpm_track(struct sgm4154x_device *sgm, enum vindpm_track track)
{
	int ret;
	pr_info("start vindpm track\n");
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_7,
			SGM4154x_VINDPM_TRACK, track);

	return ret;
}

static int sgm4154x_set_tmr2x(struct sgm4154x_device *sgm, bool enable)
{
	int ret;
	int reg_val = enable ? 1 : 0;
	pr_info("start set tmr2x\n");

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_7,
			SGM4154x_SAFETY_TIMER_RM2X, reg_val);

	return ret;
}

static int sgm4154x_set_dpm_mask(struct sgm4154x_device *sgm)
{
	int ret;
	pr_info("start dpm mask\n");
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_a,
			SGM4154x_DPM_MASK, SGM4154x_DPM_MASK);

	return ret;
}

__maybe_unused static int sgm4154x_get_term_curr(struct sgm4154x_device *sgm)
{
	int ret;
	u8 reg_val;
	int curr;
	int offset = SGM4154x_TERMCHRG_I_MIN_uA;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_3, &reg_val);
	if (ret)
		return ret;

	reg_val &= SGM4154x_TERMCHRG_CUR_MASK;
	curr = reg_val * SGM4154x_TERMCHRG_CURRENT_STEP_uA + offset;
	return curr;
}

__maybe_unused static int sgm4154x_get_prechrg_curr(struct sgm4154x_device *sgm)
{
	int ret;
	u8 reg_val;
	int curr;
	int offset = SGM4154x_PRECHRG_I_MIN_uA;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_3, &reg_val);
	if (ret)
		return ret;

	reg_val = (reg_val & SGM4154x_PRECHRG_CUR_MASK) >> 4;
	curr = reg_val * SGM4154x_PRECHRG_CURRENT_STEP_uA + offset;

	return curr;
}

static int sgm4154x_set_chg_term(struct sgm4154x_device *sgm, bool en)
{
	int reg_val = -1;

	reg_val = en <<  7;
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
					SGM4154x_TERM_EN, reg_val);
}

static int sgm4154x_enable_terminate(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);

	ret = sgm4154x_set_chg_term(sgm, en);
	if (ret < 0)
		pr_err("failed ret(%d)\n", ret);

	return ret;
}

static int sgm4154x_set_term_curr(struct charger_device *chg_dev, u32 uA)
{
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	for (reg_val = 1; reg_val < 16 && uA >= ITERM_CURRENT_STABLE[reg_val]; reg_val++)
		;
	reg_val--;
#else
	if (uA < SGM4154x_TERMCHRG_I_MIN_uA)
		uA = SGM4154x_TERMCHRG_I_MIN_uA;
	else if (uA > SGM4154x_TERMCHRG_I_MAX_uA)
		uA = SGM4154x_TERMCHRG_I_MAX_uA;

	reg_val = (uA - SGM4154x_TERMCHRG_I_MIN_uA) / SGM4154x_TERMCHRG_CURRENT_STEP_uA;
#endif
	pr_info("iterm curr = %d uA\n", uA);
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_3,
			SGM4154x_TERMCHRG_CUR_MASK, reg_val);
}

static int sgm4154x_set_prechrg_curr(struct sgm4154x_device *sgm, int uA)
{
	u8 reg_val;

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	for(reg_val = 1; reg_val < 16 && uA >= IPRECHG_CURRENT_STABLE[reg_val]; reg_val++)
		;
	reg_val--;
#else
	if (uA < SGM4154x_PRECHRG_I_MIN_uA)
		uA = SGM4154x_PRECHRG_I_MIN_uA;
	else if (uA > SGM4154x_PRECHRG_I_MAX_uA)
		uA = SGM4154x_PRECHRG_I_MAX_uA;

	reg_val = (uA - SGM4154x_PRECHRG_I_MIN_uA) / SGM4154x_PRECHRG_CURRENT_STEP_uA;
#endif
	reg_val = reg_val << 4;
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_3,
			SGM4154x_PRECHRG_CUR_MASK, reg_val);
}

static int sgm4154x_get_vbus_voltage(struct sgm4154x_device *sgm, int *vbus_volt)
{
	int ret = 0;
	int value = 0;

	ret = iio_read_channel_processed(sgm->vbus, &value);
	if (ret < 0) {
		pr_err("get vbus voltage failed");
		return -EINVAL;
	}

	*vbus_volt = value + R_VBUS_CHARGER_1 * value / R_VBUS_CHARGER_2;
	pr_info("vbus voltage: %d", *vbus_volt);

	return ret;
}

static int sgm4154x_get_ichg_curr(struct charger_device *chg_dev, u32 *uA)
{
	int ret;
	u8 ichg;
	u32 curr;

	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_2, &ichg);
	if (ret)
		return ret;

	ichg &= SGM4154x_ICHRG_I_MASK;
#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	if (ichg <= 0x8)
		curr = ichg * 5000;
	else if (ichg <= 0xF)
		curr = 40000 + (ichg - 0x8) * 10000;
	else if (ichg <= 0x17)
		curr = 110000 + (ichg - 0xF) * 20000;
	else if (ichg <= 0x20)
		curr = 270000 + (ichg - 0x17) * 30000;
	else if (ichg <= 0x30)
		curr = 540000 + (ichg - 0x20) * 60000;
	else if (ichg <= 0x3C)
		curr = 1500000 + (ichg - 0x30) * 120000;
	else
		curr = 3000000;
#else
	curr = ichg * SGM4154x_ICHRG_I_STEP_uA;
#endif
	*uA = curr;

	return 0;
}

static int sgm4154x_get_minichg_curr(struct charger_device *chg_dev, u32 *uA)
{
	*uA = SGM4154x_ICHRG_I_MIN_uA;
	return 0;
}

static int sgm4154x_set_ichrg_curr(struct charger_device *chg_dev, unsigned int uA)
{
	int ret;
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_info("%d uA\n", uA);

	if (uA < SGM4154x_ICHRG_I_MIN_uA)
		uA = SGM4154x_ICHRG_I_MIN_uA;
	else if ( uA > sgm->init_data.max_ichg)
		uA = sgm->init_data.max_ichg;
#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	if (uA <= 40000)
		reg_val = uA / 5000;
	else if (uA <= 110000)
		reg_val = 0x08 + (uA -40000) / 10000;
	else if (uA <= 270000)
		reg_val = 0x0F + (uA -110000) / 20000;
	else if (uA <= 540000)
		reg_val = 0x17 + (uA -270000) / 30000;
	else if (uA <= 1500000)
		reg_val = 0x20 + (uA -540000) / 60000;
	else if (uA <= 2940000)
		reg_val = 0x30 + (uA -1500000) / 120000;
	else
		reg_val = 0x3d;
#else
	reg_val = uA / SGM4154x_ICHRG_I_STEP_uA;
#endif
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_2,
			SGM4154x_ICHRG_I_MASK, reg_val);

	return ret;
}

static int sgm4154x_set_chrg_volt(struct charger_device *chg_dev, u32 chrg_volt)
{
	int ret;
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	if (chrg_volt < SGM4154x_VREG_V_MIN_uV)
		chrg_volt = SGM4154x_VREG_V_MIN_uV;
	else if (chrg_volt > sgm->init_data.max_vreg)
		chrg_volt = sgm->init_data.max_vreg;

	reg_val = (chrg_volt - SGM4154x_VREG_V_MIN_uV) / SGM4154x_VREG_V_STEP_uV;
	reg_val = reg_val << 3;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_4,
			SGM4154x_VREG_V_MASK, reg_val);

	return ret;
}

static int sgm4154x_get_chrg_volt(struct charger_device *chg_dev,unsigned int *volt)
{
	int ret;
	u8 vreg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_4, &vreg_val);
	if (ret)
		return ret;

	vreg_val = (vreg_val & SGM4154x_VREG_V_MASK) >> 3;

	if (15 == vreg_val)
		*volt = 4352000; //default
	else if (vreg_val < 25)
		*volt = vreg_val * SGM4154x_VREG_V_STEP_uV + SGM4154x_VREG_V_MIN_uV;

	return 0;
}

static int sgm4154x_get_vindpm_offset_os(struct sgm4154x_device *sgm)
{
	int ret;
	u8 reg_val;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_f, &reg_val);
	if (ret)
		return ret;

	reg_val = reg_val & SGM4154x_VINDPM_OS_MASK;

	return reg_val;
}

static int sgm4154x_set_vindpm_offset_os(struct sgm4154x_device *sgm,u8 offset_os)
{
	int ret;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_f,
			SGM4154x_VINDPM_OS_MASK, offset_os);

	if (ret) {
		pr_err("fail\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_set_input_volt_lim(struct charger_device *chg_dev, unsigned int vindpm)
{
	int ret;
	unsigned int offset;
	u8 reg_val;
	u8 os_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	if (vindpm < SGM4154x_VINDPM_V_MIN_uV ||
		vindpm > SGM4154x_VINDPM_V_MAX_uV)
 		return -EINVAL;

	if (vindpm < 5900000) {
		os_val = 0;
		offset = 3900000;
	} else if (vindpm >= 5900000 && vindpm < 7500000) {
		os_val = 1;
		offset = 5900000; //uv
	} else if (vindpm >= 7500000 && vindpm < 10500000) {
		os_val = 2;
		offset = 7500000; //uv
	} else {
		os_val = 3;
		offset = 10500000; //uv
	}

	sgm4154x_set_vindpm_offset_os(sgm,os_val);
	reg_val = (vindpm - offset) / SGM4154x_VINDPM_STEP_uV;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_6,
			SGM4154x_VINDPM_V_MASK, reg_val);

	return ret;
}

static int sgm4154x_get_input_volt_lim(struct charger_device *chg_dev, u32 *uV)
{
	int ret;
	int offset;
	u8 vlim;
	int temp;

	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_6, &vlim);
	if (ret)
		return ret;

	temp = sgm4154x_get_vindpm_offset_os(sgm);
	if (0 == temp)
		offset = 3900000; //uv
	else if (1 == temp)
		offset = 5900000;
	else if (2 == temp)
		offset = 7500000;
	else if (3 == temp)
		offset = 10500000;
	else
		return temp;

	*uV = offset + (vlim & 0x0F) * SGM4154x_VINDPM_STEP_uV;

	return 0;
}

__maybe_unused static int sgm4154x_get_input_minvolt_lim(struct charger_device *chg_dev, u32 *uV)
{
	*uV = SGM4154x_VINDPM_V_MIN_uV;

	return 0;
}

static int sgm4154x_set_input_curr_lim(struct charger_device *chg_dev, unsigned int iindpm)
{
	int ret;
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_info("%d uA\n", iindpm);

	if (iindpm < SGM4154x_IINDPM_I_MIN_uA ||
		iindpm > SGM4154x_IINDPM_I_MAX_uA)
		return -EINVAL;

#if (defined(__SGM41513_CHIP_ID__) || defined(__SGM41513A_CHIP_ID__) || defined(__SGM41513D_CHIP_ID__))
	reg_val = (iindpm - SGM4154x_IINDPM_I_MIN_uA) / SGM4154x_IINDPM_STEP_uA;
#else
	if (iindpm >= SGM4154x_IINDPM_I_MIN_uA && iindpm <= 3100000) //default
		reg_val = (iindpm - SGM4154x_IINDPM_I_MIN_uA) / SGM4154x_IINDPM_STEP_uA;
	else if (iindpm > 3100000 && iindpm < SGM4154x_IINDPM_I_MAX_uA)
		reg_val = 0x1E;
	else
		reg_val = SGM4154x_IINDPM_I_MASK;
#endif
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_0,
			SGM4154x_IINDPM_I_MASK, reg_val);

	return ret;
}

static int sgm4154x_get_input_curr_lim(struct charger_device *chg_dev,unsigned int *ilim)
{
	int ret;
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_0, &reg_val);
	if (ret)
		return ret;

	if (SGM4154x_IINDPM_I_MASK == (reg_val & SGM4154x_IINDPM_I_MASK))
		*ilim =  SGM4154x_IINDPM_I_MAX_uA;
	else
		*ilim = (reg_val & SGM4154x_IINDPM_I_MASK) * SGM4154x_IINDPM_STEP_uA + SGM4154x_IINDPM_I_MIN_uA;

	return 0;
}

static int sgm4154x_get_input_mincurr_lim(struct charger_device *chg_dev,u32 *ilim)
{
	*ilim = SGM4154x_IINDPM_I_MIN_uA;

	return 0;
}

static int sgm4154x_get_state(struct sgm4154x_device *sgm, struct sgm4154x_state *state)
{
	u8 chrg_stat;
	u8 fault;
	u8 det_done;
	u8 chrg_param_0, chrg_param_1, chrg_param_2;
	int ret;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_STAT, &chrg_stat);
	if (ret) {
		ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_STAT, &chrg_stat);
		if (ret) {
			pr_err("read SGM4154x_CHRG_STAT fail\n");
			return ret;
		}
	}

	state->chrg_type = chrg_stat & SGM4154x_VBUS_STAT_MASK;
	state->chrg_stat = chrg_stat & SGM4154x_CHG_STAT_MASK;
	state->online = !!(chrg_stat & SGM4154x_PG_STAT);
	state->therm_stat = !!(chrg_stat & SGM4154x_THERM_STAT);
	state->vsys_stat = !!(chrg_stat & SGM4154x_VSYS_STAT);

	pr_info("chrg_type:0x%x, chrg_stat:0x%x, online:%d\n",
		state->chrg_type, state->chrg_stat, state->online);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_FAULT, &fault);
	if (ret) {
		pr_err("read SGM4154x_CHRG_FAULT fail\n");
		return ret;
	}

	state->chrg_fault = fault;
	state->ntc_fault = fault & SGM4154x_TEMP_MASK;
	state->health = state->ntc_fault;
	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_0, &chrg_param_0);
	if (ret) {
		pr_err("read SGM4154x_CHRG_CTRL_0 fail\n");
		return ret;
	}
	state->hiz_en = !!(chrg_param_0 & SGM4154x_HIZ_EN);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_5, &chrg_param_1);
	if (ret) {
		pr_err("read SGM4154x_CHRG_CTRL_5 fail\n");
		return ret;
	}
	state->term_en = !!(chrg_param_1 & SGM4154x_TERM_EN);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_a, &chrg_param_2);
	if (ret) {
		pr_err("read SGM4154x_CHRG_CTRL_a fail\n");
		return ret;
	}
	state->vbus_gd = !!(chrg_param_2 & SGM4154x_VBUS_GOOD);

	ret = sgm4154x_read_reg(sgm, SGM4154x_INPUT_DET, &det_done);
	if (ret) {
		pr_err("read SGM4154x_INPUT_DET fail\n");
		return ret;
	}
	state->input_det_done = !!(det_done & SGM4154x_INPUT_DET_DONE_MASK);

	return 0;
}

static int sgm4154x_get_charge_stat(struct sgm4154x_device *sgm)
{
	u8 chrg_stat;
	int ret;
	int status = POWER_SUPPLY_STATUS_UNKNOWN;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_STAT, &chrg_stat);
	if (ret) {
		ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_STAT, &chrg_stat);
		if (ret) {
			pr_err("read SGM4154x_CHRG_STAT fail\n");
			return status;
		}
	}

	mutex_lock(&sgm->lock);
	sgm->state.chrg_type = chrg_stat & SGM4154x_VBUS_STAT_MASK;
	sgm->state.chrg_stat = chrg_stat & SGM4154x_CHG_STAT_MASK;
	mutex_unlock(&sgm->lock);

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	pr_info("chrg_type:0x%x, chrg_stat:0x%x, turbo_charger_active:%d , ffc_batt_full:%d\n",
		sgm->state.chrg_type, sgm->state.chrg_stat, turbo_charger_active, ffc_batt_full);
#else
	pr_info("chrg_type:0x%x, chrg_stat:0x%x\n",
		sgm->state.chrg_type, sgm->state.chrg_stat);
#endif

	if (!sgm->state.chrg_type || sgm->state.chrg_type == SGM4154x_OTG_MODE) {
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else {
		switch (sgm->state.chrg_stat) {
		case SGM4154x_NOT_CHRGING:
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		case SGM4154x_PRECHRG:
		case SGM4154x_FAST_CHRG:
			status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case SGM4154x_TERM_CHRG:
			status = POWER_SUPPLY_STATUS_FULL;
			break;
		}
	}

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	if ((turbo_charger_active == true) && (ffc_batt_full == true))
		status = POWER_SUPPLY_STATUS_FULL;
#endif

	if (sgm->battery_full)
		status = POWER_SUPPLY_STATUS_FULL;

	return status;
}

static int sgm4154x_set_hiz_en(struct charger_device *chg_dev, bool hiz_en)
{
	u8 reg_val;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_info("set %s\n", hiz_en ? "enable" : "disable");
	reg_val = hiz_en ? SGM4154x_HIZ_EN : 0;

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_0,
			SGM4154x_HIZ_EN, reg_val);
}

static int sgm4154x_enable_charger(struct sgm4154x_device *sgm)
{
	int ret;

	pr_info("enter\n");

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1,
			SGM4154x_CHRG_EN, SGM4154x_CHRG_EN);

	return ret;
}

static int sgm4154x_disable_charger(struct sgm4154x_device *sgm)
{
	int ret;

	pr_info("enter\n");

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1,
			SGM4154x_CHRG_EN, 0);

	return ret;
}

static int sgm4154x_is_charging(struct sgm4154x_device *sgm, bool *en)
{
	int ret;
	u8 val;

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_1, &val);
	if (ret) {
		pr_err("read SGM4154x_CHRG_CTRL_a fail\n");
		return ret;
	}
	*en = (val & SGM4154x_CHRG_EN) ? 1 : 0;

	return ret;
}

static int sgm4154x_check_charging_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_is_charging(sgm, en);

	return ret;
}

static int sgm4154x_charging_switch(struct charger_device *chg_dev, bool enable)
{
	int ret;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	if (enable)
		ret = sgm4154x_enable_charger(sgm);
	else
		ret = sgm4154x_disable_charger(sgm);

	return ret;
}

static int sgm4154x_set_recharge_volt(struct sgm4154x_device *sgm, int mV)
{
	u8 reg_val;

	reg_val = (mV - SGM4154x_VRECHRG_OFFSET_mV) / SGM4154x_VRECHRG_STEP_mV;

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_4,
			SGM4154x_VRECHARGE, reg_val);
}

static int sgm4154x_set_wdt_rst(struct sgm4154x_device *sgm, bool is_rst)
{
	u8 val;

	if (is_rst)
		val = SGM4154x_WDT_RST_MASK;
	else
		val = 0;

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1,
			SGM4154x_WDT_RST_MASK, val);
}

__maybe_unused static int sgm4154x_set_dpdm_hiz(struct sgm4154x_device *sgm)
{
	int ret;
	int reg_val = 0;

	/*set dp in Hiz mode*/
	reg_val = 0 << 3;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_DP_VSEL_MASK, reg_val);
	if (ret < 0) {
		pr_err("set dp hiz failed ret(%d)\n", ret);
		return ret;
	}

	/*set dm in Hiz mode*/
	reg_val = 0 << 1;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_DM_VSEL_MASK, reg_val);
	if (ret < 0) {
		pr_err("set dm hiz failed ret(%d)\n", ret);
		return ret;
	}

	return ret;
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm4154x_dump_register(struct charger_device *chg_dev)
{

	unsigned char i = 0;
	unsigned int ret = 0;
	unsigned char sgm4154x_reg[SGM4154x_REG_NUM + 1] = { 0 };
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	char reg_buff[TOTAL_DUMP_LEN] = {0};
	char temp_buff[SINGLE_DUMP_LEN] = {0};

	if (dump_reg_enable) {
		for (i = 0; i < SGM4154x_REG_NUM + 1; i++) {
			ret = sgm4154x_read_reg(sgm, i, &sgm4154x_reg[i]);
			if (ret != 0) {
				pr_info("i2c transfor error\n");
				return ret;
			}
			snprintf(temp_buff, SINGLE_DUMP_LEN, "reg[0x%02x]=0x%02x ", i, sgm4154x_reg[i]);
			strcat(reg_buff, temp_buff);
		}
		pr_info("%s", reg_buff);
	} else {
		pr_err("dump register has been disabled\n");
	}

	return ret;
}

static int sgm4154x_plug_in(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);
	struct sgm4154x_state state;

	pr_info("enter, enable charging\n");

	/* Enable charging */
	ret = sgm4154x_enable_charger(sgm);
	if (ret) {
		pr_err("Failed to enable charging:%d\n", ret);
	}

	ret = sgm4154x_get_state(sgm, &state);
	if (ret) {
		pr_err("Failed to get state:%d\n", ret);
	}

	mutex_lock(&sgm->lock);
	sgm->state = state;
	mutex_unlock(&sgm->lock);

	power_supply_changed(sgm->charger);
	return ret;
}

static int sgm4154x_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);

	pr_info("enter\n");

	ret = sgm4154x_set_dpdm_hiz(sgm);
	ret = sgm4154x_disable_charger(sgm);
	if (ret) {
		pr_err("Failed to disable charging:%d\n", ret);
	}
	sgm->pd_type_detected = false;
	return ret;
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int sgm4154x_reset_registers(struct sgm4154x_device *sgm)
{
	int ret = 0;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_b,
					SGM4151x_REG_RST, SGM4151x_REG_RST);
	if (ret < 0) {
		pr_info("reset fail\n");
		return ret;
	}

	return ret;
}

static int sgm4154x_hw_chipid_detect(struct sgm4154x_device *sgm)
{
	int ret = 0;
	u8 val = 0;

	ret = sgm4154x_read_reg(sgm,SGM4154x_CHRG_CTRL_b, &val);
	if (ret < 0) {
		pr_info("read SGM4154x_CHRG_CTRL_b fail\n");
		return ret;
	}

	val = val & SGM4154x_PN_MASK;
	pr_info("Reg[0x0B]=0x%x\n", val);

	return val;
}

static int sgm4154x_reset_watch_dog_timer(struct charger_device *chg_dev)
{
	int ret;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_info("charging_reset_watch_dog_timer\n");

	ret = sgm4154x_set_wdt_rst(sgm, 0x1);	/* RST watchdog */

	return ret;
}

static int sgm4154x_get_charging_status(struct charger_device *chg_dev, bool *is_done)
{
	//struct sgm4154x_state state;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	//sgm4154x_get_state(sgm, &state);

	if (sgm->state.chrg_stat == SGM4154x_TERM_CHRG)
		*is_done = true;
	else
		*is_done = false;

	if (sgm->battery_full)
		*is_done = true;
	else
		*is_done = false;

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	if ((turbo_charger_active == true) && (ffc_batt_full == true))
		*is_done = true;
#endif

	return 0;
}

__maybe_unused static int sgm4154x_enable_powerpath(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_info("enter\n");

	/* Enable/Disable charging */
	if (en) {
		ret = sgm4154x_enable_charger(sgm);
	} else {
		ret = sgm4154x_disable_charger(sgm);
	}

	if (ret) {
		pr_info("Failed to %s charger\n", en ? "enable" : "disable");
	}

	return ret;
}

__maybe_unused static int sgm4154x_is_powerpath_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_info("enter\n");

	ret = sgm4154x_is_charging(sgm, en);

	return ret;
}

static int sgm4154x_set_en_timer(struct sgm4154x_device *sgm)
{
	int ret;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
			SGM4154x_SAFETY_TIMER_EN, SGM4154x_SAFETY_TIMER_EN);

	return ret;
}

static int sgm4154x_set_disable_timer(struct sgm4154x_device *sgm)
{
	int ret;

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_5,
			SGM4154x_SAFETY_TIMER_EN, 0);

	return ret;
}

static int sgm4154x_enable_safetytimer(struct charger_device *chg_dev, bool en)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int ret = 0;

	if (en)
		ret = sgm4154x_set_en_timer(sgm);
	else
		ret = sgm4154x_set_disable_timer(sgm);

	return ret;
}

static int sgm4154x_get_is_safetytimer_enable(struct charger_device *chg_dev, bool *en)
{
	int ret = 0;
	u8 val = 0;

	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_read_reg(sgm,SGM4154x_CHRG_CTRL_5, &val);
	if (ret < 0) {
		pr_info("read SGM4154x_CHRG_CTRL_5 fail\n");
		return ret;
	}

	*en = !!(val & SGM4154x_SAFETY_TIMER_EN);

	return 0;
}

#if (defined(__SGM41542_CHIP_ID__)|| defined(__SGM41516D_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
static int sgm4154x_en_pe_current_partern(struct charger_device *chg_dev, bool is_up)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
			SGM4154x_EN_PUMPX, SGM4154x_EN_PUMPX);
	if (ret < 0) {
		pr_info("read SGM4154x_CHRG_CTRL_d fail\n");
		return ret;
	}

	if (is_up)
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_PUMPX_UP, SGM4154x_PUMPX_UP);
	else
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
				SGM4154x_PUMPX_DN, SGM4154x_PUMPX_DN);
	return ret;
}
#endif

static enum power_supply_property sgm4154x_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	POWER_SUPPLY_PROP_USB_TYPE,
#endif
	POWER_SUPPLY_PROP_TYPE,
	//POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_PRESENT
};

static int sgm4154x_property_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	//case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_ONLINE:
		return true;
	default:
		return false;
	}
}

static int sgm4154x_charger_set_property(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	struct sgm4154x_device *sgm = power_supply_get_drvdata(psy);
	int ret = 0;

	if (IS_ERR_OR_NULL(sgm)) {
		pr_err("get sgm device failed\n");
		return -ENODEV;
	}

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval == 2) {
			pr_info("attach is %d, start charger detection\n", val->intval);
			schedule_delayed_work(&sgm->charge_detect_delayed_work, msecs_to_jiffies(500));
		} else if (val->intval == 0) {
			pr_info("attach is %d, vbus not online \n", val->intval);
			sgm->pd_type_detected = false;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
			sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
#endif
			sgm->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			/*
			 * if usb cable being plug out between driver probe done and healthd service init done.
			 * healthd service will ignore all the events of switch charger as the desc type of switch charger is being set to unknown.
			 * so we can't set the default desc type of switch charger to unknown.
			 */
			sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_TYPE_C;
			cancel_delayed_work(&sgm->charge_detect_delayed_work);
			power_supply_changed(sgm->charger);
		} else if (val->intval == 5) {
			pr_info("attach is %d, PD type is ATTACH_TYPE_PD_DCP\n", val->intval);
			sgm->pd_type_detected = true;
			sgm->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
			sgm->psy_usb_type = POWER_SUPPLY_TYPE_USB_PD_DCP;
#endif
			sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
			power_supply_changed(sgm->charger);
		}
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = sgm4154x_set_input_curr_lim(s_chg_dev_otg, val->intval);
		break;
/*	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		sgm4154x_charging_switch(s_chg_dev_otg,val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = sgm4154x_set_input_volt_lim(s_chg_dev_otg, val->intval);
		break;*/
	default:
		return -EINVAL;
	}

	return ret;
}

static int sgm4154x_charger_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct sgm4154x_device *sgm = power_supply_get_drvdata(psy);
	struct sgm4154x_state state;
	int ret = 0;
	int value = 0;

	mutex_lock(&sgm->lock);
	state = sgm->state;
	mutex_unlock(&sgm->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = sgm4154x_get_charge_stat(sgm);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		switch (state.chrg_stat) {
		case SGM4154x_PRECHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM4154x_FAST_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
			break;
		case SGM4154x_TERM_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case SGM4154x_NOT_CHRGING:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		}
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = SGM4154x_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = SGM4154x_NAME;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = state.online;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = state.vbus_gd;
		break;

	case POWER_SUPPLY_PROP_TYPE:
		val->intval = sgm4154x_power_supply_desc.type;
		break;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = sgm->psy_usb_type;
		break;
#endif

	case POWER_SUPPLY_PROP_HEALTH:
		if (state.chrg_fault & 0xF8)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;

		switch (state.health) {
		case SGM4154x_TEMP_HOT:
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
			break;
		case SGM4154x_TEMP_WARM:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			break;
		case SGM4154x_TEMP_COOL:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			break;
		case SGM4154x_TEMP_COLD:
			val->intval = POWER_SUPPLY_HEALTH_COLD;
			break;
		}
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sgm4154x_get_vbus_voltage(sgm, &value);
		val->intval = value;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		//val->intval = state.ibus_adc;
		break;

/*	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = sgm4154x_get_input_volt_lim(sgm);
		if (ret < 0)
			return ret;

		val->intval = ret;
		break;*/

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		break;
#if 0
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = !state.hiz_en;
		break;
#endif
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = sgm->batt_vol * 1000;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

__maybe_unused static bool sgm4154x_state_changed(struct sgm4154x_device *sgm,
		struct sgm4154x_state *new_state)
{
	struct sgm4154x_state old_state;

	mutex_lock(&sgm->lock);
	old_state = sgm->state;
	mutex_unlock(&sgm->lock);

	return (old_state.chrg_type != new_state->chrg_type ||
		old_state.chrg_stat != new_state->chrg_stat ||
		old_state.online != new_state->online ||
		old_state.therm_stat != new_state->therm_stat ||
		old_state.vsys_stat != new_state->vsys_stat ||
		old_state.chrg_fault != new_state->chrg_fault
		);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
__maybe_unused static int charger_detect_init(struct sgm4154x_device *sgm)
{
	struct phy *phy;
	int ret;

	pr_info("enter\n");

	phy = phy_get(sgm->dev, "usb2-phy");
	if (IS_ERR_OR_NULL(phy)) {
		pr_err("failed to get usb2-phy\n");
		return -ENODEV;
	}

	ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, PHY_MODE_BC11_SET);

	if (ret)
		pr_err("failed to set phy ext mode\n");
	phy_put(sgm->dev, phy);

	return ret;
}

__maybe_unused static int charger_detect_release(struct sgm4154x_device *sgm)
{
	struct phy *phy;
	int ret;

	pr_info("enter\n");

	phy = phy_get(sgm->dev, "usb2-phy");
	if (IS_ERR_OR_NULL(phy)) {
		pr_err("failed to get usb2-phy\n");
		return -ENODEV;
	}

	ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, PHY_MODE_BC11_CLR);

	if (ret)
		pr_err("failed to set phy ext mode\n");
	phy_put(sgm->dev, phy);

	return ret;

}
#endif

#if 0
static int update_battery_info_from_gauge(struct sgm4154x_device *sgm)
{
	int ret = 0;
	union power_supply_propval info;

	if (IS_ERR_OR_NULL(sgm->battery)) {
		sgm->battery = power_supply_get_by_name("battery");
		if (IS_ERR_OR_NULL(sgm->battery)) {
			pr_err("failed to get battery supply\n");
		}
		return -EINVAL;
	}

	/*get Vbat from gauge*/
	ret = power_supply_get_property(sgm->battery,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &info);
	sgm->batt_vol = info.intval / 1000;
	/*get Ibat from gauge*/
	ret = power_supply_get_property(sgm->battery,
			POWER_SUPPLY_PROP_CURRENT_NOW, &info);
	sgm->batt_curr = info.intval / 1000;

	pr_info("Vbat = %d mV, Ibat = %d mA\n",
			sgm->batt_vol, sgm->batt_curr);

	return ret;
}

static void charger_monitor_work_func(struct work_struct *work)
{
	int ret = 0;
	struct sgm4154x_device *sgm = NULL;
	//static u8 last_chg_method = 0;
	struct sgm4154x_state state;

	sgm = container_of(work, struct sgm4154x_device, charge_monitor_work.work);
	if (sgm == NULL) {
		pr_err("Cann't get sgm \n");
		return;
	}

	ret = sgm4154x_get_state(sgm, &state);
	mutex_lock(&sgm->lock);
	sgm->state = state;
	mutex_unlock(&sgm->lock);

	ret = update_battery_info_from_gauge(sgm);
	if (ret) {
		pr_err("failed to get batt vol and curr\n");
	}

	if (!sgm->state.vbus_gd) {
		pr_err("Vbus not present, disable charge\n");
		sgm4154x_disable_charger(sgm);
		goto out;
	}

	if (!state.online) {
		pr_err("Vbus not online\n");
		goto out;
	}

	sgm4154x_dump_register(sgm->chg_dev);
	pr_info("enter\n");
out:
	schedule_delayed_work(&sgm->charge_monitor_work, 10 * HZ);
}
#endif

/*TN Begin modified by maocai.cao/808964 20231120 CR/EKFOGO4G-3815*/
static int sgm4154x_force_dpdm(struct sgm4154x_device *sgm)
{

	pr_info("enter\n");

	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_7,
	        SGM4154x_FORCE_DPDM, SGM4154x_FORCE_DPDM);
}

static void retry_charger_detect_work_func(struct work_struct *work)
{
	struct sgm4154x_device *sgm = NULL;
	int ret;
	sgm = container_of(work, struct sgm4154x_device, retry_charger_detect_work.work);
	if (sgm == NULL) {
		pr_err("Cann't get sgm4154x_device\n");
		return;
	}

	Charger_Detect_Init();

	ret = sgm4154x_force_dpdm(sgm);
	if (ret < 0) {
		pr_err("Cann't force dpdm\n");
		return;
	}

	sgm->force_detect_count++;
	schedule_delayed_work(&sgm->charge_detect_delayed_work, msecs_to_jiffies(1000));

	return;
}
/*TN End modified by maocai.cao/808964 20231120 CR/EKFOGO4G-3815*/

static void charger_detect_work_func(struct work_struct *work)
{
	struct sgm4154x_device *sgm = NULL;
	//static int charge_type_old = 0;
	struct sgm4154x_state state;
	int ret;

	sgm = container_of(work, struct sgm4154x_device, charge_detect_delayed_work.work);
	if (sgm == NULL) {
		pr_err("Cann't get sgm4154x_device\n");
		return;
	}

	if (!sgm->charger_wakelock->active)
		__pm_stay_awake(sgm->charger_wakelock);
	ret = sgm4154x_set_vindpm_track(sgm, SGM4154x_TRACK_300);
	ret = sgm4154x_get_state(sgm, &state);
	mutex_lock(&sgm->lock);
	sgm->state = state;
	mutex_unlock(&sgm->lock);

	if (sgm->pd_type_detected) {
		pr_err("PD type is ATTACH_TYPE_PD_DCP, no need to detect, SGM4154x charger type: DCP\n");
		power_supply_changed(sgm->charger);
		pr_info("Relax wakelock\n");
		__pm_relax(sgm->charger_wakelock);
		return;
	}

/*TN Begin modified by maocai.cao/808964 20231120 CR/EKFOGO4G-3815*/
#if 0
	if (!sgm->state.vbus_gd) {
		pr_err("Vbus not present, disable charge\n");
		sgm4154x_disable_charger(sgm);
		sgm4154x_set_dpdm_hiz(sgm);
		sgm->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
		allow_set_dp_dm_vol = false;
		goto err;
	} else {
		allow_set_dp_dm_vol = true;
	}

	if (!state.online) {
		pr_err("Vbus not online\n");
		sgm->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
		goto err;
	}
#endif
/*TN End modified by maocai.cao/808964 20231120 CR/EKFOGO4G-3815*/

#if (defined(__SGM41542_CHIP_ID__)|| defined(__SGM41516D_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
	switch(sgm->state.chrg_type) {
	case SGM4154x_USB_SDP:
		pr_info("SGM4154x charger type: SDP\n");
		sgm->chg_type = POWER_SUPPLY_TYPE_USB;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
#endif
		sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB;
		break;

	case SGM4154x_USB_CDP:
		pr_info("SGM4154x charger type: CDP\n");
		sgm->chg_type = POWER_SUPPLY_TYPE_USB_CDP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_CDP;
#endif
		sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		break;

	case SGM4154x_USB_DCP:
		pr_info("SGM4154x charger type: DCP\n");
		sgm->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
		sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		break;

	case SGM4154x_UNKNOWN:
		pr_info("SGM4154x charger type: UNKNOWN\n");
		sgm->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		sgm->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
#endif
		/*
		 * if usb cable being plug out between driver probe done and healthd service init done.
		 * healthd service will ignore all the events of switch charger as the desc type of switch charger is being set to unknown.
		 * so we can't set the default desc type of switch charger to unknown.
		 */
		sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_TYPE_C;
		if (sgm->force_detect_count < 10) {
			pr_info("SGM4154x charger type: UNKNOWN, retry bc1.2 count:%d\n", sgm->force_detect_count);
			schedule_delayed_work(&sgm->retry_charger_detect_work, msecs_to_jiffies(100));
		}
		break;

	case SGM4154x_NON_STANDARD:
		pr_info("SGM4154x charger type: NON STANDARD\n");
		sgm->chg_type = POWER_SUPPLY_TYPE_USB_NON_STD;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		sgm->psy_usb_type = POWER_SUPPLY_TYPE_USB_NON_STD;
#endif
		sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		if (sgm->force_detect_count < 10) {
			pr_info("SGM4154x charger type: NON STANDARD, retry bc1.2 count:%d\n", sgm->force_detect_count);
			schedule_delayed_work(&sgm->retry_charger_detect_work, msecs_to_jiffies(100));
		}
		break;

	default:
		pr_info("SGM4154x charger type: default\n");
		sgm->chg_type = POWER_SUPPLY_TYPE_USB_NON_STD;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		sgm->psy_usb_type = POWER_SUPPLY_TYPE_USB_NON_STD;
#endif
		sgm4154x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		if (sgm->force_detect_count < 10) {
			pr_info("SGM4154x charger type: Default, retry bc1.2 count:%d\n", sgm->force_detect_count);
			schedule_delayed_work(&sgm->retry_charger_detect_work, msecs_to_jiffies(100));
		}
		break;
	}

	if (sgm->state.chrg_type == SGM4154x_USB_SDP || sgm->state.chrg_type == SGM4154x_USB_CDP) {
		Charger_Detect_Release();
	}

	pr_info("Update: chg_type:%d, psy_usb_type:%d\n", sgm->chg_type, sgm->psy_usb_type);
#endif
	//sgm4154x_enable_charger(sgm);
	sgm4154x_dump_register(sgm->chg_dev);

//err:
	//release wakelock
	power_supply_changed(sgm->charger);
	pr_info("Relax wakelock\n");
	__pm_relax(sgm->charger_wakelock);

	return;
}

static irqreturn_t sgm4154x_irq_handler_thread(int irq, void *private)
{
	struct sgm4154x_device *sgm = private;
	struct sgm4154x_state state;
	bool prev_vbus_gd;
	bool prev_online;
	int ret = 0;

	//lock wakelock
	pr_info("enter\n");
#if 1
	ret = sgm4154x_get_state(sgm, &state);
	if (ret) {
		pr_err("Failed to get state(%d)\n", ret);
		return IRQ_HANDLED;
	}

	mutex_lock(&sgm->lock);
	prev_vbus_gd = sgm->state.vbus_gd;
	prev_online = sgm->state.online;
	sgm->state = state;
	mutex_unlock(&sgm->lock);

	if (!prev_online && sgm->state.online) {
		pr_info("adapter/usb power good, limit input and charger current\n");
		sgm4154x_set_input_curr_lim(sgm->chg_dev, 100000);
		sgm4154x_set_ichrg_curr(sgm->chg_dev, 100000);
		sgm4154x_enable_charger(sgm);
		if (sgm->state.input_det_done) {
			schedule_delayed_work(&sgm->charge_detect_delayed_work, msecs_to_jiffies(50));
		}

		/*
		 * sgm415xx can't generate interrupts when booting with cable connected.
		 * so we trigger the interrupt callback manullay in probe.
		 * manully interrupt will enter here firstly. we should clear flags here for hvdcp algorithm.
		 */
		sgm->force_detect_count = 0;
		allow_set_dp_dm_vol = true;
		return IRQ_HANDLED;
	}

	if (!prev_vbus_gd && sgm->state.vbus_gd) {
		pr_info("adapter/usb inserted\n");
		Charger_Detect_Init();
		sgm->force_detect_count = 0;
		allow_set_dp_dm_vol = true;
	} else if (prev_vbus_gd && !sgm->state.vbus_gd) {
		pr_info("adapter/usb removed\n");
		Charger_Detect_Release();
		sgm4154x_set_dpdm_hiz(sgm);
		allow_set_dp_dm_vol = false;
		power_supply_changed(sgm->charger);
/*TN End modified by maocai.cao/808964 20231120 CR/EKFOGO4G-3815*/
	}
#else
	schedule_delayed_work(&sgm->charge_detect_delayed_work, 100);
#endif
	//power_supply_changed(sgm->charger);

	return IRQ_HANDLED;
}

static char *sgm4154x_charger_supplied_to[] = {
	"battery",
	"mtk-master-charger",
};

static struct power_supply_desc sgm4154x_power_supply_desc = {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	.name = "primary_chg",
#else
	.name = "ext_charger_type",
#endif
	.type = POWER_SUPPLY_TYPE_USB,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	.usb_types = sgm4154x_usb_type,
	.num_usb_types = ARRAY_SIZE(sgm4154x_usb_type),
#endif
	.properties = sgm4154x_power_supply_props,
	.num_properties = ARRAY_SIZE(sgm4154x_power_supply_props),
	.get_property = sgm4154x_charger_get_property,
	.set_property = sgm4154x_charger_set_property,
	.property_is_writeable = sgm4154x_property_is_writeable,
};

static int sgm4154x_power_supply_init(struct sgm4154x_device *sgm, struct device *dev)
{
	struct power_supply_config psy_cfg = {
		.drv_data = sgm,
		.of_node = dev->of_node,
	};

	psy_cfg.supplied_to = sgm4154x_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(sgm4154x_charger_supplied_to);

	sgm->charger = devm_power_supply_register(sgm->dev,
			 &sgm4154x_power_supply_desc,
			 &psy_cfg);

	if (IS_ERR(sgm->charger))
		return -EINVAL;

	return 0;
}

static int sgm4154x_hw_init(struct sgm4154x_device *sgm)
{
	int ret = 0;
	struct power_supply_battery_info bat_info = { };

	bat_info.constant_charge_current_max_ua =
			SGM4154x_ICHRG_I_DEF_uA;

	bat_info.constant_charge_voltage_max_uv =
			SGM4154x_VREG_V_DEF_uV;

	bat_info.precharge_current_ua =
			SGM4154x_PRECHRG_I_DEF_uA;

	bat_info.charge_term_current_ua =
			SGM4154x_TERMCHRG_I_DEF_uA;

	sgm->init_data.max_ichg =
			SGM4154x_ICHRG_I_MAX_uA;

	sgm->init_data.max_vreg =
			SGM4154x_VREG_V_MAX_uV;

	sgm4154x_set_watchdog_timer(sgm, 0);
	sgm4154x_set_dpm_mask(sgm);

	sgm4154x_set_tmr2x(sgm, false);
	ret = sgm4154x_set_ichrg_curr(s_chg_dev_otg,
			bat_info.constant_charge_current_max_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_prechrg_curr(sgm, bat_info.precharge_current_ua);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_chrg_volt(s_chg_dev_otg,
			bat_info.constant_charge_voltage_max_uv);
	if (ret)
		goto err_out;

	ret = sgm4154x_set_term_curr(s_chg_dev_otg, bat_info.charge_term_current_ua);
	if (ret)
		goto err_out;

	/*ret = sgm4154x_set_input_volt_lim(sgm, sgm->init_data.vlim);
	if (ret)
		goto err_out;*/

	ret = sgm4154x_set_input_curr_lim(s_chg_dev_otg, sgm->init_data.ilim);
	if (ret)
		goto err_out;
#if 0
	ret = sgm4154x_set_vac_ovp(sgm); //14V
	if (ret)
		goto err_out;
#endif
	ret = sgm4154x_set_recharge_volt(sgm, 200); //100~200mv
	if (ret)
		goto err_out;

	pr_info("ichrg_curr:%d prechrg_curr:%d chrg_vol:%d term_curr:%d input_curr_lim:%d",
		bat_info.constant_charge_current_max_ua,
		bat_info.precharge_current_ua,
		bat_info.constant_charge_voltage_max_uv,
		bat_info.charge_term_current_ua,
		sgm->init_data.ilim);

	return 0;
err_out:
	return ret;
}

static int sgm4154x_parse_dt(struct sgm4154x_device *sgm)
{
	int ret;
	int irq_gpio = 0, irqn = 0;
	int chg_en_gpio = 0;

	ret = device_property_read_u32(sgm->dev,
			"input-voltage-limit-microvolt", &sgm->init_data.vlim);
	if (ret) {
		sgm->init_data.vlim = SGM4154x_VINDPM_DEF_uV;
	}

	if (sgm->init_data.vlim > SGM4154x_VINDPM_V_MAX_uV ||
		sgm->init_data.vlim < SGM4154x_VINDPM_V_MIN_uV) {
		pr_err("VIN DPM out of range\n");
		return -EINVAL;
	}

	ret = device_property_read_u32(sgm->dev,
			"input-current-limit-microamp", &sgm->init_data.ilim);
	if (ret) {
		sgm->init_data.ilim = SGM4154x_IINDPM_DEF_uA;
	}

	if (sgm->init_data.ilim > SGM4154x_IINDPM_I_MAX_uA ||
		sgm->init_data.ilim < SGM4154x_IINDPM_I_MIN_uA) {
		pr_err("IIN DPM out of range\n");
		return -EINVAL;
	}

	irq_gpio = of_get_named_gpio(sgm->dev->of_node, "sgm,irq-gpio", 0);
	if (!gpio_is_valid(irq_gpio)) {
		pr_err("%d gpio get failed\n", irq_gpio);
		return -EINVAL;
	}

	ret = gpio_request(irq_gpio, "sgm4154x irq pin");
	if (ret) {
		pr_err("%d gpio request failed\n", irq_gpio);
		return ret;
	}

	gpio_direction_input(irq_gpio);
	irqn = gpio_to_irq(irq_gpio);
	if (irqn < 0) {
		pr_err("%d gpio_to_irq failed\n", irqn);
		return irqn;
	}

	sgm->client->irq = irqn;

	chg_en_gpio = of_get_named_gpio(sgm->dev->of_node, "sgm,chg-en-gpio", 0);
	if (!gpio_is_valid(chg_en_gpio)) {
		pr_err("%d gpio get failed\n", chg_en_gpio);
		return -EINVAL;
	}

	ret = gpio_request(chg_en_gpio, "sgm chg en pin");
	if (ret) {
		pr_err("%d gpio request failed\n", chg_en_gpio);
		return ret;
	}

	gpio_direction_output(chg_en_gpio, 0); //default enable charge

	return 0;
}

__maybe_unused static int sgm4154x_enable_vbus(void)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(s_chg_dev_otg);

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1,
			SGM4154x_OTG_EN, SGM4154x_OTG_EN);

	return ret;
}

__maybe_unused static int sgm4154x_disable_vbus(void)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(s_chg_dev_otg);

	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1,
			SGM4154x_OTG_EN, 0);

	return ret;
}

__maybe_unused static int sgm4154x_is_enabled_vbus(struct regulator_dev *rdev)
{
	u8 temp = 0;
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(s_chg_dev_otg);

	ret = sgm4154x_read_reg(sgm, SGM4154x_CHRG_CTRL_1, &temp);
	return (temp&SGM4154x_OTG_EN) ? 1 : 0;
}

/*
 * If operating DP\DM voltage is needed.
 * First, you should define OEM_FIXED_ME macro in you project defconfig.
 * Second, you should add function definition in mtk charger_class.h & charger_class.c
 */

static int sgm4154x_set_volt_to_reg(u32 volt)
{
	int reg_val = 0;
	if (volt == 0)
		reg_val = 0x1;
	else if (volt == 3300000)
		reg_val = 0x3;
	else if (volt == 600000)
		reg_val = 0x2;
	else
		reg_val = 0x0;

	return reg_val;
}

static int sgm4154x_set_dp(struct charger_device *chg_dev, u32 volt)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int reg_val = 0;

	if (false == allow_set_dp_dm_vol) {
		pr_info("not allow set dp voltage\n");
/*TN Begin modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
		return -EINVAL;
/*TN End modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
	}

	reg_val = sgm4154x_set_volt_to_reg(volt);

	reg_val = reg_val << 3;
	pr_info("set_dp = %duV\n", volt);
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
			SGM4154x_DP_VSEL_MASK, reg_val);
}

static int sgm4154x_set_dm(struct charger_device *chg_dev, u32 volt)
{
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);
	int reg_val = 0;

	if (false == allow_set_dp_dm_vol) {
		pr_info("not allow set dp voltage\n");
/*TN Begin modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
		return -EINVAL;
/*TN End modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
	}

	reg_val = sgm4154x_set_volt_to_reg(volt);

	reg_val = reg_val << 1;
	pr_info("set_dm = %duV\n", volt);
	return sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_d,
			SGM4154x_DM_VSEL_MASK, reg_val);
}


#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
static int sgm4154x_enable_dpdm_hiz(struct charger_device *chg_dev)
{
	int ret;
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);

	ret = sgm4154x_set_dpdm_hiz(sgm);
	if (ret < 0)
		pr_err("set dpdm hiz failed ret(%d)\n", ret);

	return ret;
}
#endif

/*TN Begin modified by lingfei.tang/77407 20231201 CR/EKFOGO4G-5993*/
static int sgm4154x_do_event(struct charger_device *chg_dev, u32 event, u32 args)
{
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);

	pr_info("event:%d\n", event);

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(4, 19, 0))
	switch (event) {
	case EVENT_EOC:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_EOC);
		break;
	case EVENT_RECHARGE:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_RECHG);
		break;
	default:
		break;
	}
#else
	switch (event) {
	case EVENT_FULL:
		sgm->battery_full = true;
		break;
	case EVENT_RECHARGE:
	case EVENT_DISCHARGE:
		sgm->battery_full = false;
		break;
	default:
		break;
	}
	power_supply_changed(sgm->charger);
#endif
	return 0;
}
/*TN End modified by lingfei.tang/77407 20231201 CR/EKFOGO4G-5993*/

static int sgm4154x_enable_otg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	pr_info("en = %d\n", en);
	if (en) {
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1,
			SGM4154x_OTG_EN, SGM4154x_OTG_EN);
	} else {
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_1,
			SGM4154x_OTG_EN, 0);
	}

	return ret;
}

__maybe_unused static int sgm4154x_set_boost_voltage_limit(
		struct charger_device *chg_dev, u32 uV)
{
	int ret = 0;
	char reg_val = -1;
	int i = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	while (i < 4) {
		if (uV == BOOST_VOLT_LIMIT[i]) {
			reg_val = i;
			break;
		} else if (uV < BOOST_VOLT_LIMIT[i]) {
			reg_val = i - 1;
			break;
		}
		i++;
	}
	if (reg_val < 0)
		return reg_val;

	reg_val = reg_val << 4;
	ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_6,
			SGM4154x_BOOSTV, reg_val);

	return ret;
}

static int sgm4154x_set_boost_current_limit(struct charger_device *chg_dev, u32 uA)
{
	int ret = 0;
	struct sgm4154x_device *sgm = charger_get_data(chg_dev);

	if (uA <= BOOST_CURRENT_LIMIT[0])
		uA = BOOST_CURRENT_LIMIT[0];
	else if (uA >= BOOST_CURRENT_LIMIT[1])
		uA = BOOST_CURRENT_LIMIT[1];
	else if (uA - BOOST_CURRENT_LIMIT[0] < (BOOST_CURRENT_LIMIT[1] - BOOST_CURRENT_LIMIT[0]) / 2)
		uA = BOOST_CURRENT_LIMIT[0];
	else
		uA = BOOST_CURRENT_LIMIT[1];

	if (uA == BOOST_CURRENT_LIMIT[0]) {
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_2,
				SGM4154x_BOOST_LIM, 0);
	} else if (uA == BOOST_CURRENT_LIMIT[1]) {
		ret = sgm4154x_update_bits(sgm, SGM4154x_CHRG_CTRL_2,
				SGM4154x_BOOST_LIM, BIT(7));
	}
	pr_info("set boost current limit:%d\n", uA);

	return ret;
}

static int sgm4154x_get_property(struct charger_device *chg_dev,
			enum charger_property prop, union charger_propval *val)
{
	struct sgm4154x_device *sgm = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	int value = 0;
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	bool is_enabled = false;
#endif

	pr_info("prop:%d\n", prop);

	switch (prop) {
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	case CHARGER_PROP_CHARGER_ENABLED:
		ret = sgm4154x_is_charging(sgm, &is_enabled);
		if (!ret) {
			val->intval = is_enabled;
		}
		break;
	case CHARGER_PROP_CHARGER_EXIST:
		val->intval = sgm->state.vbus_gd;
		break;
	case CHARGER_PROP_CHARGER_VOLTAGE:
		ret = sgm4154x_get_vbus_voltage(sgm, &value);
		val->intval = value;
		break;
	case CHARGER_PROP_CHARGER_PROP_STATUS:
		ret = sgm4154x_get_charge_stat(sgm);
		if (ret < 0)
			break;
		val->intval = ret;
		break;
	case CHARGER_PROP_CHARGER_TYPE:
		val->intval = sgm->chg_type;
		break;

#endif
	default:
		break;
	}

	return ret;
}

#if 0
static struct regulator_ops sgm4154x_vbus_ops = {
	.enable = sgm4154x_enable_vbus,
	.disable = sgm4154x_disable_vbus,
	.is_enabled = sgm4154x_is_enabled_vbus,
};

static const struct regulator_desc sgm4154x_otg_rdesc = {
	.of_match = "usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &sgm4154x_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int sgm4154x_vbus_regulator_register(struct sgm4154x_device *sgm)
{
	struct regulator_config config = {};
	int ret = 0;
	/* otg regulator */
	config.dev = sgm->dev;
	config.driver_data = sgm;
	sgm->otg_rdev = devm_regulator_register(sgm->dev,
				&sgm4154x_otg_rdesc, &config);
	sgm->otg_rdev->constraints->valid_ops_mask |= REGULATOR_CHANGE_STATUS;
	if (IS_ERR(sgm->otg_rdev)) {
		ret = PTR_ERR(sgm->otg_rdev);
		pr_info("register otg regulator failed(%d)\n", ret);
	}

	return ret;
}
#endif

static struct charger_ops sgm4154x_chg_ops = {
	.dump_registers = sgm4154x_dump_register,
	/* cable plug in/out */
	.plug_in = sgm4154x_plug_in,
	.plug_out = sgm4154x_plug_out,
	/* enable */
	.enable = sgm4154x_charging_switch,
	.is_enabled = sgm4154x_check_charging_enabled,
	/* charging current */
	.set_charging_current = sgm4154x_set_ichrg_curr,
	.get_charging_current = sgm4154x_get_ichg_curr,
	.get_min_charging_current = sgm4154x_get_minichg_curr,
	/* charging voltage */
	.set_constant_voltage = sgm4154x_set_chrg_volt,
	.get_constant_voltage = sgm4154x_get_chrg_volt,
	/* input current limit */
	.set_input_current = sgm4154x_set_input_curr_lim,
	.get_input_current = sgm4154x_get_input_curr_lim,
	.get_min_input_current = sgm4154x_get_input_mincurr_lim,
	/* MIVR */
	.set_mivr = sgm4154x_set_input_volt_lim,
	.get_mivr = sgm4154x_get_input_volt_lim,
	//.get_mivr_state = sgm4154x_get_input_minvolt_lim,
	/* ADC */
	//.get_adc = mt6375_get_adc,
	//.get_vbus_adc = mt6375_get_vbus,
	//.get_ibus_adc = mt6375_get_ibus,
	//.get_ibat_adc = mt6375_get_ibat,
	//.get_tchg_adc = mt6375_get_tchg,
	//.get_zcv = mt6375_get_zcv,
	/* charing termination */
	.set_eoc_current = sgm4154x_set_term_curr,
	.enable_termination = sgm4154x_enable_terminate,
	//.reset_eoc_state = mt6375_reset_eoc_state,
	//.safety_check = mt6375_sw_check_eoc,
	.is_charging_done = sgm4154x_get_charging_status,
	/* power path */
	//.enable_powerpath = sgm4154x_enable_powerpath,
	//.is_powerpath_enabled = sgm4154x_is_powerpath_enabled,
	/* timer */
	.enable_safety_timer = sgm4154x_enable_safetytimer,
	.is_safety_timer_enabled = sgm4154x_get_is_safetytimer_enable,
	.kick_wdt = sgm4154x_reset_watch_dog_timer,
	/* AICL */
	//.run_aicl = mt6375_run_aicc,
	/* PE+/PE+20 */
#if (defined(__SGM41542_CHIP_ID__)|| defined(__SGM41516D_CHIP_ID__)|| defined(__SGM41543D_CHIP_ID__))
	.send_ta_current_pattern = sgm4154x_en_pe_current_partern,
#else
	.send_ta_current_pattern = NULL,
#endif
	//.set_pe20_efficiency_table = mt6375_set_pe20_efficiency_table,
	//.send_ta20_current_pattern = mt6375_set_pe20_current_pattern,
	//.reset_ta = mt6375_reset_pe_ta,
	//.enable_cable_drop_comp = mt6,
	/* OTG */
	.enable_otg = sgm4154x_enable_otg,
	.set_boost_current_limit = sgm4154x_set_boost_current_limit,
	.set_boost_voltage_limit = sgm4154x_set_boost_voltage_limit,
	.enable_hz = sgm4154x_set_hiz_en,

	/* DPDM */
	.set_dp = sgm4154x_set_dp,
	.set_dm = sgm4154x_set_dm,

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	.enable_dpdm_hz = sgm4154x_enable_dpdm_hiz,
#endif
	/*TN Begin modified by lingfei.tang/77407 20231201 CR/EKFOGO4G-5993*/
	.event = sgm4154x_do_event,
	/*TN End modified by lingfei.tang/77407 20231201 CR/EKFOGO4G-5993*/

	/* get/set property*/
	.get_property = sgm4154x_get_property,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static ssize_t dump_reg_ctrl_write(struct file *filp,
	const char *ubuf, size_t cnt, loff_t *data)
{
	char buf[8] = {0};
	long val = 0;
	int ret = 0;

	if (cnt >= sizeof(buf)) {
		pr_err( "cnt is invalid\n");
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt)) {
		pr_err("cnt is invalid\n");
		return -EFAULT;
	}

	buf[cnt] = 0;
	ret = kstrtoul(buf, 10, (unsigned long *)&val);
	if (ret < 0) {
		pr_err("cnt is invalid\n");
		return ret;
	}

	dump_reg_enable = val;
	pr_info("dump_reg_enable is %s\n", dump_reg_enable ? "enable" : "disable");

	return cnt;
}

static int dump_reg_ctrl_show(struct seq_file *m, void *v)
{
	seq_printf(m, "dump reg enable is %s\n", dump_reg_enable ? "enable" : "disable");

	return 0;
}

static int dump_reg_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, dump_reg_ctrl_show, inode->i_private);
}

static const struct proc_ops dump_reg_ctrl_fops = {
	.proc_open = dump_reg_ctrl_open,
	.proc_write = dump_reg_ctrl_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#endif

static ssize_t sgm4154x_show_registers(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct sgm4154x_device *sgm = dev_get_drvdata(dev);
	uint8_t addr;
	uint8_t val;
	uint8_t tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sgm41542");

	for (addr = 0; addr < SGM4154x_REG_NUM + 1; addr++) {
		ret = sgm4154x_read_reg(sgm, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				"Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t sgm4154x_store_register(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct sgm4154x_device *sgm = dev_get_drvdata(dev);
	int ret;
	unsigned int val;
	unsigned int reg;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= SGM4154x_REG_NUM)
		sgm4154x_write_reg(sgm, reg, val);

	return count;
}

static DEVICE_ATTR(registers, 0660, sgm4154x_show_registers, sgm4154x_store_register);

static int sgm4154x_create_device_node(struct device *dev)
{
	int ret = 0;

	ret = device_create_file(dev, &dev_attr_registers);
	if (ret < 0) {
		pr_err("failed to create register attr\n");
		return -ENODEV;
	}

	return ret;
}

static void sgm4154x_destory_device_node(struct device *dev)
{
	device_remove_file(dev, &dev_attr_registers);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static int sgm4154x_driver_probe(struct i2c_client *client)
#else
static int sgm4154x_driver_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
#endif
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct sgm4154x_device *sgm;

	char *name = NULL;

	pr_info("enter\n");

	sgm = devm_kzalloc(dev, sizeof(*sgm), GFP_KERNEL);
	if (!sgm) {
		pr_err("alloc memory failed\n");
		return -ENOMEM;
	}

	sgm->client = client;
	sgm->dev = dev;

	mutex_init(&sgm->lock);
	mutex_init(&sgm->i2c_rw_lock);

	i2c_set_clientdata(client, sgm);

	sgm->vbus = devm_iio_channel_get(sgm->dev, "pmic_vbus");
	if (IS_ERR_OR_NULL(sgm->vbus)) {
		pr_err("sgm41542 get vbus failed\n");
		return -EPROBE_DEFER;
	}

	ret = sgm4154x_hw_chipid_detect(sgm);
	if (ret != SGM4154x_PN_ID) {
		pr_info("device not found !!!\n");
		return ret;
	}

	ret = sgm4154x_parse_dt(sgm);
	if (ret) {
		pr_err("parse dts resource failed\n");
		return ret;
	}

	name = devm_kasprintf(sgm->dev, GFP_KERNEL, "%s", "sgm4154x suspend wakelock");
	sgm->charger_wakelock =	wakeup_source_register(sgm->dev, name);

	/* Register charger device */
	sgm->chg_dev = charger_device_register("primary_chg",
				&client->dev, sgm,
				&sgm4154x_chg_ops,
				&sgm4154x_chg_props);

	if (IS_ERR_OR_NULL(sgm->chg_dev)) {
		pr_info("register charger device failed\n");
		ret = PTR_ERR(sgm->chg_dev);
		return ret;
	}

	sgm->battery_full = false;
	sgm->pd_type_detected = false;
	/* otg regulator */
	s_chg_dev_otg = sgm->chg_dev;

	INIT_DELAYED_WORK(&sgm->charge_detect_delayed_work, charger_detect_work_func);
	//INIT_DELAYED_WORK(&sgm->charge_monitor_work, charger_monitor_work_func);
	INIT_DELAYED_WORK(&sgm->retry_charger_detect_work, retry_charger_detect_work_func);

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
				sgm4154x_irq_handler_thread,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				dev_name(&client->dev), sgm);
		if (ret) {
			pr_err("request irq failed\n");
			return ret;
		}
		enable_irq_wake(client->irq);
	}

	ret = sgm4154x_power_supply_init(sgm, dev);
	if (ret) {
		pr_err("Failed to register power supply\n");
		return ret;
	}

	ret = sgm4154x_hw_init(sgm);
	if (ret) {
		pr_err("Cannot initialize the chip.\n");
		return ret;
	}

	dump_reg_enable = true;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	entry = proc_create("dump_reg_ctrl", 0664, NULL, &dump_reg_ctrl_fops);
	if (!entry) {
		pr_err("create proc directory failed\n");
	}
#endif

	ret = sgm4154x_create_device_node(&(client->dev));

	//ret = sgm4154x_vbus_regulator_register(sgm);

	//schedule_delayed_work(&sgm->charge_monitor_work, msecs_to_jiffies(100));

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	FULL_PRODUCT_DEVICE_INFO(ID_SWITCH_CHARGER, "SGM41543D");
#endif
/*TN Begin modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
	sgm4154x_irq_handler_thread(client->irq, (void *)sgm);
/*TN End modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
	pr_info("successfully\n");
	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static void sgm4154x_charger_remove(struct i2c_client *client)
#else
static int sgm4154x_charger_remove(struct i2c_client *client)
#endif
{
	struct sgm4154x_device *sgm = i2c_get_clientdata(client);

	//cancel_delayed_work_sync(&sgm->charge_monitor_work);

	//regulator_unregister(sgm->otg_rdev);

	power_supply_unregister(sgm->charger);

	sgm4154x_destory_device_node(sgm->dev);
	mutex_destroy(&sgm->lock);
	mutex_destroy(&sgm->i2c_rw_lock);

#if !(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	return 0;
#endif
}

static void sgm4154x_charger_shutdown(struct i2c_client *client)
{
	int ret = 0;
	struct sgm4154x_device *sgm = i2c_get_clientdata(client);

	ret = sgm4154x_disable_charger(sgm);
	if (ret) {
		pr_err("Failed to disable charger, ret(%d)\n", ret);
	}

	ret = sgm4154x_reset_registers(sgm);
	if (ret) {
		pr_err("Failed to reset registers, ret(%d)\n", ret);
	}

	pr_info("sgm4154x_charger_shutdown\n");
}

static const struct i2c_device_id sgm4154x_i2c_ids[] = {
	{ "sgm41541", 0 },
	{ "sgm41542", 1 },
	{ "sgm41543", 2 },
	{ "sgm41543D", 3 },
	{ "sgm41513", 4 },
	{ "sgm41513A", 5 },
	{ "sgm41513D", 6 },
	{ "sgm41516", 7 },
	{ "sgm41516D", 8 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sgm4154x_i2c_ids);

static const struct of_device_id sgm4154x_of_match[] = {
	{ .compatible = "sgm,sgm41541", },
	{ .compatible = "sgm,sgm41542", },
	{ .compatible = "sgm,sgm41543", },
	{ .compatible = "sgm,sgm41543D", },
	{ .compatible = "sgm,sgm41513", },
	{ .compatible = "sgm,sgm41513A", },
	{ .compatible = "sgm,sgm41513D", },
	{ .compatible = "sgm,sgm41516", },
	{ .compatible = "sgm,sgm41516D", },
	{ },
};
MODULE_DEVICE_TABLE(of, sgm4154x_of_match);

static int sgm4154x_suspend(struct device *dev)
{
	struct sgm4154x_device *sgm = dev_get_drvdata(dev);

	pr_info("enter\n");
	if (device_may_wakeup(dev))
		enable_irq_wake(sgm->client->irq);
	disable_irq(sgm->client->irq);

	return 0;
}

static int sgm4154x_resume(struct device *dev)
{
	struct sgm4154x_device *sgm = dev_get_drvdata(dev);

	pr_info("enter\n");
	enable_irq(sgm->client->irq);
	if (device_may_wakeup(dev))
		disable_irq_wake(sgm->client->irq);

	return 0;
}

static const struct dev_pm_ops sgm4154x_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sgm4154x_suspend, sgm4154x_resume)
};

static struct i2c_driver sgm4154x_driver = {
	.driver = {
		.name = "sgm4154x-charger",
		.of_match_table = sgm4154x_of_match,
		.pm = &sgm4154x_pm_ops,
	},
	.probe = sgm4154x_driver_probe,
	.remove = sgm4154x_charger_remove,
	.shutdown = sgm4154x_charger_shutdown,
	.id_table = sgm4154x_i2c_ids,
};
module_i2c_driver(sgm4154x_driver);

MODULE_AUTHOR(" qhq <Allen_qin@sg-micro.com>");
MODULE_DESCRIPTION("sgm4154x charger driver");
MODULE_LICENSE("GPL v2");
