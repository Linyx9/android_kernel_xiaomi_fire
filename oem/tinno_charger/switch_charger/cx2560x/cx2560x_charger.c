#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/string.h>

#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/phy.h>

#include "cx2560x.h"
#include "cx2560x_reg.h"
#include "../../drivers/power/supply/charger_class.h"
#include "../../drivers/power/supply/mtk_charger.h"
#include "../../drivers/misc/mediatek/usb20/mtk_musb.h"


#undef dev_fmt
#define dev_fmt(fmt) "cx2560x: " fmt
extern void get_vbus_voltage_ext(int *val);
//extern int otg_switch_flag;

extern void Charger_Detect_Init(void);
static int cx2560x_disable_otg(struct cx2560x_device *cx);
//extern void Charger_Detect_Release(void);

static int no_usb_flag = 0;
static int force_dpdm_count = 0;
static bool usb_detect_flag = false;
static bool usb_detect_done = false;
/* add i2c test */
static struct class  *chg_i2c_test_class = NULL;
static dev_t chg_i2c_test_devt;
static struct device *chg_i2c_test_device;
static char opr_flag = -1;
static int opr_ret;
static uint8_t reg;
static uint8_t read_data;
static uint8_t write_data;
static int cx2560x_set_ichrg_curr(
		struct charger_device *chg_dev, unsigned int chrg_curr);
static void cx2560x_set_hvdcp_off(struct cx2560x_device *cx);
enum cx2560x_part_no {
	CX25600 = 0x00,
	CX25601 = 0x02,
};

int cx2560x_vtemp = 250;
EXPORT_SYMBOL(cx2560x_vtemp);

static struct power_supply_desc cx2560x_charger_desc;
static int cx2560x_force_dpdm(struct cx2560x_device *cx);
static int cx2560x_charging_switch(
	struct charger_device *chg_dev, bool enable);
static int cx2560x_set_vindpm(struct cx2560x_device *cx, unsigned int vindpm);

static char *cx2560x_charge_state[] = {
	"charge-disable",
	"pre-charge",
	"fast-charge",
	"charge-terminated",
};

static enum power_supply_usb_type cx2560x_usb_type[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,

};

static enum power_supply_usb_type cx2560x_charge_type[] = {
	POWER_SUPPLY_CHARGE_TYPE_NONE,
	POWER_SUPPLY_CHARGE_TYPE_TRICKLE,
	POWER_SUPPLY_CHARGE_TYPE_FAST,
	POWER_SUPPLY_CHARGE_TYPE_TAPER,
};

static enum power_supply_property cx2560x_charger_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
};

static enum power_supply_property cx2560x_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static enum power_supply_property cx2560x_usb_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

#define CX2560X_REG_NUM		(0x10)
static int cx2560x_dump_register(struct charger_device *chg_dev);


static int cx2560x_read_reg(
	struct cx2560x_device *cx, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(cx->client, reg);
	if (ret < 0) {
		dev_err(cx->dev, "read reg: %#x failed", reg);
		return -EINVAL;
	} else {
		*data = ret;
	}

	return 0;
}

static int cx2560x_write_reg(
	struct cx2560x_device *cx, u8 reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(cx->client, reg, value);
	if (ret < 0) {
		dev_err(cx->dev, "write reg: %#x failed", reg);
		return -EINVAL;
	}

	return 0;
}

static int cx2560x_update_bits(
	struct cx2560x_device *cx, u8 reg, u8 mask, u8 val)
{
	u8  tmp;
	int ret;

	ret = cx2560x_read_reg(cx, reg, &tmp);
	if (ret) {
		dev_err(cx->dev, "cx2560x_update_bits ret=%d\n, ret");
		return ret;
	}

	tmp &= ~mask;
	tmp |= val & mask;

	ret = cx2560x_write_reg(cx, reg, tmp);
	if (ret)
		return ret;

	return 0;
}

static int cx2560x_write_reg40(struct cx2560x_device *cx, bool enable)
{
	int ret,try_count=10;
	u8 read_val;

	if(enable)
	{
		while(try_count--)
		{
			ret = cx2560x_write_reg(cx, 0x40, 0x00);
			ret = cx2560x_write_reg(cx, 0x40, 0x50);
			ret = cx2560x_write_reg(cx, 0x40, 0x57);
			ret = cx2560x_write_reg(cx, 0x40, 0x44);

			cx2560x_read_reg(cx, 0x40, &read_val);
			if(0x03 == read_val)
			{
				ret=1;
				break;
			}
		}
	}
	else
	{
		ret = cx2560x_write_reg(cx, 0x40, 0x00);
	}
	return ret;
}



static inline int cx2560x_reset_register(
	struct cx2560x_device *cx)
{
	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_B,
		CX2560X_REG_RESET, CX2560X_REG_RESET);
}

static int cx2560x_disable_watchdog_timer(
	struct cx2560x_device *cx)
{
	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_5,
		CX2560X_WDT_TIMER_MASK, CX2560X_WDT_TIMER_DISABLE);
}

static int cx2560x_disable_dpm_irq(
	struct cx2560x_device *cx)
{
	return cx2560x_update_bits(cx,
		CX2560X_CHRG_CTRL_A, CX2560X_DPM_INT_MASK,
		CX2560X_IINDPM_INT_MASK | CX2560X_VINDPM_INT_MASK);
}

static void cx2560x_charge_enable_ctrl(
	struct cx2560x_device *cx, int en)
{
	cx->chg_en = !!en;
	cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_1,
		CX2560X_CHRG_EN, en ? CX2560X_CHRG_EN : 0);
}

static inline int cx2560x_get_charge_enable_status(
	struct cx2560x_device *cx)
{
	u8 status;

	cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_1, &status);

	return !!(status & CX2560X_CHRG_EN);
}

static void cx2560x_set_usbin_current_limit(
	struct cx2560x_device *cx , unsigned int iindpm)
{
	u8 reg_val;
	dev_info(cx->dev, "cx2560x_set_usbin_current_limit iindpm=%d\n", iindpm);
	if (iindpm > 3200)
		cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_0,
			CX2560X_IINDPM_I_MASK, CX2560X_IINDPM_I_MASK);
	else {
		reg_val = (iindpm - CX2560X_IINDPM_I_MIN_MA)
			/ CX2560X_IINDPM_STEP_MA;
		cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_0,
			CX2560X_IINDPM_I_MASK, reg_val);
	}
}

static int cx2560x_set_fast_charge_current(
	struct cx2560x_device *cx, unsigned int chrg_curr)
{
	u8 reg_val;

	if (chrg_curr > 2875)
		chrg_curr = 2875;

	if(chrg_curr<59)
		reg_val=0;
	else if(chrg_curr>=59 && chrg_curr<=815)
		reg_val=(chrg_curr-59)/63+1;
	else
		reg_val=(chrg_curr-805)/(115/2)+14;

	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_2,
		CX2560X_ICHRG_CUR_MASK, reg_val);
}

static int cx2560x_set_taper_current(
	struct cx2560x_device *cx, int term_current)
{
	u8 reg_val;

	reg_val = (term_current - CX2560X_TERMCHRG_I_MIN_MA)
				/ CX2560X_TERMCHRG_CURRENT_STEP_MA;
	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_3,
		CX2560X_TERMCHRG_CUR_MASK, reg_val);
}

static int cx2560x_set_prechg_current(
	struct cx2560x_device *cx, int prechg_current)
{
	u8 reg_val;

	reg_val = prechg_current / CX2560X_PRECHG_CURRENT_STEP_MA;
	reg_val = reg_val << 4;
	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_3,
		CX2560X_PRECHG_CUR_MASK, reg_val);
}

static int cx2560x_set_battery_voltage(
	struct cx2560x_device *cx, unsigned int chrg_volt)
{
	u8 reg_val;

	reg_val = (chrg_volt - CX2560X_VREG_V_MIN_MV)
				/ CX2560X_VREG_V_STEP_MV;
	reg_val = reg_val << 3;
	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_4,
		CX2560X_VREG_V_MASK, reg_val);
}

static int cx2560x_recharge_voltage(
	struct cx2560x_device *cx, unsigned int rechg_mv)
{
	u8 reg_val;

	if (rechg_mv == 100)
		reg_val = CX2560X_RECHARGE_VOLTAGE_100MV;
	else
		reg_val = CX2560X_RECHARGE_VOLTAGE_200MV;

	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_4,
		CX2560X_RECHARGE_VOLTAGE_MASK, reg_val);
}

static int cx2560x_vindpm_track_set(
	struct cx2560x_device *cx, unsigned int vindpm_mv)
{
	u8 reg_val;

	if (vindpm_mv == 200)
		reg_val = CX2560X_VINDPM_TRACK_SET_200MV;
	else if (vindpm_mv == 250)
		reg_val = CX2560X_VINDPM_TRACK_SET_250MV;
	else if (vindpm_mv == 300)
		reg_val = CX2560X_VINDPM_TRACK_SET_300MV;
	else
		reg_val = CX2560X_VINDPM_TRACK_SET_DISABLE;

	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_7,
		CX2560X_VINDPM_TRACK_SET_MASK, reg_val);
}

static int cx2560x_set_boost_current(
	struct cx2560x_device *cx, int boost_current)
{
	u8 reg_val;

	if (boost_current >= 1200)
		reg_val = CX2560X_BOOST_CUR_1A2;
	else
		reg_val = CX2560X_BOOST_CUR_05A;

	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_2,
		CX2560X_BOOST_CUR_MASK, reg_val);
}

static int cx2560x_chipid_detect(struct cx2560x_device *cx)
{
	int ret = 0;
	u8 val = 0;

	ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_B, &val);
	if (ret < 0) {
		dev_err(cx->dev, "read CX2560X_CHRG_CTRL_B failed");
		cx->client->addr = 0x0b;
		ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_B, &val);
		if (ret < 0) {
			dev_err(cx->dev, "read SGM41543_CHRG_CTRL_B failed");
			return -EINVAL;
		}
	}

	val = val & CX2560X_PN_MASK;
	return val;
}

