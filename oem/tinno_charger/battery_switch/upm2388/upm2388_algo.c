#define pr_fmt(fmt)	"[upm2388_algo]:%s: " fmt, __func__

#include <linux/slab.h>
#include <linux/delay.h>
#include "upm2388_algo.h"
#include <mt-plat/charger_class.h>
#include <mt-plat/charger_type.h>

int g_main_batt_average_current[TOTAL_NUM] = {0};
int g_slave_batt_average_current[TOTAL_NUM] = {0};

extern bool mtk_is_support_pps(void);

static int get_main_batt_average_current(int value)
{
	int temp_num[TOTAL_NUM] = {0};
	int sum = 0;
	int i, j, k;
	static int first_init = 0;

	if (!first_init) {
		for (i = 0; i < TOTAL_NUM; i++) {
			g_main_batt_average_current[i] = value;
			sum += g_main_batt_average_current[i];
			pr_info("g_average_current[%d]:%d\n", i, g_main_batt_average_current[i]);
		}

		first_init = 1;
		return sum / TOTAL_NUM;
	}

	for (j = TOTAL_NUM -1; j >= 1; j--) {
		temp_num[j] = g_main_batt_average_current[j - 1];
		pr_info("temp_num[%d]:%d, g_main_batt_average_current[%d]:%d\n", j, temp_num[j], j - 1, g_main_batt_average_current[j - 1]);
	}
	temp_num[0] = value;

	for (k = 0; k < TOTAL_NUM; k++) {
		g_main_batt_average_current[k] = temp_num[k];
		sum += g_main_batt_average_current[k];
		pr_info("update g_main_batt_average_current[%d]:%d\n", k, g_main_batt_average_current[k]);
	}

	return sum / TOTAL_NUM;
}

static int get_slave_batt_average_current(int value)
{
	int temp_num[TOTAL_NUM] = {0};
	int sum = 0;
	int i, j, k;
	static int first_init = 0;

	if (!first_init) {
		for (i = 0; i < TOTAL_NUM; i++) {
			g_slave_batt_average_current[i] = value;
			sum += g_slave_batt_average_current[i];
			pr_info("g_slave_batt_average_current[%d]:%d\n", i, g_slave_batt_average_current[i]);
		}

		first_init = 1;
		return sum / TOTAL_NUM;
	}

	for (j = TOTAL_NUM -1; j >= 1; j--) {
		temp_num[j] = g_slave_batt_average_current[j - 1];
		pr_info("temp_num[%d]:%d, g_slave_batt_average_current[%d]:%d\n", j, temp_num[j], j - 1, g_slave_batt_average_current[j - 1]);
	}
	temp_num[0] = value;

	for (k = 0; k < TOTAL_NUM; k++) {
		g_slave_batt_average_current[k] = temp_num[k];
		sum += g_slave_batt_average_current[k];
		pr_info("update g_slave_batt_average_current[%d]:%d\n", k, g_slave_batt_average_current[k]);
	}

	return sum / TOTAL_NUM;
}

