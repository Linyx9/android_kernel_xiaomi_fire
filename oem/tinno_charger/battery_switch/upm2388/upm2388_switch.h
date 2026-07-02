/*
 * upm2388_switching.h
 * Umpower UPM2388 Fuel Gauge Header
 *
 * Copyright (C) 2021 Umpower Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __UPM2388_SWITCHING_H
#define __UPM2388_SWITCHING_H __FILE__


#include <linux/power_supply.h>
#include <linux/i2c.h>
#include <linux/device.h>

#define I2C_ADDR_LIMITER_MAIN				(0x70 >> 1)
#define I2C_ADDR_LIMITER_SUB				(0x72 >> 1)

#define UPM2388_SW_CORE_INT1				0x00
#define UPM2388_SW_CORE_INT2				0x01
#define UPM2388_SW_PM_INT				0x02
#define UPM2388_SW_CORE_INT1_MASK			0x03
#define UPM2388_SW_CORE_INT2_MASK			0x04
#define UPM2388_SW_PM_INT_MASK				0x05
#define UPM2388_SW_CORE_STATUS1				0x06
#define UPM2388_SW_VAL1_VCHG				0x07
#define UPM2388_SW_VAL2_VCHG				0x08
#define UPM2388_SW_VAL1_VBAT				0x09
#define UPM2388_SW_VAL2_VBAT				0x0A
#define UPM2388_SW_VAL1_ICHG				0x0B
#define UPM2388_SW_VAL2_ICHG				0x0C
#define UPM2388_SW_VAL1_IDISCHG				0x0D
#define UPM2388_SW_VAL2_IDISCHG				0x0E
#define UPM2388_SW_CORE_CTRL1				0x0F
#define UPM2388_SW_CORE_CTRL2				0x10
#define UPM2388_SW_CORE_CTRL3				0x11
#define UPM2388_SW_CORE_CTRL4				0x12
#define UPM2388_SW_CORE_CTRL5				0x13
#define UPM2388_SW_CORE_CTRL6				0x14
#define UPM2388_SW_CORE_CTRL7				0x15
#define UPM2388_SW_CORE_CTRL8				0x16
#define UPM2388_SW_TOP_RECHG_CTRL1			0x17
#define UPM2388_SW_TOP_EOC_CTRL1			0x18
#define UPM2388_SW_TOP_EOC_CTRL2			0x19
#define UPM2388_SW_COMASK_CALDIS			0x1A
#define UPM2388_SW_PM_ENABLE				0x1B
#define UPM2388_SW_PM_HYST_LEVEL1			0x1C
#define UPM2388_SW_PM_HYST_LEVEL2			0x1D
#define UPM2388_SW_PM_V_OPTION				0x1E
#define UPM2388_SW_PM_I_OPTION				0x1F
#define UPM2388_SW_ID					0x29
#define UPM2388_SW_COMMON1				0x40

/* reg00 */
#define UPM2388_TSD_INT_MASK				0x80
#define UPM2388_TSD_INT_SHIFT				7

#define UPM2388_FCC_2_TRICKLE_INT_MASK			0x08
#define UPM2388_FCC_2_TRICKLE_INT_SHIFT			3

#define UPM2388_TRICKLE_2_FCC_INT_MASK			0x04
#define UPM2388_TRICKLE_2_FCC_INT_SHIFT			2

#define UPM2388_TRICKLE_2_PRE_INT_MASK			0x02
#define UPM2388_TRICKLE_2_PRE_INT_SHIFT			1

#define UPM2388_PRE_2_TRICKLE_INT_MASK			0x01
#define UPM2388_PRE_2_TRICKLE_INT_SHIFT			0

/* reg01 */
#define UPM2388_RESTART_INT_MASK			0x02
#define UPM2388_RESTART_INT_SHIFT			1

#define UPM2388_EOC_INT_MASK				0x01
#define UPM2388_EOC_INT_SHIFT				0

/* reg02 */
#define UPM2388_RR_REQ_DONE_MASK			0x08
#define UPM2388_RR_REQ_DONE_SHIFT			3

#define UPM2388_CO_REQ_FIRST_DONE_MASK			0x04
#define UPM2388_CO_REQ_FIRST_DONE_SHIFT			2

/* reg03 */
#define UPM2388_TSD_IM_MASK				0x80
#define UPM2388_TSD_IM_SHIFT				7

#define UPM2388_FCC_2_TRICKLE_BAT_IM_MASK		0x08
#define UPM2388_FCC_2_TRICKLE_BAT_IM_SHIFT		3

#define UPM2388_TRICKLE_2_FCC_BAT_IM_MASK		0x04
#define UPM2388_TRICKLE_2_FCC_BAT_IM_SHIFT		2