static int cx2560x_set_acovp(struct cx2560x_device *cx, int acovp)
{
	int ret = 0;
	u8 reg_val;

	if(12 == acovp) {
		ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_6, &reg_val);
		if (ret < 0) {
			dev_err(cx->dev, "cx2560x_read_reg CX2560X_CHRG_CTRL_6 failed\n");
			return -EINVAL;
		}
		reg_val = (reg_val & 0x3f) | 0xc0;
		ret = cx2560x_write_reg(cx, CX2560X_CHRG_CTRL_6, reg_val);
		if (ret < 0) {
			dev_err(cx->dev, "cx2560x_write_reg CX2560X_CHRG_CTRL_6 failed\n");
			return -EINVAL;
		}
	} else if (6 == acovp) {
		ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_6, &reg_val);
		if (ret < 0) {
			dev_err(cx->dev, "cx2560x_read_reg CX2560X_CHRG_CTRL_6 failed\n");
			return -EINVAL;
		}
		reg_val = (reg_val & 0x3f) | 0x40;
		ret = cx2560x_write_reg(cx, CX2560X_CHRG_CTRL_6, reg_val);
		if (ret < 0) {
			dev_err(cx->dev, "cx2560x_write_reg CX2560X_CHRG_CTRL_6 failed\n");
			return -EINVAL;
		}
	}else if (9 == acovp) {
		ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_6, &reg_val);
		if (ret < 0) {
			dev_err(cx->dev, "cx2560x_read_reg CX2560X_CHRG_CTRL_6 failed\n");
			return -EINVAL;
		}
		reg_val = (reg_val & 0x3f) | 0x80;
		ret = cx2560x_write_reg(cx, CX2560X_CHRG_CTRL_6, reg_val);
		if (ret < 0) {
			dev_err(cx->dev, "cx2560x_write_reg CX2560X_CHRG_CTRL_6 failed\n");
			return -EINVAL;
		}
	}

	return ret;
}

static int cx2560x_hardware_init(struct cx2560x_device *cx)
{
	int ret;

	ret = cx2560x_set_acovp(cx, 9);
	if (ret) {
		dev_err(cx->dev, "set cx2560x taper current failed");
		return -EPERM;
	}

	ret = cx2560x_set_taper_current(cx, 180);
	if (ret) {
		dev_err(cx->dev, "set cx2560x taper current failed");
		return -EPERM;
	}

	ret = cx2560x_set_prechg_current(cx, 300);
	if (ret) {
		dev_err(cx->dev, "set cx2560x precharge current failed");
		return -EPERM;
	}

	ret = cx2560x_set_battery_voltage(cx, 4380);
	if (ret) {
		dev_err(cx->dev, "set cx2560x charge voltage failed");
		return -EPERM;
	}

	ret = cx2560x_recharge_voltage(cx, 200);
	if (ret) {
		dev_err(cx->dev, "set cx2560x recharge voltage failed");
		return -EPERM;
	}

	ret = cx2560x_vindpm_track_set(cx, 200);
	if (ret) {
		dev_err(cx->dev, "set cx2560x vindpm track failed");
		return -EPERM;
	}

	ret = cx2560x_set_fast_charge_current(cx, 2200);
	if (ret) {
		dev_err(cx->dev, "set cx2560x fast charge current failed");
		return -EPERM;
	}

	ret = cx2560x_set_boost_current(cx, 1200);
	if (ret) {
		dev_err(cx->dev, "set cx2560x boost current failed");
		return -EPERM;
	}

	ret = cx2560x_disable_watchdog_timer(cx);
	if (ret) {
		dev_err(cx->dev, "disable cx2560x watchdog timer failed");
		return -EPERM;
	}

	ret = cx2560x_disable_dpm_irq(cx);
	if (ret) {
		dev_err(cx->dev, "disable cx2560x iindpm irq failed");
		return -EPERM;
	}

	return 0;
}

static int cx2560x_force_dpm_det(struct cx2560x_device *cx) {
	int ret;
	u8 reg_val;

	ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_7, &reg_val);
	if (ret) {
		dev_err(cx->dev, "get cx2560x 0x07 register failed");
		return -EINVAL;
	}
	if(0 != (reg_val & 0x80)) {
		dev_err(cx->dev, "cx2560x_force_dpm_det IINDET_EN=1\n");
		return -EINVAL;
	}
	reg_val = reg_val | 0x80;
	reg_val = reg_val & 0xf3;
	ret = cx2560x_write_reg(cx, CX2560X_CHRG_CTRL_7, reg_val);
	if (ret) {
		dev_err(cx->dev, "set cx2560x force dpm det register failed");
		return ret;
	}

	ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_7, &reg_val);
	if (ret) {
		dev_err(cx->dev, "get cx2560x 0x07 register failed");
		return -EINVAL;
	}
	dev_err(cx->dev, "cx2560x_force_dpm_det get 0x07 register  reg_val=%#x", reg_val);

	return 0;
}

static int cx2560x_get_charge_state(
	struct cx2560x_device *cx)
{
	int ret;
	u8 status;

	ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_8, &status);
	if (ret) {
		dev_err(cx->dev, "get cx2560x status register failed");
		return -EINVAL;
	}

	return status;
}

static int cx2560x_get_status(
	struct cx2560x_device *cx, struct cx2560x_state *state)
{
	int ret;
	u8 status, fault, ctrl;

	ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_8, &status);
	if (ret) {
		dev_err(cx->dev, "read cx2560x 0x08 register failed");
		return -EINVAL;
	}
	ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_9, &fault);
	if (ret) {
		dev_err(cx->dev, "read cx2560x 0x09 register failed");
		return -EINVAL;
	}
	ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_A, &ctrl);
	if (ret) {
		dev_err(cx->dev, "read cx2560x 0x0A register failed");
		return -EINVAL;
	}

	state->pg_stat = (status & CX2560X_PG_GOOD_MASK) >> 2;
	state->chrg_stat = (status & CX2560X_CHG_STAT_MASK) >> 3;
	state->vbus_stat = (status & CX2560X_VBUS_STAT_MASK) >> 5;
	state->online = !!(status & CX2560X_PG_STAT);
	state->vbus_gd = !!(ctrl & CX2560X_VBUS_GOOD);
	if (state->vbus_gd) {
		dev_err(cx->dev, "vbus_gd = 1");
	} else {
		dev_err(cx->dev, "vbus_gd = 0");
		ret = cx2560x_update_bits(cx, CX2560X_REG_10, REG10_DP_DAC_MASK,
		0 << REG10_DP_DAC_SHIFT);
		ret = cx2560x_update_bits(cx, CX2560X_REG_10, REG10_DM_DAC_MASK,
		0 << REG10_DM_DAC_SHIFT);
	}

	dev_info(cx->dev, "[08]: %#x, [09]: %#x, [0A]: %#x",status, fault, ctrl);

	//cx2560x_dump_register(cx->chg_dev);
	return 0;
}

static int cx2560x_get_initial_state(
	struct cx2560x_device *cx)
{
	int chg_state;

	chg_state = cx2560x_get_charge_state(cx);
	if (chg_state < 0) {
		dev_err(cx->dev, "get charger state failed\n");
		return -EINVAL;
	}

	if (chg_state & CX2560X_PG_GOOD_MASK) {
		cx->voltage_max = 5000000;
		cx->current_max = 1000000;
		cx->state.chrg_stat = CX2560X_FAST_CHARGE;
		cx->state.pg_stat = (chg_state & CX2560X_PG_GOOD_MASK) >> 2;
		cx->state.vbus_stat = (chg_state & CX2560X_VBUS_STAT_MASK) >> 5;
		//schedule_delayed_work(&cx->irq_work, msecs_to_jiffies(1000));
		schedule_delayed_work(&cx->charge_work, msecs_to_jiffies(30000));
		dev_info(cx->dev, "charger online during power up\n");
	}
	schedule_delayed_work(&cx->irq_work, msecs_to_jiffies(8000));
	return 0;
}

static int cx2560x_get_battery_temperature(
	struct cx2560x_device *cx)
{
	int ret;
	int charge_state = 0;
	union power_supply_propval psy_prop;

	ret = power_supply_get_property(cx->battery_psy,
			POWER_SUPPLY_PROP_TEMP, &psy_prop);
	if (ret) {
		dev_err(cx->dev, "get battery temp failed");
		return -ENODEV;
	}

	// set virtual temperature
	if (cx->vtemp != CX2560X_TEMP_INVALID) {
		psy_prop.intval = cx->vtemp;
		dev_info(cx->dev, "set vtemp: %d", cx->vtemp);
	}

	cx->temp = psy_prop.intval;
	dev_info(cx->dev, "battery temp: %d", cx->temp);

	if (cx->temp <= -180 || cx->temp > 550)
		charge_state = CX2560X_CHG_DISABLE;
	else if (cx->temp > -170 && cx->temp < 0)
		charge_state = CX2560X_STATE_COLD;
	else if (cx->temp >= 0 && cx->temp < 150)
		charge_state = CX2560X_STATE_COOL;
	else if (cx->temp > 150 && cx->temp <= 450)
		charge_state = CX2560X_STATE_NORMAL;
	else if (cx->temp > 450 && cx->temp <= 550)
		charge_state = CX2560X_STATE_WARM;

	return charge_state;
}


static inline int cx2560x_set_cvcharge_5v(
	struct cx2560x_device *cx)
{
	int ret;
	union power_supply_propval psy_prop;

	ret = power_supply_get_property(cx->battery_psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &psy_prop);
	if (ret) {
		dev_err(cx->dev, "get battery charge current failed");
		return -ENODEV;
	}

	// dpdm should only be controlled in dcp
	// dpdm should be HIZ state in sdp/cdp
	if (cx->state.vbus_stat == CX2560X_USB_DCP) {
		dev_info(cx->dev, "enable dp dm for dcp 5v");
		cx2560x_set_hvdcp_off(cx);
	}

	return 0;
}

