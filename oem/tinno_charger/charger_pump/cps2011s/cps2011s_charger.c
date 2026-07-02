/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.

 * Version:cps2011s_charger_V1.0.1_mtk_2024_07_02.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/version.h>

//#include "mtk_charger_intf.h"
#ifdef CONFIG_RT_REGMAP
#include <mt-plat/rt-regmap.h>
#endif /* CONFIG_RT_REGMAP */

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

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include <dev_info.h>
#endif /* CONFIG_OEM_DEVINFO */

/* Information */
#define cps2011s_DRV_VERSION	"1.0.1_MTK"
bool cps2011s_enable_flag = 0;
static struct charger_device *primary_divider_charger;

//CPS <AI_BSP_CHG> <cps> <2021-03-01> modify For 2005R 30w fast charger end
/* Registers */

#define cps2011s_REG_CONTROL1		0x00
#define cps2011s_REG_CONTROL2		0x01
#define cps2011s_REG_CONTROL3		0x02
#define cps2011s_REG_VBATOVP	    0x08	//cps modify
#define cps2011s_REG_IBATOCP	    0x09
#define cps2011s_REG_ACPROTECT	    0x04	// VBUSCON_OVP
#define cps2011s_REG_VBUSOVP	    0x06	//VIN_OVP
#define cps2011s_REG_IBUSOCUCP	    0x07	//IIN_OCP_UCP
#define cps2011s_REG_CHGCTRL0		0x00
#define cps2011s_REG_CHGCTRL1		0x01
#define cps2011s_REG_INTFLAG1		0x0B
#define cps2011s_REG_INTFLAG2		0x0D
#define cps2011s_REG_INTFLAG3		0x0F
#define cps2011s_REG_DEVINFO	    0x03
#define cps2011s_REG_ADCCTRL	    0x11
#define cps2011s_REG_IBUSADC1		0x14
#define cps2011s_REG_IBUSADC0		0x15
#define cps2011s_REG_VBUSADC1		0x12
#define cps2011s_REG_VBUSADC0		0x13
#define cps2011s_REG_VBATADC1		0x16
#define cps2011s_REG_VBATADC0		0x17
#define cps2011s_REG_IBATADC1		0x18
#define cps2011s_REG_IBATADC0		0x19
#define cps2011s_REG_TDIEADC1		0x1A
#define cps2011s_REG_REGCTRL	    0x02
#define cps2011s_REG_BUSDEGLH		0xF2
#define cps2011s_REG_VBUS_STAT		0xE3
#define cps2011s_REG_VOUTADC1		0xE0
#define cps2011s_REG_VOUTADC0		0xE1
#define cps2011s_REG_INDET3		    0xE4

#define cps2011s_CHGEN_MASK			0x70	//BIT(7)
#define cps2011s_CHGEN_SHFT			7

#define cps2011s_FREQUENCY_MASK     0xE0
#define cps2011s_FREQUENCY_SHIFT    5

#define cps2011s_ADCEN_MASK			0x80

#define cps2011s_WDTEN_MASK			BIT(3)

#define cps2011s_OVPGATE_EN_MASK	BIT(5)

#define cps2011s_WDTMR_MASK			0x03

#define cps2011s_VBUSOVP_MASK		0x7F
#define cps2011s_IBUSOCP_MASK		0x1F

#define cps2011s_VBATOVP_MASK	    0x3F
#define cps2011s_IBATOCP_MASK	    0x3F
#define cps2011s_VBUS_STAT_MASK 	0x3C
#define cps2011s_VBUS_STAT_SHIFT	2

#define cps2011s_COMP_EN_MASK		BIT(6)


///////////////////////////////////////////////////////////////////
//cps2011s Register map

#define cps2011s_DEVID	            0x03
#define cps2011s_CTRL0              0x00
#define cps2011s_ID1                0x05
#define cps2011s_ID2                0x06

enum cps2011s_irqidx {
	cps2011s_IRQIDX_IBUSUCPF = 0,
	cps2011s_IRQIDX_IBUSUCPR,
	cps2011s_IRQIDX_IBUSOCP,
	cps2011s_IRQIDX_VBUSOVP,
	cps2011s_IRQIDX_VDROVP,
	cps2011s_IRQIDX_VBUSPD,
	cps2011s_IRQIDX_VBUSCONPD,
	cps2011s_IRQIDX_VBUSCONOVP,
	cps2011s_IRQIDX_CONVOCP,
	cps2011s_IRQIDX_RLTOVP,
	cps2011s_IRQIDX_RLTUVP,
	cps2011s_IRQIDX_THERMALOFF,
	cps2011s_IRQIDX_IBATREG,
	cps2011s_IRQIDX_VBATREG,
	cps2011s_IRQIDX_IBATOCP,
	cps2011s_IRQIDX_VBATOVP,
	cps2011s_IRQIDX_FCAPSCP,
	cps2011s_IRQIDX_ADCDONE,
	cps2011s_IRQIDX_CHGONTMR,
	cps2011s_IRQIDX_VBUSUVLO,
	cps2011s_IRQIDX_VBUSCONUVLO,
	cps2011s_IRQIDX_WDTOUT,
	cps2011s_IRQIDX_VBATPOWEROK,
	cps2011s_IRQIDX_VBUSPOWEROK,
	cps2011s_IRQIDX_MAX,
};

enum cps2011s_notify {
	cps2011s_NOTIFY_IBUSUCPF = 0,
	cps2011s_NOTIFY_IBUSOCP,
	cps2011s_NOTIFY_VBUSOVP,
	cps2011s_NOTIFY_IBATOCP,
	cps2011s_NOTIFY_VBATOVP,
	cps2011s_NOTIFY_VDROVP,
	cps2011s_NOTIFY_MAX,
};

//cps2011s INT Registor 0x01
enum cps2011s_statflag_idx {
	cps2011s_SF_INTFLAG1 = 0,
	cps2011s_SF_INTFLAG2,
	cps2011s_SF_INTFLAG3,
	cps2011s_SF_INDET3,
	cps2011s_SF_MAX = 4,
};
//cps2011s INT Registor 0x01

enum cps2011s_type {
	cps2011s_TYPE_STANDALONE = 0,
	cps2011s_TYPE_SLAVE,
	cps2011s_TYPE_MASTER,
	cps2011s_TYPE_MAX,
};

#if 0
static const char *cps2011s_type_name[cps2011s_TYPE_MAX] = {
	"standalone", "slave", "master",
};
#endif

static const u32 cps2011s_chgdev_notify_map[cps2011s_NOTIFY_MAX] = {
	CHARGER_DEV_NOTIFY_IBUSUCP_FALL,
	CHARGER_DEV_NOTIFY_IBUSOCP,
	CHARGER_DEV_NOTIFY_VBUS_OVP,
	CHARGER_DEV_NOTIFY_IBATOCP,
	CHARGER_DEV_NOTIFY_BAT_OVP,
	CHARGER_DEV_NOTIFY_VDROVP,
};

static const u8 cps2011s_reg_sf[cps2011s_SF_MAX] = {
	cps2011s_REG_INTFLAG1,
	cps2011s_REG_INTFLAG2,
	cps2011s_REG_INTFLAG3,
	cps2011s_REG_INDET3,
};

struct cps2011s_reg_defval {
	u8 reg;
	u8 value;
	u8 mask;
};

static const struct cps2011s_reg_defval cps2011s_init_chip_check_reg[] = {
#if 0
	{
		.reg = cps2011s_REG_VBATOVP,
		.value = 0x22,
		.mask = cps2011s_VBATOVP_MASK,
	},
	{
		.reg = cps2011s_REG_IBATOCP,
		.value = 0x3D,
		.mask = cps2011s_IBATOCP_MASK,
	},
	{
		.reg = cps2011s_REG_CHGCTRL0,//0x0B
		.value = 0x00,
		.mask = cps2011s_WDTMR_MASK,
	},
#endif
};

struct cps2011s_dese {
	const char *chg_name;
	const char *rm_name;
	u8 rm_slave_addr;
	u32 vbatovp;
	u32 vbatovp_alm;
	u32 ibatocp;
	u32 ibatocp_alm;
	u32 ibatucp_alm;
	u32 vbusovp;
	u32 vbusovp_alm;
	u32 ibusocp;
	u32 ibusocp_alm;
	u32 vacovp;
	u32 wdt;
	u32 ibat_rsense;
	u32 ibusucpf_deglitch;
	bool vbatovp_dis;
	bool vbatovp_alm_dis;
	bool ibatocp_dis;
	bool ibatocp_alm_dis;
	bool ibatucp_alm_dis;
	bool vbusovp_alm_dis;
	bool ibusocp_dis;
	bool ibusocp_alm_dis;
	bool wdt_dis;
	bool tsbusotp_dis;
	bool tsbatotp_dis;
	bool tdieotp_dis;
	bool reg_en;
	bool voutovp_dis;
	bool ibusadc_dis;
	bool vbusadc_dis;
	bool vacadc_dis;
	bool voutadc_dis;
	bool vbatadc_dis;
	bool ibatadc_dis;
	bool tsbusadc_dis;
	bool tsbatadc_dis;
	bool tdieadc_dis;
};

