/*
* Copyright (C) 2019 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include "flashlight.h"
#include "flashlight-dt.h"
#include "flashlight-core.h"
#include <linux/pinctrl/consumer.h>



/* device tree should be defined in flashlight-dt.h */
#ifndef AW36515_DTNAME
#define AW36515_DTNAME "mediatek,flashlights_aw36515"
#endif
#ifndef AW36515_DTNAME_I2C
#define AW36515_DTNAME_I2C "mediatek,strobe_main"
#endif
#define AW36515_NAME "flashlights-aw36515"

#define AW36515_DRIVER_VERSION "V1.0.2"

#define AW36515_REG_BOOST_CONFIG     (0x07)
#define AW36515_BIT_SOFT_RST_MASK    (~(1<<7))
#define AW36515_BIT_SOFT_RST_ENABLE  (1<<7)
#define AW36515_BIT_SOFT_RST_DISABLE (0<<7)

/* define registers */
#define AW36515_REG_ENABLE           (0x01)
#define AW36515_MASK_ENABLE_LED1     (0x03)
#define AW36515_MASK_ENABLE_LED2     (0x02)
#define AW36515_DISABLE              (0x00)
#define AW36515_ENABLE_LED1          (0x03)
#define AW36515_ENABLE_LED1_TORCH    (0x0B)
#define AW36515_ENABLE_LED1_FLASH    (0x0F)
#define AW36515_ENABLE_LED2          (0x02)
#define AW36515_ENABLE_LED2_TORCH    (0x0A)
#define AW36515_ENABLE_LED2_FLASH    (0x0E)
#define AW36515_DEVICES_ID           (0x0C)

#define AW36515_REG_TORCH_LEVEL_LED1 (0x05)
#define AW36515_REG_FLASH_LEVEL_LED1 (0x03)
#define AW36515_REG_TORCH_LEVEL_LED2 (0x06)
#define AW36515_REG_FLASH_LEVEL_LED2 (0x04)

#define AW36515_REG_TIMING_CONF      (0x08)
#define AW36515_TORCH_RAMP_TIME      (0x10)
#define AW36515_FLASH_TIMEOUT        (0x09)
#define AW36515_CHIP_STANDBY         (0x80)

#define AW36515_REG_FLAG1            (0x0A)
#define AW36515_REG_FLAG2            (0x0B)

/* define channel, level */
#define AW36515_CHANNEL_NUM          1
#define AW36515_CHANNEL_CH1          0
#define AW36515_LEVEL_NUM            26
#define AW36515_LEVEL_TORCH          7

#define AW36515_HW_TIMEOUT 600 /* ms */


#define AW_I2C_RETRIES			4
#define AW_I2C_RETRY_DELAY		2

/* define mutex and work queue */
static DEFINE_MUTEX(aw36515_mutex);

struct i2c_client *aw36515_flashlight_client;

/* define usage count */
static int use_count;

/* define i2c */
static struct i2c_client *aw36515_i2c_client;


/* define pinctrl */
#define AW36515_PINCTRL_PIN_HWEN 0
#define AW36515_PINCTRL_PINSTATE_LOW 0
#define AW36515_PINCTRL_PINSTATE_HIGH 1
#define AW36515_PINCTRL_STATE_HWEN_HIGH "hwen-high"
#define AW36515_PINCTRL_STATE_HWEN_LOW  "hwen-low"

/* charger status */
#define FLASHLIGHT_CHARGER_NOT_READY 0
#define FLASHLIGHT_CHARGER_READY     1


struct pinctrl *aw36515_hwen_pinctrl;
struct pinctrl_state *aw36515_hwen_high;
struct pinctrl_state *aw36515_hwen_low;


static int aw36515_get_flag(int num)
{
	if (num == 1)
		return i2c_smbus_read_byte_data(aw36515_i2c_client, AW36515_REG_FLAG1);
	else if (num == 2)
		return i2c_smbus_read_byte_data(aw36515_i2c_client, AW36515_REG_FLAG2);

	pr_info("Error num\n");
	return 0;
}


