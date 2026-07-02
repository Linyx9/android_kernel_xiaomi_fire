// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2023 Suncore Corp.
 */
#define pr_fmt(fmt) "[cx2589x] %s: " fmt, __func__

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

#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/phy.h>
#include <linux/version.h>

#include "cx2589x_reg.h"

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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
#include <linux/phy/phy.h>
#define PHY_MODE_BC11_SET 1
#define PHY_MODE_BC11_CLR 2
#endif /* LINUX_VERSION_CODE */

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include <dev_info.h>
#endif

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
extern bool turbo_charger_active;
extern bool ffc_batt_full;
#endif

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#include <tinno_charger.h>
#endif /* CONFIG_OEM_TINNO_CHARGER */

static bool allow_set_dp_dm_vol = false;
static int cx2589x_set_boost_current_limit(struct charger_device *chg_dev, u32 uA);
static int cx2589x_enable_otg(struct charger_device *chg_dev, bool en);


/**********************************************************
 *
 *   [I2C Slave Setting]
 *
 *********************************************************/

#define CX2589x_REG_NUM		0x15
#define SINGLE_DUMP_LEN		19
#define TOTAL_DUMP_LEN		(SINGLE_DUMP_LEN * (CX2589x_REG_NUM))

#define R_VBUS_CHARGER_1   330
#define R_VBUS_CHARGER_2   39

#define BC12_RETRY_COUNT     10
#define UNKNOW_RETRY_COUNT    3

static struct proc_dir_entry *entry;
static bool dump_reg_enable;


static enum power_supply_usb_type cx2589x_usb_type[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
};

static const struct charger_properties cx2589x_chg_props = {
	.alias_name = CX2589x_NAME,
};

/**********************************************************
 *
 *   [Global Variable]
 *
 *********************************************************/
static struct power_supply_desc cx2589x_power_supply_desc;
static struct charger_device *s_chg_dev_otg;


/**********************************************************
 *
 *   [I2C Function For Read/Write cx2589x]
 *
 *********************************************************/
static int __cx2589x_read_byte(struct cx2589x_device *cx, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(cx->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from reg 0x%02X: %d\n", reg, ret);
		return ret;
	}

	*data = (u8)ret;

	return 0;
}

static int __cx2589x_read_byte_retry(struct cx2589x_device *cx, u8 reg, u8 *data)
{
	s32 ret;
	u8 i, retry_count;
	u8 tmp_val[2];

	for(retry_count = 0; retry_count < 3; retry_count++) {
		for (i = 0; i < 2; i++) {
	   		ret = i2c_smbus_read_byte_data(cx->client, reg);
			if (ret < 0){
				pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
				break;
			} else
				tmp_val[i] = ret;
		}
		if (ret >= 0) {
			if (tmp_val[0] == tmp_val[1]) {
				*data = tmp_val[0];
				return 0;
			}
		}
	}

	pr_err("i2c retry read fail: can't read from reg 0x%02X\n", reg);
	return -1;
}

static int __cx2589x_write_byte(struct cx2589x_device *cx, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(cx->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
			val, reg, ret);
		return ret;
	}
	return 0;
}

static int __cx2589x_write_byte_retry(struct cx2589x_device *cx, int reg, u8 val)
{
	s32 ret;
	u8 read_val = 0, retry_count;

	for (retry_count = 0; retry_count < 10; retry_count++) {
		ret = i2c_smbus_write_byte_data(cx->client, reg, val);
		if (ret < 0) {
			pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n", val, reg, ret);
		} else {
			read_val = i2c_smbus_read_byte_data(cx->client, reg);
			if (val == read_val)
				break;
			else {
				if (retry_count == 9) {
					 pr_err("i2c retry write fail: can't write 0x%02X to reg 0x%02X: %d\n", val, reg, ret);
					 return ret;
				}
			}
		}
	}

	return 0;
}

static int cx2589x_read_reg(struct cx2589x_device *cx, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&cx->i2c_rw_lock);
	ret = __cx2589x_read_byte(cx, reg, data);
	mutex_unlock(&cx->i2c_rw_lock);

	if (ret)
		pr_err("Failed: read reg=%02X, ret=%d\n", reg, ret);

	return ret;
}

static int cx2589x_read_reg_retry(struct cx2589x_device *cx, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&cx->i2c_rw_lock);
	ret = __cx2589x_read_byte_retry(cx, reg, data);
	mutex_unlock(&cx->i2c_rw_lock);

	if (ret)
		pr_err("Failed: read reg=%02X, ret=%d\n", reg, ret);

	return ret;
}

__maybe_unused static int cx2589x_write_reg(struct cx2589x_device *cx, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&cx->i2c_rw_lock);
	ret = __cx2589x_write_byte(cx, reg, val);
	mutex_unlock(&cx->i2c_rw_lock);

	if (ret)
		pr_err("Failed: write reg=%02X, ret=%d\n", reg, ret);

	return ret;
}

__maybe_unused static int cx2589x_write_reg_retry(struct cx2589x_device *cx, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&cx->i2c_rw_lock);
	ret = __cx2589x_write_byte_retry(cx, reg, val);
	mutex_unlock(&cx->i2c_rw_lock);

	if (ret)
		pr_err("Failed: write reg=%02X, ret=%d\n", reg, ret);

	return ret;
}