static const struct cps2011s_dese cps2011s_dese_defval = {
	.chg_name = "primary_dvchg",
	.rm_name = "cps2011s",
	.rm_slave_addr = 0x6E,	//0x55,	//0x67,
	.vbatovp = 4500000,
	.vbatovp_alm = 4400000,
	.ibatocp = 8100000,
	.ibatocp_alm = 8000000,
	.ibatucp_alm = 2000000,
	.vbusovp = 11000000,
	.vbusovp_alm = 11000000,
	.ibusocp = 4250000,
	.ibusocp_alm = 4000000,
	.vacovp = 11000000,
	.wdt = 500000,
	.ibat_rsense = 1,		/* 2mohm */
	.ibusucpf_deglitch = 0,	/* 10us */
	.vbatovp_dis = false,
	.vbatovp_alm_dis = false,
	.ibatocp_dis = false,
	.ibatocp_alm_dis = false,
	.ibatucp_alm_dis = false,
	.vbusovp_alm_dis = false,
	.ibusocp_dis = false,
	.ibusocp_alm_dis = false,
	.wdt_dis = false,
	.tsbusotp_dis = true,
	.tsbatotp_dis = false,
	.tdieotp_dis = false,
	.reg_en = false,
	.voutovp_dis = false,
};

struct cps2011s_chip {
	struct device *dev;
	struct i2c_client *client;
	struct mutex io_lock;
	struct mutex adc_lock;
	struct mutex stat_lock;
	struct mutex hm_lock;
	struct mutex suspend_lock;
	struct mutex notify_lock;
	struct charger_device *chg_dev;
	struct charger_properties chg_prop;
	struct cps2011s_dese *desc;
	int irq_gpio;
	struct task_struct *notify_task;
	int irq;
	int notify;
	u8 revision;
	u32 flag;
	u32 stat;
	u32 hm_cnt;
	enum cps2011s_type type;
	bool wdt_en;
	bool force_adc_en;
	bool stop_thread;
	wait_queue_head_t wq;

	bool charge_enabled;
	int vbus_volt;
	int ibus_curr;
	int vbat_volt;
	int ibat_curr;
	int die_temp;

#ifdef CONFIG_RT_REGMAP
	struct rt_regmap_device *rm_dev;
	struct rt_regmap_properties *rm_prop;
#endif /* CONFIG_RT_REGMAP */

	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *psy;
};

enum cps2011s_adc_channel {
	cps2011s_ADC_IBUS = 0,
	cps2011s_ADC_VBUS,
	cps2011s_ADC_VBAT,
	cps2011s_ADC_IBAT,
	cps2011s_ADC_TDIE,
	cps2011s_ADC_VOUT,
	cps2011s_ADC_MAX,
	cps2011s_ADC_NOTSUPP = cps2011s_ADC_MAX,
};

static const u8 cps2011s_adc_reg[cps2011s_ADC_MAX] = {
	cps2011s_REG_IBUSADC1,
	cps2011s_REG_VBUSADC1,
	cps2011s_REG_VBATADC1,
	cps2011s_REG_IBATADC1,
	cps2011s_REG_TDIEADC1,
	cps2011s_REG_VOUTADC1,
};

static const char *cps2011s_adc_name[cps2011s_ADC_MAX] = {
	"Ibus", "Vbus", "Vbat", "Ibat", "TDie","Vout",
};

static const u32 cps2011s_adc_accuracy_tbl[cps2011s_ADC_MAX] = {
	75000,	/* IBUS */
	4000,	/* VBUS */
	2000,	/* VBAT */
	80000,	/* IBAT */
	7,	/* TDIE */
	2000,	/* VOUT */
};


static int cps2011s_read_device(void *client, u32 addr, int len, void *dst)
{
	int ret;
	struct i2c_client *i2c = (struct i2c_client *)client;
	struct cps2011s_chip *chip = i2c_get_clientdata(i2c);

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->suspend_lock);
	ret = i2c_smbus_read_i2c_block_data(i2c, addr, len, dst);
	mutex_unlock(&chip->suspend_lock);
	pm_relax(chip->dev);

	return ret;
}

static int cps2011s_write_device(void *client, u32 addr, int len, const void *src)
{
	int ret;
	struct i2c_client *i2c = (struct i2c_client *)client;
	struct cps2011s_chip *chip = i2c_get_clientdata(i2c);

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->suspend_lock);
	ret = i2c_smbus_write_i2c_block_data(i2c, addr, len, src);
	mutex_unlock(&chip->suspend_lock);
	pm_relax(chip->dev);

	return ret;
}

#ifdef CONFIG_RT_REGMAP
RT_REG_DECL(cps2011s_REG_VBATOVP, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_IBATOCP, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_ACPROTECT, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_VBUSOVP, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_IBUSOCUCP, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_CHGCTRL0, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_CHGCTRL1, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_INTFLAG1, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_INTFLAG2, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_INTFLAG3, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_DEVINFO, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_ADCCTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_IBUSADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_IBUSADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_VBUSADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_VBUSADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_VBATADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_VBATADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_IBATADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_IBATADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_TDIEADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_REGCTRL,  1, RT_VOLATILE, {});
RT_REG_DECL(cps2011s_REG_BUSDEGLH, 1, RT_VOLATILE, {});

static const rt_register_map_t cps2011s_regmap[] = {
	RT_REG(cps2011s_REG_VBATOVP),
	RT_REG(cps2011s_REG_IBATOCP),
	RT_REG(cps2011s_REG_ACPROTECT),
	RT_REG(cps2011s_REG_VBUSOVP),
	RT_REG(cps2011s_REG_IBUSOCUCP),
	RT_REG(cps2011s_REG_CHGCTRL0),
	RT_REG(cps2011s_REG_CHGCTRL1),
	RT_REG(cps2011s_REG_INTFLAG1),
	RT_REG(cps2011s_REG_INTFLAG2),
	RT_REG(cps2011s_REG_INTFLAG3),
	RT_REG(cps2011s_REG_DEVINFO),
	RT_REG(cps2011s_REG_ADCCTRL),
	RT_REG(cps2011s_REG_IBUSADC1),
	RT_REG(cps2011s_REG_IBUSADC0),
	RT_REG(cps2011s_REG_VBUSADC1),
	RT_REG(cps2011s_REG_VBUSADC0),
	RT_REG(cps2011s_REG_VBATADC1),
	RT_REG(cps2011s_REG_VBATADC0),
	RT_REG(cps2011s_REG_IBATADC1),
	RT_REG(cps2011s_REG_IBATADC0),
	RT_REG(cps2011s_REG_TDIEADC1),
	RT_REG(cps2011s_REG_REGCTRL),
	RT_REG(cps2011s_REG_BUSDEGLH),
};

static struct rt_regmap_fops cps2011s_rm_fops = {
	.read_device = cps2011s_read_device,
	.write_device = cps2011s_write_device,
};

