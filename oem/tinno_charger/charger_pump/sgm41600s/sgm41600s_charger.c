// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2022 SG Micro TechnologyCo., Ltd.
*/

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/regmap.h>
#include <linux/version.h>

#define CONFIG_MTK_CHARGER_V5P10 1
#define CONFIG_MTK_CLASS 1

#ifdef CONFIG_MTK_CLASS
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include "mtk_battery.h"
#include "mtk_charger.h"
#include "charger_class.h"
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
#include <mt-plat/v1/mtk_battery.h>
#include <mt-plat/v1/charger_class.h>
#include <mt-plat/v1/mtk_charger.h>
#else
#include <mt-plat/mtk_battery.h>
#include <mt-plat/charger_class.h>
#include <mt-plat/mtk_charger.h>
#endif /* LINUX_VERSION_CODE */
#endif /*CONFIG_MTK_CLASS*/


#ifdef CONFIG_SGM_DVCHG_CLASS
#include "dvchg_class.h"
#endif /*CONFIG_SGM_DVCHG_CLASS*/

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include <dev_info.h>
#endif /* CONFIG_OEM_DEVINFO */

#define SGM41600S_DRV_VERSION		"V1.0"

#define SGM_KERNEL_DEBUG
#ifdef SGM_KERNEL_DEBUG
#define SGM_INFO(fmt, args...) printk("[SGMICRO] %s() called: line=%d." fmt, __func__, __LINE__, ##args)
#define SGM_DBG(fmt, args...)  printk("[SGMICRO] %s() called: line=%d." fmt, __func__, __LINE__, ##args)
#define SGM_ERR(fmt, args...)  printk("[SGMICRO] %s() called: line=%d." fmt, __func__, __LINE__, ##args)
#else
#define SGM_INFO
#define SGM_DBG
#define SGM_ERR
#endif

enum {
	SGM41600S_STANDALONG = 0,
	SGM41600S_MASTER,
	SGM41600S_SLAVE,
};

static const char* sgm41600_psy_name[] = {
	[SGM41600S_STANDALONG] = "cp-standalone",
	[SGM41600S_MASTER] = "cp-master",
	[SGM41600S_SLAVE] = "cp-slave",
};

static const char* sgm41600_irq_name[] = {
	[SGM41600S_STANDALONG] = "sgm41600-standalone-irq",
	[SGM41600S_MASTER] = "sgm41600-master-irq",
	[SGM41600S_SLAVE] = "sgm41600-slave-irq",
};

static int sgm41600_mode_data[] = {
	[SGM41600S_STANDALONG] = SGM41600S_STANDALONG,
	[SGM41600S_MASTER] = SGM41600S_MASTER,
	[SGM41600S_SLAVE] = SGM41600S_SLAVE,
};

enum {
	ADC_VBUS = 0,
	ADC_IBUS,
	ADC_VBAT,
	ADC_IBAT,
	ADC_VOUT,
	ADC_TDIE,
	ADC_MAX_NUM,
} SGM41600_ADC_CH;

static const u32 sgm41600_adc_accuracy_tbl[ADC_MAX_NUM] = {
	150000,	/* VBUS */
	35000,	/* IBUS */
	35000,	/* VBAT */
	200000,	/* IBAT */
	20000,	/* VOUT */
	4,		/* TDIE */
};

enum sgm41600_notify {
	SGM41600S_NOTIFY_OTHER = 0,
	SGM41600S_NOTIFY_IBUSOCP,
	SGM41600S_NOTIFY_VBUSOVP,
	SGM41600S_NOTIFY_IBATOCP,
	SGM41600S_NOTIFY_VBATOVP,
	SGM41600S_NOTIFY_VOUTOVP,
	SGM41600S_NOTIFY_TDIE,
	SGM41600S_NOTIFY_VAC_OVP,
};

enum sgm41600_error_stata {
	ERROR_VBUS_HIGH = 0,
	ERROR_VBUS_LOW,
	ERROR_VBUS_OVP,
	ERROR_IBUS_OCP,
	ERROR_VBAT_OVP,
	ERROR_IBAT_OCP,
};

struct flag_bit {
	int notify;
	int mask;
	char *name;
};

struct intr_flag {
	int reg;
	int len;
	struct flag_bit bit[8];
};

