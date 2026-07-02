/*opyright (C) 2024 Tinno, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "[wt6670f] %s: " fmt, __func__
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/time.h>

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#include <tinno_charger.h>
#endif /* CONFIG_OEM_TINNO_CHARGER */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include "charger_class.h"
#include "mtk_charger.h"
#include "mtk_musb.h"
#else
#include <mt-plat/upmu_common.h>
#include <mt-plat/v1/charger_class.h>
#include <mt-plat/v1/mtk_charger.h>
#include <mt-plat/v1/charger_type.h>
#endif

#include <linux/stat.h>
#include <linux/ctype.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/vmalloc.h>
#include <linux/preempt.h>
#include "wt6670f_firmware.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
#include <linux/phy/phy.h>
#define PHY_MODE_BC11_SET 1
#define PHY_MODE_BC11_CLR 2
#endif /* LINUX_VERSION_CODE */

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include <dev_info.h>
#endif /* CONFIG_OEM_DEVINFO */

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
extern bool qc_logic_probe_done;
extern int qc3p_charger_ready;
#endif

#define FIRMWARE_FILE_LENGTH            15000
#define UPDATE_SUCCESS                  0
#define ERROR_REQUESET_FIRMWARE         -1
#define ERROR_CHECK_FIREWARE_FORMAT     -2
#define ERROR_GET_CHIPID_FAILED         -3
#define ERROR_ERASE_FAILED              -4
#define ERROR_FINISH_CMD_FAILED         -5
#define ERROR_FILE_CRC_FAILED           -6
#define ERROR_HIGHADD_CMD_FAILED        -7
#define ERROR_PROGRAM_CMD_FAILED        -8
#define ERROR_CALLBACK_FAILED           -9

/*Z350*/
#define Z350_REG_VERSION                0x14
#define Z350_REG_QC30_PM                0x73
#define Z350_REG_QC35_PM                0x83

#define Z350_REG_SOFT_RESET             0X04
#define Z350_REG_QC_MODE                0x02
#define Z350_REG_HVDCP                  0x05
#define Z350_REG_BC1P2                  0x06
#define Z350_REG_CHG_TYPE               0x11
#define Z350_REG_VIN                    0x12
#define Z350_REG_VID                    0x13


/*WT6670F*/
#define WT6670F_REG_RUN_APSD            0xB0
#define WT6670F_REG_QC_MODE             0xB1
#define WT6670F_REG_SOFT_RESET          0xB3
#define WT6670F_REG_BC12_START          0xB6
#define WT6670F_REG_QC30_PM             0xBA
#define WT6670F_REG_QC35_PM             0xBB
#define WT6670F_REG_FIRM_VER            0xBF
#define WT6670F_REG_CHG_TYPE            0XBD

#define QC3P_WT6670F                    0x01 /*100021221*/
#define QC3P_Z350                       0x00
#define QC35_SDP                        0x04

#define QC35_UNKNOW                     0x02
#define POWER_SUPPLY_PROP_PD_VERIFY_IN_PROCESS 0x03
#define WT6670F_FIRMWARE_VERSION        0x0300
#define QC3_DP_DM_PR_FCC                2000000

#define ENABLE                          0x01
#define DISABLE                         0x00

#define QC35_DPDM                       0x01
#define QC30_DPDM                       0x00

#define WT6670F_ADDR                    0x34
#define SOFT_RESET_VAL                  0xff

#define QC3_VOLT_STEP                    200 /* mV */
#define QC3_BASE_VOLT                   5000 /* mV */
#define QC3_TARGE_VOLT                  6600 /* mV */

static int m_chg_type = 0;

enum usbsw_state {
	USBSW_CHG = 0,
	USBSW_USB,
};

struct wt6670f_desc {
	bool en_bc12;
	bool en_hvdcp;
	bool en_intb;
	bool en_sleep;
	const char *chg_dev_name;
	const char *alias_name;
};

static struct wt6670f_desc wt6670f_default_desc = {
	.en_bc12 = true,
	.en_hvdcp = true,
	.en_intb = true,
	.en_sleep = true,
	.chg_dev_name = "primary_qc_phy",
	.alias_name = "wt6670f",
};

struct wt6670f_charger {
	struct i2c_client *client;
	struct device *dev;

	struct wt6670f_desc *desc;
	struct charger_device *chg_dev_p;
	struct charger_properties chg_props;

	bool tcpc_attach;

	int rst_gpio;
	int int_gpio;
	int reset_pin;
	bool otg_enable;
	struct mutex chgdet_lock;
	struct mutex qc3p_lock;
	bool hvdcp_dpdm_status;
	int connector_temp;
	struct mutex irq_complete;

	int qc35_chg_type;
	int qc35_err_sta;
	int pulse_cnt;
	bool vbus_disable;
	int entry_time;
	int count;
	int rerun_apsd_count;

	bool chg_ready;
	bool bc12_unsupported;
	bool hvdcp_unsupported;
	bool intb_unsupported;
	bool sleep_unsupported;
	struct delayed_work conn_therm_work;
	struct delayed_work charger_type_det_work;
	struct delayed_work chip_reset_wt6670f;
	struct delayed_work chip_update_work;
	struct delayed_work hvdcp_det_retry_work;
	struct delayed_work get_charger_type_work;

	/* psy */
	struct power_supply *charger_psy;
	struct power_supply *qc_phy_psy;
	struct power_supply *charger_identify_psy;
	struct power_supply_desc charger_identify_psy_d;

	bool irq_waiting;
	bool resume_completed;
	int dpdm_mode;
	bool hvdcp_en;
	bool irq_data_process_enable;
	struct pinctrl *wt6670f_pinctrl;
	struct pinctrl_state *pinctrl_state_normal;
	struct pinctrl_state *pinctrl_state_isp;
	struct pinctrl_state *pinctrl_scl_normal;
	struct pinctrl_state *pinctrl_scl_isp;
	struct pinctrl_state *pinctrl_sda_normal;
	struct pinctrl_state *pinctrl_sda_isp;
	int wt6670f_sda_gpio;
	int wt6670f_scl_gpio;
	struct mutex isp_sequence_lock;

	int hvdcp_timer;
	int hvdcp_retry_timer;
	int ocp_timer;
	int charger_type;
	int usb_type;
	int qc3p_type;
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	bool first_detect_dcp;
	struct notifier_block nb;
	struct charger_device *charger_dev;
#endif
};

static struct wt6670f_charger *_chip = NULL;

static int __wt6670f_write_byte_only(struct wt6670f_charger *chip, u8 reg)
{
	s32 ret;

	ret = i2c_smbus_write_byte(chip->client, reg);
	if (ret < 0) {
		pr_err("reg[0x%02X] write failed.\n", reg);
		return ret;
	}
	return 0;
}

static int __wt6670f_write_byte(struct wt6670f_charger *chip, u8 reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		pr_err("reg[0x%02X] write failed.\n", reg);
		return ret;
	}
	return 0;
}

static int __wt6670f_read_word(struct wt6670f_charger *chip, u8 reg, u16 *data)
{
	s32 ret;

	ret = i2c_smbus_read_word_data(chip->client, reg);
	if (ret < 0) {
		pr_err("reg[0x%02X] read failed.\n", reg);
		return ret;
	}

	*data = (u16) ret;

	return 0;
}