static int cps2011s_register_regmap(struct cps2011s_chip *chip)
{
	struct i2c_client *client = chip->client;
	struct rt_regmap_properties *prop = NULL;

	dev_info(chip->dev, "%s\n", __func__);

	prop = devm_kzalloc(&client->dev, sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	prop->name = chip->desc->rm_name;
	prop->aliases = chip->desc->rm_name;
	prop->register_num = ARRAY_SIZE(cps2011s_regmap);
	prop->rm = cps2011s_regmap;
	prop->rt_regmap_mode = RT_SINGLE_BYTE | RT_CACHE_DISABLE |
			       RT_IO_PASS_THROUGH;
	prop->io_log_en = 0;

	chip->rm_prop = prop;
	chip->rm_dev = rt_regmap_device_register_ex(chip->rm_prop,
						    &cps2011s_rm_fops, chip->dev,
						    client,
						    chip->desc->rm_slave_addr,
						    chip);
	if (!chip->rm_dev) {
		dev_notice(chip->dev, "%s register regmap dev fail\n", __func__);
		return -EINVAL;
	}

	return 0;
}
#endif /* CONFIG_RT_REGMAP */

#define I2C_ACCESS_MAX_RETRY	5
static inline int __cps2011s_i2c_write8(struct cps2011s_chip *chip, u8 reg, u8 data)
{
	int ret, retry = 0;

	do {
#ifdef CONFIG_RT_REGMAP
		ret = rt_regmap_block_write(chip->rm_dev, reg, 1, &data);
#else
		ret = cps2011s_write_device(chip->client, reg, 1, &data);
#endif /* CONFIG_RT_REGMAP */
		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CW[0x%02X] = 0x%02X fail\n", __func__, reg, data);
		return ret;
	}

	dev_info(chip->dev, "%s I2CW[0x%02X] = 0x%02X\n", __func__, reg, data);

	return 0;
}


static int cps2011s_i2c_write8(struct cps2011s_chip *chip, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __cps2011s_i2c_write8(chip, reg, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}


static inline int __cps2011s_i2c_read8(struct cps2011s_chip *chip, u8 reg, u8 *data)
{
	int ret, retry = 0;

	do {
#ifdef CONFIG_RT_REGMAP
		ret = rt_regmap_block_read(chip->rm_dev, reg, 1, data);
#else
		ret = cps2011s_read_device(chip->client, reg, 1, data);
#endif /* CONFIG_RT_REGMAP */
		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CR[0x%02X] fail\n", __func__, reg);
		return ret;
	}
	dev_info(chip->dev, "%s I2CR[0x%02X] = 0x%02X\n", __func__, reg, *data);

	return 0;
}

static int cps2011s_i2c_read8(struct cps2011s_chip *chip, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __cps2011s_i2c_read8(chip, reg, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static inline int __cps2011s_i2c_write_block(struct cps2011s_chip *chip, u8 reg,
					   u32 len, const u8 *data)
{
	int ret;

#ifdef CONFIG_RT_REGMAP
	ret = rt_regmap_block_write(chip->rm_dev, reg, len, data);
#else
	ret = cps2011s_write_device(chip->client, reg, len, data);
#endif /* CONFIG_RT_REGMAP */

	return ret;
}

#if 0
static int cps2011s_i2c_write_block(struct cps2011s_chip *chip, u8 reg, u32 len,
				  const u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __cps2011s_i2c_write_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}
#endif

static inline int __cps2011s_i2c_read_block(struct cps2011s_chip *chip, u8 reg,
					  u32 len, u8 *data)
{
	int ret;

#ifdef CONFIG_RT_REGMAP
	ret = rt_regmap_block_read(chip->rm_dev, reg, len, data);
#else
	ret = cps2011s_read_device(chip->client, reg, len, data);
#endif /* CONFIG_RT_REGMAP */

	return ret;
}

static int cps2011s_i2c_read_block(struct cps2011s_chip *chip, u8 reg, u32 len,
				 u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __cps2011s_i2c_read_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

#if 0
static int cps2011s_i2c_test_bit(struct cps2011s_chip *chip, u8 reg, u8 shft,
			       bool *one)
{
	int ret;
	u8 data;

	ret = cps2011s_i2c_read8(chip, reg, &data);
	if (ret < 0) {
		*one = false;
		return ret;
	}

	*one = (data & (1 << shft)) ? true : false;
	return 0;
}
#endif

static int cps2011s_i2c_update_bits(struct cps2011s_chip *chip, u8 reg, u8 data, u8 mask)
{
	int ret;
	u8 _data;

	mutex_lock(&chip->io_lock);
	ret = __cps2011s_i2c_read8(chip, reg, &_data);
	if (ret < 0)
		goto out;
	_data &= ~mask;
	_data |= (data & mask);
	ret = __cps2011s_i2c_write8(chip, reg, _data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static inline int cps2011s_set_bits(struct cps2011s_chip *chip, u8 reg, u8 mask)
{
	return cps2011s_i2c_update_bits(chip, reg, mask, mask);
}


static inline int cps2011s_clr_bits(struct cps2011s_chip *chip, u8 reg, u8 mask)
{
	return cps2011s_i2c_update_bits(chip, reg, 0x00, mask);
}

#if 0
static int cps2011s_chg_en_update_bits(struct cps2011s_chip *chip, u8 reg, u8 en)
{
    int ret;
	u8 data;

	mutex_lock(&chip->io_lock);
	ret = __cps2011s_i2c_read8(chip, reg, &data);
	if (ret < 0)
		goto out;
	data |=  en << 7;
	ret = __cps2011s_i2c_write8(chip, reg, data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static inline int cps2011s_set_bits(struct cps2011s_chip *chip, u8 reg, u8 mask)
{
	return cps2011s_i2c_update_bits(chip, reg, mask, mask);
}

static inline int cps2011s_clr_bits(struct cps2011s_chip *chip, u8 reg, u8 mask)
{
	return cps2011s_i2c_update_bits(chip, reg, 0x00, mask);
}

static inline u8 cps2011s_val_toreg_via_tbl(const u32 *tbl, int tbl_size,
					  u32 target)
{
	int i;

	if (target < tbl[0])
		return 0;

	for (i = 0; i < tbl_size - 1; i++) {
		if (target >= tbl[i] && target < tbl[i + 1])
			return i;
	}

	return tbl_size - 1;
}
#endif

static inline u8 cps2011s_val_toreg(u32 min, u32 max, u32 step, u32 target,
				  bool ru)
{
	if (target <= min)
		return 0;

	if (target >= max)
		return (max - min) / step;

	if (ru)
		return (target - min + step) / step;
	return (target - min) / step;
}

#if 0
static const u32 cps2011s_wdt[] = {
	500000, 1000000, 2000000,5000000, 10000000,
	20000000, 40000000, 80000000,
};

static u8 cps2011s_wdt_toreg(u32 uS)
{
	return cps2011s_val_toreg_via_tbl(cps2011s_wdt, ARRAY_SIZE(cps2011s_wdt), uS);
}
#endif

static u8 cps2011s_vbatovp_toreg(u32 uV)
{
	return cps2011s_val_toreg(4000000, 5000000, 25000, uV, false);
}

static u8 cps2011s_vbusovp_toreg(u32 uV)
{
	return cps2011s_val_toreg(4000000, 14000000, 100000, uV, false);
}

static u8 cps2011s_ibusocp_2to1_toreg(u32 uA)
{
	return cps2011s_val_toreg(1900000, 5000000, 100000, uA, false);
}

static u8 cps2011s_ibusocp_bypass_toreg(u32 uA)
{
	return cps2011s_val_toreg(2900000, 6000000, 100000, uA, false);
}
static u8 cps2011s_ibatocp_toreg(u32 uA)
{
	return cps2011s_val_toreg(3000000, 9000000, 100000, uA, true);
}

static int __cps2011s_update_status(struct cps2011s_chip *chip);

static int __cps2011s_init_chip(struct cps2011s_chip *chip);

static int __cps2011s_get_adc(struct cps2011s_chip *chip,
			enum cps2011s_adc_channel chan, int *val)
{
	int ret = 0;
	u8 data[2];
	struct power_supply *bat_psy;
	union power_supply_propval prop;

	dev_info(chip->dev, "%s chan = %d\n", __func__, chan);

	if (chan == cps2011s_ADC_IBAT) {
		dev_info(chip->dev, "%s get ibat from fuel gauge\n", __func__);
		bat_psy = power_supply_get_by_name("battery");
		if (IS_ERR_OR_NULL(bat_psy)) {
			dev_info(chip->dev, "%s Couldn't get bat_psy\n", __func__);
			*val = 0; // default return 0 mA
		} else {
			ret = power_supply_get_property(bat_psy,
						POWER_SUPPLY_PROP_CURRENT_NOW, &prop);
			*val = prop.intval; // current in uA
		}
		dev_info(chip->dev, "%s %d %d", __func__, chan, *val);
		goto out;
	}

	if (ret < 0)
		goto out;

	usleep_range(12000, 15000);

	if (chan == cps2011s_ADC_TDIE)
		ret = cps2011s_i2c_read_block(chip, cps2011s_adc_reg[chan], 1, data);
	else
		ret = cps2011s_i2c_read_block(chip, cps2011s_adc_reg[chan], 2, data);

	if (ret < 0)
		goto out_dis;

	switch (chan) {
	case cps2011s_ADC_IBUS:
		*val = ((data[0] * 256) + data[1]) * 2000;
		break;
	case cps2011s_ADC_VBUS:
		*val = ((data[0] * 256) + data[1]) * 4 * 1000;
		break;
	case cps2011s_ADC_VBAT:
		*val = ((data[0] * 256) + data[1]) * 2000;
		break;
	case cps2011s_ADC_IBAT:
		*val = ((data[0] * 256) + data[1]) * 2000;
		break;
	case cps2011s_ADC_VOUT:
		*val = ((data[0] * 256) + data[1]) * 2000;
		break;
	case cps2011s_ADC_TDIE:
		*val = data[0];
		if (*val > 40) {
			*val = *val - 40;
		} else {
			*val = 0;
		}
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}
	if (ret < 0)
		dev_err(chip->dev, "%s %s fail(%d)\n", __func__, cps2011s_adc_name[chan], ret);
	else
		dev_info(chip->dev, "%s %s %d %d %d\n", __func__, cps2011s_adc_name[chan], data[0], data[1], *val);
out_dis:
out:
	return ret;
}

/* Must be called while holding a lock */
__maybe_unused static int cps2011s_enable_wdt(struct charger_device *chg_dev, bool en)
{
	struct cps2011s_chip *chip = charger_get_data(chg_dev);

	dev_info(chip->dev, "%s %d\n", __func__, en);

	if (en) {
		cps2011s_i2c_update_bits(chip, 0x00, 0x00, 0x08);
	} else {
		cps2011s_i2c_update_bits(chip, 0x00, 0x08, 0x08);
	}

	return 0;
}

static int cps2011s_enable_chg(struct charger_device *chg_dev, bool en) //set enable 2:1 mode
{
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	u8 _data;
	int val;

	dev_info(chip->dev, "%s %d\n", __func__, en);

	if (en) {
		cps2011s_i2c_update_bits(chip, 0x02, 0x40, 0x40);
		cps2011s_i2c_update_bits(chip, 0x11, 0x80, 0x80);
		cps2011s_i2c_update_bits(chip, 0x00, 0x20, 0x70);
		cps2011s_enable_flag = 1;
    } else {
		cps2011s_i2c_update_bits(chip, 0x00, 0x00, 0x70);
		__cps2011s_i2c_read8(chip, 0x00, &_data);
		_data &= 0x70;
		val = (_data >> 4);
		if (val == 0) {
			cps2011s_enable_flag = 0;
		}
	}

	return 0;
}

__maybe_unused static int cps2011s_enable_chg_bypass(struct charger_device *chg_dev, bool en) //set enable bypass mode
{
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	u8 _data;
	int val;

	dev_info(chip->dev, "%s %d\n", __func__, en);

	if (en) {
		cps2011s_i2c_update_bits(chip, 0x02, 0x40, 0x40);
		cps2011s_i2c_update_bits(chip, 0x11, 0x80, 0x80);
		cps2011s_i2c_update_bits(chip, 0x00, 0x10, 0x70);
		cps2011s_enable_flag = 1;
	} else {
		cps2011s_i2c_update_bits(chip, 0x00, 0x00, 0x70);
		__cps2011s_i2c_read8(chip, 0x00, &_data);

		_data &= 0x70;
		val = (_data >> 4);
		if (val == 0) {
			cps2011s_enable_flag = 0;
		}
	}

	return 0;
}

static int cps2011s_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret, chg_en = 0;
	u8 data;
	struct cps2011s_chip *chip = charger_get_data(chg_dev);

	ret = cps2011s_i2c_read8(chip, 0x00, &data);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CR[0x%02X] fail\n", __func__, data);
	} else {
		chg_en = (data & 0x70) >> 4;
	}

	dev_info(chip->dev, "%s chg_en = %d\n", __func__, chg_en);

	*en = chg_en;

	return ret;
}

__maybe_unused static int cps2011s_ibusucp_enabled(struct charger_device *chg_dev, bool *en)
{
    int ret, value;
	struct cps2011s_chip *chip = charger_get_data(chg_dev);

	value = (!!en << 7);

	if (en){
		ret = cps2011s_i2c_update_bits(chip, 0x07, value, 0x80);
	} else {
		ret = cps2011s_i2c_update_bits(chip, 0x07, 0x00, 0x80);
	}

	dev_info(chip->dev, "%s %d, ret(%d)\n", __func__, *en, ret);

	return ret;
}

int cps2011s_enter_sleep(bool is_on)
{
	struct cps2011s_chip *chip;
	if (!primary_divider_charger) {
		primary_divider_charger = get_charger_by_name("primary_dvchg");
		if (!primary_divider_charger) {
			pr_info("%s: get primary_dvchg device failed\n", __func__);
			return -ENODEV;
		}
	}

  	chip = charger_get_data(primary_divider_charger);
	dev_info(chip->dev, "%s %d\n", __func__, is_on);
	if (is_on) {
		if (!cps2011s_enable_flag) {
			cps2011s_i2c_update_bits(chip, 0x00, 0x00, 0x70);
			cps2011s_i2c_update_bits(chip, 0x02, 0x00, 0x40);
			cps2011s_i2c_update_bits(chip, 0x11, 0x00, 0x80);
			//__cps2011s_i2c_write8(chip, 0x02, 0x83);
			//__cps2011s_i2c_write8(chip, 0x11, 0x00);
		}
	} else {
		if (!cps2011s_enable_flag) {
			cps2011s_i2c_update_bits(chip, 0x02, 0x40, 0x40);
			cps2011s_i2c_update_bits(chip, 0x11, 0x80, 0x80);
			//__cps2011s_i2c_write8(chip, 0x02, 0xC3);
			//__cps2011s_i2c_write8(chip, 0x11, 0x80);
		}
	}
	return 0;
}

#if IS_ENABLED(CONFIG_OEM_CHARGER_PUMP)
static int cps2011s_enable_ovpgate(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct cps2011s_chip *chip = charger_get_data(chg_dev);

	dev_info(chip->dev, "%s %d\n", __func__, en);

	if (en)
		ret = cps2011s_i2c_update_bits(chip, cps2011s_REG_ACPROTECT, 0x20, 0x20);
	else
		ret = cps2011s_i2c_update_bits(chip, cps2011s_REG_ACPROTECT, 0x00, 0x20);

	return ret;
}
#endif /* CONFIG_OEM_CHARGER_PUMP */

static inline enum cps2011s_adc_channel to_cps2011s_adc(enum adc_channel chan)
{
	switch (chan) {
	case ADC_CHANNEL_VBUS:  //0
		return cps2011s_ADC_VBUS;
	case ADC_CHANNEL_VBAT:   //2
		return cps2011s_ADC_VBAT;
	case ADC_CHANNEL_IBUS:   //3
		return cps2011s_ADC_IBUS;
	case ADC_CHANNEL_IBAT: //4
		return cps2011s_ADC_IBAT;
	case ADC_CHANNEL_TEMP_JC: //5
		return cps2011s_ADC_TDIE;
	case ADC_CHANNEL_VOUT:  //9
		return cps2011s_ADC_VOUT;
	default:
		break;
	}

	return cps2011s_ADC_NOTSUPP;
}

static int switch_clk[] = {
	300, 400, 500, 600, 700, 800,  900, 1000, 1100, 1200, 1300, 1400, 1500
};

static int cps2011s_dump_register(struct cps2011s_chip *chip)
{
	int rc = 0;
	int addr = 0;
	u8 data;

	for (addr = 0x00; addr < 0x1C; addr++) {
		rc = cps2011s_i2c_read8(chip, addr, &data);
		if (rc == 0) {
			dev_info(chip->dev, "%s read register 0x%x = 0x%x\n", __func__, addr, data);
		}
	}

	for (addr = 0xE0; addr < 0xF5; addr++) {
		rc = cps2011s_i2c_read8(chip, addr, &data);
		if (rc == 0) {
			dev_info(chip->dev, "%s read register 0x%x = 0x%x\n", __func__, addr, data);
		}
	}

	return 0;
}

static int cps2011s_set_switch_clk(struct cps2011s_chip *chip, int clk)
{
	int rc = 0, i;

	for (i = 0; i < ARRAY_SIZE(switch_clk); i++) {
		if (clk <= switch_clk[i])
			break;
	}

	rc = cps2011s_i2c_update_bits(chip, cps2011s_REG_CHGCTRL1,
				i << cps2011s_FREQUENCY_SHIFT, cps2011s_FREQUENCY_MASK);

	return rc;
}

__maybe_unused static int mtk_set_cps2011s_switch_clk(struct charger_device *chg_dev, int clk)
{
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	int rc = 0;

	rc = cps2011s_set_switch_clk(chip, clk);
	if (rc < 0) {
		dev_err(chip->dev, "%s failed\n", __func__);
	}

	return rc;
}

static int mtk_cps2011s_dump_register(struct charger_device *chg_dev)
{
	struct cps2011s_chip *chip = charger_get_data(chg_dev);

	cps2011s_dump_register(chip);

	return 0;
}

__maybe_unused static int cps2011s_read_device_id(struct charger_device *chg_dev)
{
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	int rc, dev_id;
	u8 result = 0;

	rc = cps2011s_i2c_read8(chip, cps2011s_REG_DEVINFO, &result);
	if (rc < 0) {
		dev_err(chip->dev, "%s I2CR[0x%02X] fail\n", __func__, rc);
		return rc;
	}

	dev_id = (result & 0x0F);
	dev_info(chip->dev, "%s device ID: 0x%x\n", __func__, dev_id);

	return dev_id;
}

static int cps2011s_enable_adc(struct cps2011s_chip *chip, bool enable)
{
	int ret = 0;

	dev_info(chip->dev, "%s: %d", __func__, enable);

	if (enable) {
		ret = cps2011s_i2c_write8(chip, cps2011s_REG_ADCCTRL, 0x80);
	} else {
		ret = cps2011s_i2c_write8(chip, cps2011s_REG_ADCCTRL, 0x00);
	}

	if (ret < 0) {
		dev_err(chip->dev, "%s failed(%d)\n", __func__, ret);
	}

	return ret;
}

static int cps2011s_get_adc_enabled(struct cps2011s_chip *chip, bool *enable)
{
	int ret = 0;
	u8 reg_val = 0;

	ret = cps2011s_i2c_read8(chip, cps2011s_REG_ADCCTRL, &reg_val);
	if (ret < 0) {
		dev_err(chip->dev, "%s failed(%d)\n", __func__, ret);
	} else {
		*enable = !!reg_val;
	}

	dev_info(chip->dev, "%s: %d", __func__, *enable);

	return ret;
}


static int cps2011s_enable_comparators(struct cps2011s_chip *chip, bool enable)
{
	int ret = 0;

	dev_info(chip->dev, "%s: %d", __func__, enable);

	if (enable) {
		ret = cps2011s_i2c_update_bits(chip, cps2011s_REG_CONTROL3,
						cps2011s_COMP_EN_MASK, cps2011s_COMP_EN_MASK);
	} else {
		ret = cps2011s_i2c_update_bits(chip, cps2011s_REG_CONTROL3,
						0x00, cps2011s_COMP_EN_MASK);
	}

	if (ret < 0) {
		dev_err(chip->dev, "%s failed(%d)\n", __func__, ret);
	}

	return ret;
}

#if IS_ENABLED(CONFIG_OEM_CHARGER_PUMP)
static int mtk_cps2011s_enable_adc(struct charger_device *chg_dev, bool enable)
{
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	int ret = 0;

	dev_info(chip->dev, "%s: %d", __func__, enable);

	ret = cps2011s_enable_adc(chip, enable);
	ret = cps2011s_enable_comparators(chip, enable);

	return ret;
}

static int mtk_cps2011s_is_adc_enabled(struct charger_device *chg_dev, bool *enable)
{
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	int ret = 0;

	ret = cps2011s_get_adc_enabled(chip, enable);

	return ret;
}
#endif /* CONFIG_OEM_CHARGER_PUMP */

static int cps2011s_get_adc(struct charger_device *chg_dev,
			enum adc_channel chan, int *min, int *max)
{
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	enum cps2011s_adc_channel _chan = to_cps2011s_adc(chan);
	int ret;

#if 0
	u8 data;
	int i=0;
	for(i=0;i<0x19;i++)
	{
		__cps2011s_i2c_read8(chip, i, &data);
		dev_info(chip->dev, "%s read register 0x%x=0x%x\n", __func__,i,data);
	}
	for(i=0x40;i<0x50;i++)
	{
		__cps2011s_i2c_read8(chip, i, &data);
		dev_info(chip->dev, "%s read register 0x%x=0x%x\n", __func__,i,data);
	}
#endif
	if (_chan == cps2011s_ADC_NOTSUPP) {
		dev_info(chip->dev, "%s _chan error\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&chip->adc_lock);
	ret = __cps2011s_get_adc(chip, _chan, max);
	if (ret < 0)
		goto out;

	if (min != max)
		*min = *max;
out:
	mutex_unlock(&chip->adc_lock);
	return ret;
}

static int cps2011s_get_adc_accuracy(struct charger_device *chg_dev,
			enum adc_channel chan, int *min, int *max)
{
	enum cps2011s_adc_channel _chan = to_cps2011s_adc(chan);

	if (_chan == cps2011s_ADC_NOTSUPP)
		return -EINVAL;

	*min = *max = cps2011s_adc_accuracy_tbl[_chan];

	return 0;
}

/////////////////////////////////////////////////////////////////////////
static int cps2011s_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
#if 1
	struct cps2011s_chip *chip = charger_get_data(chg_dev);

	u8 reg = cps2011s_vbusovp_toreg(uV);
	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uV, reg);

	return cps2011s_i2c_update_bits(chip, cps2011s_REG_VBUSOVP, reg,
				      cps2011s_VBUSOVP_MASK);

#endif
     return 0;
}

static int cps2011s_set_2to1_ibusocp(struct charger_device *chg_dev, u32 uA)
{
#if 1
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	u8 reg = cps2011s_ibusocp_2to1_toreg(uA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uA, reg);
	return cps2011s_i2c_update_bits(chip, cps2011s_REG_IBUSOCUCP, reg,
				      cps2011s_IBUSOCP_MASK);
#endif
     return 0;
}

__maybe_unused static int cps2011s_set_bypass_ibusocp(struct charger_device *chg_dev, u32 uA)
{
#if 1
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	u8 reg = cps2011s_ibusocp_bypass_toreg(uA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uA, reg);
	return cps2011s_i2c_update_bits(chip, cps2011s_REG_IBUSOCUCP, reg,
				      cps2011s_IBUSOCP_MASK);
#endif
     return 0;
}

static int cps2011s_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
#if 1
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	u8 reg = cps2011s_vbatovp_toreg(uV);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uV, reg);
	return cps2011s_i2c_update_bits(chip, cps2011s_REG_VBATOVP, reg,
				      cps2011s_VBATOVP_MASK);
#endif
    //return 0;
}

static int cps2011s_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
   return 0;
}


static int cps2011s_reset_vbatovp_alarm(struct charger_device *chg_dev)
{
    return 0;
}

static int cps2011s_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{

    return 0;
}

static int cps2011s_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
    return 0;
}

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
static int mtk_cps2011s_is_vbushigher(struct charger_device *chg_dev, bool *err)
{
    return 0;
}

static int mtk_cps2011s_is_vbat_present(struct charger_device *chg_dev, bool *err)
{
    return 0;
}

static int mtk_cps2011s_is_vbus_present(struct charger_device *chg_dev, bool *err)
{
    return 0;
}
#endif /* CONFIG_OEM_TURBO_CHARGER */

static int cps2011s_reset_vbusovp_alarm(struct charger_device *chg_dev)
{
    return 0;
}

static int cps2011s_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
#if 1
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	u8 reg = cps2011s_ibatocp_toreg(uA);

	dev_info(chip->dev, "%s %d(0x%02X)\n", __func__, uA, reg);
	return cps2011s_i2c_update_bits(chip, cps2011s_REG_IBATOCP, reg,
					cps2011s_IBATOCP_MASK);
#endif
     return 0;
}

static int cps2011s_init_chip(struct charger_device *chg_dev)
{

#if 0
	int i, ret;
	struct cps2011s_chip *chip = charger_get_data(chg_dev);
	const struct cps2011s_reg_defval *reg_defval;
	u8 val;

	for (i = 0; i < ARRAY_SIZE(cps2011s_init_chip_check_reg); i++) {
		reg_defval = &cps2011s_init_chip_check_reg[i];
		ret = cps2011s_i2c_read8(chip, reg_defval->reg, &val);
		if (ret < 0)
			return ret;
		if ((val & reg_defval->mask) == reg_defval->value) {
			dev_info(chip->dev,
				"%s chip reset happened, reinit\n", __func__);
			return __cps2011s_init_chip(chip);
		}
	}
#endif
    struct cps2011s_chip *chip = charger_get_data(chg_dev);

    return __cps2011s_init_chip(chip);
}

//#endif
static inline void cps2011s_set_notify(struct cps2011s_chip *chip,
				     enum cps2011s_notify notify)
{
	mutex_lock(&chip->notify_lock);
	chip->notify |= BIT(notify);
	mutex_unlock(&chip->notify_lock);
}

static int cps2011s_ibusucpf_irq_handler(struct cps2011s_chip *chip)
{
	bool ucpf = !!(chip->stat & BIT(cps2011s_IRQIDX_IBUSUCPF));

	dev_info(chip->dev, "%s %d\n", __func__, ucpf);
	if (ucpf)
		cps2011s_set_notify(chip, cps2011s_NOTIFY_IBUSUCPF);

	return 0;
}

static int cps2011s_ibusucpr_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(cps2011s_IRQIDX_IBUSUCPR)));

	return 0;
}