static struct intr_flag cp_intr_flag[] = {
	{ .reg = 0x0D, .len = 8, .bit = {
			{.mask = BIT(0), .name = "ibus ucp fall flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(1), .name = "ibus ucp timeout flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(2), .name = "ibus ocp flag", .notify = SGM41600S_NOTIFY_IBUSOCP},
			{.mask = BIT(3), .name = "bus ovp flag", .notify = SGM41600S_NOTIFY_VBUSOVP},
			{.mask = BIT(4), .name = "dvrop ovp flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(5), .name = "vbus Pull-Down flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(6), .name = "vac Pull-Down flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(7), .name = "vac ovp flag", .notify = SGM41600S_NOTIFY_VAC_OVP},
		},
	},
	{ .reg = 0x0F, .len = 8, .bit = {
    		{.mask = BIT(0), .name = "peack ocp flag", .notify = SGM41600S_NOTIFY_OTHER},
    		{.mask = BIT(1), .name = "vbus high flag", .notify = SGM41600S_NOTIFY_OTHER},
    		{.mask = BIT(2), .name = "vbus low flag", .notify = SGM41600S_NOTIFY_OTHER},
    		{.mask = BIT(3), .name = "tdie otp flag", .notify = SGM41600S_NOTIFY_TDIE},
    		{.mask = BIT(4), .name = "ibat reg flag", .notify = SGM41600S_NOTIFY_OTHER,},
    		{.mask = BIT(5), .name = "Vbat reg flag", .notify = SGM41600S_NOTIFY_OTHER},
    		{.mask = BIT(6), .name = "ibat ocp flag", .notify = SGM41600S_NOTIFY_IBATOCP},
			{.mask = BIT(7), .name = "vbat ovp flag", .notify = SGM41600S_NOTIFY_VBATOVP},
		},
	},
	{ .reg = 0x11, .len = 8, .bit = {
			{.mask = BIT(7), .name = "vbus insert flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(6), .name = "vbat insert flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(5), .name = "wdt timeout flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(4), .name = "vac insert flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(3), .name = "bus absent fault flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(2), .name = "vout ovp flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(1), .name = "adc done flag", .notify = SGM41600S_NOTIFY_OTHER},
			{.mask = BIT(0), .name = "pin Diagosis Fail flag", .notify = SGM41600S_NOTIFY_OTHER},
		},
	},
};

/************************************************************************/
#define SGM41600S_DEVICE_ID				0x12
#define SGM41600S_REGMAX				0xF4
#define SGM41600S_REG_VALID				0x28
#define SGM41600S_REG51					0x51
#define SGM41600S_REG5E					0x5E
#define SGM41600S_REG14					0x14
#define SGM41600S_REG0D					0x0D
#define SGM41600S_REG0F					0x0F
#define SGM41600S_REG11					0x11

/******************* define ********************/
#define SGM41600_BUS_OVP_MAX_uV       	14000000
#define SGM41600_BUS_OVP_MIN_uV       	4000000
#define SGM41600_BUS_OVP_STEP_uV      	100000
#define SGM41600_BUS_OVP_DEF_uV      	11500000

#define SGM41600_IBUS_OCP_MAX_uA       	5700000
#define SGM41600_IBUS_OCP_MIN_uA       	1200000
#define SGM41600_IBUS_OCP_STEP_uA      	300000
#define SGM41600_IBUS_OCP_DEF_uA      	3000000


#define SGM41600_BAT_OVP_MAX_uV       	5000000
#define SGM41600_BAT_OVP_MIN_uV       	4000000
#define SGM41600_BAT_OVP_STEP_uV      	25000
#define SGM41600_BAT_OVP_DEF_uV		  	4350000

#define SGM41600_IBAT_OCP_MAX_uA       	9300000
#define SGM41600_IBAT_OCP_MIN_uA       	3000000
#define SGM41600_IBAT_OCP_STEP_uA      	100000
#define SGM41600_IBAT_OCP_DEF_uA	   	8200000

#define SGM41600_DP_DAC	     			GENMASK(7, 5)
#define SGM41600_DM_DAC	     			GENMASK(4, 2)
#define SGM41600_EN_HVDCP	 			BIT(1)

#define SGM41600_CHARGE_MODE_OFF        0
#define SGM41600_CHARGE_MODE_BYPASS     1
#define SGM41600_CHARGE_MODE_DIV2		2

enum sgm41600_reg_range {
	SGM41600S_VBAT_OVP,
	SGM41600S_IBAT_OCP,
	SGM41600S_VBUS_OVP,
	SGM41600S_IBUS_OCP,
	SGM41600S_IBUS_OCP_BYPASS,
};

struct reg_range {
	u32 min;
	u32 max;
	u32 step;
	u32 offset;
	const u32 *table;
	u16 num_table;
	bool round_up;
};

#define SGM41600S_CHG_RANGE(_min, _max, _step, _offset, _ru) \
{ \
	.min = _min, \
	.max = _max, \
	.step = _step, \
	.offset = _offset, \
	.round_up = _ru, \
}

#define SGM41600S_CHG_RANGE_T(_table, _ru) \
	{ .table = _table, .num_table = ARRAY_SIZE(_table), .round_up = _ru, }


static const struct reg_range sgm41600_reg_range[] = {
	[SGM41600S_VBAT_OVP]        = SGM41600S_CHG_RANGE(4000, 5000, 25, 4000, false),
	[SGM41600S_IBAT_OCP]        = SGM41600S_CHG_RANGE(3000, 9300, 100, 3000, false),
	[SGM41600S_VBUS_OVP]        = SGM41600S_CHG_RANGE(4000, 14000, 100, 4000, false),
	[SGM41600S_IBUS_OCP]        = SGM41600S_CHG_RANGE(1500, 4600, 100, 1500, false),
	[SGM41600S_IBUS_OCP_BYPASS]    = SGM41600S_CHG_RANGE(2500, 5600, 100, 2500, false),
};


enum sgm41600_fields {
	REG_RST,CHG_MODE,WDT_DIS,WDT_TIMER, //0x00
	FSW_SET,FSW_SHIFT,PEAK_OCP_SET,
	PIN_DIAG_EN,FSW_DITHER_EN,VBUS_LO_EN,VBUS_HI_EN,VBUS_LO,VBUS_HI,
	SGM_DEVICE_ID,
	OVPGATE_SET,OVPGATE_ISET,OVPGATE_EN,AC_OVP_EN,AC_OVP,
	AC_PDN_EN,BUS_PDN_EN,VDRP_OVP_EN,VDRP_OVP_DEG,IBUS_RCP_EN,VDRP_OVP,//0x05
	BUS_OVP_EN,BUS_OVP,
	IBUS_UCP_EN,IBUS_UCP,IBUS_OCP_EN,IBUS_OCP, //0x07
	VBUS_HI_DEG,VBUS_LO_DEG,IBUS_UCP_BLK,IBUS_UCP_FALL_DEG,
	BAT_OVP_EN,BAT_OVP,
	IBAT_OCP_EN,IBAT_RSNS,IBAT_OCP,//0x0A
	REG_TIMEOUT_DIS,IBAT_REG_EN,IBAT_REG,VBAT_REG_EN,VBAT_REG,
	PMID2OUT_UVP,PMID2OUT_OVP,VOUT_OVP_EN,VOUT_OVP,VOUT_OVP_DEG,//0x0C
	AC_OVP_FLAG,AC_PDN_FLAG,BUS_PDN_FLAG,VDRP_OVP_FLAG,BUS_OVP_FLAG,IBUS_OCP_FLAG,IBUS_UCP_TIMEOUT_FLAG,IBUS_UCP_FALL_FLAG,
	AC_OVP_MASK,AC_PDN_MASK,BUS_PDN_MASK,VDRP_OVP_MASK,BUS_OVP_MASK,IBUS_OCP_MASK,IBUS_UCP_TIMEOUT_MASK,IBUS_UCP_FALL_MASK,
	BAT_OVP_FLAG,IBAT_OCP_FLAG,VBAT_REG_FLAG,IBAT_REG_FLAG,TDIE_OTP_FLAG,VBUS_LO_FLAG,VBUS_HI_FLAG,PEAK_OCP_FLAG,
	BAT_OVP_MASK,IBAT_OCP_MASK,VBAT_REG_MASK,IBAT_REG_MASK,TDIE_OTP_MASK,VBUS_LO_MASK,VBUS_HI_MASK,PEAK_OCP_MASK,//0x10
	BUS_INSERT_FLAG,BAT_INSERT_FLAG,WD_TIMEOUT_FLAG,AC_ABSENT_FLAG,BUS_ABSENT_FLAG,VOUT_OVP_FLAG,ADC_DONE_FLAG,PIN_DIAG_FLAG,
	BUS_INSERT_MASK,BAT_INSERT_MASK,WD_TIMEOUT_MASK,AC_ABSENT_MASK,BUS_ABSENT_MASK,VOUT_OVP_MASK,ADC_DONE_MASK,PIN_DIAG_MASK,
	ADC_EN,ADC_RATE,VBUS_ADC_DIS,IBUS_ADC_DIS,VBAT_ADC_DIS,IBAT_ADC_DIS,TDIE_ADC_DIS,VOUT_ADC_DIS, //0x13
	F_MAX_FIELDS,
};


//REGISTER
static const struct reg_field sgm41600_reg_fields[] = {
	/*reg00*/
	[REG_RST] = REG_FIELD(0x00, 7, 7),
	[CHG_MODE] = REG_FIELD(0x00, 4, 6),
	[WDT_DIS] = REG_FIELD(0x00, 3, 3),
	[WDT_TIMER] = REG_FIELD(0x00, 0, 2),
	/*reg01*/
	[FSW_SET] = REG_FIELD(0x01, 4, 7),
	[FSW_SHIFT] = REG_FIELD(0x01, 2, 3),
	[PEAK_OCP_SET] = REG_FIELD(0x01, 0, 1),
	/*reg02*/
	[PIN_DIAG_EN] = REG_FIELD(0x02, 7, 7),
	[FSW_DITHER_EN] = REG_FIELD(0x02, 6, 6),
	[VBUS_LO_EN] = REG_FIELD(0x02, 5, 5),
	[VBUS_HI_EN] = REG_FIELD(0x02, 4, 4),
	[VBUS_LO] = REG_FIELD(0x02, 2, 3),
	[VBUS_HI] = REG_FIELD(0x02, 0, 1),
	/*reg03*/
	[SGM_DEVICE_ID] = REG_FIELD(0x03, 0, 7),
	/*reg04*/
	[OVPGATE_SET] = REG_FIELD(0x04, 7, 7),
	[OVPGATE_ISET] = REG_FIELD(0x04, 6, 6),
	[OVPGATE_EN] = REG_FIELD(0x04, 5, 5),
	[AC_OVP_EN] = REG_FIELD(0x04, 4, 4),
	[AC_OVP] = REG_FIELD(0x04, 0, 3),
	/*reg05*/
	[AC_PDN_EN] = REG_FIELD(0x05, 7, 7),
	[BUS_PDN_EN] = REG_FIELD(0x05, 6, 6),
	[VDRP_OVP_EN] = REG_FIELD(0x05, 5, 5),
	[VDRP_OVP_DEG] = REG_FIELD(0x05, 4, 4),
	[IBUS_RCP_EN] = REG_FIELD(0x05, 3, 3),
	[VDRP_OVP] = REG_FIELD(0x05, 0, 2),
	/*reg06*/
	[BUS_OVP_EN] = REG_FIELD(0x06, 7, 7),
	[BUS_OVP] = REG_FIELD(0x06, 0, 6),
	/*reg07*/
	[IBUS_UCP_EN] = REG_FIELD(0x07, 7, 7),
	[IBUS_UCP] = REG_FIELD(0x07, 6, 6),
	[IBUS_OCP_EN] = REG_FIELD(0x07, 5, 5),
	[IBUS_OCP] = REG_FIELD(0x07, 0, 3),
	/*reg08*/
	[VBUS_HI_DEG] = REG_FIELD(0x08, 7, 7),
	[VBUS_LO_DEG] = REG_FIELD(0x08, 6, 6),
	[IBUS_UCP_BLK] = REG_FIELD(0x08, 2, 4),
	[IBUS_UCP_FALL_DEG] = REG_FIELD(0x08, 0, 1),
	/*reg09*/
	[BAT_OVP_EN] = REG_FIELD(0x09, 7, 7),
	[BAT_OVP] = REG_FIELD(0x09, 0, 5),
	/*reg0a*/
	[IBAT_OCP_EN] = REG_FIELD(0x0a, 7, 7),
	[IBAT_RSNS] = REG_FIELD(0x0a, 6, 6),
	[IBAT_OCP] = REG_FIELD(0x0a, 0, 5),
	/*reg0b*/
	[REG_TIMEOUT_DIS] = REG_FIELD(0x0b, 6, 6),
	[IBAT_REG_EN] = REG_FIELD(0x0b, 5, 5),
	[IBAT_REG] = REG_FIELD(0x0b, 3, 4),
	[VBAT_REG_EN] = REG_FIELD(0x0b, 2, 2),
	[VBAT_REG] = REG_FIELD(0x0b, 0, 1),
	/*reg0c*/
	[PMID2OUT_UVP] = REG_FIELD(0x0c, 6, 7),
	[PMID2OUT_OVP] = REG_FIELD(0x0c, 4, 5),
	[VOUT_OVP_EN] = REG_FIELD(0x0c, 3, 3),
	[VOUT_OVP] = REG_FIELD(0x0c, 1, 2),
	[VOUT_OVP_DEG] = REG_FIELD(0x0c, 0, 0),
	/*reg0d*/
	[AC_OVP_FLAG] = REG_FIELD(0x0d, 7, 7),
	[AC_PDN_FLAG] = REG_FIELD(0x0d, 6, 6),
	[BUS_PDN_FLAG] = REG_FIELD(0x0d, 5, 5),
	[VDRP_OVP_FLAG] = REG_FIELD(0x0d, 4, 4),
	[BUS_OVP_FLAG] = REG_FIELD(0x0d, 3, 3),
	[IBUS_OCP_FLAG] = REG_FIELD(0x0d, 2, 2),
	[IBUS_UCP_TIMEOUT_FLAG] = REG_FIELD(0x0d, 1, 1),
	[IBUS_UCP_FALL_FLAG] = REG_FIELD(0x0d, 0, 0),
	/*reg0e*/
	[AC_OVP_MASK] = REG_FIELD(0x0e, 7, 7),
	[AC_PDN_MASK] = REG_FIELD(0x0e, 6, 6),
	[BUS_PDN_MASK] = REG_FIELD(0x0e, 5, 5),
	[VDRP_OVP_MASK] = REG_FIELD(0x0e, 4, 4),
	[BUS_OVP_MASK] = REG_FIELD(0x0e, 3, 3),
	[IBUS_OCP_MASK] = REG_FIELD(0x0e, 2, 2),
	[IBUS_UCP_TIMEOUT_MASK] = REG_FIELD(0x0e, 1, 1),
	[IBUS_UCP_FALL_MASK] = REG_FIELD(0x0e, 0, 0),
	/*reg0F*/
	[BAT_OVP_FLAG] = REG_FIELD(0x0f, 7, 7),
	[IBAT_OCP_FLAG] = REG_FIELD(0x0f, 6, 6),
	[VBAT_REG_FLAG] = REG_FIELD(0x0f, 5, 5),
	[IBAT_REG_FLAG] = REG_FIELD(0x0f, 4, 4),
	[TDIE_OTP_FLAG] = REG_FIELD(0x0f, 3, 3),
	[VBUS_LO_FLAG] = REG_FIELD(0x0f, 2, 2),
	[VBUS_HI_FLAG] = REG_FIELD(0x0f, 1, 1),
	[PEAK_OCP_FLAG] = REG_FIELD(0x0f, 0, 0), 
	/*reg10*/
	[BAT_OVP_MASK] = REG_FIELD(0x10, 7, 7),
	[IBAT_OCP_MASK] = REG_FIELD(0x10, 6, 6),
	[VBAT_REG_MASK] = REG_FIELD(0x10, 5, 5),
	[IBAT_REG_MASK] = REG_FIELD(0x10, 4, 4),
	[TDIE_OTP_MASK] = REG_FIELD(0x10, 3, 3),
	[VBUS_LO_MASK] = REG_FIELD(0x10, 2, 2),
	[VBUS_HI_MASK] = REG_FIELD(0x10, 1, 1),
	[PEAK_OCP_MASK] = REG_FIELD(0x10, 0, 0),
	/*reg11*/
	[BUS_INSERT_FLAG] = REG_FIELD(0x11, 7, 7),
	[BAT_INSERT_FLAG] = REG_FIELD(0x11, 6, 6),
	[WD_TIMEOUT_FLAG] = REG_FIELD(0x11, 5, 5),
	[AC_ABSENT_FLAG] = REG_FIELD(0x11, 4, 4),
	[BUS_ABSENT_FLAG] = REG_FIELD(0x11, 3, 3),
	[VOUT_OVP_FLAG] = REG_FIELD(0x11, 2, 2),
	[ADC_DONE_FLAG] = REG_FIELD(0x11, 1, 1),
	[PIN_DIAG_FLAG] = REG_FIELD(0x11, 0, 0),
	/*reg12*/
	[BUS_INSERT_MASK] = REG_FIELD(0x12, 7, 7),
	[BAT_INSERT_MASK] = REG_FIELD(0x12, 6, 6),
	[WD_TIMEOUT_MASK] = REG_FIELD(0x12, 5, 5),
	[AC_ABSENT_MASK] = REG_FIELD(0x12, 4, 4),
	[BUS_ABSENT_MASK] = REG_FIELD(0x12, 3, 3),
	[VOUT_OVP_MASK] = REG_FIELD(0x12, 2, 2),
	[ADC_DONE_MASK] = REG_FIELD(0x12, 1, 1),
	[PIN_DIAG_MASK] = REG_FIELD(0x12, 0, 0),
	/*reg13*/
	[ADC_EN] = REG_FIELD(0x13, 7, 7),
	[ADC_RATE] = REG_FIELD(0x13, 6, 6),
	[VBUS_ADC_DIS] = REG_FIELD(0x13, 5, 5),
	[IBUS_ADC_DIS] = REG_FIELD(0x13, 4, 4),
	[VBAT_ADC_DIS] = REG_FIELD(0x13, 3, 3),
	[IBAT_ADC_DIS] = REG_FIELD(0x13, 2, 2),
	[TDIE_ADC_DIS] = REG_FIELD(0x13, 1, 1),
	[VOUT_ADC_DIS] = REG_FIELD(0x13, 0, 0),
};

static const struct regmap_config sgm41600_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SGM41600S_REGMAX,
};

