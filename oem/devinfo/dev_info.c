/*
* Copyright (c) TN Technologies Co., Ltd. 2022-2022. All rights reserved.
*
* Oem device info driver for all bsp module
*
* @Author oem
* @Since  2022/12/28
*/
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "dev_info.h"
//#include <linux/dev_info.h>

static unsigned int charge_power = 0;
static unsigned int nfc_exist = 0;
static unsigned int boot_mode = 0;
static unsigned int lcd_res = 0;
static unsigned int hw_sku = 0;
static char batterysn_buff[OEM_BUFF_SIZE_16] = {0};

module_param_named(chgpower, charge_power, int, 0644);
module_param_named(nfcexist, nfc_exist, int, 0644);
module_param_named(bootmode, boot_mode, int, 0644);
module_param_named(lcdres, lcd_res, int, 0644);
module_param_named(hwsku, hw_sku, int, 0644);
module_param_string(batterysn, batterysn_buff, OEM_BUFF_SIZE_16, 0644);

unsigned int oem_pcba_charge_power(void)
{
	return charge_power;
}
EXPORT_SYMBOL(oem_pcba_charge_power);

unsigned int oem_pcba_nfc_exist(void)
{
	return nfc_exist;
}
EXPORT_SYMBOL(oem_pcba_nfc_exist);

unsigned int oem_boot_mode(void)
{
	return boot_mode;
}
EXPORT_SYMBOL(oem_boot_mode);

unsigned int oem_lcd_res(void)
{
	return lcd_res;
}
EXPORT_SYMBOL(oem_lcd_res);

unsigned int oem_hw_sku(void)
{
	return hw_sku;
}
EXPORT_SYMBOL(oem_hw_sku);

char *oem_battery_sn(void)
{
	return batterysn_buff;
}
EXPORT_SYMBOL(oem_battery_sn);

/* /sys/devices/platform/$PRODUCT_DEVICE_INFO */
#define PRODUCT_DEVICE_INFO    "product-device-info"