static int __wt6670f_write_word(struct wt6670f_charger *chip, u8 reg, u16 val)
{
	s32 ret;

	ret = i2c_smbus_write_word_data(chip->client, reg, val);
	if (ret < 0) {
		pr_err("reg[0x%02X] write failed.\n", reg);
		return ret;
	}
	return 0;
}

static int __wt6670f_read_block(struct wt6670f_charger *chip, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_i2c_block_data(chip->client, reg, 4, data);
	if (ret < 0) {
		pr_err("reg[0x%02X] read failed.\n", reg);
		return ret;
	}

	return 0;
}

static int wt6670f_write_byte_only(struct wt6670f_charger *chip, u8 reg)
{
	return __wt6670f_write_byte_only(chip, reg);
}

static int wt6670f_write_byte(struct wt6670f_charger *chip, u8 reg, u8 data)
{
	return __wt6670f_write_byte(chip, reg, data);
}

static int wt6670f_read_word(struct wt6670f_charger *chip, u8 reg, u16 *data)
{
	return __wt6670f_read_word(chip, reg, data);
}

static int wt6670f_write_word(struct wt6670f_charger *chip, u8 reg, u16 data)
{
	return __wt6670f_write_word(chip, reg, data);
}

__maybe_unused static int wt6670f_read_block(struct wt6670f_charger *chip, u8 reg, u8 *data)
{
	return __wt6670f_read_block(chip, reg, data);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
__maybe_unused static int charger_detect_init(struct wt6670f_charger *chip)
{
	struct phy *phy;
	int ret;

	pr_info("enter\n");

	phy = phy_get(chip->dev, "usb2-phy");
	if (IS_ERR_OR_NULL(phy)) {
		pr_err("failed to get usb2-phy\n");
		return -ENODEV;
	}

	ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, PHY_MODE_BC11_SET);
	if (ret)
		pr_err("failed to set phy ext mode\n");

	phy_put(chip->dev, phy);

	return ret;
}

__maybe_unused static int charger_detect_release(struct wt6670f_charger *chip)
{
	struct phy *phy;
	int ret;

	pr_info("enter\n");

	phy = phy_get(chip->dev, "usb2-phy");
	if (IS_ERR_OR_NULL(phy)) {
		pr_err("failed to get usb2-phy\n");
		return -ENODEV;
	}

	ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, PHY_MODE_BC11_CLR);
	if (ret)
		pr_err("failed to set phy ext mode\n");

	phy_put(chip->dev, phy);

	return ret;
}
#endif

static int wt6670f_set_usbsw_state(struct wt6670f_charger *chip, int state)
{
	pr_info("state = %s\n", state ? "usb" : "chg");

	if (state == USBSW_CHG) {
		Charger_Detect_Init();
	} else {
		Charger_Detect_Release();
	}

	return 0;
}

static u16 wt6670f_get_firmware_version(struct wt6670f_charger *chip, u16 *val)
{
	int ret = 0;
	u16 tmp = 0;

	ret = wt6670f_read_word(chip, WT6670F_REG_FIRM_VER, &tmp);
	if (ret < 0) {
		pr_err("get version failed!\n");
	} else {
		*val = (u16)(((tmp & 0xFF) << 8) | ((tmp >> 8) & 0xFF));
		pr_info("get version success, 0x%04x!\n", *val);
	}

	return ret;
}

static void wt6670f_do_reset(struct wt6670f_charger *chip)
{
	if (IS_ERR_OR_NULL(chip)) {
		pr_err("chip is NULL\n");
		return;
	}

	pr_info("enter\n");

	gpio_set_value(chip->rst_gpio, 1);
	mdelay(15);
	gpio_set_value(chip->rst_gpio, 0);
	mdelay(40);
}

void wt6670f_charger_reset_reg(void)
{
	if (IS_ERR_OR_NULL(_chip)) {
		pr_err("_chip is NULL\n");
		return;
	}

	struct wt6670f_charger *chip =  _chip;
	pr_info("enter\n");

	gpio_set_value(chip->rst_gpio, 1);
}

void wt6670f_reset_chg_type(void)
{
	if (IS_ERR_OR_NULL(_chip)) {
		pr_err("%s _chip is NULL\n", __func__);
		return;
	}

	struct wt6670f_charger *chip =  _chip;
	chip->chg_ready = false;
}
EXPORT_SYMBOL_GPL(wt6670f_reset_chg_type);

int wt6670f_reset_charger_type(void)
{
	if (IS_ERR_OR_NULL(_chip)) {
		pr_err("invalid _chip, ignore reset charger type\n");
		return -ENODEV;
	} else {
		pr_info("clear total_count\n");
		_chip->qc3p_type = QC3P_POWER_NONE;
		_chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		_chip->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		_chip->first_detect_dcp = true;
		_chip->count = 0;
		gpio_direction_output(_chip->rst_gpio, 1);
		return 0;
	}
}
EXPORT_SYMBOL_GPL(wt6670f_reset_charger_type);

void wt6670f_do_init(struct wt6670f_charger *chip)
{
	u16 firmware_version = 0;
	u16 val = 0;

	wt6670f_do_reset(chip);

	gpio_direction_output(chip->rst_gpio, 1);

	firmware_version = wt6670f_get_firmware_version(chip, &val);
	wt6670f_reset_charger_type();

	pr_info("firmware version = 0x%2x\n", val);
}

__maybe_unused static u16 wt6670f_get_id(struct wt6670f_charger *chip, u8 reg)
{
	int ret = 0;
	u16 id;

	ret = wt6670f_read_word(chip, reg, &id);
	if (ret){
		pr_err("get vendor id failed!\n");
		return 0;
	} else {
		pr_info("get vendor id : %d\n", id);
		return id;
	}
}

void wt6670f_do_reinit(struct wt6670f_charger *chip)
{
	wt6670f_do_reset(chip);
}

__maybe_unused static void wt6670f_en_hvdcp(struct wt6670f_charger *chip, bool enable)
{
	if (enable) {
		wt6670f_write_byte(chip, Z350_REG_HVDCP, 0x01);
	} else {
		wt6670f_write_byte(chip, Z350_REG_HVDCP, 0x00);
	}

	pr_info("%d\n", enable);
}

__maybe_unused static u32 wt6670f_start_detection(struct wt6670f_charger *chip)
{
	int ret = 0;

	pr_info("wt6670f run apsd\n");

	ret = wt6670f_write_byte_only(chip, WT6670F_REG_RUN_APSD);

	return ret;
}

__maybe_unused static u32 wt6670f_start_bc12_detection(struct wt6670f_charger *chip)
{
	int ret = 0;

	pr_info("wt6670f run bc12\n");

	ret = wt6670f_write_byte(chip, WT6670F_REG_BC12_START, 0x01);

	return ret;
}

__maybe_unused static void wt6670f_force_qc2_5V(struct wt6670f_charger *chip)
{
	pr_info("QC2 mode 5V\n");
	wt6670f_write_byte(chip, WT6670F_REG_QC_MODE, 0x01);
}

__maybe_unused static void wt6670f_force_qc3_5V(struct wt6670f_charger *chip)
{
	pr_info("QC3 mode 5V\n");
	wt6670f_write_byte(chip, WT6670F_REG_QC_MODE, 0x04);
}