//Antaiui <AI_BSP_CHG> <cps> <2021-03-01> modify For 2005R 30w fast charger end
static int cps2011s_ibusocp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	//cps2011s_set_notify(chip, cps2011s_NOTIFY_IBUSOCP);
	return 0;
}

static int cps2011s_vbusovp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	//cps2011s_set_notify(chip, cps2011s_NOTIFY_VBUSOVP);
	return 0;
}

static int cps2011s_vdrovp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	//cps2011s_set_notify(chip, cps2011s_NOTIFY_VDROVP);
	return 0;
}

static int cps2011s_vbuspd_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(cps2011s_IRQIDX_VBUSPD)));

	return 0;
}

static int cps2011s_vbusconpd_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(cps2011s_IRQIDX_VBUSCONPD)));

	return 0;
}

static int cps2011s_vbusconovp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(cps2011s_IRQIDX_VBUSCONOVP)));

	return 0;
}

static int cps2011s_converocp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(cps2011s_IRQIDX_CONVOCP)));

	return 0;
}

static int cps2011s_rltovp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(cps2011s_IRQIDX_RLTOVP)));

	return 0;
}

static int cps2011s_rltuvp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(cps2011s_IRQIDX_RLTUVP)));

	return 0;
}

static int cps2011s_thermaloff_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s %d\n", __func__,
		 !!(chip->stat & BIT(cps2011s_IRQIDX_THERMALOFF)));

	return 0;
}

