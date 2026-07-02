/* SPDX-License-Identifier: GPL-2.0-only */
// cx2589x Charger Driver
// Copyright (C) 2023 Suncore Corp. - http://www.cx-semi.com.cn

#ifndef _CX2589x_CHARGER_H__
#define _CX2589x_CHARGER_H__

#include <linux/i2c.h>

#define CX2589x_MANUFACTURER	"SUNCORE"


#define CX2589x_NAME		"cx25890h"
#define CX2589x_PN_ID		(BIT(4)| BIT(3))


/*define register*/
#define CX2589x_REG_00 0x00
#define CX2589x_REG_01 0x01
#define CX2589x_REG_02 0x02
#define CX2589x_REG_03 0x03
#define CX2589x_REG_04 0x04
#define CX2589x_REG_05 0x05
#define CX2589x_REG_06 0x06
#define CX2589x_REG_07 0x07
#define CX2589x_REG_08 0x08
#define CX2589x_REG_09 0x09
#define CX2589x_REG_0A 0x0A
#define CX2589x_REG_0B 0x0B
#define CX2589x_REG_0C 0x0C
#define CX2589x_REG_0D 0x0D
#define CX2589x_REG_0E 0x0E
#define CX2589x_REG_0F 0x0F
#define CX2589x_REG_10 0x10
#define CX2589x_REG_11 0x11
#define CX2589x_REG_12 0x12
#define CX2589x_REG_13 0x13
#define CX2589x_REG_14 0x14
#define CX2589x_REG_15 0x15

/*reset chip*/
#define CX2589x_RESET_REG	BIT(7)

/* charge status flags  */
#define CX2589x_CHRG_EN		BIT(4)
#define CX2589x_HIZ_EN		BIT(7)
#define CX2589x_TERM_EN		BIT(7)
//#define CX2589x_VAC_OVP_MASK	GENMASK(7, 6)
//#define CX2589x_DPDM_ONGOING	BIT(7)
#define CX2589x_PG_STAT 	BIT(2)
#define CX2589x_VBUS_GOOD	BIT(7)

/*OTG*/
#define CX2589x_BOOSTV		GENMASK(7, 4)
#define CX2589x_BOOST_LIM	GENMASK(2, 0)
#define CX2589x_OTG_EN		BIT(5)

/* Part ID  */
#define CX2589x_PN_MASK		GENMASK(5, 3)

/* WDT TIMER SET  */
#define CX2589x_WDT_TIMER_MASK		GENMASK(5, 4)
#define CX2589x_WDT_TIMER_DISABLE	0
#define CX2589x_WDT_TIMER_40S		BIT(4)
#define CX2589x_WDT_TIMER_80S		BIT(5)
#define CX2589x_WDT_TIMER_160S		(BIT(4)| BIT(5))

#define CX2589x_WDT_RST_MASK		BIT(6)

/* SAFETY TIMER SET  */
#define CX2589x_SAFETY_TIMER_MASK	GENMASK(2, 1)
#define CX2589x_SAFETY_TIMER_DISABLE	0
#define CX2589x_SAFETY_TIMER_EN		BIT(3)
#define CX2589x_SAFETY_TIMER_5H		0
#define CX2589x_SAFETY_TIMER_8H		BIT(1)
#define CX2589x_SAFETY_TIMER_12H	BIT(2)
#define CX2589x_SAFETY_TIMER_20H	(BIT(2)| BIT(1))


/* recharge voltage  */
#define CX2589x_VRECHARGE		BIT(0)
#define CX2589x_VRECHRG_STEP_mV		100
#define CX2589x_VRECHRG_OFFSET_mV	100

/* charge status  */
#define CX2589x_VSYS_STAT		BIT(0)
#define CX2589x_THERM_STAT		BIT(7)
#define CX2589x_PG_STAT		BIT(2)
#define CX2589x_CHG_STAT_MASK		GENMASK(4, 3)
#define CX2589x_NOT_CHRGING		0
#define CX2589x_PRECHRG		BIT(3)
#define CX2589x_FAST_CHRG		BIT(4)
#define CX2589x_TERM_CHRG		(BIT(3)| BIT(4))

