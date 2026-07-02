#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/reboot.h>
#include "mtk_charger_algorithm_class.h"
#include "turbo_charger.h"
#include "charger_class.h"
#include <linux/version.h>

#include "tinno_charger.h"
#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include <dev_info.h>
#endif
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
extern bool qc_logic_probe_done;
extern bool mtk_can_charging;
extern bool is_turbo_charger_ready;
extern bool turbo_charger_active;
/*TN Begin modified by lingfei.tang/77407 20231201 CR/EKFOGO4G-5993*/
extern int ffc_reduce_count;
/*TN End modified by lingfei.tang/77407 20231201 CR/EKFOGO4G-5993*/
extern int z350_set_volt_count(int count);
extern int z350_reset_charger_type(void);
extern int wt6670f_set_volt_count(int count);
extern int wt6670f_reset_charger_type(void);
extern bool ffc_batt_full;
#endif

extern int g_thermal_charging_current_limit;  /*TN add by chao.zhang1/860682 20230926 CR/EKFOGO4G-1785*/

static int log_level = TURBO_CHARGER_DBG_LEVEL;
module_param(log_level, int, 0644);

int turbo_charger_get_log_level(void)
{
	return log_level;
}

static int heartbeat_delay_ms = -1;
bool g_capacity_level_limited_flag = 0;
bool g_battery_protect_enable_flag = 0;
static bool ignore_hysteresis_degc = false;
//static int g_in_flag = 0;

static const struct turbo_charger_config turbo_config = {
	.bat_volt_lp_lmt        = BAT_VOLT_LOOP_LMT,
	.bat_curr_lp_lmt        = BAT_CURR_LOOP_LMT,
	.bus_volt_lp_lmt        = BUS_VOLT_LOOP_LMT,
	.bus_curr_lp_lmt        = (BAT_CURR_LOOP_LMT >> 1),

	.fc2_taper_current      = 20000,    //20mA
	.fc2_steps              = 1,

	.min_adapter_volt_required  = 11000,
	.min_adapter_curr_required  = 5000,

	.min_vbat_for_cp        = 3500,

	.cp_sec_enable          = false,
	.fc2_disable_sw         = false,
};

static int turbo_charger_check_cp_psy(struct turbo_charger_algo_info *info)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(info->cp_psy)) {
		info->cp_psy = power_supply_get_by_name("cp-standalone");
		if (IS_ERR_OR_NULL(info->cp_psy)) {
			TURBO_CHARGER_ERR("cp-standalone not found\n");
			return -ENODEV;
		}
	}

	TURBO_CHARGER_INFO("the cp_psy is %s\n", info->cp_psy->desc->name);

	return ret;
}

static int turbo_charger_check_usb_psy(struct turbo_charger_algo_info *info)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(info->usb_psy)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
		info->usb_psy = power_supply_get_by_name("primary_chg");
#else
		info->usb_psy = power_supply_get_by_name("ext_charger_type");
#endif /* LINUX_VERSION_CODE */
		if (IS_ERR_OR_NULL(info->usb_psy)) {
			TURBO_CHARGER_ERR("usb_psy not found\n");
			return -ENODEV;
		}
	}

	TURBO_CHARGER_INFO("the usb_psy is %s\n", info->usb_psy->desc->name);

	return ret;
}

static int turbo_charger_check_batt_psy(struct turbo_charger_algo_info *info)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(info->batt_psy)) {
		info->batt_psy = power_supply_get_by_name("battery");
		if (IS_ERR_OR_NULL(info->batt_psy)) {
			TURBO_CHARGER_ERR("batt psy not found\n");
			return -ENODEV;
		}
	}

	TURBO_CHARGER_INFO("the psy is %s\n", info->batt_psy->desc->name);

	return ret;
}

static int turbo_charger_get_sw_device(struct turbo_charger_algo_info *info)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(info->sw_chg)) {
		info->sw_chg = get_charger_by_name("primary_chg");
		if (IS_ERR_OR_NULL(info->sw_chg)) {
			TURBO_CHARGER_ERR("get primary_chg failed\n");
			return -EINVAL;
		}
	}

	TURBO_CHARGER_DBG("get primary_chg successfully\n");

	return ret;
}

static int turbo_charger_update_sw_status(struct turbo_charger_algo_info *info)
{
	int ret = 0;
	bool charge_enabled = false;
	union power_supply_propval val = { 0,};

	ret = power_supply_get_property(info->usb_psy,
					POWER_SUPPLY_PROP_ONLINE, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get usb online failed(%d)\n", ret);
	} else {
		info->sw.usb_online = val.intval;
	}

	ret = charger_dev_is_enabled(info->sw_chg, &charge_enabled);
	if (ret) {
		TURBO_CHARGER_ERR("get charger enabled status failed(%d)\n", ret);
	} else {
		info->sw.charge_enabled = charge_enabled;
	}

	ret = power_supply_get_property(info->usb_psy,
					POWER_SUPPLY_PROP_USB_TYPE, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get charger type failed(%d)\n", ret);
	} else {
		info->sw.charger_type = val.intval;
	}

	TURBO_CHARGER_DBG("usb_online:%d, chg_enabled:%d, charger_type:%d\n", info->sw.usb_online, info->sw.charge_enabled, info->sw.charger_type);

	return ret;
}

static int turbo_charger_get_qc_device(struct turbo_charger_algo_info *info)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(info->qc_logic_psy)) {
		info->qc_logic_psy = power_supply_get_by_name("qc_phy_z350");
		if (IS_ERR_OR_NULL(info->qc_logic_psy)) {
			info->qc_logic_psy = power_supply_get_by_name("qc_phy_wt6670f");
			if (IS_ERR_OR_NULL(info->qc_logic_psy)) {
				TURBO_CHARGER_DBG("get qc_phy psy failed\n");
				return -ENODEV;
			}
		}
	}

	if (!strncmp(info->qc_logic_psy->desc->name, "qc_phy_z350", 11)) {
		info->qc_phy_z350 = true;
	} else if (!strncmp(info->qc_logic_psy->desc->name, "qc_phy_wt6670f", 14)) {
		info->qc_phy_wt6670f = true;
	}

	TURBO_CHARGER_DBG("get primary_qc_phy psy successfully\n");

	return ret;
}

static int turbo_charger_get_cp_device(struct turbo_charger_algo_info *info)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(info->cp_chg)) {
		info->cp_chg = get_charger_by_name("primary_dvchg");
		if (IS_ERR_OR_NULL(info->cp_chg)) {
			TURBO_CHARGER_ERR("get primary_dvchg failed\n");
			return -EINVAL;
		}
	}

	TURBO_CHARGER_DBG("get primary_dvchg successfully\n");

	return ret;
}