static void batt_status_update_work_fn(struct work_struct *work)
{
	struct upm2388 *upm = container_of(work, struct upm2388,
			batt_status_update_work.work);
	union power_supply_propval val = {0};
	int rc = 0;

	pr_info("enter\n");
	/* get master fg device*/
	if (!upm->master_fg_psy) {
		upm->master_fg_psy = power_supply_get_by_name("bms");
		if (!upm->master_fg_psy) {
			pr_err("failed get bms psy device\n");
			goto err;
		}
		pr_info("found bms psy device\n");
	}

	/* get slave fg device*/
	if (!upm->slave_fg_psy) {
		upm->slave_fg_psy = power_supply_get_by_name("bms_s");
		if (!upm->slave_fg_psy) {
			pr_err("failed get bms_s psy device\n");
			goto err;
		}
		pr_info("found bms_s psy device\n");
	}

	/* get main battery info */
	rc = power_supply_get_property(upm->master_fg_psy,
				POWER_SUPPLY_PROP_TEMP, &val);
	if (rc < 0) {
		pr_err("failed get main batt temp\n");
	} else {
		upm->main_batt_temp = val.intval / 10;
		pr_info("get main batt temp: %d\n", upm->main_batt_temp);
	}

	rc = power_supply_get_property(upm->master_fg_psy,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (rc < 0) {
		pr_err("failed get main batt volt\n");
	} else {
		upm->main_batt_volt = val.intval / 1000; // convert uV to mV
		pr_info("get main batt volt: %d\n", upm->main_batt_volt);
	}

	rc = power_supply_get_property(upm->master_fg_psy,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (rc < 0) {
		pr_err("failed get main batt curr\n");
	} else {
		upm->main_batt_curr = val.intval / 1000; // convert uA to mA
		upm->main_batt_ave_curr = get_main_batt_average_current(upm->main_batt_curr);
		pr_info("get main batt curr: %d, ave_curr:%d\n", upm->main_batt_curr, upm->main_batt_ave_curr);
	}

	rc = power_supply_get_property(upm->master_fg_psy,
				POWER_SUPPLY_PROP_CAPACITY, &val);
	if (rc < 0) {
		pr_err("failed get main batt soc\n");
	} else {
		upm->main_batt_soc = val.intval / 10;
		pr_info("get main batt soc: %d\n", upm->main_batt_soc);
	}

	/* get slave battery temp */
	rc = power_supply_get_property(upm->slave_fg_psy,
				POWER_SUPPLY_PROP_TEMP, &val);
	if (rc < 0) {
		pr_err("failed get slave batt temp\n");
	} else {
		upm->slave_batt_temp = val.intval / 10;
		pr_info("get slave batt temp: %d\n", upm->slave_batt_temp);
	}

	rc = power_supply_get_property(upm->slave_fg_psy,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (rc < 0) {
		pr_err("failed get slave batt volt\n");
	} else {
		upm->slave_batt_volt = val.intval / 1000; // convert uV to mV
		pr_info("get slave batt volt: %d\n", upm->slave_batt_volt);
	}

	rc = power_supply_get_property(upm->slave_fg_psy,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (rc < 0) {
		pr_err("failed get slave batt curr\n");
	} else {
		upm->slave_batt_curr = val.intval / 1000; // convert uA to mA
		upm->slave_batt_ave_curr = get_slave_batt_average_current(upm->slave_batt_curr);
		pr_info("get slave batt curr: %d, ave_curr:%d\n", upm->slave_batt_curr, upm->slave_batt_ave_curr);
	}

	rc = power_supply_get_property(upm->slave_fg_psy,
				POWER_SUPPLY_PROP_CAPACITY, &val);
	if (rc < 0) {
		pr_err("failed get slave batt soc\n");
	} else {
		upm->slave_batt_soc = val.intval / 10;
		pr_info("get slave batt soc: %d\n", upm->slave_batt_soc);
	}

	schedule_delayed_work(&upm->batt_status_update_work, msecs_to_jiffies(WAIT_FOR_CHECK_STATUS_MS));
	return;
err:
	pr_err("update batt status failed\n");
	cancel_delayed_work(&upm->batt_status_update_work);
	return;
}

static void update_curr_thermal_zone(struct upm2388 *upm)
{
	pr_info("batt temp (m:%d s:%d)\n", upm->main_batt_temp, upm->slave_batt_temp);

	if ((upm->main_batt_temp <= -20) || (upm->main_batt_temp >= 60)
		|| (upm->slave_batt_temp <= -20) || (upm->slave_batt_temp >= 60)) {
		pr_err("batt temp out of range(-20, 60), enter thermal zones invalid\n");
		upm->thermal_zones = THERMAL_ZONES_INVALID;
	} else if ((upm->main_batt_temp > -20) && (upm->main_batt_temp <= 0)) {
		if ((upm->slave_batt_temp > -20) && (upm->slave_batt_temp <= 0)) {
			pr_info("enter thermal zones M cold S cold\n");
			upm->thermal_zones = THERMAL_ZONES_M_COLD_S_COLD;
		} else if ((upm->slave_batt_temp > 0) && (upm->slave_batt_temp < 15)) {
			pr_info("enter thermal zones M cold S cool\n");
			upm->thermal_zones = THERMAL_ZONES_M_COLD_S_COOL;
		} else if ((upm->slave_batt_temp >= 15) && (upm->slave_batt_temp < 45)) {
			pr_info("enter thermal zones M cold S normal\n");
			upm->thermal_zones = THERMAL_ZONES_M_COLD_S_NORMAL;
		} else if ((upm->slave_batt_temp >= 45) && (upm->slave_batt_temp < 55)) {
			pr_info("enter thermal zones M cold S warm\n");
			upm->thermal_zones = THERMAL_ZONES_M_COLD_S_WARM;
		} else if ((upm->slave_batt_temp >= 55) && (upm->slave_batt_temp < 60)) {
			pr_info("enter thermal zones M cold S hot\n");
			upm->thermal_zones = THERMAL_ZONES_M_COLD_S_HOT;
		}
	} else if ((upm->main_batt_temp > 0) && (upm->main_batt_temp < 15)) {
		if ((upm->slave_batt_temp > -20) && (upm->slave_batt_temp <= 0)) {
			pr_info("enter thermal zones M cool S cold\n");
			upm->thermal_zones = THERMAL_ZONES_M_COOL_S_COLD;
		} else if ((upm->slave_batt_temp > 0) && (upm->slave_batt_temp < 15)) {
			pr_info("enter thermal zones M cool S cool\n");
			upm->thermal_zones = THERMAL_ZONES_M_COOL_S_COOL;
		} else if ((upm->slave_batt_temp >= 15) && (upm->slave_batt_temp < 45)) {
			pr_info("enter thermal zones M cool S normal\n");
			upm->thermal_zones = THERMAL_ZONES_M_COOL_S_NORMAL;
		} else if ((upm->slave_batt_temp >= 45) && (upm->slave_batt_temp < 55)) {
			pr_info("enter thermal zones M cool S warm\n");
			upm->thermal_zones = THERMAL_ZONES_M_COOL_S_WARM;
		} else if ((upm->slave_batt_temp >= 55) && (upm->slave_batt_temp < 60)) {
			pr_info("enter thermal zones M cool S hot\n");
			upm->thermal_zones = THERMAL_ZONES_M_COOL_S_HOT;
		}
	} else if ((upm->main_batt_temp >= 15) && (upm->main_batt_temp < 45)) {
		if ((upm->slave_batt_temp > -20) && (upm->slave_batt_temp <= 0)) {
			pr_info("enter thermal zones M normal S cold\n");
			upm->thermal_zones = THERMAL_ZONES_M_NORMAL_S_COLD;
		} else if ((upm->slave_batt_temp > 0) && (upm->slave_batt_temp < 15)) {
			pr_info("enter thermal zones M normal S cool\n");
			upm->thermal_zones = THERMAL_ZONES_M_NORMAL_S_COOL;
		} else if ((upm->slave_batt_temp >= 15) && (upm->slave_batt_temp < 45)) {
			pr_info("enter thermal zones M normal S normal\n");
			upm->thermal_zones = THERMAL_ZONES_M_NORMAL_S_NORMAL;
		} else if ((upm->slave_batt_temp >= 45) && (upm->slave_batt_temp < 55)) {
			pr_info("enter thermal zones M normal S warm\n");
			upm->thermal_zones = THERMAL_ZONES_M_NORMAL_S_WARM;
		} else if ((upm->slave_batt_temp >= 55) && (upm->slave_batt_temp < 60)) {
			pr_info("enter thermal zones M normal S hot\n");
			upm->thermal_zones = THERMAL_ZONES_M_NORMAL_S_HOT;
		}
	} else if ((upm->main_batt_temp >= 45) && (upm->main_batt_temp < 55)) {
		if ((upm->slave_batt_temp > -20) && (upm->slave_batt_temp <= 0)) {
			pr_info("enter thermal zones M warm S cold\n");
			upm->thermal_zones = THERMAL_ZONES_M_WARM_S_COLD;
		} else if ((upm->slave_batt_temp > 0) && (upm->slave_batt_temp < 15)) {
			pr_info("enter thermal zones M warm S cool\n");
			upm->thermal_zones = THERMAL_ZONES_M_WARM_S_COOL;
		} else if ((upm->slave_batt_temp >= 15) && (upm->slave_batt_temp < 45)) {
			pr_info("enter thermal zones M warm S normal\n");
			upm->thermal_zones = THERMAL_ZONES_M_WARM_S_NORMAL;
		} else if ((upm->slave_batt_temp >= 45) && (upm->slave_batt_temp < 55)) {
			pr_info("enter thermal zones M warm S warm\n");
			upm->thermal_zones = THERMAL_ZONES_M_WARM_S_WARM;
		} else if ((upm->slave_batt_temp >= 55) && (upm->slave_batt_temp < 60)) {
			pr_info("enter thermal zones M warm S hot\n");
			upm->thermal_zones = THERMAL_ZONES_M_WARM_S_HOT;
		}
	} else if ((upm->main_batt_temp >= 55) && (upm->main_batt_temp < 60)) {
		if ((upm->slave_batt_temp > -20) && (upm->slave_batt_temp <= 0)) {
			pr_info("enter thermal zones M hot S cold\n");
			upm->thermal_zones = THERMAL_ZONES_M_HOT_S_COLD;
		} else if ((upm->slave_batt_temp > 0) && (upm->slave_batt_temp < 15)) {
			pr_info("enter thermal zones M hot S cool\n");
			upm->thermal_zones = THERMAL_ZONES_M_HOT_S_COOL;
		} else if ((upm->slave_batt_temp >= 15) && (upm->slave_batt_temp < 45)) {
			pr_info("enter thermal zones M hot S normal\n");
			upm->thermal_zones = THERMAL_ZONES_M_HOT_S_NORMAL;
		} else if ((upm->slave_batt_temp >= 45) && (upm->slave_batt_temp < 55)) {
			pr_info("enter thermal zones M hot S warm\n");
			upm->thermal_zones = THERMAL_ZONES_M_HOT_S_WARM;
		} else if ((upm->slave_batt_temp >= 55) && (upm->slave_batt_temp < 60)) {
			pr_info("enter thermal zones M hot S hot\n");
			upm->thermal_zones = THERMAL_ZONES_M_HOT_S_HOT;
		}
	} else {
		pr_info("Invalid thermal zones\n");
		upm->thermal_zones = THERMAL_ZONES_INVALID;
	}

	return;
}

#if 0
static void update_switch_dischg_current_limit_once(struct upm2388 *upm)
{
	int temp_curr_val = 0;

	return;

	if (upm->main_batt_volt < 3600) {
		if (upm->main_batt_ave_curr < 0) {
			pr_info("main batt discharging, convert negative value to positive\n");
			temp_curr_val = (-1) * upm->main_batt_ave_curr;
		}

		if (temp_curr_val < 100) {
			temp_curr_val = 50;
		} else if ((temp_curr_val < 200 ) && (temp_curr_val >= 100)) {
			temp_curr_val = 100;
		} else if ((temp_curr_val < 300 ) && (temp_curr_val >= 200)) {
			temp_curr_val = 200;
		} else if ((temp_curr_val < 400 ) && (temp_curr_val >= 300)) {
			temp_curr_val = 300;
		} else if ((temp_curr_val < 500 ) && (temp_curr_val >= 400)) {
			temp_curr_val = 400;
		} else if ((temp_curr_val < 600 ) && (temp_curr_val >= 500)) {
			temp_curr_val = 500;
		} else if ((temp_curr_val < 700 ) && (temp_curr_val >= 600)) {
			temp_curr_val = 600;
		} else if ((temp_curr_val < 800 ) && (temp_curr_val >= 700)) {
			temp_curr_val = 700;
		} else if ((temp_curr_val < 900 ) && (temp_curr_val >= 800)) {
			temp_curr_val = 800;
		} else if ((temp_curr_val < 1000 ) && (temp_curr_val >= 900)) {
			temp_curr_val = 900;
		} else if ((temp_curr_val < 1100 ) && (temp_curr_val >= 1000)) {
			temp_curr_val = 1000;
		} else if ((temp_curr_val < 1200 ) && (temp_curr_val >= 1100)) {
			temp_curr_val = 1100;
		} else if ((temp_curr_val < 1300 ) && (temp_curr_val >= 1200)) {
			temp_curr_val = 1200;
		} else if (temp_curr_val >= 1300) {
			temp_curr_val = 1300;
		}

		upm->pre_limit_dischg_curr = upm->limit_dischg_curr = temp_curr_val;
		upm2388_set_dischg_current_limit(upm, upm->pre_limit_dischg_curr);

		pr_info("set dischg current limit to %d mA\n", upm->pre_limit_dischg_curr);
	}

	return;
}
#endif

static void set_sub_discharging_curr(struct upm2388 *upm, int curr);

static void __maybe_unused update_switch_dischg_current_limit(struct upm2388 *upm)
{
	int temp_curr_val_main = 0;
	int temp_curr_val_slave = 0;
//	int delta_curr_val = 0;
	int final_set_curr_val = 0;

#if 0
	if (upm->main_batt_volt < 3600) {
		if (upm->main_batt_ave_curr < 0) {
			pr_info("main batt discharging, convert negative value to positive\n");
			temp_curr_val = (-1) * upm->main_batt_ave_curr;
		}

		delta_curr_val = temp_curr_val - upm->pre_limit_dischg_curr;
		pr_info("delta current value is %d mA\n", delta_curr_val);

		if (delta_curr_val < 0) {
			pr_info("main batt dischg current decreased, should decrease dischg limit current\n");
			upm->limit_dischg_curr = ((upm->pre_limit_dischg_curr - ((-1) * delta_curr_val) + 50) / 100) * 100;
			if (upm->limit_dischg_curr <= 0)
				upm->limit_dischg_curr = 50;
		} else {
			pr_info("main batt dischg current increased, should increase dischg limit current\n");
			upm->limit_dischg_curr = ((upm->pre_limit_dischg_curr + delta_curr_val) / 100) * 100;
			if (upm->limit_dischg_curr > 1300)
				upm->limit_dischg_curr = 1300;
		}

		pr_info("set dischg current limit to %d mA and pre limit current is %d mA\n",
			upm->limit_dischg_curr, upm->pre_limit_dischg_curr);

		upm2388_set_dischg_current_limit(upm, upm->limit_dischg_curr);
		upm->pre_limit_dischg_curr = upm->limit_dischg_curr;
	}
#else
	//if (upm->main_batt_volt < 3600) {
		if (upm->main_batt_ave_curr < 0 && upm->slave_batt_ave_curr < 0) { // main batt and slave batt both in dis-charging
			temp_curr_val_main = (-1) * upm->main_batt_ave_curr;
			temp_curr_val_slave = (-1) * upm->slave_batt_ave_curr;
			final_set_curr_val = temp_curr_val_main + temp_curr_val_slave;
			pr_info("convert main batt curr (%d) to (%d) and slave batt curr (%d) to (%d)\n",
				upm->main_batt_ave_curr, temp_curr_val_main,
				upm->slave_batt_ave_curr, temp_curr_val_slave);
		} else if (upm->main_batt_ave_curr >= 0 && upm->slave_batt_ave_curr < 0) { // main batt charging and slave batt dis-charging
			temp_curr_val_slave = (-1) * upm->slave_batt_ave_curr;
			final_set_curr_val = temp_curr_val_slave - upm->main_batt_ave_curr;
			pr_info("convert slave batt current (%d) to (%d)\n", upm->slave_batt_ave_curr, temp_curr_val_slave);
		} else if (upm->main_batt_ave_curr < 0 && upm->slave_batt_ave_curr >= 0) { // main batt dis-charging and slave batt charging
			temp_curr_val_main = (-1) * upm->main_batt_ave_curr;
			final_set_curr_val = temp_curr_val_main - upm->slave_batt_ave_curr;
			pr_info("convert main batt current (%d) to (%d)\n", upm->main_batt_ave_curr, temp_curr_val_main);
		}

		if (final_set_curr_val < 0) {
			final_set_curr_val = (-1) * final_set_curr_val;
			pr_info("convert final_set_curr_val to (%d)\n", final_set_curr_val);
		}

		if (final_set_curr_val >= 1300)
			final_set_curr_val = 1300;
		else if (final_set_curr_val <= 50)
			final_set_curr_val = 50;

		set_sub_discharging_curr(upm, final_set_curr_val);

		pr_info("sub set dischg current limit to %d mA\n", final_set_curr_val);
	//}
#endif

	return;
}

//add for main switch set
static void set_main_input_curr(struct upm2388 *upm, int curr) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->main_switch_psy) {
		upm->main_switch_psy = power_supply_get_by_name("upm2388_main_ch0");
		if (!upm->main_switch_psy) {
			pr_err("failed get main_switch_psy device\n");
			return;
		}
		pr_info("found main_switch_psy device\n");
	}
	val.intval = curr;
	rc = power_supply_set_property(upm->main_switch_psy, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (rc < 0)
		pr_info("main_switch_psy online fail(%d)\n", rc);

	return;
}

static void set_main_discharging_curr(struct upm2388 *upm, int curr) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->main_switch_psy) {
		upm->main_switch_psy = power_supply_get_by_name("upm2388_main_ch0");
		if (!upm->main_switch_psy) {
			pr_err("failed get main_switch_psy device\n");
			return;
		}
		pr_info("found main_switch_psy device\n");
	}
	val.intval = curr;
	rc = power_supply_set_property(upm->main_switch_psy, POWER_SUPPLY_PROP_INPUT_POWER_LIMIT, &val);
	if (rc < 0)
		pr_info("main_switch_psy online fail(%d)\n", rc);

	return;
}

static void set_main_eoc_vol(struct upm2388 *upm, int vol) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->main_switch_psy) {
		upm->main_switch_psy = power_supply_get_by_name("upm2388_main_ch0");
		if (!upm->main_switch_psy) {
			pr_err("failed get main_switch_psy device\n");
			return;
		}
		pr_info("found main_switch_psy device\n");
	}
	val.intval = vol;
	rc = power_supply_set_property(upm->main_switch_psy, POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);
	if (rc < 0)
		pr_info("main_switch_psy online fail(%d)\n", rc);

	return;
}