static int cx2589x_update_bits(struct cx2589x_device *cx, u8 reg,
	u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	mutex_lock(&cx->i2c_rw_lock);
	ret = __cx2589x_read_byte(cx, reg, &tmp);
	if (ret) {
		pr_err("Failed: reg=0x%x, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = __cx2589x_write_byte(cx, reg, tmp);
	if (ret)
		pr_err("Failed: reg=0x%x, ret=%d\n", reg, ret);

out:
	mutex_unlock(&cx->i2c_rw_lock);
	return ret;
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/

static int cx2589x_set_watchdog_timer(struct cx2589x_device *cx, int time)
{
	int ret;
	u8 reg_val;

	if (time == 0)
		reg_val = CX2589x_WDT_TIMER_DISABLE;
	else if (time == 40)
		reg_val = CX2589x_WDT_TIMER_40S;
	else if (time == 80)
		reg_val = CX2589x_WDT_TIMER_80S;
	else
		reg_val = CX2589x_WDT_TIMER_160S;

	ret = cx2589x_update_bits(cx, CX2589x_REG_07, CX2589x_WDT_TIMER_MASK, reg_val);

	return ret;
}

__maybe_unused static int cx2589x_get_term_curr(struct cx2589x_device *cx)
{
	int ret;
	u8 reg_val;
	int curr;
	int offset = CX2589x_TERMCHRG_I_MIN_uA;

	ret = cx2589x_read_reg(cx, CX2589x_REG_05, &reg_val);
	if (ret)
		return ret;

	reg_val &= CX2589x_TERMCHRG_CUR_MASK;
	if (reg_val <= CX2589x_TERMCHRG_335mA) {
		curr = reg_val * CX2589x_TERMCHRG_CURRENT_STEP1_uA + offset;
	} else {
		offset = CX2589x_TERMCHRG_I_MIDDLE_uA;
		curr = (reg_val - CX2589x_TERMCHRG_335mA) * CX2589x_TERMCHRG_CURRENT_STEP2_uA + offset;
	}

	return curr;
}

__maybe_unused static int cx2589x_get_prechrg_curr(struct cx2589x_device *cx)
{
	int ret;
	u8 reg_val;
	int curr;
	int offset = CX2589x_PRECHRG_I_MIN_uA;

	ret = cx2589x_read_reg(cx, CX2589x_REG_05, &reg_val);
	if (ret)
		return ret;

	reg_val = (reg_val&CX2589x_PRECHRG_CUR_MASK) >> 4;
	curr = reg_val * CX2589x_PRECHRG_CURRENT_STEP1_uA + offset;

	if (reg_val <= CX2589x_PRECHRG_337mA) {
		curr = reg_val * CX2589x_PRECHRG_CURRENT_STEP1_uA + offset;
	} else {
		offset = CX2589x_PRECHRG_I_MIDDLE_uA;
		curr = (reg_val - CX2589x_PRECHRG_337mA) * CX2589x_PRECHRG_CURRENT_STEP2_uA + offset;
	}


	return curr;
}


/*
Termination Current Limit
0000 - 0101: 40mA - 335mA, step=59mA
0110 - 1011: 400mA - 725mA, step=65mA
ITERM > 725mA is not defined
Default: 217mA (0011)
0000 - 40mA, 0001 - 99mA, 0010 - 158mA, 0011 - 217mA
0100 - 276mA, 0101 - 335mA, 0110 - 400mA, 0111 - 465mA
1000 - 530mA, 1001 - 595mA, 1010 - 660mA, 1011 - 725mA
*/
static int cx2589x_set_term_curr(struct charger_device *chg_dev, u32 uA)
{
	u8 reg_val;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	if (uA < CX2589x_TERMCHRG_I_MIN_uA)
		uA = CX2589x_TERMCHRG_I_MIN_uA;
	else if (uA > CX2589x_TERMCHRG_I_MAX_uA)
		uA = CX2589x_TERMCHRG_I_MAX_uA;

	if (uA <= CX2589x_TERMCHRG_I_MIDDLE_uA) {
		reg_val = (uA - CX2589x_TERMCHRG_I_MIN_uA + (CX2589x_TERMCHRG_CURRENT_STEP1_uA / 2))
			/ CX2589x_TERMCHRG_CURRENT_STEP1_uA;
	} else {
		reg_val = (uA - CX2589x_TERMCHRG_I_MIDDLE_uA + (CX2589x_TERMCHRG_CURRENT_STEP2_uA / 2))
			/ CX2589x_TERMCHRG_CURRENT_STEP2_uA + CX2589x_TERMCHRG_335mA;
	}
	pr_info("set iterm uA=%d uA, reg_val=0x%x\n", uA, reg_val);

	return cx2589x_update_bits(cx, CX2589x_REG_05, CX2589x_TERMCHRG_CUR_MASK, reg_val);
}

static int cx2589x_set_chg_term(struct cx2589x_device *cx, bool en)
{
	int reg_val = -1;

	reg_val = en <<  7;
	return cx2589x_update_bits(cx, CX2589x_REG_07,
					CX2589x_TERM_EN, reg_val);
}

static int cx2589x_enable_terminate(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct cx2589x_device *cx = dev_get_drvdata(&chg_dev->dev);

	ret = cx2589x_set_chg_term(cx, en);
	if (ret < 0)
		pr_err("failed ret(%d)\n", ret);

	return ret;
}

/*
Precharge Current Limit
0000 - 0101: 52mA - 337mA, step=57mA
0110 - 1011: 401mA - 721mA, step=64mA
IPRECHG > 721mA is not defined.
Default: 109mA (0001)
0000 - 52mA, 0001 - 109mA, 0010 - 166mA, 0011 - 223mA
0100 - 280mA, 0101 - 337mA, 0110 - 401mA, 0111 - 465mA
1000 - 529mA, 1001 - 593mA, 1010 - 657mA, 1011 - 721mA
*/
__maybe_unused static int cx2589x_set_prechrg_curr(struct cx2589x_device *cx, int uA)
{
	u8 reg_val;


	if (uA < CX2589x_PRECHRG_I_MIN_uA)
		uA = CX2589x_PRECHRG_I_MIN_uA;
	else if (uA > CX2589x_PRECHRG_I_MAX_uA)
		uA = CX2589x_PRECHRG_I_MAX_uA;

	if (uA <= CX2589x_PRECHRG_I_MIDDLE_uA) {
		reg_val = (uA - CX2589x_PRECHRG_I_MIN_uA + (CX2589x_PRECHRG_CURRENT_STEP1_uA / 2))
			/ CX2589x_PRECHRG_CURRENT_STEP1_uA;
	} else {
		reg_val = (uA - CX2589x_PRECHRG_I_MIDDLE_uA + (CX2589x_PRECHRG_CURRENT_STEP2_uA / 2))
			/ CX2589x_PRECHRG_CURRENT_STEP2_uA + CX2589x_PRECHRG_337mA;
	}
	reg_val = reg_val << 4;
	pr_info("set prechrg uA=%d uA, reg_val=0x%x\n", uA, reg_val);

	return cx2589x_update_bits(cx, CX2589x_REG_05, CX2589x_PRECHRG_CUR_MASK, reg_val);
}

static int cx2589x_get_ichg_curr(struct charger_device *chg_dev, u32 *uA)
{
	int ret;
	u8 ichg;
	u32 curr;

	struct cx2589x_device *cx = charger_get_data(chg_dev);

	ret = cx2589x_read_reg(cx, CX2589x_REG_04, &ichg);
	if (ret)
		return ret;

	ichg &= CX2589x_ICHRG_I_MASK;

	curr = ichg * CX2589x_ICHRG_I_STEP_uA;

	*uA = curr;

	return 0;
}

static int cx2589x_get_minichg_curr(struct charger_device *chg_dev, u32 *uA)
{
	*uA = CX2589x_ICHRG_I_MIN_uA;
	return 0;
}

static int cx2589x_set_ichrg_curr(struct charger_device *chg_dev, unsigned int uA)
{
	int ret;
	u8 reg_val;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	pr_info("%d uA\n", uA);

	if (uA < CX2589x_ICHRG_I_MIN_uA)
		uA = CX2589x_ICHRG_I_MIN_uA;
	else if (uA > cx->init_data.max_ichg)
		uA = cx->init_data.max_ichg;

	reg_val = uA / CX2589x_ICHRG_I_STEP_uA;

	ret = cx2589x_update_bits(cx, CX2589x_REG_04,
			CX2589x_ICHRG_I_MASK, reg_val);

	return ret;
}

static int cx2589x_set_chrg_volt(struct charger_device *chg_dev, u32 chrg_volt)
{
	int ret;
	u8 reg_val;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	if (chrg_volt < CX2589x_VREG_V_MIN_uV)
		chrg_volt = CX2589x_VREG_V_MIN_uV;
	else if (chrg_volt > cx->init_data.max_vreg)
		chrg_volt = cx->init_data.max_vreg;

	reg_val = (chrg_volt - CX2589x_VREG_V_MIN_uV) / CX2589x_VREG_V_STEP_uV;
	reg_val = reg_val << 2;
	ret = cx2589x_update_bits(cx, CX2589x_REG_06, CX2589x_VREG_V_MASK, reg_val);

	return ret;
}

static int cx2589x_get_chrg_volt(struct charger_device *chg_dev,unsigned int *volt)
{
	int ret;
	u8 vreg_val;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	ret = cx2589x_read_reg(cx, CX2589x_REG_06, &vreg_val);
	if (ret)
		return ret;

	vreg_val = (vreg_val & CX2589x_VREG_V_MASK) >> 2;


	*volt = vreg_val * CX2589x_VREG_V_STEP_uV + CX2589x_VREG_V_MIN_uV;

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
__maybe_unused static int charger_detect_init(struct cx2589x_device *cx)
{
	struct phy *phy;
	int ret;

	pr_info("enter\n");
	phy = phy_get(cx->dev, "usb2-phy");
	if (IS_ERR_OR_NULL(phy)) {
		pr_err("failed to get usb2-phy\n");
		return -ENODEV;
	}

	ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, PHY_MODE_BC11_SET);
	if (ret)
		pr_err("failed to set phy ext mode\n");

	phy_put(cx->dev, phy);
	return ret;
}

__maybe_unused static int charger_detect_release(struct cx2589x_device *cx)
{
	struct phy *phy;
	int ret;

	pr_info("enter\n");
	phy = phy_get(cx->dev, "usb2-phy");
	if (IS_ERR_OR_NULL(phy)) {
		pr_err("failed to get usb2-phy\n");
		return -ENODEV;
	}

	ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, PHY_MODE_BC11_CLR);
	if (ret)
		pr_err("failed to set phy ext mode\n");

	phy_put(cx->dev, phy);
	return ret;
}
#endif

static int cx2589x_force_vindpm(struct cx2589x_device *cx, bool en)
{
	return cx2589x_update_bits(cx, CX2589x_REG_0D,
			CX2589x_FORCE_VINDPM_MASK, (u8)en << 7);
}

static int cx2589x_force_dpdm(struct cx2589x_device *cx)
{
	int ret;

	pr_info("enter\n");

	cx2589x_update_bits(cx, CX2589x_REG_15, CX2589x_DP_VSEL_MASK, 0x1 << 5);  //dp=0v
	cx2589x_update_bits(cx, CX2589x_REG_15, CX2589x_DM_VSEL_MASK, 0x1 << 2);  //dm=0v
	msleep(100);
	cx2589x_update_bits(cx, CX2589x_REG_15, CX2589x_DP_VSEL_MASK, 0);  //dp=hiz
	cx2589x_update_bits(cx, CX2589x_REG_15, CX2589x_DM_VSEL_MASK, 0);  //dm=hiz

	ret = cx2589x_update_bits(cx, CX2589x_REG_02, CX2589x_FORCE_DPDM_MASK, CX2589x_FORCE_DPDM_MASK);

	return ret;
}

static void retry_charger_detect_work_func(struct work_struct *work)
{
	struct cx2589x_device *cx = NULL;
	int ret;

	cx = container_of(work, struct cx2589x_device, retry_charger_detect_work.work);
	if (IS_ERR_OR_NULL(cx)) {
		pr_err("Cann't get cx2589x_device\n");
		return;
	}

	Charger_Detect_Init();

	ret = cx2589x_force_dpdm(cx);
	if (ret < 0) {
		pr_err("Cann't force dpdm\n");
		return;
	}

	cx->force_detect_count++;
	schedule_delayed_work(&cx->charger_type_detect_work, msecs_to_jiffies(500));

	return;
}

static int cx2589x_set_input_volt_lim(struct charger_device *chg_dev, unsigned int vindpm)
{
	int ret;
	u8 reg_val;

	struct cx2589x_device *cx = charger_get_data(chg_dev);

	if (vindpm < CX2589x_VINDPM_V_MIN_uV)
		vindpm = CX2589x_VINDPM_V_MIN_uV;
	else if (vindpm > CX2589x_VINDPM_V_MAX_uV)
		vindpm = CX2589x_VINDPM_V_MAX_uV;


	reg_val = (vindpm - CX2589x_VINDPM_V_OFFSET_uV) / CX2589x_VINDPM_STEP_uV;

	cx2589x_force_vindpm(cx, true);
	ret = cx2589x_update_bits(cx, CX2589x_REG_0D, CX2589x_VINDPM_V_MASK, reg_val);

	return ret;
}

static int cx2589x_get_input_volt_lim(struct charger_device *chg_dev, u32 *uV)
{
	int ret;
	u8 vlim;

	struct cx2589x_device *cx = charger_get_data(chg_dev);
	ret = cx2589x_read_reg(cx, CX2589x_REG_0D, &vlim);
	if (ret)
		return ret;

	cx2589x_force_vindpm(cx, true);

	*uV = CX2589x_VINDPM_V_OFFSET_uV + (vlim & CX2589x_VINDPM_V_MASK) * CX2589x_VINDPM_STEP_uV;
	if (*uV < CX2589x_VINDPM_V_MIN_uV)
		*uV = CX2589x_VINDPM_V_MIN_uV;

	return 0;
}

__maybe_unused static int cx2589x_get_input_minvolt_lim(struct charger_device *chg_dev, u32 *uV)
{
	*uV = CX2589x_VINDPM_V_MIN_uV;

	return 0;
}

#define CX2589x_DCP_CURRENT 2200000
#define CX2589x_DCP_CURRENT_OFFSET 200000
static int cx2589x_set_input_curr_lim(struct charger_device *chg_dev, unsigned int iindpm)
{
	int ret;
	u8 reg_val;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	pr_info("%d uA\n", iindpm);

	if (iindpm < CX2589x_IINDPM_I_MIN_uA)
		iindpm = CX2589x_IINDPM_I_MIN_uA;
	else if (iindpm > CX2589x_IINDPM_I_MAX_uA)
		iindpm = CX2589x_IINDPM_I_MAX_uA;

	if (iindpm == CX2589x_DCP_CURRENT) {
		iindpm += CX2589x_DCP_CURRENT_OFFSET;
		pr_info("DCP type re-set icl to %d uA\n", iindpm);
	}
	reg_val = (iindpm - CX2589x_IINDPM_I_MIN_uA) / CX2589x_IINDPM_STEP_uA;

	ret = cx2589x_update_bits(cx, CX2589x_REG_00, CX2589x_IINDPM_I_MASK, reg_val);

	return ret;
}

static int cx2589x_get_input_curr_lim(struct charger_device *chg_dev,unsigned int *ilim)
{
	int ret;
	u8 reg_val;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	ret = cx2589x_read_reg(cx, CX2589x_REG_00, &reg_val);
	if (ret)
		return ret;


	*ilim = (reg_val & CX2589x_IINDPM_I_MASK) * CX2589x_IINDPM_STEP_uA + CX2589x_IINDPM_I_MIN_uA;

	return 0;
}

static int cx2589x_get_input_mincurr_lim(struct charger_device *chg_dev,u32 *ilim)
{
	*ilim = CX2589x_IINDPM_I_MIN_uA;

	return 0;
}

static int cx2589x_get_state(struct cx2589x_device *cx, struct cx2589x_state *state)
{
	u8 chrg_stat, therm_stat;
	u8 fault;
	u8 chrg_param_0, chrg_param_1, chrg_param_2;
	int ret;

	//ret = cx2589x_read_reg(cx, CX2589x_REG_0B, &chrg_stat);
	ret = cx2589x_read_reg_retry(cx, CX2589x_REG_0B, &chrg_stat);
	if (ret) {
		//ret = cx2589x_read_reg(cx, CX2589x_REG_0B, &chrg_stat);
		ret = cx2589x_read_reg_retry(cx, CX2589x_REG_0B, &chrg_stat);
		if (ret) {
			pr_err("read CX2589x_REG_0B fail\n");
			return ret;
		}
	}

	state->chrg_type = chrg_stat & CX2589x_VBUS_STAT_MASK;
	state->chrg_stat = chrg_stat & CX2589x_CHG_STAT_MASK;
	state->online = !!(chrg_stat & CX2589x_PG_STAT);
	state->vsys_stat = !!(chrg_stat & CX2589x_VSYS_STAT);

	ret = cx2589x_read_reg(cx, CX2589x_REG_0E, &therm_stat);
	if (ret) {
		pr_err("read CX2589x_REG_0E fail\n");
		return ret;
	}
	state->therm_stat = !!(therm_stat & CX2589x_THERM_STAT);

	pr_info("chrg_type:0x%x, chrg_stat:0x%x, online:%d\n",
		state->chrg_type >> 5, state->chrg_stat >> 3, state->online);

	ret = cx2589x_read_reg(cx, CX2589x_REG_0C, &fault);
	if (ret) {
		pr_err("read CX2589x_REG_0C fail\n");
		return ret;
	}

	state->chrg_fault = fault &CX2589x_CHG_FAULT_MASK;
	state->ntc_fault = fault & CX2589x_TEMP_MASK;
	state->health = state->ntc_fault;
	ret = cx2589x_read_reg(cx, CX2589x_REG_00, &chrg_param_0);
	if (ret) {
		pr_err("read CX2589x_REG_00 fail\n");
		return ret;
	}
	state->hiz_en = !!(chrg_param_0 & CX2589x_HIZ_EN);

	ret = cx2589x_read_reg(cx, CX2589x_REG_07, &chrg_param_1);
	if (ret) {
		pr_err("read CX2589x_REG_07 fail\n");
		return ret;
	}
	state->term_en = !!(chrg_param_1 & CX2589x_TERM_EN);

	ret = cx2589x_read_reg(cx, CX2589x_REG_11, &chrg_param_2);
	if (ret) {
		pr_err("read CX2589x_REG_11 fail\n");
		return ret;
	}
	state->vbus_gd = !!(chrg_param_2 & CX2589x_VBUS_GOOD);

	return 0;
}

static int cx2589x_get_charge_stat(struct cx2589x_device *cx)
{
	u8 chrg_stat;
	int ret;
	int status = POWER_SUPPLY_STATUS_UNKNOWN;

	ret = cx2589x_read_reg(cx, CX2589x_REG_0B, &chrg_stat);
	if (ret) {
		pr_err("read CX2589x_CHRG_STAT fail\n");
		return status;
	}

	mutex_lock(&cx->lock);
	cx->state.chrg_type = chrg_stat & CX2589x_VBUS_STAT_MASK;
	cx->state.chrg_stat = chrg_stat & CX2589x_CHG_STAT_MASK;
	mutex_unlock(&cx->lock);

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	pr_info("chrg_type:0x%x, chrg_stat:0x%x, turbo_charger_active:%d , ffc_batt_full:%d\n",
		cx->state.chrg_type, cx->state.chrg_stat, turbo_charger_active, ffc_batt_full);
#else
	pr_info("chrg_type:0x%x, chrg_stat:0x%x\n",
		cx->state.chrg_type, cx->state.chrg_stat);
#endif

	if (!cx->state.chrg_type || cx->state.chrg_type == CX2589x_OTG_MODE) {
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else {
		switch (cx->state.chrg_stat) {
		case CX2589x_NOT_CHRGING:
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		case CX2589x_PRECHRG:
		case CX2589x_FAST_CHRG:
			status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case CX2589x_TERM_CHRG:
			status = POWER_SUPPLY_STATUS_FULL;
			break;
		}
	}

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	if ((turbo_charger_active == true) && (ffc_batt_full == true))
		status = POWER_SUPPLY_STATUS_FULL;
#endif

	if (cx->battery_full)
		status = POWER_SUPPLY_STATUS_FULL;

	return status;
}

static int cx2589x_set_hiz_en(struct charger_device *chg_dev, bool hiz_en)
{
	u8 reg_val;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	pr_info("set %s\n", hiz_en ? "enable" : "disable");

	reg_val = hiz_en ? CX2589x_HIZ_EN : 0;

	return cx2589x_update_bits(cx, CX2589x_REG_00, CX2589x_HIZ_EN, reg_val);
}

__maybe_unused static int cx2589x_reset_chip(struct cx2589x_device *cx)
{
	int ret;

	ret = cx2589x_update_bits(cx, CX2589x_REG_14, CX2589x_RESET_REG, CX2589x_RESET_REG);

	return ret;
}


static int cx2589x_enable_charger(struct cx2589x_device *cx)
{
	int ret;

	pr_info("enter\n");

	ret = cx2589x_update_bits(cx, CX2589x_REG_03, CX2589x_CHRG_EN, CX2589x_CHRG_EN);

	return ret;
}

static int cx2589x_disable_charger(struct cx2589x_device *cx)
{
	int ret;

	pr_info("enter\n");

	ret = cx2589x_update_bits(cx, CX2589x_REG_03, CX2589x_CHRG_EN, 0);

	return ret;
}

static int cx2589x_is_charging(struct charger_device *chg_dev,bool *en)
{
	int ret;
	u8 val;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	ret = cx2589x_read_reg(cx, CX2589x_REG_03, &val);
	if (ret) {
		pr_err("read CX2589x_REG_03 fail\n");
		return ret;
	}
	*en = (val & CX2589x_CHRG_EN) ? 1 : 0;

	return ret;
}

static int cx2589x_charging_switch(struct charger_device *chg_dev,bool enable)
{
	int ret;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	if (enable)
		ret = cx2589x_enable_charger(cx);
	else
		ret = cx2589x_disable_charger(cx);

	return ret;
}

static int cx2589x_set_recharge_volt(struct cx2589x_device *cx, int mV)
{
	u8 reg_val;

	reg_val = (mV - CX2589x_VRECHRG_OFFSET_mV) / CX2589x_VRECHRG_STEP_mV;

	return cx2589x_update_bits(cx, CX2589x_REG_06, CX2589x_VRECHARGE, reg_val);
}

static int cx2589x_set_wdt_rst(struct cx2589x_device *cx, bool is_rst)
{
	u8 val;

	if (is_rst)
		val = CX2589x_WDT_RST_MASK;
	else
		val = 0;

	return cx2589x_update_bits(cx, CX2589x_REG_03, CX2589x_WDT_RST_MASK, val);
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int cx2589x_dump_register(struct charger_device *chg_dev)
{

	unsigned char i = 0;
	unsigned int ret = 0;
	unsigned char cx2589x_reg[CX2589x_REG_NUM + 1] = { 0 };
	struct cx2589x_device *cx = charger_get_data(chg_dev);
	char reg_buff[TOTAL_DUMP_LEN] = {0};
	char temp_buff[SINGLE_DUMP_LEN] = {0};

	if (dump_reg_enable) {
		for (i = 0; i < CX2589x_REG_NUM + 1; i++) {
			ret = cx2589x_read_reg(cx, i, &cx2589x_reg[i]);
			if (ret != 0) {
				pr_err("i2c transfor error\n");
				return ret;
			}
			snprintf(temp_buff, SINGLE_DUMP_LEN, "reg[0x%02x]=0x%02x ", i, cx2589x_reg[i]);
			strcat(reg_buff, temp_buff);
		}
		pr_info("%s", reg_buff);
	} else {
		pr_err("dump register has been disabled\n");
	}

	return 0;
}

/**********************************************************
 *
 *   [Internal Function]
 *
 *********************************************************/
static int cx2589x_hw_chipid_detect(struct cx2589x_device *cx)
{
	int ret = 0;
	u8 val = 0;

	ret = cx2589x_read_reg(cx,CX2589x_REG_14, &val);
	if (ret < 0) {
		pr_info("read CX2589x_REG_14 fail\n");
		return ret;
	}

	val = val & CX2589x_PN_MASK;
	pr_info("Reg[0x14]=0x%x\n", val);

	return val;
}

static int cx2589x_reset_watch_dog_timer(struct charger_device *chg_dev)
{
	int ret;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	pr_info("+++\n");

	ret = cx2589x_set_wdt_rst(cx, true);	/* RST watchdog */

	return ret;
}

static int cx2589x_get_charging_status(struct charger_device *chg_dev, bool *is_done)
{
	//struct cx2589x_state state;
	struct cx2589x_device *cx = charger_get_data(chg_dev);
	//cx2589x_get_state(cx, &state);

	if (cx->state.chrg_stat == CX2589x_TERM_CHRG)
		*is_done = true;
	else
		*is_done = false;

	if (cx->battery_full)
		*is_done = true;
	else
		*is_done = false;

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	if ((turbo_charger_active == true) && (ffc_batt_full == true))
		*is_done = true;
#endif

	return 0;
}

static int cx2589x_set_en_timer(struct cx2589x_device *cx)
{
	int ret;

	ret = cx2589x_update_bits(cx, CX2589x_REG_07, CX2589x_SAFETY_TIMER_EN, CX2589x_SAFETY_TIMER_EN);

	return ret;
}

static int cx2589x_set_disable_timer(struct cx2589x_device *cx)
{
	int ret;

	ret = cx2589x_update_bits(cx, CX2589x_REG_07, CX2589x_SAFETY_TIMER_EN, 0);

	return ret;
}

static int cx2589x_enable_safetytimer(struct charger_device *chg_dev, bool en)
{
	struct cx2589x_device *cx = charger_get_data(chg_dev);
	int ret = 0;

	if (en)
		ret = cx2589x_set_en_timer(cx);
	else
		ret = cx2589x_set_disable_timer(cx);

	return ret;
}

static int cx2589x_get_is_safetytimer_enable(struct charger_device *chg_dev, bool *en)
{
	int ret = 0;
	u8 val = 0;

	struct cx2589x_device *cx = charger_get_data(chg_dev);

	ret = cx2589x_read_reg(cx, CX2589x_REG_07, &val);
	if (ret < 0) {
		pr_info("read CX2589x_REG_07 fail\n");
		return ret;
	}

	*en = !!(val & CX2589x_SAFETY_TIMER_EN);

	return 0;
}


static int cx2589x_en_pe_current_partern(struct charger_device *chg_dev, bool is_up)
{
	int ret = 0;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	ret = cx2589x_update_bits(cx, CX2589x_REG_04, CX2589x_EN_PUMPX, CX2589x_EN_PUMPX);
	if (ret < 0) {
		pr_info("read CX2589x_REG_04 fail\n");
		return ret;
	}

	if (is_up)
		ret = cx2589x_update_bits(cx, CX2589x_REG_09, CX2589x_PUMPX_UP, CX2589x_PUMPX_UP);
	else
		ret = cx2589x_update_bits(cx, CX2589x_REG_09, CX2589x_PUMPX_DN, CX2589x_PUMPX_DN);
	return ret;
}

static int cx2589x_set_dpdm_hiz(struct cx2589x_device *cx)
{
	int ret;

	ret = cx2589x_update_bits(cx, CX2589x_REG_15, CX2589x_DP_VSEL_MASK, 0);
	if (ret < 0)
		pr_err("set dp hiz failed, ret(%d)\n", ret);

	ret = cx2589x_update_bits(cx, CX2589x_REG_15, CX2589x_DM_VSEL_MASK, 0);
	if (ret < 0)
		pr_err("set dm hiz failed, ret(%d)\n", ret);

	return ret;
}

static int cx2589x_get_vbus(struct cx2589x_device *cx, int *vbus_volt)
{
	int ret;
	int value;

	ret = iio_read_channel_processed(cx->vbus, &value);
	if (ret < 0) {
		pr_info("get vbus voltage failed");
		return ret;
	}

	*vbus_volt = value + R_VBUS_CHARGER_1 * value / R_VBUS_CHARGER_2;

	pr_info("vbus voltage: %d", *vbus_volt);

	return ret;
}

static enum power_supply_property cx2589x_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_TYPE,
	//POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_PRESENT
};

static int cx2589x_property_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	//case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		return true;
	default:
		return false;
	}
}

static int cx2589x_charger_set_property(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	struct cx2589x_device *cx = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval == 2) {
			pr_info("attach is %d, start charger detection %d\n", val->intval, cx->state.hiz_en);
			cx->typec_attached = true;
			if (!cx->fake_sdp_type) {
				schedule_delayed_work(&cx->charger_type_detect_work, msecs_to_jiffies(300));
			} else {
				/*
				 * due to pd phy will generate 2 interrupts when plug in very slowly.
				 * need force dpdm for new bc1.2 detection for clearing the fake sdp type.
				 */
				pr_info("force dpdm for plug in slowly\n");
				schedule_delayed_work(&cx->retry_charger_detect_work, msecs_to_jiffies(50));
			}
		} else if (val->intval == 0) {
			pr_info("attach is %d, vbus not online\n", val->intval);
			cx->typec_attached = false;
			cx->pd_type_detected = false;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
			cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
#endif
			cx->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			/*
			 * if usb cable being plug out between driver probe done and healthd service init done.
			 * healthd service will ignore all the events of switch charger as the desc type of switch charger is being set to unknown.
			 * so we can't set the default desc type of switch charger to unknown.
			 */
			cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_TYPE_C;

			/*
			 * due to we set Auto DPDM enable func to disable when detecting the DCP.
			 * we should set it back to enable when plug out the charger for next time auto detect.
			 */
			cx2589x_update_bits(cx, CX2589x_REG_02, CX2589x_AUTO_DPDM_MASK, 1);
			cancel_delayed_work(&cx->charger_type_detect_work);
			cancel_delayed_work(&cx->unknow_charger_type_detect_work);
			cancel_delayed_work(&cx->retry_charger_detect_work);
			power_supply_changed(cx->charger);
		} else if (val->intval == 5) {
			pr_info("attach is %d, PD type is ATTACH_TYPE_PD_DCP\n", val->intval);
			cx->pd_type_detected = true;
			cx->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
			cx->psy_usb_type = POWER_SUPPLY_TYPE_USB_PD_DCP;
#endif
			cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
			power_supply_changed(cx->charger);
		}
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = cx2589x_set_input_curr_lim(s_chg_dev_otg, val->intval);
		break;
/*	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		cx2589x_charging_switch(s_chg_dev_otg,val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = cx2589x_set_input_volt_lim(s_chg_dev_otg, val->intval);
		break;*/
	default:
		return -EINVAL;
	}

	return ret;
}