/* charge type  */
#define CX2589x_VBUS_STAT_MASK		GENMASK(7, 5)
#define CX2589x_NO_INPUT	0
#define CX2589x_USB_SDP		BIT(5)
#define CX2589x_USB_CDP		BIT(6)
#define CX2589x_USB_DCP		(BIT(5) | BIT(6))
#define CX2589x_UNKNOWN		(BIT(7) | BIT(5))
#define CX2589x_NON_STANDARD		(BIT(7) | BIT(6))
#define CX2589x_OTG_MODE		(BIT(7) | BIT(6) | BIT(5))

/*charge fault*/
#define CX2589x_CHG_FAULT_MASK		GENMASK(5, 4)
#define CX2589x_BOOST_FAULT_MASK	GENMASK(6,6)


/* TEMP Status  */
#define CX2589x_TEMP_MASK		GENMASK(2, 0)
#define CX2589x_TEMP_NORMAL		0
#define CX2589x_TEMP_WARM		BIT(1)
#define CX2589x_TEMP_COOL		(BIT(0) | BIT(1))
#define CX2589x_TEMP_COLD		(BIT(0) | BIT(2))
#define CX2589x_TEMP_HOT		(BIT(2) | BIT(1))

/* precharge current  */
#define CX2589x_PRECHRG_CUR_MASK		GENMASK(7, 4)
#define CX2589x_PRECHRG_CURRENT_STEP1_uA	57000
#define CX2589x_PRECHRG_CURRENT_STEP2_uA	64000
#define CX2589x_PRECHRG_I_MIN_uA		52000
#define CX2589x_PRECHRG_I_MAX_uA		721000
#define CX2589x_PRECHRG_I_MIDDLE_uA		337000
#define CX2589x_PRECHRG_I_DEF_uA		128000
#define CX2589x_PRECHRG_337mA		5

/* termination current  */
#define CX2589x_TERMCHRG_CUR_MASK		GENMASK(3, 0)
#define CX2589x_TERMCHRG_CURRENT_STEP1_uA	59000
#define CX2589x_TERMCHRG_CURRENT_STEP2_uA	65000
#define CX2589x_TERMCHRG_I_MIN_uA		40000
#define CX2589x_TERMCHRG_I_MAX_uA		725000
#define CX2589x_TERMCHRG_I_MIDDLE_uA		335000
#define CX2589x_TERMCHRG_I_DEF_uA		256000
#define CX2589x_TERMCHRG_335mA		5

/* charge current  */
#define CX2589x_ICHRG_I_MASK		GENMASK(6, 0)

#define CX2589x_ICHRG_I_MIN_uA		0
#define CX2589x_ICHRG_I_STEP_uA		64000
#define CX2589x_ICHRG_I_MAX_uA		5056000
#define CX2589x_ICHRG_I_DEF_uA		2048000

/* charge voltage  */
#define CX2589x_VREG_V_MASK		GENMASK(7, 2)
#define CX2589x_VREG_V_MAX_uV		4608000
#define CX2589x_VREG_V_MIN_uV		3840000
#define CX2589x_VREG_V_DEF_uV		4208000
#define CX2589x_VREG_V_STEP_uV		16000

/* iindpm current  */
#define CX2589x_IINDPM_I_MASK		GENMASK(5, 0)
#define CX2589x_IINDPM_I_MIN_uA	100000
#define CX2589x_IINDPM_I_MAX_uA	3250000
#define CX2589x_IINDPM_STEP_uA		50000
#define CX2589x_IINDPM_DEF_uA		2400000

/* vindpm voltage  */
#define CX2589x_VINDPM_V_MASK		GENMASK(6, 0)
#define CX2589x_VINDPM_V_MIN_uV		3900000
#define CX2589x_VINDPM_V_OFFSET_uV 	2600000
#define CX2589x_VINDPM_V_MAX_uV		15300000
#define CX2589x_VINDPM_STEP_uV		100000
#define CX2589x_VINDPM_DEF_uV		4400000
#define CX2589x_VINDPM_OS_MASK		GENMASK(4, 0)
#define CX2589x_FORCE_VINDPM_MASK		GENMASK(7, 7)