static int dev_info_probe(struct platform_device *pdev);
static int dev_info_remove(struct platform_device *pdev);
static ssize_t store_product_dev_info(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_product_dev_info(struct device *dev, struct device_attribute *attr, char *buf);
static OEM_DEV_INFO prod_dev_array[ID_MAX];
static OEM_DEV_INFO *pi_p = prod_dev_array;

static struct platform_driver dev_info_driver = {
	.probe = dev_info_probe,
	.remove = dev_info_remove,
	.driver = {
		   .name = PRODUCT_DEVICE_INFO,
	},
};

static struct platform_device dev_info_device = {
	.name = PRODUCT_DEVICE_INFO,
	.id = -1,
};

#define PRODUCT_DEV_INFO_ATTR(_name)                         \
{                                       \
	.attr = { .name = #_name, .mode = S_IRUGO | S_IWUSR | S_IWGRP,},  \
	.show = show_product_dev_info,                  \
	.store = store_product_dev_info,                              \
}

static struct device_attribute product_dev_attr_array[] = {
	PRODUCT_DEV_INFO_ATTR(info_lcd),
	PRODUCT_DEV_INFO_ATTR(info_tp),
	PRODUCT_DEV_INFO_ATTR(info_gyro),
	PRODUCT_DEV_INFO_ATTR(info_gsensor),
	PRODUCT_DEV_INFO_ATTR(info_lsensor),
	PRODUCT_DEV_INFO_ATTR(info_msensor),
	PRODUCT_DEV_INFO_ATTR(info_battery),
	PRODUCT_DEV_INFO_ATTR(info_main1_camera),
	PRODUCT_DEV_INFO_ATTR(info_main2_camera),
	PRODUCT_DEV_INFO_ATTR(info_sub1_camera),
	PRODUCT_DEV_INFO_ATTR(info_sub2_camera),
	PRODUCT_DEV_INFO_ATTR(info_front1_camera),
	PRODUCT_DEV_INFO_ATTR(info_front2_camera),
	PRODUCT_DEV_INFO_ATTR(info_fingerprint),
	PRODUCT_DEV_INFO_ATTR(info_nfc),
	PRODUCT_DEV_INFO_ATTR(info_hall),
	PRODUCT_DEV_INFO_ATTR(info_panel),
	PRODUCT_DEV_INFO_ATTR(info_cardslot),
	PRODUCT_DEV_INFO_ATTR(info_sar),
	PRODUCT_DEV_INFO_ATTR(info_audiopa),
	PRODUCT_DEV_INFO_ATTR(info_chargerchip),
	PRODUCT_DEV_INFO_ATTR(info_sdcard),
	PRODUCT_DEV_INFO_ATTR(info_switch_charger),
	PRODUCT_DEV_INFO_ATTR(info_charger_pump),
	PRODUCT_DEV_INFO_ATTR(info_cc_logic),
	PRODUCT_DEV_INFO_ATTR(info_qc_logic),
	PRODUCT_DEV_INFO_ATTR(info_psensor),
// add new ...
};

/*
* int full_product_device_info(int id, const char *info, int (*cb)(char* buf, void *args), void *args);
*/
int full_product_device_info(int id, const char *info, FuncPtr cb, void *args)
{
	oemklog("%s: - [%d, %s, %pf]\n", __func__, id, info, cb);

	if (id >= 0 &&  id < ID_MAX) {
		memset(pi_p[id].show, 0, OEM_BUFF_SIZE_128);
		if (cb != NULL && pi_p[id].cb == NULL) {
			pi_p[id].cb = cb;
			pi_p[id].args = args;
		} else if (info != NULL) {
			strcpy(pi_p[id].show, info);
			pi_p[id].cb = NULL;
			pi_p[id].args = NULL;
		}
		return 0;
	}

	return -1;
}
EXPORT_SYMBOL(full_product_device_info);

static ssize_t store_product_dev_info(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t show_product_dev_info(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i = 0;
	char *show = NULL;
	const ptrdiff_t x = (attr - product_dev_attr_array);

	if (x >= ID_MAX) {
		BUG_ON(1);
	}

	show = pi_p[x].show;
	if (pi_p[x].cb != NULL) {
		pi_p[x].cb(show, pi_p[x].args);
	}

	oemklog("%s: - offset(%d): %s\n", __func__, (int)x, show);
	if (strlen(show) > 0) {
		i = sprintf(buf, "%s ", show);
	} else {
		oemklog("%s - offset(%d): NULL!\n", __func__, (int)x);
	}

	return i;
}

static int dev_info_probe(struct platform_device *pdev)
{
	int i, rc;

	pr_info( "[%s] charge power %d, nfc exist %d, boot mode %d, oem battery sn %s, lcd_res %d, hwsku %d\n",
		__func__, charge_power, nfc_exist, boot_mode, batterysn_buff, lcd_res, hw_sku);

	for (i = 0; i < ARRAY_SIZE(product_dev_attr_array); i++) {
		rc = device_create_file(&pdev->dev, &product_dev_attr_array[i]);
		if (rc) {
			oemklog( "%s, create_attrs_failed:%d,%d\n", __func__, i, rc);
		}
	}

	return 0;
}

static int dev_info_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(product_dev_attr_array); i++) {
		device_remove_file(&pdev->dev, &product_dev_attr_array[i]);
	}

	return 0;
}

static int __init dev_info_drv_init(void)
{
	if (platform_device_register(&dev_info_device) != 0) {
		oemklog( "device_register fail!.\n");
		return -1;
	}

	if (platform_driver_register(&dev_info_driver) != 0) {
		oemklog( "driver_register fail!.\n");
		return -1;
	}

	return 0;
}

static void __exit dev_info_drv_exit(void)
{
	platform_driver_unregister(&dev_info_driver);
	platform_device_unregister(&dev_info_device);
}

module_init(dev_info_drv_init);
module_exit(dev_info_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ruirui.cao <ruirui.cao@tinno.com>");
MODULE_DESCRIPTION("dev info driver");
MODULE_LICENSE("GPL");