static void cx2560x_charge_online_work(
	struct work_struct *work)
{
	int ret, err = 0;
	int new_temp;
	int chg_state;
	union power_supply_propval psy_prop;

	struct cx2560x_device *cx = container_of(
		work, struct cx2560x_device, charge_work.work);

	if (!cx->battery_psy) {
		cx->battery_psy = power_supply_get_by_name("battery");
		if (!cx->battery_psy) {
			err = -ENODEV;
			dev_err(cx->dev, "get battery power supply failed");
			goto out;
		}
	}
	/*get Vbat from gauge*/
	ret = power_supply_get_property(cx->battery_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &psy_prop);
	cx->batt_vol = psy_prop.intval / 1000;
	if (cx->batt_vol > 4300) {
		ret = cx2560x_set_cvcharge_5v(cx);
		if (ret) {
			err = -EPERM;
			dev_err(cx->dev, "set constant charge voltage to 5v failed");
			goto out;
		}
	}
	/*get Ibat from gauge*/
	ret = power_supply_get_property(cx->battery_psy,
			POWER_SUPPLY_PROP_CURRENT_NOW, &psy_prop);
	cx->batt_curr = psy_prop.intval / 1000;

	dev_info(cx->dev, "%s: Vbat = %d mV, Ibat = %d mA\n",
			__func__, cx->batt_vol, cx->batt_curr);
	new_temp = cx2560x_get_battery_temperature(cx);
	if (new_temp < 0) {
		err = -ENODEV;
		dev_err(cx->dev, "get charge state failed");
		goto out;
	}

	ret = cx2560x_get_charge_state(cx);
	if (ret < 0) {
		err = -EINVAL;
		dev_err(cx->dev, "get charge state failed");
		goto out;
	}
	chg_state = (ret & CX2560X_CHG_STAT_MASK) >> 3;
	if (cx->state.chrg_stat != chg_state) {
		cx->state.chrg_stat = chg_state;
		dev_info(cx->dev, "charge state: %s",
			cx2560x_charge_state[chg_state]);
		power_supply_changed(cx->charger_psy);
	}

out:
	if (err || !cx->state.pg_stat)
		cancel_delayed_work(&cx->charge_work);
	else
		schedule_delayed_work(&cx->charge_work, msecs_to_jiffies(1000));
}

static void cx2560x_psy_change_work(
	struct work_struct *work)
{
	struct cx2560x_device *cx = container_of(
		work, struct cx2560x_device, psy_work.work);

	power_supply_changed(cx->charger_psy);
}

static int cx2560x_set_volt_to_reg(enum dpdm_set_volt volt)
{
	int reg_val = 0;

	switch(volt) {
		case DPDM_SET_VOLT_HZ:
			reg_val = 0x00;
		break;
		case DPDM_SET_VOLT_0_V:
			reg_val = 0x01;
		break;
		case DPDM_SET_VOLT_0_6_V:
			reg_val = 0x02;
		break;
		case DPDM_SET_VOLT_3_3_V:
			reg_val = 0x06;
		break;
		default:
			reg_val = 0x00;
	}

	return reg_val;
}

static int cx2560x_set_dpvolt(struct charger_device *chg_dev, enum dpdm_set_volt volt)
{
	struct cx2560x_device *cx = charger_get_data(chg_dev);
	int reg_val = 0;
	reg_val = cx2560x_set_volt_to_reg(volt);

	reg_val = reg_val << 4;
	dev_info(cx->dev, "%s: set_dp=%duV reg_val=0x%x\n", __func__, volt, reg_val);
	return 	cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,
		CX2560X_DP_VSEL_MASK, reg_val);
}
static int cx2560x_set_dmvolt(struct charger_device *chg_dev, enum dpdm_set_volt volt)
{
	struct cx2560x_device *cx = charger_get_data(chg_dev);
	int reg_val = 0;
	reg_val = cx2560x_set_volt_to_reg(volt);

	dev_info(cx->dev, "%s: set_dm = %duV reg_val=9x%x\n", __func__, volt, reg_val);
	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,
		CX2560X_DM_VSEL_MASK, reg_val);
}

static void cx2560x_set_hvdcp_en(struct cx2560x_device *cx)
{
	//cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,0x01<<3,0x01<<3);
}
static void cx2560x_set_hvdcp_off(struct cx2560x_device *cx)
{
	//cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,0x01<<3,0x00<<3);
}
static void cx2560x_set_dpdm_hiz(struct cx2560x_device *cx)
{
	cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,
		CX2560X_DP_VSEL_MASK | CX2560X_DM_VSEL_MASK,
		CX2560X_DP_HIZ | CX2560X_DM_HIZ);
}

int dpdm_flag = 0;

static irqreturn_t cx2560x_irq_handler_thread(
	int irq, void *private)
{
	bool prev_vbus_gd;
	struct cx2560x_state state;
	struct cx2560x_device *cx = private;
	u8 data;
	//int ret = 0;

	dev_info(cx->dev, "charger isr");
	prev_vbus_gd = cx->state.vbus_gd;
	cx2560x_get_status(cx, &state);
	if(prev_vbus_gd != cx->state.vbus_gd) {
		cx2560x_set_dpdm_hiz(cx);
	}

	// status not changed
	if (cx->state.pg_stat == state.pg_stat) {
		dev_info(cx->dev, "state not changed");
		return IRQ_HANDLED;
	}
	cx->state = state;

	if (!prev_vbus_gd && cx->state.vbus_gd) {

		if (!cx->charger_wakelock->active)
			__pm_stay_awake(cx->charger_wakelock);

		dev_info(cx->dev, "%s: adapter/usb inserted\n", __func__);
		Charger_Detect_Init();
		dev_info(cx->dev, "insert: chg_type = %d, psy_usb_type = %d,cx->state.vbus_stat=%d\n", cx->chg_type, cx->psy_usb_type, cx->state.vbus_stat);
		if (cx->usb2_phy->otg->gadget){
			//ret = usb_gadget_connect(cx->usb2_phy->otg->gadget);
			pr_err("%s: gadget->state: %d\n", __func__,cx->usb2_phy->otg->gadget->state);
		}

		force_dpdm_count = 3;
		no_usb_flag = 0;
		usb_detect_flag = false;
		usb_detect_done = false;

		cx->force_detect_count = 0;
		cx2560x_read_reg(cx,CX2560X_CHRG_CTRL_8,&data);
		if (data & 0x04) {
			schedule_delayed_work(&cx->vindpm_work, msecs_to_jiffies(3000));
		}
		else {
			cancel_delayed_work(&cx->vindpm_work);
		}
	} else if (prev_vbus_gd && !cx->state.vbus_gd) {
		dev_info(cx->dev, "%s: adapter/usb removed\n", __func__);
		__pm_relax(cx->charger_wakelock);

		//disable-otg
		cx2560x_disable_otg(cx);
		cx2560x_set_hvdcp_off(cx);
		Charger_Detect_Init();
		cx2560x_set_dpdm_hiz(cx);

		cx->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		cx2560x_charger_desc.type =POWER_SUPPLY_TYPE_UNKNOWN;
		dev_info(cx->dev, "no input detected");

		cancel_delayed_work(&cx->charge_usb_detect_work);
		no_usb_flag = 0;

		cx2560x_read_reg(cx,CX2560X_CHRG_CTRL_8,&data);
		if (data & 0x04) {
			schedule_delayed_work(&cx->vindpm_work, msecs_to_jiffies(3000));
		}
		else {
			cancel_delayed_work(&cx->vindpm_work);
		}
		dpdm_flag = 0;

	}

	if (cx->state.pg_stat) {
		cx->state.chrg_stat = CX2560X_PRECHARGE;
		cx2560x_set_usbin_current_limit(cx, 500);
		schedule_delayed_work(&cx->irq_work, msecs_to_jiffies(700));
		schedule_delayed_work(&cx->charge_work, msecs_to_jiffies(3000));
		dev_info(cx->dev, "schedule irq work");
	} else {
		cx->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		cx2560x_charger_desc.type =POWER_SUPPLY_TYPE_UNKNOWN;
		cx->ibat = 3000;
		cx->voltage_max = 0;
		cx->current_max = 0;
		cx->state.vbus_stat = 0;
		cx->state.chrg_stat = 0;
		cx->temp_state = CX2560X_STATE_NORMAL;
		cx2560x_charge_enable_ctrl(cx, 1);
		cancel_delayed_work(&cx->irq_work);
		cancel_delayed_work(&cx->charge_work);
		dev_info(cx->dev, "cancel irq work");
	}

	power_supply_changed(cx->charger_psy);

	return IRQ_HANDLED;
}

static void cx2560x_charge_irq_work(
	struct work_struct *work)
{
	struct cx2560x_state state;
	struct cx2560x_device *cx = container_of(
		work, struct cx2560x_device, irq_work.work);

	cx2560x_set_dpdm_hiz(cx);

	dev_info(cx->dev, "irq work");
	if (!cx->charger_wakelock->active)
		__pm_stay_awake(cx->charger_wakelock);

	cx2560x_get_status(cx, &state);
	cx->state.vbus_stat = state.vbus_stat;