/************************************************************************/

struct sgm41600_cfg_e {
	int vbat_ovp_en;
	int vbat_ovp;
	int ibat_ocp_en;
	int ibat_ocp;
	int vac_ovp_en;
	int vac_ovp;
	int vbus_ovp_en;
	int vbus_ovp;
	int vout_ovp_en;
	int vout_ovp;
	int ibus_ocp_en;
	int ibus_ocp;
	int ibus_ucp_fall_en;
	int ibus_ucp_fall;
	int ibat_reg;
	int vbat_reg;
	int vbus_low_en;
	int vbus_low;
	int vbus_hi_en;
	int vbus_hi;
	int vdrop_ovp_en;
	int vdrop_ovp_deg;
	int vdrop_ovp;
	int fsw_set;
	int wdt_dis;
	int wd_timeout;
	int ibat_sns_r;
	int mode;
};

struct sgm41600_chip {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct regmap_field *rmap_fields[F_MAX_FIELDS];

	struct sgm41600_cfg_e cfg;
	int irq_gpio;
	int irq;

	int mode;

	bool charge_enabled;
	int usb_present;
	int vbus_volt;
	int ibus_curr;
	int vbat_volt;
	int ibat_curr;
	int die_temp;

#ifdef CONFIG_MTK_CLASS
	struct charger_device *chg_dev;
#endif /*CONFIG_MTK_CLASS*/

#ifdef CONFIG_SGM_DVCHG_CLASS
	struct dvchg_dev *charger_pump;
#endif /*CONFIG_SGM_DVCHG_CLASS*/

	const char *chg_dev_name;

	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *psy;
};

#ifdef CONFIG_MTK_CLASS
static const struct charger_properties sgm41600_chg_props = {
	.alias_name = "sgm41600_chg",
};
#endif /*CONFIG_MTK_CLASS*/


/********************COMMON API***********************/
#if 0
__maybe_unused static u8 val2reg(enum sgm41600_reg_range id, u32 val)
{
	int i;
	u8 reg;
	const struct reg_range *range= &sgm41600_reg_range[id];

	if (!range)
		return val;

	if (range->table) {
		if (val <= range->table[0])
			return 0;
		for (i = 1; i < range->num_table - 1; i++) {
			if (val == range->table[i])
				return i;
			if (val > range->table[i] &&
				val < range->table[i + 1])
				return range->round_up ? i + 1 : i;
		}
		return range->num_table - 1;
	}
	if (val <= range->min)
		reg = 0;
	else if (val >= range->max)
		reg = (range->max - range->offset) / range->step;
	else if (range->round_up)
		reg = (val - range->offset) / range->step + 1;
	else
		reg = (val - range->offset) / range->step;
	return reg;
}