static int wt6670f_charger_type(struct wt6670f_charger *chip)
{
	int ret;
	u16 data;
	u8 data1, data2;

	ret = wt6670f_read_word(chip, WT6670F_REG_CHG_TYPE, &data);
	pr_info("wt6670f get protocol 0x%x\n", data);


	if (ret < 0) {
		pr_err("wt6670f get protocol fail\n");
		return ret;
	}

	// Get data2 part
	data1 = data & 0xFF;
	data2 = data >> 8;

	pr_info("get charger type, rowdata = 0x%04x, data1 = 0x%02x, data2 = 0x%02x \n", data, data1, data2);

	if ((data2 == 0x03) && ((data1 > 0x9) || (data1 == 0x7))) {
		pr_info("fail to get charger type, error happens!\n");
		return -EINVAL;
	}

	if (data2 == 0x04) {
		pr_info("detected QC3+ charger:0x%02x!\n", data1);
	}

	if ((data1 > 0x00 && data1 < 0x07) || (data1 > 0x07 && data1 < 0x0a)) {
		ret = data1;
	} else {
		ret = 0x00;
	}

	return ret;
}

int wt6670f_set_qc3_volt_count(int count);
static void wt6670f_get_charger_type_func_work(struct work_struct *work)
{
	struct wt6670f_charger *chip = container_of(work, struct wt6670f_charger,
										get_charger_type_work.work);

	if (IS_ERR_OR_NULL(chip)) {
		pr_err("invalid chip\n");
		return;
	}

	wt6670f_do_reset(chip);

	/*Set cur is 500ma before do BC1.2 & QC*/
	charger_dev_set_charging_current(chip->charger_dev, 500000);
	charger_dev_set_input_current(chip->charger_dev, 500000);

	charger_dev_enable_dpdm_hz(chip->charger_dev);
	Charger_Detect_Init();

	m_chg_type = 0;
	wt6670f_start_bc12_detection(chip);
	mdelay(3100);
	m_chg_type = wt6670f_charger_type(chip);

	switch (m_chg_type) {
	case 0x1: // Floating
	chip->charger_type = POWER_SUPPLY_TYPE_USB_FLOAT;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	chip->usb_type = POWER_SUPPLY_TYPE_USB_FLOAT;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->qc3p_type = QC3P_POWER_NONE;
#endif
	break;

	case 0x2: // SDP
	chip->charger_type = POWER_SUPPLY_TYPE_USB;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	chip->usb_type = POWER_SUPPLY_USB_TYPE_SDP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->qc3p_type = QC3P_POWER_NONE;
#endif
	break;

	case 0x3: // CDP
	chip->charger_type = POWER_SUPPLY_TYPE_USB_CDP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	chip->usb_type = POWER_SUPPLY_USB_TYPE_CDP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->qc3p_type = QC3P_POWER_NONE;
#endif
	break;

	case 0x4: // DCP
	chip->charger_type = POWER_SUPPLY_TYPE_USB_DCP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->qc3p_type = QC3P_POWER_NONE;
#endif
	break;

	case 0x5: // QC2.0
	chip->charger_type = POWER_SUPPLY_TYPE_USB_QC2;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->qc3p_type = QC3P_POWER_NONE;
#endif
	break;

	case 0x6: // QC3.0
	chip->charger_type = POWER_SUPPLY_TYPE_USB_QC3;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->qc3p_type = QC3P_POWER_15W;
#endif
	break;

	case 0x8: // QC3+ 18W
	chip->charger_type = POWER_SUPPLY_TYPE_USB_QC3P;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->qc3p_type = QC3P_POWER_18W;
#endif
	break;

	case 0x9: // QC3+ 27W
	chip->charger_type = POWER_SUPPLY_TYPE_USB_QC3P;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->qc3p_type = QC3P_POWER_27W;
#endif
	break;
	}

	Charger_Detect_Release();

	power_supply_changed(chip->qc_phy_psy);
}

__maybe_unused static irqreturn_t wt6670f_interrupt_handler(int irq, void *data)
{
	struct wt6670f_charger *chip = (struct wt6670f_charger *)data;
	int type;

	pr_info("INT OCCURED\n");

	type = wt6670f_charger_type(chip);

	return IRQ_HANDLED;
}

static int wt6670f_i2c_read_cmd(const struct i2c_client *client, char read_addr, const char *buf)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg[2];
	char send_buf[2] = {0x61, 0x00};

	send_buf[1] = read_addr;

	msg[0].addr = WT6670F_ADDR;
	msg[0].len = 2;
	msg[0].buf = send_buf;

	msg[1].addr = WT6670F_ADDR;
	msg[1].flags = I2C_M_STOP & I2C_M_NOSTART;
	msg[1].flags |= I2C_M_RD;
	msg[1].len = 64;
	msg[1].buf = (char *)buf;

	ret = i2c_transfer(adap, &msg[0], 2);

	return ret;
}

static int wt6670f_i2c_get_chip_id(const struct i2c_client *client, const char *buf)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg[2];
	char send_buf[2] = {0x80, 0x00};

	msg[0].addr = WT6670F_ADDR;
	msg[0].len = 2;
	msg[0].buf = send_buf;

	msg[1].addr = WT6670F_ADDR;
	msg[1].flags = I2C_M_STOP & I2C_M_NOSTART;
	msg[1].flags |= I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = (char *)buf;

	ret = i2c_transfer(adap, &msg[0], 2);

	return ret;
}

static int wt6670f_i2c_master_send(const struct i2c_client *client, const char *buf, int count)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;

	msg.addr = WT6670F_ADDR;
	msg.flags = 0x0000;
	msg.flags = msg.flags | I2C_M_STOP;
	msg.len = count;
	msg.buf = (char *)buf;

	ret = i2c_transfer(adap, &msg, 1);

	return (ret == 1) ? count : ret;
}

__maybe_unused static int wt6670f_i2c_sequence_send(const struct i2c_client *client, const char *buf, int count)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg[2];

	msg[0].addr = 0x2B;	//WT6670F_ADDR;
	msg[0].flags = 0x0000;
	msg[0].flags = msg[0].flags | I2C_M_NO_RD_ACK | I2C_M_IGNORE_NAK | I2C_M_NOSTART;
	msg[0].len = 1;
	msg[0].buf = (char *)buf;

	msg[1].addr = 0x48;	//WT6670F_ADDR;
	msg[1].flags = 0x0000;
	msg[1].flags = msg[1].flags | I2C_M_NO_RD_ACK | I2C_M_IGNORE_NAK | I2C_M_NOSTART;
	msg[1].len = 1;
	msg[1].buf = (char *)buf;

	ret = i2c_transfer(adap, msg, count);

	return (ret == 2) ? count : ret;
}

#define FIRWARE_SIZE                0x01
#define ENABLE_ISP_CMD_LEN          7
#define I2C_MASTER_SEND_LEN         3
#define ISP_CHIP_ID                 0x70