	if (state.vbus_stat == CX2560X_USB_NONE) {
		cx->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		cx2560x_charger_desc.type =POWER_SUPPLY_TYPE_UNKNOWN;
		Charger_Detect_Init();
		cx2560x_set_hvdcp_off(cx);
		cx2560x_set_dpdm_hiz(cx);
		cx->ibat = 3000;
		cx->voltage_max = 0;
		cx->current_max = 0;
		cx->state.vbus_stat = 0;
		cx->state.chrg_stat = 0;
		cx->temp_state = CX2560X_STATE_NORMAL;
		cx2560x_charge_enable_ctrl(cx, 1);
		cancel_delayed_work(&cx->charge_work);
		cancel_delayed_work(&cx->vindpm_work);
		dev_info(cx->dev, "no input detected");
	} else if (state.vbus_stat == CX2560X_USB_DCP) {
		dev_info(cx->dev, "dcp detected1");
		Charger_Detect_Init();
		cx->voltage_max = 5000000;
		cx->current_max = 2000000;
		cx->chg_type = POWER_SUPPLY_TYPE_USB_DCP;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		cx2560x_charger_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		cx2560x_charge_enable_ctrl(cx, 0);
		cx2560x_set_vindpm(cx, 8400);
		msleep(1000);
		cx2560x_set_hvdcp_en(cx);
		msleep(1000);
		cx2560x_set_usbin_current_limit(cx, 1400);
		cx2560x_charge_enable_ctrl(cx, 1);
		schedule_delayed_work(&cx->vindpm_work, msecs_to_jiffies(300));
		dev_info(cx->dev, "dcp detected2");
	} else if (state.vbus_stat == CX2560X_USB_SDP) {
		//Charger_Detect_Release();

		if(!usb_detect_flag)
			schedule_delayed_work(&cx->charge_usb_detect_work, 5 * HZ);
		if(no_usb_flag == 0){
			pr_info("[%s] CX2589x charger type: SDP\n", __func__);
			cx->chg_type = POWER_SUPPLY_TYPE_USB;
			cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;

			cx2560x_set_usbin_current_limit(cx, 500);
			dev_info(cx->dev, "sdp detected");
		}else{
			pr_info("[%s] CX2589x charger type: UNKNOWN/FLOAT\n", __func__);
			cx->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
			cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			cx2560x_set_usbin_current_limit(cx, 1000);
			dev_info(cx->dev, "unknown charge detected");
		}
		cx->voltage_max = 5000000;
		cx->current_max = 500000;
		cx2560x_charger_desc.type =POWER_SUPPLY_TYPE_USB;
	} else if (cx->state.vbus_stat == CX2560X_USB_CDP) {
		//Charger_Detect_Release();
		cx->voltage_max = 5000000;
		cx->current_max = 1500000;
		cx->chg_type = POWER_SUPPLY_TYPE_USB_CDP;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		cx2560x_charger_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		cx2560x_set_usbin_current_limit(cx, 1500);
		dev_info(cx->dev, "cdp detected");
	} else if (cx->state.vbus_stat == CX2560X_USB_UNKNOWN) {
		//Charger_Detect_Init();
		cx->voltage_max = 5000000;
		cx->current_max = 500000;
		cx->chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
		cx->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		cx2560x_charger_desc.type =POWER_SUPPLY_TYPE_USB;
		cx2560x_set_usbin_current_limit(cx, 1000);
		dev_info(cx->dev, "unknown charge detected");
	} else if (cx->state.vbus_stat == CX2560X_USB_NSTANDA) {
		 //Charger_Detect_Init();
		cx->voltage_max = 5000000;
		cx->current_max = 1000000;
		cx2560x_set_usbin_current_limit(cx, 1000);
		dev_info(cx->dev, "not standard detected");
		cx2560x_force_dpdm(cx);
	}

	//cx2560x_get_charger_type(cx);
	dev_info(cx->dev, "Update: chg_type = %d, psy_usb_type = %d,cx->state.vbus_stat=%d\n", cx->chg_type, cx->psy_usb_type, cx->state.vbus_stat);
	power_supply_changed(cx->charger_psy);
	if(true == usb_detect_done || state.vbus_stat != CX2560X_USB_SDP ) {
		__pm_relax(cx->charger_wakelock);
	}
}

static void charger_usb_detect_work_func(struct work_struct *work)
{
	struct delayed_work *charge_usb_detect_work = NULL;
	struct cx2560x_device *cx = NULL;
	struct cx2560x_state state;

	//int ret;

	charge_usb_detect_work = container_of(work, struct delayed_work, work);
	if (charge_usb_detect_work == NULL) {
		pr_err("Cann't get charge_usb_detect_work\n");
		return;
	}

	cx = container_of(charge_usb_detect_work, struct cx2560x_device, charge_usb_detect_work);
	if (cx == NULL) {
		pr_err("Cann't get cx2560x_device\n");
		return;
	}
	usb_detect_flag = true;
	pr_err("%s: enter\n", __func__);
	if (cx->usb2_phy->otg->gadget){
		//ret = usb_gadget_connect(cx->usb2_phy->otg->gadget);
		pr_err("%s: gadget->state: %d\n", __func__,cx->usb2_phy->otg->gadget->state);
		if(cx->usb2_phy->otg->gadget->state == 0){
			do{
				pr_err("%s: SDP retry:%d\n", __func__,force_dpdm_count);
				cx2560x_force_dpdm(cx);
				msleep(1000);
				cx2560x_get_status(cx, &state);
				if(state.vbus_stat != CX2560X_USB_SDP)
					break;
				msleep(2000);
			}while(force_dpdm_count-- > 0);
			if(state.vbus_stat == CX2560X_USB_SDP)
				no_usb_flag = 1;
		}else{
			no_usb_flag = 0;
		}
		pr_err("%s: exit\n", __func__);
		schedule_delayed_work(&cx->irq_work, 0);
	}
	usb_detect_done = true;
	return;
}

static int cx2560x_get_chrg_volt(struct charger_device *chg_dev, unsigned int *volt);
static int cx2560x_charger_get_property(
	struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int value = 0;

	int vbus = 0;
	u32 chg_cv = 0;
	struct cx2560x_device *cx = power_supply_get_drvdata(psy);
	struct cx2560x_state state;

	cx2560x_get_status(cx, &state);
	cx->state = state;
	switch (psp) {
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = cx2560x_charger_desc.type;
		dev_info(cx->dev, "charger psy prop type: %d", val->intval);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if (cx->state.pg_stat)
			val->intval = 1;
		else
			val->intval = 0;
		dev_info(cx->dev, "charger online: %d", val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = cx2560x_charge_type[cx->state.chrg_stat];
		dev_info(cx->dev, "charger psy charge type: %d", val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		value = cx->state.chrg_stat;
		if (value == CX2560X_PRECHARGE || value == CX2560X_FAST_CHARGE)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (!cx->state.pg_stat)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (cx->state.pg_stat && !cx->chg_en)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (value == CX2560X_TERM_CHARGE)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		dev_info(cx->dev, "charger psy prop status: %d", val->intval);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		if (!cx->state.vbus_gd) {
			dev_info(cx->dev, "vbus not good\n");
			val->intval = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		} else {
			dev_info(cx->dev, "vbus is good\n");
			if (cx->state.vbus_stat == 0)
				val->intval = POWER_SUPPLY_USB_TYPE_UNKNOWN;
			if (cx->state.vbus_stat == 1)
				val->intval = POWER_SUPPLY_USB_TYPE_SDP;
			if (cx->state.vbus_stat == 2)
				val->intval = POWER_SUPPLY_USB_TYPE_CDP;
			if (cx->state.vbus_stat == 3)
				val->intval = POWER_SUPPLY_USB_TYPE_DCP;
			if (cx->state.vbus_stat == 5)
				val->intval = POWER_SUPPLY_USB_TYPE_SDP;
			if (cx->state.vbus_stat == 6)
				val->intval = POWER_SUPPLY_USB_TYPE_DCP;
		}
		dev_info(cx->dev, "charger usb type: %d,cx->state.vbus_stat=%d", val->intval,cx->state.vbus_stat);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		get_vbus_voltage_ext(&vbus);
		val->intval = vbus;
		dev_info(cx->dev, "charger voltage: %d", val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		cx2560x_get_chrg_volt(cx->chg_dev, &chg_cv);
		val->intval = chg_cv;
		dev_info(cx->dev, "charger constant voltage:%d\n", val->intval);
		break;
	default:
		dev_err(cx->dev, "get charger prop: %u is not supported", psp);
		return -EINVAL;
	}

	return 0;
}

static struct power_supply_desc cx2560x_charger_desc = {
	.name = "cx2560x",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = cx2560x_usb_type,
	.num_usb_types = ARRAY_SIZE(cx2560x_usb_type),
	.properties = cx2560x_charger_props,
	.num_properties = ARRAY_SIZE(cx2560x_charger_props),
	.get_property = cx2560x_charger_get_property,
};

static char *cx2560x_charger_supplied_to[] = {
	"battery",
	"mtk-master-charger",
};

static struct power_supply *cx2560x_register_charger_psy(
	struct cx2560x_device *cx)
{
	struct power_supply_config psy_cfg = {
		.drv_data = cx,
		.of_node = cx->dev->of_node,
	};

	psy_cfg.supplied_to = cx2560x_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(cx2560x_charger_supplied_to);
	return power_supply_register(cx->dev, &cx2560x_charger_desc, &psy_cfg);
}

static int cx2560x_ac_get_property(
	struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct cx2560x_device *cx = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (cx->state.vbus_stat == CX2560X_USB_DCP ||
			cx->state.vbus_stat == CX2560X_USB_UNKNOWN ||
			cx->state.vbus_stat == CX2560X_USB_NSTANDA)
			val->intval = 1;
		else
			val->intval = 0;
		dev_info(cx->dev, "ac online: %d", val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = cx->current_max;
		dev_info(cx->dev, "ac current maxium: %d", cx->current_max);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = cx->voltage_max;
		dev_info(cx->dev, "ac voltage maxium: %d", cx->voltage_max);
		break;
	default:
		dev_err(cx->dev, "get ac prop: %u is not supported", psp);
		return -EINVAL;
	}

	return 0;
}

static struct power_supply_desc cx2560x_ac_desc = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = cx2560x_ac_props,
	.num_properties = ARRAY_SIZE(cx2560x_ac_props),
	.get_property = cx2560x_ac_get_property,
};

static struct power_supply *cx2560x_register_ac_psy(
	struct cx2560x_device *cx)
{
	struct power_supply_config psy_cfg = {
		.drv_data = cx,
		.of_node = cx->dev->of_node,
	};

	return power_supply_register(cx->dev, &cx2560x_ac_desc, &psy_cfg);
}

static int cx2560x_usb_get_property(
	struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct cx2560x_device *cx = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB;
		dev_info(cx->dev, "usb psy prop type: %d", val->intval);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if (cx->state.vbus_stat == CX2560X_USB_SDP ||
			cx->state.vbus_stat == CX2560X_USB_CDP)
			val->intval = 1;
		else
			val->intval = 0;
		dev_info(cx->dev, "usb online: %d", val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = cx->current_max;
		dev_info(cx->dev, "usb current maxium: %d", cx->current_max);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = cx->voltage_max;
		dev_info(cx->dev, "usb voltage maxium: %d", cx->voltage_max);
		break;
	default:
		dev_err(cx->dev, "get usb prop: %u is not supported", psp);
		return -EINVAL;
	}

	return 0;
}

static struct power_supply_desc cx2560x_usb_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = cx2560x_usb_props,
	.num_properties = ARRAY_SIZE(cx2560x_usb_props),
	.get_property = cx2560x_usb_get_property,
};

static struct power_supply *cx2560x_register_usb_psy(
	struct cx2560x_device *cx)
{
	struct power_supply_config psy_cfg = {
		.drv_data = cx,
		.of_node = cx->dev->of_node,
	};

	return power_supply_register(cx->dev, &cx2560x_usb_desc, &psy_cfg);
}

static int cx2560x_enable_vbus(struct regulator_dev *rdev)
{
	struct cx2560x_device *cx = rdev_get_drvdata(rdev);

	cx->otg_mode = 1;
	dev_info(cx->dev, "enable otg");
	return cx2560x_update_bits(cx,
			CX2560X_CHRG_CTRL_1, CX2560X_OTG_EN, CX2560X_OTG_EN);
}

static int cx2560x_disable_vbus(struct regulator_dev *rdev)
{
	struct cx2560x_device *cx = rdev_get_drvdata(rdev);

	cx->otg_mode = 0;
	dev_info(cx->dev, "dibable otg");
	return cx2560x_update_bits(cx,
		CX2560X_CHRG_CTRL_1, CX2560X_OTG_EN, 0);
}

static int cx2560x_vbus_state(struct regulator_dev *rdev)
{
	u8 otg_en;
	struct cx2560x_device *cx = rdev_get_drvdata(rdev);

	cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_1, &otg_en);
	otg_en = !!(otg_en & CX2560X_OTG_EN);
	dev_info(cx->dev, "otg state: %d", otg_en);

	return otg_en;
}

