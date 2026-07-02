// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */
#define pr_fmt(fmt) "[z350] %s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/version.h>
#include <linux/time.h>
#include "qc_logic_z350.h"

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#include <tinno_charger.h>
#endif /* CONFIG_OEM_TINNO_CHARGER */

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
#include "../../turbo_charger/turbo_charger.h"
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include "charger_class.h"
#include "mtk_charger.h"
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
#include <mt-plat/v1/charger_class.h>
#else
#include <mt-plat/charger_class.h>
#endif /* LINUX_VERSION_CODE */

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include <dev_info.h>
#endif

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
extern bool qc_logic_probe_done;
extern int qc3p_charger_ready;
#endif

#define CMD_RERUN_APSD                  0x01

#define ADDR_QC_MODE                    0x02
#define MODE_QC2_5V                     0x01
#define MODE_QC2_9V                     0x02
#define MODE_QC2_12V                    0x03
#define MODE_QC3_5V                     0x04
#define MODE_QC3P_5V                    0x05

#define FUNC_ENABLE                     0x01
#define ADDR_INTB_EN                    0x03
#define ADDR_HVDCP_EN                   0x05
#define ADDR_BC12_EN                    0x06

#define PULSE_DP                      0x8000
#define PULSE_DM                      0x0000
#define ADDR_QC3_PULSE                  0x73
#define ADDR_QC3P_PULSE                 0x83

#define ADDR_CHGER_STATUS               0x11
#define ADDR_VBUS_ADC                   0x12
#define VENDOR_ID                       0x13
#define ADDR_VER                        0x14

#define QC3_VOLT_STEP                    200 /* mV */
#define QC3_BASE_VOLT                   5000 /* mV */
#define QC3_TARGE_VOLT                  6600 /* mV */

struct z350_chip {
	struct i2c_client *i2c;
	struct mutex mutex;
	struct mutex qc3p_lock;
	int irq_gpio;
	int reset_gpio;
	int charger_type;
	int usb_type;
	int qc3p_type;
	int vendor_id;
	int count;
	struct delayed_work usb_detect_delayed_work;
	struct delayed_work hvdcp_timeout_delayed_work;
	struct delayed_work init_delayed_work;
	struct power_supply *z350_usb_psy;
	struct power_supply *usb_psy;
	int rerun_done;
	int qc3_vset_mv;
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	bool first_detect_dcp;
	struct notifier_block nb;
	struct charger_device *charger_dev;
#endif
};

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
static struct z350_chip *g_chip = NULL;
#endif

static int first_error = 0;
static struct power_supply_desc z350_desc;

/* i2c operate interfaces */

static int z350_read_byte(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	struct z350_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->mutex);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	mutex_unlock(&chip->mutex);
	if (ret < 0) {
		pr_err("0x%x error, ret(%d)\n", reg, ret);
		return ret;
	}
	//pr_info("Read: [0x%x]=0x%x\n", reg, ret);

	ret &= 0xff;
	*dest = ret;

	return 0;
}

static int z350_read_word(struct i2c_client *i2c, u8 reg, u16 *dest)
{
	struct z350_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->mutex);
	ret = i2c_smbus_read_word_data(i2c, reg);
	mutex_unlock(&chip->mutex);
	if (ret < 0) {
		pr_err("0x%x error, ret(%d)\n", reg, ret);
		return ret;
	}
	ret &= 0xffff;
	*dest = (ret << 8) | (ret >> 8);
	//pr_info("Read: [0x%x]=0x%x\n", reg, *dest);
	return 0;
}

static int z350_read_double_word(struct i2c_client *i2c, u8 reg, u32 *dest)
{
	struct z350_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->mutex);
	ret = i2c_smbus_read_i2c_block_data(i2c, reg, 4, (u8 *)dest);
	mutex_unlock(&chip->mutex);
	if (ret < 0) {
		pr_err("0x%x error, ret(%d)\n", reg, ret);
		return ret;
	}
	//pr_info("Read: [0x%x]=0x%x\n", reg, *dest);

	return 0;
}

static int z350_write_byte(struct i2c_client *i2c, u8 reg, u8 value)
{
	struct z350_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	if (reg == ADDR_HVDCP_EN) {
		pr_info("Write: [0x%x]=0x%x\n", reg, value);
	} else {
		pr_info("Write: [0x%x]=0x%x\n", reg, value);
	}

	mutex_lock(&chip->mutex);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	mutex_unlock(&chip->mutex);
	if (ret < 0)
		pr_err("0x%x error, ret(%d)\n", reg, ret);

	return ret;
}