static void wt6670f_isp_flow(struct wt6670f_charger *chip)
{
	char enable_isp_cmd[7] = {0x57, 0x54, 0x36, 0x36, 0x37, 0x30, 0x46};
	char set_addr_high_byte_cmd[3] = {0x10, 0x01, 0x00};
	char enable_isp_flash_mode_cmd[3] = {0x10, 0x02, 0x08};
	char chip_erase_cmd[3] = {0x20, 0x00, 0x00};
	char finish_cmd[3] = {0x00, 0x00, 0x00};
	char mem_data[64] = {0x00};
	u8 program_cmd[66] = {0x00};
	unsigned int pos = 0;
	char chip_id = 0x00;
	char high_addr = 0;
	char low_addr = 0;
	char length = 0;
	int i, j, ret, len = 0;
	u8 *code = NULL;
	char flash_addr = 0;
	unsigned int size = sizeof(wt6670f_fw_bin);

	code = kzalloc(FIRWARE_SIZE, GFP_KERNEL);
	if (IS_ERR_OR_NULL(code)) {
		pr_err("invalid code\n");
		return;
	}

	ret = wt6670f_i2c_master_send(chip->client, enable_isp_cmd, ENABLE_ISP_CMD_LEN);
	if (ret != ENABLE_ISP_CMD_LEN)
		pr_err("enable_isp_cmd is failed, ret(%d)\n", ret);
	else
		pr_info("enable_isp_cmd successfully\n");

	ret = wt6670f_i2c_get_chip_id(chip->client, &chip_id);
	pr_info("chip_id: 0x%02x, ret(%d)\n", chip_id, ret);

	if (chip_id == ISP_CHIP_ID) {
		ret = wt6670f_i2c_master_send(chip->client, enable_isp_flash_mode_cmd, I2C_MASTER_SEND_LEN);
		pr_info("enable_isp_flash_mode_cmd, ret(%d)\n", ret);

		ret = wt6670f_i2c_master_send(chip->client, chip_erase_cmd, I2C_MASTER_SEND_LEN);
		if (ret != I2C_MASTER_SEND_LEN) {
			pr_err("chip_erase_cmd is failed, ret(%d)\n", ret);
			ret = ERROR_ERASE_FAILED;
			goto update_failed;
		}

		mdelay(20);

		ret = wt6670f_i2c_master_send(chip->client, finish_cmd, I2C_MASTER_SEND_LEN);
		if (ret != I2C_MASTER_SEND_LEN) {
			pr_err("finish_cmd is failed, ret(%d)\n", ret);
			ret = ERROR_FINISH_CMD_FAILED;
			goto update_failed;
		}
		/* flash program 64 byte 256 count */
		while (pos < size) {
			high_addr = (pos >> 8) & 0x0f ;
			low_addr = pos % 256;
			length = FIRWARE_SIZE;
			pr_debug("high_addr = %02x, low_addr = %02x, length = %d\n", high_addr, low_addr, length);

			memset(code, 0, FIRWARE_SIZE);
			set_addr_high_byte_cmd[2] = high_addr;
			ret = wt6670f_i2c_master_send(chip->client, set_addr_high_byte_cmd, 3);
			if (ret != 3) {
				pr_err("set_addr_high_byte_cmd is failed, ret(%d)\n", ret);
				ret = ERROR_HIGHADD_CMD_FAILED;
				goto update_failed;
			}

			if ((pos + FIRWARE_SIZE) > size) {
				len = size % FIRWARE_SIZE;
			} else {
				len = FIRWARE_SIZE;
			}

			for (i = 0; i < len; i++) {
				code[i] = wt6670f_fw_bin[pos + i];
			}

			if (len != FIRWARE_SIZE) {
				for (i = len; i < FIRWARE_SIZE; i++) {
					code[i] = 0xff;
				}
			}
			/* high addr is 0x41 */
			program_cmd[0] = 0x41;
			program_cmd[1] = low_addr;
			memcpy(program_cmd + 2, code, FIRWARE_SIZE);
			ret = wt6670f_i2c_master_send(chip->client, program_cmd, FIRWARE_SIZE + 2);
			if (ret != FIRWARE_SIZE + 2) {
				pr_err("program_cmd is failed, ret(%d)\n", ret);
				ret = ERROR_PROGRAM_CMD_FAILED;
				goto update_failed;
			}

			ret = wt6670f_i2c_master_send(chip->client, finish_cmd, 3);
			if (ret != 3) {
				pr_err("finish_cmd is failed, ret(%d)\n", ret);
				ret = ERROR_FINISH_CMD_FAILED;
				goto update_failed;
			}
			pos = pos + length;
			pr_debug("pos = %d\n", pos);
		}

		for (i = 0; i < 16; i++) {
			set_addr_high_byte_cmd[2] = i;
			ret = wt6670f_i2c_master_send(chip->client, set_addr_high_byte_cmd, 3);
			if (ret != 3) {
				pr_err("set_addr_high_byte_cmd is failed, ret(%d)\n", ret);
				ret = ERROR_HIGHADD_CMD_FAILED;
				goto update_failed;
			}
			/* flash addr is 0x00, 0x40, 0x80, 0xc0 */
			flash_addr = 0x00;
			wt6670f_i2c_read_cmd(chip->client, flash_addr, mem_data);
			for (j = 0; j < 64; j++) {
				pr_debug("mem_data[%d]=%02x\n", j + flash_addr + i * 256, mem_data[j]);
				pr_debug("wt6670f_fw_bin[%d]=%02x\n", j + flash_addr + i * 256, wt6670f_fw_bin[j + flash_addr + i * 256]);
				if (wt6670f_fw_bin[j + flash_addr + i * 256] != mem_data[j]) {
					pr_err("flash data is wrong [%d]\n", __LINE__);
					ret = ERROR_CALLBACK_FAILED;
					goto update_failed;
				}
			}

			flash_addr = 0x40;
			wt6670f_i2c_read_cmd(chip->client, flash_addr, mem_data);
			for (j = 0; j < 64; j++) {
				pr_debug("mem_data[%d] = %02x\n", j + flash_addr + i * 256, mem_data[j]);
				pr_debug("wt6670f_fw_bin[%d] = %02x\n", j + flash_addr + i * 256, wt6670f_fw_bin[j + flash_addr + i * 256]);
				if (wt6670f_fw_bin[j + flash_addr + i * 256] != mem_data[j]) {
					pr_err("flash data is wrong [%d]\n", __LINE__);
					ret = ERROR_CALLBACK_FAILED;
					goto update_failed;
				}
			}

			flash_addr = 0x80;
			wt6670f_i2c_read_cmd(chip->client, flash_addr, mem_data);
			for (j = 0; j < 64; j++) {
				pr_debug("mem_data[%d] = %02x\n", j + flash_addr + i * 256, mem_data[j]);
				pr_debug("wt6670f_fw_bin[%d] = %02x\n", j + flash_addr + i * 256, wt6670f_fw_bin[j + flash_addr + i * 256]);
				if (wt6670f_fw_bin[j + flash_addr + i * 256] != mem_data[j]) {
					pr_err("flash data is wrong [%d]\n", __LINE__);
					ret = ERROR_CALLBACK_FAILED;
					goto update_failed;
				}
			}

			flash_addr = 0xC0;
			wt6670f_i2c_read_cmd(chip->client, flash_addr, mem_data);
			for (j = 0; j < 64; j++) {
				pr_debug("mem_data[%d] = %02x\n", j + flash_addr + i * 256, mem_data[j]);
				pr_debug("wt6670f_fw_bin[%d] = %02x\n", j + flash_addr + i * 256, wt6670f_fw_bin[j + flash_addr + i * 256]);
				if (wt6670f_fw_bin[j + flash_addr + i * 256] != mem_data[j]) {
					pr_err("flash data is wrong [%d]\n", __LINE__);
					ret = ERROR_CALLBACK_FAILED;
					goto update_failed;
				}
			}
		}

		ret = UPDATE_SUCCESS;
	} else {
		pr_err("chip id is not right, and end update.\n");
		ret = ERROR_GET_CHIPID_FAILED;
		goto update_failed;
	}

update_failed:
	pr_info("chip id is %d, end update. 0--success\n", ret);
	wt6670f_do_reset(chip);
	kfree(code);

	return ;
}