#define UPM2388_TRICKLE_2_PRE_BAT_IM_MASK		0x02
#define UPM2388_TRICKLE_2_PRE_BAT_IM_SHIFT		1

#define UPM2388_PRE_2_TRICKLE_BAT_IM_MASK		0x01
#define UPM2388_PRE_2_TRICKLE_BAT_IM_SHIFT		0

/* reg04 */
#define UPM2388_RESTART_IM_MASK				0x02
#define UPM2388_RESTART_IM_SHIFT			1

#define UPM2388_EOC_IM_MASK				0x01
#define UPM2388_EOC_IM_SHIFT				0

/* reg05 */
#define UPM2388_RR_REQ_DONE_IM_MASK			0x08
#define UPM2388_RR_REQ_DONE_IM_SHIFT			3

#define UPM2388_CO_REQ_FIRST_DONE_IM_MASK		0x04
#define UPM2388_CO_REQ_FIRST_DONE_IM_SHIFT		2

/* reg06 */
#define UPM2388_SW_CORE_STATUS_MASK			0xC0
#define UPM2388_SW_CORE_STATUS_SHIFT			6
#define UPM2388_SW_CORE_STAT_NORMAL			0
#define UPM2388_SW_CORE_STAT_CHG_LIMIT			1
#define UPM2388_SW_CORE_STAT_DISCHG_LIMIT		2
#define UPM2388_SW_CORE_STAT_SUPPLEMENT_MODE		3

#define UPM2388_FAST_CHG_MASK				0x04
#define UPM2388_FAST_CHG_SHIFT				2

#define UPM2388_TRICKLE_CHG_MASK			0x02
#define UPM2388_TRICKLE_CHG_SHIFT			1

#define UPM2388_PRE_CHG_MASK				0x01
#define UPM2388_PRE_CHG_SHIFT				0

/* reg07 */
#define UPM2388_VAL1_VCHG_MASK				0xFF
#define UPM2388_VAL1_VCHG_SHIFT				0

/* reg08 */
#define UPM2388_VAL2_VCHG_MASK				0xF0
#define UPM2388_VAL2_VCHG_SHIFT				4

#define UPM2388_VCHG_BASE				0
#define UPM2388_VCHG_LSB				1250
#define UPM2388_VCHG_OFFSET				0

/* reg09 */
#define UPM2388_VAL1_VBAT_MASK				0xFF
#define UPM2388_VAL1_VBAT_SHIFT				0

/* reg0A */
#define UPM2388_VAL2_VBAT_MASK				0xF0
#define UPM2388_VAL2_VBAT_SHIFT				4

#define UPM2388_VBAT_BASE				0
#define UPM2388_VBAT_LSB				1250
#define UPM2388_VBAT_OFFSET				0

/* reg0B */
#define UPM2388_VAL1_ICHG_MASK				0xFF
#define UPM2388_VAL1_ICHG_SHIFT				0

/* reg0C */
#define UPM2388_VAL2_ICHG_MASK				0xF0
#define UPM2388_VAL2_ICHG_SHIFT				4

#define UPM2388_ICHG_BASE				0
#define UPM2388_ICHG_LSB				20508
#define UPM2388_ICHG_LSB_DIV				10
#define UPM2388_ICHG_OFFSET				0

/* reg0D */
#define UPM2388_VAL1_IDISCHG_MASK			0xFF
#define UPM2388_VAL1_IDISCHG_SHIFT			0

/* reg0E */
#define UPM2388_VAL2_IDISCHG_MASK			0xF0
#define UPM2388_VAL2_IDISCHG_SHIFT			4

#define UPM2388_IDISCHG_BASE				0
#define UPM2388_IDISCHG_LSB				20508
#define UPM2388_IDISCHG_LSB_DIV				10
#define UPM2388_IDISCHG_OFFSET				0

/* reg0F */
#define UPM2388_RECHG_EVAL_START_MASK			0x02
#define UPM2388_RECHG_EVAL_START_SHIFT			1

#define UPM2388_EOC_EVAL_START_MASK			0x01
#define UPM2388_EOC_EVAL_START_SHIFT			0

/* reg11 */
#define UPM2388_IN_OK_MASK				0x80
#define UPM2388_IN_OK_SHIFT				7

#define UPM2388_SUPLLEMENT_MODE_MASK			0x40
#define UPM2388_SUPLLEMENT_MODE_SHIFT			6

#define UPM2388_SW_CORE_DIS_MODE_REG_MASK		0x0C
#define UPM2388_SW_CORE_DIS_MODE_REG_SHIFT		2
#define UPM2388_DIS_MODE_CURR_REGUL_ONLY		0
#define UPM2388_DIS_MODE_NO_REGUL_FULL_ON		3

