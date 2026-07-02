/*
* Copyright (c) TN Technologies Co., Ltd. 2022-2022. All rights reserved.
*
* Oem device info driver for all bsp module
*
* @Author oem
* @Since  2022/12/28
*/
#ifndef __DEV_INFO_H__
#define __DEV_INFO_H__

#include <linux/types.h>

#define LOG_TAG_TINNO  "[PRODUCT_DEV_INFO]:"
#define oemklog(fmt, args...)    printk(KERN_ERR LOG_TAG_TINNO fmt, ##args)

#define OEM_BUFF_SIZE_16       16
#define OEM_BUFF_SIZE_32       32
#define OEM_BUFF_SIZE_128      128

#define CHARGE_POWER_33W       33
#define CHARGE_POWER_18W       18

#define FULL_PRODUCT_DEVICE_CB(id, cb, args) \
	do { \
		full_product_device_info(id, NULL, cb, args); \
	} \
	while(0)

#define FULL_PRODUCT_DEVICE_INFO(id, info) \
	do { \
		full_product_device_info(id, info, NULL, NULL); \
	} \
	while(0)

typedef int (*FuncPtr)(char* buf, void *args);

typedef struct product_dev_info {
	char show[OEM_BUFF_SIZE_128];
	FuncPtr cb;
	void *args;
} OEM_DEV_INFO;

enum product_dev_info_attribute {
	ID_LCD = 0,
	ID_TP,
	ID_GYRO,
	ID_GSENSOR,
	ID_LSENSOR,
	ID_MSENSOR,
	ID_BATTERY,
	ID_MAIN1_CAM,
	ID_MAIN2_CAM,
	ID_SUB1_CAM,
	ID_SUB2_CAM,
	ID_FRONT1_CAM,
	ID_FRONT2_CAM,
	ID_FINGERPRINT,
	ID_NFC,
	ID_HALL,
	ID_PANEL,
	ID_CARDSLOT,
	ID_SAR_SENSOR,
	ID_AUDIOPA,
	ID_CHARGECHIP,
	ID_SDCARD,
	ID_SWITCH_CHARGER,
	ID_CHARGER_PUMP,
	ID_CC_LOGIC,
	ID_QC_LOGIC,
	ID_PSENSOR,
// add new..
	ID_MAX
};

extern int full_product_device_info(int id, const char *info, FuncPtr cb, void *args);

extern unsigned int oem_pcba_nfc_exist(void);
extern unsigned int oem_pcba_charge_power(void);
extern char *oem_battery_sn(void);
extern unsigned int oem_boot_mode(void);
extern unsigned int oem_lcd_res(void);
extern unsigned int oem_hw_sku(void);

#endif