static int z350_write_word(struct i2c_client *i2c, u8 reg, u16 value)
{
	struct z350_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	pr_info("Write: [0x%x]=0x%x\n", reg, value);
	mutex_lock(&chip->mutex);
	ret = i2c_smbus_write_word_data(i2c, reg, value);
	mutex_unlock(&chip->mutex);
	if (ret < 0)
		pr_err("0x%x error, ret(%d)\n", reg, ret);

	return ret;
}

static int z350_parse_dts(struct device_node *np, struct z350_chip *chip)
{
	int ret;
	u32 val = 0;

	chip->irq_gpio = of_get_named_gpio(np, "z350,irq_gpio", 0);
	if (chip->irq_gpio < 0) {
		pr_err("error invalid irq gpio err: %d\n", chip->irq_gpio);
		return -EINVAL;;
	}

	ret = gpio_request(chip->irq_gpio, "z350_irq_gpio");
	if (ret < 0) {
		pr_err("request irq gpio failed: %d\n", ret);
	}

	pr_info("valid irq_gpio number: %d\n", chip->irq_gpio);

	chip->reset_gpio = of_get_named_gpio(np, "z350,reset_gpio", 0);
	if (chip->reset_gpio < 0) {
		pr_err("error invalid reset gpio err: %d\n", chip->reset_gpio);
		return -EINVAL;  //may no need reset
	}

	ret = gpio_request(chip->reset_gpio, "z350_reset_gpio");
	if (ret < 0) {
		pr_err("request reset gpio failed: %d\n", ret);
	}

	pr_info("valid reset_gpio number: %d\n", chip->reset_gpio);

	//gpio_direction_output(chip->reset_gpio, 1);

	ret = of_property_read_u32(np, "z350,qc3-vset-mv", &val);
	if (ret < 0) {
		pr_err("get qc3-vset-mv failed: %d\n", ret);
		chip->qc3_vset_mv = QC3_TARGE_VOLT;
	} else
		chip->qc3_vset_mv = val;

	return 0;
}

u8 z350_check_crc(u32 *file, u32 file_size)
{
	u32 crc = 0xFFFF1326;
	const u32 poly = 0x04C11DB6;
	u32 newbit, newword, rl_crc;
	u16 i = 0, j = 0;
	crc = 0xFFFF1326;
	for (i = 0; i < file_size / 4; i++) {
		for (j = 0; j < 32; j++) {
			newbit = ((crc >> 31) ^ ((*file >> j) & 1)) & 1;
			if (newbit)
				newword = poly;
			else
				newword = 0;
			rl_crc = (crc << 1) | newbit;
			crc = rl_crc ^ newword;
		}
		file++;
	}
	if (crc == 0xC704DD7B)
		return 1;
	else
		return 0;
}

u8 write_memory_one_word_i2c(struct z350_chip *chip, u32 addr, u32 data)
{
	u8 reg;
	u8 BUSY_flag = 0 ;
	u32 i = 0;
	u8 DATA[6] = {addr & 0xff, (addr >> 8) & 0x03, data & 0xff, (data >> 8) & 0xFF, (data >> 16) & 0xFF, (data >> 24) & 0xFF};

	for (i = 0; i < 6; i++) { // Select memory block, configure address and data
		reg = DATA[i];
		z350_write_byte(chip->i2c, 0x45 + i, reg);
	}
	reg = 0x01;
	z350_write_byte(chip->i2c, 0x4B, reg); // Write enable
	reg = 0x00;
	z350_write_byte(chip->i2c, 0x4B, reg);
	for (i = 0; i < 50; i++) { // Wait for a data program to complete, The timeout is 50*2000us
		// The value of the read 0x4b register is stored in variable BUSY_flag
		z350_read_byte(chip->i2c, 0x4B, &BUSY_flag);
		BUSY_flag &= 0x80; // Bit7 is the busy flag bit
		if (BUSY_flag == 0x00)
			return 1;
		udelay(2000);
	}

	return 0;
}

u32 read_memory_one_word_i2c(struct z350_chip *chip, u32 addr)
{
	u32 data;
	u32 i = 0;
	u8 ADDR[2] = {addr & 0xff, (addr >> 8) & 0x03};
	u8 reg;

	for (i = 0; i < 2; i++) {
		reg = ADDR[i];
		z350_write_byte(chip->i2c, 0x45 + i, reg); //write address
	}
	z350_read_double_word(chip->i2c, 0x4C, &data);

	return data;
}

void z350_program_init(struct z350_chip *chip)	 // Execute only once at the beginning of the program.
{
	u8 reg;
	reg = 0x1f;
	z350_write_byte(chip->i2c, 0x42, reg); // CPU reset
	reg = 0x9f;
	z350_write_byte(chip->i2c, 0x43, reg); // close watchdog,open memory clock
	reg = 0x31;
	z350_write_byte(chip->i2c, 0x44, reg); //select the memory interface
	reg = 0x33;
	z350_write_byte(chip->i2c, 0x44, reg); //write CS to 1, select the chip, select the memory interface
}