#define UPM2388_SW_CORE_CHG_MODE_REG_MASK		0x03
#define UPM2388_SW_CORE_CHG_MODE_REG_SHIFT		0
#define UPM2388_CHG_MODE_CURR_REGUL_ONLY		0
#define UPM2388_CHG_MODE_NO_REGUL_FULL_ON		3

/* reg12 */
#define UPM2388_FCC_CHG_CURRENT_LIMIT_MASK		0x7F
#define UPM2388_FCC_CHG_CURRENT_LIMIT_SHIFT		0

/* reg13 */
#define UPM2388_TRICKLE_CHG_CURRENT_LIMIT_MASK		0x3F
#define UPM2388_TRICKLE_CHG_CURRENT_LIMIT_SHIFT		0

/* reg14 */
#define UPM2388_DISCHG_CURRENT_LIMIT_MASK		0x7F
#define UPM2388_DISCHG_CURRENT_LIMIT_SHIFT		0

/* reg17 */
#define UPM2388_RECHG_VOLTAGE_MASK			0xFF
#define UPM2388_RECHG_VOLTAGE_SHIFT			0
#define UPM2388_RECHG_VOLTAGE_BASE			4000
#define UPM2388_RECHG_VOLTAGE_LSB			10
#define UPM2388_RECHG_VOLTAGE_MIN			4000
#define UPM2388_RECHG_VOLTAGE_MAX			5120
#define UPM2388_RECHG_VOLTAGE_OFFSET			144

/* reg18 */
#define UPM2388_EOC_VOLTAGE_MASK			0xFF
#define UPM2388_EOC_VOLTAGE_SHIFT			0
#define UPM2388_EOC_VOLTAGE_BASE			4000
#define UPM2388_EOC_VOLTAGE_LSB				10
#define UPM2388_EOC_VOLTAGE_MIN				4000
#define UPM2388_EOC_VOLTAGE_MAX				5120
#define UPM2388_EOC_VOLTAGE_OFFSET		        144

/* reg19 */
#define UPM2388_EOC_CURRENT_MASK			0x1F
#define UPM2388_EOC_CURRENT_SHIFT			0
#define UPM2388_EOC_CURRENT_BASE			0
#define UPM2388_EOC_CURRENT_LSB				325
#define UPM2388_EOC_CURRENT_MIN				0
#define UPM2388_EOC_CURRENT_MAX				6500


/* reg1B */
#define UPM2388_VCHG_CONTINUOUS_MASK			0x80
#define UPM2388_VCHG_CONTINUOUS_SHIFT			7

#define UPM2388_VBAT_CONTINUOUS_MASK			0x40
#define UPM2388_VBAT_CONTINUOUS_SHIFT			6

#define UPM2388_ICHG_CONTINUOUS_MASK			0x20
#define UPM2388_ICHG_CONTINUOUS_SHIFT			5

#define UPM2388_IDISCHG_CONTINUOUS_MASK			0x10
#define UPM2388_IDISCHG_CONTINUOUS_SHIFT		4

#define UPM2388_VCHG_1SHOT_MASK				0x08
#define UPM2388_VCHG_1SHOT_SHIFT			3

#define UPM2388_VBAT_1SHOT_MASK				0x04
#define UPM2388_VBAT_1SHOT_SHIFT			2

#define UPM2388_ICHG_1SHOT_MASK				0x02
#define UPM2388_ICHG_1SHOT_SHIFT			1

#define UPM2388_IDISCHG_1SHOT_MASK			0x01
#define UPM2388_IDISCHG_1SHOT_SHIFT			0

/* reg1C */
#define UPM2388_HYST_LEV_VCHG_MASK			0xE0
#define UPM2388_HYST_LEV_VCHG_SHIFT			5
#define UPM2388_HYST_LEV_VCHG_BASE			50
#define UPM2388_HYST_LEV_VCHG_LSB			50
#define UPM2388_HYST_LEV_VCHG_MIN			50
#define UPM2388_HYST_LEV_VCHG_MAX			400

#define UPM2388_HYST_LEV_VBAT_MASK			0x1C
#define UPM2388_HYST_LEV_VBAT_SHIFT			2
#define UPM2388_HYST_LEV_VBAT_BASE			50
#define UPM2388_HYST_LEV_VBAT_LSB			50
#define UPM2388_HYST_LEV_VBAT_MIN			50
#define UPM2388_HYST_LEV_VBAT_MAX			400

/* reg1D */
#define UPM2388_HYST_LEV_ICHG_MASK			0xE0
#define UPM2388_HYST_LEV_ICHG_SHIFT			5