static int cps2011s_ibatreg_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int cps2011s_vbatreg_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int cps2011s_ibatocp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	//cps2011s_set_notify(chip, cps2011s_NOTIFY_IBATOCP);
	return 0;
}

static int cps2011s_vbatovp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	//cps2011s_set_notify(chip, cps2011s_NOTIFY_VBATOVP);
	return 0;
}

static int cps2011s_fcapscp_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	//cps2011s_set_notify(chip, cps2011s_NOTIFY_VBATOVP);
	return 0;
}

static int cps2011s_adcdone_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int cps2011s_chgontiout_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int cps2011s_vbusuvlo_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int cps2011s_vbusconuvlo_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int cps2011s_wdttiout_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int cps2011s_vbatpowerok_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int cps2011s_vbuspowerok_irq_handler(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

//reigster
static ssize_t cps2011s_show_registers(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct cps2011s_chip *chip = dev_get_drvdata(dev);
	uint8_t addr;
	uint8_t tmpbuf[300];
	int len;
	int idx = 0;
	int ret = 0;
	u8 data;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "cps2011s");

	for (addr = 0x00; addr < 0x1C; addr++) {
		ret = cps2011s_i2c_read8(chip, addr, &data);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, data);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	for (addr = 0xE0; addr < 0xF5; addr++) {
		ret = cps2011s_i2c_read8(chip, addr, &data);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, data);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t cps2011s_store_register(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct cps2011s_chip *chip = dev_get_drvdata(dev);
	int ret;
	int val;
	unsigned int reg;
	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && ((reg >= 0x00 && reg < 0x1C) || (reg >= 0xE0 && reg < 0xF5)))
		cps2011s_i2c_write8(chip, reg, (u8)val);

	return count;
}