static int turbo_charger_get_input_voltage_settled(struct turbo_charger_algo_info *info, u32 *vbus_volt)
{
	int ret = 0;
	union power_supply_propval val = {0,};

	ret = power_supply_get_property(info->cp_psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (ret) {
		TURBO_CHARGER_ERR("failed to get vbus voltage from CP\n");
	} else {
		*vbus_volt = val.intval;
		TURBO_CHARGER_DBG("get vbus voltage from CP is %d uV\n", *vbus_volt);
	}

	return ret;
}


static int turbo_charger_get_turbo_result_history(struct turbo_charger_algo_info *info)
{
	return info->turbo_result_history_history[info->turbo_result_history_idx];
}

static void turbo_charger_clear_turbo_result_history(struct turbo_charger_algo_info *info)
{
	u32 idx;

	for (idx = 0; idx < TURBO_RET_HISTORY_SIZE; idx++) {
		info->turbo_result_history_history[idx] = 0;
	}

	info->turbo_charger_result = 0;
}

static int turbo_charger_get_ibus_current(struct turbo_charger_algo_info *info, u32 *ibus_pump)
{
	int ret = 0;
	union power_supply_propval val = {0,};

	ret = power_supply_get_property(info->cp_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (ret) {
		TURBO_CHARGER_ERR("failed(%d)\n", ret);
	} else {
		*ibus_pump = val.intval;
		TURBO_CHARGER_DBG("ibus:%d uA\n", *ibus_pump);
	}

	return ret;
}

static int turbo_charger_update_cp_status(struct turbo_charger_algo_info *info)
{
	int ret = 0;
	union power_supply_propval val = {0,};
	bool present, adc_enabled = false;
	int adc_val = 0;

	/*get adc status from CP*/
	ret = charger_dev_is_adc_enabled(info->cp_chg, &adc_enabled);
	if (ret) {
		TURBO_CHARGER_ERR("get adc enabled status failed(%d)\n", ret);
	} else {
		info->cp.adc_enabled = adc_enabled;
		if (info->cp.adc_enabled == false) {
			TURBO_CHARGER_ERR("enable adc first\n");
			charger_dev_enable_adc(info->cp_chg, true);
			info->cp.adc_enabled = true;
			mdelay(40);
		}
	}

	/*get Vbat from CP*/
	ret = power_supply_get_property(info->cp_psy,
					POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get cp vbat_volt failed(%d)\n", ret);
	} else {
		info->cp.vbat_volt = val.intval;
	}

	/*get Ibat from CP*/
	ret = power_supply_get_property(info->cp_psy,
					POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get cp ibat_curr failed(%d)\n", ret);
	} else {
		info->cp.ibat_curr = val.intval;
	}

	/*get Vbus from CP*/
	ret = power_supply_get_property(info->cp_psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get cp vbus_volt failed(%d)\n", ret);
	} else {
		info->cp.vbus_volt = val.intval;
	}

	/*get Ibus from CP*/
	ret = power_supply_get_property(info->cp_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get cp ibus_curr failed(%d)\n", ret);
	} else {
		info->cp.ibus_curr = val.intval;
	}

	/*get TDIE from CP*/
	ret = power_supply_get_property(info->cp_psy,
					POWER_SUPPLY_PROP_TEMP, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get cp die_temp failed(%d)\n", ret);
	} else {
		info->cp.die_temp = val.intval;
	}

	/*get charge_enabled from CP*/
	ret = power_supply_get_property(info->cp_psy,
					POWER_SUPPLY_PROP_ONLINE, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get cp charge_enabled failed(%d)\n", ret);
	} else {
		info->cp.charge_enabled = val.intval;
	}

	/*get vout from CP*/
	ret = charger_dev_get_adc(info->cp_chg, ADC_CHANNEL_VOUT, &adc_val, &adc_val);
	if (ret < 0) {
		TURBO_CHARGER_ERR("get cp vout failed(%d)\n", ret);
	} else {
		info->cp.vout_volt = adc_val;
	}

	/*get vbus err status from CP*/
	ret = charger_dev_is_vbuslowerr(info->cp_chg, &info->cp.vbus_error_low);
	if (ret) {
		TURBO_CHARGER_ERR("get cp vbus low status failed(%d)\n", ret);
	}

	ret = charger_dev_is_vbushigher(info->cp_chg, &info->cp.vbus_error_high);
	if (ret) {
		TURBO_CHARGER_ERR("get cp vbus high status failed(%d)\n", ret);
	}

	/*get Vbat present status from CP*/
	ret = charger_dev_is_vbat_present(info->cp_chg, &present);
	if (ret) {
		TURBO_CHARGER_ERR("get vbat present failed(%d)\n",ret);
	} else {
		info->cp.batt_pres = !(present);
	}

	/*get Vbus present status from CP*/
	ret = charger_dev_is_vbus_present(info->cp_chg, &present);
	if (ret) {
		TURBO_CHARGER_ERR("get vbus present failed(%d)\n", ret);
	} else {
		info->cp.vbus_pres = !(present);
	}

	TURBO_CHARGER_DBG("vbat:%d uV, vout:%d uV, ibat:%d uA, vbus:%d uV, ibus:%d uA, die_temp:%d, cp_enabled:%d, adc_enabled:%d\n",
				info->cp.vbat_volt, info->cp.vout_volt, info->cp.ibat_curr, info->cp.vbus_volt,
				info->cp.ibus_curr, info->cp.die_temp, info->cp.charge_enabled, info->cp.adc_enabled);

	return ret;
}

static int turbo_charger_enable_cp(struct turbo_charger_algo_info *info, bool enable)
{
	int ret;

	TURBO_CHARGER_DBG("enter\n");

	ret = charger_dev_enable(info->cp_chg, enable);
	if (ret) {
		TURBO_CHARGER_ERR("failed to %s cp chg\n",
					enable ? "enable" : "disable");
		return -EINVAL;
	}

	return ret;
}

static int turbo_charger_check_cp_enabled(struct turbo_charger_algo_info *info)
{
	int ret;
	bool enable = false;

	ret = charger_dev_is_enabled(info->cp_chg, &enable);
	if (ret) {
		TURBO_CHARGER_ERR("failed to get cp chg status\n");
		return -EINVAL;
	} else {
		info->cp.charge_enabled = enable;
		TURBO_CHARGER_DBG("get cp chg status is %s\n",
					info->cp.charge_enabled ? "enabled" : "disabled");
	}

	return ret;
}

static int turbo_charger_update_batt_status(struct turbo_charger_algo_info *info)
{
	int ret = 0;
	union power_supply_propval val = {0,};

	/*get SOC from FG*/
	ret = power_supply_get_property(info->batt_psy,
					POWER_SUPPLY_PROP_CAPACITY, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get soc failed(%d)\n", ret);
	} else {
		info->batt.soc = val.intval;
	}

	/*get batt temp from FG*/
	ret = power_supply_get_property(info->batt_psy,
					POWER_SUPPLY_PROP_TEMP, &val);
	if (ret) {
		TURBO_CHARGER_ERR("get batt temp failed(%d)\n", ret);
	} else {
		info->batt.temp = val.intval / 10;
	}

	TURBO_CHARGER_DBG("soc:%d, batt temp:%d C\n", info->batt.soc, info->batt.temp);

	return ret;
}

static void turbo_charger_move_state(struct turbo_charger_algo_info *info,
					enum turbo_charger_state_t state)
{
	TURBO_CHARGER_DBG("state change : %s -> %s\n",
				turbo_charger_state_str[info->state],
				turbo_charger_state_str[state]);

	info->state = state;
}

static int turbo_charger_enable_sw(struct turbo_charger_algo_info *info, bool en)
{
	int ret;

	TURBO_CHARGER_DBG("en:%d\n", en);

	ret = charger_dev_enable(info->sw_chg, en);
	if (ret < 0) {
		TURBO_CHARGER_ERR("failed(%d)\n", ret);
	} else {
		TURBO_CHARGER_DBG("successfully\n");
	}

	//info->sw.charge_enabled = en;
	return ret;
}

static int turbo_charger_enable_term_sw(struct turbo_charger_algo_info *info, bool en)
{
	int ret = 0;

	TURBO_CHARGER_DBG("en:%d\n", en);

	ret = charger_dev_enable_termination(info->sw_chg, en);
	if (ret < 0) {
		TURBO_CHARGER_ERR("failed(%d)\n", ret);
	} else {
		TURBO_CHARGER_DBG("successfully\n");
	}

	return ret;
}

static int turbo_charger_set_curr_limit_sw(struct turbo_charger_algo_info *info, int limit)
{
	int ret;

	TURBO_CHARGER_DBG("limit input current:%d uA\n", limit);

	ret = charger_dev_set_input_current(info->sw_chg, limit);
	if (ret < 0) {
		TURBO_CHARGER_ERR("failed(%d)\n", ret);
	} else {
		TURBO_CHARGER_DBG("successfully\n");
	}

	return ret;
}

static int turbo_charger_set_chg_curr_limit_sw(struct turbo_charger_algo_info *info, int limit)
{
	int ret;

	TURBO_CHARGER_DBG("limit charging current:%d uA\n", limit);

	ret = charger_dev_set_charging_current(info->sw_chg, limit);
	if (ret < 0) {
		TURBO_CHARGER_ERR("failed(%d)\n", ret);
	} else {
		TURBO_CHARGER_DBG("successfully\n");
	}

	return ret;
}

static bool turbo_charger_find_temp_zone(struct turbo_charger_algo_info *info, int temp_c)
{
	int i;
	u32 num_zones;
	int max_temp;
	u32 prev_zone;
	int hotter_t, colder_t;
	int hotter_fcc, colder_fcc;
	struct turbo_charger_step_zone *zones;

	zones = info->temp_zones;
	num_zones = info->turbo_charger_temp_zones_num;
	prev_zone = info->pres_temp_zone;

	TURBO_CHARGER_DBG("prev zone %d, temp_c %d\n", prev_zone, temp_c);

	max_temp = zones[num_zones - 1].temp_c;

	if (prev_zone == ZONE_NONE) {
		for (i = num_zones - 1; i >= 0; i--) {
			if (temp_c >= zones[i].temp_c) {
				if (i == num_zones - 1)
					info->pres_temp_zone = ZONE_HOT;
				else
					info->pres_temp_zone = i + 1;
				return true;
			}
		}
		info->pres_temp_zone = ZONE_COLD;
		return true;
	}

	if (prev_zone == ZONE_COLD) {
		if (temp_c  >= MIN_TEMP_C + HYSTERESIS_DEGC)
			info->pres_temp_zone = ZONE_FIRST;
	} else if (prev_zone == ZONE_HOT) {
		if (temp_c <= max_temp - HYSTERESIS_DEGC)
			info->pres_temp_zone = num_zones - 1;
	} else {
		if (prev_zone == ZONE_FIRST) {
			hotter_t = zones[prev_zone].temp_c;
			colder_t = MIN_TEMP_C;
			hotter_fcc = zones[prev_zone + 1].chrg_step_power->chrg_step_curr;
			colder_fcc = 0;
		} else if (prev_zone == num_zones - 1) {
			hotter_t = zones[prev_zone].temp_c;
			colder_t = zones[prev_zone - 1].temp_c;
			hotter_fcc = 0;
			colder_fcc = zones[prev_zone - 1].chrg_step_power->chrg_step_curr;
		} else {
			hotter_t = zones[prev_zone].temp_c;
			colder_t = zones[prev_zone - 1].temp_c;
			hotter_fcc = zones[prev_zone + 1].chrg_step_power->chrg_step_curr;
			colder_fcc = zones[prev_zone - 1].chrg_step_power->chrg_step_curr;
		}

		if (!ignore_hysteresis_degc) {
			if (zones[prev_zone].chrg_step_power->chrg_step_curr < hotter_fcc)
				hotter_t += HYSTERESIS_DEGC;
			if (zones[prev_zone].chrg_step_power->chrg_step_curr < colder_fcc)
				colder_t -= HYSTERESIS_DEGC;
		}

		if (temp_c <= MIN_TEMP_C)
			info->pres_temp_zone = ZONE_COLD;
		else if (temp_c >= max_temp)
			info->pres_temp_zone = ZONE_HOT;
		else if (temp_c >= hotter_t)
			info->pres_temp_zone++;
		else if (temp_c < colder_t)
			info->pres_temp_zone--;
	}

	TURBO_CHARGER_DBG("batt temp_c %d, prev zone %d, pres zone %d, "
				"hotter_fcc %d mA, colder_fcc %d mA, "
				"hotter_t %d C, colder_t %d C\n",
				temp_c, prev_zone, info->pres_temp_zone,
				hotter_fcc, colder_fcc, hotter_t, colder_t);
	if (prev_zone != info->pres_temp_zone) {
		TURBO_CHARGER_DBG("Entered Temp Zone %d!\n", info->pres_temp_zone);
		return true;
	}

	return false;
}

static bool turbo_charger_find_chrg_step(struct turbo_charger_algo_info *info,
				int temp_zone, int vbatt_volt)
{
	int batt_volt, i;
	bool find_step = false;

	struct turbo_charger_step_zone *zone;
	struct turbo_charger_step_power *chrg_steps;
	struct turbo_charger_step_info chrg_step_inline;
	struct turbo_charger_step_info prev_step;

	if (IS_ERR_OR_NULL(info)) {
		TURBO_CHARGER_ERR("called before info valid!\n");
		return false;
	}

	if (info->pres_temp_zone == ZONE_HOT ||
		info->pres_temp_zone == ZONE_COLD ||
		info->pres_temp_zone < ZONE_FIRST) {
			TURBO_CHARGER_ERR("pres temp zone is HOT or COLD, can't find chrg step\n");
			return false;
	}

	zone = &info->temp_zones[info->pres_temp_zone];
	chrg_steps = zone->chrg_step_power;
	prev_step = info->chrg_step;

	batt_volt = vbatt_volt;
	chrg_step_inline.temp_c = zone->temp_c;

	for (i = 0; i < info->turbo_charger_step_nums; i++) {
		if (chrg_steps[i].chrg_step_volt > 0 && batt_volt < chrg_steps[i].chrg_step_volt) {
			if ((i + 1) < info->turbo_charger_step_nums &&
				chrg_steps[i + 1].chrg_step_volt > 0) {
				chrg_step_inline.chrg_step_cv_tapper_curr = chrg_steps[i + 1].chrg_step_curr;
			} else {
				chrg_step_inline.chrg_step_cv_tapper_curr = chrg_steps[i].chrg_step_curr;
			}
			chrg_step_inline.chrg_step_cc_curr = chrg_steps[i].chrg_step_curr;
			chrg_step_inline.chrg_step_cv_volt = chrg_steps[i].chrg_step_volt;
			chrg_step_inline.pres_chrg_step = i;
			find_step = true;
			TURBO_CHARGER_DBG("[%d]: find chrg step\n", __LINE__);
			break;
		}
	}

	if (find_step) {
		TURBO_CHARGER_DBG("chrg step %d, step cc curr %d uA, step cv volt %d uV, "
					"step cv tapper curr %d uA\n",
					chrg_step_inline.pres_chrg_step,
					chrg_step_inline.chrg_step_cc_curr,
					chrg_step_inline.chrg_step_cv_volt,
					chrg_step_inline.chrg_step_cv_tapper_curr);
		info->chrg_step = chrg_step_inline;
	} else {
		for (i = 0; i < info->turbo_charger_step_nums; i++) {
			if (chrg_steps[i].chrg_step_volt > 0 &&
				batt_volt > chrg_steps[i].chrg_step_volt) {
				if ((i + 1) < info->turbo_charger_step_nums &&
					chrg_steps[i + 1].chrg_step_volt > 0) {
					chrg_step_inline.chrg_step_cv_tapper_curr = chrg_steps[i + 1].chrg_step_curr;
				} else {
					chrg_step_inline.chrg_step_cv_tapper_curr = chrg_steps[i].chrg_step_curr;
				}
				chrg_step_inline.chrg_step_cc_curr = chrg_steps[i].chrg_step_curr;
				chrg_step_inline.chrg_step_cv_volt = chrg_steps[i].chrg_step_volt;
				chrg_step_inline.pres_chrg_step = i;
				find_step = true;
				TURBO_CHARGER_DBG("[%d]: find chrg step\n", __LINE__);
			}
		}

		if (find_step) {
			TURBO_CHARGER_DBG("chrg step %d, "
							"step cc curr %d uA, step cv volt %d uV, "
							"step cv tapper curr %d uA\n",
							chrg_step_inline.pres_chrg_step,
							chrg_step_inline.chrg_step_cc_curr,
                            chrg_step_inline.chrg_step_cv_volt,
							chrg_step_inline.chrg_step_cv_tapper_curr);
			info->chrg_step = chrg_step_inline;
		}
	}

	if (find_step) {
		if (info->chrg_step.chrg_step_cc_curr == info->chrg_step.chrg_step_cv_tapper_curr)
			info->chrg_step.last_step = true;
		else
			info->chrg_step.last_step = false;

		TURBO_CHARGER_DBG("Temp zone:%d, "
					"select chrg step %d, step cc curr %d uA, "
					"step cv volt %d uV, step cv tapper curr %d uA, "
					"is the last chrg step %d\n",
					info->pres_temp_zone,
					info->chrg_step.pres_chrg_step,
					info->chrg_step.chrg_step_cc_curr,
					info->chrg_step.chrg_step_cv_volt,
					info->chrg_step.chrg_step_cv_tapper_curr,
					info->chrg_step.last_step);

		if (prev_step.pres_chrg_step != info->chrg_step.pres_chrg_step) {
			TURBO_CHARGER_DBG("Find the next chrg step\n");
			return true;
		}
	}

	return false;
}

static int turbo_charger_set_volt_count(struct turbo_charger_algo_info *info, int count)
{
	int ret = 0;

	if (info->qc_phy_z350) {
		ret = z350_set_volt_count(count);
	} else if (info->qc_phy_wt6670f) {
		ret = wt6670f_set_volt_count(count);
	}

	return ret;
}

static void turbo_charger_select_pdo(struct turbo_charger_algo_info *info,
					int target_uv, int target_ua)
{
	int ret, vbus_val;
	int ibatt_curr = 0, vbatt_volt = 0, vbus_volt = 0, ibus_curr = 0, ibus_pump = 0;
	int req_volt_inc_step = 0, req_volt_dec_step = 0;
	int req_curr_inc_step = 0, req_curr_dec_step = 0;
	int real_inc_step = 0,	   real_dec_step = 0;
	int calculated_vbus;
	int retry_count;
	int target_vbus_volt = 0;
	int count = 0;
	int vbus_volt_new;

	TURBO_CHARGER_DBG("target_volt:%d uV, target_curr:%d uA\n", target_uv, target_ua);

	turbo_charger_update_cp_status(info);
	vbatt_volt = info->cp.vbat_volt;
	ibatt_curr = info->cp.ibat_curr;
	vbus_volt = info->cp.vbus_volt;
	ibus_pump = info->cp.ibus_curr;
	if (ibus_pump < 0)
		ibus_pump = 0;

	if (target_ua > info->turbo_charger_request_curr_prev) {
		req_curr_inc_step = (target_ua - info->turbo_charger_request_curr_prev) / 2;
		if (req_curr_inc_step < 0)
			req_curr_inc_step = 0;
	} else {
		req_curr_dec_step = (info->turbo_charger_request_curr_prev - target_ua) / 2;
		if (req_curr_dec_step < 0)
			req_curr_dec_step = 0;
	}
	TURBO_CHARGER_DBG("target_curr:%d uA, req_curr_inc_step:%d uA, req_curr_dec_step:%d uA\n",
					target_ua, req_curr_inc_step, req_curr_dec_step);

	if (target_uv > (info->turbo_charger_request_volt_prev)) {
		req_volt_inc_step = target_uv - info->turbo_charger_request_volt_prev;
		if (req_volt_inc_step < 0)
			req_volt_inc_step = 0;
	} else {
		req_volt_dec_step = info->turbo_charger_request_volt_prev - target_uv;
		if (req_volt_dec_step < 0)
			req_volt_dec_step = 0;
	}
	TURBO_CHARGER_DBG("target_volt:%d uV, req_volt_inc_step:%d uV, req_volt_dec_step:%d uV\n",
					target_uv, req_volt_inc_step, req_volt_dec_step);

	if (target_ua > info->turbo_charger_curr_max || target_uv > info->turbo_charger_volt_max) {
		TURBO_CHARGER_DBG("target vbus or ibus out of range[%d uV: %d uA], skip inc volt\n",
					info->turbo_charger_volt_max, info->turbo_charger_curr_max);
		req_volt_inc_step = 0;
		req_curr_inc_step = 0;
	}

	TURBO_CHARGER_DBG("prev_caltulated, current inc:%d, dec:%d, voltage inc:%d, dec:%d\n",
					req_curr_inc_step, req_curr_dec_step, req_volt_inc_step, req_volt_dec_step);

	real_inc_step = max(req_volt_inc_step, req_curr_inc_step);
	real_dec_step = max(req_volt_dec_step, req_curr_dec_step);
	TURBO_CHARGER_DBG("real_inc_step:%d, real_dec_step:%d\n",
					real_inc_step, real_dec_step);

	ibus_curr = ibus_pump + info->sw_charging_curr_limited;
	calculated_vbus =  vbus_volt + (ibus_curr * 100) / 1000;

	if (real_dec_step > 0) {  // decrease current vbus
		target_vbus_volt = vbus_volt - real_dec_step;
		if (target_vbus_volt < 4500000) {
			count = 0;
		} else {
			count -= real_dec_step / 20000; //every step for QC3+ is 20 mV
			if (real_dec_step % 20000 > 0) {
				count--;
			}
		}
		TURBO_CHARGER_DBG("vbus will decreased to volt:%d uV, vbus_now:%d uV, count:%d\n",
						target_vbus_volt, vbus_volt, count);
	} else if ((real_inc_step > 0 &&  // increase current vbus
				(calculated_vbus <= info->turbo_charger_volt_max) &&
				ibus_curr <= info->turbo_charger_curr_max)) {
		target_vbus_volt = vbus_volt + real_inc_step;
		/*TN Begin modify vbus ovp by rongxing.li/860682 20231128 CR/EKFOGO4G-8390*/
		if (target_vbus_volt >= info->turbo_charger_volt_max) {
			TURBO_CHARGER_DBG("target vbus volt out of range[%d uV], skip inc volt\n",
						info->turbo_charger_volt_max);
			count = 0;
		} else {
		/*TN End modify vbus ovp by rongxing.li/860682 20231128 CR/EKFOGO4G-8390*/
			count += real_inc_step / 20000; //every step for QC3+ is 20 mV
			if (real_dec_step % 20000 > 0) {
				count++;
			}
		}
		TURBO_CHARGER_DBG("vbus will increased to volt:%d uV, vbus_now:%d uV, count:%d\n",
						target_vbus_volt, vbus_volt, count);
	}

	for (retry_count = 0; retry_count < 3; retry_count++) {
		/*TN Begin modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
		ret = turbo_charger_set_volt_count(info, count);
		if (count > 0)
			info->total_count += count;
		else if (count < 0)
			info->total_count -= abs(count);
		turbo_charger_get_input_voltage_settled(info, &vbus_val);
		if ((ret != 0) || (info->total_count > MAX_INC_PULSE && vbus_val < CP_BUS_UVP_THRESHOLD)) {
		/*TN End modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
			TURBO_CHARGER_ERR("set vol count to qc logic failed, switch to main charger!\n");
			turbo_charger_move_state(info, TURBO_STATE_STOP_CHARGE);
		} else {
			udelay(10000 * abs(count));

			ret = turbo_charger_get_input_voltage_settled(info, &vbus_val);
			if (!ret) {
				vbus_volt_new = vbus_val;
				TURBO_CHARGER_DBG("vbus_volt_new :%d uV, vbus_volt_old: %d uV, total_count:%d\n", vbus_volt_new, vbus_volt, info->total_count);
				if (vbus_volt_new > 0 &&
						((count > 0 && (vbus_volt_new > vbus_volt)) ||
						(count < 0 && (vbus_volt_new < vbus_volt)))) {
					TURBO_CHARGER_DBG("%s vbus voltage successfully\n",
									count > 0 ? "increase" : "decrease");
					break;
				}
#if 0
				else if(vbus_volt_new > 0) {
					pr_err("%s, vbus modify failed, retry!\n", __func__);
					count =(count > 0)? ( count + (vbus_volt - vbus_volt_new) / 20):
							(count - (vbus_volt_new - vbus_volt) / 20);
					vbus_volt = vbus_volt_new;
				}
#endif
				else {
					TURBO_CHARGER_DBG("keep current vbus voltage on %d uV\n", vbus_volt_new);
					break;
				}
			} else {
				TURBO_CHARGER_ERR("vbus read failed, exit\n");
				break;
			}
		}
	}

	TURBO_CHARGER_DBG("pdo select over\n");

	ret = turbo_charger_get_ibus_current(info, &ibus_pump);
	if (!ret) {
		info->turbo_charger_request_volt_prev = vbus_volt_new;
		info->turbo_charger_request_volt = vbus_volt_new;
		info->turbo_charger_request_curr_prev = ibus_pump;
		info->turbo_charger_request_curr = ibus_pump;
	}

	TURBO_CHARGER_DBG("after adjust vbus volt "
				"turbo charger request_volt:%d uV, target_vbus:%d uV, "
				"request_curr:%d uA, target_cur:%d uA\n",
				vbus_volt_new, info->turbo_charger_request_volt,
				ibus_pump, info->turbo_charger_request_curr);
}

static int turbo_charger_sm_work_func(struct turbo_charger_algo_info *info)
{
	int ret = 0;
	int volt_change;
	///bool qc_pps_balance;
	int chrg_cv_delta_volt = 0;
	static int chrg_cv_taper_tunning_cnt = 0;
	static int chrg_cc_power_tuning_cnt = 0;
	struct turbo_charger_step_info chrg_step;
	int state = info->state;
	int vbus_volt = info->cp.vbus_volt;
	int vbatt_volt = info->cp.vbat_volt;
	//int vout_volt = info->cp.vout_volt;
	int ibus_curr = info->cp.ibus_curr;
	int ibatt_curr = info->cp.ibat_curr;
	int batt_soc = info->batt.soc;

	if (info->pres_temp_zone == ZONE_COLD ||
			info->pres_temp_zone == ZONE_HOT ||
			(vbatt_volt > info->batt_ovp_limit && !info->sw.charge_enabled)) {
		TURBO_CHARGER_DBG("invalid condition, move to stop charge\n");
		turbo_charger_move_state(info, TURBO_STATE_STOP_CHARGE);
	}

	chrg_step = info->chrg_step;

	switch (info->state) {
	case TURBO_STATE_DISCONNECT:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		is_turbo_charger_ready = false;
		info->turbo_charger_request_volt = 0;
		TURBO_CHARGER_DBG("vbatt_volt:%d uV is ok, start turbo charging\n", vbatt_volt);
		turbo_charger_move_state(info, TURBO_STATE_CHRG_PUMP_ENTRY);
		heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
		info->sys_therm_cooling = false;
		break;

	case TURBO_STATE_ENTRY:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		if (info->turbo_charger_support && batt_soc < CP_CHRG_SOC_LIMIT) {
			turbo_charger_find_chrg_step(info, info->pres_temp_zone, vbatt_volt);
			//charger_dev_enable_termination(info->sw_chg, true);
			charger_dev_enable_vbus_ovp(info->sw_chg, false);
			/*TN Begin modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
			if (!info->cp.charge_enabled && chrg_step.chrg_step_cc_curr > CURRENT_2000_MA &&
				(g_thermal_charging_current_limit > CURRENT_2000_MA || g_thermal_charging_current_limit == THERMAL_NOT_LIMIT)) {
			/*TN End modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
				TURBO_CHARGER_INFO("Enter into CHRG PUMP, chrg step cc curr %d uA\n", chrg_step.chrg_step_cc_curr);
				turbo_charger_enable_cp(info, true);
				msleep(50);
				turbo_charger_check_cp_enabled(info);
				turbo_charger_move_state(info, TURBO_STATE_CHRG_PUMP_ENTRY);
			} else {
				TURBO_CHARGER_INFO("Enter into PMIC switch charging, chrg step cc curr %d uA\n", chrg_step.chrg_step_cc_curr);
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
		}
		heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
		break;

	case TURBO_STATE_SW_ENTRY:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		is_turbo_charger_ready = false;
		info->turbo_charger_request_volt = vbus_volt;
		info->turbo_charger_request_curr = ibus_curr;
		//turbo_charger_enable_term_sw(info, true);
		if (!info->sw.charge_enabled)
			turbo_charger_enable_sw(info, true);
		turbo_charger_set_curr_limit_sw(info, info->turbo_charging_curr_min + 200000);
		turbo_charger_set_chg_curr_limit_sw(info, info->turbo_charging_curr_min);
		turbo_charger_move_state(info, TURBO_STATE_SW_LOOP);
		heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
		break;

	case TURBO_STATE_CHRG_PUMP_ENTRY:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		is_turbo_charger_ready = true;
		/*TN Begin modified by lingfei.tang/77407 20231201 CR/EKFOGO4G-5993*/
		ffc_reduce_count = 0;
		/*TN End modified by lingfei.tang/77407 20231201 CR/EKFOGO4G-5993*/
		turbo_charger_find_chrg_step(info, info->pres_temp_zone, vbatt_volt);
		if (turbo_config.fc2_disable_sw) {
			turbo_charger_enable_sw(info, false);
			turbo_charger_set_curr_limit_sw(info, info->sw_charging_curr_limited);
			charger_dev_dump_registers(info->sw_chg);
			if (!info->sw.charge_enabled)
				turbo_charger_move_state(info, TURBO_STATE_SINGLE_CP_ENTRY);
		} else {
			/*TN Begin modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
			if (g_thermal_charging_current_limit > CURRENT_2000_MA || g_thermal_charging_current_limit == THERMAL_NOT_LIMIT) {
				turbo_charger_enable_term_sw(info, false);
				turbo_charger_set_curr_limit_sw(info, info->sw_charging_curr_limited);
				turbo_charger_set_chg_curr_limit_sw(info, info->sw_charging_curr_limited);
				turbo_charger_move_state(info, TURBO_STATE_SINGLE_CP_ENTRY);
			} else {
				TURBO_CHARGER_INFO("Enter into PMIC switch charging, thermal limit charging to %d uA!!!\n", g_thermal_charging_current_limit);
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
			/*TN End modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
		}

		if (g_capacity_level_limited_flag) { // demo mode limit charging
			if (!info->sw.charge_enabled && (batt_soc >= DEMO_SOC_LIMIT)) {
				TURBO_CHARGER_DBG("Demo vesion is on, limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
		}

		if (g_battery_protect_enable_flag) { // battery protection mode, limit charging
			if (!info->sw.charge_enabled) {
				TURBO_CHARGER_DBG("battery protct mode is on, limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
		}

		if (!mtk_can_charging) { // mtk charger thread limit charging
			if (!info->sw.charge_enabled) {
				TURBO_CHARGER_INFO("mtk charger thread limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_STOP_CHARGE);
			}
		}

		info->batt_thermal_cooling = false;
		info->sys_therm_cooling = false;
		//charger_set_qc_type(1);
		//charger_set_thread_cv(4496000);
		// need to check this following two callback function exist or not
		//charger_dev_set_constant_voltage(info->sw_chg, 4500000);  //qc battery cv
		//charger_dev_set_eoc_current(info->sw_chg, 600000); //qc cut-off current


		info->turbo_charger_request_volt = (2 * vbatt_volt) % 20000;
		info->turbo_charger_request_volt = (2 * vbatt_volt) - info->turbo_charger_request_volt + info->turbo_charger_volt_comp; //2 * vbatt_volt + 200mv;

		info->turbo_charger_request_curr = min(info->turbo_charger_curr_max, info->turbo_charging_curr_min);

		TURBO_CHARGER_DBG("turbo init, request_volt:%d uV, request_curr:%d uA, volt_comp:%d uV\n",
						info->turbo_charger_request_volt, info->turbo_charger_request_curr, info->turbo_charger_volt_comp);

		info->turbo_charger_request_curr_prev = info->turbo_charger_request_curr;
		info->turbo_charger_request_volt_prev = vbus_volt;  //5V
		//info->turbo_charger_request_volt_prev = info->cp.vbus_volt;  //5V
		info->turbo_charger_vbatt_volt_prev = vbatt_volt;
		info->turbo_charger_ibatt_curr_prev = ibatt_curr;
		info->turbo_charger_therm_loop_cn = 0;
		heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
		break;

	case TURBO_STATE_SINGLE_CP_ENTRY:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		if (!info->cp.charge_enabled) {
			turbo_charger_enable_cp(info, true);
			msleep(100);
			turbo_charger_check_cp_enabled(info);
		}
		turbo_charger_move_state(info, TURBO_STATE_PPS_TUNNING_CURR);
		heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
		break;

	case TURBO_STATE_PPS_TUNNING_CURR:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;
		TURBO_CHARGER_DBG("Temp zone:%d, select chrg step %d, step cc curr %d, "
					"step cv volt %d, step cv tapper curr %d, "
					"is the last chrg step %d\n",
					info->pres_temp_zone,
					info->chrg_step.pres_chrg_step,
					info->chrg_step.chrg_step_cc_curr,
					info->chrg_step.chrg_step_cv_volt,
					info->chrg_step.chrg_step_cv_tapper_curr,
					info->chrg_step.last_step);

		if (!info->cp.charge_enabled) {
			info->turbo_charger_volt_comp = DEFAULT_TURBO_VOLT_COMP;
			turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
		} else if (vbatt_volt > info->chrg_step.chrg_step_cv_volt) {
			if ((info->turbo_charger_request_curr - info->turbo_charger_curr_steps) > info->typec_middle_current ||
					(ibatt_curr - info->turbo_charger_curr_steps) > info->chrg_step.chrg_step_cc_curr) {
				info->turbo_charger_request_curr -= info->turbo_charger_curr_steps;
			}
			turbo_charger_move_state(info, TURBO_STATE_CP_CC_LOOP);
			TURBO_CHARGER_DBG("During the going up curr process, "
						"the chrg step was changed, "
						"stop increase turbo curr and Enter into CC stage as soon\n");
		} else if (info->turbo_charger_result < 0) {
			if (turbo_charger_get_turbo_result_history(info) != 0) {
				turbo_charger_move_state(info, TURBO_STATE_CP_CC_LOOP);
				TURBO_CHARGER_DBG("Too many pdo request failed, Enter into CC stage direct\n");
				turbo_charger_clear_turbo_result_history(info);
			}
			info->turbo_charger_request_curr = info->turbo_charger_request_curr_prev;
			goto schedule;
		} else if ((info->turbo_charger_request_curr + info->turbo_charger_curr_steps) <= info->turbo_charger_curr_max &&
						(vbatt_volt < info->chrg_step.chrg_step_cv_volt) &&
						(ibatt_curr < chrg_step.chrg_step_cc_curr) &&
						(info->system_thermal_level == THERMAL_NOT_LIMIT || (info->system_thermal_level != THERMAL_NOT_LIMIT && ibatt_curr < info->system_thermal_level))) {
			TURBO_CHARGER_DBG("turbo request curr: %d uA, turbo curr max: %d uA,"
							" chrg_step_cc_curr: %d uA, turbo_charger_curr_steps: %d uA\n",
							info->turbo_charger_request_curr, info->turbo_charger_curr_max,
							info->chrg_step.chrg_step_cc_curr, info->turbo_charger_curr_steps);
			volt_change = info->turbo_charger_curr_steps;
			info->turbo_charger_request_curr += min(volt_change, info->chrg_step.chrg_step_cc_curr - ibatt_curr);

			TURBO_CHARGER_DBG("Increase turbo curr:%d uA, volt_change:%d uA\n", info->turbo_charger_request_curr, volt_change);
			volt_change = 0;
		} else {
			if (ibatt_curr > info->chrg_step.chrg_step_cc_curr) {
				volt_change = ((ibatt_curr - info->chrg_step.chrg_step_cc_curr) % 20000 == 0) ?
					(ibatt_curr - info->chrg_step.chrg_step_cc_curr) :
					(ibatt_curr - info->chrg_step.chrg_step_cc_curr + 20000);
				info->turbo_charger_request_curr -= volt_change;
				TURBO_CHARGER_DBG("Decrese turbo curr %d uA, volt_change %d uA\n", info->turbo_charger_request_curr, volt_change);
				volt_change = 0;
				turbo_charger_move_state(info, TURBO_STATE_PPS_TUNNING_VOLT);
			} else if (info->system_thermal_level != -1 && ibatt_curr > info->system_thermal_level) {
				info->turbo_charger_request_curr -= info->turbo_charger_curr_steps;
			}
		}

		if (g_capacity_level_limited_flag) {
			if (!info->sw.charge_enabled && (batt_soc >= DEMO_SOC_LIMIT)) {
				TURBO_CHARGER_DBG("Demo vesion is on, limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
		}

		if (g_battery_protect_enable_flag) {
			if (!info->sw.charge_enabled) {
				TURBO_CHARGER_DBG("battery protct mode is on, limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
		}

		if (!mtk_can_charging) { // mtk charger thread limit charging
			if (!info->sw.charge_enabled) {
				TURBO_CHARGER_INFO("mtk charger thread limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_STOP_CHARGE);
			}
		}

		heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
		break;

	case TURBO_STATE_PPS_TUNNING_VOLT:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;

		if (!info->cp.charge_enabled) {
			TURBO_CHARGER_DBG("CP MASTER was disabled, "
							"Enter into SW directly\n");
			info->turbo_charger_volt_comp = DEFAULT_TURBO_VOLT_STEPS;
			turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
		} else if (vbatt_volt > info->chrg_step.chrg_step_cv_volt) {
			info->turbo_charger_request_volt -= info->turbo_charger_volt_steps;
			turbo_charger_move_state(info, TURBO_STATE_CP_CC_LOOP);
			TURBO_CHARGER_DBG("Duing the volt going up process, "
						"the chrg step was changed, "
						"stop increase turbo volt and "
						"Enter into CC stage as soon!\n");
		} else if (info->turbo_charger_result < 0) {
			if (turbo_charger_get_turbo_result_history(info) != 0) {
				turbo_charger_move_state(info, TURBO_STATE_CP_CC_LOOP);
				turbo_charger_clear_turbo_result_history(info);
				TURBO_CHARGER_DBG("Too many pdo request failed, "
						 "Enter into CC stage directly!\n");
			}
			info->turbo_charger_request_volt = info->turbo_charger_request_volt_prev;
			goto schedule;
		} else if ((info->turbo_charger_request_volt + info->turbo_charger_volt_steps) <= info->turbo_charger_volt_max
					&& vbatt_volt < info->chrg_step.chrg_step_cv_volt
					&& ibatt_curr < ((info->chrg_step.pres_chrg_step == STEP_FIRST) ?
						info->chrg_step.chrg_step_cc_curr + info->step_first_current_comp : info->chrg_step.chrg_step_cc_curr)) {
					TURBO_CHARGER_DBG("turbo_charger_request_volt: %d uV, turbo_charger_volt_max: %d uV, chrg_step_cv_volt: %d uV, "
							"turbo_charger_volt_steps: %d uV\n",
								info->turbo_charger_request_volt, info->turbo_charger_volt_max,
								info->chrg_step.chrg_step_cv_volt, info->turbo_charger_volt_steps);
					if (info->turbo_charger_request_volt + 3 * info->turbo_charger_volt_steps <= info->turbo_charger_volt_max) {
						volt_change = 3 * info->turbo_charger_volt_steps;
					} else {
						volt_change = info->turbo_charger_volt_steps;
					}

					ibus_curr = (info->chrg_step.pres_chrg_step == STEP_FIRST) ?
									info->chrg_step.chrg_step_cc_curr + info->step_first_current_comp : info->chrg_step.chrg_step_cc_curr;

					info->turbo_charger_request_volt += min(volt_change, ibus_curr - ibatt_curr);
					TURBO_CHARGER_DBG("Increase turbo volt %d uV, volt_change: %d, ibus_curr: %d \n",
								info->turbo_charger_request_volt, volt_change, ibus_curr);
		} else {
			if (ibatt_curr > info->chrg_step.chrg_step_cc_curr) {
				volt_change = ((ibatt_curr - info->chrg_step.chrg_step_cc_curr) % 20000 == 0) ?
										(ibatt_curr - info->chrg_step.chrg_step_cc_curr) :
										(ibatt_curr - info->chrg_step.chrg_step_cc_curr + 20000);

				volt_change = min(volt_change, info->turbo_charger_curr_steps);
				info->turbo_charger_request_curr -= volt_change;

				TURBO_CHARGER_DBG("Decrease turbo curr %d, volt_change: %d\n", info->turbo_charger_request_curr, volt_change);
				volt_change = 0;
			}

			TURBO_CHARGER_DBG("Enter into CC loop stage !\n");
			turbo_charger_move_state(info, TURBO_STATE_CP_CC_LOOP);
		}
		break;

	case TURBO_STATE_CP_CC_LOOP:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		TURBO_CHARGER_DBG("Temp zone:%d, vbatt_volt %d, select chrg step %d, step cc curr %d, "
					"step cv volt %d, step cv tapper curr %d, "
					"is the last chrg step %d\n",
					info->pres_temp_zone, vbatt_volt,
					info->chrg_step.pres_chrg_step,
					info->chrg_step.chrg_step_cc_curr,
					info->chrg_step.chrg_step_cv_volt,
					info->chrg_step.chrg_step_cv_tapper_curr,
					info->chrg_step.last_step);

		if (info->cp_psy && (!info->cp.charge_enabled)) {
			info->turbo_charger_volt_comp = DEFAULT_TURBO_VOLT_COMP;
			turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			info->turbo_charger_cc_loop_stage = false;
			heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;
			goto schedule;
		}
#if 0
		if (info->pps_result < 0) {
			pr_err("%s: Last select pdo failed\n", __func__);
			info->pps_result = usbqc_get_pps_result_history(info);
			switch(info->pps_result) {
				case BLANCE_POWER:
					qc_pps_balance = true;
					usbqc_clear_pps_result_history(info);
					break;
				case RESET_POWER:
					turbo_charger_move_state(info, PM_STATE_ENTRY);
					usbqc_clear_pps_result_history(info);
					break;
				default:
					break;
			}
			info->qc_request_volt = info->qc_request_volt_prev;
			goto schedule;
		}
#endif
#if 0
		if(((!info->sys_therm_cooling) || (info->systerm_thermal_level >= chrg_step.chrg_step_cc_curr))
			&& !info->batt_therm_cooling) {
			if(info->qc_request_curr - info->pps_curr_steps > info->typec_middle_current) {
				info->qc_request_curr -= info->pps_curr_steps;
				info->qc_request_volt += 0;// chan need mmi_calculate_delta_volt(info->qc_request_volt_prev, info->qc_request_curr, info->pps_curr_steps);
			} else {
				if(info->qc_request_curr + info->pps_curr_steps < info->qc_curr_max) {
					info->qc_request_curr += info->pps_curr_steps;
				} else if(info->qc_request_volt + info->pps_volt_steps < info->qc_volt_max) {
					info->qc_request_volt += info->pps_volt_steps;
				}
			}
			chrg_cc_power_tuning_cnt = 0;
		} else if(ibatt_curr < chrg_step.chrg_step_cc_curr && info->qc_request_volt < info->qc_volt_max) {
			chrg_cc_power_tuning_cnt++;
		} else if(ibatt_curr > chrg_step.chrg_step_cc_curr + CC_CURR_DEBOUNCE) {
			info->qc_request_volt -= info->pps_volt_steps;
		} else {
			chrg_cc_power_tuning_cnt = 0;
			if(ibatt_curr < chrg_step.chrg_step_cc_curr) {
				heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
			}
		}
#endif
		if (!info->turbo_charger_cc_loop_stage) {
			info->turbo_charger_cc_loop_stage = true;
			info->turbo_charger_vbatt_volt_prev = vbatt_volt;
		}

		if (ibatt_curr < info->chrg_step.chrg_step_cc_curr) {
			info->turbo_charger_request_curr += info->turbo_charger_curr_steps;
			TURBO_CHARGER_DBG("CC_LOOP_STEP_1\n");
		} else if (ibatt_curr > info->chrg_step.chrg_step_cc_curr + CC_CURR_DEBOUNCE) {
			if (vbatt_volt > info->turbo_charger_vbatt_volt_prev + 1000) {
				info->turbo_charger_vbatt_volt_prev = vbatt_volt;
				info->turbo_charger_request_curr -= info->turbo_charger_curr_steps;
			} else {
				TURBO_CHARGER_DBG("CC loop work well, continue\n");
			}
			TURBO_CHARGER_DBG("CC_LOOP_STEP_2\n");
		} else {
			chrg_cc_power_tuning_cnt = 0;
			TURBO_CHARGER_DBG("CC_LOOP_STEP_3\n");
		}
		TURBO_CHARGER_DBG("ibatt_curr:%d uA, cc curr:%d uA, taper_tunning_count:%d\n",
					ibatt_curr, info->chrg_step.chrg_step_cc_curr, chrg_cc_power_tuning_cnt);

		TURBO_CHARGER_DBG("vbatt_volt:%d uV, cc volt:%d uV, taper_tunning_count:%d\n",
					vbatt_volt, info->chrg_step.chrg_step_cv_volt, chrg_cv_taper_tunning_cnt);

		if (vbatt_volt >= info->chrg_step.chrg_step_cv_volt) {
			if (chrg_cv_taper_tunning_cnt > CV_TAPPER_COUNT) {
				turbo_charger_move_state(info, TURBO_STATE_CP_CV_LOOP);
				info->turbo_charger_cc_loop_stage = false;
				chrg_cv_taper_tunning_cnt = 0;
				chrg_cv_delta_volt = CV_DELTA_VOLT;
			} else {
				chrg_cv_taper_tunning_cnt++;
			}
		} else {
			chrg_cv_taper_tunning_cnt = 0;
		}

		if (g_capacity_level_limited_flag) {
			if (!info->sw.charge_enabled && (batt_soc >= DEMO_SOC_LIMIT)) {
				TURBO_CHARGER_DBG("Demo vesion is on, limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
		}

		if (g_battery_protect_enable_flag) {
			if (!info->sw.charge_enabled) {
				TURBO_CHARGER_DBG("battery protct mode is on, limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
		}

		if (!mtk_can_charging) { // mtk charger thread limit charging
			if (!info->sw.charge_enabled) {
				TURBO_CHARGER_INFO("mtk charger thread limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_STOP_CHARGE);
			}
		}

		heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;
		break;

	case TURBO_STATE_CP_CV_LOOP:
		heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		if (info->cp_psy && (!info->cp.charge_enabled)) {
				info->turbo_charger_volt_comp = DEFAULT_TURBO_VOLT_COMP;
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
				heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;
				goto schedule;
		}
#if 0
		if (info->pps_result < 0) {
			pr_err("%s: Last select pdo failed\n", __func__);
			info->pps_result = usbqc_get_pps_result_history(info);
			switch (info->pps_result) {
			case BLANCE_POWER:
					info->qc_request_curr -=
					info->pps_curr_steps;
					usbqc_clear_pps_result_history(info);
				break;
			case RESET_POWER:
				turbo_charger_move_state(info, PM_STATE_ENTRY);
				usbqc_clear_pps_result_history(info);
				break;
			default:
				break;
			}
			info->qc_request_volt = info->qc_request_volt_prev;
			goto schedule;
		}
#endif
#if 0
		if (vbatt_volt >= chrg_step.chrg_step_cv_volt
			&& (!chrg_step.last_step &&
			ibatt_curr < chrg_step.chrg_step_cv_tapper_curr)) {
			//|| ibatt_curr < info->cp.charging_current) {
			if (chrg_cv_taper_tunning_cnt >= CV_TAPPER_COUNT) {
				if (ibatt_curr < info->turbo_charging_curr_min) {
					turbo_charger_find_chrg_step(info,
							info->pres_temp_zone, vbatt_volt);
					turbo_charger_move_state(info, TURBO_STATE_CP_QUIT);
					heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;
				} else {
					if (turbo_charger_find_chrg_step(info,
							info->pres_temp_zone, vbatt_volt)) {
						heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;
						turbo_charger_move_state(info,
									TURBO_STATE_CP_CC_LOOP);
					} else {
						turbo_charger_move_state(info, TURBO_STATE_CP_QUIT);
					}
				}
				chrg_cv_taper_tunning_cnt = 0;
			} else {
				chrg_cv_taper_tunning_cnt++;
			}
		} else if (!info->sys_therm_cooling
			&& !info->batt_therm_cooling)  {
			chrg_cv_taper_tunning_cnt = 0;
			if (vbatt_volt > chrg_step.chrg_step_cv_volt + 10000) {
				if (chrg_cv_delta_volt > 20000)
					info->turbo_charger_request_volt -= chrg_cv_delta_volt;
				else
					info->turbo_charger_request_volt -= 20000;
			} else if (vbatt_volt < chrg_step.chrg_step_cv_volt - 10000) {
				chrg_cv_delta_volt -= 20000;
				info->turbo_charger_request_volt += 20000;
			} else {
				pr_info("%s: CV loop work well\n", __func__);
			}
		} else {
			if (vbatt_volt > (chrg_step.chrg_step_cv_volt + 10000)) {
				if (chrg_cv_delta_volt > 20000)
					info->turbo_charger_request_volt -= chrg_cv_delta_volt;
				else
					info->turbo_charger_request_volt -= 20000;
			} else {
				pr_info("%s CV loop work well\n", __func__);
			}
		}
#endif
	 TURBO_CHARGER_DBG("Temp zone:%d, vbatt_volt %d select chrg step %d, step cc curr %d, "
				"step cv volt %d, step cv tapper curr %d, "
				"is the last chrg step %d\n",
			info->pres_temp_zone, vbatt_volt,
			info->chrg_step.pres_chrg_step,
			info->chrg_step.chrg_step_cc_curr,
			info->chrg_step.chrg_step_cv_volt,
			info->chrg_step.chrg_step_cv_tapper_curr,
			info->chrg_step.last_step);

		if (vbatt_volt >= chrg_step.chrg_step_cv_volt && (!chrg_step.last_step)) {
			turbo_charger_find_chrg_step(info, info->pres_temp_zone, vbatt_volt);
			if (ibatt_curr > info->chrg_step.chrg_step_cv_tapper_curr + 10000) {
				heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;
				turbo_charger_move_state(info, TURBO_STATE_CP_CC_LOOP);
			}
		} else {
			if (ffc_batt_full) {
				if (chrg_cv_taper_tunning_cnt > CV_TAPPER_COUNT) {
					turbo_charger_find_chrg_step(info, info->pres_temp_zone, vbatt_volt);
					turbo_charger_move_state(info, TURBO_STATE_CP_QUIT);
					heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
					chrg_cv_taper_tunning_cnt = 0;
					info->cp_chg_done = true;
				} else {
					chrg_cv_taper_tunning_cnt++;
				}
			} else if (info->chrg_step.last_step && (ibatt_curr < info->turbo_charging_curr_min) &&
					(info->batt.temp < BATTERY_TEMP_LOW_TURBO || info->batt.temp >= BATTERY_TEMP_HIGH_TURBO)) {
				info->cp_chg_done = true;
				is_turbo_charger_ready = false;
				if (info->cp.charge_enabled) {
					turbo_charger_set_curr_limit_sw(info, info->turbo_charging_curr_min + 200000);
					turbo_charger_set_chg_curr_limit_sw(info, info->turbo_charging_curr_min);
					turbo_charger_enable_sw(info, true);
					turbo_charger_enable_cp(info, false);
					turbo_charger_check_cp_enabled(info);
				}

				info->sys_therm_cooling = false;
				info->batt_therm_cooling = false;
				info->batt_therm_cooling_cnt = 0;

				turbo_charger_move_state(info, TURBO_STATE_RECOVERY_SW);
				heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
			} else {
				if (vbatt_volt > info->chrg_step.chrg_step_cv_volt + 10000) {
					info->turbo_charger_request_volt -= 20000;
				} else if (vbatt_volt < info->chrg_step.chrg_step_cv_volt - 10000) {
					info->turbo_charger_request_volt += 20000;
				} else {
					TURBO_CHARGER_DBG("CV loop work well, continue\n");
				}
			}
		}
		TURBO_CHARGER_DBG("CV Level turbo_charger_request_curr: %d, turbo_charger_request_volt: %d\n",
						info->turbo_charger_request_curr, info->turbo_charger_request_volt);

		if (g_capacity_level_limited_flag) {
			if (!info->sw.charge_enabled && (batt_soc >= DEMO_SOC_LIMIT)) {
				TURBO_CHARGER_DBG("Demo vesion is on, limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
		}

		if (g_battery_protect_enable_flag) {
			if (!info->sw.charge_enabled) {
				TURBO_CHARGER_DBG("battery protct mode is on, limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_SW_ENTRY);
			}
		}

		if (!mtk_can_charging) { // mtk charger thread limit charging
			if (!info->sw.charge_enabled) {
				TURBO_CHARGER_INFO("mtk charger thread limit charging!!!\n");
				turbo_charger_move_state(info, TURBO_STATE_STOP_CHARGE);
			}
		}
		break;

	case TURBO_STATE_CP_QUIT:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		is_turbo_charger_ready = false;

		if (info->cp.charge_enabled) {
			//turbo_charger_set_curr_limit_sw(info, info->turbo_charging_curr_min + 200000);
			//turbo_charger_set_chg_curr_limit_sw(info, info->turbo_charging_curr_min);
			//turbo_charger_enable_sw(info, true);
			turbo_charger_enable_cp(info, false);
			turbo_charger_check_cp_enabled(info);
		}

		info->sys_therm_cooling = false;
		info->batt_therm_cooling = false;
		info->batt_therm_cooling_cnt = 0;

		//turbo_charger_move_state(info, TURBO_STATE_RECOVERY_SW);
		turbo_charger_move_state(info, TURBO_STATE_STOP_CHARGE);
		heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
		break;

	case TURBO_STATE_RECOVERY_SW:
		heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		is_turbo_charger_ready = false;
		if (!info->sw.charge_enabled)
			turbo_charger_enable_sw(info, true);
		turbo_charger_set_curr_limit_sw(info, info->turbo_charging_curr_min + 200000); //disable qc thermal
		turbo_charger_set_chg_curr_limit_sw(info, info->turbo_charging_curr_min);
		power_supply_changed(info->usb_psy);//update psy to wakeup charger thread

		info->turbo_charger_request_curr = info->turbo_charger_request_curr_prev;
		if (ibatt_curr > info->chrg_step.chrg_step_cc_curr) {
			info->turbo_charger_request_volt -= CV_DELTA_VOLT;
			chrg_cv_taper_tunning_cnt = 0;
		} else {
			info->turbo_charger_request_volt = vbus_volt;
			chrg_cv_taper_tunning_cnt++;
		}

		if (chrg_cv_taper_tunning_cnt > CV_TAPPER_COUNT) {
			heartbeat_delay_ms = HEARTBEAT_SHORT_DELAY_MS;
			//turbo_charger_enable_term_sw(info, true);
			charger_dev_enable_vbus_ovp(info->sw_chg, true);
			turbo_charger_move_state(info, TURBO_STATE_SW_LOOP);
		}
		TURBO_CHARGER_DBG("turbo_charger_support:%d, pres_chrg_step:%d, turbo_charger_request_volt:%d, chrg_step_cc_curr:%d\n",
						info->turbo_charger_support, info->chrg_step.pres_chrg_step,
						info->turbo_charger_request_volt, info->turbo_charger_request_curr);
		break;

	case TURBO_STATE_SW_LOOP:
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		TURBO_CHARGER_DBG("turbo_charger_support:%d, pres_chrg_step:%d, last_step:%d vbatt_volt:%d, chrg_step_cc_curr:%d, batt_soc:%d\n",
						info->turbo_charger_support, info->chrg_step.pres_chrg_step, info->chrg_step.last_step,
						vbatt_volt, info->chrg_step.chrg_step_cc_curr, batt_soc);
		is_turbo_charger_ready = false;
		if (info->turbo_charger_support
				&& info->chrg_step.pres_chrg_step != (info->turbo_charger_step_nums - 1)
				&& info->chrg_step.last_step == false
				&& vbatt_volt > info->pl_chrg_vbatt_min
				&& info->chrg_step.chrg_step_cc_curr > info->turbo_charging_curr_min
				&& batt_soc < CP_CHRG_SOC_LIMIT
				&& info->system_thermal_level > info->thermal_min_level) {
			TURBO_CHARGER_INFO("Enter CP, the reason is : turbo charger support %d, vbatt_volt %d uV, chrg step %d\n",
						info->turbo_charger_support, vbatt_volt, info->chrg_step.pres_chrg_step);
			turbo_charger_move_state(info, TURBO_STATE_CHRG_PUMP_ENTRY);
		} else if (!info->sw.charge_enabled) {
			turbo_charger_move_state(info, TURBO_STATE_STOP_CHARGE);
		} else {
			TURBO_CHARGER_INFO("Continue to SW charging, vbatt_volt %d uV, ibatt %d uA\n", vbatt_volt, ibatt_curr);
		}

		/*if ((ibatt_curr < 900000) && (g_in_flag == 0)) { // Switch battery cv when the current is less than 900ma
			g_in_flag = 1;
			TURBO_CHARGER_DBG("into ibatt_curr < 900000\n");
			//charger_set_thread_cv(4488000);
			charger_dev_set_constant_voltage(info->sw_chg, 4490000);
		} else if (ibatt_curr > 916000) {			  //Switch battery cv when the current is greater than 900ma
			TURBO_CHARGER_DBG("into ibatt_curr > 916000\n");
			g_in_flag = 0;
		}*/
		heartbeat_delay_ms = HEARTBEAT_SW_DELAY_MS;
		break;

	case TURBO_STATE_STOP_CHARGE:
		heartbeat_delay_ms = HEARTBEAT_SW_DELAY_MS;
		TURBO_CHARGER_DBG("current state is : %s\n", turbo_charger_state_str[info->state]);
		is_turbo_charger_ready = false;
		if (info->cp.charge_enabled) {
			turbo_charger_enable_cp(info, false);
			turbo_charger_check_cp_enabled(info);
		}

		if (info->sw.charge_enabled) {
			turbo_charger_enable_sw(info, false);
		}

		//usbqc_pm_set_swchg_cap(info, 2500);
		turbo_charger_find_chrg_step(info, info->pres_temp_zone, vbatt_volt);
		if (info->pres_temp_zone != ZONE_COLD
				&& info->pres_temp_zone != ZONE_HOT
				&& info->sw.charge_enabled
				&& !info->cp_chg_done
				&& info->chrg_step.chrg_step_cc_curr > 0
				&& mtk_can_charging) {
			turbo_charger_move_state(info, TURBO_STATE_ENTRY);
			heartbeat_delay_ms = HEARTBEAT_NEXT_STATE_MS;
		}
		//info->qc_request_curr = 3000000;
		info->turbo_charger_request_volt = 5000000;
		info->turbo_charger_request_volt_prev = info->turbo_charger_request_volt;
		break;

	default:
		break;
	}

schedule:
	info->turbo_charger_request_volt = min(info->turbo_charger_request_volt, info->turbo_charger_volt_max);
	info->turbo_charger_request_curr = min(info->turbo_charger_request_curr, info->turbo_charger_curr_max);

	TURBO_CHARGER_DBG("For Thermal into schedule level is %d uA, sys therm curr is %d uA\n",
		info->system_thermal_level, info->turbo_charger_sys_therm_curr);
	if (info->system_thermal_level == THERMAL_NOT_LIMIT) {
		TURBO_CHARGER_DBG("For Thermal level: %d, min Level: %d State: %d, middle: %d\n",
				info->system_thermal_level, info->thermal_min_level,
				info->state,  info->typec_middle_current);
		info->sys_therm_cooling = false;
		info->batt_thermal_cooling = false;
	} else if (info->system_thermal_level <= info->thermal_min_level && info->batt_thermal_cooling) {
		info->batt_thermal_cooling = false;
		info->sys_therm_cooling = false;
		turbo_charger_move_state(info, TURBO_STATE_CP_QUIT);
		TURBO_CHARGER_DBG("For Thermal is the highest : %d, "
					"Force enter into switch charging !\n", info->system_thermal_level);
	} else if (info->system_thermal_level > info->thermal_min_level
				&& (info->state == TURBO_STATE_CP_CC_LOOP || info->state == TURBO_STATE_CP_CV_LOOP)) {
		TURBO_CHARGER_DBG("For Thermal level is %d\n", info->system_thermal_level);
		if (!info->sys_therm_cooling) {
			info->sys_therm_cooling = true;
			info->turbo_charger_sys_therm_volt = info->turbo_charger_request_volt_prev;
			info->turbo_charger_sys_therm_curr = info->turbo_charger_request_curr_prev;
			info->turbo_charger_ibatt_curr_prev = ibatt_curr;
		}
		info->batt_thermal_cooling = true;
		//info->sys_therm_cooling = false;
		if (ibatt_curr > info->system_thermal_level + CC_CURR_DEBOUNCE) {
			if ((info->turbo_charger_sys_therm_curr - THERMAL_TUNNING_CURR) >= (info->typec_middle_current - 200000)) {
				if (ibatt_curr < info->turbo_charger_ibatt_curr_prev - 33000) {
					info->turbo_charger_ibatt_curr_prev = ibatt_curr;
					info->turbo_charger_sys_therm_curr -= THERMAL_TUNNING_CURR;
					info->turbo_charger_therm_loop_cn = 0;
					TURBO_CHARGER_DBG("into ibatt_curr < info->turbo_charger_ibatt_curr_prev - 20000\n");
				} else {
					info->turbo_charger_therm_loop_cn++;
					TURBO_CHARGER_DBG("turbo_charger_therm_loop_cn is %d\n", info->turbo_charger_therm_loop_cn);
					if (info->turbo_charger_therm_loop_cn == 10) {
						info->turbo_charger_sys_therm_curr -= THERMAL_TUNNING_CURR;
						info->turbo_charger_therm_loop_cn = 0;
					}
					TURBO_CHARGER_DBG("thermal decrease current loop well, turbo charger sys therm curr is %d, continue\n",
								info->turbo_charger_sys_therm_curr);
				}
				//info->qc_request_curr = info->qc_request_curr_prev - THERMAL_TUNNING_CURR;//= info->qc_sys_therm_curr;
				TURBO_CHARGER_DBG("For thermal, decrease turbo curr %d\n", info->turbo_charger_sys_therm_curr);
			} else {
				TURBO_CHARGER_DBG("For Thermal turbo_charger_sys_therm_curr %d uA was less than %d uA, "
								"Give up thermal mitigation!\n",
								info->turbo_charger_sys_therm_curr - THERMAL_TUNNING_CURR, info->typec_middle_current);
			}
		} else if (ibatt_curr < info->system_thermal_level - CC_CURR_DEBOUNCE) {
			if (ibatt_curr + THERMAL_TUNNING_CURR <= info->chrg_step.chrg_step_cc_curr) {
				if (info->turbo_charger_sys_therm_curr <= (info->system_thermal_level / 2)) {
					info->turbo_charger_sys_therm_curr += THERMAL_TUNNING_CURR;
					//info->qc_request_curr += THERMAL_TUNNING_CURR;//info->qc_sys_therm_curr;
					TURBO_CHARGER_DBG("For Thermal, increase turbo charger sys therm curr %d uA\n",info->turbo_charger_sys_therm_curr);
				}
			}
		}
		heartbeat_delay_ms = THERMAL_DELAY_LESS_MS;
	} else if (info->state == TURBO_STATE_CP_CC_LOOP || info->state == TURBO_STATE_CP_CV_LOOP) {
		info->batt_thermal_cooling = true;
	}/*else if(info->system_thermal_level != THERMAL_NOT_LIMIT
		&& info->system_thermal_level > info->thermal_min_level && info->state == PM_STATE_SW_LOOP) {
			turbo_charger_move_state(info, PM_STATE_ENTRY);
	}*/
	if (info->sys_therm_cooling)
		info->turbo_charger_request_curr = min(info->turbo_charger_request_curr, info->turbo_charger_sys_therm_curr);

	if (state != TURBO_STATE_DISCONNECT && state != TURBO_STATE_SW_LOOP) {
		if (g_capacity_level_limited_flag) {
			if (!info->sw.charge_enabled && (batt_soc >= DEMO_SOC_LIMIT)) {
				info->turbo_charger_request_volt = 5000000;
			}
		}

		if (g_battery_protect_enable_flag) {
			if (!info->sw.charge_enabled) {
				info->turbo_charger_request_volt = 5000000;
			}
		}
		turbo_charger_select_pdo(info, info->turbo_charger_request_volt, info->turbo_charger_request_curr);
	}

	return ret;
}

static void turbo_charger_update_status_work(struct work_struct *work)
{
	int ret = 0;
	struct turbo_charger_algo_info *info = container_of(work,
				struct turbo_charger_algo_info, turbo_charger_work.work);

	TURBO_CHARGER_INFO("enter\n");
	mutex_lock(&info->turbo_charger_lock);

	ret = turbo_charger_update_sw_status(info);
	if (ret < 0) {
		schedule_delayed_work(&info->turbo_charger_work,
					msecs_to_jiffies(RETRY_TURBO_CHARGER));
		mutex_unlock(&info->turbo_charger_lock);
		return;
	}

	ret = turbo_charger_update_cp_status(info);
	if (ret < 0) {
		schedule_delayed_work(&info->turbo_charger_work,
					msecs_to_jiffies(RETRY_TURBO_CHARGER));
		mutex_unlock(&info->turbo_charger_lock);
		return;
	}

	ret = turbo_charger_update_batt_status(info);
	if (ret < 0) {
		schedule_delayed_work(&info->turbo_charger_work,
					msecs_to_jiffies(RETRY_TURBO_CHARGER));
		mutex_unlock(&info->turbo_charger_lock);
		return;
	}

	turbo_charger_find_temp_zone(info, info->batt.temp);

	ret = turbo_charger_sm_work_func(info);
	if (!ret && info->turbo_charger_active) {
		TURBO_CHARGER_DBG("schedule SM work, sm state %s, heartbeat delay %d ms\n",
					turbo_charger_state_str[info->state], heartbeat_delay_ms);
		if (heartbeat_delay_ms != -1) {
			schedule_delayed_work(&info->turbo_charger_work,
						msecs_to_jiffies(heartbeat_delay_ms));
		} else {
			schedule_delayed_work(&info->turbo_charger_work,
						msecs_to_jiffies(NORMAL_TURBO_CHARGER));
		}
	}

	info->system_thermal_level = g_thermal_charging_current_limit;
	TURBO_CHARGER_INFO("thermal limit charging current:%d uA\n", info->system_thermal_level);

	mutex_unlock(&info->turbo_charger_lock);

	return;
}

static void turbo_charger_reset_charger_type(struct turbo_charger_algo_info *info)
{
	if (info->qc_phy_z350) {
		z350_reset_charger_type();
	} else if (info->qc_phy_wt6670f) {
		wt6670f_reset_charger_type();
	}
}

static void turbo_charger_disconnect(struct turbo_charger_algo_info *info)
{
	TURBO_CHARGER_DBG("enter\n");

	turbo_charger_enable_cp(info, false);
	turbo_charger_check_cp_enabled(info);
	cancel_delayed_work(&info->turbo_charger_work);

	if (info->sw.charge_enabled) {
		turbo_charger_enable_sw(info, false);
	}

	info->turbo_charger_request_volt = 0;
	info->turbo_charger_request_curr = 0;
	info->turbo_charger_support = false;
	info->turbo_charger_active = false;
	is_turbo_charger_ready = false;
	/*TN Begin modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
	info->total_count = 0;
	/*TN End modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
	info->cp_chg_done = false;

	turbo_charger_reset_charger_type(info);
	turbo_charger_set_curr_limit_sw(info, info->turbo_charging_curr_min + 200000);
	turbo_charger_set_chg_curr_limit_sw(info, info->turbo_charging_curr_min);
	charger_dev_enable_adc(info->cp_chg, false);
	charger_dev_set_constant_voltage(info->sw_chg, 4500000); //Default battery cv
	//charger_dev_set_eoc_current(info->sw_chg, 300000); //sw cut-off current
	turbo_charger_enable_term_sw(info, true);

	turbo_charger_move_state(info, TURBO_STATE_DISCONNECT);
}

static int turbo_charger_reset_type(struct notifier_block *nb, unsigned long code, void *unused)
{
	struct turbo_charger_algo_info *info = container_of(nb,
				struct turbo_charger_algo_info, reboot_notifier);

	if (code == SYS_RESTART || code == SYS_POWER_OFF) {
		TURBO_CHARGER_INFO("start rebooting, reset turbo charger type\n");
		turbo_charger_disconnect(info);
	}

	return NOTIFY_DONE;
}

static int turbo_charger_psy_notifier_cb(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct turbo_charger_algo_info *info = container_of(nb,
						struct turbo_charger_algo_info, nb);
	union power_supply_propval val = {0};
	struct power_supply *psy = data;
	int ret = 0;

	TURBO_CHARGER_DBG("enter, power supply name is %s\n", psy->desc->name);

	if (IS_ERR_OR_NULL(info)) {
		TURBO_CHARGER_ERR("failed to get turbo_charger_algo_info device\n");
		return NOTIFY_DONE;
	}

	TURBO_CHARGER_DBG("++\n");

	ret = turbo_charger_update_sw_status(info);
	if (ret < 0) {
		TURBO_CHARGER_ERR("failed to update switch charger status\n");
		return NOTIFY_DONE;
	}

	if (psy == info->qc_logic_psy) {
		ret = power_supply_get_property(info->qc_logic_psy,
						POWER_SUPPLY_PROP_TYPE, &val);
		if (ret < 0) {
			TURBO_CHARGER_ERR("failed to get qc type\n");
		} else {
			info->turbo_charger_type = val.intval;
			if (info->turbo_charger_type == QC3P_POWER_27W && mtk_can_charging) {
				info->turbo_charger_support = true;
				info->turbo_charger_active = true;
				schedule_delayed_work(&info->turbo_charger_work,
							msecs_to_jiffies(0));
			}
			TURBO_CHARGER_DBG("turbo charger type:%d, mtk_can_charging:%d\n",
						info->turbo_charger_type, mtk_can_charging);
		}
	} else if (psy == info->usb_psy) {
		//if (!info->sw.usb_online && info->turbo_charger_active) {
		if (info->sw.charger_type == POWER_SUPPLY_TYPE_UNKNOWN && info->turbo_charger_active) {
			TURBO_CHARGER_DBG("plug out charger, stop turbo charger\n");
			turbo_charger_disconnect(info);
		}
	}

	turbo_charger_active = info->turbo_charger_active;
	return NOTIFY_OK;
}

static int turbo_charger_parse_dt(struct turbo_charger_algo_info *info)
{
	struct device_node *np = info->dev->of_node;
	u32 val;
	int ret = 0;
	u32 args[7];
	int idx;

	ret = of_property_read_u32(np, "volt-steps", &val);
	if (ret >= 0) {
		info->turbo_charger_volt_steps = val;
		TURBO_CHARGER_DBG("of find property volt-steps:%d\n",
						info->turbo_charger_volt_steps);
	} else {
		TURBO_CHARGER_DBG("use default volt-steps:%d\n",
						DEFAULT_TURBO_VOLT_STEPS);
		info->turbo_charger_volt_steps = DEFAULT_TURBO_VOLT_STEPS;
	}

	ret = of_property_read_u32(np, "curr-steps", &val);
	if (ret > 0) {
		info->turbo_charger_curr_steps = val;
		TURBO_CHARGER_DBG("of find property curr-steps:%d\n",
						info->turbo_charger_curr_steps);
	} else {
		TURBO_CHARGER_DBG("use default curr-steps:%d\n",
						DEFAULT_TURBO_CURR_STEPS);
		info->turbo_charger_curr_steps = DEFAULT_TURBO_CURR_STEPS;
	}

	ret = of_property_read_u32(np, "volt-max", &val);
	if (ret >= 0) {
		info->turbo_charger_volt_max = val;
		TURBO_CHARGER_DBG("of find property volt-max:%d\n",
						info->turbo_charger_volt_max);
	} else {
		TURBO_CHARGER_DBG("use default volt-max:%d\n",
						DEFAULT_TURBO_VOLT_MAX);
		info->turbo_charger_volt_max = DEFAULT_TURBO_VOLT_MAX;
	}

	ret = of_property_read_u32(np, "curr-max", &val);
	if (ret >= 0) {
		info->turbo_charger_curr_max = val;
		TURBO_CHARGER_DBG("of find property curr-max:%d\n",
						info->turbo_charger_curr_max);
	} else {
		TURBO_CHARGER_DBG("use default curr-max:%d\n",
						DEFAULT_TURBO_CURR_MAX);
		info->turbo_charger_curr_max = DEFAULT_TURBO_CURR_MAX;
	}

	ret = of_property_read_u32(np, "batt-curr-boost", &val);
	if (ret >= 0) {
		info->batt_curr_boost = val;
		TURBO_CHARGER_DBG("of find property batt-curr-boost:%d\n",
						info->batt_curr_boost);
	} else {
		TURBO_CHARGER_DBG("use default batt-curr-boost:%d\n",
						DEFAULT_BATT_CURR_BOOST);
		info->batt_curr_boost = DEFAULT_BATT_CURR_BOOST;
	}

	ret = of_property_read_u32(np, "batt-ovp-limit", &val);
	if (ret >= 0) {
		info->batt_ovp_limit = val;
		TURBO_CHARGER_DBG("of find property batt-ovp-limit:%d\n",
						info->batt_ovp_limit);
	} else {
		TURBO_CHARGER_DBG("use default batt-ovp-limits:%d\n",
						DEFAULT_BATT_OVP_LIMIT);
		info->batt_ovp_limit = DEFAULT_BATT_OVP_LIMIT;
	}

	ret = of_property_read_u32(np, "pl-chrg-vbatt-min", &val);
	if (ret >= 0) {
		info->pl_chrg_vbatt_min = val;
		TURBO_CHARGER_DBG("of find property pl-chrg-vbatt-min:%d\n",
						info->pl_chrg_vbatt_min);
	} else {
		TURBO_CHARGER_DBG("use default pl-chrg-vbatt-min:%d\n",
						DEFAULT_PL_CHRG_VBATT_MIN);
		info->pl_chrg_vbatt_min = DEFAULT_PL_CHRG_VBATT_MIN;
	}

	ret = of_property_read_u32(np, "typec-middle-current", &val);
	if (ret >= 0) {
		info->typec_middle_current = val;
		TURBO_CHARGER_DBG("of find property typec-middle-current:%d\n",
						info->typec_middle_current);
	} else {
		TURBO_CHARGER_DBG("use default typec-middle-current:%d\n",
						DEFAULT_TYPEC_MIDDLE_CURRENT);
		info->typec_middle_current = DEFAULT_TYPEC_MIDDLE_CURRENT;
	}

	ret = of_property_read_u32(np, "step-first-current-comp", &val);
	if (ret >= 0) {
		info->step_first_current_comp = val;
		TURBO_CHARGER_DBG("of find property step-first-current-comp:%d\n",
						info->step_first_current_comp);
	} else {
		TURBO_CHARGER_DBG("use default step-first-current-comp:%d\n",
						DEFAULT_STEP_FIRST_CURR_COMP);
		info->step_first_current_comp = DEFAULT_STEP_FIRST_CURR_COMP;
	}

	ret = of_property_read_u32(np, "temp-zones-num", &val);
	if (ret >= 0) {
		info->turbo_charger_temp_zones_num = val;
		TURBO_CHARGER_DBG("of find property temp-zones-num:%d\n",
						info->turbo_charger_temp_zones_num);
	} else {
		TURBO_CHARGER_DBG("use default temp-zones-num:%d\n",
						DEFAULT_TEMP_ZONES_NUM);
		info->turbo_charger_temp_zones_num = DEFAULT_TEMP_ZONES_NUM;
	}

	info->temp_zones =
		(struct turbo_charger_step_zone *)kzalloc(sizeof(struct turbo_charger_step_zone) * (info->turbo_charger_temp_zones_num), GFP_KERNEL);
	if (!info->temp_zones) {
		TURBO_CHARGER_DBG("no memory\n");
	}

	for (idx = 0; idx < info->turbo_charger_temp_zones_num; idx++) {
		of_property_read_u32_index(np, "turbo-chrg-temp-zones", 7 * idx, &args[0]);
		of_property_read_u32_index(np, "turbo-chrg-temp-zones", 7 * idx + 1, &args[1]);
		of_property_read_u32_index(np, "turbo-chrg-temp-zones", 7 * idx + 2, &args[2]);
		of_property_read_u32_index(np, "turbo-chrg-temp-zones", 7 * idx + 3, &args[3]);
		of_property_read_u32_index(np, "turbo-chrg-temp-zones", 7 * idx + 4, &args[4]);
		of_property_read_u32_index(np, "turbo-chrg-temp-zones", 7 * idx + 5, &args[5]);
		of_property_read_u32_index(np, "turbo-chrg-temp-zones", 7 * idx + 6, &args[6]);

		info->temp_zones[idx].temp_c = args[0];
		info->temp_zones[idx].chrg_step_power[0].chrg_step_volt = args[1] * 1000;
		info->temp_zones[idx].chrg_step_power[0].chrg_step_curr = args[2] * 1000;
		info->temp_zones[idx].chrg_step_power[1].chrg_step_volt = args[3] * 1000;
		info->temp_zones[idx].chrg_step_power[1].chrg_step_curr = args[4] * 1000;
		info->temp_zones[idx].chrg_step_power[2].chrg_step_volt = args[5] * 1000;
		info->temp_zones[idx].chrg_step_power[2].chrg_step_curr = args[6] * 1000;

		TURBO_CHARGER_DBG("turbo charger temp zone idx:%d, temp_c:%d, "
				"0_chrg_step_volt:%d mV, 0_chrg_step_curr:%d mA, "
				"1_chrg_step_volt:%d mV, 1_chrg_step_curr:%d mA, "
				"2_chrg_step_volt:%d mV, 2_chrg_step_curr:%d mA\n", idx,
				args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
	}

	ret = of_property_read_u32(np, "thermal-min-level", &val);
	if (ret >= 0) {
		info->thermal_min_level = val;
		TURBO_CHARGER_DBG("of find property thermal-min-level:%d\n",
						info->thermal_min_level);
	} else {
		TURBO_CHARGER_DBG("use default thermal-min-level:%d\n",
						DEFAULT_THERMAL_MIN_LEVEL);
		info->thermal_min_level = DEFAULT_THERMAL_MIN_LEVEL;
	}

	ret = of_property_read_u32(np, "sw-charging-curr-limited", &val);
	if (ret >= 0) {
		info->sw_charging_curr_limited = val;
		TURBO_CHARGER_DBG("of find property sw-charging-curr-limited:%d\n",
						info->sw_charging_curr_limited);
	} else {
		TURBO_CHARGER_DBG("use default sw-charging-curr-limited:%d\n",
						DEFAULT_SW_CURR_LIMITED);
		info->sw_charging_curr_limited = DEFAULT_SW_CURR_LIMITED;
	}

	ret = of_property_read_u32(np, "charging-curr-min", &val);
	if (ret >= 0) {
		info->turbo_charging_curr_min = val;
		TURBO_CHARGER_DBG("of find property charging-curr-min:%d\n",
						info->turbo_charging_curr_min);
	} else {
		TURBO_CHARGER_DBG("use default charging-curr-min:%d\n",
						DEFAULT_CHARGING_CURR_MIN);
		info->turbo_charging_curr_min = DEFAULT_CHARGING_CURR_MIN;
	}

	ret = of_property_read_u32(np, "turbo-volt-comp", &val);
	if (ret >= 0) {
		info->turbo_charger_volt_comp = val;
		TURBO_CHARGER_DBG("of find property turbo-volt-comp:%d\n",
						info->turbo_charger_volt_comp);
	} else {
		TURBO_CHARGER_DBG("use default turbo-volt-comp:%d\n", DEFAULT_TURBO_VOLT_COMP);
		info->turbo_charger_volt_comp = DEFAULT_TURBO_VOLT_COMP;
	}

	return ret;
}

static int turbo_charger_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct turbo_charger_algo_info *info;

	TURBO_CHARGER_INFO("enter\n");

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	if (oem_pcba_charge_power() != CHARGE_POWER_33W) {
		TURBO_CHARGER_ERR("not support 33W turbo charger, "
					"not init algorithm\n");
		return -ENODEV;
	}
#endif

#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	if (!qc_logic_probe_done) {
		TURBO_CHARGER_ERR("qc logic not exist, "
					"not init turbo charger algorithm\n");
		return -ENODEV;
	}
#endif

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	ret = turbo_charger_parse_dt(info);
	if (ret < 0) {
		TURBO_CHARGER_ERR("parse dt fail(%d)\n", ret);
		ret = -EINVAL;
		goto free_mem;
	}

	info->turbo_charger_step_nums = 3;
	info->pres_temp_zone = ZONE_NONE;
	info->system_thermal_level = THERMAL_NOT_LIMIT;
	info->turbo_charger_support = false;
	info->turbo_charger_result = 0;
	info->turbo_charger_cc_loop_stage = false;
	info->qc_phy_z350 = false;
	info->qc_phy_wt6670f = false;

	mutex_init(&info->turbo_charger_lock);

	device_init_wakeup(info->dev, true);

	ret = turbo_charger_check_usb_psy(info);
	if (ret < 0)
		return -ENODEV;

	ret = turbo_charger_check_cp_psy(info);
	if (ret < 0)
		return -ENODEV;

	ret = turbo_charger_check_batt_psy(info);
	if (ret < 0)
		return -EPROBE_DEFER;

	ret = turbo_charger_get_sw_device(info);
	if (ret < 0)
		return -EPROBE_DEFER;

	ret = turbo_charger_get_cp_device(info);
	if (ret < 0)
		return -EPROBE_DEFER;

	ret = turbo_charger_get_qc_device(info);
	if (ret < 0)
		return -EPROBE_DEFER;

	info->nb.notifier_call = turbo_charger_psy_notifier_cb;
	power_supply_reg_notifier(&info->nb);

	info->reboot_notifier.notifier_call = turbo_charger_reset_type;
	info->reboot_notifier.priority = 255;
	register_reboot_notifier(&info->reboot_notifier);

	INIT_DELAYED_WORK(&info->turbo_charger_work, turbo_charger_update_status_work);

	TURBO_CHARGER_INFO("successfully\n");
	return 0;

free_mem:
	devm_kfree(&pdev->dev, info);

	TURBO_CHARGER_ERR("failed\n");
	return ret;

}

static int turbo_charger_remove(struct platform_device *pdev)
{
	struct turbo_charger_algo_info *info = platform_get_drvdata(pdev);

	if (info) {
		mutex_destroy(&info->turbo_charger_lock);
		cancel_delayed_work(&info->turbo_charger_work);
		unregister_reboot_notifier(&info->reboot_notifier);
		power_supply_unreg_notifier(&info->nb);
	}

	return 0;
}

static int turbo_charger_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct turbo_charger_algo_info *info = platform_get_drvdata(pdev);

	TURBO_CHARGER_INFO("++\n");
	mutex_lock(&info->turbo_charger_lock);

	return 0;
}

static int turbo_charger_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct turbo_charger_algo_info *info = platform_get_drvdata(pdev);

	TURBO_CHARGER_INFO("++\n");
	mutex_unlock(&info->turbo_charger_lock);

	return 0;
}

static SIMPLE_DEV_PM_OPS(turbo_charger_pm_ops, turbo_charger_suspend, turbo_charger_resume);

static const struct of_device_id mtk_turbo_charger_of_match[] = {
	{ .compatible = "tinno,turbo_charger", },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_turbo_charger_of_match);

static struct platform_driver turbo_charger_platdrv = {
	.probe = turbo_charger_probe,
	.remove = turbo_charger_remove,
	.driver = {
		.name = "turbo_charger",
		.owner = THIS_MODULE,
		.pm = &turbo_charger_pm_ops,
		.of_match_table = mtk_turbo_charger_of_match,
	},
};

static int __init turbo_charger_init(void)
{
	return platform_driver_register(&turbo_charger_platdrv);
}

static void __exit turbo_charger_exit(void)
{
	platform_driver_unregister(&turbo_charger_platdrv);
}
late_initcall(turbo_charger_init);
module_exit(turbo_charger_exit);

MODULE_AUTHOR("hao.jia <hao.jia@tinno.com>");
MODULE_DESCRIPTION("Tinno Turbo Charger Algorithm");
MODULE_LICENSE("GPL");