__maybe_unused static u32 reg2val(enum sgm41600_reg_range id, u8 reg)
{
	const struct reg_range *range= &sgm41600_reg_range[id];
	if (!range)
		return reg;
	return range->table ? range->table[reg] :
				range->offset + range->step * reg;
}
#endif

/*********************************************************/
static int sgm41600_field_read(struct sgm41600_chip *sgm,
                enum sgm41600_fields field_id, int *val)
{
	int ret;

	ret = regmap_field_read(sgm->rmap_fields[field_id], val);
	if (ret < 0) {
		SGM_ERR("i2c read field %d fail: %d\n", field_id, ret);
	}

	return ret;
}

static int sgm41600_field_write(struct sgm41600_chip *sgm,
                enum sgm41600_fields field_id, int val)
{
	int ret;

	ret = regmap_field_write(sgm->rmap_fields[field_id], val);
	if (ret < 0) {
		SGM_ERR("i2c write field %d fail: %d\n", field_id, ret);
	}

	return ret;
}

static int sgm41600_read_block(struct sgm41600_chip *sgm,
                int reg, uint8_t *val, int len)
{
	int ret;

	ret = regmap_bulk_read(sgm->regmap, reg, val, len);
	if (ret < 0) {
		SGM_ERR("i2c read %02x block failed %d\n", reg, ret);
	}

	return ret;
}



/*******************************************************/
__maybe_unused static int sgm41600_detect_device(struct sgm41600_chip *sgm)
{
	int ret;
	int val;

	ret = sgm41600_field_read(sgm, SGM_DEVICE_ID, &val);
	if (ret < 0) {
		SGM_ERR("i2c read fail(%d)\n", ret);
		return ret;
	}

	if (val != SGM41600S_DEVICE_ID) {
		SGM_ERR("not find SGM41600S, ID = 0x%02x\n", ret);
		return -EINVAL;
	}

	return ret;
}

__maybe_unused static int sgm41600_reg_reset(struct sgm41600_chip *sgm)
{
	return sgm41600_field_write(sgm, REG_RST, 1);
}

__maybe_unused static int sgm41600_dump_reg(struct sgm41600_chip *sgm)
{
	int ret;
	int i;
	int val;

	for (i = 0; i <= SGM41600S_REG_VALID; i++) {
		ret = regmap_read(sgm->regmap, i, &val);
		SGM_ERR("reg[0x%02x] = 0x%02x\n", i, val);
	}

	ret = regmap_read(sgm->regmap, SGM41600S_REG51, &val);
	SGM_ERR("reg[0x%02x] = 0x%02x\n", SGM41600S_REG51, val);

	ret = regmap_read(sgm->regmap, SGM41600S_REG5E, &val);
	SGM_ERR("reg[0x%02x] = 0x%02x\n", SGM41600S_REG5E, val);

	return ret;
}

__maybe_unused static int sgm41600_enable_charge(struct sgm41600_chip *sgm, bool en)
{
	int ret;
	SGM_INFO("charger en:%d\n",en);

	if (en)
		ret = sgm41600_field_write(sgm, CHG_MODE, 0x2);
	else
		ret = sgm41600_field_write(sgm, CHG_MODE, 0);

	sgm41600_dump_reg(sgm);

	return ret;
}

__maybe_unused static int sgm41600_check_charge_enabled(struct sgm41600_chip *sgm, bool *enabled)
{
	int ret, val;

	ret = sgm41600_field_read(sgm, CHG_MODE, &val);

	if (val == 1 || val == 2)
		*enabled = true;
	else
		*enabled = false;

	SGM_INFO("en:%d\n", val);

	return ret;
}

__maybe_unused static int sgm41600_get_status(struct sgm41600_chip *sgm, uint32_t *status)
{
	int ret, val;
	*status = 0;

	ret = sgm41600_field_read(sgm, VBUS_HI_FLAG, &val);
	if (ret < 0) {
		SGM_ERR("fail to read VBUS_HI_FLAG(%d)\n", ret);
		return ret;
	}
	if (val != 0)
		*status |= BIT(ERROR_VBUS_HIGH);

	ret = sgm41600_field_read(sgm, VBUS_LO_FLAG, &val);
	if (ret < 0) {
		SGM_ERR("fail to read VBUS_LO_FLAG(%d)\n", ret);
		return ret;
	}
	if (val != 0)
		*status |= BIT(ERROR_VBUS_LOW);

	return ret;
}

__maybe_unused static int sgm41600_enable_adc(struct sgm41600_chip *sgm, bool en)
{
	SGM_INFO("en:%d\n", en);
	return sgm41600_field_write(sgm, ADC_EN, !!en);
}

__maybe_unused static int sgm41600_set_adc_scanrate(struct sgm41600_chip *sgm, bool oneshot)
{
	SGM_INFO("scanrate:%d\n",oneshot);
	return sgm41600_field_write(sgm, ADC_RATE, !!oneshot);
}

static int sgm41600_get_adc_data(struct sgm41600_chip *sgm,
            int channel, int *result)
{
	uint8_t val[2] = {0};
	int ret;
	int step = 0;
	struct power_supply *bat_psy;
	union power_supply_propval prop;

	if (channel >= ADC_MAX_NUM)
		return -EINVAL;

	ret = sgm41600_read_block(sgm, SGM41600S_REG14 + (channel << 1), val, 2);
	if (ret < 0) {
		return ret;
	}

	switch (channel) {
	case ADC_VBUS:
		step = 4;
		*result = (val[1] | (val[0] << 8)) * step;
		break;
	case ADC_IBUS:
		step = 2;
		*result = (val[1] | (val[0] << 8)) * step;
		break;
	case ADC_VBAT:
		step = 2;
		*result = (val[1] | (val[0] << 8)) * step;
		break;
	case ADC_IBAT:
#if 0 // due to HW design, we can only get the IBAT from external fuel gauge
		step = 25;
		*result = (val[1] | (val[0] << 8)) * step / 10;
#else
		SGM_INFO("get ibat from fuel gauge\n");
		bat_psy = power_supply_get_by_name("battery");
		if (IS_ERR_OR_NULL(bat_psy)) {
			SGM_INFO("Couldn't get bat_psy\n");
			*result = 0; // default return 0 mA
		} else {
			ret = power_supply_get_property(bat_psy,
				POWER_SUPPLY_PROP_CURRENT_NOW, &prop);
			*result = prop.intval; // current in uA
		}
#endif
		break;
	case ADC_VOUT:
		step = 2;
		*result = (val[1] | (val[0] << 8)) * step;
		break;
	case ADC_TDIE:
		step = 1;
		*result = val[0] * step - 40;
		break;
	case 6:
		break;
	default:
		break;
	}

	SGM_INFO("adc channel:%d %d\n", channel, *result);

	return ret;
}

__maybe_unused static int sgm41600_set_busovp_th(struct sgm41600_chip *sgm, int threshold)
{
	u8 val;

	if (threshold > SGM41600_BUS_OVP_MAX_uV)
		threshold = SGM41600_BUS_OVP_DEF_uV;
	else if (threshold < SGM41600_BUS_OVP_MIN_uV)
		threshold = SGM41600_BUS_OVP_DEF_uV;

	val = (threshold - SGM41600_BUS_OVP_MIN_uV) / SGM41600_BUS_OVP_STEP_uV;

	SGM_INFO("set busovp:%d-%#x\n", threshold, val);
	return sgm41600_field_write(sgm, BUS_OVP, val);
}

__maybe_unused static int sgm41600_set_busocp_th(struct sgm41600_chip *sgm, int threshold)
{
	u8 val = 0;

	if (threshold > SGM41600_IBUS_OCP_MAX_uA)
		threshold = SGM41600_IBUS_OCP_MAX_uA;
	else if (threshold < SGM41600_IBUS_OCP_MIN_uA)
		threshold = SGM41600_IBUS_OCP_MIN_uA;

	val = (threshold - SGM41600_IBUS_OCP_MIN_uA) / SGM41600_IBUS_OCP_STEP_uA;

	SGM_INFO("set busocp:%d-%#x\n", threshold, val);

    return sgm41600_field_write(sgm, IBUS_OCP, val);
}