static int aw36515_pinctrl_init(struct i2c_client *client)
{
	int ret = 0;

	/* get pinctrl */
	aw36515_hwen_pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR(aw36515_hwen_pinctrl)) {
		pr_err("Failed to get flashlight pinctrl.\n");
		ret = PTR_ERR(aw36515_hwen_pinctrl);
		return ret;
	}

	/* Flashlight HWEN pin initialization */
	aw36515_hwen_high = pinctrl_lookup_state(
			aw36515_hwen_pinctrl,
			AW36515_PINCTRL_STATE_HWEN_HIGH);
	if (IS_ERR(aw36515_hwen_high)) {
		pr_err("Failed to init (%s)\n",
			AW36515_PINCTRL_STATE_HWEN_HIGH);
		ret = PTR_ERR(aw36515_hwen_high);
	}
	aw36515_hwen_low = pinctrl_lookup_state(
			aw36515_hwen_pinctrl,
			AW36515_PINCTRL_STATE_HWEN_LOW);
	if (IS_ERR(aw36515_hwen_low)) {
		pr_err("Failed to init (%s)\n", AW36515_PINCTRL_STATE_HWEN_LOW);
		ret = PTR_ERR(aw36515_hwen_low);
	}

	return ret;
}


static int aw36515_pinctrl_set(int pin, int state)
{
	int ret = 0;

	if (IS_ERR(aw36515_hwen_pinctrl)) {
		pr_info("pinctrl is not available\n");
		return -1;
	}

	switch (pin) {
	case AW36515_PINCTRL_PIN_HWEN:
		if (state == AW36515_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(aw36515_hwen_low))
			pinctrl_select_state(aw36515_hwen_pinctrl,
					aw36515_hwen_low);
		else if (state == AW36515_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(aw36515_hwen_high))
			pinctrl_select_state(aw36515_hwen_pinctrl,
					aw36515_hwen_high);
		else
			pr_info("set err, pin(%d) state(%d)\n", pin, state);
		break;
	default:
		pr_info("set err, pin(%d) state(%d)\n", pin, state);
		break;
	}

	return ret;
}


/* platform data
* torch_pin_enable: TX1/TORCH pin isa hardware TORCH enable
* pam_sync_pin_enable: TX2 Mode The ENVM/TX2 is a PAM Sync. on input
* thermal_comp_mode_enable: LEDI/NTC pin in Thermal Comparator Mode
* strobe_pin_disable: STROBE Input disabled
* vout_mode_enable: Voltage Out Mode enable
*/
struct aw36515_platform_data {
	u8 torch_pin_enable;
	u8 pam_sync_pin_enable;
	u8 thermal_comp_mode_enable;
	u8 strobe_pin_disable;
	u8 vout_mode_enable;
};

/* aw36515 chip data */
struct aw36515_chip_data {
	struct i2c_client *client;
	struct aw36515_platform_data *pdata;
	struct mutex lock;
	u8 last_flag;
	u8 no_pdata;
};

/******************************************************************************
 * aw36515 operations
 *****************************************************************************/
static const int aw36515_current[AW36515_LEVEL_NUM] = {
	22,  53,  84,  115,  146, 177, 208, 239, 270, 302,
	333, 364, 395, 426,  457, 488, 519, 551, 582, 613,
	644, 675, 706, 737, 768, 800
};