#define UPM2388_HYST_LEV_IDISCHG_MASK			0x1C
#define UPM2388_HYST_LEV_IDISCHG_SHIFT			2

/* reg1E */
#define UPM2388_SEL_VOLT_REFRESH_MASK			0x30
#define UPM2388_SEL_VOLT_REFRESH_SHIFT			4
#define UPM2388_VOLT_REFRESH_500MS			0
#define UPM2388_VOLT_REFRESH_1000MS			1
#define UPM2388_VOLT_REFRESH_2000MS			2
#define UPM2388_VOLT_REFRESH_4000MS			3

/* reg1F */
#define UPM2388_SEL_CURR_REFRESH_MASK			0x30
#define UPM2388_SEL_CURR_REFRESH_SHIFT			4
#define UPM2388_CURR_REFRESH_62MS5			0
#define UPM2388_CURR_REFRESH_125MS			1
#define UPM2388_CURR_REFRESH_250MS			2
#define UPM2388_CURR_REFRESH_500MS			3

/* reg29 */
#define UPM2388_ES_NO_MASK				0xC0
#define UPM2388_ES_NO_SHIFT				6
#define UPM2388_ES_NO					2

#define UPM2388_REV_NO_MASK				0x30
#define UPM2388_REV_NO_SHIFT				4

/* reg40 */
#define UPM2388_CM_TSD_EN_MASK				0x40
#define UPM2388_CM_TSD_EN_SHIFT				6
#define UPM2388_SUPLLEMENT_MODE_ON				1
#define UPM2388_SUPLLEMENT_MODE_OFF				0

/* define this macro to enable upm2388 algorithm */
#define UPM2388_ALGO					1

enum current_limiter_type {
	LIMITER_UNI = 0x0,
	LIMITER_MAIN = 0x1,
	LIMITER_SUB = 0x2,
};

static const char *limiter_type_name[] = {
	"uni",
	"main",
	"sub",
};

struct match_table_data {
	int	index;
	int	type;
	int	channel;
};

struct upm2388_platform_data {
	char	*switching_name;
	int	bat_int;
	int	bat_enb;
	int	chg_current_limit;
	int	dischg_current_limit;
	int	recharge_volatge;
	int	float_voltage; /* for interrupt setting, not used */
	int	eoc; /* for interrupt setting, not used */
	int	hys_vchg;
	int	hys_vbat;
	int	hys_ichg;
	int	hys_idischg;
	bool	tsd_en;
};

struct upm2388 {
	struct device			*dev;
	struct i2c_client		*client;
	struct mutex			i2c_lock;
	struct upm2388_platform_data	*pdata;
	struct match_table_data		mtd;
	int				irq;
	int				rev_id;
	int				es_num;

	bool				in_ok;
	bool				supllement_mode;
	bool				power_meter;
	bool				tsd;

	struct power_supply_desc	limiter_psy_desc;
	struct power_supply		*limiter_psy;

	/* work struct */
	struct delayed_work		switch_update_work;
	struct delayed_work		batt_status_update_work;

	/* power supply device */
	struct power_supply		*batt_psy;
	struct power_supply		*chg_psy;
	struct power_supply		*master_fg_psy;
	struct power_supply		*slave_fg_psy;
	struct power_supply		*main_switch_psy;
	struct power_supply		*sub_switch_psy;

	int				chr_online_status;
	int				main_batt_status;
	int				main_batt_temp;
	int				main_batt_curr;
	int				main_batt_volt;
	int				main_batt_soc;
	int				main_batt_ave_curr;

	int				slave_batt_status;
	int				slave_batt_temp;
	int				slave_batt_curr;
	int				slave_batt_volt;
	int				slave_batt_soc;
	int				slave_batt_ave_curr;

	int				thermal_zones;
	int				pre_thermal_zones;

	int				pre_limit_dischg_curr;
	int				limit_dischg_curr;
};

int upm2388_set_trickle_chg_current_limit(struct upm2388 *upm, int ma);
int upm2388_set_fcc_chg_current_limit(struct upm2388 *upm, int ma);
int upm2388_set_eoc_voltage(struct upm2388 *upm, int mv);
int upm2388_set_eoc_current(struct upm2388 *upm, int ma);
int upm2388_set_dischg_current_limit(struct upm2388 *upm, int ma);
void upm2388_dump_regs(struct i2c_client *client);
int upm2388_set_supllement_mode_en(struct upm2388 *upm, bool en);
int upm2388_get_supllement_mode(struct upm2388 *upm, int *en);
int upm2388_set_recharge_voltage(struct upm2388 *upm, int mv);

#endif /* __UPM2388_SWITCHING_H */