__maybe_unused static int sgm41600_set_batovp_th(struct sgm41600_chip *sgm, int threshold)
{
	u8 val = 0;

	if (threshold > SGM41600_BAT_OVP_MAX_uV)
		threshold = SGM41600_BAT_OVP_MAX_uV;
	else if (threshold < SGM41600_BAT_OVP_MIN_uV)
		threshold = SGM41600_BAT_OVP_MIN_uV;

	val = (threshold - SGM41600_BAT_OVP_MIN_uV) / SGM41600_BAT_OVP_STEP_uV;

	SGM_INFO("set batovp:%d-%#x\n", threshold, val);

	return sgm41600_field_write(sgm, BAT_OVP, val);
}

__maybe_unused static int sgm41600_set_batocp_th(struct sgm41600_chip *sgm, int threshold)
{
	u8 val;

	if (threshold > SGM41600_IBAT_OCP_MAX_uA)
		threshold = SGM41600_IBAT_OCP_MAX_uA;
	else if (threshold < SGM41600_IBAT_OCP_MIN_uA)
		threshold = SGM41600_IBAT_OCP_MIN_uA;

	val = (threshold - SGM41600_IBAT_OCP_MIN_uA) / SGM41600_IBAT_OCP_STEP_uA;

	SGM_INFO("set batocp:%d-%#x\n", threshold, val);

	return sgm41600_field_write(sgm, IBAT_OCP, val);
}

__maybe_unused static int sgm41600_set_vbusovp_alarm(struct sgm41600_chip *sgm, int threshold)
{
	SGM_INFO("set:%d\n", threshold);

	return 0;
}

__maybe_unused static int sgm41600_set_vbatovp_alarm(struct sgm41600_chip *sgm, int threshold)
{
	SGM_INFO("set:%d\n", threshold);

	return 0;
}

__maybe_unused static int sgm41600_is_vbuslowerr(struct sgm41600_chip *sgm, bool *err)
{
	int ret;
	int val;

	ret = sgm41600_field_read(sgm, VBUS_LO_FLAG, &val);
	if (ret < 0) {
		return ret;
	}

	SGM_INFO("set:%d\n",val);
	*err = (bool)val;

	return ret;
}

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
static int sgm41600_is_vbushigher(struct sgm41600_chip *sgm, bool *err)
{
	int ret;
	int val;

	ret = sgm41600_field_read(sgm, VBUS_HI_FLAG, &val);
	if (ret < 0) {
		return ret;
	}

	SGM_INFO("set:%d\n", val);
	*err = (bool)val;

	return ret;
}

static int sgm41600_is_vbat_present(struct sgm41600_chip *sgm, bool *present)
{
	int ret;
	int val;

	ret = sgm41600_field_read(sgm, BAT_INSERT_FLAG, &val);
	if (ret < 0) {
		return ret;
	}

	SGM_INFO("%d", val);

	*present = (bool)val;

	return ret;
}

static int sgm41600_is_vbus_present(struct sgm41600_chip *sgm, bool *present)
{
	int ret;
	int val;

	ret = sgm41600_field_read(sgm, BUS_INSERT_FLAG, &val);
	if (ret < 0) {
		return ret;
	}

	SGM_INFO("%d", val);

	*present = (bool)val;

	return ret;
}
#endif /* CONFIG_OEM_TURBO_CHARGER */

static int sgm41600_get_adc_enabld(struct sgm41600_chip *sgm, bool *enable)
{
	int ret;
	int val;

	ret = sgm41600_field_read(sgm, ADC_EN, &val);
	if (ret < 0) {
		return ret;
	}

	SGM_INFO("%d", val);

	*enable = (bool)val;

	return ret;
}

#if IS_ENABLED(CONFIG_OEM_CHARGER_PUMP)
static int mtk_sgm41600_enable_adc(struct charger_device *chg_dev, bool enable)
{
	int ret;
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	SGM_INFO("%d", enable);

	ret = sgm41600_enable_adc(sgm, enable);
	return ret;
}

static int mtk_sgm41600_is_adc_enabled(struct charger_device *chg_dev, bool *enable)
{
	int ret;
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	SGM_INFO("enter");

	ret = sgm41600_get_adc_enabld(sgm, enable);
	return ret;
}
#endif /* CONFIG_OEM_CHARGER_PUMP */

__maybe_unused static int sgm41600_init_device(struct sgm41600_chip *sgm)
{
	int ret = 0;
	int i;
	struct {
		enum sgm41600_fields field_id;
		int conv_data;
	} props[] = {
		{BAT_OVP_EN, sgm->cfg.vbat_ovp_en},
		{BAT_OVP, sgm->cfg.vbat_ovp},
		{IBAT_OCP_EN, sgm->cfg.ibat_ocp_en},
		{IBAT_OCP, sgm->cfg.ibat_ocp},
		{AC_OVP_EN, sgm->cfg.vac_ovp_en},
		{AC_OVP, sgm->cfg.vac_ovp},
		{BUS_OVP_EN, sgm->cfg.vbus_ovp_en},
		{BUS_OVP, sgm->cfg.vbus_ovp},
		{VOUT_OVP_EN, sgm->cfg.vout_ovp_en},
		{VOUT_OVP, sgm->cfg.vout_ovp},
		{IBUS_OCP_EN, sgm->cfg.ibus_ocp_en},
		{IBUS_OCP, sgm->cfg.ibus_ocp},
		{IBUS_UCP_EN, sgm->cfg.ibus_ucp_fall_en},
		{IBUS_UCP,   sgm->cfg.ibus_ucp_fall},

		{IBAT_REG, sgm->cfg.ibat_reg},
		{VBAT_REG, sgm->cfg.vbat_reg},
		{VBUS_LO_EN, sgm->cfg.vbus_low_en},
		{VBUS_LO, sgm->cfg.vbus_low},
		{VBUS_HI_EN, sgm->cfg.vbus_hi_en},
		{VBUS_HI, sgm->cfg.vbus_hi},
		{VDRP_OVP_EN, sgm->cfg.vdrop_ovp_en},
		{VDRP_OVP_DEG, sgm->cfg.vdrop_ovp_deg},
		{VDRP_OVP, sgm->cfg.vdrop_ovp},
		{FSW_SET, sgm->cfg.fsw_set},
		{WDT_DIS, sgm->cfg.wdt_dis},
		{WDT_TIMER, sgm->cfg.wd_timeout},
		{IBAT_RSNS, sgm->cfg.ibat_sns_r},
		{CHG_MODE, sgm->cfg.mode},
	};

	ret = sgm41600_reg_reset(sgm);
	if (ret < 0) {
		SGM_ERR("Failed to reset registers(%d)\n", ret);
	}
	msleep(5);

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = sgm41600_field_write(sgm, props[i].field_id, props[i].conv_data);
	}

//	ret = sgm41600_field_write(sgm, FSW_DITHER_EN, 1);

	if (sgm->mode == SGM41600S_SLAVE) {
		//ret = sgm41600_field_write(sgm, VBUS_INRANGE_DET_DIS, 1);
		if (ret < 0) {
			SGM_ERR("Failed to set vbus in range(%d)\n", ret);
		}
	}

	//sgm41600_enable_adc(sgm, true);
	sgm41600_dump_reg(sgm);

	return ret;
}


/*********************mtk charger interface start**********************************/
#ifdef CONFIG_MTK_CLASS
static inline int to_sgm41600_adc(enum adc_channel chan)
{
	switch (chan) {
	case ADC_CHANNEL_VBUS:
		return ADC_VBUS;
	case ADC_CHANNEL_VBAT:
		return ADC_VBAT;
	case ADC_CHANNEL_IBUS:
		return ADC_IBUS;
	case ADC_CHANNEL_IBAT:
		return ADC_IBAT;
	case ADC_CHANNEL_TEMP_JC:
		return ADC_TDIE;
	case ADC_CHANNEL_VOUT:
		return ADC_VOUT;
	default:
		break;
	}
	return ADC_MAX_NUM;
}

static int mtk_sgm41600_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);
	int ret;

	ret = sgm41600_check_charge_enabled(sgm, en);

	return ret;
}

static int mtk_sgm41600_enable_chg(struct charger_device *chg_dev, bool en)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);
	int ret;

	ret = sgm41600_enable_charge(sgm,en);

	return ret;
}

static int mtk_sgm41600_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	return sgm41600_set_busovp_th(sgm, uV);
}