static void set_main_eoc_curr(struct upm2388 *upm, int curr) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->main_switch_psy) {
		upm->main_switch_psy = power_supply_get_by_name("upm2388_main_ch0");
		if (!upm->main_switch_psy) {
			pr_err("failed get main_switch_psy device\n");
			return;
		}
		pr_info("found main_switch_psy device\n");
	}
	val.intval = curr;
	rc = power_supply_set_property(upm->main_switch_psy, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val);
	if (rc < 0)
		pr_info("main_switch_psy online fail(%d)\n", rc);

	return;
}

static void set_main_supllement_mode_en(struct upm2388 *upm, int en) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->main_switch_psy) {
		upm->main_switch_psy = power_supply_get_by_name("upm2388_main_ch0");
		if (!upm->main_switch_psy) {
			pr_err("failed get main_switch_psy device\n");
			return;
		}
		pr_info("found main_switch_psy device\n");
	}
	val.intval = en;
	rc = power_supply_set_property(upm->main_switch_psy, POWER_SUPPLY_PROP_CHARGE_ENABLE_CONTROL, &val);
	if (rc < 0)
		pr_info("main_switch_psy online fail(%d)\n", rc);

	return;
}

static void __maybe_unused set_main_recharge_vol(struct upm2388 *upm, int vol) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->main_switch_psy) {
		upm->main_switch_psy = power_supply_get_by_name("upm2388_main_ch0");
		if (!upm->main_switch_psy) {
			pr_err("failed get main_switch_psy device\n");
			return;
		}
		pr_info("found main_switch_psy device\n");
	}
	val.intval = vol;
	rc = power_supply_set_property(upm->main_switch_psy, POWER_SUPPLY_PROP_RECHARGE_VOLTAGE, &val);
	if (rc < 0)
		pr_info("main_switch_psy online fail(%d)\n", rc);

	return;
}
//add for sub switch set
static void set_sub_input_curr(struct upm2388 *upm, int curr) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->sub_switch_psy) {
		upm->sub_switch_psy = power_supply_get_by_name("upm2388_sub_ch0");
		if (!upm->sub_switch_psy) {
			pr_err("failed get sub_switch_psy device\n");
			return;
		}
		pr_info("found sub_switch_psy device\n");
	}
	val.intval = curr;
	rc = power_supply_set_property(upm->sub_switch_psy, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (rc < 0)
		pr_info("sub_switch_psy online fail(%d)\n", rc);

	return;
}

static void set_sub_discharging_curr(struct upm2388 *upm, int curr) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->sub_switch_psy) {
		upm->sub_switch_psy = power_supply_get_by_name("upm2388_sub_ch0");
		if (!upm->sub_switch_psy) {
			pr_err("failed get sub_switch_psy device\n");
			return;
		}
		pr_info("found sub_switch_psy device\n");
	}
	val.intval = curr;
	rc = power_supply_set_property(upm->sub_switch_psy, POWER_SUPPLY_PROP_INPUT_POWER_LIMIT, &val);
	if (rc < 0)
		pr_info("sub_switch_psy online fail(%d)\n", rc);

	return;
}

static void set_sub_eoc_vol(struct upm2388 *upm, int vol) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->sub_switch_psy) {
		upm->sub_switch_psy = power_supply_get_by_name("upm2388_sub_ch0");
		if (!upm->sub_switch_psy) {
			pr_err("failed get sub_switch_psy device\n");
			return;
		}
		pr_info("found sub_switch_psy device\n");
	}
	val.intval = vol;
	rc = power_supply_set_property(upm->sub_switch_psy, POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);
	if (rc < 0)
		pr_info("sub_switch_psy online fail(%d)\n", rc);

	return;
}

static void set_sub_eoc_curr(struct upm2388 *upm, int curr) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->sub_switch_psy) {
		upm->sub_switch_psy = power_supply_get_by_name("upm2388_sub_ch0");
		if (!upm->sub_switch_psy) {
			pr_err("failed get sub_switch_psy device\n");
			return;
		}
		pr_info("found sub_switch_psy device\n");
	}
	val.intval = curr;
	rc = power_supply_set_property(upm->sub_switch_psy, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val);
	if (rc < 0)
		pr_info("sub_switch_psy online fail(%d)\n", rc);

	return;
}

static void set_sub_supllement_mode_en(struct upm2388 *upm, bool en) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->sub_switch_psy) {
		upm->sub_switch_psy = power_supply_get_by_name("upm2388_sub_ch0");
		if (!upm->sub_switch_psy) {
			pr_err("failed get sub_switch_psy device\n");
			return;
		}
		pr_info("found sub_switch_psy device\n");
	}
	val.intval = en;
	rc = power_supply_set_property(upm->sub_switch_psy, POWER_SUPPLY_PROP_CHARGE_ENABLE_CONTROL, &val);
	if (rc < 0)
		pr_info("sub_switch_psy online fail(%d)\n", rc);

	return;
}

static void __maybe_unused set_sub_recharge_vol(struct upm2388 *upm, int vol) {
	union power_supply_propval val = {0};
	int rc = 0;

	if (!upm->sub_switch_psy) {
		upm->sub_switch_psy = power_supply_get_by_name("upm2388_sub_ch0");
		if (!upm->sub_switch_psy) {
			pr_err("failed get sub_switch_psy device\n");
			return;
		}
		pr_info("found sub_switch_psy device\n");
	}
	val.intval = vol;
	rc = power_supply_set_property(upm->sub_switch_psy, POWER_SUPPLY_PROP_RECHARGE_VOLTAGE, &val);
	if (rc < 0)
		pr_info("sub_switch_psy online fail(%d)\n", rc);

	return;
}