static DEVICE_ATTR(registers, 0660, cps2011s_show_registers, cps2011s_store_register);

static void cps2011s_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

struct irq_map_desc {
	const char *name;
	int (*hdlr)(struct cps2011s_chip *chip);
	u8 flag_idx;
	u8 stat_idx;
	u8 flag_mask;
	u8 stat_mask;
	u32 irq_idx;
	bool stat_only;
};

#define cps2011s_IRQ_DESC(_name, _flag_i, _stat_i, _flag_s, _stat_s, _irq_idx, \
			_stat_only) \
	{.name = #_name, .hdlr = cps2011s_##_name##_irq_handler, \
	 .flag_idx = _flag_i, .stat_idx = _stat_i, \
	 .flag_mask = (1 << _flag_s), .stat_mask = (1 << _stat_s), \
	 .irq_idx = _irq_idx, .stat_only = _stat_only}

/*
 * RSS: Reister index of flag, Shift of flag, Shift of state
 * RRS: Register index of flag, Register index of state, Shift of flag
 * RS: Register index of flag, Shift of flag
 * RSSO: Register index of state, Shift of state, State Only
 */
#define cps2011s_IRQ_DESC_RSS(_name, _flag_i, _flag_s, _stat_s, _irq_idx) \
	cps2011s_IRQ_DESC(_name, _flag_i, _flag_i, _flag_s, _stat_s, _irq_idx, \
			false)

#define cps2011s_IRQ_DESC_RRS(_name, _flag_i, _stat_i, _flag_s, _irq_idx) \
	cps2011s_IRQ_DESC(_name, _flag_i, _stat_i, _flag_s, _flag_s, _irq_idx, \
			false)

#define cps2011s_IRQ_DESC_RS(_name, _flag_i, _flag_s, _irq_idx) \
	cps2011s_IRQ_DESC(_name, _flag_i, _flag_i, _flag_s, _flag_s, _irq_idx, \
			false)

#define cps2011s_IRQ_DESC_RSSO(_name, _flag_i, _flag_s, _irq_idx) \
	cps2011s_IRQ_DESC(_name, _flag_i, _flag_i, _flag_s, _flag_s, _irq_idx, \
			true)

static const struct irq_map_desc cps2011s_irq_map_tbl[cps2011s_IRQIDX_MAX] = {
	cps2011s_IRQ_DESC_RS(ibusucpf, cps2011s_SF_INTFLAG1, 0,
				cps2011s_IRQIDX_IBUSUCPF),
	cps2011s_IRQ_DESC_RS(ibusucpr, cps2011s_SF_INTFLAG1, 1,
				cps2011s_IRQIDX_IBUSUCPR),
	cps2011s_IRQ_DESC_RS(ibusocp, cps2011s_SF_INTFLAG1, 2,
				cps2011s_IRQIDX_IBUSOCP),
	cps2011s_IRQ_DESC_RS(vbusovp, cps2011s_SF_INTFLAG1, 3,
				cps2011s_IRQIDX_VBUSOVP),
	cps2011s_IRQ_DESC_RS(vdrovp, cps2011s_SF_INTFLAG1, 4,
				cps2011s_IRQIDX_VDROVP),
	cps2011s_IRQ_DESC_RS(vbuspd, cps2011s_SF_INTFLAG1, 5,
				cps2011s_IRQIDX_VBUSPD),
	cps2011s_IRQ_DESC_RS(vbusconpd, cps2011s_SF_INTFLAG1, 6,
				cps2011s_IRQIDX_VBUSCONPD),
	cps2011s_IRQ_DESC_RS(vbusconovp, cps2011s_SF_INTFLAG1, 7,
				cps2011s_IRQIDX_VBUSCONOVP),
	cps2011s_IRQ_DESC_RS(converocp, cps2011s_SF_INTFLAG2, 0,
				cps2011s_IRQIDX_CONVOCP),
	cps2011s_IRQ_DESC_RS(rltovp, cps2011s_SF_INTFLAG2, 1,
				cps2011s_IRQIDX_RLTOVP),
	cps2011s_IRQ_DESC_RS(rltuvp, cps2011s_SF_INTFLAG2, 2,
				cps2011s_IRQIDX_RLTUVP),
	cps2011s_IRQ_DESC_RS(thermaloff, cps2011s_SF_INTFLAG2, 3,
				cps2011s_IRQIDX_THERMALOFF),
	cps2011s_IRQ_DESC_RS(ibatreg, cps2011s_SF_INTFLAG2, 4,
				cps2011s_IRQIDX_IBATREG),
	cps2011s_IRQ_DESC_RS(vbatreg, cps2011s_SF_INTFLAG2, 5,
				cps2011s_IRQIDX_VBATREG),
	cps2011s_IRQ_DESC_RS(ibatocp, cps2011s_SF_INTFLAG2, 6,
				cps2011s_IRQIDX_IBATOCP),
	cps2011s_IRQ_DESC_RS(vbatovp, cps2011s_SF_INTFLAG2, 7,
				cps2011s_IRQIDX_VBATOVP),
	cps2011s_IRQ_DESC_RS(fcapscp, cps2011s_SF_INTFLAG3, 0,
				cps2011s_IRQIDX_FCAPSCP),
	cps2011s_IRQ_DESC_RS(adcdone, cps2011s_SF_INTFLAG3, 1,
				cps2011s_IRQIDX_ADCDONE),
	cps2011s_IRQ_DESC_RS(chgontiout, cps2011s_SF_INTFLAG3, 2,
				cps2011s_IRQIDX_CHGONTMR),
	cps2011s_IRQ_DESC_RS(vbusuvlo, cps2011s_SF_INTFLAG3, 3,
				cps2011s_IRQIDX_VBUSUVLO),
	cps2011s_IRQ_DESC_RS(vbusconuvlo, cps2011s_SF_INTFLAG3, 4,
				cps2011s_IRQIDX_VBUSCONUVLO),
	cps2011s_IRQ_DESC_RS(wdttiout, cps2011s_SF_INTFLAG3, 5,
				cps2011s_IRQIDX_WDTOUT),
	cps2011s_IRQ_DESC_RS(vbatpowerok, cps2011s_SF_INTFLAG3, 6,
				cps2011s_IRQIDX_VBATPOWEROK),
	cps2011s_IRQ_DESC_RS(vbuspowerok, cps2011s_SF_INTFLAG3, 7,
				cps2011s_IRQIDX_VBUSPOWEROK),
};

static int __cps2011s_update_status(struct cps2011s_chip *chip)
{
	int i;
	u8 sf[cps2011s_SF_MAX] = {0};
	const struct irq_map_desc *desc;

	for (i = 0; i < cps2011s_SF_MAX; i++)
		cps2011s_i2c_read8(chip, cps2011s_reg_sf[i], &sf[i]);

	for (i = 0; i < ARRAY_SIZE(cps2011s_irq_map_tbl); i++) {

		desc = &cps2011s_irq_map_tbl[i];

		if (sf[desc->flag_idx] & desc->flag_mask) {
			if (!desc->stat_only)
				chip->flag |= BIT(desc->irq_idx);
		}

		if (sf[desc->stat_idx] & desc->stat_mask) {
			if (desc->stat_only &&
			    !(chip->stat & BIT(desc->irq_idx)))
				chip->flag |= BIT(desc->irq_idx);
			chip->stat |= BIT(desc->irq_idx);
		} else {
			if (desc->stat_only &&
			    (chip->stat & BIT(desc->irq_idx)))
				chip->flag |= BIT(desc->irq_idx);
			chip->stat &= ~BIT(desc->irq_idx);
		}
	}

	return 0;
}