/* DP DM SEL  */
#define CX2589x_FORCE_DPDM_MASK			BIT(1)
#define CX2589x_AUTO_DPDM_MASK		BIT(0)
#define CX2589x_DP_VSEL_MASK		GENMASK(7, 5)
#define CX2589x_DM_VSEL_MASK		GENMASK(4, 2)

/* PUMPX SET  */
#define CX2589x_EN_PUMPX		BIT(7)
#define CX2589x_PUMPX_UP		BIT(1)
#define CX2589x_PUMPX_DN		BIT(0)

/*init param*/
#define CX2589x_EN_ILIM			BIT(6)
#define CX2589x_BOOST_FREQ_500K		BIT(5)
#define CX2589x_EN_ICO			BIT(4)
#define CX2589x_EN_HVDCP		BIT(3)

/* bat comp  */
#define CX2589x_BAT_COMP_MASK		GENMASK(7, 5)
#define CX2589x_BAT_COMP_MAX		140
#define CX2589x_BAT_COMP_MIN		0
#define CX2589x_BAT_COMP_DEF		0
#define CX2589x_BAT_COMP_STEP		20
/* vclamp  */
#define CX2589x_VCLAMP_MASK		GENMASK(4, 2)
#define CX2589x_VCLAMP_MAX_uV		224000
#define CX2589x_VCLAMP_MIN_uV		000
#define CX2589x_VCLAMP_DEF_uV		000
#define CX2589x_VCLAMP_STEP_uV		32000

struct cx2589x_init_data {
	u32 ichg;	/* charge current		*/
	u32 ilim;	/* input current		*/
	u32 vreg;	/* regulation voltage		*/
	u32 iterm;	/* termination current		*/
	u32 iprechg;	/* precharge current		*/
	u32 vlim;	/* minimum system voltage limit */
	u32 max_ichg;
	u32 max_vreg;
};

struct cx2589x_state {
	bool vsys_stat;
	bool therm_stat;
	bool online;
	u8 chrg_stat;
	u8 vbus_status;

	bool chrg_en;
	bool hiz_en;
	bool term_en;
	bool vbus_gd;
	u8 chrg_type;
	u8 health;
	u8 chrg_fault;
	u8 ntc_fault;
};

struct cx2589x_device {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *charger;
	struct power_supply *usb;
	struct power_supply *ac;
	struct mutex lock;
	struct mutex i2c_rw_lock;

	struct usb_phy *usb2_phy;
	struct usb_phy *usb3_phy;
	struct notifier_block usb_nb;
	struct work_struct usb_work;
	unsigned long usb_event;
	struct regmap *regmap;

	char model_name[I2C_NAME_SIZE];
	int device_id;

	struct cx2589x_init_data init_data;
	struct cx2589x_state state;
	u32 watchdog_timer;
#if 1//defined(CONFIG_MTK_GAUGE_VERSION) && (CONFIG_MTK_GAUGE_VERSION == 30)
	struct charger_device *chg_dev;
#endif
	struct regulator_dev *otg_rdev;

	struct delayed_work charger_type_detect_work;
	struct delayed_work charge_monitor_work;
	struct delayed_work unknow_charger_type_detect_work;
	struct delayed_work retry_charger_detect_work;
	struct notifier_block pm_nb;
	bool cx2589x_suspend_flag;

	struct wakeup_source *charger_wakelock;
	bool enable_sw_jeita;

	int chg_type;
	int psy_usb_type;
	struct power_supply *battery;
	int batt_vol;
	int batt_curr;
	struct iio_channel *vbus;
	int force_detect_count;
	bool battery_full;
	int unknow_detect_count;
	bool fake_sdp_type;
	bool unknow_type_check;
	bool typec_attached;
	bool pd_type_detected;
};

#endif /* _CX2589x_CHARGER_H__ */