static struct regulator_ops cx2560x_vbus_ops = {
	.enable = cx2560x_enable_vbus,
	.disable = cx2560x_disable_vbus,
	.is_enabled = cx2560x_vbus_state,
};

static const struct regulator_desc cx2560x_otg_rdesc = {
	.name = "cx2560x-vbus",
	.of_match = "cx2560x-otg-vbus",
	.ops = &cx2560x_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
};

static struct regulator_dev *cx2560x_register_regulator(
	struct cx2560x_device *cx)
{
	struct regulator_config config = {
		.dev = cx->dev,
		.driver_data = cx,
	};

	return regulator_register(&cx2560x_otg_rdesc, &config);
}

static int cx2560x_gpio_config(struct cx2560x_device *cx)
{
	int ret, irqn;

	cx->en_gpio = of_get_named_gpio(cx->dev->of_node, "cx,chg-en-gpio", 0);
	if (!gpio_is_valid(cx->en_gpio)) {
		dev_err(cx->dev, "get cx,chg-en-gpio failed");
		return -EINVAL;
	}
	ret = gpio_request_one(cx->en_gpio, GPIOF_OUT_INIT_LOW, "cx_chg_en_pin");
	if (ret) {
		dev_err(cx->dev, "request cx,chg-en-gpio failed");
		return -EPERM;
	}

	cx->irq_gpio = of_get_named_gpio(cx->dev->of_node, "cx,intr_gpio", 0);
	if (!gpio_is_valid(cx->irq_gpio)) {
		dev_err(cx->dev, "get cx,irq-gpio failed\n");
		goto err_out;
	}
	irqn = gpio_to_irq(cx->irq_gpio);
	if (irqn > 0)
		cx->client->irq = irqn;
	else {
		dev_err(cx->dev, "map cx,irq-gpio to irq failed");
		goto err_out;
	}

	return 0;

err_out:
	gpio_free(cx->en_gpio);
	return -EINVAL;
}

static ssize_t cx2560x_reg_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t size)
{
	int i, ret;
	char *reg_val[2];
	ulong addr, val;
	struct cx2560x_device *cx = dev_get_drvdata(dev);

	if (strstr(buf, ":")) {
		for (i = 0; i < ARRAY_SIZE(reg_val); i++)
			reg_val[i] = strsep((char **)&buf, ":");

		ret = kstrtoul(reg_val[0], 16, &addr);
		if (ret < 0) {
			dev_err(dev, "get reg addr failed\n");
			return -EINVAL;
		}

		ret = kstrtoul(reg_val[1], 16, &val);
		if (ret < 0) {
			dev_err(dev, "get reg val failed\n");
			return -EINVAL;
		}
		ret = i2c_smbus_write_byte_data(cx->client, addr, val);
		if (ret < 0) {
			dev_err(dev, "write reg[%x]: %x failed\n",
				(uint32_t)addr, (uint32_t)val);
			return -EIO;
		}
	} else {
		ret = kstrtoul(buf, 16, &val);
		if (ret) {
			dev_err(dev, "kstrtoul [%s] failed\n", buf);
			return -EINVAL;
		}
		cx->reg_addr = val;
	}

	return size;
}

static struct device_attribute dev_attr_reg =
	__ATTR(reg, S_IWUSR, NULL, cx2560x_reg_store);

static ssize_t cx2560x_reg_dump(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	int i, ret;
	ssize_t len = 0;
	struct cx2560x_device *cx = dev_get_drvdata(dev);
	u8 reg_index[16] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	};

	for (i = 0; i < ARRAY_SIZE(reg_index); i++) {
		ret = i2c_smbus_read_byte_data(cx->client, reg_index[i]);
		if (ret < 0) {
			dev_err(dev, "read reg [%#x] failed\n", cx->reg_addr);
			return -EINVAL;
		}
		len += snprintf(buf + len, PAGE_SIZE, "%#04x: %#04x\n", reg_index[i], ret);
	}

	return len;
}

static struct device_attribute dev_attr_dump =
	__ATTR(dump, S_IRUSR, cx2560x_reg_dump, NULL);

static ssize_t cx2560x_charge_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t size)
{
	struct cx2560x_device *cx = dev_get_drvdata(dev);

	if (!strncmp(buf, "1", 1)) {
		cx->qc_force = QC_TYPE_FORCE_1P0;
		dev_info(dev, "enable qc1.0");
	} else if (!strncmp(buf, "2", 1)) {
		cx->qc_force = QC_TYPE_FORCE_2P0;
		dev_info(dev, "enable qc2.0");
	} else if (!strncmp(buf, "3", 1)) {
		cx->qc_force = QC_TYPE_FORCE_3P0;
		dev_info(dev, "enable qc3.0");
	} else {
		dev_err(dev, "invalid parameter: %s", buf);
		return -EINVAL;
	}

	return size;
}

static struct device_attribute dev_attr_charge =
	__ATTR(charge, S_IWUSR, NULL, cx2560x_charge_store);

static ssize_t cx2560x_dpdm_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t size)
{
	struct cx2560x_device *cx = dev_get_drvdata(dev);

	if (!strncmp(buf, "dp", 2)) {
		cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,
			CX2560X_DP_VSEL_MASK, CX2560X_DP_3V3);
		usleep_range(100, 100);
		cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,
			CX2560X_DP_VSEL_MASK, CX2560X_DP_0V6);
	} else if (!strncmp(buf, "dm", 2)) {
		cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,
			CX2560X_DM_VSEL_MASK, CX2560X_DM_0V6);
		usleep_range(1000, 1000);
		cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,
			CX2560X_DM_VSEL_MASK, CX2560X_DM_3V3);
	} else {
		dev_err(dev, "error params");
		return -EINVAL;
	}

	return size;
}

static struct device_attribute dev_attr_dpdm =
	__ATTR(dpdm, S_IWUSR, NULL, cx2560x_dpdm_store);

static ssize_t cx2560x_qc3v0_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t size)
{
	struct cx2560x_device *cx = dev_get_drvdata(dev);

	sscanf(buf, "%d %d\n", &cx->chg_volt, &cx->chg_curr);
	dev_info(dev, "qc3.0 voltage: %d, current: %d",
		cx->chg_volt, cx->chg_curr);

	return size;
}

static struct device_attribute dev_attr_qc3v0 =
	__ATTR(qc3v0, S_IWUSR, NULL, cx2560x_qc3v0_store);

static ssize_t cx2560x_vtemp_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t size)
{
	struct cx2560x_device *cx = dev_get_drvdata(dev);

	sscanf(buf, "%d\n", &cx->vtemp);
	cx2560x_vtemp = cx->vtemp;
	schedule_delayed_work(&cx->psy_work, msecs_to_jiffies(100));
	dev_info(dev, "virtual temperature %d", cx->vtemp);

	return size;
}

static struct device_attribute dev_attr_vtemp =
	__ATTR(vtemp, S_IWUSR, NULL, cx2560x_vtemp_store);

static int cx2560x_charging_switch(
	struct charger_device *chg_dev, bool enable)
{
	struct cx2560x_device *cx = charger_get_data(chg_dev);

	if (enable) {
		cx2560x_charge_enable_ctrl(cx, 1);
	} else {
		cx2560x_charge_enable_ctrl(cx, 0);
	}
	return 0;
}

static int cx2560x_set_ichrg_curr(
		struct charger_device *chg_dev, unsigned int chrg_curr)
{
	u8 reg_val;
	int ret = 0;
	int batt_vol = 0;
	union power_supply_propval psy_prop;

	struct cx2560x_device *cx = charger_get_data(chg_dev);

	chrg_curr = chrg_curr / 1000;

	if (!cx->battery_psy) {
		cx->battery_psy = power_supply_get_by_name("battery");
		if (!cx->battery_psy) {
			dev_err(cx->dev, "get battery power supply failed");
			return -ENODEV;
		}
	}