__maybe_unused static int cps2011s_update_status(struct cps2011s_chip *chip)
{
	int ret;

	mutex_lock(&chip->stat_lock);
	ret = __cps2011s_update_status(chip);
	mutex_unlock(&chip->stat_lock);

	return ret;
}

static int cps2011s_notify_task_threadfn(void *data)
{
	int i;
	struct cps2011s_chip *chip = data;
	printk("cps2011s_notify_task_threadfn===\n");

	while (!kthread_should_stop()) {
		wait_event_interruptible(chip->wq, chip->notify != 0 ||
					 kthread_should_stop());
		if (kthread_should_stop())
			goto out;
		pm_stay_awake(chip->dev);
		mutex_lock(&chip->notify_lock);

		for (i = 0; i < cps2011s_NOTIFY_MAX; i++) {
			if (chip->notify & BIT(i)) {
				chip->notify &= ~BIT(i);
				mutex_unlock(&chip->notify_lock);
				charger_dev_notify(chip->chg_dev,
						   cps2011s_chgdev_notify_map[i]);
				mutex_lock(&chip->notify_lock);
			}
		}
		mutex_unlock(&chip->notify_lock);
		pm_relax(chip->dev);
	}
out:
	return 0;
}

static irqreturn_t cps2011s_irq_handler(int irq, void *data)
{
	int i;
	struct cps2011s_chip *chip = data;
	const struct irq_map_desc *desc;

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->stat_lock);
	__cps2011s_update_status(chip);
	for (i = 0; i < ARRAY_SIZE(cps2011s_irq_map_tbl); i++) {
		desc = &cps2011s_irq_map_tbl[i];
		if ((chip->flag & (1 << desc->irq_idx)) && desc->hdlr)
			desc->hdlr(chip);
	}
	chip->flag = 0;
	wake_up_interruptible(&chip->wq);
	mutex_unlock(&chip->stat_lock);
	pm_relax(chip->dev);

	return IRQ_HANDLED;
}

static const struct charger_ops cps2011s_chg_ops = {
	.enable = cps2011s_enable_chg,
	//.enable_chg_bypass = cps2011s_enable_chg_bypass,
	.is_enabled = cps2011s_is_chg_enabled,
	.get_adc = cps2011s_get_adc,
	.set_vbusovp = cps2011s_set_vbusovp,
	.set_ibusocp = cps2011s_set_2to1_ibusocp,
	.set_vbatovp = cps2011s_set_vbatovp,
	.set_ibatocp = cps2011s_set_ibatocp,
	.init_chip = cps2011s_init_chip,
	.dump_registers = mtk_cps2011s_dump_register,
	//.device_id = cps2011s_read_device_id
	//.set_switch = cps2011s_set_switch_clk,
	//.enable_wdt = cps2011s_enable_wdt,
#if IS_ENABLED(CONFIG_OEM_CHARGER_PUMP)
	.enable_ovpgate = cps2011s_enable_ovpgate,
	.enable_adc = mtk_cps2011s_enable_adc,
	.is_adc_enabled = mtk_cps2011s_is_adc_enabled,
#endif /* CONFIG_OEM_CHARGER_PUMP */
/////////////////////////////////////////////////////////////
	.set_vbatovp_alarm = cps2011s_set_vbatovp_alarm,
	.reset_vbatovp_alarm = cps2011s_reset_vbatovp_alarm,
	.set_vbusovp_alarm = cps2011s_set_vbusovp_alarm,
	.reset_vbusovp_alarm = cps2011s_reset_vbusovp_alarm,
//////////////////////////////////////////////////////////
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	.is_vbushigher = mtk_cps2011s_is_vbushigher,
	.is_vbat_present = mtk_cps2011s_is_vbat_present,
	.is_vbus_present = mtk_cps2011s_is_vbus_present,
#endif /* CONFIG_OEM_TURBO_CHARGER */
	.is_vbuslowerr = cps2011s_is_vbuslowerr,
	.get_adc_accuracy = cps2011s_get_adc_accuracy,
};

static int cps2011s_register_chgdev(struct cps2011s_chip *chip)
{
	chip->chg_prop.alias_name = chip->desc->chg_name;
	chip->chg_dev = charger_device_register(chip->desc->chg_name,
				chip->dev, chip, &cps2011s_chg_ops, &chip->chg_prop);

	if (!chip->chg_dev)
		return -EINVAL;

	return 0;
}