/*AW36515 TORCH current(mA)~(Brightness Code*1.96mA)+0.98mA*/
static const unsigned char aw36515_torch_level[AW36515_LEVEL_NUM] = {
	0x01, 0x05, 0x0a, 0x0f, 0x14, 0x19, 0x1e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*AW36515 FLAsH current(mA)=(Brightness Code*7.83mA)+3.91mA*/
static const unsigned char aw36515_flash_level[AW36515_LEVEL_NUM] = {
	0x02, 0x06, 0x0a, 0x0e, 0x12, 0x16, 0x1a, 0x1e, 0x22, 0x26,
	0x2a, 0x2e, 0x32, 0x35, 0x39, 0x3d, 0x41, 0x45, 0x49, 0x4d,
	0x51, 0x55, 0x59, 0x5d, 0x61, 0x65
};

/*OCP81375 TORCH current (mA)~(Brightness Codex3.90mA)+3.90mA*/
static const unsigned char ocp81375_torch_level[AW36515_LEVEL_NUM] = {
	0x00, 0x03, 0x06, 0x09, 0x0d, 0x10, 0x13, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*OCP81375 FLASH current(mA)~(Brightness Codex15.63mA)+15.63mA*/
static const unsigned char ocp81375_flash_level[AW36515_LEVEL_NUM] = {
	0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12,
	0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e, 0x20, 0x22, 0x24, 0x26,
	0x28, 0x2a, 0x2c, 0x2e, 0x30, 0x32
};

static volatile unsigned char aw36515_reg_enable;

static volatile int aw36515_level_ch1 = -1;

static int aw36515_is_torch(int level)
{
	pr_info("%s aw36515_flashlight level is %d\n", __func__, level);
	if (level >= AW36515_LEVEL_TORCH)
		return -1;

	return 0;
}

static int aw36515_verify_level(int level)
{

	if (level < 0)
		level = 0;
	else if (level >= AW36515_LEVEL_NUM)
		level = AW36515_LEVEL_NUM - 1;

	return level;
}

/* i2c wrapper function */
static int aw36515_i2c_write(struct i2c_client *client, unsigned char reg, unsigned char val)
{
	int ret;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (ret < 0) {
			pr_info("%s: i2c_write addr=0x%02X, data=0x%02X, cnt=%d, error=%d\n",
				   __func__, reg, val, cnt, ret);
		} else {
			pr_info("%s: i2c_write addr=0x%02X, data=0x%02X, cnt=%d, ret=%d\n",
				   __func__, reg, val, cnt, ret);
			break;
		}
		cnt++;
		msleep(AW_I2C_RETRY_DELAY);
	}

	return ret;
}

static int aw36515_i2c_read(struct i2c_client *client, unsigned char reg, unsigned char *val)
{
	int ret;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret < 0) {
			pr_info("%s: i2c_read addr=0x%02X, cnt=%d, error=%d\n",
				   __func__, reg, cnt, ret);
		} else {
			*val = ret;
			pr_info("%s: i2c_read addr=0x%02X, cnt=%d, ret=%d\n",
				   __func__, reg, cnt, ret);
			break;
		}
		cnt++;
		msleep(AW_I2C_RETRY_DELAY);
	}

	return ret;
}

static void aw36515_soft_reset(void)
{
	unsigned char reg_val;

	aw36515_i2c_read(aw36515_i2c_client, AW36515_REG_BOOST_CONFIG, &reg_val);
	reg_val &= AW36515_BIT_SOFT_RST_MASK;
	reg_val |= AW36515_BIT_SOFT_RST_ENABLE;
	aw36515_i2c_write(aw36515_i2c_client, AW36515_REG_BOOST_CONFIG, reg_val);
	msleep(5);
}

/* flashlight led1 enable function */
static int aw36515_enable_ch1(void)
{
	unsigned char reg, val;

	reg = AW36515_REG_ENABLE;
	if (!aw36515_is_torch(aw36515_level_ch1)) {
		/* torch mode */
		aw36515_reg_enable |= AW36515_ENABLE_LED1_TORCH;
	} else {
		/* flash mode */
		aw36515_reg_enable |= AW36515_ENABLE_LED1_FLASH;
	}

	val = aw36515_reg_enable;

	return aw36515_i2c_write(aw36515_i2c_client, reg, val);
}

static int aw36515_enable(int channel)
{

	if (channel == AW36515_CHANNEL_CH1)
		aw36515_enable_ch1();
	else {
		pr_err("Error channel\n");
		return -1;
	}

	return 0;
}

/* flashlight led1 disable function */
static int aw36515_disable_ch1(void)
{
	unsigned char reg, val;

	reg = AW36515_REG_ENABLE;
	if (!aw36515_is_torch(aw36515_level_ch1)) {
		/* torch mode */
		aw36515_reg_enable &= (~AW36515_ENABLE_LED1);
	} else {
		/* flash mode */
		aw36515_reg_enable &= (~AW36515_ENABLE_LED1_FLASH);
	}
	val = aw36515_reg_enable;

	return aw36515_i2c_write(aw36515_i2c_client, reg, val);
}

static int aw36515_disable(int channel)
{

	if (channel == AW36515_CHANNEL_CH1)
		aw36515_disable_ch1();
	else {
		pr_err("Error channel\n");
		return -1;
	}

	return 0;
}