static int cx2589x_charger_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct cx2589x_device *cx = power_supply_get_drvdata(psy);
	struct cx2589x_state state;
	int ret = 0;
	int value = 0;

	mutex_lock(&cx->lock);
	//ret = cx2589x_get_state(cx, &state);
	state = cx->state;
	mutex_unlock(&cx->lock);
	if (ret)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = cx2589x_get_charge_stat(cx);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		switch (state.chrg_stat) {
		case CX2589x_PRECHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case CX2589x_FAST_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
			break;
		case CX2589x_TERM_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case CX2589x_NOT_CHRGING:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		}
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = CX2589x_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = CX2589x_NAME;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = state.online;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = state.vbus_gd;
		break;

	case POWER_SUPPLY_PROP_TYPE:
		val->intval = cx2589x_power_supply_desc.type;
		break;

	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = cx->psy_usb_type;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (state.chrg_fault & 0xF8)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;

		switch (state.health) {
		case CX2589x_TEMP_HOT:
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
			break;
		case CX2589x_TEMP_WARM:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			break;
		case CX2589x_TEMP_COOL:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			break;
		case CX2589x_TEMP_COLD:
			val->intval = POWER_SUPPLY_HEALTH_COLD;
			break;
		}
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = cx2589x_get_vbus(cx, &value);
		val->intval = value;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		//val->intval = state.ibus_adc;
		break;