static int cps2011s_clearall_irq(struct cps2011s_chip *chip)
{
	int i, ret;
	u8 data;

	for (i = 0; i < cps2011s_SF_MAX; i++) {
		ret = cps2011s_i2c_read8(chip, cps2011s_reg_sf[i], &data);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int cps2011s_init_irq(struct cps2011s_chip *chip)
{
	int ret = 0;

	dev_info(chip->dev, "%s\n", __func__);
	ret = cps2011s_clearall_irq(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s clr all irq fail(%d)\n", __func__, ret);
		return ret;
	}

	if (chip->type == cps2011s_TYPE_SLAVE)
		return 0;


	if (gpio_is_valid(chip->irq_gpio)) {
		ret = gpio_request_one(chip->irq_gpio, GPIOF_DIR_IN, "cps2011s_irq");
		if (ret) {
			dev_err(chip->dev,"%s failed to request cps2011s_irq\n", __func__);
			return -EINVAL;
		}
	}

	chip->irq = gpio_to_irq(chip->irq_gpio);
	if (chip->irq < 0) {
		dev_err(chip->dev, "%s irq mapping fail(%d)\n", __func__, chip->irq);
		return ret;
	}

	dev_info(chip->dev, "%s irq = %d\n", __func__, chip->irq);

	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
				cps2011s_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "cps2011s_irq", chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s request thread irq fail(%d)\n", __func__, ret);
		return ret;
	}

	device_init_wakeup(chip->dev, true);

	return 0;
}

#define cps2011s_DT_VALPROP(name, reg, shft, mask, func, base) \
	{#name, offsetof(struct cps2011s_dese, name), reg, shft, mask, func, base}


static int cps2011s_parse_dt(struct cps2011s_chip *chip)
{
	struct cps2011s_dese *desc;
	struct device_node *np = chip->dev->of_node;
//	struct device_node *child_np;

	if (!np) {
		dev_err(chip->dev, "%s get dev node failed\n", __func__);
		return -ENODEV;
	}

//	if (chip->type == cps2011s_TYPE_SLAVE)
//		goto ignore_intr;

	chip->irq_gpio = of_get_named_gpio(np, "cps2011s,intr", 0);
	if (!gpio_is_valid(chip->irq_gpio)) {
		dev_err(chip->dev, "%s get intr gpio failed\n", __func__);
		return -ENODEV;;
	}

//ignore_intr:
	desc = devm_kzalloc(chip->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc) {
		dev_err(chip->dev, "%s malloc mem failed\n", __func__);
		return -ENOMEM;
	}

	memcpy(desc, &cps2011s_dese_defval, sizeof(*desc));

	if (of_property_read_string(np, "rm_name", &desc->rm_name) < 0)
		dev_err(chip->dev, "%s no rm name\n", __func__);

	chip->desc = desc;

	return 0;
}

static int __cps2011s_init_chip(struct cps2011s_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);

	cps2011s_i2c_write8(chip, 0x01, 0x40);   //switch frequency 500KHz
	cps2011s_i2c_write8(chip, 0x00, 0x08);   //disable watchdog
	cps2011s_i2c_write8(chip, 0x02, 0xB2);	//disable ENCOMP and set RLT UVP/OVP
	cps2011s_i2c_write8(chip, 0x04, 0x18);	//set vbuscon ovp 12V and vbuscon ovp enable
	cps2011s_i2c_write8(chip, 0x06, 0xCB);	//set vbus ovp 11.5V and vbus ovp enable
	cps2011s_i2c_write8(chip, 0x07, 0xB5);	//set ibus ucp/ocp enable & ibusocp 5A
	cps2011s_i2c_write8(chip, 0x08, 0x9D);	//set vbatovp is 4.725V & enable
	cps2011s_i2c_write8(chip, 0x09, 0x2A);	//set ibatocp disabled
	cps2011s_i2c_write8(chip, 0x0A, 0x00);  //set vbatreg and ibatreg disable
	cps2011s_i2c_write8(chip, 0xE2, 0x00);  //set Automatic DPDM detection disable

	cps2011s_dump_register(chip);
	return 0;
}

static int cps2011s_check_devinfo(struct i2c_client *client,
					u8 *chip_rev, enum cps2011s_type *type)
{
	int ret = 0;

	ret = i2c_smbus_read_byte_data(client, cps2011s_DEVID);
	if (ret < 0) {
		dev_err(&client->dev, "%s i2c read error\n", __func__);
		return ret;
	}

	*chip_rev = ret & 0xff;

	dev_info(&client->dev, "%s devid(0x%02X)\n", __func__, *chip_rev);

	if (!(*chip_rev == cps2011s_ID1 || *chip_rev == cps2011s_ID2))
		ret = -ENODEV;

	return ret;
}

static enum power_supply_property cps2011s_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static int cps2011s_charger_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct cps2011s_chip *chip = power_supply_get_drvdata(psy);
	int result;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		cps2011s_is_chg_enabled(chip->chg_dev, &chip->charge_enabled);
		val->intval = chip->charge_enabled;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = __cps2011s_get_adc(chip, cps2011s_ADC_VBUS, &result);
		if (ret >= 0)
			chip->vbus_volt = result;
		val->intval = chip->vbus_volt;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = __cps2011s_get_adc(chip, cps2011s_ADC_IBUS, &result);
		if (ret >= 0)
			chip->ibus_curr = result;
		val->intval = chip->ibus_curr;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = __cps2011s_get_adc(chip, cps2011s_ADC_VBAT, &result);
		if (ret >= 0)
			chip->vbat_volt = result;
		val->intval = chip->vbat_volt;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = __cps2011s_get_adc(chip, cps2011s_ADC_IBAT, &result);
		if (ret >= 0)
			chip->ibat_curr = result;
		val->intval = chip->ibat_curr;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = __cps2011s_get_adc(chip, cps2011s_ADC_TDIE, &result);
		if (ret >= 0)
			chip->die_temp = result;
		val->intval = chip->die_temp;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "ConvenientPower";
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cps2011s_charger_set_property(struct power_supply *psy,
	enum power_supply_property prop,
	const union power_supply_propval *val)
{
	struct cps2011s_chip *chip = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		cps2011s_enable_chg(chip->chg_dev, val->intval);
		dev_info(chip->dev, "POWER_SUPPLY_PROP_ONLINE: %s\n",
			val->intval ? "enable" : "disable");
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cps2011s_charger_is_writeable(struct power_supply *psy,
					enum power_supply_property prop)
{
	return 0;
}

static int cps2011s_psy_register(struct cps2011s_chip *chip)
{
	chip->psy_cfg.drv_data = chip;
	chip->psy_cfg.of_node = chip->dev->of_node;

	chip->psy_desc.name = "cp-standalone";

	chip->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	chip->psy_desc.properties = cps2011s_charger_props;
	chip->psy_desc.num_properties = ARRAY_SIZE(cps2011s_charger_props);
	chip->psy_desc.get_property = cps2011s_charger_get_property;
	chip->psy_desc.set_property = cps2011s_charger_set_property;
	chip->psy_desc.property_is_writeable = cps2011s_charger_is_writeable;


	chip->psy = devm_power_supply_register(chip->dev, &chip->psy_desc, &chip->psy_cfg);
	if (IS_ERR(chip->psy)) {
		dev_err(chip->dev, "%s failed to register psy\n", __func__);
		return PTR_ERR(chip->psy);
	}

	dev_info(chip->dev, "%s power supply register successfully\n", chip->psy_desc.name);

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static int cps2011s_i2c_probe(struct i2c_client *client)
#else
static int cps2011s_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
#endif
{
	int ret;
	struct cps2011s_chip *chip;
	u8 chip_rev;
	enum cps2011s_type type;

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	if (oem_pcba_charge_power() != CHARGE_POWER_33W) {
		pr_err("found 18W device, not init cps2011s\n");
		return -ENODEV;
	}
#endif

	dev_info(&client->dev, "%s(%s)\n", __func__, cps2011s_DRV_VERSION);//	"1.0.8_MTK"

	ret = cps2011s_check_devinfo(client, &chip_rev, &type);
	if (ret < 0)
		return ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->dev = &client->dev;
	chip->client = client;
	chip->revision = chip_rev;
	chip->type = cps2011s_TYPE_STANDALONE;//type;
	mutex_init(&chip->io_lock);
	mutex_init(&chip->adc_lock);
	mutex_init(&chip->stat_lock);
	mutex_init(&chip->hm_lock);
	mutex_init(&chip->suspend_lock);
	mutex_init(&chip->notify_lock);
	init_waitqueue_head(&chip->wq);
	i2c_set_clientdata(client, chip);

	ret = cps2011s_parse_dt(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s parse dt fail(%d)\n", __func__, ret);
		goto err;
	}

#ifdef CONFIG_RT_REGMAP
	ret = cps2011s_register_regmap(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s reg regmap fail(%d)\n",
			__func__, ret);
		goto err;
	}
#endif /* CONFIG_RT_REGMAP */

	ret = __cps2011s_init_chip(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s init chip fail(%d)\n", __func__, ret);
		goto err_initchip;
	}

	ret = cps2011s_register_chgdev(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s reg chgdev fail(%d)\n",
			__func__, ret);
		goto err_initchip;
	}

	ret = cps2011s_psy_register(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s psy register failed(%d)\n", __func__, ret);
		goto err_register_psy;
	}

	chip->notify_task = kthread_run(cps2011s_notify_task_threadfn, chip,
					"notify_thread");
	if (IS_ERR(chip->notify_task)) {
		dev_notice(chip->dev, "%s run notify thread fail(%d)\n",
			__func__, ret);
		ret = PTR_ERR(chip->notify_task);
		goto err_initirq;
	}

	ret = cps2011s_init_irq(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s init irq fail(%d)\n", __func__, ret);
		goto err_initirq;
	}

	cps2011s_create_device_node(&(client->dev));

	#ifdef CONFIG_AI_BSP_MTK_DEVICE_CHECK
	{
		#include <linux/ai_device_check.h>
		struct ai_device_info ai_chg_hw_info;
		ai_chg_hw_info.ai_dev_type = AI_DEVICE_TYPE_CHARGER;
		strcpy(ai_chg_hw_info.name, "cps2011s");
		ai_set_device_info(ai_chg_hw_info);
	}
	#endif

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	FULL_PRODUCT_DEVICE_INFO(ID_CHARGER_PUMP, "CPS2011S");
#endif

	dev_info(chip->dev, "%s successfully\n", __func__);
	return 0;

err_initirq:
	charger_device_unregister(chip->chg_dev);
err_register_psy:
	power_supply_unregister(chip->psy);
err_initchip:

#if 0
#ifdef CONFIG_CPS_REGMAP
	cps_regmap_device_unregister(chip->rm_dev);
#endif /* CONFIG_CPS_REGMAP */
#endif

err:
	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->suspend_lock);
	mutex_destroy(&chip->hm_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);
	return ret;
}

static void cps2011s_i2c_shutdown(struct i2c_client *client)
{
	dev_info(&client->dev, "%s\n", __func__);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static void cps2011s_i2c_remove(struct i2c_client *client)
#else
static int cps2011s_i2c_remove(struct i2c_client *client)
#endif
{
	struct cps2011s_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s\n", __func__);

	if (!chip)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
		return;
#else
		return 0;
#endif

	if (chip->notify_task)
		kthread_stop(chip->notify_task);

	power_supply_unregister(chip->psy);
	charger_device_unregister(chip->chg_dev);

#if 0
#ifdef CONFIG_CPS_REGMAP
	cps_regmap_device_unregister(chip->rm_dev);
#endif /* CONFIG_CPS_REGMAP */
#endif

	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->suspend_lock);
	mutex_destroy(&chip->hm_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	return;
#else
	return 0;
#endif
}

static int __maybe_unused cps2011s_i2c_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct cps2011s_chip *chip = i2c_get_clientdata(i2c);

	dev_info(dev, "%s\n", __func__);
	mutex_lock(&chip->suspend_lock);
	if (device_may_wakeup(dev))
		enable_irq_wake(chip->irq);
	return 0;
}

static int __maybe_unused cps2011s_i2c_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct cps2011s_chip *chip = i2c_get_clientdata(i2c);

	dev_info(dev, "%s\n", __func__);
	mutex_unlock(&chip->suspend_lock);
	if (device_may_wakeup(dev))
		disable_irq_wake(chip->irq);
	return 0;
}

static SIMPLE_DEV_PM_OPS(cps2011s_pm_ops, cps2011s_i2c_suspend, cps2011s_i2c_resume);

static const struct of_device_id cps2011s_of_id[] = {
	{ .compatible = "CPS,cps2011s" },
	{},
};
MODULE_DEVICE_TABLE(of, cps2011s_of_id);

static const struct i2c_device_id cps2011s_i2c_id[] = {
	{ "cps2011s", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, cps2011s_i2c_id);

static struct i2c_driver cps2011s_i2c_driver = {
	.driver = {
		.name = "cps2011s",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cps2011s_of_id),
		.pm = &cps2011s_pm_ops,
	},
	.probe = cps2011s_i2c_probe,
	.shutdown = cps2011s_i2c_shutdown,
	.remove = cps2011s_i2c_remove,
	.id_table = cps2011s_i2c_id,
};
module_i2c_driver(cps2011s_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPS cps2011s Charger Driver");
MODULE_AUTHOR("Chengfeng Lai<XiChengfeng.Lai@convenientpower.com>");
MODULE_VERSION(cps2011s_DRV_VERSION);

/*
 * 1.0.0_MTK
 * Initial release
 *
 * 1.0.1_MTK
 * The initial configuration was updated
 *
 */