/* set flashlight1 level */
static int aw36515_set_level_ch1(int level)
{
	int ret;
	unsigned char reg1, reg2, val = 0;
	unsigned char device_id;
	int dev_id;
	dev_id = aw36515_i2c_read(aw36515_i2c_client, AW36515_DEVICES_ID, &device_id);
	pr_info("%s AW36515_DEVICES_ID %d\n", __func__, dev_id);

	level = aw36515_verify_level(level);

	/* set torch brightness level */
	reg1 = AW36515_REG_TORCH_LEVEL_LED1;
	reg2 = AW36515_REG_TORCH_LEVEL_LED2;
	/*select flash ic by devices id*/
	if (dev_id == 58) {
		val = ocp81375_torch_level[level];
        pr_info("%s this is ocp81375 torch value is 0x%02X\n", __func__,val);
    } else {
	    val = aw36515_torch_level[level];
        pr_info("%s this is aw36515 torch value is 0x%02X\n", __func__,val);
	}
	ret = aw36515_i2c_write(aw36515_i2c_client, reg1, val);
	ret = aw36515_i2c_write(aw36515_i2c_client, reg2, val);
	
	aw36515_level_ch1 = level;

	/* set flash brightness level */
	reg1 = AW36515_REG_FLASH_LEVEL_LED1;
	reg2 = AW36515_REG_FLASH_LEVEL_LED2;
	/*select flash ic by devices id*/
	if (dev_id == 58) {
		val = ocp81375_flash_level[level];
        pr_info("%s this is ocp81375 flash value is 0x%02X\n", __func__,val);
    } else {
	    val = aw36515_flash_level[level];
        pr_info("%s this is aw36515 flash value is 0x%02X\n", __func__,val);
	}
	ret = aw36515_i2c_write(aw36515_i2c_client, reg1, val);
	ret = aw36515_i2c_write(aw36515_i2c_client, reg2, val);
	return ret;
}

static int aw36515_set_level(int channel, int level)
{
	if (channel == AW36515_CHANNEL_CH1)
		aw36515_set_level_ch1(level);
	else {
		pr_err("Error channel\n");
		return -1;
	}

	return 0;
}

/* flashlight init */
int aw36515_init(void)
{
	int ret;
	unsigned char reg, val;

	usleep_range(2000, 2500);

	/* clear enable register */
	reg = AW36515_REG_ENABLE;
	val = AW36515_DISABLE;
	aw36515_pinctrl_set(AW36515_PINCTRL_PIN_HWEN, AW36515_PINCTRL_PINSTATE_HIGH);
	mdelay(2);
	/* soft rst */
	aw36515_soft_reset();
	mdelay(2);
	ret = aw36515_i2c_write(aw36515_i2c_client, reg, val);

	aw36515_reg_enable = val;

	/* set torch current ramp time and flash timeout */
	reg = AW36515_REG_TIMING_CONF;
	val = AW36515_TORCH_RAMP_TIME | AW36515_FLASH_TIMEOUT;
	ret = aw36515_i2c_write(aw36515_i2c_client, reg, val);

	return ret;
}

/* flashlight uninit */
int aw36515_uninit(void)
{
	aw36515_disable(AW36515_CHANNEL_CH1);
	aw36515_pinctrl_set(AW36515_PINCTRL_PIN_HWEN, AW36515_PINCTRL_PINSTATE_LOW);
	return 0;
}


/******************************************************************************
 * Flashlight operations
 *****************************************************************************/