/*	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = cx2589x_get_input_volt_lim(cx);
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
		val->intval = cx->batt_vol * 1000;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}


__maybe_unused static bool cx2589x_state_changed(struct cx2589x_device *cx,
		struct cx2589x_state *new_state)
{
	struct cx2589x_state old_state;

	mutex_lock(&cx->lock);
	old_state = cx->state;
	mutex_unlock(&cx->lock);

	return (old_state.chrg_type != new_state->chrg_type ||
		old_state.chrg_stat != new_state->chrg_stat ||
		old_state.online != new_state->online ||
		old_state.therm_stat != new_state->therm_stat ||
		old_state.vsys_stat != new_state->vsys_stat ||
		old_state.chrg_fault != new_state->chrg_fault
		);
}

static int update_battery_info_from_gauge(struct cx2589x_device *cx)
{
	int ret = 0;
	union power_supply_propval info;

	if (IS_ERR_OR_NULL(cx->battery)) {
		cx->battery = power_supply_get_by_name("battery");
		if (IS_ERR_OR_NULL(cx->battery)) {
			pr_err("failed to get battery supply\n");
		}
		return -EINVAL;
	}

	/*get Vbat from gauge*/
	ret = power_supply_get_property(cx->battery,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &info);
	cx->batt_vol = info.intval / 1000;
	/*get Ibat from gauge*/
	ret = power_supply_get_property(cx->battery,
			POWER_SUPPLY_PROP_CURRENT_NOW, &info);
	cx->batt_curr = info.intval / 1000;

	pr_info("Vbat = %d mV, Ibat = %d mA\n", cx->batt_vol, cx->batt_curr);

	return ret;
}

static void charger_monitor_work_func(struct work_struct *work)
{
	int ret = 0;
	int vbus_volt;
	struct cx2589x_device * cx = NULL;
	struct delayed_work *charge_monitor_work = NULL;
	//static u8 last_chg_method = 0;
	struct cx2589x_state state;

	charge_monitor_work = container_of(work, struct delayed_work, work);
	if (charge_monitor_work == NULL) {
		pr_err("Cann't get charge_monitor_work\n");
		return;
	}

	cx = container_of(charge_monitor_work, struct cx2589x_device, charge_monitor_work);
	if (cx == NULL) {
		pr_err("Cann't get cx\n");
		return;
	}

#if 0
	if (cx->usb2_phy->otg->gadget){
		pr_err("%s: gadget->state: %d\n", __func__, cx->usb2_phy->otg->gadget->state);
	}
#endif

	ret = cx2589x_get_state(cx, &state);
	mutex_lock(&cx->lock);
	cx->state = state;
	mutex_unlock(&cx->lock);

	ret = update_battery_info_from_gauge(cx);
	if (ret) {
		pr_err("failed to get batt vol and curr\n");
	}

	if (!cx->state.vbus_gd) {
		pr_err("Vbus not present\n");
		//cx2589x_disable_charger(cx);
		goto out;
	}

	if (!state.online) {
		pr_err("Vbus not online\n");
		goto out;
	}

	cx2589x_dump_register(cx->chg_dev);
	pr_info("+++\n");

	cx2589x_get_vbus(cx, &vbus_volt);
	if (vbus_volt > 8500)
		cx2589x_set_input_volt_lim(s_chg_dev_otg, 8400000);
	else
		cx2589x_set_input_volt_lim(s_chg_dev_otg, 4500000);

out:
	schedule_delayed_work(&cx->charge_monitor_work, 10 * HZ);
}