	ret = power_supply_get_property(cx->battery_psy,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &psy_prop);
	batt_vol = psy_prop.intval / 1000;

	if(chrg_curr<0)
		chrg_curr=0;

	if (chrg_curr > 2875)
		chrg_curr = 2875;

	if(chrg_curr<59)
		reg_val=0;
	else if(chrg_curr>=59 && chrg_curr<=815)
		reg_val=(chrg_curr-59)/63+1;
	else
		reg_val=((chrg_curr-805)*2)/115+14;

	pr_info("[%s] chrg_curr=%d, reg_val=%d\n", __func__, chrg_curr, reg_val);
	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_2,
					CX2560X_ICHRG_CUR_MASK,
					reg_val);
}

static int cx2560x_get_input_curr_lim(
	struct charger_device *chg_dev, unsigned int *ilim)
{
	int ret;
	u8 reg_val;
	struct cx2560x_device *cx = charger_get_data(chg_dev);

	ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_0, &reg_val);
	if (ret)
		return -EINVAL;
	if (CX2560X_IINDPM_I_MASK == (reg_val & CX2560X_IINDPM_I_MASK))
		*ilim = CX2560X_IINDPM_I_MAX_MA;
	else
		*ilim = ((reg_val & CX2560X_IINDPM_I_MASK)
				* CX2560X_IINDPM_STEP_MA
				+ CX2560X_IINDPM_I_MIN_MA) * 1000;

	return 0;
}

static int cx2560x_set_input_curr_lim(
	struct charger_device *chg_dev, unsigned int iindpm)
{
	struct cx2560x_device *cx = charger_get_data(chg_dev);

	pr_info("[%s] %d\n", __func__, iindpm);
	cx2560x_set_usbin_current_limit(cx, iindpm / 1000);
	return 0;
}

static int cx2560x_get_chrg_volt(
	struct charger_device *chg_dev, unsigned int *volt)
{
	int ret;
	u8 vreg_val;
	struct cx2560x_device *cx = charger_get_data(chg_dev);

	ret = cx2560x_read_reg(cx, CX2560X_CHRG_CTRL_4, &vreg_val);
	if (ret)
		return ret;

	vreg_val = (vreg_val & CX2560X_VREG_V_MASK) >> 3;

	if (15 == vreg_val)
		*volt = 4352000;
	else if (vreg_val < 25)
		*volt = (vreg_val * CX2560X_VREG_V_STEP_MV
				+ CX2560X_VREG_V_MIN_MV) * 1000;

	return 0;
}

static int cx2560x_set_chrg_volt(
	struct charger_device *chg_dev, unsigned int chrg_volt)
{
	int ret;
	struct cx2560x_device *cx = charger_get_data(chg_dev);
	pr_err("cx2560x_set_chrg_volt chrg_volt=%d\n", chrg_volt);

	ret = cx2560x_set_battery_voltage(cx, chrg_volt/1000);
	return 0;
}

static int cx2560x_set_wdt_rst(
	struct cx2560x_device *cx, bool is_rst)
{
	u8 val;

	if (is_rst)
		val = CX2560X_WDT_RST_MASK;
	else
		val = 0;

	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_1,
			CX2560X_WDT_RST_MASK, val);
}

static int cx2560x_reset_watch_dog_timer(
	struct charger_device *chg_dev)
{
	struct cx2560x_device *cx = charger_get_data(chg_dev);

	return cx2560x_set_wdt_rst(cx, 0x1);
}

static int cx2560x_set_input_volt_lim(
	struct charger_device *chg_dev, unsigned int vindpm)
{
	return 0;
}

static int cx2560x_get_vbus(
	struct charger_device *chg_dev, u32* vbus)
{
	u32 val;
	get_vbus_voltage_ext(&val);
	*vbus = val;
	return 0;
}

static int cx2560x_get_charging_status(
	struct charger_device *chg_dev, bool *is_done)
{
	struct cx2560x_device *cx = charger_get_data(chg_dev);

	if (cx->state.chrg_stat == CX2560X_TERM_CHARGE)
		*is_done = true;
	else
		*is_done = false;

	return 0;
}

static inline int cx2560x_set_en_timer(
	struct cx2560x_device *cx)
{
	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_5,
				CX2560X_SAFETY_TIMER_EN, CX2560X_SAFETY_TIMER_EN);
}

static inline int cx2560x_set_disable_timer(
	struct cx2560x_device *cx)
{
	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_5,
				CX2560X_SAFETY_TIMER_EN, 0);
}

static int cx2560x_enable_safetytimer(
	struct charger_device *chg_dev, bool en)
{
	return 0;
}

static int cx2560x_get_is_safetytimer_enable(
	struct charger_device *chg_dev, bool *en)
{
	int ret = 0;
	u8 val = 0;

	struct cx2560x_device *cx = charger_get_data(chg_dev);

	ret = cx2560x_read_reg(cx,CX2560X_CHRG_CTRL_5,&val);
	if (ret < 0) {
		pr_info("[%s] read SGM4154x_CHRG_CTRL_5 fail\n", __func__);
		return ret;
	} else
		*en = !!(val & CX2560X_SAFETY_TIMER_EN);

	return 0;
}

static int cx2560x_enable_otg(struct cx2560x_device *cx)
{
    int ret;

	ret = cx2560x_update_bits(cx, CX2560X_REG_02, 0x80, 0x00);
	if (ret)
		dev_info(cx->dev, "temporary iboost limit 500mA failed\n");


	ret = cx2560x_update_bits(cx, CX2560X_REG_01,
					REG01_OTG_CONFIG_MASK, REG01_OTG_DISABLE << REG01_OTG_CONFIG_SHIFT);
	if (ret != 0)
	    dev_info(cx->dev, "disable cx25601x otg failed\n");

	ret=cx2560x_write_reg40(cx, 1);
	if(ret != 0)
		dev_info(cx->dev, " otg write reg40 successfully; \n");

	ret = cx2560x_write_reg(cx,0x83,0x03);
	ret = cx2560x_write_reg(cx,0x41,0x88);
	ret = cx2560x_update_bits(cx, CX2560X_REG_01,REG01_OTG_CONFIG_MASK,
		REG01_OTG_ENABLE << REG01_OTG_CONFIG_SHIFT);
	msleep(100);
	ret = cx2560x_update_bits(cx, CX2560X_REG_02, 0x80, 0x80);

	ret = cx2560x_write_reg(cx,0x83,0x01);
	msleep(10);
	ret = cx2560x_write_reg(cx,0x41,0x08);

	cx2560x_write_reg40(cx, 0);

	cx2560x_set_dpdm_hiz(cx);

	dev_info(cx->dev, "cx2560x_enable_otg sucess \n");

    return ret;
/*
	u8 val,temp,data;
	int ret;

	ret = cx2560x_read_reg(cx, CX2560X_REG_02, &data);
	temp = data & REG02_BOOST_LIM_MASK;
	ret = cx2560x_update_bits(cx, CX2560X_REG_02,REG02_BOOST_LIM_MASK, 
			REG02_BOOST_LIM_0P5A << REG02_BOOST_LIM_SHIFT);

	val = REG01_OTG_ENABLE << REG01_OTG_CONFIG_SHIFT;
	pr_info("cx2560x_enable_otg enter\n");
	ret = cx2560x_update_bits(cx, CX2560X_REG_01,REG01_OTG_CONFIG_MASK, val);
	msleep(500);
	ret = cx2560x_update_bits(cx, CX2560X_REG_02,REG02_BOOST_LIM_MASK, temp); */
	return ret;

}

static bool cx2560x_is_otg_enable(struct cx2560x_device *cx)
{
	u8 val = 0;

	cx2560x_read_reg(cx, CX2560X_REG_01, &val);
	val = val & 0x20;
	pr_info("cx2560x_is_otg_enable enter val = %d\n", val);

	if (0 != val) {
		return 1;
	} else {
		return 0;
	}
}

static int cx2560x_disable_otg(struct cx2560x_device *cx)
{
	u8 val = REG01_OTG_DISABLE << REG01_OTG_CONFIG_SHIFT;
	struct cx2560x_state state;

	cx2560x_get_status(cx, &state);

	cx2560x_write_reg40(cx, 1);
	cx2560x_write_reg(cx,0x83,0x00);
	cx2560x_write_reg40(cx, 0);

	pr_info("cx2560x_disable_otg enter\n");
	if(0 == cx2560x_is_otg_enable(cx) && (state.vbus_stat == CX2560X_USB_DCP)) {
		cx2560x_set_hvdcp_off(cx);
		cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,CX2560X_DP_VSEL_MASK,CX2560X_DP_0V);
		cx2560x_dump_register(cx->chg_dev);
	}

	return cx2560x_update_bits(cx, CX2560X_REG_01,
					REG01_OTG_CONFIG_MASK, val);
}

static int cx2560x_set_otg(
	struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct cx2560x_device *cx = charger_get_data(chg_dev);
	if (en) {
		ret = cx2560x_enable_otg(cx);
		if (ret < 0) {
			dev_err(cx->dev, "cx2560x_enable_otg fail");
			return ret;
		}
	} else {
		ret = cx2560x_disable_otg(cx);
		if (ret < 0) {
			dev_err(cx->dev, "cx2560x_disable_otg fail");
			return ret;
		}
	}
	return 0;
}

static int cx2560x_set_boost_current_limit(
	struct charger_device *chg_dev, u32 uA)
{
	return 0;
}

static int cx2560x_do_event(
	struct charger_device *chg_dev, u32 event,
				u32 args)
{
	if (chg_dev == NULL)
		return -EINVAL;

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_EOC);
		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		charger_dev_notify(chg_dev, CHARGER_DEV_NOTIFY_RECHG);
		break;
	default:
		break;
	}

	return 0;
}