static int aw36515_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;

	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	/* verify channel */
	if (channel < 0 || channel >= AW36515_CHANNEL_NUM) {
		pr_err("Failed with error channel\n");
		return -EINVAL;
	}

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		pr_info("FLASH_IOC_SET_TIME_OUT_TIME_MS(%d): %d\n",
				channel, (int)fl_arg->arg);
		break;

	case FLASH_IOC_SET_DUTY:
		pr_info("FLASH_IOC_SET_DUTY(%d): %d\n",
				channel, (int)fl_arg->arg);
		aw36515_set_level(channel, fl_arg->arg);
		break;

	case FLASH_IOC_SET_ONOFF:
		pr_info("FLASH_IOC_SET_ONOFF(%d): %d\n",
				channel, (int)fl_arg->arg);
		if (fl_arg->arg == 1) {
			aw36515_enable(channel);
		} else {
			aw36515_disable(channel);
		}
		break;

	case FLASH_IOC_GET_DUTY_NUMBER:
		pr_info("FLASH_IOC_GET_DUTY_NUMBER(%d)\n", channel);
		fl_arg->arg = AW36515_LEVEL_NUM;
		break;

	case FLASH_IOC_GET_MAX_TORCH_DUTY:
		pr_info("FLASH_IOC_GET_MAX_TORCH_DUTY(%d)\n", channel);
		fl_arg->arg = AW36515_LEVEL_TORCH - 1;
		break;

	case FLASH_IOC_GET_DUTY_CURRENT:
		fl_arg->arg = aw36515_verify_level(fl_arg->arg);
		pr_info("FLASH_IOC_GET_DUTY_CURRENT(%d): %d\n",
				channel, (int)fl_arg->arg);
		fl_arg->arg = aw36515_current[fl_arg->arg];
		break;

	case FLASH_IOC_GET_HW_TIMEOUT:
		pr_info("FLASH_IOC_GET_HW_TIMEOUT(%d)\n", channel);
		fl_arg->arg = AW36515_HW_TIMEOUT;
		break;

	case FLASH_IOC_IS_CHARGER_READY:
  		pr_info("FLASH_IOC_IS_CHARGER_READY(%d)\n", channel);
 		fl_arg->arg = FLASHLIGHT_CHARGER_READY;
  		pr_info("FLASH_IOC_IS_CHARGER_READY(%d)\n", fl_arg->arg);
  		break;

	case FLASH_IOC_GET_HW_FAULT:
		pr_info("FLASH_IOC_GET_HW_FAULT(%d)\n", channel);
		fl_arg->arg = aw36515_get_flag(1);
		break;

	case FLASH_IOC_GET_HW_FAULT2:
		pr_info("FLASH_IOC_GET_HW_FAULT2(%d)\n", channel);
		fl_arg->arg = aw36515_get_flag(2);
		break;
	case FLASH_IOC_GET_REGISTER:
		aw36515_i2c_read(aw36515_i2c_client, fl_arg->addr, &fl_arg->data);
		break;
	case FLASH_IOC_SET_REGISTER:
		aw36515_i2c_write(aw36515_i2c_client, fl_arg->addr, fl_arg->data);
		break;
	default:
		pr_info("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

static int aw36515_open(void)
{
	/* Actual behavior move to set driver function */
	/* since power saving issue */
	return 0;
}

static int aw36515_release(void)
{
	/* uninit chip and clear usage count */
	mutex_lock(&aw36515_mutex);
	use_count--;
	if (!use_count)
		aw36515_uninit();
	if (use_count < 0)
		use_count = 0;
	mutex_unlock(&aw36515_mutex);

	pr_info("Release: %d\n", use_count);

	return 0;
}

static int aw36515_set_driver(int set)
{
	/* init chip and set usage count */
	mutex_lock(&aw36515_mutex);
	if (!use_count)
		aw36515_init();
	use_count++;
	mutex_unlock(&aw36515_mutex);

	pr_info("Set driver: %d\n", use_count);

	return 0;
}

static ssize_t aw36515_strobe_store(struct flashlight_arg arg)
{
	aw36515_set_driver(1);
	aw36515_set_level(arg.ct, arg.level);
	aw36515_enable(arg.ct);
	msleep(arg.dur);
	aw36515_disable(arg.ct);
	aw36515_set_driver(0);

	return 0;
}

static struct flashlight_operations aw36515_ops = {
	aw36515_open,
	aw36515_release,
	aw36515_ioctl,
	aw36515_strobe_store,
	aw36515_set_driver
};


/******************************************************************************
 * I2C device and driver
 *****************************************************************************/
static int aw36515_chip_init(struct aw36515_chip_data *chip)
{
	/* NOTE: Chip initialication move to
	*"set driver" operation for power saving issue.
	* aw36515_init();
	*/

	return 0;
}

/***************************************************************************/
/*AW36515 Debug file */
/***************************************************************************/
static ssize_t
aw36515_get_reg(struct device *cd, struct device_attribute *attr, char *buf)
{
	unsigned char reg_val;
	unsigned char i;
	ssize_t len = 0;

	for (i = 0; i < 0x0E; i++) {
		aw36515_i2c_read(aw36515_i2c_client, i ,&reg_val);
		len += snprintf(buf+len, PAGE_SIZE-len,
			"reg0x%2X = 0x%2X\n", i, reg_val);
	}
	len += snprintf(buf+len, PAGE_SIZE-len, "\r\n");
	return len;
}

static ssize_t aw36515_set_reg(struct device *cd,
		struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int databuf[2];

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2)
		aw36515_i2c_write(aw36515_i2c_client, databuf[0], databuf[1]);
	return len;
}