static int cx2589x_set_dp(struct charger_device *chg_dev, u32 volt);

static void charger_type_detect_work_func(struct work_struct *work)
{
	struct cx2589x_device *cx = NULL;
	struct cx2589x_state state;
	int ret;
	u8 fault, status, val;
	u8 retry_otg = 10;

	cx = container_of(work, struct cx2589x_device, charger_type_detect_work.work);
	if (IS_ERR_OR_NULL(cx)) {
		pr_err("Cann't get cx2589x_device\n");
		return;
	}


	if (!cx->charger_wakelock->active)
		__pm_stay_awake(cx->charger_wakelock);

	ret = cx2589x_get_state(cx, &state);
	mutex_lock(&cx->lock);
	cx->state = state;
	mutex_unlock(&cx->lock);

	if (cx->pd_type_detected) {
		pr_err("PD type is ATTACH_TYPE_PD_DCP, no need to detect, CX2589x charger type: DCP\n");
		power_supply_changed(cx->charger);
		pr_info("Relax wakelock\n");
		__pm_relax(cx->charger_wakelock);
		return;
	}

	if (!cx->state.vbus_gd) {
		pr_err("Vbus not present\n");
		//cx2589x_disable_charger(cx);
		cx->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		goto err;
	}

	if (!state.online) {
		pr_err("Vbus not online\n");
		cx->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		goto err;
	}

	switch(cx->state.chrg_type) {
	case CX2589x_USB_SDP:
#if 1
		if (cx->fake_sdp_type == false) {
			pr_info("CX2589x charger type: SDP\n");
			cx->chg_type = POWER_SUPPLY_TYPE_USB;
			cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
			cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB;
		} else {
			pr_info("CX2589x charger type: FLOAT\n");
			cx->chg_type = POWER_SUPPLY_TYPE_USB_FLOAT;
			cx->psy_usb_type = POWER_SUPPLY_TYPE_USB_FLOAT;
			cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		}
#else
		pr_info("CX2589x charger type: SDP\n");
		cx->chg_type = POWER_SUPPLY_TYPE_USB;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB;
#endif
		break;

	case CX2589x_USB_CDP:
		pr_info("CX2589x charger type: CDP\n");
		cx->chg_type = POWER_SUPPLY_TYPE_USB_CDP;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		break;

	case CX2589x_USB_DCP:
		pr_info("CX2589x charger type: DCP\n");
		cx->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		break;

	case CX2589x_NON_STANDARD:
		pr_info("CX2589x charger type: NON STANDARD\n");
		cx->chg_type = POWER_SUPPLY_TYPE_USB_NON_STD;
		cx->psy_usb_type = POWER_SUPPLY_TYPE_USB_NON_STD;
		cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_DCP;

		if (cx->force_detect_count < BC12_RETRY_COUNT) {
			pr_info("CX2589x charger type: NON STANDARD, retry bc1.2 count:%d\n", cx->force_detect_count);
			schedule_delayed_work(&cx->retry_charger_detect_work, msecs_to_jiffies(100));
		}
		break;

	case CX2589x_UNKNOWN:
		pr_info("CX2589x charger type: UNKNOWN\n");
		cx->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		/*
		 * if usb cable being plug out between driver probe done and healthd service init done.
		 * healthd service will ignore all the events of switch charger as the desc type of switch charger is being set to unknown.
		 * so we can't set the default desc type of switch charger to unknown.
		 */
		cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_TYPE_C;

		if (cx->force_detect_count < BC12_RETRY_COUNT) {
			pr_info("CX2589x charger type: UNKNOWN, retry bc1.2 count:%d\n", cx->force_detect_count);
			schedule_delayed_work(&cx->retry_charger_detect_work, msecs_to_jiffies(100));
		}
		break;

	default:
		pr_info("CX2589x charger type: default\n");
		cx->chg_type = POWER_SUPPLY_TYPE_USB_NON_STD;
		cx->psy_usb_type = POWER_SUPPLY_TYPE_USB_NON_STD;
		cx2589x_power_supply_desc.type = POWER_SUPPLY_TYPE_USB_DCP;

		if (cx->force_detect_count < BC12_RETRY_COUNT) {
			pr_info("CX2589x charger type: default, retry bc1.2 count:%d\n", cx->force_detect_count);
			schedule_delayed_work(&cx->retry_charger_detect_work, msecs_to_jiffies(100));
		}
		break;
	}

	if (cx->state.chrg_type == CX2589x_USB_SDP || cx->state.chrg_type == CX2589x_USB_CDP) {
		Charger_Detect_Release();
	/*
	 * due to the cx25890h will pull up the DP voltage to 0.6V after the DCP detected.
	 * we should set Auto DPDM enable func to disable to pull down the DP voltage for QC3+ detection.
	 */
	} else if (cx->state.chrg_type == CX2589x_USB_DCP) {
#if IS_ENABLED(CONFIG_OEM_DEVINFO)
		if (oem_pcba_charge_power() == CHARGE_POWER_33W) {
			cx2589x_update_bits(cx, CX2589x_REG_02, CX2589x_AUTO_DPDM_MASK, 0);
		}
#endif
	}

	pr_info("Update: chg_type:%d, psy_usb_type:%d\n", cx->chg_type, cx->psy_usb_type);

	//otg retry
	ret = cx2589x_read_reg(cx, CX2589x_REG_0C, &fault);
	if (ret)
		pr_err("read reg0c fail\n");

	pr_info("reg0c fault = %02x\n", fault);
	if (fault & CX2589x_BOOST_FAULT_MASK) {
		do {
			pr_err("otg ocp in irq\n");
			ret = cx2589x_enable_otg(s_chg_dev_otg, true);
			msleep(2);
			ret = cx2589x_read_reg(cx, CX2589x_REG_0B, &status);
			if (ret)
				pr_err("read reg0B fail\n");
			val = (status & CX2589x_VBUS_STAT_MASK);
		} while (retry_otg-- && val != CX2589x_OTG_MODE);
	}

	cx2589x_dump_register(cx->chg_dev);
err:
	//release wakelock
	power_supply_changed(cx->charger);
	pr_err("Relax wakelock\n");
	__pm_relax(cx->charger_wakelock);

	return;
}

static void unknow_charger_type_detect_work_func(struct work_struct *work)
{
	struct cx2589x_device *cx = NULL;
	struct cx2589x_state state;
	int gadget_state = USB_STATE_NOTATTACHED;

	cx = container_of(work, struct cx2589x_device,
			unknow_charger_type_detect_work.work);
	if (IS_ERR_OR_NULL(cx)) {
		pr_err("Cann't get cx2589x_device\n");
		return;
	}

	pr_info("enter\n");

	cx2589x_get_state(cx, &state);
	if (state.chrg_type != CX2589x_USB_SDP)
		return;

	if (!IS_ERR_OR_NULL(cx->usb2_phy->otg->gadget)) {
		cx->unknow_type_check = true;
		do {
			gadget_state = cx->usb2_phy->otg->gadget->state;
			pr_info("gadget state:%d\n", gadget_state);
			if (gadget_state != USB_STATE_NOTATTACHED) {
				cx->fake_sdp_type = false;
				break;
			}
			pr_info("unknow type retry:%d\n", cx->unknow_detect_count);
			Charger_Detect_Init();
			cx2589x_force_dpdm(cx);
			msleep(500);
			cx2589x_get_state(cx, &state);
			if (state.chrg_type != CX2589x_USB_SDP)
				break;
			cx->unknow_detect_count++;
		} while (cx->unknow_detect_count < UNKNOW_RETRY_COUNT);

		if (state.chrg_type == CX2589x_USB_SDP && gadget_state == USB_STATE_NOTATTACHED)
			cx->fake_sdp_type = true;

		schedule_delayed_work(&cx->charger_type_detect_work, msecs_to_jiffies(50));
	}

	pr_err("exit\n");
	return;
}

static irqreturn_t cx2589x_irq_handler_thread(int irq, void *private)
{
	struct cx2589x_device *cx = private;
	struct cx2589x_state state;
	bool prev_vbus_gd;
	bool prev_online;
	int ret = 0;
	int vbus_volt;

	pr_info("enter\n");
#if 1
	ret = cx2589x_get_state(cx, &state);
	if (ret) {
		pr_err("Failed to get state:%d\n", ret);
		return IRQ_HANDLED;
	}

	/*
	 * due to cx2589x will trigger an remove interrupt when set Hiz enable.
	 * we should ignore this remove for maintaining the USB communication.
	 */
	if (state.hiz_en && cx->typec_attached) {
		pr_info("hiz enable caused interrupt, ignore handler\n");
		return IRQ_HANDLED;
	}

	mutex_lock(&cx->lock);
	prev_vbus_gd = cx->state.vbus_gd;
	prev_online = cx->state.online;
	cx->state = state;
	mutex_unlock(&cx->lock);

	if (!prev_online && cx->state.online) {
		pr_info("adapter/usb power good, limit input and charger current\n");
		cx2589x_set_input_curr_lim(cx->chg_dev, 100000);
		cx2589x_set_ichrg_curr(cx->chg_dev, 100000);
		cx2589x_enable_charger(cx);
		schedule_delayed_work(&cx->charger_type_detect_work, msecs_to_jiffies(50));
		return IRQ_HANDLED;
	}

	if (!prev_vbus_gd && cx->state.vbus_gd) {
		pr_info("adapter/usb inserted\n");
		if (!cx->charger_wakelock->active)
			__pm_stay_awake(cx->charger_wakelock);
		Charger_Detect_Init();
		cx->force_detect_count = 0;
		cx->unknow_detect_count = 0;
		cx->fake_sdp_type = false;
		cx->unknow_type_check = false;
		allow_set_dp_dm_vol = true;
	} else if (prev_vbus_gd && !cx->state.vbus_gd) {
		cx2589x_get_vbus(cx, &vbus_volt);
		pr_info("adapter/usb removed state.online=0x%x vbus=%d\n", cx->state.online, vbus_volt);
		cx2589x_update_bits(cx, CX2589x_REG_02, CX2589x_AUTO_DPDM_MASK, 1);
		Charger_Detect_Release();
		cx2589x_set_dpdm_hiz(cx);
		allow_set_dp_dm_vol = false;
		cx->force_detect_count = 0;
		cx->unknow_detect_count = 0;
		cx->fake_sdp_type = false;
		cx->unknow_type_check = false;
		power_supply_changed(cx->charger);
		if (cx->charger_wakelock->active)
			__pm_relax(cx->charger_wakelock);
	}
#else
	schedule_delayed_work(&cx->charger_type_detect_work, msecs_to_jiffies(100));
#endif
	//power_supply_changed(cx->charger);
	return IRQ_HANDLED;
}