static int wt6670f_parse_dt(struct wt6670f_charger *chip)
{
	struct wt6670f_desc *desc = NULL;
	struct device_node *np = chip->dev->of_node;

	if (IS_ERR_OR_NULL(np)) {
		pr_err("device tree info missing.\n");
		return -EINVAL;
	}

	chip->rst_gpio = of_get_named_gpio(np, "wt,wt6670f_rst_gpio", 0);
	if (!gpio_is_valid(chip->rst_gpio)) {
		pr_err("get wt6670f_rst_gpio failed\n");
		return -EINVAL;
	} else
		pr_info("rst_gpio: %d\n", chip->rst_gpio);

	chip->int_gpio = of_get_named_gpio(np, "wt,wt6670f_int_gpio", 0);
	if (!gpio_is_valid(chip->int_gpio)) {
		pr_err("get wt6670f_int_gpio failed\n");
		return -EINVAL;
	} else
		pr_info("int_gpio: %d\n", chip->int_gpio);

	chip->wt6670f_sda_gpio = of_get_named_gpio(np, "wt,wt6670f_sda_gpio", 0);
	if (!gpio_is_valid(chip->wt6670f_sda_gpio)) {
		pr_err("get wt6670f_sda_gpio failed\n");
		return -EINVAL;
	} else
		pr_info("wt6670f_sda_gpio: %d\n", chip->wt6670f_sda_gpio);

	chip->wt6670f_scl_gpio = of_get_named_gpio(np, "wt,wt6670f_scl_gpio", 0);
	if (!gpio_is_valid(chip->wt6670f_scl_gpio)) {
		pr_err("get wt6670f_scl_gpio failed\n");
		return -EINVAL;
	} else
		pr_info("wt6670f_scl_gpio: %d\n", chip->wt6670f_scl_gpio);

	chip->bc12_unsupported = of_property_read_bool(np, "wt,bc12_unsupported");
	chip->hvdcp_unsupported = of_property_read_bool(np, "wt,hvdcp_unsupported");
	chip->intb_unsupported = of_property_read_bool(np, "wt,intb_unsupported");
	chip->sleep_unsupported = of_property_read_bool(np, "wt,sleep_unsupported");

	chip->desc = &wt6670f_default_desc;

	desc = devm_kzalloc(chip->dev, sizeof(struct wt6670f_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	memcpy(desc, &wt6670f_default_desc, sizeof(struct wt6670f_desc));

	if (of_property_read_string(np, "charger_name", &desc->chg_dev_name) < 0)
		pr_err("dts no charger name\n");

	if (of_property_read_string(np, "alias_name", &desc->alias_name) < 0)
		pr_err("no alias name\n");

	desc->en_bc12 = of_property_read_bool(np, "en_bc12");
	desc->en_hvdcp = of_property_read_bool(np, "en_hvdcp");
	desc->en_intb = of_property_read_bool(np, "en_intb");
	desc->en_sleep = of_property_read_bool(np, "en_sleep");

	chip->desc = desc;
	chip->chg_props.alias_name = chip->desc->alias_name;

	pr_info("chg_name:%s alias:%s\n", chip->desc->chg_dev_name, chip->chg_props.alias_name);

	return 0;
}

static int isp_pinctrl_enable(struct wt6670f_charger *chip)
{
	char chip_id = 0x00;
	int ret = 0;
	u16 version = 0;
	char i2c_buf_enable_cmd[7] = {0x57, 0x54, 0x36, 0x36, 0x37, 0x30, 0x46};

	ret = wt6670f_get_firmware_version(chip, &version);
	if ((ret) || (version != WT6670F_FIRMWARE_VERSION)) {
		pr_err("get version failed and will update firmware, ret(%d)\n", ret);

		pr_info("step 1.1: select pinstate\n");

		ret = pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_scl_isp);
		ret = pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_sda_isp);
		if (ret < 0) {
			pr_info("Failed to select isp pinstate %d\n", ret);
			goto error;
		}

		pr_info("step 1.2: request 2 gpio\n");

		ret = devm_gpio_request(chip->dev, chip->wt6670f_sda_gpio, "wt6670f_sda_gpio");
		if (ret) {
			pr_err("request wt6670f_sda_gpio failed, ret(%d)\n", ret);
			goto error;
		}

		ret = devm_gpio_request(chip->dev, chip->wt6670f_scl_gpio, "wt6670f_scl_gpio");
		if (ret) {
			pr_err("request wt6670f_scl_gpio failed, rc = %d\n", ret);
			goto error;
		}

		mutex_lock(&chip->isp_sequence_lock);
		pr_info("step 2\n");

		gpio_set_value(chip->rst_gpio, 1);
		mdelay(11);
		gpio_set_value(chip->rst_gpio, 0);
		mdelay(3);

		gpio_direction_output(chip->wt6670f_sda_gpio, 0);
		gpio_direction_output(chip->wt6670f_scl_gpio, 0);

		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 0);// sda:0
		udelay(5);

		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		gpio_set_value(chip->wt6670f_sda_gpio, 0);// sda:[0]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 1);// sda:[1]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 0);// sda:[0]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 1);// sda:[1]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 0);// sda:[0]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 1);// sda:[1]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 1);// sda:[1]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 0);// sda:[0]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 1);// sda:[1]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 0);// sda:[0]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 1);// sda:[1]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 0);// sda:[0]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 0);// sda:[0]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 1);// sda:[1]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		gpio_set_value(chip->wt6670f_sda_gpio, 0);// sda:[0]
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 1);// scl:1
		udelay(5);
		gpio_set_value(chip->wt6670f_scl_gpio, 0);// scl:0

		mdelay(10);

		gpio_free(chip->wt6670f_sda_gpio);
		gpio_free(chip->wt6670f_scl_gpio);

		pr_info("step 2.1: select pinstate normal to i2c\n");
		ret = pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_scl_normal);
		ret = pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_sda_normal);
		if (ret < 0) {
			pr_err("Failed to select normal pinstate, ret(%d)\n", ret);
			goto error;
		}

		mutex_unlock(&chip->isp_sequence_lock);
		pr_info("step 3\n");

		ret = wt6670f_i2c_master_send(chip->client, i2c_buf_enable_cmd, 7);
		pr_info("step3 chip_id = 0x%02x, ret(%d)\n", ret, chip_id);

		pr_info("step 4\n");

		ret = wt6670f_i2c_get_chip_id(chip->client, &chip_id);
		pr_info("step4 chip_id = 0x%02x, ret(%d)\n", chip_id, ret);
	}
	return 0;