__maybe_unused static int mtk_sgm41600_set_mode(struct charger_device *chg_dev, u32 mode)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);
	int val,ret;

	ret = sgm41600_field_write(sgm, CHG_MODE, !!mode);
	sgm->cfg.mode = mode;

	ret = sgm41600_field_read(sgm, CHG_MODE, &val);
	if (ret < 0) {
		SGM_ERR("fail to read MODE(%d)\n", ret);
	}
	SGM_ERR("after set mode ,read MODE(%d)\n",val);

	return ret;
}

static int mtk_sgm41600_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	return sgm41600_set_busocp_th(sgm, uA);
}

static int mtk_sgm41600_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);
	int ret;

	ret = sgm41600_set_batovp_th(sgm, uV);
	if (ret < 0)
		return ret;

	return ret;
}

static int mtk_sgm41600_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);
	int ret;

	ret = sgm41600_set_batocp_th(sgm, uA);
	if (ret < 0)
		return ret;

	return ret;
}

static int mtk_sgm41600_get_adc(struct charger_device *chg_dev, enum adc_channel chan,
			  int *min, int *max)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	sgm41600_get_adc_data(sgm, to_sgm41600_adc(chan), max);

	if (chan != ADC_CHANNEL_TEMP_JC)
		*max = *max * 1000;

	if (min != max)
		*min = *max;

	return 0;
}

static int mtk_sgm41600_get_adc_accuracy(struct charger_device *chg_dev,
				   enum adc_channel chan, int *min, int *max)
{
	//*min = *max = sgm41600_adc_accuracy_tbl[to_sgm41600_adc(chan)];
	return -1;
}

static int mtk_sgm41600_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	return sgm41600_is_vbuslowerr(sgm,err);
}

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
static int mtk_sgm41600_is_vbushigher(struct charger_device *chg_dev, bool *err)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	return sgm41600_is_vbushigher(sgm, err);
}

static int mtk_sgm41600_is_vbat_present(struct charger_device *chg_dev, bool *present)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	return sgm41600_is_vbat_present(sgm, present);
}

static int mtk_sgm41600_is_vbus_present(struct charger_device *chg_dev, bool *present)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	return sgm41600_is_vbus_present(sgm, present);
}
#endif

static int mtk_sgm41600_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);
	int ret;

	ret = sgm41600_set_vbatovp_alarm(sgm, uV);
	if (ret < 0)
		return ret;

	return ret;
}

static int mtk_sgm41600_reset_vbatovp_alarm(struct charger_device *chg_dev)
{
	//struct sgm41600_chip *sgm = charger_get_data(chg_dev);
	SGM_INFO("enter\n");
	return 0;
}

static int mtk_sgm41600_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);
	int ret;

	ret = sgm41600_set_vbusovp_alarm(sgm, uV);
	if (ret < 0)
		return ret;

	return ret;
}

static int mtk_sgm41600_reset_vbusovp_alarm(struct charger_device *chg_dev)
{
	//struct sgm41600_chip *sgm = charger_get_data(chg_dev);
	SGM_INFO("enter\n");
	return 0;
}

static int mtk_sgm41600_init_chip(struct charger_device *chg_dev)
{
	struct sgm41600_chip *sgm = charger_get_data(chg_dev);

	return sgm41600_init_device(sgm);
}

static const struct charger_ops sgm41600_chg_ops = {
	.enable = mtk_sgm41600_enable_chg,
	.is_enabled = mtk_sgm41600_is_chg_enabled,
	.get_adc = mtk_sgm41600_get_adc,
	.get_adc_accuracy = mtk_sgm41600_get_adc_accuracy,
	.set_vbusovp = mtk_sgm41600_set_vbusovp,
	.set_ibusocp = mtk_sgm41600_set_ibusocp,
	.set_vbatovp = mtk_sgm41600_set_vbatovp,
	.set_ibatocp = mtk_sgm41600_set_ibatocp,
	.init_chip = mtk_sgm41600_init_chip,
	.is_vbuslowerr = mtk_sgm41600_is_vbuslowerr,
	.set_vbatovp_alarm = mtk_sgm41600_set_vbatovp_alarm,
	.reset_vbatovp_alarm = mtk_sgm41600_reset_vbatovp_alarm,
	.set_vbusovp_alarm = mtk_sgm41600_set_vbusovp_alarm,
	.reset_vbusovp_alarm = mtk_sgm41600_reset_vbusovp_alarm,
	//.set_cp_mode = mtk_sgm41600_set_mode,
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	.is_vbushigher = mtk_sgm41600_is_vbushigher,
	.is_vbat_present = mtk_sgm41600_is_vbat_present,
	.is_vbus_present = mtk_sgm41600_is_vbus_present,
#endif /* CONFIG_OEM_TURBO_CHARGER */
#if IS_ENABLED(CONFIG_OEM_CHARGER_PUMP)
	.enable_adc = mtk_sgm41600_enable_adc,
	.is_adc_enabled = mtk_sgm41600_is_adc_enabled,
#endif /* CONFIG_OEM_CHARGER_PUMP */

};
#endif /*CONFIG_MTK_CLASS*/

/********************mtk charger interface end*************************************************/

#ifdef CONFIG_SGM_DVCHG_CLASS
static int sc_sgm41600_set_enable(struct dvchg_dev *charger_pump, bool enable)
{
	struct sgm41600_chip *sgm = dvchg_get_private(charger_pump);
	int ret;

	ret = sgm41600_enable_charge(sgm,enable);

	return ret;
}

static int sc_sgm41600_get_is_enable(struct dvchg_dev *charger_pump, bool *enable)
{
	struct sgm41600_chip *sgm = dvchg_get_private(charger_pump);
	int ret;

	ret = sgm41600_check_charge_enabled(sgm, enable);

	return ret;
}

static int sc_sgm41600_get_status(struct dvchg_dev *charger_pump, uint32_t *status)
{
	struct sgm41600_chip *sgm = dvchg_get_private(charger_pump);
	int ret = 0;

	ret = sgm41600_get_status(sgm, status);

	return ret;
}

static int sc_sgm41600_get_adc_value(struct dvchg_dev *charger_pump, enum sc_adc_channel ch, int *value)
{
	struct sgm41600_chip *sgm = dvchg_get_private(charger_pump);
	int ret = 0;

	ret = sgm41600_get_adc_data(sgm, ch, value);

	return ret;
}

static struct dvchg_ops sc_sgm41600_dvchg_ops = {
	.set_enable = sc_sgm41600_set_enable,
	.get_status = sc_sgm41600_get_status,
	.get_is_enable = sc_sgm41600_get_is_enable,
	.get_adc_value = sc_sgm41600_get_adc_value,
};
#endif /*CONFIG_SGM_DVCHG_CLASS*/

/********************creat devices note start*************************************************/
static ssize_t sgm41600_show_registers(struct device *dev,
                struct device_attribute *attr, char *buf)
{
	struct sgm41600_chip *sgm = dev_get_drvdata(dev);
	u8 addr;
	int val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sgm41600");
	for (addr = 0x0; addr <= SGM41600S_REGMAX; addr++) {
		ret = regmap_read(sgm->regmap, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
					"Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t sgm41600_store_register(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	struct sgm41600_chip *sgm = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= SGM41600S_REGMAX){
		SGM_INFO("reg = %#x.val = %#x\n", reg, val);
		regmap_write(sgm->regmap, (unsigned char)reg, (unsigned char)val);
	}

	return count;
}

static DEVICE_ATTR(registers, 0664, sgm41600_show_registers, sgm41600_store_register);

static void sgm41600_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}
/********************creat devices note end*************************************************/

/*
* interrupt does nothing, just info event chagne, other module could get info
* through power supply interface
*/
#ifdef CONFIG_MTK_CLASS
static inline int status_reg_to_charger(enum sgm41600_notify notify)
{
	switch (notify) {
	case SGM41600S_NOTIFY_IBUSOCP:
		return CHARGER_DEV_NOTIFY_IBUSOCP;
	case SGM41600S_NOTIFY_VBUSOVP:
		return CHARGER_DEV_NOTIFY_VBUS_OVP;
	case SGM41600S_NOTIFY_IBATOCP:
		return CHARGER_DEV_NOTIFY_IBATOCP;
	case SGM41600S_NOTIFY_VBATOVP:
		return CHARGER_DEV_NOTIFY_BAT_OVP;
	case SGM41600S_NOTIFY_VOUTOVP:
		return CHARGER_DEV_NOTIFY_VOUTOVP;
	default:
        return -EINVAL;
		break;
	}
	return -EINVAL;
}
#endif /*CONFIG_MTK_CLASS*/