static char *cx2589x_charger_supplied_to[] = {
	"battery",
	"mtk-master-charger",
};

static struct power_supply_desc cx2589x_power_supply_desc = {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	.name = "primary_chg",
#else
	.name = "ext_charger_type",
#endif
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = cx2589x_usb_type,
	.num_usb_types = ARRAY_SIZE(cx2589x_usb_type),
	.properties = cx2589x_power_supply_props,
	.num_properties = ARRAY_SIZE(cx2589x_power_supply_props),
	.get_property = cx2589x_charger_get_property,
	.set_property = cx2589x_charger_set_property,
	.property_is_writeable = cx2589x_property_is_writeable,
};

static int cx2589x_power_supply_init(struct cx2589x_device *cx, struct device *dev)
{
	struct power_supply_config psy_cfg = {
		.drv_data = cx,
		.of_node = dev->of_node,
	};

	psy_cfg.supplied_to = cx2589x_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(cx2589x_charger_supplied_to);

	cx->charger = devm_power_supply_register(cx->dev,
			 &cx2589x_power_supply_desc,
			 &psy_cfg);

	if (IS_ERR(cx->charger))
		return -EINVAL;

	return 0;
}

static int cx2589x_write_reg40(struct cx2589x_device *cx,bool enable)
{
	int ret,try_count=10;
	u8 read_val;

	if (enable) {
		while (try_count--) {
			ret = cx2589x_write_reg(cx, 0x40, 0x00);
			ret = cx2589x_write_reg(cx, 0x40, 0x50);
			ret = cx2589x_write_reg(cx, 0x40, 0x57);
			ret = cx2589x_write_reg(cx, 0x40, 0x44);

			cx2589x_read_reg(cx, 0x40, &read_val);
			if (0x03 == read_val) {
				ret = 1;
				break;
			}
		}
	} else {
		ret = cx2589x_write_reg(cx, 0x40, 0x00);
	}

	return ret;
}

static int cx2589x_init_charge(struct cx2589x_device *cx)
{
	int ret;
	u8 data;

	ret = cx2589x_write_reg40(cx, true);
	if (ret == 1)
		pr_info("write reg40 success\n");

	ret = cx2589x_write_reg_retry(cx, 0x41, 0x08);
	ret = cx2589x_write_reg_retry(cx, 0x44, 0x18);
	ret = cx2589x_read_reg_retry(cx, 0x41, &data);
	if (data != 0x08)
		pr_err("Failed to write: reg41 = %02x\n", data);
	else
		pr_info("write reg41 ok\n");

	ret = cx2589x_write_reg40(cx, false);

	return ret;
}

static int cx2589x_set_bat_comp(struct cx2589x_device *cx, u32 ir_mohm)
{
	int ret;
	u8 reg_val;
	if (ir_mohm < CX2589x_BAT_COMP_MIN)
		ir_mohm = CX2589x_BAT_COMP_MIN;
	else if (ir_mohm > CX2589x_BAT_COMP_MAX)
		ir_mohm = CX2589x_BAT_COMP_MAX;
	reg_val = (ir_mohm - CX2589x_BAT_COMP_MIN) / CX2589x_BAT_COMP_STEP;
	ret = cx2589x_update_bits(cx, CX2589x_REG_08, CX2589x_BAT_COMP_MASK, reg_val << 5);
	return ret;
}
static int cx2589x_set_vclamp(struct cx2589x_device *cx, u32 ir_uv)
{
	int ret;
	u8 reg_val;
	if (ir_uv < CX2589x_VCLAMP_MIN_uV)
		ir_uv = CX2589x_VCLAMP_MIN_uV;
	else if (ir_uv > CX2589x_VCLAMP_MAX_uV)
		ir_uv = CX2589x_VCLAMP_MAX_uV;
	reg_val = (ir_uv - CX2589x_VCLAMP_MIN_uV) / CX2589x_VCLAMP_STEP_uV;
	ret = cx2589x_update_bits(cx, CX2589x_REG_08, CX2589x_VCLAMP_MASK, reg_val << 2);
	return ret;
}

static int cx2589x_hw_init(struct cx2589x_device *cx)
{
	int ret = 0;
	struct power_supply_battery_info bat_info = { };

	bat_info.constant_charge_current_max_ua =
			CX2589x_ICHRG_I_DEF_uA;

	bat_info.constant_charge_voltage_max_uv =
			CX2589x_VREG_V_DEF_uV;

	bat_info.precharge_current_ua =
			CX2589x_PRECHRG_I_DEF_uA;

	bat_info.charge_term_current_ua =
			CX2589x_TERMCHRG_I_DEF_uA;

	cx->init_data.max_ichg =
			CX2589x_ICHRG_I_MAX_uA;

	cx->init_data.max_vreg =
			CX2589x_VREG_V_MAX_uV;

	pr_info("init device enter\n");
	//cx2589x_reset_chip(cx);
	cx2589x_enable_charger(cx);
	cx2589x_init_charge(cx);
	cx2589x_set_watchdog_timer(cx, 0);
	cx2589x_update_bits(cx, CX2589x_REG_00, CX2589x_EN_ILIM, 0);  //disable ILIM pin
	cx2589x_update_bits(cx, CX2589x_REG_02, CX2589x_EN_ICO, 0);   //disable ico
	cx2589x_update_bits(cx, CX2589x_REG_02, CX2589x_EN_HVDCP, 0); //disable hvdcp
	cx2589x_update_bits(cx, CX2589x_REG_02, CX2589x_BOOST_FREQ_500K, 0x20); //boost freq 1-500khz
	cx2589x_update_bits(cx, CX2589x_REG_0A, CX2589x_BOOST_LIM, 0x4);  //BOOST_LIM = 1.65A

	ret = cx2589x_set_ichrg_curr(s_chg_dev_otg,
			bat_info.constant_charge_current_max_ua);
	if (ret)
		goto err_out;

	ret = cx2589x_set_chrg_volt(s_chg_dev_otg, 4448000);
	if (ret)
		goto err_out;

	ret = cx2589x_set_term_curr(s_chg_dev_otg, bat_info.charge_term_current_ua);
	if (ret)
		goto err_out;

	ret = cx2589x_set_input_volt_lim(s_chg_dev_otg, 4500000);
	if (ret)
		goto err_out;

	ret = cx2589x_set_input_curr_lim(s_chg_dev_otg, cx->init_data.ilim);
	if (ret)
		goto err_out;
#if 0
	ret = cx2589x_set_vac_ovp(cx); //14V
	if (ret)
		goto err_out;
#endif
	ret = cx2589x_set_recharge_volt(cx, 100); //100~200mv
	if (ret)
		goto err_out;

	ret = cx2589x_set_bat_comp(cx, 0);
	if (ret)
		goto err_out;

	ret = cx2589x_set_vclamp(cx, 0);
	if (ret)
		goto err_out;

	pr_info("ichrg_curr:%d prechrg_curr:%d chrg_vol:%d term_curr:%d input_curr_lim:%d",
		bat_info.constant_charge_current_max_ua,
		256000,
		4448000,
		bat_info.charge_term_current_ua,
		cx->init_data.ilim);

	return 0;
err_out:
	return ret;
}

static int cx2589x_parse_dt(struct cx2589x_device *cx)
{
	int ret;
	int irq_gpio = 0, irqn = 0;
	int chg_en_gpio = 0;

	ret = device_property_read_u32(cx->dev,
			"input-voltage-limit-microvolt", &cx->init_data.vlim);
	if (ret) {
		cx->init_data.vlim = CX2589x_VINDPM_DEF_uV;
	}

	if (cx->init_data.vlim > CX2589x_VINDPM_V_MAX_uV ||
		cx->init_data.vlim < CX2589x_VINDPM_V_MIN_uV) {
		pr_err("VIN DPM out of range\n");
		return -EINVAL;
	}

	ret = device_property_read_u32(cx->dev,
			"input-current-limit-microamp", &cx->init_data.ilim);
	if (ret) {
		cx->init_data.ilim = CX2589x_IINDPM_DEF_uA;
	}

	if (cx->init_data.ilim > CX2589x_IINDPM_I_MAX_uA ||
		cx->init_data.ilim < CX2589x_IINDPM_I_MIN_uA) {
		pr_err("IIN DPM out of range\n");
		return -EINVAL;
	}

	irq_gpio = of_get_named_gpio(cx->dev->of_node, "cx,irq-gpio", 0);
	if (!gpio_is_valid(irq_gpio)) {
		pr_err("%d gpio get failed\n", irq_gpio);
		return -EINVAL;
	}

	ret = gpio_request(irq_gpio, "cx2589x irq pin");
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

	cx->client->irq = irqn;

	chg_en_gpio = of_get_named_gpio(cx->dev->of_node, "cx,chg-en-gpio", 0);
	if (!gpio_is_valid(chg_en_gpio)) {
		pr_err("%d gpio get failed\n", chg_en_gpio);
		return -EINVAL;
	}

	ret = gpio_request(chg_en_gpio, "cx chg en pin");
	if (ret) {
		pr_err("%d gpio request failed\n", chg_en_gpio);
		return ret;
	}

	gpio_direction_output(chg_en_gpio, 0); //default enable charge

	return 0;
}