error:
	pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_sda_normal);
	pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_scl_normal);

	gpio_free(chip->wt6670f_sda_gpio);
	gpio_free(chip->wt6670f_scl_gpio);

	return ret;
}

static int wt6670f_pinctrl_init(struct wt6670f_charger *chip)
{
	int ret;

	chip->wt6670f_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->wt6670f_pinctrl)) {
		ret = PTR_ERR(chip->wt6670f_pinctrl);
		pr_err("Target does not use pinctrl, ret(%d)\n", ret);
		goto err_pinctrl_get;
	}

	chip->pinctrl_state_normal = pinctrl_lookup_state(chip->wt6670f_pinctrl, "rst_normal");
	if (IS_ERR_OR_NULL(chip->pinctrl_state_normal)) {
		ret = PTR_ERR(chip->pinctrl_state_normal);
		pr_err("Can not lookup wt6670f_normal pinstate, ret(%d)\n", ret);
		goto err_pinctrl_lookup;
	}

	chip->pinctrl_state_isp = pinctrl_lookup_state(chip->wt6670f_pinctrl, "rst_isp");
	if (IS_ERR_OR_NULL(chip->pinctrl_state_isp)) {
		ret = PTR_ERR(chip->pinctrl_state_isp);
		pr_err("Can not lookup wt6670f_isp  pinstate, ret(%d)\n", ret);
		goto err_pinctrl_lookup;
	}

	chip->pinctrl_scl_normal = pinctrl_lookup_state(chip->wt6670f_pinctrl, "scl_normal");
	if (IS_ERR_OR_NULL(chip->pinctrl_scl_normal)) {
		ret = PTR_ERR(chip->pinctrl_scl_normal);
		pr_err("Can not lookup scl_normal pinstate, ret(%d)\n", ret);
		goto err_pinctrl_lookup;
	}

	chip->pinctrl_scl_isp = pinctrl_lookup_state(chip->wt6670f_pinctrl, "scl_isp");
	if (IS_ERR_OR_NULL(chip->pinctrl_scl_isp)) {
		ret = PTR_ERR(chip->pinctrl_scl_isp);
		pr_err("Can not lookup scl_isp pinstate, ret(%d)\n", ret);
		goto err_pinctrl_lookup;
	}

	chip->pinctrl_sda_normal = pinctrl_lookup_state(chip->wt6670f_pinctrl, "sda_normal");
	if (IS_ERR_OR_NULL(chip->pinctrl_sda_normal)) {
		ret = PTR_ERR(chip->pinctrl_sda_normal);
		pr_err("Can not lookup sda_normal pinstate, ret(%d)\n", ret);
		goto err_pinctrl_lookup;
	}

	chip->pinctrl_sda_isp = pinctrl_lookup_state(chip->wt6670f_pinctrl, "sda_isp");
	if (IS_ERR_OR_NULL(chip->pinctrl_sda_isp)) {
		ret = PTR_ERR(chip->pinctrl_sda_isp);
		pr_err("Can not lookup sda_isp  pinstate, ret(%d)\n", ret);
		goto err_pinctrl_lookup;
	}

	return 0;

err_pinctrl_get:
	devm_pinctrl_put(chip->wt6670f_pinctrl);
err_pinctrl_lookup:
	chip->wt6670f_pinctrl = NULL;

	return ret;
}

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
int wt6670f_set_volt_count(int count)
{
	struct wt6670f_charger *chip =  _chip;
	int ret = 0;
	u16 step = abs(count);

	pr_info("Set qc3 vbus with %d pulse!\n", count);

	if (count  < 0) {
		chip->count -= step;
		step &= 0x7FFF;
		step = ((step & 0xff) << 8) | ((step >> 8) & 0xff);
		if (chip->count < 0)
			chip->count = 0;
	} else if (count > 0) {
		chip->count += step;
		step |= 0x8000;
		step = ((step & 0xff) << 8) | ((step >> 8) & 0xff);
	} else {
		pr_info("return witch count == 0\n");
		return 0;
	}

	pr_info("---->chip->count = %d  step = %04x\n", chip->count, step);

	ret = wt6670f_write_word(chip, WT6670F_REG_QC35_PM, step);

	return ret;
}
EXPORT_SYMBOL_GPL(wt6670f_set_volt_count);

int wt6670f_set_qc3_volt_count(int count)
{
	int ret = 0;
	u16 step = abs(count);
	struct wt6670f_charger *chip =  _chip;

	pr_info("Set vbus with %d pulse!\n", count);

	if (count  < 0) {
		chip->count -= step;
		step &= 0x7FFF;
		step = ((step & 0xff) << 8) | ((step >> 8) & 0xff);
		if (chip->count < 0)
			chip->count = 0;
	} else if (count > 0) {
		chip->count += step;
		step |= 0x8000;
		step = ((step & 0xff) << 8) | ((step >> 8) & 0xff);
	} else {
		pr_info("return witch count == 0\n");
		return 0;
	}

	pr_info("---->chip->count = %d  step = %04x\n", chip->count, step);

	ret = wt6670f_write_word(chip, WT6670F_REG_QC30_PM, step);

	return ret;
}
EXPORT_SYMBOL_GPL(wt6670f_set_qc3_volt_count);

static int wt6670f_check_sw_chg_psy(struct wt6670f_charger *chip)
{
	if (IS_ERR_OR_NULL(chip->charger_psy)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
		chip->charger_psy = power_supply_get_by_name("primary_chg");
#else
		chip->charger_psy = power_supply_get_by_name("ext_charger_type");
#endif
		if (IS_ERR_OR_NULL(chip->charger_psy)) {
			pr_err("get chg psy failed\n");
			return -ENODEV;
		}
	}

	pr_info("found %s power supply device\n", chip->charger_psy->desc->name);

	return 0;
}