static struct charger_device *primary_charger;
#if 1
//single switch
static void switch_update_work_fn(struct work_struct *work)
{
	struct upm2388 *upm = container_of(work, struct upm2388,
			switch_update_work.work);
	static enum charger_type chr_type;
	union power_supply_propval val = {0};
	int supllement_mode = -1;
	int rc = 0;

	pr_info("enter\n");

	/* get buck-charger psy device*/
	if (!primary_charger) {
		primary_charger = get_charger_by_name("primary_chg");
		if (!primary_charger) {
			pr_err("get primary charger device failed\n");
			return;
		}
	}
	/* get battery psy device*/
	if (!upm->batt_psy) {
		upm->batt_psy = power_supply_get_by_name("battery");
		if (!upm->batt_psy) {
			pr_err("failed get batt psy device\n");
			return;
		}
		pr_info("found batt psy device\n");
	}

	/* get charger psy device*/
	if (!upm->chg_psy) {
		upm->chg_psy = power_supply_get_by_name("charger");
		if (!upm->chg_psy) {
			pr_err("failed get charger psy device\n");
			return;
		}
		pr_info("found charger psy device\n");
	}

	/* get main switch psy device*/
	if (!upm->main_switch_psy) {
		upm->main_switch_psy = power_supply_get_by_name("upm2388_main_ch0");
		if (!upm->main_switch_psy) {
			pr_err("failed get main_switch_psy device\n");
			return;
		}
		pr_info("found main_switch_psy device\n");
	}

	/* get sub switch psy device*/
	if (!upm->sub_switch_psy) {
		upm->sub_switch_psy = power_supply_get_by_name("upm2388_sub_ch0");
		if (!upm->sub_switch_psy) {
			pr_err("failed get sub_switch_psy device\n");
			return;
		}
		pr_info("found sub_switch_psy device\n");
	}

	/* get current thermal zones */
	update_curr_thermal_zone(upm);

	/* get main battery charging status */
	rc = power_supply_get_property(upm->batt_psy,
					POWER_SUPPLY_PROP_STATUS, &val);
	if (rc < 0) {
		pr_err("failed get main battery status\n");
		return;
	} else {
		upm->main_batt_status = val.intval;
		pr_info("get main battery status: %d\n", upm->main_batt_status);
	}

	/* get charger online status */
	rc = power_supply_get_property(upm->chg_psy,
					POWER_SUPPLY_PROP_ONLINE, &val);
	if (rc < 0) {
		pr_err("failed get charger online status\n");
		return;
	} else {
		upm->chr_online_status = val.intval;
		pr_info("get charger online status: %d\n", upm->chr_online_status);
	}

	/* get charger type */
	chr_type = mt_get_charger_type();

	if (upm->chr_online_status == 1) {
		/* run algo for charging status */
		pr_info("charger online\n");

		switch (upm->thermal_zones) {
		case THERMAL_ZONES_M_COLD_S_COLD: /* -20<T1≤0 && -20<T2≤0 */
		case THERMAL_ZONES_M_COLD_S_HOT: /* -20<T1≤0 && 55≤T2<60 */
		case THERMAL_ZONES_M_HOT_S_COLD: /* 55≤T1<60 && -20<T2≤0 */
		case THERMAL_ZONES_M_HOT_S_HOT: /* 55≤T1<60 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				//upm2388_set_supllement_mode_en(upm, true);
				set_sub_supllement_mode_en(upm, true);
				set_main_supllement_mode_en(upm, true);
				set_main_discharging_curr(upm, 8400);
				set_sub_discharging_curr(upm, 50);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
				//update_switch_dischg_current_limit_once(upm);
			} else {
				pr_info("main_batt_volt:%d, slave_batt_volt:%d, curr_thermal_zones:%d\n", upm->main_batt_volt, upm->slave_batt_volt, upm->thermal_zones);
				if (upm->main_batt_volt < 4100 && upm->slave_batt_volt < 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, true);
				} else {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;

		case THERMAL_ZONES_M_COLD_S_COOL: /* -20<T1≤0 && 0<T2<15 */
		case THERMAL_ZONES_M_HOT_S_COOL: /* 55≤T1<60 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				set_main_supllement_mode_en(upm, true);
				set_sub_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_eoc_voltage(upm, 4100);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				pr_info("main_batt_volt:%d, curr_thermal_zones:%d\n", upm->main_batt_volt, upm->thermal_zones);
				if (upm->main_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, true);
				}
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_COLD_S_NORMAL: /* -20<T1≤0 && 15≤T2<45 */
		case THERMAL_ZONES_M_HOT_S_NORMAL: /* 55≤T1<60 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_main_supllement_mode_en(upm, true);
				set_sub_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_eoc_voltage(upm, 4100);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				pr_info("main_batt_volt:%d, curr_thermal_zones:%d\n", upm->main_batt_volt, upm->thermal_zones);
				if (upm->main_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, true);
				}
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_COLD_S_WARM: /* -20<T1≤0 && 45≤T2<55 */
		case THERMAL_ZONES_M_HOT_S_WARM: /* 55≤T1<60 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, true);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_eoc_voltage(upm, 4100);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 100);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				pr_info("main_batt_volt:%d,slave_batt_volt:%d curr_thermal_zones:%d\n", upm->main_batt_volt, upm->slave_batt_volt, upm->thermal_zones);
				if (upm->main_batt_volt <= 4100 && upm->slave_batt_volt <= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, true);
				} else {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				}
				if (upm->slave_batt_volt >= 4100 || ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0))) {
					set_sub_supllement_mode_en(upm, true);
				} else {
					set_sub_supllement_mode_en(upm, false);
				}
			}
				update_switch_dischg_current_limit(upm);
			break;
		case THERMAL_ZONES_M_COOL_S_COLD: /* 0<T1<15 && -20<T2≤0 */
		case THERMAL_ZONES_M_COOL_S_HOT: /* 0<T1<15 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, true);
				set_main_supllement_mode_en(upm, false);

				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				//set_main_eoc_curr(upm, 200);

				//set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 50);
				//set_sub_eoc_curr(upm, 100);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				pr_info("slave_batt_temp:%d, curr_thermal_zones:%d\n", upm->slave_batt_temp, upm->thermal_zones);
				if (upm->slave_batt_temp >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					if (upm->main_batt_volt >= (upm->slave_batt_volt + 30)) {
						set_main_supllement_mode_en(upm, true);
						charger_dev_enable(primary_charger, false);
						charger_dev_enable_powerpath(primary_charger, true);
					} else {
						set_main_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						charger_dev_enable_powerpath(primary_charger, true);
						if (chr_type != STANDARD_HOST)
							set_main_input_curr(upm, 900);
					}
				}
			}
			break;
		case THERMAL_ZONES_M_COOL_S_COOL: /* 0<T1<15 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_eoc_voltage(upm, 4480);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);

				set_sub_input_curr(upm, 260);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				charger_dev_enable(primary_charger, true);
				charger_dev_enable_powerpath(primary_charger, true);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt >= 3450 ) {
					if (chr_type == STANDARD_HOST) {
						set_sub_supllement_mode_en(upm, false);
						set_main_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
						set_sub_supllement_mode_en(upm, false);
						set_main_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						set_main_input_curr(upm, 1000);
						set_sub_input_curr(upm, 260);
					} else {
						if (upm->main_batt_volt > 4250 && (upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
							set_sub_supllement_mode_en(upm, true);
							set_main_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
						} else if (upm->main_batt_volt > 4250) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						} else {
							charger_dev_enable(primary_charger, true);
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							set_main_input_curr(upm, 2000);
							set_sub_input_curr(upm, 520);
						}
					}
				} else {
					if (chr_type == STANDARD_HOST) {
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					} else {
						set_main_input_curr(upm, 1000);
						set_sub_input_curr(upm, 260);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_COOL_S_NORMAL: /* 0<T1<15 && 15≤T2<45 */
		    if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_eoc_voltage(upm, 4480);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);

				set_sub_input_curr(upm, 260);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				charger_dev_enable(primary_charger, true);
				charger_dev_enable_powerpath(primary_charger, true);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt >= 3450 ) {
					if (chr_type == STANDARD_HOST) {
						set_sub_supllement_mode_en(upm, false);
						set_main_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
						set_sub_supllement_mode_en(upm, false);
						set_main_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						set_main_input_curr(upm, 1000);
						set_sub_input_curr(upm, 260);
					} else {
						if (upm->main_batt_volt > 4250 && (upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
							set_sub_supllement_mode_en(upm, true);
							set_main_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
						} else if (upm->main_batt_volt > 4250) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						} else {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 2000);
							set_sub_input_curr(upm, 520);
						}
					}
				} else {
					if (chr_type == STANDARD_HOST) {
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					} else {
						set_main_input_curr(upm, 1000);
						set_sub_input_curr(upm, 260);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_COOL_S_WARM: /* 0<T1<15 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 260);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				if (upm->slave_batt_volt >= 4100) {
					set_sub_supllement_mode_en(upm, true);
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							set_main_input_curr(upm, 590);
							set_sub_input_curr(upm, 170);
						} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						} else {
							if (upm->main_batt_volt < 4080) {
								set_main_supllement_mode_en(upm, false);
								set_sub_supllement_mode_en(upm, false);
								charger_dev_enable(primary_charger, true);
								set_main_input_curr(upm, 1310);
								set_sub_input_curr(upm, 360);
							} else {
								if ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
									set_main_supllement_mode_en(upm, true);
									set_sub_supllement_mode_en(upm, true);
									charger_dev_enable(primary_charger, false);
									charger_dev_enable_powerpath(primary_charger, true);
								}
							}
						}
					} else {
						if (chr_type == STANDARD_HOST) {
							set_main_input_curr(upm, 590);
							set_sub_input_curr(upm, 170);
						} else {
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						}
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_COLD: /* 15≤T1<45 && -20<T2≤0 */
		case THERMAL_ZONES_M_NORMAL_S_HOT: /* 15≤T1<45 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				set_sub_supllement_mode_en(upm, true);
				set_main_supllement_mode_en(upm, false);

				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_sub_discharging_curr(upm, 50);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				if (upm->slave_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					if (upm->main_batt_volt >= (upm->slave_batt_volt + 30)) {
						set_main_supllement_mode_en(upm, true);
						charger_dev_enable(primary_charger, false);
						charger_dev_enable_powerpath(primary_charger, true);
					} else {
						set_main_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						charger_dev_enable_powerpath(primary_charger, true);
						if (chr_type != STANDARD_HOST)
							set_main_input_curr(upm, 900);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_COOL: /* 15≤T1<45 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_eoc_voltage(upm, 4480);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);

				set_sub_input_curr(upm, 260);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				charger_dev_enable(primary_charger, true);
				charger_dev_enable_powerpath(primary_charger, true);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt >= 3450 ) {
					if (chr_type == STANDARD_HOST) {
						set_sub_supllement_mode_en(upm, false);
						set_main_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
						set_sub_supllement_mode_en(upm, false);
						set_main_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						set_main_input_curr(upm, 1000);
						set_sub_input_curr(upm, 260);
					} else {
						if (upm->main_batt_volt > 4250 && (upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
							set_main_supllement_mode_en(upm, true);
							set_sub_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
						} else if (upm->main_batt_volt > 4250) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						} else {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 2000);
							set_sub_input_curr(upm, 520);
						}
					}
				} else {
					set_main_supllement_mode_en(upm, false);
					set_sub_supllement_mode_en(upm, false);
					if (chr_type == STANDARD_HOST) {
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					} else {
						set_main_input_curr(upm, 1000);
						set_sub_input_curr(upm, 260);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_NORMAL: /* 15≤T1<45 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_eoc_voltage(upm, 4480);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);

				set_sub_input_curr(upm, 260);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				charger_dev_enable(primary_charger, true);
				charger_dev_enable_powerpath(primary_charger, true);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d, mtk_is_support_pps:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt, mtk_is_support_pps());
				if (upm->main_batt_volt >= 3450 ) {
					if (chr_type == STANDARD_HOST) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, true);
						set_main_input_curr(upm, 1000);
						set_sub_input_curr(upm, 260);
					} else if (mtk_is_support_pps()) {
						if (upm->main_batt_volt > 4480 && ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250)) {
							set_main_supllement_mode_en(upm, true);
							set_sub_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
							charger_dev_enable_powerpath(primary_charger, true);
						} else if (upm->main_batt_volt > 4450) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 4280);
							set_sub_input_curr(upm, 1200);
						} else if (upm->main_batt_volt > 4300) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 5500);
							set_sub_input_curr(upm, 1500);
						} else {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 6000);
							set_sub_input_curr(upm, 1620);
						}
					} else {
						if (upm->main_batt_volt > 4250 && ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250)) {
							set_main_supllement_mode_en(upm, true);
							set_sub_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
							charger_dev_enable_powerpath(primary_charger, true);
						} else if (upm->main_batt_volt > 4250) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						} else {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 2000);
							set_sub_input_curr(upm, 520);
						}
					}
				} else {
					set_main_supllement_mode_en(upm, false);
					set_sub_supllement_mode_en(upm, false);
					if (chr_type == STANDARD_HOST) {
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					} else {
						set_main_input_curr(upm, 1000);
						set_sub_input_curr(upm, 260);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_WARM: /* 15≤T1<45 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 260);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->slave_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
					set_sub_supllement_mode_en(upm, true);
				} else {
					charger_dev_enable(primary_charger, true);
					charger_dev_enable_powerpath(primary_charger, true);
					set_sub_supllement_mode_en(upm, false);
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 590);
							set_sub_input_curr(upm, 170);
						} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						} else {
							if (upm->main_batt_volt > 4080 && ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250)) {
								set_sub_supllement_mode_en(upm, true);
								set_main_supllement_mode_en(upm, true);
								charger_dev_enable(primary_charger, false);
							} else {
								set_main_supllement_mode_en(upm, false);
								set_sub_supllement_mode_en(upm, false);
								charger_dev_enable(primary_charger, true);
								set_main_input_curr(upm, 2000);
								set_sub_input_curr(upm, 520);
							}
						}
					} else {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						if (chr_type == STANDARD_HOST) {
							set_main_input_curr(upm, 590);
							set_sub_input_curr(upm, 170);
						} else {
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						}
					}
					update_switch_dischg_current_limit(upm);
				}
			}
			break;
		case THERMAL_ZONES_M_WARM_S_COLD: /* 45≤T1<55 && -20<T2≤0 */
		case THERMAL_ZONES_M_WARM_S_HOT: /* 45≤T1<55 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, true);
				set_main_supllement_mode_en(upm, false);

				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_vol(upm, 4100);

				set_sub_discharging_curr(upm, 50);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt < 4100 && upm->slave_batt_volt < 4100) {
					if (upm->main_batt_volt >= (upm->slave_batt_volt + 30)) {
						set_main_supllement_mode_en(upm, true);
						charger_dev_enable(primary_charger, false);
						charger_dev_enable_powerpath(primary_charger, true);
					} else {
						if (upm->main_batt_volt >= 3450) {
							if (chr_type == STANDARD_HOST) {
								set_main_supllement_mode_en(upm, false);
								set_main_input_curr(upm, 590);
							} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
								set_main_supllement_mode_en(upm, false);
								set_main_input_curr(upm, 1000);
							} else {
								if (upm->main_batt_volt < 4100) {
									set_main_supllement_mode_en(upm, false);
									charger_dev_enable(primary_charger, true);
									set_main_input_curr(upm, 1700);
								} else {
									set_main_supllement_mode_en(upm, true);
									charger_dev_enable(primary_charger, false);
								}
							}
						} else {
							set_main_supllement_mode_en(upm, false);
							set_main_input_curr(upm, 590);
						}
					}
				} else {
						set_main_supllement_mode_en(upm, false);
						charger_dev_enable(primary_charger, false);
						charger_dev_enable_powerpath(primary_charger, false);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_WARM_S_COOL: /* 45≤T1<55 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4100);

				set_sub_input_curr(upm, 260);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
				charger_dev_enable(primary_charger, true);
				charger_dev_enable_powerpath(primary_charger, true);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt >= 4100) {
					set_main_supllement_mode_en(upm, true);
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 590);
							set_sub_input_curr(upm, 170);
							if ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
								set_main_supllement_mode_en(upm, true);
								set_sub_supllement_mode_en(upm, true);
								charger_dev_enable(primary_charger, false);
							}
						} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						} else {
							if (upm->main_batt_volt < 4100) {
								set_sub_supllement_mode_en(upm, false);
								set_main_supllement_mode_en(upm, false);
								charger_dev_enable(primary_charger, true);
								set_main_input_curr(upm, 1700);
								set_sub_input_curr(upm, 460);
							} else {
								set_sub_supllement_mode_en(upm, true);
								set_main_supllement_mode_en(upm, true);
								charger_dev_enable(primary_charger, false);
							}
						}
					} else {
						set_main_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_WARM_S_NORMAL: /* 45≤T1<55 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4100);

				set_sub_input_curr(upm, 260);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_main_input_curr(upm, 590);
							set_sub_input_curr(upm, 170);
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							if ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
								set_sub_supllement_mode_en(upm, true);
								set_main_supllement_mode_en(upm, true);
								charger_dev_enable(primary_charger, false);
							}
						} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						} else {
							if (upm->main_batt_volt < 4100) {
								set_sub_supllement_mode_en(upm, false);
								set_main_supllement_mode_en(upm, false);
								charger_dev_enable(primary_charger, true);
								set_main_input_curr(upm, 1700);
								set_sub_input_curr(upm, 460);
							} else {
								set_sub_supllement_mode_en(upm, true);
								set_main_supllement_mode_en(upm, true);
								charger_dev_enable(primary_charger, false);
							}
						}
					} else {
						set_sub_supllement_mode_en(upm, false);
						set_main_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_WARM_S_WARM: /* 45≤T1<55 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				set_sub_supllement_mode_en(upm, false);
				set_main_supllement_mode_en(upm, false);

				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4100);

				set_sub_input_curr(upm, 260);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt < 4100 && upm->slave_batt_volt < 4100) {
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 590);
							set_sub_input_curr(upm, 170);
							if ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
								set_sub_supllement_mode_en(upm, true);
								set_main_supllement_mode_en(upm, true);
								charger_dev_enable(primary_charger, false);
							}
						} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1000);
							set_sub_input_curr(upm, 260);
						} else {
							if (upm->main_batt_volt < 4100) {
								set_sub_supllement_mode_en(upm, false);
								set_main_supllement_mode_en(upm, false);
								charger_dev_enable(primary_charger, true);
								set_main_input_curr(upm, 1700);
								set_sub_input_curr(upm, 460);
							} else {
								set_sub_supllement_mode_en(upm, true);
								set_main_supllement_mode_en(upm, true);
								charger_dev_enable(primary_charger, false);
							}
						}
					} else {
						set_sub_supllement_mode_en(upm, false);
						set_main_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 590);
						set_sub_input_curr(upm, 170);
					}
				} else {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		default:
			pr_err("Invalid case, Ignore setting the upm2388 switch\n");
			break;
		}
	} else {
		/* run algo for not charging status */
		pr_info("charger not online\n");

		switch (upm->thermal_zones) {
		case THERMAL_ZONES_M_COLD_S_COLD: /* -20<T1≤0 && -20<T2≤0 */
		case THERMAL_ZONES_M_COLD_S_HOT: /* -20<T1≤0 && 55≤T2<60 */
		case THERMAL_ZONES_M_HOT_S_COLD: /* 55≤T1<60 && -20<T2≤0 */
		case THERMAL_ZONES_M_HOT_S_HOT: /* 55≤T1<60 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_supllement_mode_en(upm, true);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_sub_discharging_curr(upm, 50);
				//update_switch_dischg_current_limit_once(upm);
			} else {
				update_switch_dischg_current_limit(upm);
			}
			break;

		case THERMAL_ZONES_M_COLD_S_COOL: /* -20<T1≤0 && 0<T2<15 */
		case THERMAL_ZONES_M_HOT_S_COOL: /* 55≤T1<60 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
			} else {
				/* need confirm with HW */
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_COLD_S_NORMAL: /* -20<T1≤0 && 15≤T2<45 */
		case THERMAL_ZONES_M_HOT_S_NORMAL: /* 55≤T1<60 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
			} else {
				/* need confirm with HW */
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_COLD_S_WARM: /* -20<T1≤0 && 45≤T2<55 */
		case THERMAL_ZONES_M_HOT_S_WARM: /* 55≤T1<60 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 50);
				set_sub_eoc_curr(upm, 100);
				set_sub_eoc_vol(upm, 4100);
			} else {
				/* need confirm with HW */
				if (upm->slave_batt_volt >= 4100) {
					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					}
				} else {
					if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
						if (supllement_mode == 0) {
							pr_info("should enable supllement mode\n");
							upm2388_set_supllement_mode_en(upm, true);
						}
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_COOL_S_COLD: /* 0<T1<15 && -20<T2≤0 */
		case THERMAL_ZONES_M_COOL_S_HOT: /* 0<T1<15 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
/* 				if (supllement_mode == 0) {
					pr_info("should enable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, true);
				} */
				set_sub_supllement_mode_en(upm, true);
				set_main_supllement_mode_en(upm, false);

				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_discharging_curr(upm, 1000);
			}
			break;
		case THERMAL_ZONES_M_COOL_S_COOL: /* 0<T1<15 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1000);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_COOL_S_NORMAL: /* 0<T1<15 && 15≤T2<45 */
		    if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 1000);
				set_sub_discharging_curr(upm, 1000);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					pr_info("slave batt charging current between(0, 200)mA\n");
/* 					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					} */
					set_sub_supllement_mode_en(upm, true);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_COOL_S_WARM: /* 0<T1<15 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1000);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
			} else {
				/* need confirm with HW */
				if (upm->slave_batt_volt >= 4100) {
/* 					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					} */
					set_sub_supllement_mode_en(upm, true);
				} else {
					if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
/* 						upm2388_get_supllement_mode(upm, &supllement_mode);
						if (supllement_mode == 0) {
							pr_info("should enable supllement mode\n");
							upm2388_set_supllement_mode_en(upm, true);
						} */
						set_sub_supllement_mode_en(upm, true);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_COLD: /* 15≤T1<45 && -20<T2≤0 */
		case THERMAL_ZONES_M_NORMAL_S_HOT: /* 15≤T1<45 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
/* 				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 0) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, true);
				} */
				set_sub_supllement_mode_en(upm, true);
				set_main_supllement_mode_en(upm, false);

				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_discharging_curr(upm, 1000);
			} else {
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_COOL: /* 15≤T1<45 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1000);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
/* 					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should disable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					} */
					set_sub_supllement_mode_en(upm, true);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_NORMAL: /* 15≤T1<45 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 1000);
				set_sub_discharging_curr(upm, 1000);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
/* 					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					} */
					set_sub_supllement_mode_en(upm, true);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_WARM: /* 15≤T1<45 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1000);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
			} else {
				/* need confirm with HW */
				if (upm->slave_batt_volt >= 4100) {
/* 					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					} */
					set_sub_supllement_mode_en(upm, true);
				} else {
					if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
/* 						upm2388_get_supllement_mode(upm, &supllement_mode);
						if (supllement_mode == 0) {
							pr_info("should enable supllement mode\n");
							upm2388_set_supllement_mode_en(upm, true);
						} */
						set_sub_supllement_mode_en(upm, true);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_WARM_S_COLD: /* 45≤T1<55 && -20<T2≤0 */
		case THERMAL_ZONES_M_WARM_S_HOT: /* 45≤T1<55 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
/* 				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 0) {
					pr_info("should enable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, true);
				} */
				set_sub_supllement_mode_en(upm, true);
				set_main_supllement_mode_en(upm, false);

				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_discharging_curr(upm, 1000);
			} else {
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_WARM_S_COOL: /* 45≤T1<55 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1000);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
/* 					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					} */
					set_sub_supllement_mode_en(upm, true);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_WARM_S_NORMAL: /* 45≤T1<55 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1000);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
/* 					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					} */
					set_sub_supllement_mode_en(upm, true);
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_WARM_S_WARM: /* 45≤T1<55 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_get_supllement_mode(upm, &supllement_mode);
				if (supllement_mode == 1) {
					pr_info("should disable supllement mode\n");
					upm2388_set_supllement_mode_en(upm, false);
				}
				upm2388_set_trickle_chg_current_limit(upm, 64);
				set_main_input_curr(upm, 8400);
				set_main_discharging_curr(upm, 8400);
				set_main_eoc_curr(upm, 200);
				set_main_eoc_vol(upm, 4480);

				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1000);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
			} else {
				/* need confirm with HW */
				if (upm->slave_batt_volt >= 4100)  {
/* 					upm2388_get_supllement_mode(upm, &supllement_mode);
					if (supllement_mode == 0) {
						pr_info("should enable supllement mode\n");
						upm2388_set_supllement_mode_en(upm, true);
					} */
					set_sub_supllement_mode_en(upm, true);
				} else {
					if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
/* 						upm2388_get_supllement_mode(upm, &supllement_mode);
						if (supllement_mode == 0) {
							pr_info("should enable supllement mode\n");
							upm2388_set_supllement_mode_en(upm, true);
						} */
						set_sub_supllement_mode_en(upm, true);
					}
				}
				update_switch_dischg_current_limit(upm);
			}
			break;
		default:
			pr_err("Invalid case, Ignore setting the upm2388 switch\n");
			break;
		}
	}

	upm2388_dump_regs(upm->client);
	schedule_delayed_work(&upm->switch_update_work, msecs_to_jiffies(NORMAL_TEMP_UPDATE_MS));
}
#endif