static void sgm41600_check_fault_status(struct sgm41600_chip *sgm)
{
	int ret;
	u8 flag = 0;
	int i, j;
#ifdef CONFIG_MTK_CLASS
	int noti;
#endif /*CONFIG_MTK_CLASS*/

	for (i=0;i < ARRAY_SIZE(cp_intr_flag);i++) {
		ret = sgm41600_read_block(sgm, cp_intr_flag[i].reg, &flag, 1);
		for (j = 0; j < cp_intr_flag[i].len; j++) {
			if (flag & cp_intr_flag[i].bit[j].mask) {
				SGM_INFO("trigger :%s\n", cp_intr_flag[i].bit[j].name);
#ifdef CONFIG_MTK_CLASS
				noti = status_reg_to_charger(cp_intr_flag[i].bit[j].notify);
				if (noti >= 0) {
					charger_dev_notify(sgm->chg_dev, noti);
				}
#endif /*CONFIG_MTK_CLASS*/
			}
		}
	}
}

static irqreturn_t sgm41600_irq_handler(int irq, void *data)
{
	struct sgm41600_chip *sgm = data;

	SGM_INFO("sgm41600 INT OCCURED\n");

	sgm41600_check_fault_status(sgm);

	power_supply_changed(sgm->psy);

	return IRQ_HANDLED;
}

static int sgm41600_register_interrupt(struct sgm41600_chip *sgm)
{
	int ret;

	if (gpio_is_valid(sgm->irq_gpio)) {
		ret = gpio_request_one(sgm->irq_gpio, GPIOF_DIR_IN,"sgm41600_irq");
		if (ret) {
			SGM_INFO("failed to request sgm41600_irq\n");
			return -EINVAL;
		}
		sgm->irq = gpio_to_irq(sgm->irq_gpio);
		if (sgm->irq < 0) {
			SGM_INFO("sgm41600 failed to gpio_to_irq\n");
			return -EINVAL;
		}
	} else {
		SGM_INFO("sgm41600 irq gpio not provided\n");
		return -EINVAL;
	}

	if (sgm->irq) {
		ret = devm_request_threaded_irq(&sgm->client->dev, sgm->irq,
				NULL, sgm41600_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				sgm41600_irq_name[sgm->mode], sgm);

		if (ret < 0) {
			SGM_INFO("request irq for irq=%d failed, ret =%d\n", sgm->irq, ret);
			return ret;
		}
		enable_irq_wake(sgm->irq);
	}

	return ret;
}
/********************interrupte end*************************************************/


/************************psy start**************************************/
static enum power_supply_property sgm41600_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_TEMP,
};