void z350_program_end(struct z350_chip *chip) // Execute only once at the ending of the program.
{
	u8 reg;
	reg = 0x31;
	z350_write_byte(chip->i2c, 0x44, reg); //clear the CS signal
	reg = 0x30;
	z350_write_byte(chip->i2c, 0x44, reg); //clear the other signal
}

void z350_verify_init(struct z350_chip *chip) // Execute only once at the beginning of the program.
{
	u8 reg;
	reg = 0x1f;
	z350_write_byte(chip->i2c, 0x42, reg); // CPU reset
	reg = 0x9f;
	z350_write_byte(chip->i2c, 0x43, reg); // close watchdog,open memory clock
	reg = 0x37;
	z350_write_byte(chip->i2c, 0x44, reg); //write READ to 1
}

void z350_verify_end(struct z350_chip *chip) // Execute only once at the ending of the program.
{
	u8 reg;
	reg = 0x30;
	z350_write_byte(chip->i2c, 0x44, reg); //write READ to 0
}

void z350_hard_reset(struct z350_chip *chip)
{
	gpio_direction_output(chip->reset_gpio, 0);
	mdelay(10);
	gpio_direction_output(chip->reset_gpio, 1);
	mdelay(10);
	gpio_direction_output(chip->reset_gpio, 0);
}

#define NON_TEST_MODE	0x00
#define TEST_MODE	0x01
#define PROG_ERROR	0x02
u8 z350_program(struct z350_chip *chip, u32 *file, u32 file_size)
{
	u32 temp1;
	u8 temp2;
	u8 flag = 0;
	u32 *file_w = file;
	u32 *file_r = file;
	u16 i = 0;
	u8 retry = 3;

	if (z350_check_crc(file, file_size) == 0) {
		pr_err("Check CRC failed!\n");
		return 0;
	}

	file_size /= 4;

	z350_read_double_word(chip->i2c, 0x13, &temp1);
	pr_info("[0x13] = 0x%x\n", temp1); //debug

	if (temp1 == 0x30353349) {
		temp2 = 0x6B;
		z350_write_byte(chip->i2c, 0x40, temp2);
		udelay(1000);
		temp2 = 0;
		z350_read_byte(chip->i2c, 0x40, &temp2);
		if (temp2 == 0x6B)
			flag = TEST_MODE;
		else
			flag = NON_TEST_MODE;
	} else {
		z350_read_double_word(chip->i2c, 0x41, &temp1);
		if (temp1 == 0x30079F00)
			flag = TEST_MODE;
		else
			flag = NON_TEST_MODE;
	}

	pr_info("flag = 0x%x\n", flag); //debug
	while (flag && retry) {
		z350_program_init(chip);
		pr_info("programming the first data to 0!\n");
		if (write_memory_one_word_i2c(chip, 0x00, 0) == 0) {
			flag = PROG_ERROR;
			pr_info("Failed to programming the first data to 0!\n");
			//break;
		} else {
			pr_info("programming binary!\n");
			file_w++;
			for (i = 1; i < file_size; i++) {
				if (write_memory_one_word_i2c(chip, i, *file_w++) == 0) {
					flag = PROG_ERROR;
					pr_info("z350_programming failed!\n");
					break;
				} else
					flag = TEST_MODE;
			}
		}
		z350_program_end(chip);

		if ((i == file_size) && (flag == TEST_MODE)) {
			pr_info("Verify binary!\n");
			z350_verify_init(chip);
			file_r++;
			for (i = 1; i < file_size; i++) {
				if (*file_r++ != read_memory_one_word_i2c(chip, i)) {
					flag = PROG_ERROR;
					pr_info("Verify failed!\n");
					break;
				} else
					flag = TEST_MODE;
			}
			z350_verify_end(chip);

			if (flag == TEST_MODE) {
				pr_info("programming the first data!\n");
				z350_program_init(chip);
				if (write_memory_one_word_i2c(chip, 0x00, *file) == 0) {
					flag = PROG_ERROR;
					pr_info("Failed to programming the first data!\n");
					//break;
				}
				z350_program_end(chip);
				pr_info("Verify first data!\n");
				z350_verify_init(chip);
				if (*file != read_memory_one_word_i2c(chip, 0x00)) {
					flag = PROG_ERROR;
					pr_info("Verify failed first data !\n");
					//break;//check错误
				}
				z350_verify_end(chip);
			}
		}
		if (flag != PROG_ERROR)
			break;
		retry--;
	}

	z350_hard_reset(chip);

	if (flag == TEST_MODE)
		return 1;
	else
		return 0;
}