static int cx2589x_enable_vbus(void)
{
	int ret = 0;
	u8 val;
	struct cx2589x_device *cx = charger_get_data(s_chg_dev_otg);

	pr_info("enable_otg enter\n");
	ret = cx2589x_read_reg(cx, CX2589x_REG_0C, &val);
	ret = cx2589x_set_boost_current_limit(s_chg_dev_otg, 2150000);

	ret = cx2589x_write_reg40(cx, true);
	if (ret == 1)
		pr_info("write reg40 success\n");

	ret = cx2589x_write_reg(cx, 0x83, 0x03);
	ret = cx2589x_write_reg(cx, 0x41, 0x88);

	ret = cx2589x_update_bits(cx, CX2589x_REG_03, CX2589x_OTG_EN, CX2589x_OTG_EN);
	msleep(100);
	ret = cx2589x_set_boost_current_limit(s_chg_dev_otg, 1650000);
	pr_info("cx2589x_otg_enter\n");

	ret = cx2589x_write_reg(cx, 0x83, 0x01);
	msleep(10);
	ret = cx2589x_write_reg(cx, 0x41, 0x08);
	ret = cx2589x_write_reg40(cx, false);

	return ret;
}

static int cx2589x_disable_vbus(void)
{
	int ret = 0;
	struct cx2589x_device *cx = charger_get_data(s_chg_dev_otg);

	pr_info("disable_otg enter\n");

	ret = cx2589x_write_reg40(cx,true);
	if (ret == 1)
		pr_info("write reg40 success\n");

	ret = cx2589x_write_reg(cx, 0x83, 0x00);
	ret = cx2589x_write_reg40(cx, false);
	ret = cx2589x_update_bits(cx, CX2589x_REG_03, CX2589x_OTG_EN, 0);

	return ret;
}


__maybe_unused static int cx2589x_is_enabled_vbus(struct regulator_dev *rdev)
{
	u8 temp = 0;
	int ret = 0;
	struct cx2589x_device *cx = charger_get_data(s_chg_dev_otg);

	ret = cx2589x_read_reg(cx, CX2589x_REG_03, &temp);
	return (temp & CX2589x_OTG_EN) ? 1 : 0;
}

static int cx2589x_set_volt_to_reg(u32 volt)
{
	int reg_val = 0;
	if (volt == 0)
		reg_val = 0x1;
	else if (volt == 3300000)
		reg_val = 0x6;
	else if (volt == 600000)
		reg_val = 0x2;
	else
		reg_val = 0x0;

	return reg_val;
}

static int cx2589x_set_dp(struct charger_device *chg_dev, u32 volt)
{
	struct cx2589x_device *cx = charger_get_data(chg_dev);
	int reg_val = 0;
	reg_val = cx2589x_set_volt_to_reg(volt);

	reg_val = reg_val << 5;
	pr_info("set_dp = %duV\n", volt);
	return cx2589x_update_bits(cx, CX2589x_REG_15, CX2589x_DP_VSEL_MASK, reg_val);
}

static int cx2589x_set_dm(struct charger_device *chg_dev, u32 volt)
{
	struct cx2589x_device *cx = charger_get_data(chg_dev);
	int reg_val = 0;
	reg_val = cx2589x_set_volt_to_reg(volt);

	reg_val = reg_val << 2;
	pr_info("set_dm = %duV\n", volt);
	return cx2589x_update_bits(cx, CX2589x_REG_15, CX2589x_DM_VSEL_MASK, reg_val);
}

static int cx2589x_enable_otg(struct charger_device *chg_dev, bool en)
{
	int ret = 0;

	pr_info("en = %d\n", en);
	if (en) {
		ret = cx2589x_enable_vbus();
	} else {
		ret = cx2589x_disable_vbus();
	}

	return ret;
}

static int cx2589x_do_event(struct charger_device *chg_dev, u32 event, u32 args)
{
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	pr_info("event:%d\n", event);

	switch (event) {
	case EVENT_FULL:
		cx->battery_full = true;
		break;
	case EVENT_RECHARGE:
	case EVENT_DISCHARGE:
		cx->battery_full = false;
		break;
	default:
		break;
	}
	power_supply_changed(cx->charger);
	return 0;
}

__maybe_unused static int cx2589x_set_boost_voltage_limit(
		struct charger_device *chg_dev, u32 uV)
{
	int ret = 0;
	u8 reg_val;

	struct cx2589x_device *cx = charger_get_data(chg_dev);

	if (uV < 4550000)
		uV = 4550000;
	else if (uV > 5510000)
		uV = 5510000;

	reg_val = (uV - 4550000) / 64000;

	reg_val = reg_val << 4;
	ret = cx2589x_update_bits(cx, CX2589x_REG_0A, CX2589x_BOOSTV, reg_val);

	return ret;
}

static int cx2589x_set_boost_current_limit(struct charger_device *chg_dev, u32 uA)
{
	int ret = 0;
	u8 val;
	struct cx2589x_device *cx = charger_get_data(chg_dev);

	if (uA < 750000)
		val = 0;
	else if (uA < 1200000)
		val = 1;
	else if (uA < 1400000)
		val = 2;
	else if (uA < 1650000)
		val = 3;
	else if (uA < 1875000)
		val = 4;
	else if (uA < 2150000)
		val = 5;
	else if (uA < 2450000)
		val = 6;
	else
		val = 7;

	ret = cx2589x_update_bits(cx, CX2589x_REG_0A, CX2589x_BOOST_LIM, val);
	pr_info("set boost current limit uA=%d, reg_val=0x%x\n", uA, val);

	return ret;
}

#if 0
static struct regulator_ops cx2589x_vbus_ops = {
	.enable = cx2589x_enable_vbus,
	.disable = cx2589x_disable_vbus,
	.is_enabled = cx2589x_is_enabled_vbus,
};

static const struct regulator_desc cx2589x_otg_rdesc = {
	.of_match = "usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &cx2589x_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int cx2589x_vbus_regulator_register(struct cx2589x_device *cx)
{
	struct regulator_config config = {};
	int ret = 0;
	/* otg regulator */
	config.dev = cx->dev;
	config.driver_data = cx;

	cx->otg_rdev = devm_regulator_register(cx->dev,
				&cx2589x_otg_rdesc, &config);
	cx->otg_rdev->constraints->valid_ops_mask |= REGULATOR_CHANGE_STATUS;
	if (IS_ERR(cx->otg_rdev)) {
		ret = PTR_ERR(cx->otg_rdev);
		pr_info("register otg regulator failed (%d)\n", ret);
	}

	return ret;
}
#endif

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
static int cx2589x_enable_dpdm_hiz(struct charger_device *chg_dev)
{
	int ret;
	struct cx2589x_device *cx = dev_get_drvdata(&chg_dev->dev);

	ret = cx2589x_set_dpdm_hiz(cx);
	if (ret < 0)
		pr_err("set dpdm hiz failed ret(%d)\n", ret);
	else
		pr_info("set dpdm hiz successfully\n");

	return ret;
}
#endif

static int cx2589x_plug_in(struct charger_device *chg_dev)
{
	int ret = 0;
	struct cx2589x_device *cx = dev_get_drvdata(&chg_dev->dev);
	struct cx2589x_state state;

	pr_info("enter\n");

	/* Enable charging */
	ret = cx2589x_enable_charger(cx);
	if (ret) {
		pr_err("Failed to enable charging(%d)\n", ret);
	}

	ret = cx2589x_get_state(cx, &state);
	if (ret) {
		pr_err("Failed to get state(%d)\n", ret);
	}

	/*
	 * due to cx2589x will report SDP type when plug Float/unknow type.
	 * we should queue a work to check weather it is a Float/unknow type or a real SDP type.
	 */
	if (!cx->unknow_type_check && cx->chg_type == POWER_SUPPLY_TYPE_USB)
		schedule_delayed_work(&cx->unknow_charger_type_detect_work, msecs_to_jiffies(1500));

	mutex_lock(&cx->lock);
	cx->state = state;
	mutex_unlock(&cx->lock);

	power_supply_changed(cx->charger);
	return ret;
}

static int cx2589x_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct cx2589x_device *cx = dev_get_drvdata(&chg_dev->dev);

	pr_info("enter\n");
	/*
	 * disable Hiz when plug out charger
	 * as cx2589x will not exit Hiz mode automatically when inserting charger next time.
 	 */
	cx2589x_set_dpdm_hiz(cx);
	cx2589x_set_hiz_en(cx->chg_dev, false);
	ret = cx2589x_disable_charger(cx);
	if (ret) {
		pr_err("Failed to disable charging(%d)\n", ret);
	}
	cx->pd_type_detected = false;
	return ret;
}

static struct charger_ops cx2589x_chg_ops = {
	.dump_registers = cx2589x_dump_register,
	.plug_in = cx2589x_plug_in,
	.plug_out = cx2589x_plug_out,
	/* enable */
	.enable = cx2589x_charging_switch,
	.is_enabled = cx2589x_is_charging,
	/* charging current */
	.set_charging_current = cx2589x_set_ichrg_curr,
	.get_charging_current = cx2589x_get_ichg_curr,
	.get_min_charging_current = cx2589x_get_minichg_curr,
	/* charging voltage */
	.set_constant_voltage = cx2589x_set_chrg_volt,
	.get_constant_voltage = cx2589x_get_chrg_volt,
	/* input current limit */
	.set_input_current = cx2589x_set_input_curr_lim,
	.get_input_current = cx2589x_get_input_curr_lim,
	.get_min_input_current = cx2589x_get_input_mincurr_lim,
	/* MIVR */
	.set_mivr = cx2589x_set_input_volt_lim,
	.get_mivr = cx2589x_get_input_volt_lim,
	//.get_mivr_state = cx2589x_get_input_minvolt_lim,
	/* charing termination */
	.set_eoc_current = cx2589x_set_term_curr,
	.enable_termination = cx2589x_enable_terminate,
	//.reset_eoc_state = mt6375_reset_eoc_state,
	//.safety_check = mt6375_sw_check_eoc,
	.is_charging_done = cx2589x_get_charging_status,
	/* power path */
	//.enable_powerpath = mt6375_enable_buck,
	//.is_powerpath_enabled = mt6375_is_buck_enabled,
	/* timer */
	.enable_safety_timer = cx2589x_enable_safetytimer,
	.is_safety_timer_enabled = cx2589x_get_is_safetytimer_enable,
	.kick_wdt = cx2589x_reset_watch_dog_timer,
	/* AICL */
	//.run_aicl = mt6375_run_aicc,