static int sgm41600_charger_get_property(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
	struct sgm41600_chip *sgm = power_supply_get_drvdata(psy);
	int result;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		sgm41600_check_charge_enabled(sgm, &sgm->charge_enabled);
		val->intval = sgm->charge_enabled;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sgm41600_get_adc_data(sgm, ADC_VBUS, &result);
		if (!ret)
			sgm->vbus_volt = result;
		val->intval = sgm->vbus_volt * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = sgm41600_get_adc_data(sgm, ADC_IBUS, &result);
		if (!ret)
			sgm->ibus_curr = result;
		val->intval = sgm->ibus_curr * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sgm41600_get_adc_data(sgm, ADC_VBAT, &result);
		if (!ret)
			sgm->vbat_volt = result;
		val->intval = sgm->vbat_volt * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sgm41600_get_adc_data(sgm, ADC_IBAT, &result);
		if (!ret)
			sgm->ibat_curr = result;
		val->intval = sgm->ibat_curr;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = sgm41600_get_adc_data(sgm, ADC_TDIE, &result);
		if (!ret)
			sgm->die_temp = result;
		val->intval = sgm->die_temp;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sgm41600_charger_set_property(struct power_supply *psy,
                    enum power_supply_property prop,
                    const union power_supply_propval *val)
{
	struct sgm41600_chip *sgm = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		sgm41600_enable_charge(sgm, val->intval);
		SGM_INFO( "POWER_SUPPLY_PROP_ONLINE: %s\n",
			val->intval ? "enable" : "disable");
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sgm41600_charger_is_writeable(struct power_supply *psy,
                    enum power_supply_property prop)
{
	return 0;
}

static int sgm41600_psy_register(struct sgm41600_chip *sgm)
{
	sgm->psy_cfg.drv_data = sgm;
	sgm->psy_cfg.of_node = sgm->dev->of_node;

	sgm->psy_desc.name = sgm41600_psy_name[sgm->mode];

	sgm->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	sgm->psy_desc.properties = sgm41600_charger_props;
	sgm->psy_desc.num_properties = ARRAY_SIZE(sgm41600_charger_props);
	sgm->psy_desc.get_property = sgm41600_charger_get_property;
	sgm->psy_desc.set_property = sgm41600_charger_set_property;
	sgm->psy_desc.property_is_writeable = sgm41600_charger_is_writeable;

	sgm->psy = devm_power_supply_register(sgm->dev,
				&sgm->psy_desc, &sgm->psy_cfg);
	if (IS_ERR(sgm->psy)) {
		SGM_ERR("failed to register psy\n");
		return PTR_ERR(sgm->psy);
	}

	SGM_INFO( "%s power supply register successfully\n", sgm->psy_desc.name);

	return 0;
}


/************************psy end**************************************/

static int sgm41600_set_work_mode(struct sgm41600_chip *sgm, int mode)
{
	sgm->mode = mode;

	SGM_INFO("SGM41600S work mode is %s\n", sgm->mode == SGM41600S_STANDALONG
		? "standalone" : (sgm->mode == SGM41600S_MASTER ? "master" : "slave"));

	return 0;
}

static int sgm41600_parse_dt(struct sgm41600_chip *sgm, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int i;
	int ret;
	struct {
		char *name;
		int *conv_data;
	} props[] = {
		{"sgm,sgm41600,vbat-ovp-en", &(sgm->cfg.vbat_ovp_en)},
		{"sgm,sgm41600,vbat-ovp", &(sgm->cfg.vbat_ovp)},
		{"sgm,sgm41600,ibat-ocp-en", &(sgm->cfg.ibat_ocp_en)},
		{"sgm,sgm41600,ibat-ocp", &(sgm->cfg.ibat_ocp)},
		{"sgm,sgm41600,vac-ovp-en", &(sgm->cfg.vac_ovp_en)},
		{"sgm,sgm41600,vac-ovp", &(sgm->cfg.vac_ovp)},
		{"sgm,sgm41600,vbus-ovp-en", &(sgm->cfg.vbus_ovp_en)},
		{"sgm,sgm41600,vbus-ovp", &(sgm->cfg.vbus_ovp)},
		{"sgm,sgm41600,vout-ovp-en", &(sgm->cfg.vout_ovp_en)},
		{"sgm,sgm41600,vout-ovp", &(sgm->cfg.vout_ovp)},
		{"sgm,sgm41600,ibus-ocp-en", &(sgm->cfg.ibus_ocp_en)},
		{"sgm,sgm41600,ibus-ocp", &(sgm->cfg.ibus_ocp)},
		{"sgm,sgm41600,ibus-ucp-fall-en", &(sgm->cfg.ibus_ucp_fall_en)},
		{"sgm,sgm41600,ibus-ucp-fall", &(sgm->cfg.ibus_ucp_fall)},

		{"sgm,sgm41600,ibat-reg", &(sgm->cfg.ibat_reg)},
		{"sgm,sgm41600,vbat-reg", &(sgm->cfg.vbat_reg)},
		{"sgm,sgm41600,vbus-low-en", &(sgm->cfg.vbus_low_en)},
		{"sgm,sgm41600,vbus-low", &(sgm->cfg.vbus_low)},
		{"sgm,sgm41600,vbus-high-en", &(sgm->cfg.vbus_hi_en)},
		{"sgm,sgm41600,vbus-high", &(sgm->cfg.vbus_hi)},
		{"sgm,sgm41600,vdrop-ovp-en", &(sgm->cfg.vdrop_ovp_en)},
		{"sgm,sgm41600,vdrop-ovp-deg", &(sgm->cfg.vdrop_ovp_deg)},
		{"sgm,sgm41600,vdrop-ovp", &(sgm->cfg.vdrop_ovp)},
		{"sgm,sgm41600,fsw-set", &(sgm->cfg.fsw_set)},
		{"sgm,sgm41600,wdt-dis", &(sgm->cfg.wdt_dis)},
		{"sgm,sgm41600,wd-timeout", &(sgm->cfg.wd_timeout)},
		{"sgm,sgm41600,ibat-sns-r", &(sgm->cfg.ibat_sns_r)},
		{"sgm,sgm41600,mode", &(sgm->cfg.mode)},
	};

	/* initialize data for optional properties */
	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = of_property_read_u32(np, props[i].name,
					props[i].conv_data);
		if (ret < 0) {
			SGM_ERR("can not read %s \n", props[i].name);
			return ret;
		}
	}

	sgm->irq_gpio = of_get_named_gpio(np, "sgm41600,intr_gpio", 0);
	if (!gpio_is_valid(sgm->irq_gpio)) {
		SGM_INFO("fail to valid gpio : %d\n", sgm->irq_gpio);
		return -EINVAL;
	}

#ifdef CONFIG_MTK_CHARGER_V5P10
	if (of_property_read_string(np, "charger_name", &sgm->chg_dev_name) < 0) {
		sgm->chg_dev_name = "charger";
		SGM_INFO("sgm41600 no charger name\n");
	}
#elif defined(CONFIG_MTK_CHARGER_V4P19)
	if (of_property_read_string(np, "charger_name_v4_19", &sgm->chg_dev_name) < 0) {
		sgm->chg_dev_name = "charger";
		SGM_INFO("sgm41600 no charger name\n");
	}
#endif /*CONFIG_MTK_CHARGER_V4P19*/
	SGM_ERR("end\n");
	return 0;
}

static struct of_device_id sgm41600_charger_match_table[] = {
	{   .compatible = "sgm,sgm41600-standalone",
		.data = &sgm41600_mode_data[SGM41600S_STANDALONG], },
	{   .compatible = "sgm,sgm41600-master",
		.data = &sgm41600_mode_data[SGM41600S_MASTER], },
	{   .compatible = "sgm,sgm41600-slave",
		.data = &sgm41600_mode_data[SGM41600S_SLAVE], },
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static int sgm41600_charger_probe(struct i2c_client *client)
#else
static int sgm41600_charger_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
#endif
{
	struct sgm41600_chip *sgm;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	int ret, i;

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	if (oem_pcba_charge_power() != CHARGE_POWER_33W) {
		pr_err("found 18W device, not init sgm41600s\n");
		return -ENODEV;
	}
#endif

	SGM_INFO("sgm driver version:(%s)\n", SGM41600S_DRV_VERSION);

	sgm = devm_kzalloc(&client->dev, sizeof(struct sgm41600_chip), GFP_KERNEL);
	if (!sgm) {
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	sgm->dev = &client->dev;
	sgm->client = client;
	sgm->client->addr = 0x6E;  // due to the same slave address of another charger pump, config the real slave address here.

	sgm->regmap = devm_regmap_init_i2c(client,
					&sgm41600_regmap_config);
	if (IS_ERR(sgm->regmap)) {
		SGM_ERR("Failed to initialize regmap\n");
		ret = PTR_ERR(sgm->regmap);
		goto err_regmap_init;
	}

	for (i = 0; i < ARRAY_SIZE(sgm41600_reg_fields); i++) {
		const struct reg_field *reg_fields = sgm41600_reg_fields;

		sgm->rmap_fields[i] =
			devm_regmap_field_alloc(sgm->dev,
									sgm->regmap,
									reg_fields[i]);
		if (IS_ERR(sgm->rmap_fields[i])) {
			SGM_ERR("cannot allocate regmap field\n");
			ret = PTR_ERR(sgm->rmap_fields[i]);
			goto err_regmap_field;
		}
	}
	ret = sgm41600_parse_dt(sgm, &client->dev);
	if (ret < 0) {
		SGM_ERR("parse dt failed(%d)\n", ret);
		goto err_parse_dt;
	}
	ret = sgm41600_detect_device(sgm);
	if (ret < 0) {
		SGM_ERR("detect device fail\n");
		goto err_detect_dev;
	}

	i2c_set_clientdata(client, sgm);
	sgm41600_create_device_node(&(client->dev));

	match = of_match_node(sgm41600_charger_match_table, node);
	if (match == NULL) {
		SGM_ERR("device tree match not found!\n");
		goto err_match_node;
	}

	ret = sgm41600_set_work_mode(sgm, *(int *)match->data);
	if (ret) {
		SGM_INFO("Fail to set work mode!\n");
		goto err_set_mode;
	}

	ret = sgm41600_init_device(sgm);
	if (ret < 0) {
		SGM_ERR("%s init device failed(%d)\n", __func__, ret);
		goto err_init_device;
	}

	ret = sgm41600_psy_register(sgm);
	if (ret < 0) {
		SGM_ERR("psy register failed(%d)\n", ret);
		goto err_register_psy;
	}

	ret = sgm41600_register_interrupt(sgm);
	if (ret < 0) {
		SGM_ERR("register irq fail(%d)\n", ret);
		goto err_register_irq;
	}

#ifdef CONFIG_MTK_CLASS
	sgm->chg_dev = charger_device_register(sgm->chg_dev_name,
							&client->dev, sgm,
							&sgm41600_chg_ops,
							&sgm41600_chg_props);
	if (IS_ERR_OR_NULL(sgm->chg_dev)) {
		ret = PTR_ERR(sgm->chg_dev);
		SGM_INFO("Fail to register charger!\n");
		goto err_register_mtk_charger;
	}
#endif /*CONFIG_MTK_CLASS*/

#ifdef CONFIG_SGM_DVCHG_CLASS
	sgm->charger_pump = dvchg_register("sc_dvchg",
					sgm->dev, &sc_sgm41600_dvchg_ops, sgm);
	if (IS_ERR_OR_NULL(sgm->charger_pump)) {
		ret = PTR_ERR(sgm->charger_pump);
		SGM_INFO("Fail to register charger!\n");
		goto err_register_sc_charger;
	}
#endif /* CONFIG_SGM_DVCHG_CLASS */

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	FULL_PRODUCT_DEVICE_INFO(ID_CHARGER_PUMP, "SGM41600S");
#endif /* CONFIG_OEM_DEVINFO */

	SGM_ERR("sgm41600[%s] probe successfully!\n",
				sgm->mode == SGM41600S_MASTER ? "master" : "slave");
	return 0;

err_register_psy:
err_register_irq:
#ifdef CONFIG_MTK_CLASS
err_register_mtk_charger:
#endif /*CONFIG_MTK_CLASS*/
#ifdef CONFIG_SGM_DVCHG_CLASS
err_register_sc_charger:
#endif /*CONFIG_SGM_DVCHG_CLASS*/
err_init_device:
	power_supply_unregister(sgm->psy);
err_detect_dev:
err_match_node:
err_set_mode:
err_parse_dt:
err_regmap_init:
err_regmap_field:
	devm_kfree(&client->dev, sgm);
err_kzalloc:
	SGM_ERR("sgm41600 probe fail\n");
	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static void sgm41600_charger_remove(struct i2c_client *client)
#else
static int sgm41600_charger_remove(struct i2c_client *client)
#endif
{
	struct sgm41600_chip *sgm = i2c_get_clientdata(client);

	power_supply_unregister(sgm->psy);
	devm_kfree(&client->dev, sgm);

#if !(LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	return 0;
#endif
}

#ifdef CONFIG_PM_SLEEP
static int sgm41600_suspend(struct device *dev)
{
	struct sgm41600_chip *sgm = dev_get_drvdata(dev);

	SGM_INFO("Suspend successfully!");
	if (device_may_wakeup(dev))
		enable_irq_wake(sgm->irq);
	disable_irq(sgm->irq);

	return 0;
}

static int sgm41600_resume(struct device *dev)
{
	struct sgm41600_chip *sgm = dev_get_drvdata(dev);

	SGM_INFO("Resume successfully!");
	if (device_may_wakeup(dev))
		disable_irq_wake(sgm->irq);
	enable_irq(sgm->irq);

	return 0;
}

static const struct dev_pm_ops sgm41600_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(sgm41600_suspend, sgm41600_resume)
};
#endif

static struct i2c_driver sgm41600_charger_driver = {
	.driver     = {
		.name   = "sgm41600",
		.owner  = THIS_MODULE,
		.of_match_table = sgm41600_charger_match_table,
#ifdef CONFIG_PM_SLEEP
		.pm = &sgm41600_pm,
#endif
	},
	.probe      = sgm41600_charger_probe,
	.remove     = sgm41600_charger_remove,
};

module_i2c_driver(sgm41600_charger_driver);

MODULE_DESCRIPTION("SWGM SGM41600S Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Mike_shi@sg-micro.com");