static void z350_rerun_apsd(struct z350_chip *chip)
{
	u16 reg_val;
	reg_val = 0;

	z350_write_byte(chip->i2c, ADDR_HVDCP_EN, (u8)reg_val);
	reg_val = FUNC_ENABLE;

	z350_write_byte(chip->i2c, CMD_RERUN_APSD, (u8)reg_val);
	chip->count = 0;

	pr_info("enter\n");
}

static void z350_hard_reset_once(struct z350_chip *chip)
{
	gpio_direction_output(chip->reset_gpio, 1);
	msleep(10);
	gpio_direction_output(chip->reset_gpio, 0);
	msleep(10);
}

static void z350_try_initialization(struct z350_chip *chip)
{
	static bool init = false;
	int ret = 0;
	u16 reg_val;

	if (init)
		return;

	pr_info("enter\n");

	if (z350_read_word(chip->i2c, ADDR_VER, &reg_val))
		return;

	pr_info("VER=0x%x\n", reg_val);
	if (reg_val < 0x0A0F) {
		pr_info("FW ver is older than 0x0A0F!\n");
		if (z350_program(chip, (u32 *)Z350_0A0F_BIN, 0xe48) == 0)
			pr_err("FW programming failed!\n");
		else
			pr_info("FW programming successfully!\n");
		msleep(100);
	}

	reg_val = MODE_QC3P_5V;
	ret = z350_write_byte(chip->i2c, ADDR_QC_MODE, (u8)reg_val);

	reg_val = FUNC_ENABLE;
	ret |= z350_write_byte(chip->i2c, ADDR_INTB_EN, (u8)reg_val);
	ret |= z350_write_byte(chip->i2c, ADDR_BC12_EN, (u8)reg_val);
	if (ret) {
		pr_err("init Z350 fail!\n");
		return;
	}

	pr_info("first time rerun apsd after init\n");
	z350_rerun_apsd(chip);
	init = true;

	return;
}

static void z350_qc3_pulse(struct z350_chip *chip, int target_cnt)
{
	int cnt;
	u16 reg_val;

	if (target_cnt == chip->count || target_cnt > 35)
		return;

	cnt = target_cnt - chip->count;
	if (cnt > 0)
		reg_val = (cnt << 8) | 0x80;
	else
		reg_val = (-1 * cnt) << 8;

	if (z350_write_word(chip->i2c, ADDR_QC3_PULSE, reg_val)) {
		pr_err("request qc3 voltage fail!\n");
		return;
	}
	chip->count = target_cnt;
}

static void init_work_func(struct work_struct *work)
{
	struct z350_chip *chip = container_of(work, struct z350_chip,
								init_delayed_work.work);

	pr_info("enter\n");
	z350_try_initialization(chip);
}