static int wt6670f_psy_notifier_cb(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct wt6670f_charger *chip = container_of(nb, struct wt6670f_charger, nb);
	union power_supply_propval val = {0};
	struct power_supply *psy = data;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;
	int ret = 0;

	if (IS_ERR_OR_NULL(chg_psy)) {
		chg_psy = power_supply_get_by_name("mtk-master-charger");
		if (IS_ERR_OR_NULL(chg_psy)) {
			pr_err("failed to get mtk-master-charger device\n");
			return NOTIFY_DONE;
		} else {
			info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
			if (IS_ERR_OR_NULL(info)) {
				pr_err("failed to get mtk charger info device\n");
				return NOTIFY_DONE;
			}
		}
	}

	pr_debug("enter, power supply name is %s\n", psy->desc->name);

	if (IS_ERR_OR_NULL(chip)) {
		pr_err("failed to get wt6670f_charger chip device\n");
		return NOTIFY_DONE;
	}

	if (IS_ERR_OR_NULL(chip->charger_dev)) {
		chip->charger_dev = get_charger_by_name("primary_chg");
		if (IS_ERR_OR_NULL(chip->charger_dev)) {
			pr_err("get primary chg dev failed\n");
			return NOTIFY_DONE;
		}
	}

	ret = wt6670f_check_sw_chg_psy(chip);
	if (ret) {
		pr_err("can't get charger psy failed\n");
	} else if (psy == chip->charger_psy) {
		ret = power_supply_get_property(chip->charger_psy,
						POWER_SUPPLY_PROP_USB_TYPE, &val);
		if (ret) {
			pr_err("get charger type from switch charger failed\n");
		} else {
			if (val.intval == POWER_SUPPLY_USB_TYPE_DCP) {
				if (adapter_dev_get_property(info->select_adapter, CAP_TYPE) == MTK_PD_APDO) {
					pr_info("ignore QC3 or QC3P detection due to pd pps adapter\n");
					return NOTIFY_DONE;
				}

				if (chip->qc3p_type == QC3P_POWER_NONE && chip->first_detect_dcp == true) {
					pr_info("detect DCP and QC3P not detected, try to QC3P detection\n");
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
					info->is_hvdcp_detecting = true;
#endif
					schedule_delayed_work(&chip->get_charger_type_work, 0);
					msleep(4000);
					if (chip->qc3p_type == QC3P_POWER_15W) {
						pr_info("detect QC3 type, set vbus to %d mV\n", QC3_TARGE_VOLT);
						wt6670f_set_qc3_volt_count((QC3_TARGE_VOLT - QC3_BASE_VOLT) / QC3_VOLT_STEP);
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
						info->hvdcp_boost_done_time = ktime_get_boottime();
#endif
					} else if (chip->qc3p_type == QC3P_POWER_18W
						|| chip->qc3p_type == QC3P_POWER_27W
						|| chip->qc3p_type == QC3P_POWER_40W) {
						pr_info("detect QC3P type\n");
						qc3p_charger_ready = true;
					} else if (chip->charger_type == POWER_SUPPLY_TYPE_USB_QC2
								&& chip->qc3p_type == QC3P_POWER_NONE) {
						gpio_direction_output(chip->rst_gpio, 1);
						chip->first_detect_dcp = false;
						pr_info("get QC2 reset to DCP\n");
					} else if (chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP
								&& chip->qc3p_type == QC3P_POWER_NONE) {
						pr_info("only support DCP adapter, Ignore next detection\n");
						chip->first_detect_dcp = false;
						gpio_direction_output(chip->rst_gpio, 1);
					}
					power_supply_changed(chip->qc_phy_psy);
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
					info->is_hvdcp_detecting = false;
#endif
				} else {
					pr_info("QC3P or QC3 already detected done, Ignore detection\n");
				}
			} else if (val.intval != POWER_SUPPLY_USB_TYPE_UNKNOWN) {
				pr_info("Non-DCP, Ignore QC3P detection\n");
				power_supply_changed(chip->qc_phy_psy);
			} else {
				pr_info("removed charger, reset all type\n");
				wt6670f_reset_charger_type();
			}
		}
	}
	return NOTIFY_OK;
}
#endif

static int wt6670f_property_is_writeable(struct power_supply *psy,
	enum power_supply_property prop)
{
	switch (prop) {
	//case POWER_SUPPLY_PROP_PRESENT:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	case POWER_SUPPLY_PROP_USB_TYPE:
		return true;
#endif
	default:
		return false;
	}
}

static int wt6670f_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	//struct wt6670f_charger *chip = power_supply_get_drvdata(psy);
	//u16 reg_val;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wt6670f_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct wt6670f_charger *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	/*case POWER_SUPPLY_PROP_ONLINE:
		if (chip->charger_type)
			val->intval = 1;
		else
			val->intval = 0;
		break;*/
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = chip->qc3p_type;
		break;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = chip->usb_type;
		break;
#endif
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = chip->charger_type;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static char *wt6670f_usb_supplied_to[] = {
	"battery",
};

static enum power_supply_property wt6670f_props[] = {
	//POWER_SUPPLY_PROP_PRESENT,  //for z350 initialization
	POWER_SUPPLY_PROP_TYPE,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	POWER_SUPPLY_PROP_USB_TYPE, //for apsd rerun
#endif
	POWER_SUPPLY_PROP_CHARGE_TYPE,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static enum power_supply_usb_type wt6670f_usb_type[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};
#endif

static struct power_supply_desc wt6670f_desc = {
	.name = "qc_phy_wt6670f",
	.type = POWER_SUPPLY_TYPE_USB,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	.usb_types = wt6670f_usb_type,
	.num_usb_types = ARRAY_SIZE(wt6670f_usb_type),
#endif
	.get_property = wt6670f_get_property,
	.set_property = wt6670f_set_property,
	.properties = wt6670f_props,
	.num_properties = ARRAY_SIZE(wt6670f_props),
	.property_is_writeable = wt6670f_property_is_writeable,
};

static int wt6670f_power_supply_init(struct wt6670f_charger *chip, struct device *dev)
{
	struct power_supply_config psy_cfg = {
				.drv_data = chip,
				.of_node = dev->of_node,
	};
	psy_cfg.supplied_to = wt6670f_usb_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(wt6670f_usb_supplied_to);

	chip->qc_phy_psy = devm_power_supply_register(dev,
						 &wt6670f_desc,
						 &psy_cfg);

	if (IS_ERR(chip->qc_phy_psy)) {
		pr_err("err = %ld\n", PTR_ERR(chip->qc_phy_psy));
		return -EINVAL;
	}
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static int wt6670f_charger_probe(struct i2c_client *client)
#else
static int wt6670f_charger_probe(struct i2c_client *client,
        const struct i2c_device_id *id)
#endif
{
	int ret;
	u16 firmware_version = 0;
	struct wt6670f_charger *chip = NULL;
	struct device_node *np = NULL;

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	if (oem_pcba_charge_power() != CHARGE_POWER_33W) {
		pr_err("found 18W device, not init wt6670f\n");
		return -ENODEV;
	}
#endif
	pr_info("start!\n");
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (IS_ERR_OR_NULL(chip)) {
		pr_err("Couldn't allocate memory\n");
		return -ENOMEM;
	}

	_chip = chip;

	chip->client = client;
	chip->dev = &client->dev;
	mutex_init(&chip->irq_complete);
	mutex_init(&chip->isp_sequence_lock);

	chip->rerun_apsd_count = 0;
	chip->tcpc_attach = false;
	chip->irq_data_process_enable = false;
	chip->irq_waiting = false;
	chip->dpdm_mode = QC30_DPDM;
	chip->resume_completed = true;
	chip->hvdcp_en = true;
	np = chip->dev->of_node; 

	i2c_set_clientdata(client, chip);

	wt6670f_set_usbsw_state(chip, USBSW_CHG);
	ret = wt6670f_parse_dt(chip);
	if (ret) {
		pr_err("Couldn't parse DT nodes, ret(%d)\n", ret);
		goto err_mutex_init;
	}

	ret = devm_gpio_request(&client->dev, chip->rst_gpio, "wt6670f reset gpio");
	if (ret) {
		pr_err("request wt6670f reset gpio failed, ret(%d)\n", ret);
		goto err_mutex_init;
	}
#if 0
	gpio_direction_output(chip->rst_gpio, 1);
	gpio_set_value(chip->rst_gpio, 0);
#endif
	ret = devm_gpio_request(&client->dev, chip->int_gpio, "wt6670f int gpio");
	if (ret) {
		pr_err("request wt6670f int gpio failed, ret(%d)\n", ret);
		goto err_mutex_init;
	}

	gpio_direction_output(chip->int_gpio, 1);

#if 0
	irqn = gpio_to_irq(chip->int_gpio);
	if (irqn < 0) {
		pr_err("%d gpio_to_irq failed\n", irqn);
		return irqn;
	}

	chip->client->irq = irqn;
	pr_info("request wt6670f int gpio, irqn:%d\n", irqn);

	ret = devm_request_threaded_irq(chip->dev, chip->client->irq,
			NULL, wt6670f_interrupt_handler,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT, 
			"wt6670f_irq", chip);
	if (ret < 0) {
		pr_err("request thread irq fail(%d)\n", ret);
		return ret;
	}
#endif

	chip->charger_psy = power_supply_get_by_name("primary_chg");
	if (!chip->charger_psy) {
		pr_err("get charger power supply failed\n");
		return -EPROBE_DEFER;
	}

	chip->charger_dev = get_charger_by_name("primary_chg");
	if (!chip->charger_dev) {
		pr_err("get charger device failed\n");
		return -EPROBE_DEFER;
	}

	ret = wt6670f_pinctrl_init(chip);
	if (!ret && chip->wt6670f_pinctrl) {
		ret = pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_state_isp);
		ret = pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_scl_normal);
		ret = pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_sda_normal);
		if (ret < 0)
			pr_err("Failed to select active pinstate, ret(%d)\n", ret);
	}

	gpio_direction_output(chip->rst_gpio, 1);
	gpio_set_value(chip->rst_gpio, 0);
	mdelay(10);

	ret = wt6670f_get_firmware_version(chip, &firmware_version);
	pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_scl_isp);
	pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_sda_isp);

	wt6670f_reset_chg_type();
	pr_info("firmware_version = 0x%x\n", firmware_version);

	if (firmware_version != WT6670F_FIRMWARE_VERSION) {
		pr_info("firmware need upgrade run wt6670f_isp again!\n");
		wt6670f_do_reset(chip);
		isp_pinctrl_enable(chip);
		wt6670f_isp_flow(chip);
		//wt6670f_do_reset(chip);
		pr_info("firmware run wt6670f_isp end!\n");
	}

	pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_scl_normal);
	pinctrl_select_state(chip->wt6670f_pinctrl, chip->pinctrl_sda_normal);
	mdelay(5);

	ret = wt6670f_get_firmware_version(chip, &firmware_version);
	if ((ret < 0) || firmware_version != WT6670F_FIRMWARE_VERSION) {
		ret = -ENODEV;
		goto err_register_psy;
	} else {
		pr_info("firmware_version = 0x%x, ret(%d)\n", firmware_version, ret);
	}

	INIT_DELAYED_WORK(&chip->get_charger_type_work, wt6670f_get_charger_type_func_work);

	ret = wt6670f_power_supply_init(chip, &chip->client->dev);
	if (ret) {
		pr_err("failed to register power supply, ret(%d)\n", ret);
		goto err_register_psy;
	}

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->nb.notifier_call = wt6670f_psy_notifier_cb;
	power_supply_reg_notifier(&chip->nb);
	qc_logic_probe_done = true;
	chip->first_detect_dcp = true;