#if 0
//dual switch
static void switch_update_work_fn(struct work_struct *work)
{
	struct upm2388 *upm = container_of(work, struct upm2388,
			switch_update_work.work);
	static enum charger_type chr_type;
	union power_supply_propval val = {0};
	//int supllement_mode = -1;
	int rc = 0;

	pr_info("enter\n");

	/* get buck-charger psy device*/
	if (!primary_charger) {
		primary_charger = get_charger_by_name("primary_chg");
		if (!primary_charger) {
			pr_err("get primary charger device failed\n");
			return;
		}
	}
	/* get battery psy device*/
	if (!upm->batt_psy) {
		upm->batt_psy = power_supply_get_by_name("battery");
		if (!upm->batt_psy) {
			pr_err("failed get batt psy device\n");
			return;
		}
		pr_info("found batt psy device\n");
	}

	/* get charger psy device*/
	if (!upm->chg_psy) {
		upm->chg_psy = power_supply_get_by_name("charger");
		if (!upm->chg_psy) {
			pr_err("failed get charger psy device\n");
			return;
		}
		pr_info("found charger psy device\n");
	}

	if (!upm->main_switch_psy) {
		upm->main_switch_psy = power_supply_get_by_name("upm2388_main_ch0");
		if (!upm->main_switch_psy) {
			pr_err("failed get main_switch_psy device\n");
			return;
		}
		pr_info("found main_switch_psy device\n");
	}

	if (!upm->sub_switch_psy) {
		upm->sub_switch_psy = power_supply_get_by_name("upm2388_sub_ch0");
		if (!upm->sub_switch_psy) {
			pr_err("failed get sub_switch_psy device\n");
			return;
		}
		pr_info("found sub_switch_psy device\n");
	}

	/* get current thermal zones */
	update_curr_thermal_zone(upm);

	/* get main battery charging status */
	rc = power_supply_get_property(upm->batt_psy,
					POWER_SUPPLY_PROP_STATUS, &val);
	if (rc < 0) {
		pr_err("failed get main battery status\n");
		return;
	} else {
		upm->main_batt_status = val.intval;
		pr_info("get main battery status: %d\n", upm->main_batt_status);
	}

	/* get charger online status */
	rc = power_supply_get_property(upm->chg_psy,
					POWER_SUPPLY_PROP_ONLINE, &val);
	if (rc < 0) {
		pr_err("failed get charger online status\n");
		return;
	} else {
		upm->chr_online_status = val.intval;
		pr_info("get charger online status: %d\n", upm->chr_online_status);
	}

	/* get charger type */
	chr_type = mt_get_charger_type();

	if (upm->chr_online_status == 1) {
		/* run algo for charging status */
		pr_info("charger online\n");

		switch (upm->thermal_zones) {
		case THERMAL_ZONES_M_COLD_S_COLD: /* -20<T1≤0 && -20<T2≤0 */
		case THERMAL_ZONES_M_COLD_S_HOT: /* -20<T1≤0 && 55≤T2<60 */
		case THERMAL_ZONES_M_HOT_S_COLD: /* 55≤T1<60 && -20<T2≤0 */
		case THERMAL_ZONES_M_HOT_S_HOT: /* 55≤T1<60 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				set_main_supllement_mode_en(upm, true);

				set_main_discharging_curr(upm, 4000);

				set_sub_supllement_mode_en(upm, true);
				set_sub_discharging_curr(upm, 1500);
				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
				//update_switch_dischg_current_limit_once(upm);
			} else {
				pr_info("main_batt_volt:%d, slave_batt_volt:%d, curr_thermal_zones:%d\n", upm->main_batt_volt, upm->slave_batt_volt, upm->thermal_zones);
				if (upm->main_batt_volt < 4100 && upm->slave_batt_volt < 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, true);
				} else {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				}
			}
			break;
		case THERMAL_ZONES_M_COLD_S_COOL: /* -20<T1≤0 && 0<T2<15 */
		case THERMAL_ZONES_M_HOT_S_COOL: /* 55≤T1<60 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, true);
				set_main_discharging_curr(upm, 4000);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				pr_info("main_batt_volt:%d, curr_thermal_zones:%d\n", upm->main_batt_volt, upm->thermal_zones);
				if (upm->main_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
					set_sub_input_curr(upm, 360);
					if (upm->slave_batt_ave_curr < 100)
						set_sub_supllement_mode_en(upm, true);
				} else {
					set_sub_input_curr(upm, 500);
					charger_dev_enable_powerpath(primary_charger, true);
					if (upm->slave_batt_volt > (upm->main_batt_volt + 20)) {
						charger_dev_enable(primary_charger, false);
						set_sub_supllement_mode_en(upm, true);
					} else {
						if (upm->slave_batt_volt >= 4120 && upm->slave_batt_ave_curr < 100) {
							charger_dev_enable(primary_charger, false);
							set_sub_supllement_mode_en(upm, true);
						} else {
							charger_dev_enable(primary_charger, true);
							set_sub_supllement_mode_en(upm, false);
						}
					}
				}
			}
			break;
		case THERMAL_ZONES_M_COLD_S_NORMAL: /* -20<T1≤0 && 15≤T2<45 */
		case THERMAL_ZONES_M_HOT_S_NORMAL: /* 55≤T1<60 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, true);
				set_main_discharging_curr(upm, 4000);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				pr_info("main_batt_volt:%d, curr_thermal_zones:%d\n", upm->main_batt_volt, upm->thermal_zones);
				if (upm->main_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
					set_sub_supllement_mode_en(upm, false);
					set_sub_input_curr(upm, 360);
					if (upm->slave_batt_ave_curr < 100)
						set_sub_supllement_mode_en(upm, true);
				} else {
					charger_dev_enable_powerpath(primary_charger, true);
					if (upm->slave_batt_volt > (upm->main_batt_volt + 20)) {
						set_sub_supllement_mode_en(upm, true);
						charger_dev_enable(primary_charger, false);
						charger_dev_enable_powerpath(primary_charger, true);
					} else {
						if (chr_type == STANDARD_HOST) {
							charger_dev_enable(primary_charger, true);
							set_sub_supllement_mode_en(upm, false);
							set_sub_input_curr(upm, 500);
							if (upm->slave_batt_volt > 4120 && upm->slave_batt_ave_curr < 100) {
								set_sub_supllement_mode_en(upm, true);
								charger_dev_enable(primary_charger, false);
							}
						} else {
							if (upm->slave_batt_volt > 4120 && upm->slave_batt_ave_curr < 100) {
								set_sub_supllement_mode_en(upm, true);
								charger_dev_enable(primary_charger, false);
							} else {
								set_sub_supllement_mode_en(upm, false);
								charger_dev_enable(primary_charger, true);
								set_sub_input_curr(upm, 1000);
							}
						}
					}
				}
			}
			break;
		case THERMAL_ZONES_M_COLD_S_WARM: /* -20<T1≤0 && 45≤T2<55 */
		case THERMAL_ZONES_M_HOT_S_WARM: /* 55≤T1<60 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 64);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, true);
				set_main_discharging_curr(upm, 4000);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				pr_info("main_batt_volt:%d,slave_batt_volt:%d curr_thermal_zones:%d\n", upm->main_batt_volt, upm->slave_batt_volt, upm->thermal_zones);
				if (upm->main_batt_volt >= 4100 && upm->slave_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
					set_sub_supllement_mode_en(upm, true);
				} else if (upm->main_batt_volt < 4100 && upm->slave_batt_volt < 4100) {
					charger_dev_enable_powerpath(primary_charger, true);
					if (upm->slave_batt_volt > (upm->main_batt_volt + 20)) {
						charger_dev_enable(primary_charger, false);
						set_sub_supllement_mode_en(upm, true);
					} else {
						if (upm->slave_batt_volt >= 4080 && upm->slave_batt_ave_curr < 100) {
							set_sub_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
						} else {
							set_sub_input_curr(upm, 500);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
						}
					}
				} else {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
					set_sub_supllement_mode_en(upm, false);
					set_sub_input_curr(upm, 360);
				}
			}
			break;
		case THERMAL_ZONES_M_COOL_S_COLD: /* 0<T1<15 && -20<T2≤0 */
		case THERMAL_ZONES_M_COOL_S_HOT: /* 0<T1<15 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 360);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);

				set_sub_supllement_mode_en(upm, true);
				set_sub_discharging_curr(upm, 1500);

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				pr_info("slave_batt_temp:%d, curr_thermal_zones:%d\n", upm->slave_batt_temp, upm->thermal_zones);
				if (upm->slave_batt_temp >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
					set_main_supllement_mode_en(upm, false);
					set_main_input_curr(upm, 360);
					if (upm->main_batt_ave_curr < 150)
						set_main_supllement_mode_en(upm, true);
				} else {
					charger_dev_enable_powerpath(primary_charger, true);
					if (upm->main_batt_volt >= (upm->slave_batt_volt + 20)) {
						set_main_supllement_mode_en(upm, true);
						charger_dev_enable(primary_charger, false);
					} else {
						if (upm->main_batt_volt > 4120 && upm->main_batt_ave_curr < 150) {
							set_main_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
						} else {
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_sub_input_curr(upm, 500);
						}
					}
				}
			}
			break;
		case THERMAL_ZONES_M_COOL_S_COOL: /* 0<T1<15 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 460);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);

				set_sub_supllement_mode_en(upm,false);
				set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);

				charger_dev_enable(primary_charger, true);
				charger_dev_enable_powerpath(primary_charger, true);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt >= 3450) {
					if (chr_type == STANDARD_HOST) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 460);
						set_sub_input_curr(upm, 170);
					} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 720);
						set_sub_input_curr(upm, 260);
					} else {
						if (upm->main_batt_volt > 4250 && (upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
							set_main_supllement_mode_en(upm, true);
							set_sub_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
							charger_dev_enable_powerpath(primary_charger, true);
						} else if (upm->main_batt_volt > 4250) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 720);
							set_sub_input_curr(upm, 260);
						} else {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1430);
							set_sub_input_curr(upm, 520);
						}
					}
				} else {
					set_main_supllement_mode_en(upm, false);
					set_sub_supllement_mode_en(upm, false);
					if (chr_type == STANDARD_HOST) {
						set_main_input_curr(upm, 460);
						set_sub_input_curr(upm, 170);
					} else {
						set_main_input_curr(upm, 720);
						set_sub_input_curr(upm, 260);
					}
				}
			}
			break;
		case THERMAL_ZONES_M_COOL_S_NORMAL: /* 0<T1<15 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 460);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);

				set_sub_supllement_mode_en(upm,false);
				set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);

				charger_dev_enable(primary_charger, true);
				charger_dev_enable_powerpath(primary_charger, true);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt >= 3450) {
					if (chr_type == STANDARD_HOST) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 460);
						set_sub_input_curr(upm, 170);
					} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 720);
						set_sub_input_curr(upm, 260);
					} else {
						if (upm->main_batt_volt > 4250 && (upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
							set_main_supllement_mode_en(upm, true);
							set_sub_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
							charger_dev_enable_powerpath(primary_charger, true);
						} else if (upm->main_batt_volt > 4250) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 720);
							set_sub_input_curr(upm, 260);
						} else {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1430);
							set_sub_input_curr(upm, 520);
						}
					}
				} else {
					set_main_supllement_mode_en(upm, false);
					set_sub_supllement_mode_en(upm, false);
					if (chr_type == STANDARD_HOST) {
						set_main_input_curr(upm, 460);
						set_sub_input_curr(upm, 170);
					} else {
						set_main_input_curr(upm, 720);
						set_sub_input_curr(upm, 260);
					}
				}
			}
			break;
		case THERMAL_ZONES_M_COOL_S_WARM: /* 0<T1<15 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 460);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				if (upm->slave_batt_volt >= 4100) {
					set_sub_supllement_mode_en(upm, true);
					set_main_supllement_mode_en(upm, false);
					set_main_input_curr(upm, 360);
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					charger_dev_enable(primary_charger, true);
					charger_dev_enable_powerpath(primary_charger, true);
					pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							if (upm->main_batt_volt < 4080) {
								set_sub_supllement_mode_en(upm, false);
								set_main_supllement_mode_en(upm, false);
								set_main_input_curr(upm, 720);
								set_sub_input_curr(upm, 260);
							} else {
								if ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
									set_main_supllement_mode_en(upm, true);
									set_sub_supllement_mode_en(upm,true);
									charger_dev_enable(primary_charger, false);
									charger_dev_enable_powerpath(primary_charger, true);
								}
							}
						}
					} else {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm,false);
						if (chr_type == STANDARD_HOST) {
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							set_main_input_curr(upm, 720);
							set_sub_input_curr(upm, 260);
						}
					}
				//update_switch_dischg_current_limit(upm);
				}
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_COLD: /* 15≤T1<45 && -20<T2≤0 */
		case THERMAL_ZONES_M_NORMAL_S_HOT: /* 15≤T1<45 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 360);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);

				set_sub_supllement_mode_en(upm, true);
/* 				set_sub_input_curr(upm, 170); */
				set_sub_discharging_curr(upm, 1500);
/* 				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100); */

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				if (upm->slave_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
					set_main_input_curr(upm, 360);
					if (upm->main_batt_ave_curr < 150 && upm->main_batt_ave_curr > 0) {
						set_main_supllement_mode_en(upm, true);
					}
				} else {
					if (upm->main_batt_volt >= (upm->slave_batt_volt + 20)) {
						set_main_supllement_mode_en(upm, true);
						charger_dev_enable(primary_charger, false);
						charger_dev_enable_powerpath(primary_charger, true);
					} else {
						if (upm->main_batt_ave_curr < 150 && upm->main_batt_ave_curr > 0) {
							set_main_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
							charger_dev_enable_powerpath(primary_charger, true);
						} else {
							set_main_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							charger_dev_enable_powerpath(primary_charger, true);
							if (chr_type == STANDARD_HOST)
								set_main_input_curr(upm, 500);
							else
								set_main_input_curr(upm, 1000);
						}
					}
				}
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_COOL: /* 15≤T1<45 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 460);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);

				charger_dev_enable(primary_charger, true);
				charger_dev_enable_powerpath(primary_charger, true);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt >= 3450) {
					if (chr_type == STANDARD_HOST) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 460);
						set_sub_input_curr(upm, 170);
					} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 720);
						set_sub_input_curr(upm, 260);
					} else {
						if (upm->main_batt_volt > 4250 && (upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
							set_main_supllement_mode_en(upm, true);
							set_sub_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
							charger_dev_enable_powerpath(primary_charger, true);
						} else if (upm->main_batt_volt > 4250) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 720);
							set_sub_input_curr(upm, 260);
						} else {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1430);
							set_sub_input_curr(upm, 520);
						}
					}
				} else {
					set_main_supllement_mode_en(upm, false);
					set_sub_supllement_mode_en(upm, false);
					if (chr_type == STANDARD_HOST) {
						set_main_input_curr(upm, 460);
						set_sub_input_curr(upm, 170);
					} else {
						set_main_input_curr(upm, 720);
						set_sub_input_curr(upm, 260);
					}
				}
				//update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_NORMAL: /* 15≤T1<45 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 460);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);

				charger_dev_enable(primary_charger, true);
				charger_dev_enable_powerpath(primary_charger, true);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt >= 3450) {
					if (chr_type == STANDARD_HOST) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 460);
						set_sub_input_curr(upm, 170);
					} else if (chr_type == CHARGING_HOST || chr_type == NONSTANDARD_CHARGER) {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 720);
						set_sub_input_curr(upm, 260);
					} else {
						if (upm->main_batt_volt > 4250 && (upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
							set_main_supllement_mode_en(upm, true);
							set_sub_supllement_mode_en(upm, true);
							charger_dev_enable(primary_charger, false);
							charger_dev_enable_powerpath(primary_charger, true);
						} else if (upm->main_batt_volt > 4250) {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 720);
							set_sub_input_curr(upm, 260);
						} else {
							set_main_supllement_mode_en(upm, false);
							set_sub_supllement_mode_en(upm, false);
							charger_dev_enable(primary_charger, true);
							set_main_input_curr(upm, 1430);
							set_sub_input_curr(upm, 520);
						}
					}
				} else {
					set_main_supllement_mode_en(upm, false);
					set_sub_supllement_mode_en(upm, false);
					if (chr_type == STANDARD_HOST) {
						set_main_input_curr(upm, 460);
						set_sub_input_curr(upm, 170);
					} else {
						set_main_input_curr(upm, 720);
						set_sub_input_curr(upm, 260);
					}
				}
				//update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_WARM: /* 15≤T1<45 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 460);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				if (upm->slave_batt_volt >= 4100) {
					set_sub_supllement_mode_en(upm, true);
					set_main_supllement_mode_en(upm, false);
					set_main_input_curr(upm, 360);
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					charger_dev_enable(primary_charger, true);
					charger_dev_enable_powerpath(primary_charger, true);
					pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							if (upm->main_batt_volt < 4080) {
								set_sub_supllement_mode_en(upm, false);
								set_main_supllement_mode_en(upm, false);
								set_main_input_curr(upm, 720);
								set_sub_input_curr(upm, 260);
							} else if ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
								set_main_supllement_mode_en(upm, true);
								set_sub_supllement_mode_en(upm,true);
								charger_dev_enable(primary_charger, false);
								charger_dev_enable_powerpath(primary_charger, true);
							}
						}
					} else {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm,false);
						if (chr_type == STANDARD_HOST) {
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							set_main_input_curr(upm, 720);
							set_sub_input_curr(upm, 260);
						}
					}
				//update_switch_dischg_current_limit(upm);
				}
			}
			break;
		case THERMAL_ZONES_M_WARM_S_COLD: /* 45≤T1<55 && -20<T2≤0 */
		case THERMAL_ZONES_M_WARM_S_HOT: /* 45≤T1<55 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 360);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 130);
				set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, true);
/* 				set_sub_input_curr(upm, 170); */
				set_sub_discharging_curr(upm, 1500);
/* 				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100); */

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt < 4100 && upm->slave_batt_volt < 4100) {
					if (upm->main_batt_volt >= (upm->slave_batt_volt + 20)) {
						set_main_supllement_mode_en(upm, true);
						charger_dev_enable(primary_charger, false);
						charger_dev_enable_powerpath(primary_charger, true);
					} else {
						charger_dev_enable(primary_charger, true);
						set_main_supllement_mode_en(upm, false);
						set_main_input_curr(upm, 500);
					}
				} else if (upm->main_batt_volt >= 4100 && upm->slave_batt_volt >= 4100) {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
					set_main_supllement_mode_en(upm, true);
				} else {
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
					set_main_supllement_mode_en(upm, false);
					set_main_input_curr(upm, 360);
				}
				//update_switch_dischg_current_limit(upm);
			}
			break;
		case THERMAL_ZONES_M_WARM_S_COOL: /* 45≤T1<55 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 460);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->slave_batt_volt >= 4100) {
					set_main_supllement_mode_en(upm, true);
					set_sub_supllement_mode_en(upm, false);
					set_sub_input_curr(upm, 360);
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					charger_dev_enable(primary_charger, true);
					charger_dev_enable_powerpath(primary_charger, true);
					pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							if (upm->main_batt_volt < 4080) {
								set_sub_supllement_mode_en(upm, false);
								set_main_supllement_mode_en(upm, false);
								set_main_input_curr(upm, 720);
								set_sub_input_curr(upm, 260);
							} else if ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
								set_main_supllement_mode_en(upm, true);
								set_sub_supllement_mode_en(upm,true);
								charger_dev_enable(primary_charger, false);
								charger_dev_enable_powerpath(primary_charger, true);
							}
						}
					} else {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm,false);
						if (chr_type == STANDARD_HOST) {
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							set_main_input_curr(upm, 720);
							set_sub_input_curr(upm, 260);
						}
					}
				//update_switch_dischg_current_limit(upm);
				}
			}
			break;
		case THERMAL_ZONES_M_WARM_S_NORMAL: /* 45≤T1<55 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 360);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->slave_batt_volt >= 4100) {
					set_main_supllement_mode_en(upm, true);
					set_sub_supllement_mode_en(upm, false);
					set_sub_input_curr(upm, 360);
					charger_dev_enable(primary_charger, false);
					charger_dev_enable_powerpath(primary_charger, false);
				} else {
					charger_dev_enable(primary_charger, true);
					charger_dev_enable_powerpath(primary_charger, true);
					pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							if (upm->main_batt_volt < 4080) {
								set_sub_supllement_mode_en(upm, false);
								set_main_supllement_mode_en(upm, false);
								set_main_input_curr(upm, 720);
								set_sub_input_curr(upm, 260);
							} else if ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
								set_main_supllement_mode_en(upm, true);
								set_sub_supllement_mode_en(upm,true);
								charger_dev_enable(primary_charger, false);
								charger_dev_enable_powerpath(primary_charger, true);
							}
						}
					} else {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm,false);
						if (chr_type == STANDARD_HOST) {
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							set_main_input_curr(upm, 720);
							set_sub_input_curr(upm, 260);
						}
					}
				//update_switch_dischg_current_limit(upm);
				}
			}
			break;
		case THERMAL_ZONES_M_WARM_S_WARM: /* 45≤T1<55 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 360);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);

				charger_dev_enable(primary_charger, false);
				charger_dev_enable_powerpath(primary_charger, false);
			} else {
				/* need confirm with HW */
				pr_info("curr_thermal_zones:%d, chr_type:%d, main_batt_volt:%d\n", upm->thermal_zones, chr_type, upm->main_batt_volt);
				if (upm->main_batt_volt < 4100 && upm->slave_batt_volt < 4100) {
					charger_dev_enable(primary_charger, true);
					charger_dev_enable_powerpath(primary_charger, true);
					if (upm->main_batt_volt >= 3450 ) {
						if (chr_type == STANDARD_HOST) {
							set_sub_supllement_mode_en(upm, false);
							set_main_supllement_mode_en(upm, false);
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							if (upm->main_batt_volt < 4080) {
								set_sub_supllement_mode_en(upm, false);
								set_main_supllement_mode_en(upm, false);
								set_main_input_curr(upm, 720);
								set_sub_input_curr(upm, 260);
							} else if ((upm->main_batt_ave_curr + upm->slave_batt_ave_curr) <= 250) {
								set_main_supllement_mode_en(upm, true);
								set_sub_supllement_mode_en(upm,true);
								charger_dev_enable(primary_charger, false);
								charger_dev_enable_powerpath(primary_charger, true);
							}
						}
					} else {
						set_main_supllement_mode_en(upm, false);
						set_sub_supllement_mode_en(upm,false);
						if (chr_type == STANDARD_HOST) {
							set_main_input_curr(upm, 460);
							set_sub_input_curr(upm, 170);
						} else {
							set_main_input_curr(upm, 720);
							set_sub_input_curr(upm, 260);
						}
					}
				} else {
						set_main_supllement_mode_en(upm, true);
						set_sub_supllement_mode_en(upm,true);
						charger_dev_enable(primary_charger, false);
						charger_dev_enable_powerpath(primary_charger, false);
				}
			}
			break;
		default:
			pr_err("Invalid case, Ignore setting the upm2388 switch\n");
			break;
		}
	} else {
		/* run algo for not charging status */
		pr_info("charger not online\n");

		switch (upm->thermal_zones) {
		case THERMAL_ZONES_M_COLD_S_COLD: /* -20<T1≤0 && -20<T2≤0 */
		case THERMAL_ZONES_M_COLD_S_HOT: /* -20<T1≤0 && 55≤T2<60 */
		case THERMAL_ZONES_M_HOT_S_COLD: /* 55≤T1<60 && -20<T2≤0 */
		case THERMAL_ZONES_M_HOT_S_HOT: /* 55≤T1<60 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				//upm->pre_thermal_zones = upm->thermal_zones;

				//upm2388_set_trickle_chg_current_limit(upm, 170);
				//upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, true);
				//set_main_input_curr(upm, 360);
				set_main_discharging_curr(upm, 4000);
				//set_main_eoc_curr(upm, 170);
				//set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, true);
				//set_sub_input_curr(upm, 170);
				set_sub_discharging_curr(upm, 1500);
				//set_sub_eoc_curr(upm, 130);
				//set_sub_eoc_vol(upm, 4100);
			}
			break;

		case THERMAL_ZONES_M_COLD_S_COOL: /* -20<T1≤0 && 0<T2<15 */
		case THERMAL_ZONES_M_HOT_S_COOL: /* 55≤T1<60 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, true);
				//set_main_input_curr(upm, 360);
				set_main_discharging_curr(upm, 4000);
				//set_main_eoc_curr(upm, 170);
				//set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);

			} else {
				/* need confirm with HW */
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_COLD_S_NORMAL: /* -20<T1≤0 && 15≤T2<45 */
		case THERMAL_ZONES_M_HOT_S_NORMAL: /* 55≤T1<60 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, true);
				//set_main_input_curr(upm, 360);
				set_main_discharging_curr(upm, 4000);
				//set_main_eoc_curr(upm, 170);
				//set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);

			} else {
				/* need confirm with HW */
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_COLD_S_WARM: /* -20<T1≤0 && 45≤T2<55 */
		case THERMAL_ZONES_M_HOT_S_WARM: /* 55≤T1<60 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, true);
				//set_main_input_curr(upm, 360);
				set_main_discharging_curr(upm, 4000);
				//set_main_eoc_curr(upm, 170);
				//set_main_eoc_vol(upm, 4100);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
			} else {
				/* need confirm with HW */
				if (upm->slave_batt_volt >= 4100 || ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0))) {
					set_sub_supllement_mode_en(upm, true);
				} else {
					set_sub_supllement_mode_en(upm, false);
				}
			}
			break;
		case THERMAL_ZONES_M_COOL_S_COLD: /* 0<T1<15 && -20<T2≤0 */
		case THERMAL_ZONES_M_COOL_S_HOT: /* 0<T1<15 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);

				set_sub_supllement_mode_en(upm, true);
				//set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				//set_sub_eoc_curr(upm, 130);
				//set_sub_eoc_vol(upm, 4100);
			} else {
				if ((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0)) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_COOL_S_COOL: /* 0<T1<15 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 900);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
				if((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0)) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_COOL_S_NORMAL: /* 0<T1<15 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;

				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
				if((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0)) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_COOL_S_WARM: /* 0<T1<15 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_trickle_chg_current_limit(upm, 170);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);
				set_main_recharge_vol(upm, 4380);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
				set_sub_recharge_vol(upm, 4000);
			} else {
				if((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0)) {
					set_main_supllement_mode_en(upm, true);
				}
				if (upm->slave_batt_volt >= 4100 || ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0))) {
					set_sub_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_COLD: /* 15≤T1<45 && -20<T2≤0 */
		case THERMAL_ZONES_M_NORMAL_S_HOT: /* 15≤T1<45 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_trickle_chg_current_limit(upm, 170);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);
				set_main_recharge_vol(upm, 4380);

				set_sub_supllement_mode_en(upm, true);
				//set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1500);
				//set_sub_eoc_curr(upm, 130);
				//set_sub_eoc_vol(upm, 4100);
				//set_sub_recharge_vol(upm, 4000);
			} else {
				if((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0)) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_COOL: /* 15≤T1<45 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);
				//set_main_recharge_vol(upm, 4380);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
				//set_sub_recharge_vol(upm, 4380);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
				if((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0)) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_NORMAL: /* 15≤T1<45 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);
				//set_main_recharge_vol(upm, 4380);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
				//set_sub_recharge_vol(upm, 4380);
			} else {
				if ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
				if((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0)) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_NORMAL_S_WARM: /* 15≤T1<45 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_trickle_chg_current_limit(upm, 170);
				//upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4480);
				set_main_recharge_vol(upm, 4380);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
				set_sub_recharge_vol(upm, 4000);
			} else {
				if((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0)) {
					set_main_supllement_mode_en(upm, true);
				}
				if (upm->slave_batt_volt >= 4100 || ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0))) {
					set_sub_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_WARM_S_COLD: /* 45≤T1<55 && -20<T2≤0 */
		case THERMAL_ZONES_M_WARM_S_HOT: /* 45≤T1<55 && 55≤T2<60 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_trickle_chg_current_limit(upm, 170);
				//upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4100);
				set_main_recharge_vol(upm, 4000);

				set_sub_supllement_mode_en(upm, true);
				//set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				//set_sub_eoc_curr(upm, 130);
				//set_sub_eoc_vol(upm, 4100);
				//set_sub_recharge_vol(upm, 4000);
			} else {
				if (upm->main_batt_volt >= 4100 || ((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0))) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_WARM_S_COOL: /* 45≤T1<55 && 0<T2<15 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_trickle_chg_current_limit(upm, 170);
				//upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4100);
				set_main_recharge_vol(upm, 4000);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 360);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
				set_sub_recharge_vol(upm, 4380);
			} else {
				if((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
				if (upm->main_batt_volt >= 4100 || ((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0))) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_WARM_S_NORMAL: /* 45≤T1<55 && 15≤T2<45 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_trickle_chg_current_limit(upm, 170);
				//upm2388_set_recharge_voltage(upm, 4380);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4100);
				set_main_recharge_vol(upm, 4000);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4480);
				set_sub_recharge_vol(upm, 4380);
			} else {
				if((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0)) {
					set_sub_supllement_mode_en(upm, true);
				}
				if (upm->main_batt_volt >= 4100 || ((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0))) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		case THERMAL_ZONES_M_WARM_S_WARM: /* 45≤T1<55 && 45≤T2<55 */
			if (upm->pre_thermal_zones != upm->thermal_zones) {
				pr_info("curr_thermal_zones:%d, pre_thermel_zones:%d\n", upm->thermal_zones, upm->pre_thermal_zones);
				upm->pre_thermal_zones = upm->thermal_zones;
				upm2388_set_trickle_chg_current_limit(upm, 170);
				upm2388_set_recharge_voltage(upm, 4000);

				set_main_supllement_mode_en(upm, false);
				set_main_input_curr(upm, 500);
				set_main_discharging_curr(upm, 4000);
				set_main_eoc_curr(upm, 170);
				set_main_eoc_vol(upm, 4100);
				//set_main_recharge_vol(upm, 4000);

				set_sub_supllement_mode_en(upm, false);
				set_sub_input_curr(upm, 500);
				set_sub_discharging_curr(upm, 1500);
				set_sub_eoc_curr(upm, 130);
				set_sub_eoc_vol(upm, 4100);
				//set_sub_recharge_vol(upm, 4380);
			} else {
				if(upm->slave_batt_volt >=4100 || ((upm->slave_batt_ave_curr <= 200) && (upm->slave_batt_ave_curr > 0))) {
					set_sub_supllement_mode_en(upm, true);
				}
				if (upm->main_batt_volt >= 4100 || ((upm->main_batt_ave_curr <= 200) && (upm->main_batt_ave_curr > 0))) {
					set_main_supllement_mode_en(upm, true);
				}
			}
			break;
		default:
			pr_err("Invalid case, Ignore setting the upm2388 switch\n");
			break;
		}
	}

	upm2388_dump_regs(upm->client);
	schedule_delayed_work(&upm->switch_update_work, msecs_to_jiffies(NORMAL_TEMP_UPDATE_MS));
}
#endif