/* for i2c auto test */
static ssize_t fts_rw_reg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int32_t ret = 0;
	struct i2c_client * client = (struct i2c_client *)dev->driver_data;
	struct cx2560x_device *cx = i2c_get_clientdata(client);

	if(opr_flag == 0) {
		if (opr_ret < 0) {
			return sprintf(buf, "Write fail reg=%x: write_data=%x\n", reg, write_data);
		}
		else {
			return sprintf(buf, "Write success reg=%x: write_data=%x\n", reg, write_data);
		}
	} else if(opr_flag == 1) {
		ret = cx2560x_read_reg(cx, reg, &read_data);
		if (ret < 0) {
			pr_info("%s: i2c read fail!\n", __func__);
			return sprintf(buf, "Read fail reg=%x, read_data=%x\n", reg, read_data);
		}
		return sprintf(buf, "Read success reg=%x, read_data=%x\n", reg, read_data);
	}
	return sprintf(buf, "fts_rw_reg_show operation error\n");
}

static ssize_t fts_rw_reg_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client * client = (struct i2c_client *)dev->driver_data;
	struct cx2560x_device *cx = i2c_get_clientdata(client);
	uint32_t data = 0;

	pr_info("fts_rw_reg_store\n");
	if(strlen(buf) == 3) {
		sscanf(buf, "%x", &data);
		reg = data & 0xff;
		opr_flag = 1;
		pr_info("fts_rw_reg_store: read reg:%x\n" , reg);
	} else if(strlen(buf) == 5) {
		sscanf(buf, "%x", &data);
		reg = (data >> 8) & 0xff;
		write_data = data & 0xff;
		opr_ret = cx2560x_write_reg(cx, reg, write_data);
		opr_flag = 0;
		pr_info("fts_rw_reg_store: write reg:%x, data:%x\n" , reg, write_data);
	} else {
		opr_flag = -1;
		pr_info("fts_rw_reg_store buf error.\n");
	}
	return count;
}

static DEVICE_ATTR(fts_rw_reg, 0664, fts_rw_reg_show, fts_rw_reg_store);
static struct attribute * chg_i2c_test_attributes[] = {
	&dev_attr_fts_rw_reg.attr,
	NULL,
};

static struct attribute_group chg_i2c_test_attribute_group = {
	.attrs = chg_i2c_test_attributes
};

static const struct attribute_group * chg_i2c_test_groups[] = {
	&chg_i2c_test_attribute_group,
	NULL,
};

static int cx2560x_en_pe_current_partern(
	struct charger_device *chg_dev, bool is_up)
{
	return 0;
}

static struct charger_ops cx2560x_chg_ops = {
	.enable = cx2560x_charging_switch,
	.get_charging_current = NULL,
	.set_charging_current = cx2560x_set_ichrg_curr,
	.get_input_current = cx2560x_get_input_curr_lim,
	.set_input_current = cx2560x_set_input_curr_lim,
	.get_constant_voltage = cx2560x_get_chrg_volt,
	.set_constant_voltage = cx2560x_set_chrg_volt,
	.kick_wdt = cx2560x_reset_watch_dog_timer,
	.set_mivr = cx2560x_set_input_volt_lim,
	.is_charging_done = cx2560x_get_charging_status,
	.enable_safety_timer = cx2560x_enable_safetytimer,
	.is_safety_timer_enabled = cx2560x_get_is_safetytimer_enable,
	.enable_otg = cx2560x_set_otg,
	.set_boost_current_limit = cx2560x_set_boost_current_limit,
	.event = cx2560x_do_event,
	.send_ta_current_pattern = cx2560x_en_pe_current_partern,
	.set_pe20_efficiency_table = NULL,
	.send_ta20_current_pattern = NULL,
	.enable_cable_drop_comp = NULL,
	/* DPDM */
	.set_dp = cx2560x_set_dpvolt,
	.set_dm = cx2560x_set_dmvolt,
	.get_vbus_adc = cx2560x_get_vbus,
};

static const struct charger_properties cx2560x_chg_props = {
	.alias_name = "cx2560x",
};

static int cx2560x_create_sysfs_file(
	struct cx2560x_device *cx)
{
	int ret;

	ret = device_create_file(cx->dev, &dev_attr_reg);
	if (ret) {
		dev_err(cx->dev, "device_create_file reg failed\n");
		goto err_create_reg;
	}

	ret = device_create_file(cx->dev, &dev_attr_dump);
	if (ret) {
		dev_err(cx->dev, "device_create_file dump failed\n");
		goto err_create_dump;
	}

	ret = device_create_file(cx->dev, &dev_attr_charge);
	if (ret) {
		dev_err(cx->dev, "device_create_file hvdcp_enable failed\n");
		goto err_create_charge;
	}

	ret = device_create_file(cx->dev, &dev_attr_dpdm);
	if (ret) {
		dev_err(cx->dev, "device_create_file dpdm failed\n");
		goto err_create_dpdm;
	}

	ret = device_create_file(cx->dev, &dev_attr_qc3v0);
	if (ret) {
		dev_err(cx->dev, "device_create_file dpdm failed\n");
		goto err_create_qc3v0;
	}

	ret = device_create_file(cx->dev, &dev_attr_vtemp);
	if (ret) {
		dev_err(cx->dev, "device_create_file vtemp failed\n");
		goto err_create_vtemp;
	}

	return 0;

err_create_vtemp:
	device_remove_file(cx->dev, &dev_attr_qc3v0);
err_create_qc3v0:
	device_remove_file(cx->dev, &dev_attr_dpdm);
err_create_dpdm:
	device_remove_file(cx->dev, &dev_attr_charge);
err_create_charge:
	device_remove_file(cx->dev, &dev_attr_dump);
err_create_dump:
	device_remove_file(cx->dev, &dev_attr_reg);
err_create_reg:
	return -EPERM;
}
static int cx2560x_set_vindpm(struct cx2560x_device *cx, unsigned int vindpm) {
	u8 reg_val;

	if (vindpm < 3900)
		reg_val = 0x0;
	else if (vindpm > 14200)
		reg_val = 0x67;
	else
		reg_val = (vindpm - 3900) / 100;

	return cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_11,
					CX2560X_VINDPM_V_MASK, reg_val);
}
static int cx2560x_force_dpdm(struct cx2560x_device *cx) {
	int ret;
	if(0 != dpdm_flag)
		return -ENOMEM;
	dpdm_flag = 1;

	dev_err(cx->dev, "cx2560x_force_dpdm\n");

	cx2560x_update_bits(cx, CX2560X_CHRG_CTRL_10,
		CX2560X_DP_VSEL_MASK | CX2560X_DM_VSEL_MASK,
		CX2560X_DP_0V | CX2560X_DM_0V);

	msleep(100);
	cx2560x_set_dpdm_hiz(cx);

	cx2560x_force_dpm_det(cx);
	ret = cx2560x_set_vindpm(cx, 4400);
	return ret;
}
static void cx2560x_force_detection_dwork_handler(struct work_struct *work)
{
	int ret;
	struct cx2560x_device *cx = container_of(work, struct cx2560x_device, force_detect_dwork.work);
	struct cx2560x_state state;


	cx2560x_get_status(cx, &state);
	//Charger_Detect_Init();
	ret = cx2560x_force_dpdm(cx);
	if (ret) {
		dev_err(cx->dev, "%s: force dpdm failed(%d)\n", __func__, ret);
		return;
	}
	dev_err(cx->dev,"%s force dpdm state.vbus_stat = %d,force_detect_count=%d \n", __func__, state.vbus_stat,cx->force_detect_count);
	cx->force_detect_count++;

}

static void cx2560x_charger_auto_set_vindpm_work(struct work_struct *work)
{
	int ret;
	int vbus_volt = 0;
	struct cx2560x_device *cx = container_of(work, struct cx2560x_device, vindpm_work.work);

	get_vbus_voltage_ext(&vbus_volt);

	dev_err(cx->dev, "%s: vbus_volt=%d\n", __func__, vbus_volt);
	if (vbus_volt > 8000) {
		ret = cx2560x_set_vindpm(cx, 8400);
		if (ret) {
			dev_err(cx->dev, "set cx2560x_set_vindpm 8v failed");
		}
	} else {
		ret = cx2560x_set_vindpm(cx, 4400);
		if (ret) {
			dev_err(cx->dev, "set cx2560x_set_vindpm 4.4v failed");
		}
	}
	dev_err(cx->dev, "%s: \n", __func__);
	schedule_delayed_work(&cx->vindpm_work, msecs_to_jiffies(3000));
	return;
}