static void hvdcp_timeout_work_func(struct work_struct *work)
{
	struct z350_chip *chip = container_of(work, struct z350_chip,
								hvdcp_timeout_delayed_work.work);
	union power_supply_propval val = {0};
	int ret = 0;

	pr_info("enter\n");
	if (chip->usb_psy) {
		val.intval = 1000000; //INT_MAX;
		ret = power_supply_set_property(chip->usb_psy,
						POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
		if (ret) {
			pr_err("failed to set input current limit\n");
		}
	}
}

static int z350_check_sw_chg_psy(struct z350_chip *chip);
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
int z350_reset_charger_type(void);
#endif

static void usb_detect_work_func(struct work_struct *work)
{
	struct z350_chip *chip = container_of(work, struct z350_chip,
									usb_detect_delayed_work.work);
	/*TN Begin modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
	u16 reg_val, val_temp;
	/*TN End modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
	int ret;
	u8 apsd_result = 0xFF;

	ret = z350_read_word(chip->i2c, ADDR_CHGER_STATUS, &reg_val);
	if (ret)
		return;

	pr_info("CHGER_STATUS:0x%x\n", reg_val);
#if 0
	if (reg_val == 0) {
		chip->count = 0;
		chip->rerun_done = 0;
		reg_val = 0;
		z350_write_byte(chip->i2c, ADDR_HVDCP_EN, (u8)reg_val);
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		chip->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		chip->qc3p_type = QC3P_POWER_NONE;
		goto exit;
	}
#endif
	if (reg_val == 0x902) {
		if (first_error == 0) {
			first_error = 1;
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
			z350_reset_charger_type();
#endif
			pr_info("first_error = %d\n", first_error);
			power_supply_changed(chip->usb_psy);
		} else {
			z350_write_byte(chip->i2c, ADDR_QC_MODE, MODE_QC2_5V);
			ret = z350_read_word(chip->i2c, ADDR_QC_MODE, &reg_val);
			if (ret)
				return;
			pr_info("mode:0x%x\n", reg_val);
		}
		goto exit;
	}

/*TN Begin modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
	val_temp = reg_val >> 8;
	if ((val_temp & 0xFF) == 0x2 || (val_temp & 0xFF) == 0x3 || (val_temp & 0xFF) == 0x1) {
/*TN End modified by maocai.cao/808964 20231124 CR/EKFOGO4G-3815*/
		if (!chip->rerun_done) {
			pr_info("rerun apsd\n");
			z350_rerun_apsd(chip);
			chip->rerun_done++;
		}
		chip->charger_type = POWER_SUPPLY_TYPE_USB_DCP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_NONE;
#endif
		goto exit;
	}
	apsd_result = reg_val >> 8;
	switch (apsd_result) {
	case 0x1:
		chip->charger_type = POWER_SUPPLY_TYPE_APPLE_BRICK_ID;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_NONE;
#endif
		break;

	case 0x2:
		chip->charger_type = POWER_SUPPLY_TYPE_APPLE_BRICK_ID;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_NONE;
#endif
		break;

	case 0x3:
		chip->charger_type = POWER_SUPPLY_TYPE_USB;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_SDP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_NONE;
#endif
		break;

	case 0x4:
		chip->charger_type = POWER_SUPPLY_TYPE_USB_CDP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_CDP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_NONE;
#endif
		break;

	case 0x5:
		chip->charger_type = POWER_SUPPLY_TYPE_USB_DCP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_NONE;
#endif
		break;

	case 0x6:
		chip->charger_type = POWER_SUPPLY_TYPE_USB_QC2;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_NONE;
#endif
		break;

	case 0x7:
		chip->charger_type = POWER_SUPPLY_TYPE_USB_QC3;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_15W;
#endif
		break;

	case 0x8:
		chip->charger_type = POWER_SUPPLY_TYPE_USB_QC3P;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_18W;
#endif
		break;

	case 0x9:
		chip->charger_type = POWER_SUPPLY_TYPE_USB_QC3P;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_27W;
#endif
		break;

	case 0x12:
		chip->charger_type = POWER_SUPPLY_TYPE_USB_QC3P;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		chip->qc3p_type = QC3P_POWER_40W;
#endif
		break;

	case 0x10:
		chip->charger_type = POWER_SUPPLY_TYPE_USB_QC2;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_DCP;
#endif
		reg_val = FUNC_ENABLE;
		z350_write_byte(chip->i2c, ADDR_HVDCP_EN, (u8)reg_val);
		break;

	default:
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		chip->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
#endif
		break;
	}
exit:
	if (apsd_result >= 0x6 && apsd_result != 0x10) {
		hvdcp_timeout_work_func(&chip->hvdcp_timeout_delayed_work.work);
		cancel_delayed_work_sync(&chip->hvdcp_timeout_delayed_work);
	}

	power_supply_changed(chip->z350_usb_psy);
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	pr_info("charger_type = %d, qc3p_type = %d\n",
						chip->charger_type, chip->qc3p_type);
#else
	pr_info("charger_type = %d\n", chip->charger_type);
#endif

	return;
}

static irqreturn_t z350_irq_thread(int irq, void *handle)
{
	struct z350_chip *chip = (struct z350_chip *)handle;
	int ret = 0;
	u16 reg_val;

	pr_info("enter and clear interrupt flag\n");

	//usb_detect_work_func(&chip->usb_detect_delayed_work.work);
	ret = z350_read_word(chip->i2c, ADDR_CHGER_STATUS, &reg_val);
	if (ret) {
		ret = z350_read_word(chip->i2c, ADDR_CHGER_STATUS, &reg_val);
		return IRQ_HANDLED;
	}

	return IRQ_HANDLED;
}

static u32 z350_get_vbus(struct z350_chip *chip)
{
	u16 reg_val;

	if (z350_read_word(chip->i2c, ADDR_VBUS_ADC, &reg_val)) {
		pr_err("Read VBUS fail!\n");
		return 0;
	}

	return (reg_val * 2400 >> 7);
}