static DEVICE_ATTR(reg, 0660, aw36515_get_reg, aw36515_set_reg);

static int aw36515_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);

	err = device_create_file(dev, &dev_attr_reg);

	return err;
}

static int
aw36515_i2c_probe(struct i2c_client *client)
{
	struct aw36515_chip_data *chip;
	struct aw36515_platform_data *pdata = client->dev.platform_data;
	int err, rval;

	pr_info("%s Probe start.\n", __func__);
	/* check i2c */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("Failed to check i2c functionality.\n");
		err = -ENODEV;
		goto err_out;
	}

	/* init chip private data */
	chip = kzalloc(sizeof(struct aw36515_chip_data), GFP_KERNEL);
	if (!chip) {
		err = -ENOMEM;
		goto err_out;
	}
	chip->client = client;

	/* init platform data */
	if (!pdata) {
		pr_err("Platform data does not exist\n");
		pdata =
		kzalloc(sizeof(struct aw36515_platform_data), GFP_KERNEL);
		if (!pdata) {
			err = -ENOMEM;
			goto err_init_pdata;
		}
		chip->no_pdata = 1;
	}
	rval = aw36515_pinctrl_init(client);
	if (rval < 0){
		pr_err("Failed to aw36515_pinctrl_init\n");
		return rval;
	}

	chip->pdata = pdata;
	i2c_set_clientdata(client, chip);
	aw36515_i2c_client = client;

	/* init mutex and spinlock */
	mutex_init(&chip->lock);

	/* init chip hw */
	aw36515_chip_init(chip);

	/* register flashlight operations */
	if (flashlight_dev_register(AW36515_NAME, &aw36515_ops)) {
		pr_err("Failed to register flashlight device.\n");
		err = -EFAULT;
		goto err_free;
	}

	/* clear usage count */
	use_count = 0;

	aw36515_create_sysfs(client);

	pr_info("%s Probe done.\n", __func__);
	return 0;

err_free:
	kfree(chip->pdata);
err_init_pdata:
	i2c_set_clientdata(client, NULL);
	kfree(chip);
err_out:
	return err;
}

static void aw36515_i2c_remove(struct i2c_client *client)
{
	struct aw36515_chip_data *chip = i2c_get_clientdata(client);

	pr_info("Remove start.\n");

	/* unregister flashlight operations */
	flashlight_dev_unregister(AW36515_NAME);

	/* free resource */
	if (chip->no_pdata)
		kfree(chip->pdata);
	kfree(chip);

	pr_info("Remove done.\n");

}

static void aw36515_i2c_shutdown(struct i2c_client *client)
{
	pr_info("aw36515 shutdown start.\n");

	aw36515_i2c_write(aw36515_i2c_client, AW36515_REG_ENABLE,
						AW36515_CHIP_STANDBY);

	pr_info("aw36515 shutdown done.\n");
}


static const struct i2c_device_id aw36515_i2c_id[] = {
	{AW36515_NAME, 0},
	{}
};

#ifdef CONFIG_OF
static const struct of_device_id aw36515_i2c_of_match[] = {
	{.compatible = AW36515_DTNAME_I2C},
	{},
};
#endif

static struct i2c_driver aw36515_i2c_driver = {
	.driver = {
		   .name = AW36515_NAME,
#ifdef CONFIG_OF
		   .of_match_table = aw36515_i2c_of_match,
#endif
		   },
	.probe = aw36515_i2c_probe,
	.remove = aw36515_i2c_remove,
	.shutdown = aw36515_i2c_shutdown,
	.id_table = aw36515_i2c_id,
};

module_i2c_driver(aw36515_i2c_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joseph <zhangzetao@awinic.com.cn>");
MODULE_DESCRIPTION("AW Flashlight AW36515 Driver");