static int cx2560x_driver_probe(
	struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct cx2560x_device *cx;
	u8 data;
	char *name = NULL;

	dev_err(&client->dev, "cx2560x_driver_probe\n");

	cx = devm_kzalloc(&client->dev, sizeof(*cx), GFP_KERNEL);
	if (!cx) {
		dev_err(&client->dev, "allocate memory for cx2560x failed\n");
		return -ENOMEM;
	}

	cx->chg_en = 1;
	cx->ibat = 3000;
	cx->chg_volt = 5600;
	cx->chg_curr = 3000;
	cx->vtemp = CX2560X_TEMP_INVALID;
	cx->qc_type = QUICK_CHARGE_1P0;
	cx->qc_force = QC_TYPE_FORCE_2P0;
	cx->temp_state = CX2560X_STATE_NORMAL;
	cx->client = client;
	cx->dev = &client->dev;
	cx->chg_desc = &cx2560x_charger_desc;
	i2c_set_clientdata(client, cx);
	INIT_DELAYED_WORK(&cx->irq_work, cx2560x_charge_irq_work);
	INIT_DELAYED_WORK(&cx->psy_work, cx2560x_psy_change_work);
	INIT_DELAYED_WORK(&cx->charge_work, cx2560x_charge_online_work);
	INIT_DELAYED_WORK(&cx->vindpm_work, cx2560x_charger_auto_set_vindpm_work);
	INIT_DELAYED_WORK(&cx->charge_usb_detect_work, charger_usb_detect_work_func);

	ret=cx2560x_write_reg40(cx, 1);
	if(ret==1)
		dev_err(&client->dev, " write reg40 successfully; \n", __func__);

	cx2560x_write_reg(cx, 0x41, 0x28);
	cx2560x_write_reg(cx, 0x44, 0x18);
	cx2560x_read_reg(cx, 0x41, &data);

	dev_err(&client->dev, "%s;enter;0x41=%x;\n", __func__, data);

	ret=cx2560x_write_reg40(cx, false);
	dev_err(&client->dev, " cx2560x init sucess \n", __func__);

	ret = cx2560x_chipid_detect(cx);
	if (ret != CX2560X_PN_ID) {
		dev_err(cx->dev, "charger ic not founded");
		devm_kfree(cx->dev, cx);
		return -ENODEV;;
	}

	ret = cx2560x_gpio_config(cx);
	if (ret < 0) {
		dev_err(cx->dev, "config gpio failed\n");
		goto err_gpio_config;
	}

	ret = cx2560x_hardware_init(cx);
	if (ret) {
		dev_err(cx->dev, "hardware init failed");
		goto err_hardware_init;
	}

	name = devm_kasprintf(cx->dev, GFP_KERNEL, "%s","cx2560x suspend wakelock");
	cx->charger_wakelock =	wakeup_source_register(cx->dev, name);

	/* Register charger device */
	cx->chg_dev = charger_device_register("primary_chg",
						&client->dev, cx,
						&cx2560x_chg_ops,
						&cx2560x_chg_props);
	if (IS_ERR_OR_NULL(cx->chg_dev)) {
		dev_err(cx->dev, "register charger device failed\n");
		ret = PTR_ERR(cx->chg_dev);
		goto err_hardware_init;
	}

	ret = devm_request_threaded_irq(cx->dev, client->irq,
				NULL, cx2560x_irq_handler_thread,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				dev_name(cx->dev), cx);
	if (ret) {
		dev_err(cx->dev, "request interrupt failed\n");
		goto err_request_irq;
	}
	ret = enable_irq_wake(client->irq);
	if (ret) {
		dev_err(cx->dev, "enable irq wake failed\n");
		goto err_irq_wake;
	}

	cx->charger_psy = cx2560x_register_charger_psy(cx);
	if (IS_ERR_OR_NULL(cx->charger_psy)) {
		dev_err(cx->dev, "register charger power supply failed\n");
		goto err_register_charger_psy;
	}

	cx->usb_psy = cx2560x_register_usb_psy(cx);
	if (IS_ERR_OR_NULL(cx->usb_psy)) {
		dev_err(cx->dev, "register usb power supply failed\n");
		goto err_register_usb_psy;
	}

	cx->ac_psy = cx2560x_register_ac_psy(cx);
	if (IS_ERR_OR_NULL(cx->ac_psy)) {
		dev_err(cx->dev, "register ac power supply failed\n");
		goto err_register_ac_psy;
	}

	cx->otg_rdev = cx2560x_register_regulator(cx);
	if (IS_ERR_OR_NULL(cx->otg_rdev)) {
		dev_err(cx->dev, "register regulator failed\n");
		goto err_register_rdev;
	}
	/* for i2c auto test, and current node is fix */
	chg_i2c_test_class = class_create(THIS_MODULE, "mcharger_iic_test");
	if (IS_ERR(chg_i2c_test_class)) {
		ret = PTR_ERR(chg_i2c_test_class);
		pr_err("create chg_i2c_test_class error\n");
	}
	chg_i2c_test_class->dev_groups = chg_i2c_test_groups;
	ret = alloc_chrdev_region(&chg_i2c_test_devt, 0, 1, "chg_i2c_test_devt");
	if(ret < 0) {
		pr_err("alloc_chrdev_region failed!");
	}
	chg_i2c_test_device = device_create(chg_i2c_test_class, NULL, chg_i2c_test_devt, client, "chg_i2c_test_node");
	if (IS_ERR(chg_i2c_test_device)) {
		ret = PTR_ERR(chg_i2c_test_device);
		pr_err("chg_i2c_test_device create error: %d" , ret);
	}
	ret = cx2560x_create_sysfs_file(cx);
	if (ret) {
		dev_err(cx->dev, "create sysfs failed\n");
		goto err_create_sysfs;
	}
	INIT_DELAYED_WORK(&cx->force_detect_dwork, cx2560x_force_detection_dwork_handler);

	//msleep(10);
	//cx2560x_set_hvdcp_off(cx);

	//get usb device
	cx->usb2_phy = devm_usb_get_phy(cx->dev, USB_PHY_TYPE_USB2);

	if (IS_ERR_OR_NULL(cx->usb2_phy)) {
		dev_err(cx->dev, "usb_get_phy failed\n");
		return ret;
	}else{
		dev_err(cx->dev, "usb_get_phy success\n");
	}

	//msleep(500); //add for guanjichongdian
	//cx2560x_force_dpdm(cx);
	//msleep(500); //add for guanjichongdian
	schedule_delayed_work(&cx->force_detect_dwork, 5 * HZ);
	cx2560x_set_hvdcp_en(cx);
	ret = cx2560x_get_initial_state(cx);
	if (ret) {
		dev_err(cx->dev, "get initial state failed\n");
		goto err_irq_wake;
	}
	cx2560x_read_reg(cx, 0x08, &data);
	if( ((data & CX2560X_VBUS_STAT_MASK) >> 5) == CX2560X_USB_SDP)
		schedule_delayed_work(&cx->charge_usb_detect_work, 10 * HZ);

	dev_info(cx->dev, "cx2560x_driver_probe finished");

	return 0;

err_create_sysfs:
	regulator_unregister(cx->otg_rdev);
err_register_rdev:
	power_supply_unregister(cx->usb_psy);
err_register_usb_psy:
	power_supply_unregister(cx->ac_psy);
err_register_ac_psy:
	power_supply_unregister(cx->charger_psy);
err_register_charger_psy:
	disable_irq_wake(client->irq);
err_irq_wake:
	free_irq(client->irq, cx);
err_request_irq:
	charger_device_unregister(cx->chg_dev);
err_hardware_init:
	gpio_free(cx->irq_gpio);
	gpio_free(cx->en_gpio);
err_gpio_config:
	devm_kfree(cx->dev, cx);
	return -EBUSY;
}

static void cx2560x_remove_sysfs_file(
	struct cx2560x_device *cx)
{
	device_remove_file(cx->dev, &dev_attr_vtemp);
	device_remove_file(cx->dev, &dev_attr_qc3v0);
	device_remove_file(cx->dev, &dev_attr_dpdm);
	device_remove_file(cx->dev, &dev_attr_charge);
	device_remove_file(cx->dev, &dev_attr_dump);
	device_remove_file(cx->dev, &dev_attr_reg);
}

static int cx2560x_charger_remove(struct i2c_client *client)
{
	struct cx2560x_device *cx = i2c_get_clientdata(client);

	cx2560x_remove_sysfs_file(cx);
	regulator_unregister(cx->otg_rdev);
	power_supply_unregister(cx->usb_psy);
	power_supply_unregister(cx->ac_psy);
	power_supply_unregister(cx->charger_psy);
	disable_irq_wake(client->irq);
	free_irq(client->irq, cx);
	charger_device_unregister(cx->chg_dev);
	gpio_free(cx->irq_gpio);
	gpio_free(cx->en_gpio);
	devm_kfree(cx->dev, cx);

	return 0;
}

void cx2560x_charger_shutdown(struct i2c_client *client)
{
	struct cx2560x_device *cx = i2c_get_clientdata(client);
	int ret = 0;

	cx2560x_set_hvdcp_off(cx);
	cx2560x_set_dpdm_hiz(cx);
	dev_err(cx->dev, "cx2560x_charger_shutdown ret=%d\n", ret);
	cx2560x_dump_register(cx->chg_dev);

	msleep(50);
}

static int cx2560x_device_suspend(struct device *dev)
{
	struct cx2560x_device *cx = dev_get_drvdata(dev);

	if (cx->state.pg_stat) {
		cancel_delayed_work(&cx->charge_work);
		dev_info(cx->dev, "cancel charge work");
	}

	return 0;
}

static int cx2560x_device_resume(struct device *dev)
{
	struct cx2560x_device *cx = dev_get_drvdata(dev);

	if (cx->state.pg_stat) {
		schedule_delayed_work(&cx->charge_work, msecs_to_jiffies(100));
		dev_info(cx->dev, "resume charge work");
	}

	return 0;
}

static int cx2560x_dump_register(struct charger_device *chg_dev)
{
	unsigned char i = 0;
	unsigned int ret = 0;
	unsigned char cx2560x_reg[CX2560X_REG_NUM + 1] = { 0 };
	struct cx2560x_device *cx = charger_get_data(chg_dev);

	for (i = 0; i < CX2560X_REG_NUM + 1; i++) {
		ret = cx2560x_read_reg(cx, i, &cx2560x_reg[i]);
		if (ret != 0) {
			pr_info("%s, [cx2560x] i2c transfor error\n", __func__);
			return 1;
		}
		pr_info("%s, [0x%x]=0x%x ", __func__, i, cx2560x_reg[i]);
	}
	return 0;
}
static struct dev_pm_ops cx2560x_pm_osp = {
	.suspend = cx2560x_device_suspend,
	.resume = cx2560x_device_resume,
};

static const struct i2c_device_id cx2560x_i2c_ids[] = {
	{ "cx2560x-charger", CX25601 }, { },
};

static const struct of_device_id cx2560x_of_match[] = {
	{ .compatible = "cx,cx2560x_charger", }, { },
};

static struct i2c_driver cx2560x_driver = {
	.driver = {
		.name = "cx2560x-charger",
		.of_match_table = cx2560x_of_match,
		.pm = &cx2560x_pm_osp,
	},
	.probe = cx2560x_driver_probe,
	.remove = cx2560x_charger_remove,
	.shutdown = cx2560x_charger_shutdown,
	.id_table = cx2560x_i2c_ids,
};

module_i2c_driver(cx2560x_driver);
MODULE_DESCRIPTION("SUNCORE CX2560x Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("SUNCORE");