int upm2388_algo_init(struct upm2388 *upm)
{
	int rc = 0;

	pr_info("start\n");

	upm->main_batt_curr = -EINVAL;
	upm->main_batt_soc = -EINVAL;
	upm->main_batt_status = -EINVAL;
	upm->main_batt_temp = -EINVAL;
	upm->main_batt_volt = -EINVAL;
	upm->main_batt_ave_curr = -EINVAL;

	upm->slave_batt_curr = -EINVAL;
	upm->slave_batt_soc = -EINVAL;
	upm->slave_batt_status = -EINVAL;
	upm->slave_batt_temp = -EINVAL;
	upm->slave_batt_volt = -EINVAL;
	upm->slave_batt_ave_curr = -EINVAL;

	upm->thermal_zones = THERMAL_ZONES_INIT;
	upm->pre_thermal_zones = THERMAL_ZONES_INIT;

	upm->limit_dischg_curr = -EINVAL;
	upm->pre_limit_dischg_curr = -EINVAL;

	INIT_DELAYED_WORK(&upm->switch_update_work, switch_update_work_fn);
	INIT_DELAYED_WORK(&upm->batt_status_update_work, batt_status_update_work_fn);

	schedule_delayed_work(&upm->switch_update_work, msecs_to_jiffies(NORMAL_TEMP_UPDATE_MS));
	schedule_delayed_work(&upm->batt_status_update_work, msecs_to_jiffies(WAIT_FOR_CHECK_STATUS_MS));

	return rc;
}

int upm2388_algo_deinit(struct upm2388 *upm)
{
	int rc = 0;

	pr_info("exit\n");

	cancel_delayed_work_sync(&upm->switch_update_work);
	cancel_delayed_work_sync(&upm->batt_status_update_work);

	return rc;
}