#endif

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	FULL_PRODUCT_DEVICE_INFO(ID_QC_LOGIC, "WT6670F");
#endif

	pr_info("wt6670f successfully probed.\n");

	return 0;
err_register_psy:
	devm_pinctrl_put(chip->wt6670f_pinctrl);
	gpio_free(chip->rst_gpio);
	gpio_free(chip->int_gpio);
	gpio_free(chip->wt6670f_scl_gpio);
	gpio_free(chip->wt6670f_sda_gpio);

err_mutex_init:
	mutex_destroy(&chip->irq_complete);
	mutex_destroy(&chip->isp_sequence_lock);

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static void wt6670f_charger_remove(struct i2c_client *client)
#else
static int wt6670f_charger_remove(struct i2c_client *client)
#endif
{
	struct wt6670f_charger *chip = i2c_get_clientdata(client);

	pr_info("enter\n");
	mutex_destroy(&chip->irq_complete);
	mutex_destroy(&chip->isp_sequence_lock);
	chip->hvdcp_en = true;
#if 0
	cancel_delayed_work_sync(&chip->charger_type_det_work);
	cancel_delayed_work_sync(&chip->chip_update_work);
	cancel_delayed_work_sync(&chip->hvdcp_det_retry_work);
#endif

#if !(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	return 0;
#endif
}

static int wt6670f_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wt6670f_charger *chip = i2c_get_clientdata(client);

	//cancel_delayed_work_sync(&chip->conn_therm_work);

	mutex_lock(&chip->irq_complete);
	chip->resume_completed = false;
	mutex_unlock(&chip->irq_complete);

	return 0;
}

static int wt6670f_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wt6670f_charger *chip = i2c_get_clientdata(client);

	if (chip->irq_waiting) {
		pr_err_ratelimited("Aborting suspend, an interrupt was detected while suspending\n");
		return -EBUSY;
	}
	return 0;
}

static int wt6670f_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct wt6670f_charger *chip = i2c_get_clientdata(client);
	//union power_supply_propval val;
	//int usb_present = 0;

	mutex_lock(&chip->irq_complete);
	chip->resume_completed = true;
	if (chip->irq_waiting) {
		mutex_unlock(&chip->irq_complete);
		enable_irq(client->irq);
	} else {
		mutex_unlock(&chip->irq_complete);
	}

	return 0;
}

static const struct dev_pm_ops wt6670f_pm_ops = {
	.suspend = wt6670f_suspend,
	.suspend_noirq = wt6670f_suspend_noirq,
	.resume = wt6670f_resume,
};

static void wt6670f_charger_shutdown(struct i2c_client *client)
{
	struct wt6670f_charger *chip = i2c_get_clientdata(client);

	pr_info("enter\n");

	wt6670f_write_byte(chip, WT6670F_REG_SOFT_RESET, SOFT_RESET_VAL);

	chip->hvdcp_en = true;
#if 0
	cancel_delayed_work_sync(&chip->charger_type_det_work);
	cancel_delayed_work_sync(&chip->chip_update_work);
	cancel_delayed_work_sync(&chip->hvdcp_det_retry_work);
#endif
	mutex_destroy(&chip->irq_complete);
	mutex_destroy(&chip->isp_sequence_lock);
}

static const struct of_device_id wt6670f_match_table[] = {
	{ .compatible = "wt,wt6670f_charger",},
	{ },
};

static const struct i2c_device_id wt6670f_charger_id[] = {
	{"wt6670f_charger", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, wt6670f_charger_id);

static struct i2c_driver wt6670f_charger_driver = {
	.driver     = {
		.name       = "wt6670f_charger",
		.owner      = THIS_MODULE,
		.of_match_table = wt6670f_match_table,
		.pm     = &wt6670f_pm_ops,
	},
	.probe      = wt6670f_charger_probe,
	.remove     = wt6670f_charger_remove,
	.id_table   = wt6670f_charger_id,
	.shutdown   = wt6670f_charger_shutdown,
};

module_i2c_driver(wt6670f_charger_driver);

MODULE_AUTHOR("hao.jia@tinno.com");
MODULE_DESCRIPTION("wt6670f Charger");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:wt6670f-charger");