	/* PE+/PE+20 */
	.send_ta_current_pattern = cx2589x_en_pe_current_partern,

	//.set_pe20_efficiency_table = mt6375_set_pe20_efficiency_table,
	//.send_ta20_current_pattern = mt6375_set_pe20_current_pattern,
	//.reset_ta = mt6375_reset_pe_ta,
	//.enable_cable_drop_comp = mt6,
	/* OTG */
	.enable_otg = cx2589x_enable_otg,
	.set_boost_current_limit = cx2589x_set_boost_current_limit,
	.set_boost_voltage_limit = cx2589x_set_boost_voltage_limit,
	.enable_hz = cx2589x_set_hiz_en,
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	.enable_dpdm_hz = cx2589x_enable_dpdm_hiz,
#endif

	/* DPDM */
	.set_dp = cx2589x_set_dp,
	.set_dm = cx2589x_set_dm,
	.event = cx2589x_do_event,
};

static ssize_t dump_reg_ctrl_write(struct file *filp,
	const char *ubuf, size_t cnt, loff_t *data)
{
	char buf[8] = {0};
	long val = 0;
	int ret = 0;

	if (cnt >= sizeof(buf)) {
		pr_err("cnt is invalid\n");
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt)) {
		pr_err("copy failed\n");
		return -EFAULT;
	}

	buf[cnt] = 0;
	ret = kstrtoul(buf, 10, (unsigned long *)&val);
	if (ret < 0) {
		pr_err("val is invalid\n");
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

static ssize_t cx2589x_show_registers(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct cx2589x_device *cx = dev_get_drvdata(dev);
	uint8_t addr;
	uint8_t val;
	uint8_t tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "cx25890h");

	for (addr = 0; addr < CX2589x_REG_NUM + 1; addr++) {
		ret = cx2589x_read_reg(cx, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				"Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t cx2589x_store_register(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct cx2589x_device *cx = dev_get_drvdata(dev);
	int ret;
	unsigned int val;
	unsigned int reg;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= CX2589x_REG_NUM)
		cx2589x_write_reg(cx, reg, val);

	return count;
}

static DEVICE_ATTR(registers, 0660, cx2589x_show_registers, cx2589x_store_register);

static int cx2589x_create_device_node(struct device *dev)
{
	int ret = 0;

	ret = device_create_file(dev, &dev_attr_registers);
	if (ret < 0) {
		pr_err("failed to create register attr\n");
		return -ENODEV;
	}

	return ret;
}

static void cx2589x_destory_device_node(struct device *dev)
{
	device_remove_file(dev, &dev_attr_registers);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static int cx2589x_driver_probe(struct i2c_client *client)
#else
static int cx2589x_driver_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
#endif
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct cx2589x_device *cx;

	char *name = NULL;

	pr_info("enter\n");

	cx = devm_kzalloc(dev, sizeof(*cx), GFP_KERNEL);
	if (!cx) {
		pr_err("alloc memory failed\n");
		return -ENOMEM;
	}

	cx->client = client;
	cx->dev = dev;
	cx->battery_full = false;
	cx->typec_attached = false;
	cx->force_detect_count = 0;
	cx->fake_sdp_type = false;
	cx->unknow_type_check = false;
	cx->pd_type_detected = false;

	mutex_init(&cx->lock);
	mutex_init(&cx->i2c_rw_lock);

	i2c_set_clientdata(client, cx);

	cx->vbus = devm_iio_channel_get(cx->dev, "pmic_vbus");
	if (IS_ERR_OR_NULL(cx->vbus)) {
		pr_err("cx25890h get vbus failed\n");
		return -EPROBE_DEFER;
	}

	ret = cx2589x_hw_chipid_detect(cx);
	if (ret != CX2589x_PN_ID) {
		pr_err("device not found !!!\n");
		return ret;
	}

	ret = cx2589x_parse_dt(cx);
	if (ret) {
		pr_err("parse dts resource failed\n");
		return ret;
	}

	name = devm_kasprintf(cx->dev, GFP_KERNEL, "%s", "cx2589x suspend wakelock");
	cx->charger_wakelock =	wakeup_source_register(cx->dev, name);

	/* Register charger device */
	cx->chg_dev = charger_device_register("primary_chg",
				&client->dev, cx,
				&cx2589x_chg_ops,
				&cx2589x_chg_props);

	if (IS_ERR_OR_NULL(cx->chg_dev)) {
		pr_err("register charger device failed\n");
		ret = PTR_ERR(cx->chg_dev);
		return ret;
	}

	/* otg regulator */
	s_chg_dev_otg = cx->chg_dev;

	INIT_DELAYED_WORK(&cx->charger_type_detect_work, charger_type_detect_work_func);
	INIT_DELAYED_WORK(&cx->charge_monitor_work, charger_monitor_work_func);
	INIT_DELAYED_WORK(&cx->unknow_charger_type_detect_work, unknow_charger_type_detect_work_func);
	INIT_DELAYED_WORK(&cx->retry_charger_detect_work, retry_charger_detect_work_func);

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
				cx2589x_irq_handler_thread,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				dev_name(&client->dev), cx);
		if (ret) {
			pr_err("request irq failed\n");
			return ret;
		}
		enable_irq_wake(client->irq);
	}

	ret = cx2589x_power_supply_init(cx, dev);
	if (ret) {
		pr_err("Failed to register power supply\n");
		return ret;
	}

	ret = cx2589x_hw_init(cx);
	if (ret) {
		pr_err("Can't initialize the chip\n");
		return ret;
	}

	dump_reg_enable = true;
	entry = proc_create("dump_reg_ctrl", 0664, NULL, &dump_reg_ctrl_fops);
	if (!entry) {
		pr_err("Create proc directory failed\n");
	}

	ret = cx2589x_create_device_node(&(client->dev));

	//ret = cx2589x_vbus_regulator_register(cx);

	//pr_info("run charger_type_detect_work\n");

	//schedule_delayed_work(&cx->charger_type_detect_work, msecs_to_jiffies(1000));
	//schedule_delayed_work(&cx->charge_monitor_work, msecs_to_jiffies(100));

	//usb device
#if 1
	cx->usb2_phy = devm_usb_get_phy(dev, USB_PHY_TYPE_USB2);

	if (IS_ERR_OR_NULL(cx->usb2_phy)) {
		pr_err("usb_get_phy failed\n");
		return ret;
	} else {
		pr_info("usb_get_phy success\n");
	}
#endif
	//schedule_delayed_work(&cx->charge_usb_detect_work, 5 * HZ);

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	FULL_PRODUCT_DEVICE_INFO(ID_SWITCH_CHARGER, "CX25890H");
#endif
/* TN Begin modified by xuan.wang/20241016 CR/EKLAMU-7909 */
	if (cx->state.vbus_gd) {
		cx2589x_force_dpdm(cx);
	}
/* TN End modified by xuan.wang/20241016 CR/EKLAMU-7909 */

	schedule_delayed_work(&cx->unknow_charger_type_detect_work, msecs_to_jiffies(10000));

	pr_info("successfully\n");

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static void cx2589x_charger_remove(struct i2c_client *client)
#else
static int cx2589x_charger_remove(struct i2c_client *client)
#endif
{
	struct cx2589x_device *cx = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&cx->charge_monitor_work);
	cancel_delayed_work_sync(&cx->unknow_charger_type_detect_work);
	cancel_delayed_work_sync(&cx->charger_type_detect_work);
	cancel_delayed_work_sync(&cx->retry_charger_detect_work);
	//regulator_unregister(cx->otg_rdev);
	power_supply_unregister(cx->charger);
	cx2589x_destory_device_node(cx->dev);
	mutex_destroy(&cx->lock);
	mutex_destroy(&cx->i2c_rw_lock);

#if !(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	return 0;
#endif
}

static void cx2589x_charger_shutdown(struct i2c_client *client)
{
	/*
	int ret = 0;
	struct cx2589x_device *cx = i2c_get_clientdata(client);

	ret = cx2589x_disable_charger(cx);
	if (ret) {
		pr_err("Failed to disable charger, ret = %d\n", ret);
	}
	pr_info("cx2589x_charger_shutdown\n");
	*/
	return;
}

static const struct i2c_device_id cx2589x_i2c_ids[] = {
	{ "cx25890h", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, cx2589x_i2c_ids);

static const struct of_device_id cx2589x_of_match[] = {
	{ .compatible = "suncore,cx25890h", },
	{ },
};
MODULE_DEVICE_TABLE(of, cx2589x_of_match);

#ifdef CONFIG_PM_SLEEP
static int cx2589x_suspend(struct device *dev)
{
	struct cx2589x_device *cx = dev_get_drvdata(dev);

	pr_info("enter\n");
	if (device_may_wakeup(dev))
		enable_irq_wake(cx->client->irq);
	disable_irq(cx->client->irq);

	return 0;
}

static int cx2589x_resume(struct device *dev)
{
	struct cx2589x_device *cx = dev_get_drvdata(dev);

	pr_info("enter\n");
	enable_irq(cx->client->irq);
	if (device_may_wakeup(dev))
		disable_irq_wake(cx->client->irq);

	return 0;
}
#endif

static const struct dev_pm_ops cx2589x_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cx2589x_suspend, cx2589x_resume)
};

static struct i2c_driver cx2589x_driver = {
	.driver = {
		.name = "cx2589x-charger",
		.of_match_table = cx2589x_of_match,
		.pm = &cx2589x_pm_ops,
	},
	.probe = cx2589x_driver_probe,
	.remove = cx2589x_charger_remove,
	.shutdown = cx2589x_charger_shutdown,
	.id_table = cx2589x_i2c_ids,
};
module_i2c_driver(cx2589x_driver);

MODULE_AUTHOR("chochen <cho.chen@cx-semi.com.cn>");
MODULE_DESCRIPTION("cx2589x charger driver");
MODULE_LICENSE("GPL v2");