static int z350_property_is_writeable(struct power_supply *psy,
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

static int z350_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct z350_chip *chip = power_supply_get_drvdata(psy);
	u16 reg_val;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		if (val->intval) {
			//z350_try_initialization(chip);
			cancel_delayed_work_sync(&chip->init_delayed_work);
			schedule_delayed_work(&chip->init_delayed_work, 0);
		}
		break;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	case POWER_SUPPLY_PROP_USB_TYPE:
		if (val->intval == POWER_SUPPLY_USB_TYPE_UNKNOWN)
			z350_rerun_apsd(chip);
		break;
#endif
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		reg_val = (val->intval == 5000000) ? MODE_QC2_5V : MODE_QC3P_5V;
		z350_write_byte(chip->i2c, ADDR_QC_MODE, (u8)reg_val);
		if (reg_val == MODE_QC3P_5V) {
			z350_rerun_apsd(chip);
			cancel_delayed_work_sync(&chip->usb_detect_delayed_work);
			schedule_delayed_work(&chip->usb_detect_delayed_work, 50);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int z350_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct z350_chip *chip = power_supply_get_drvdata(psy);

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
		val->intval = z350_get_vbus(chip);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = chip->qc3_vset_mv * 1000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static char *z350_usb_supplied_to[] = {
	"battery",
};

static enum power_supply_property z350_props[] = {
	//POWER_SUPPLY_PROP_PRESENT,  //for z350 initialization
	POWER_SUPPLY_PROP_TYPE,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	POWER_SUPPLY_PROP_USB_TYPE, //for apsd rerun
#endif
	POWER_SUPPLY_PROP_CHARGE_TYPE,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static enum power_supply_usb_type z350_usb_type[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};
#endif
static struct power_supply_desc z350_desc = {
	.name = "qc_phy_z350",
	.type = POWER_SUPPLY_TYPE_USB,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	.usb_types = z350_usb_type,
	.num_usb_types = ARRAY_SIZE(z350_usb_type),
#endif
	.get_property = z350_get_property,
	.set_property = z350_set_property,
	.properties = z350_props,
	.num_properties = ARRAY_SIZE(z350_props),
	.property_is_writeable = z350_property_is_writeable,
};

static int z350_power_supply_init(struct z350_chip *chip, struct device *dev)
{
	struct power_supply_config psy_cfg = { .drv_data = chip,
			   .of_node = dev->of_node,
	};
	psy_cfg.supplied_to = z350_usb_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(z350_usb_supplied_to);

	chip->z350_usb_psy = devm_power_supply_register(dev,
						 &z350_desc,
						 &psy_cfg);

	if (IS_ERR(chip->z350_usb_psy)) {
		pr_info("err = %ld\n", PTR_ERR(chip->z350_usb_psy));
		return -EINVAL;
	}
	return 0;
}

static int z350_suspend(struct device *dev)
{
	struct z350_chip *chip = dev_get_drvdata(dev);

	pr_info("enter\n");
	disable_irq_wake(chip->i2c->irq);
	disable_irq(chip->i2c->irq);

	return 0;
}

static int z350_resume(struct device *dev)
{
	struct z350_chip *chip = dev_get_drvdata(dev);

	pr_info("enter\n");

	enable_irq_wake(chip->i2c->irq);
	enable_irq(chip->i2c->irq);

	//schedule_delayed_work(&chip->usb_detect_delayed_work, 200);
	return 0;
}

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
int z350_set_volt_count(int count)
{
	int ret = 0;
	struct device *dev = NULL;
	u16 step = abs(count);

	if (IS_ERR_OR_NULL(g_chip)) {
		pr_err("invalid g_chip, ignore set volt count\n");
		ret = -ENODEV;
	} else {
		dev = &g_chip->i2c->dev;
		pr_info("set QC3P vbus with %d pulse!\n", count);
		if (count  < 0) {
			g_chip->count -= step;
			step &= 0x7FFF;
			step = ((step & 0xff) << 8) | ((step >> 8) & 0xff);
			if (g_chip->count < 0)
				g_chip->count = 0;
		} else if (count > 0) {
			g_chip->count += step;
			step |= 0x8000;
			step = ((step & 0xff) << 8) | ((step >> 8) & 0xff);
		} else {
			pr_info("return witch count == 0\n");
			return 0;
		}

		pr_info("total_count = %d  step = %04x\n", g_chip->count, step);
		ret = z350_write_word(g_chip->i2c, ADDR_QC3P_PULSE, step);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(z350_set_volt_count);

int z350_reset_charger_type(void)
{
	struct device *dev = NULL;

	if (IS_ERR_OR_NULL(g_chip)) {
		pr_err("invalid g_chip, ignore reset charger type\n");
		return -ENODEV;
	} else {
		dev = &g_chip->i2c->dev;
		pr_info("enter and clear info\n");
		g_chip->qc3p_type = QC3P_POWER_NONE;
		g_chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		g_chip->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		g_chip->count = 0;
		g_chip->first_detect_dcp = true;
		first_error = 0;
		gpio_direction_output(g_chip->reset_gpio, 1);
		return 0;
	}
}
EXPORT_SYMBOL_GPL(z350_reset_charger_type);

static int z350_check_sw_chg_psy(struct z350_chip *chip);
static int z350_psy_notifier_cb(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct z350_chip *chip = container_of(nb, struct z350_chip, nb);
	union power_supply_propval val = {0};
	struct power_supply *psy = data;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;
	int idx, ret = 0;

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
		pr_err("failed to get z350 chip device\n");
		return NOTIFY_DONE;
	}

	if (IS_ERR_OR_NULL(chip->charger_dev)) {
		chip->charger_dev = get_charger_by_name("primary_chg");
		if (IS_ERR_OR_NULL(chip->charger_dev)) {
			pr_err("get primary chg dev failed\n");
			return NOTIFY_DONE;
		}
	}

	ret = z350_check_sw_chg_psy(chip);
	if (ret) {
		pr_err("can't get usb psy failed\n");
	} else if (psy == chip->usb_psy) {
		ret = power_supply_get_property(chip->usb_psy,
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
					mutex_lock(&chip->qc3p_lock);
					pr_info("detect DCP and QC3P not detected, try to QC3P detection\n");
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
					info->is_hvdcp_detecting = true;
#endif
					/*Set cur is 500ma before do BC1.2 & QC*/
					charger_dev_set_charging_current(chip->charger_dev, 500000);
					charger_dev_set_input_current(chip->charger_dev, 500000);
					ret = charger_dev_enable_dpdm_hz(chip->charger_dev);
					if (ret < 0) {
						pr_err("failed to enable switch charger DP DM Hiz (%d)\n", ret);
					}
					gpio_direction_output(chip->reset_gpio, 0);
					msleep(10);
					for (idx = 0; idx < 5; idx++) {
						pr_info("read count : %d\n", idx);
						schedule_delayed_work(&chip->usb_detect_delayed_work, 0);
						msleep(900);
					}
					mutex_unlock(&chip->qc3p_lock);
					if (chip->qc3p_type == QC3P_POWER_15W) {
						pr_info("detect QC3 type, set vbus to %d mV\n", QC3_TARGE_VOLT);
						z350_qc3_pulse(chip, ((QC3_TARGE_VOLT - QC3_BASE_VOLT) / QC3_VOLT_STEP));
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
						info->hvdcp_boost_done_time = ktime_get_boottime();
#endif
						//gpio_direction_output(chip->reset_gpio, 1);
					} else if (chip->qc3p_type == QC3P_POWER_18W
						|| chip->qc3p_type == QC3P_POWER_27W
						|| chip->qc3p_type == QC3P_POWER_40W) {
						pr_info("detect QC3P type\n");
						qc3p_charger_ready = true;
					} else if (chip->charger_type == POWER_SUPPLY_TYPE_USB_QC2
								&& chip->qc3p_type == QC3P_POWER_NONE) {
						z350_hard_reset_once(chip);
						chip->first_detect_dcp = false;
						pr_info("get QC2 reset to DCP\n");
					} else if (chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP
								&& chip->qc3p_type == QC3P_POWER_NONE) {
						pr_info("only support DCP adapter, Ignore next detection\n");
						chip->first_detect_dcp = false;
						gpio_direction_output(chip->reset_gpio, 1);
					}
					power_supply_changed(chip->z350_usb_psy);
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
					info->is_hvdcp_detecting = false;
#endif
				} else {
					pr_info("QC3P or QC3 already detected done, Ignore detection\n");
				}
			} else if (val.intval != POWER_SUPPLY_USB_TYPE_UNKNOWN) {
				pr_info("Non-DCP, Ignore QC3P detection\n");
				power_supply_changed(chip->z350_usb_psy);
			} else {
				pr_info("removed charger, reset all type\n");
				z350_reset_charger_type();
			}
		}
	}
	return NOTIFY_OK;
}
#endif

static int z350_check_sw_chg_psy(struct z350_chip *chip)
{
	if (IS_ERR_OR_NULL(chip->usb_psy)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
		chip->usb_psy = power_supply_get_by_name("primary_chg");
#else
		chip->usb_psy = power_supply_get_by_name("ext_charger_type");
#endif
		if (IS_ERR_OR_NULL(chip->usb_psy)) {
			pr_err("get chg psy failed\n");
			return -ENODEV;
		}
	}

	pr_info("found %s power supply device\n", chip->usb_psy->desc->name);

	return 0;
}

static int z350_check_vendor_id(struct z350_chip *chip)
{
	int ret = 0;
	u32 reg_val = 0;

	pr_info("enter\n");

	z350_hard_reset_once(chip);

	ret = z350_read_double_word(chip->i2c, VENDOR_ID, &reg_val);
	if (ret) {
		pr_err("failed get vendor id\n");
		ret = -ENODEV;
	} else {
		pr_info("vendor id:0x%x\n", reg_val);
		chip->vendor_id = reg_val;
	}
	gpio_direction_output(chip->reset_gpio, 1);

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static int z350_probe(struct i2c_client *client)
#else
static int z350_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct z350_chip *chip;
	struct device *cdev = &client->dev;
	struct device_node *np = client->dev.of_node;
	int ret, irq;

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	if (oem_pcba_charge_power() != CHARGE_POWER_33W) {
		pr_err("found 18W device, not init z350\n");
		return -ENODEV;
	}
#endif
	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA |
				I2C_FUNC_SMBUS_WORD_DATA)) {
		pr_err("smbus data not supported!\n");
		return -EIO;
	}

	chip = devm_kzalloc(cdev, sizeof(struct z350_chip), GFP_KERNEL);
	if (!chip) {
		pr_err("can't alloc z350_chip\n");
		return -ENOMEM;
	}

	chip->i2c = client;
	i2c_set_clientdata(client, chip);

	ret = z350_parse_dts(np, chip);
	if (ret < 0) {
		pr_err("parse dt failed\n");
		goto err_parse_dt;
	}

	ret = z350_check_vendor_id(chip);
	if (ret < 0) {
		pr_err("no dev, check vendor id failed\n");
		goto err_parse_dt;
	}

	mutex_init(&chip->mutex);
	mutex_init(&chip->qc3p_lock);

	/* try initialize device before request irq */
	//z350_try_initialization(chip);

	irq = gpio_to_irq(chip->irq_gpio);
	if (irq < 0) {
		pr_err("error gpio_to_irq returned %d\n", irq);
		goto err_request_irq;
	} else {
		pr_info("requesting IRQ %d\n", irq);
		client->irq = irq;
	}

	ret = request_threaded_irq(client->irq, NULL,
					z350_irq_thread,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"z350_irq",
					chip);
	if (ret) {
		pr_err("error failed to request IRQ\n");
		goto err_request_irq;
	}

	ret = enable_irq_wake(client->irq);
	if (ret < 0) {
		pr_err("failed to enable wakeup src %d\n", ret);
		goto err_enable_irq;
	}

	INIT_DELAYED_WORK(&chip->usb_detect_delayed_work, usb_detect_work_func);
	INIT_DELAYED_WORK(&chip->hvdcp_timeout_delayed_work, hvdcp_timeout_work_func);
	INIT_DELAYED_WORK(&chip->init_delayed_work, init_work_func);

	ret = z350_power_supply_init(chip, &chip->i2c->dev);
	if (ret) {
		pr_err("failed to register power supply\n");
		goto err_enable_irq;
	}

	//z350_hard_reset_once(chip);//reset to wake z350 when no plug in
	schedule_delayed_work(&chip->init_delayed_work, 100);

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	chip->nb.notifier_call = z350_psy_notifier_cb;
	power_supply_reg_notifier(&chip->nb);
	qc_logic_probe_done = true;
	g_chip = chip;
	chip->first_detect_dcp = true;
#endif

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	FULL_PRODUCT_DEVICE_INFO(ID_QC_LOGIC, "INJOINIC_Z350");
#endif

	pr_info("z350 chip finish probe\n");
	return 0;

err_enable_irq:
	free_irq(client->irq, NULL);
err_request_irq:
	mutex_destroy(&chip->mutex);
	mutex_destroy(&chip->qc3p_lock);
	i2c_set_clientdata(client, NULL);
err_parse_dt:
	gpio_free(chip->irq_gpio);
	gpio_free(chip->reset_gpio);
	devm_kfree(cdev, chip);
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	qc_logic_probe_done = false;
#endif
	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static void z350_remove(struct i2c_client *client)
#else
static int z350_remove(struct i2c_client *client)
#endif
{
	struct z350_chip *chip = i2c_get_clientdata(client);

	if (client->irq) {
		disable_irq_wake(client->irq);
		free_irq(client->irq, chip);
	}

	mutex_destroy(&chip->mutex);
	mutex_destroy(&chip->qc3p_lock);
	i2c_set_clientdata(client, NULL);

	devm_kfree(&chip->i2c->dev, chip);
#if !(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	return 0;
#endif
}

static void z350_shutdown(struct i2c_client *client)
{
	struct z350_chip *chip = i2c_get_clientdata(client);
	u16 reg_val;

	reg_val = 0; //disable hvdcp
	z350_write_byte(chip->i2c, ADDR_HVDCP_EN, (u8)reg_val);

	pr_info("z350_shutdown\n");
}

static const struct of_device_id z350_dt_match[] =
{
	{
		.compatible = "injoinic,z350",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, z350_dt_match);

static const struct i2c_device_id z350_id_table[] = {
	{
		.name = "z350",
	},
};

static SIMPLE_DEV_PM_OPS(z350_dev_pm, z350_suspend, z350_resume);

static struct i2c_driver z350_i2c_driver = {
	.driver = {
		.name = "z350",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(z350_dt_match),
		.pm = &z350_dev_pm,
	},
	.probe	= z350_probe,
	.remove   = z350_remove,
	.shutdown = z350_shutdown,
	.id_table = z350_id_table,
};

module_i2c_driver(z350_i2c_driver);

MODULE_DESCRIPTION("I2C bus driver for z350 USB Type-C");
MODULE_LICENSE("GPL v2");
