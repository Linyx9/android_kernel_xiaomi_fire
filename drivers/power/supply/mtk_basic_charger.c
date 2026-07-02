// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 *
 * Filename:
 * ---------
 *    mtk_basic_charger.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include "mtk_charger.h"

/* TN Begin modified by hao.jia/809321 20240729 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
#include "../../../oem/tinno_charger/tinno_charger.h"
#define RECHARGE_CAPACITY 95
#define RECHARGE_OVER_TEMP_GAP 5
#define HVDCP_AICL_CURRENT_STEP 100000
#define HVDCP_AICL_TARGET_VOLTAGE 6200
#define HVDCP_AICL_BASE_CURRENT 1000000
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by hao.jia/809321 20240729 CR/EKLAMU-202 */

/*TN Begin modified by hao.jia/809321 20240628 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
#define FFC_BAT_VOLT_COMP_MV	100
#define FFC_BAT_REDU_CURR_MA	1500
#define FFC_BAT_VOLT_MAX_UV	4528000
#define FFC_BAT_VOLT_STEP_UV	5000
bool is_turbo_charger_ready = false;
EXPORT_SYMBOL(is_turbo_charger_ready);

bool turbo_charger_active = false;
EXPORT_SYMBOL(turbo_charger_active);

bool ffc_batt_full = false;
EXPORT_SYMBOL(ffc_batt_full);

int ffc_reduce_count = 0;
EXPORT_SYMBOL(ffc_reduce_count);

extern int g_thermal_charging_current_limit;

#endif /* CONFIG_OEM_TURBO_CHARGER */

#define SW_BAT_VOLT_COMP_UV	16000
#define SW_BAT_REDU_CURR_MA	2000
/*TN End modified by hao.jia/809321 20240628 CR/EKLAMU-202 */

/* TN Begin modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
#define FACTORY_AGAING_SOC_GAP	15
#endif
/* TN End modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */

static int _uA_to_mA(int uA)
{
	if (uA == -1)
		return -1;
	else
		return uA / 1000;
}

/* TN Begin modified by xinjun.lu/860715 20241217 CR/EKLAMU-7959 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
static int check_hvdcp_aicl(struct mtk_charger *info)
{
	int i = 0;
	int vbus_now = 0;
	int temp_input_current_limit = 0;
	int count = 0;
	int mivr = 0;

	charger_dev_get_mivr(info->chg1_dev, &mivr);
	count = (info->data.hvdcp_input_current_limit - HVDCP_AICL_BASE_CURRENT) / HVDCP_AICL_CURRENT_STEP;
	chr_err("%s count=%d mivr=%d\n", __func__, count, mivr);

	for (i = 0; i < count; i++) {
		temp_input_current_limit = HVDCP_AICL_BASE_CURRENT + (i * HVDCP_AICL_CURRENT_STEP);
		charger_dev_set_input_current(info->chg1_dev, temp_input_current_limit + HVDCP_AICL_CURRENT_STEP);
		mdelay(100);
		vbus_now = get_vbus(info);
		if (vbus_now < 0) {
			chr_err("%s get vbus voltage failed\n", __func__);
			break;
		}
		if (vbus_now && vbus_now <= HVDCP_AICL_TARGET_VOLTAGE) {
			if (vbus_now >= mivr / 1000) {
				chr_err("%s vbus(%d) > vindpm(%d), it is not plug out\n", __func__, vbus_now, mivr / 1000);
				info->restart_hvdcp_work = true;
			}
			chr_err("%s find last input current:%d\n", __func__, temp_input_current_limit);
			break;
		}
		if (i == count -1) {
			temp_input_current_limit = temp_input_current_limit + HVDCP_AICL_CURRENT_STEP;
			chr_err("%s adapter support hvdcp input current, use hvdcp default\n", __func__);
		}
	}

	return temp_input_current_limit;
}
#endif
/* TN END modified by xinjun.lu/860715 20241217 CR/EKLAMU-7959 */

static void select_cv(struct mtk_charger *info)
{
	u32 constant_voltage;

	if (info->enable_sw_jeita)
		if (info->sw_jeita.cv != 0) {
			info->setting.cv = info->sw_jeita.cv;
			return;
		}
/*TN Begin modified by hao.jia/809321 20240907 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	if ((turbo_charger_active == true) && (info->sw_jeita.sm == TEMP_T2_TO_T3)) {
		constant_voltage = FFC_BAT_VOLT_MAX_UV;
	} else
#endif /* CONFIG_OEM_TURBO_CHARGER */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	if (!IS_ERR_OR_NULL(info->current_alg) &&
			info->current_alg->alg_id == PE5_ID &&
			chg_alg_is_algo_running(info->current_alg) &&
			info->sw_jeita.sm == TEMP_T2_TO_T3) {
		constant_voltage = info->pe50.target_fv;
	} else
#endif /* CONFIG_PE50_FFC_SUPPORT */
/*TN End modified by hao.jia/809321 20240907 CR/EKLAMU-202 */
	{
		constant_voltage = info->data.battery_cv;
	}
	info->setting.cv = constant_voltage;
}

static int mtk_charger_force_disable_power_path(struct mtk_charger *info,
	bool disable)
{
	int ret = 0;

	if (!info)
		return -EINVAL;

	mutex_lock(&info->pp_lock[CHG1_SETTING]);

	if (disable == info->force_disable_pp[CHG1_SETTING])
		goto out;

	info->force_disable_pp[CHG1_SETTING] = disable;
	ret = charger_dev_enable_powerpath(info->chg1_dev,
		info->force_disable_pp[CHG1_SETTING] ?
		false : info->enable_pp[CHG1_SETTING]);
out:
	mutex_unlock(&info->pp_lock[CHG1_SETTING]);
	return ret;
}

static bool is_typec_adapter(struct mtk_charger *info)
{
	int rp;
	int cap_type;

	rp = adapter_dev_get_property(info->adapter_dev[PD], TYPEC_RP_LEVEL);
	cap_type = adapter_dev_get_property(info->adapter_dev[PD], CAP_TYPE);
/* TN Begin modified by jirui.li/860702 20240927 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	if (cap_type == MTK_CAP_TYPE_UNKNOWN &&
			rp != 500 &&
			info->chr_type != POWER_SUPPLY_TYPE_USB &&
			info->chr_type != POWER_SUPPLY_TYPE_USB_CDP &&
			info->chr_type != POWER_SUPPLY_TYPE_USB_QC3P &&
			info->chr_type != POWER_SUPPLY_TYPE_USB_QC3)
#else
	if (cap_type == MTK_CAP_TYPE_UNKNOWN &&
			rp != 500 &&
			info->chr_type != POWER_SUPPLY_TYPE_USB &&
			info->chr_type != POWER_SUPPLY_TYPE_USB_CDP)
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by jirui.li/860702 20240927 CR/EKLAMU-202 */
		return true;

	return false;
}

static bool support_fast_charging(struct mtk_charger *info)
{
	struct chg_alg_device *alg;
	int i = 0, state = 0;
	bool ret = false;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		if (alg == NULL)
			continue;

		if (info->enable_fast_charging_indicator &&
		    ((alg->alg_id & info->fast_charging_indicator) == 0))
			continue;

		chg_alg_set_current_limit(alg, &info->setting);
		state = chg_alg_is_algo_ready(alg);
/* TN Begin modified by xinjun.lu/860715 20241105 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
		chr_err("%s %s ret:%s, prtocol_state:%d\n",
			__func__, dev_name(&alg->dev),
			chg_alg_state_to_str(state), info->protocol_state);
#else
		chr_debug("%s %s ret:%s, prtocol_state:%d\n",
			__func__, dev_name(&alg->dev),
			chg_alg_state_to_str(state), info->protocol_state);
#endif
/* TN End modified by xinjun.lu/860715 20241105 CR/EKLAMU-202 */

		if (state == ALG_READY || state == ALG_RUNNING) {
			ret = true;
			break;
		}
	}
	return ret;
}

static bool select_charging_current_limit(struct mtk_charger *info,
	struct chg_limit_setting *setting)
{
	struct charger_data *pdata, *pdata2, *pdata_dvchg;
	bool is_basic = false;
	u32 ichg1_min = 0, aicr1_min = 0;
	int ret;
/* TN Begin modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
	int uisoc = 0;

	uisoc = get_uisoc(info);
#endif
/* TN End modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */
	select_cv(info);

	pdata = &info->chg_data[CHG1_SETTING];
	pdata2 = &info->chg_data[CHG2_SETTING];
	pdata_dvchg = &info->chg_data[DVCHG1_SETTING];
	// pdata_dvchg2 = &info->chg_data[DVCHG2_SETTING];

	if (info->usb_unlimited) {
		pdata->input_current_limit =
					info->data.ac_charger_input_current;
		pdata->charging_current_limit =
					info->data.ac_charger_current;
		is_basic = true;
		goto done;
	}

	if (info->water_detected) {
		pdata->input_current_limit = info->data.usb_charger_current;
		pdata->charging_current_limit = info->data.usb_charger_current;
		is_basic = true;
		goto done;
	}

	if (((info->bootmode == 1) ||
	    (info->bootmode == 5)) && info->enable_meta_current_limit != 0) {
		pdata->input_current_limit = 200000; // 200mA
		is_basic = true;
		goto done;
	}

	if (info->atm_enabled == true
		&& (info->chr_type == POWER_SUPPLY_TYPE_USB ||
		info->chr_type == POWER_SUPPLY_TYPE_USB_CDP)
		) {
		pdata->input_current_limit = 100000; /* 100mA */
		is_basic = true;
		goto done;
	}

	if (info->chr_type == POWER_SUPPLY_TYPE_USB &&
	    info->usb_type == POWER_SUPPLY_USB_TYPE_SDP) {
		pdata->input_current_limit =
				info->data.usb_charger_current;
		/* it can be larger */
		pdata->charging_current_limit =
				info->data.usb_charger_current;
		is_basic = true;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_CDP) {
		pdata->input_current_limit =
			info->data.charging_host_charger_current;
		pdata->charging_current_limit =
			info->data.charging_host_charger_current;
		is_basic = true;

	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_DCP) {
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
		if (!info->is_hvdcp_detecting) {
			pdata->input_current_limit =
				info->data.ac_charger_input_current;
			pdata->charging_current_limit =
				info->data.ac_charger_current;
		} else {
			pdata->input_current_limit =
				500000;
			pdata->charging_current_limit =
				500000;
		}
#else
		pdata->input_current_limit =
			info->data.ac_charger_input_current;
		pdata->charging_current_limit =
			info->data.ac_charger_current;
#endif
		if (info->config == DUAL_CHARGERS_IN_SERIES) {
			pdata2->input_current_limit =
				pdata->input_current_limit;
			pdata2->charging_current_limit = 2000000;
		}
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB &&
	    info->usb_type == POWER_SUPPLY_USB_TYPE_DCP) {
		/* NONSTANDARD_CHARGER */
		pdata->input_current_limit =
			info->data.usb_charger_current;
		pdata->charging_current_limit =
			info->data.usb_charger_current;
		is_basic = true;
/* TN Begin modified by hao.jia/809321 20240823 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_FLOAT || info->chr_type == POWER_SUPPLY_TYPE_USB_NON_STD) {
		/* Float/Non-std Charger */
		chr_info("[%s]: for Float/Non-STD mode, set charge current:%d, input current:%d\n",
						__func__, info->data.usb_charger_current, info->data.usb_charger_current);
		pdata->input_current_limit =
			info->data.usb_charger_current;
		pdata->charging_current_limit =
			info->data.usb_charger_current;
		is_basic = true;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_QC2) {
		/* QC2.0 Charger */
		chr_info("[%s]: for QC2 mode, set charge current:%d, input current:%d\n",
						__func__, info->data.ac_charger_current, info->data.ac_charger_input_current);
		pdata->input_current_limit =
			info->data.ac_charger_input_current;
		pdata->charging_current_limit =
			info->data.ac_charger_current;
		is_basic = true;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_QC3) {
		/* QC3.0 Charger */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
		if (info->aicl_check) {
			charger_dev_set_charging_current(info->chg1_dev, info->data.hvdcp_charging_current_limit);
			info->aicl_final_ic = check_hvdcp_aicl(info);
			info->aicl_check = false;
			chr_err("%s hvdcp used input charging current limit: %d\n", __func__, info->aicl_final_ic);
		}
#endif
		chr_info("[%s]: for QC3 mode, set charge current:%d, input current:%d\n",
						__func__, info->data.hvdcp_charging_current_limit, info->data.hvdcp_input_current_limit);
		pdata->input_current_limit =
			info->data.hvdcp_input_current_limit;
		pdata->charging_current_limit =
			info->data.hvdcp_charging_current_limit;

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
		if (info->aicl_final_ic && info->aicl_final_ic < pdata->input_current_limit) {
			chr_err("[%s]: for QC3 mode, check and set aicl input current:%d\n",
						__func__, info->aicl_final_ic);
			pdata->input_current_limit = info->aicl_final_ic;
		}
		if (info->restart_hvdcp_work) {
			info->restart_hvdcp_work = false;
			schedule_delayed_work(&info->hvdcp_work, 0);
			chr_err("%s start restart_hvdcp_work\n", __func__);
		}
#endif
		is_basic = true;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_PDC) {
		/* PDC Charger */
		chr_info("[%s]: for PDC mode, set charge current:%d, input current:%d\n",
						__func__, info->data.pdc_charging_current_limit, info->data.pdc_input_current_limit);
		pdata->input_current_limit =
			info->data.pdc_input_current_limit;
		pdata->charging_current_limit =
			info->data.pdc_charging_current_limit;
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_QC3P) {
		/* QC3+ Charger */
		if (is_turbo_charger_ready) {
			chr_info("%s: turbo charge, limit input and charging current to 100mA\n", __func__);
			pdata->input_current_limit = 100000;  // uA
			pdata->charging_current_limit = 100000;  // uA
		} else {
			chr_info("%s: from turbo charge to basic charge, limit input and charging current to 1200mA\n", __func__);
			pdata->input_current_limit = 1400000;  // uA
			pdata->charging_current_limit = 1200000;  // uA
		}

		if (ffc_batt_full == true) {
			pdata->input_current_limit = info->data.ac_charger_input_current;
			pdata->charging_current_limit = 0; // mA
		}
#endif /* CONFIG_OEM_TURBO_CHARGER */
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by hao.jia/809321 20240823 CR/EKLAMU-202 */
	} else {
		/*chr_type && usb_type cannot match above, set 500mA*/
		pdata->input_current_limit =
				info->data.usb_charger_current;
		pdata->charging_current_limit =
				info->data.usb_charger_current;
		is_basic = true;
	}

/* TN Begin modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	if (!IS_ERR_OR_NULL(info->current_alg) && info->current_alg->alg_id == PE5_ID) {
		info->setting.pe50_fcc_limit = ((info->pe50.target_fcc < 0) ? 0 : info->pe50.target_fcc);
		if (pdata->thermal_charging_current_limit < 0 ||
			pdata->thermal_charging_current_limit > info->pe50.min_therm_current_limit)
			info->setting.pe50_current_limit_dvchg1 = pdata->thermal_charging_current_limit;
		else
			info->setting.pe50_current_limit_dvchg1 = info->pe50.min_therm_current_limit;
	}
#endif
/* TN End modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */

	if (support_fast_charging(info))
		is_basic = false;
	else {
		is_basic = true;
		/* AICL */
		if (!info->disable_aicl)
			charger_dev_run_aicl(info->chg1_dev,
				&pdata->input_current_limit_by_aicl);
		if (info->enable_dynamic_mivr) {
			if (pdata->input_current_limit_by_aicl >
				info->data.max_dmivr_charger_current)
				pdata->input_current_limit_by_aicl =
					info->data.max_dmivr_charger_current;
		}
		if (is_typec_adapter(info)) {
			if (adapter_dev_get_property(info->adapter_dev[PD]
			, TYPEC_RP_LEVEL)
				== 3000) {
				pdata->input_current_limit = 3000000;
				pdata->charging_current_limit = 3000000;
			} else if (adapter_dev_get_property(info->adapter_dev[PD],
				TYPEC_RP_LEVEL) == 1500) {
				pdata->input_current_limit = 1500000;
				pdata->charging_current_limit = 2000000;
			} else {
				chr_err("type-C: inquire rp error\n");
				if (info->en_cts_mode) {
					pdata->input_current_limit = 100000;
					pdata->charging_current_limit = 100000;
				} else {
					pdata->input_current_limit = 500000;
					pdata->charging_current_limit = 500000;
				}
			}

			chr_err("type-C:%d current:%d\n",
				info->ta_status[PD],
				adapter_dev_get_property(info->adapter_dev[PD],
					TYPEC_RP_LEVEL));
		}
	}

	if (info->enable_sw_jeita) {
		if (IS_ENABLED(CONFIG_USBIF_COMPLIANCE)
			&& info->chr_type == POWER_SUPPLY_TYPE_USB)
			chr_debug("USBIF & STAND_HOST skip current check\n");
		else {
/* TN Begin modified by jirui.li/860702 20240904 CR/EKLAMU-834 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
				chr_info("[%s] charging_current_limit:%d, temp_charging_current_limit:%d\n",
						__func__, pdata->charging_current_limit, pdata->temp_charging_current_limit);
				if (pdata->temp_charging_current_limit < pdata->charging_current_limit) {
					pdata->charging_current_limit = pdata->temp_charging_current_limit;
				//	pdata->input_current_limit = pdata->temp_charging_current_limit;
				}
#endif /* CONFIG_OEM_TINNO_CHARGER */
/* TN End modified by jirui.li/860702 20240904 CR/EKLAMU-834 */
		}
	}

/* TN Begin modified by xinjun.lu/860715 20240924 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	if (!IS_ERR_OR_NULL(info->current_alg) && info->current_alg->alg_id == PE5_ID) {
		pdata->charging_current_limit = ((info->pe50.target_fcc < 0) ? 0 : info->pe50.target_fcc);
		info->pe50.target_usb = pdata->input_current_limit;
	}
#endif

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	if (info->pe50.pres_chrg_step == STEP_FULL_PE50) {
		pdata->charging_current_limit = 0;
	}
#endif

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER) && IS_ENABLED(CONFIG_FACTORY_BUILD)
	if (uisoc >= info->factory_charging_limit_soc) {
		info->start_factory_discharging = true;
	} else if (uisoc <= info->factory_charging_limit_soc - FACTORY_AGAING_SOC_GAP) {
		info->start_factory_discharging = false;
	}

	if (info->start_factory_discharging) {
		pdata->input_current_limit = 0;
		pdata->charging_current_limit = 0;
	}
	chr_err("%s:start_factory_discharging=%d\n", __func__, info->start_factory_discharging);
#endif
/* TN End modified by xinjun.lu/860715 20240924 CR/EKLAMU-202 */

	sc_select_charging_current(info, pdata);

/* TN Begin modified by xinjun.lu/860715 20240719 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	if (info->disable_thermal_current_limit) {
		pdata->thermal_charging_current_limit = -1;
		pdata->thermal_input_current_limit = -1;
		pdata2->thermal_charging_current_limit = -1;
		pdata2->thermal_input_current_limit = -1;
		g_thermal_charging_current_limit = -1;
	}
#endif
/* TN End modified by xinjun.lu/860715 20240719 CR/EKLAMU-202 */

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <=
			pdata->charging_current_limit) {
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
			info->setting.charging_current_limit1 =
					pdata->thermal_charging_current_limit;
		}
		pdata->thermal_throttle_record = true;
	} else
		info->setting.charging_current_limit1 = info->sc.sc_ibat;
/* TN Begin modified by xinjun.lu/860715 20241016 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	info->setting.charging_current_limit1 = pdata->charging_current_limit;
#endif
/* TN End modified by xinjun.lu/860715 20241016 CR/EKLAMU-202 */

	if (pdata->thermal_input_current_limit != -1) {
		if (pdata->thermal_input_current_limit <=
			pdata->input_current_limit) {
			pdata->input_current_limit =
					pdata->thermal_input_current_limit;
			info->setting.input_current_limit1 =
					pdata->input_current_limit;
		}
		pdata->thermal_throttle_record = true;
	} else
		info->setting.input_current_limit1 = -1;
/* TN Begin modified by xinjun.lu/860715 20241016 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	info->setting.input_current_limit1 = pdata->input_current_limit;
#endif
/* TN End modified by xinjun.lu/860715 20241016 CR/EKLAMU-202 */

	/* only in pdtest mode */
	if (pdata->usb_input_current_limit != -1) {
		if (pdata->usb_input_current_limit < 100000 &&
		adapter_dev_get_property(info->adapter_dev[PD],
		PD_SRC_PDO_SUPPORT_USB_SUSPEND)) {
			info->en_power_path = false;
		} else if (pdata->usb_input_current_limit >= 100000 &&
			pdata->pd_input_current_limit >= 100000)
			info->en_power_path = true;
		if (pdata->usb_input_current_limit <=
			pdata->input_current_limit) {
			pdata->input_current_limit =
				pdata->usb_input_current_limit;
			info->setting.input_current_limit1 =
				pdata->input_current_limit;
		}
	} else {
		info->setting.input_current_limit1 =
		info->setting.input_current_limit1 == -1?
		-1:info->setting.input_current_limit1;
		info->en_power_path =
		pdata->pd_input_current_limit >= 100000;
	}
	// for pdtest: first run
	if (pdata->pd_input_current_limit != -1) {
		if (pdata->pd_input_current_limit <=
			pdata->input_current_limit) {
			pdata->input_current_limit =
					pdata->pd_input_current_limit;
			info->setting.input_current_limit1 =
					pdata->input_current_limit;
			info->en_power_path =
					pdata->pd_input_current_limit >= 100000;
		}
	} else {
		info->setting.input_current_limit1 =
		info->setting.input_current_limit1 == -1?
		-1:info->setting.input_current_limit1;
	}

	if (info->en_cts_mode)
		chr_err("pdtest: %d, pd: %d, usb: %d, ret: %d\n",
		info->en_cts_mode, pdata->pd_input_current_limit,
		pdata->usb_input_current_limit, pdata->input_current_limit);

	if (pdata2->thermal_charging_current_limit != -1) {
		if (pdata2->thermal_charging_current_limit <=
			pdata2->charging_current_limit) {
			pdata2->charging_current_limit =
					pdata2->thermal_charging_current_limit;
			info->setting.charging_current_limit2 =
					pdata2->charging_current_limit;
		}
	} else
		info->setting.charging_current_limit2 = info->sc.sc_ibat;
/* TN Begin modified by xinjun.lu/860715 20241016 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	info->setting.charging_current_limit2 = pdata2->charging_current_limit;
#endif
/* TN End modified by xinjun.lu/860715 20241016 CR/EKLAMU-202 */

	if (pdata2->thermal_input_current_limit != -1) {
		if (pdata2->thermal_input_current_limit <=
			pdata2->input_current_limit) {
			pdata2->input_current_limit =
					pdata2->thermal_input_current_limit;
			info->setting.input_current_limit2 =
					pdata2->input_current_limit;
		}
	} else
		info->setting.input_current_limit2 = -1;
/* TN Begin modified by xinjun.lu/860715 20241016 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
		info->setting.input_current_limit2 = pdata2->input_current_limit;
#endif
/* TN End modified by xinjun.lu/860715 20241016 CR/EKLAMU-202 */

	if (is_basic == true && pdata->input_current_limit_by_aicl != -1
		&& !info->charger_unlimited
		&& !info->disable_aicl) {
		if (pdata->input_current_limit_by_aicl <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->input_current_limit_by_aicl;
	}
	info->setting.input_current_limit_dvchg1 =
		pdata_dvchg->thermal_input_current_limit;

done:
	ret = charger_dev_get_min_charging_current(info->chg1_dev, &ichg1_min);
	if (ret != -EOPNOTSUPP && pdata->charging_current_limit < ichg1_min) {
		pdata->charging_current_limit = 0;
		/* For TC_018, pleasae don't modify the format */
		chr_err("min_charging_current is too low %d %d\n",
			pdata->charging_current_limit, ichg1_min);
		is_basic = true;
	}

	ret = charger_dev_get_min_input_current(info->chg1_dev, &aicr1_min);
	if (ret != -EOPNOTSUPP && pdata->input_current_limit < aicr1_min) {
		pdata->input_current_limit = 0;
		/* For TC_018, pleasae don't modify the format */
		chr_err("min_input_current is too low %d %d\n",
			pdata->input_current_limit, aicr1_min);
		is_basic = true;
	}
	/* For TC_018, pleasae don't modify the format */
	chr_err("m:%d chg1:%d,%d,%d,%d chg2:%d,%d,%d,%d dvchg1:%d sc:%d %d %d type:%d:%d usb_unlimited:%d usbif:%d usbsm:%d ii:%d,%d aicl:%d atm:%d bm:%d b:%d\n",
		info->config,
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		_uA_to_mA(pdata->input_current_limit),
		_uA_to_mA(pdata->charging_current_limit),
		_uA_to_mA(pdata2->thermal_input_current_limit),
		_uA_to_mA(pdata2->thermal_charging_current_limit),
		_uA_to_mA(pdata2->input_current_limit),
		_uA_to_mA(pdata2->charging_current_limit),
		_uA_to_mA(pdata_dvchg->thermal_input_current_limit),
		info->sc.pre_ibat,
		info->sc.sc_ibat,
		info->sc.solution,
		info->chr_type, info->ta_status[info->select_adapter_idx],
		info->usb_unlimited,
		IS_ENABLED(CONFIG_USBIF_COMPLIANCE), info->usb_state,
		_uA_to_mA(pdata->usb_input_current_limit),
		_uA_to_mA(pdata->pd_input_current_limit),
		pdata->input_current_limit_by_aicl, info->atm_enabled,
		info->bootmode, is_basic);

	return is_basic;
}

static int do_algorithm(struct mtk_charger *info)
{
	struct chg_alg_device *alg;
	struct charger_data *pdata;
	struct chg_alg_notify notify;
	bool is_basic = true;
	bool chg_done = false;
	bool cs_chg_done = false;
	int i;
	int ret, ret2, ret3;
	int val = 0;
	int lst_rnd_alg_idx = info->lst_rnd_alg_idx;
	int vbat = 0, vbat_cs = 0, ibat_cs = 0;
	int cs_ir_cmp = 0;
/* TN Begin modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	int uisoc = 0;
#endif
/* TN End modified by xinjun.lu/860715 20241011 CR/EKLAMU-202 */

	pdata = &info->chg_data[CHG1_SETTING];
/*TN Begin modified by hao.jia/809321 20240909 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
	if (turbo_charger_active == true) {
		if (info->pres_chrg_step == STEP_FULL) {
			chg_done = true;
			ffc_batt_full = true;
			ffc_reduce_count = 0;
			chr_info("%s:ffc battery chg_done\n", __func__);
		}
	} else
#endif /* CONFIG_OEM_TURBO_CHARGER */
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	{
		if (info->pe50.pres_chrg_step == STEP_FULL_PE50)
			chg_done = true;
		chr_info("%s:chg_done=%d\n", __func__, chg_done);
	}
#else
	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
#endif
/*TN End modified by hao.jia/809321 20240909 CR/EKLAMU-202 */

#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
	uisoc = get_uisoc(info);
	if (chg_done && !info->is_chg_done) {
		info->charge_full_soc_for_over_temp = uisoc;
	}

	/* step 1:Recharge condition */
	if (info->is_chg_done &&
		((uisoc <= RECHARGE_CAPACITY &&
			info->battery_temp >= BATTERY_TEMP_LOW &&
			info->battery_temp <= BATTERY_TEMP_HIGH)
			|| (uisoc <= info->charge_full_soc_for_over_temp - RECHARGE_OVER_TEMP_GAP &&
			(info->battery_temp < BATTERY_TEMP_LOW ||
			info->battery_temp > BATTERY_TEMP_HIGH)))) {
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		if (turbo_charger_active == true) {
			if (info->pres_chrg_step == STEP_FULL) {
				info->pres_chrg_step = STEP_NORM;
				ffc_batt_full = false;
				chg_done = false;
			}
		} else
#endif /* CONFIG_OEM_TURBO_CHARGER */
		{
			if (info->pe50.pres_chrg_step == STEP_FULL_PE50) {
				info->pe50.pres_chrg_step = STEP_NORM_PE50;
				chg_done = false;
			}
		}
	}

	/* step 2:Plug out recharging condition */
	if (!chg_done && info->is_chg_done) {
#if IS_ENABLED(CONFIG_OEM_TURBO_CHARGER)
		if (info->pres_chrg_step != STEP_FULL)
			chg_done = false;
#endif
		if (info->pe50.pres_chrg_step != STEP_FULL_PE50)
			chg_done = false;
	}

	chr_info("%s:soc=%d chg_done=%d is_chg_done=%d\n", __func__, uisoc, chg_done, info->is_chg_done);

#endif
/* TN End modified by xinjun.lu/860715 20240924 CR/EKLAMU-202 */

	is_basic = select_charging_current_limit(info, &info->setting);

	if (info->cschg1_dev && info->cs_with_gauge
		&& !info->cs_hw_disable) {
		cs_dev_is_charging_done(info->cschg1_dev, &cs_chg_done);
		if (info->is_cs_chg_done != cs_chg_done) {
			if (cs_chg_done) {
				cs_dev_do_event(info->cschg1_dev, EVENT_FULL, 0);
				chr_err("%s cs side battery full\n", __func__);
			} else {
				cs_dev_do_event(info->cschg1_dev, EVENT_RECHARGE, 0);
				chr_err("%s cs battery recharge\n", __func__);
			}
		}
	if (info->is_chg_done != chg_done) {
		if (chg_done) {
			charger_dev_do_event(info->chg1_dev, EVENT_FULL, 0);
			info->polling_interval = CHARGING_FULL_INTERVAL;
				chr_err("%s main side battery full\n", __func__);
			} else {
				charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
				info->polling_interval = CHARGING_INTERVAL;
				chr_err("%s main side battery recharge\n", __func__);
			}
		}
		if (cs_chg_done && chg_done) {
			info->dual_chg_stat = BOTH_EOC;
			chr_err("%s: close curr selc\n", __func__);
			charger_cs_status_control(info->cschg1_dev, 0);
			info->cs_hw_disable = true;
			chr_err("%s: dual_chg_stat = %d\n", __func__, info->dual_chg_stat);
		} else {
			info->dual_chg_stat = STILL_CHG;
			chr_err("%s: dual_chg_stat = %d\n", __func__, info->dual_chg_stat);
		}
	} else if (info->is_chg_done != chg_done) {
		if (chg_done) {
			// add check cs voltage
			charger_dev_do_event(info->chg1_dev, EVENT_FULL, 0);
			info->polling_interval = CHARGING_FULL_INTERVAL;
			charger_cs_enable_lowpower(info->cschg1_dev, 1);
			chr_err("%s battery full\n", __func__);
		} else {
			charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
			info->polling_interval = CHARGING_INTERVAL;
			charger_cs_enable_lowpower(info->cschg1_dev, 0);
			chr_err("%s battery recharge\n", __func__);
		}
	}

	/* CS */
	if (info->cschg1_dev && !info->cs_hw_disable) {
		ret = charger_dev_set_constant_voltage(info->cschg1_dev, V_CS_BATTERY_CV);
		if (ret < 0)
			chr_err("%s: failed to set cs1 cv to: %d mV.\n", __func__, V_CS_BATTERY_CV);
		/* get battery info */
		vbat = get_battery_voltage(info);
		get_cs_side_battery_voltage(info, &vbat_cs);
		ret = get_cs_side_battery_current(info, &ibat_cs);

		if (ret == FROM_CS_ADC) // sc adc
			cs_ir_cmp = 25 * ibat_cs;	// 25mohm is sc side's measurement resistance.
		else if (ret == FROM_CHG_IC) // chg ic
			cs_ir_cmp = 0 * ibat_cs;
		if (vbat_cs - vbat > V_BATT_EXTRA_DIFF) {
			if (vbat_cs >= V_CS_BATTERY_CV + cs_ir_cmp) {
				if (ibat_cs > CS_CC_MIN && info->cs_cc_now - 100 > CS_CC_MIN) {
					info->cs_cc_now -= 100;
					charger_dev_set_charging_current(info->cschg1_dev, info->cs_cc_now);
				}
			}
			chr_err("cs_ir_cmp:%d, cs_cc_now:%d\n", cs_ir_cmp, info->cs_cc_now);
		} else
			info->cs_cc_now = AC_CS_NORMAL_CC;
		chr_err("cs_ir_cmp:%d, cs_cc_now:%d\n", cs_ir_cmp, info->cs_cc_now);
		charger_dev_dump_registers(info->cschg1_dev);
		// charger_cs_parallel_mode_setting(info->cschg1_dev, info->cs_para_mode);
		ret = cs_dev_check_cs_temp(info->cschg1_dev);
	}

	chr_err("%s is_basic:%d\n", __func__, is_basic);
	if (is_basic != true) {
		is_basic = true;
		for (i = 0; i < MAX_ALG_NO; i++) {
			alg = info->alg[i];
			if (alg == NULL)
				continue;

			if (info->enable_fast_charging_indicator &&
			    ((alg->alg_id & info->fast_charging_indicator) == 0))
				continue;

			if (!info->enable_hv_charging ||
			    pdata->charging_current_limit == 0 ||
			    pdata->input_current_limit == 0) {
				chg_alg_get_prop(alg, ALG_MAX_VBUS, &val);
				if (val > 5000)
					chg_alg_stop_algo(alg);
				chr_err("%s: alg:%s alg_vbus:%d\n", __func__,
					dev_name(&alg->dev), val);
				continue;
			}

			if (info->alg_new_arbitration && info->alg_unchangeable &&
				(lst_rnd_alg_idx > -1)) {
				if (lst_rnd_alg_idx != i)
					continue;
			}

			if (chg_done != info->is_chg_done) {
				if (chg_done) {
					notify.evt = EVT_FULL;
					notify.value = 0;
				} else {
					notify.evt = EVT_RECHARGE;
					notify.value = 0;
				}
				chg_alg_notifier_call(alg, &notify);
				chr_err("%s notify:%d\n", __func__, notify.evt);
			}

			chg_alg_set_current_limit(alg, &info->setting);
			ret = chg_alg_is_algo_ready(alg);

			chr_err("%s %s ret:%s, %d\n", __func__,
				dev_name(&alg->dev),
				chg_alg_state_to_str(ret), ret);

			if (ret == (int) ALG_INIT_FAIL || ret == (int) ALG_TA_NOT_SUPPORT) {
				/* try next algorithm */
				continue;
			} else if (ret == (int) ALG_WAIVER) {
				if (info->alg_new_arbitration)
					continue; /* try next algorithm */
				else {
					is_basic = true;
					break;
				}
			} else if (ret == (int) ALG_TA_CHECKING || ret == (int) ALG_DONE ||
						ret == (int) ALG_NOT_READY) {
				/* wait checking , use basic first */
				is_basic = true;
				if (info->alg_new_arbitration && !info->alg_unchangeable &&
					(lst_rnd_alg_idx > -1)) {
					if (lst_rnd_alg_idx != i && lst_rnd_alg_idx < MAX_ALG_NO)
						chg_alg_stop_algo(info->alg[lst_rnd_alg_idx]);
				}
				break;
			} else if (ret == (int) ALG_READY || ret == (int) ALG_RUNNING) {
				is_basic = false;
				if (info->alg_new_arbitration && !info->alg_unchangeable &&
					(lst_rnd_alg_idx > -1)) {
					if (lst_rnd_alg_idx != i && lst_rnd_alg_idx < MAX_ALG_NO)
						chg_alg_stop_algo(info->alg[lst_rnd_alg_idx]);
				}
				chg_alg_start_algo(alg);
				chr_err("%s: %d, %d.\n", __func__, ret, info->cs_hw_disable);
				if (ret == (int) ALG_RUNNING && info->cschg1_dev && !info->cs_hw_disable) {
					ret = charger_dev_set_charging_current(info->cschg1_dev, info->cs_cc_now);
					if (ret < 0)
						chr_err("%s: failed to set cs1 cc to: 1500mA.\n", __func__);
				}
				info->lst_rnd_alg_idx = i;
				break;
			} else {
				chr_err("algorithm ret is error");
				is_basic = true;
			}
		}
/* TN Begin modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
		if (!IS_ERR_OR_NULL(alg) && (alg->alg_id == PE5_ID || alg->alg_id == PDC_ID)) {
			info->current_alg = alg;
		} else {
			info->current_alg = NULL;
		}
#endif
#if IS_ENABLED(CONFIG_OEM_TINNO_CHARGER)
		if (!IS_ERR_OR_NULL(alg) && alg->alg_id == PDC_ID && chg_alg_is_algo_running(alg)) {
			info->ext_chr_type = POWER_SUPPLY_TYPE_USB_PDC;
		}
		chr_err("%s: info->ext_chr_type = %d.\n", __func__, info->ext_chr_type);
#endif

/* TN End modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */
	} else {
		if (info->enable_hv_charging != true ||
		    pdata->charging_current_limit == 0 ||
		    pdata->input_current_limit == 0) {
			for (i = 0; i < MAX_ALG_NO; i++) {
				alg = info->alg[i];
				if (alg == NULL)
					continue;

				chg_alg_get_prop(alg, ALG_MAX_VBUS, &val);
				if (val > 5000 && chg_alg_is_algo_running(alg))
					chg_alg_stop_algo(alg);

				chr_err("%s: Stop hv charging. en_hv:%d alg:%s alg_vbus:%d\n",
					__func__, info->enable_hv_charging,
					dev_name(&alg->dev), val);
			}
		}
/* TN Begin modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
		info->current_alg = NULL;
#endif
/* TN End modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */

	}

/* TN Begin modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */
#if IS_ENABLED(CONFIG_PE50_FFC_SUPPORT)
	if (!IS_ERR_OR_NULL(info->current_alg))
		chr_info("%s: Used current alg: %s \n",
						__func__, dev_name(&info->current_alg->dev));
	else
		chr_err("%s: Used current alg is NULL\n",
						__func__);
#endif
/* TN End modified by xinjun.lu/860715 20240821 CR/EKLAMU-202 */

	info->is_chg_done = chg_done;

	if (is_basic == true) {
		charger_dev_set_input_current(info->chg1_dev,
			pdata->input_current_limit);
		charger_dev_set_charging_current(info->chg1_dev,
			pdata->charging_current_limit);

		if (info->en_cts_mode) {
			// close power path
			if (info->power_path_en && !info->en_power_path) {
				mtk_charger_force_disable_power_path(info, true);
				info->power_path_en = false;
			// open power path
			} else if (!info->power_path_en && info->en_power_path) {
				mtk_charger_force_disable_power_path(info, false);
				info->power_path_en = true;
			}
		}
		info->lst_rnd_alg_idx = -1;
		/* CS */
		if (info->cschg1_dev && !info->cs_hw_disable) {
			ret = charger_dev_set_charging_current(info->cschg1_dev, info->cs_cc_now);
			if (ret < 0)
				chr_err("%s: failed to set cs1 cc to: %d mA.\n", __func__, info->cs_cc_now);
		}
		chr_debug("%s:old_cv=%d,cv=%d, vbat_mon_en=%d\n",
			__func__,
			info->old_cv,
			info->setting.cv,
			info->setting.vbat_mon_en);
		if (info->old_cv == 0 || (info->old_cv != info->setting.cv)
		    || info->setting.vbat_mon_en == 0) {
			charger_dev_enable_6pin_battery_charging(
				info->chg1_dev, false);
			charger_dev_set_constant_voltage(info->chg1_dev,
				info->setting.cv);
			if (info->setting.vbat_mon_en && info->stop_6pin_re_en != 1)
				charger_dev_enable_6pin_battery_charging(
					info->chg1_dev, true);
			info->old_cv = info->setting.cv;
		} else {
			if (info->setting.vbat_mon_en && info->stop_6pin_re_en != 1) {
				info->stop_6pin_re_en = 1;
				charger_dev_enable_6pin_battery_charging(
					info->chg1_dev, true);
			}
		}
	}

	if (pdata->input_current_limit == 0 ||
	    pdata->charging_current_limit == 0)
		charger_dev_enable(info->chg1_dev, false);
	else {
		alg = get_chg_alg_by_name("pe5p");
		ret = chg_alg_is_algo_ready(alg);
		alg = get_chg_alg_by_name("pe5");
		ret2 = chg_alg_is_algo_ready(alg);
		alg = get_chg_alg_by_name("hvbp");
		ret3 = chg_alg_is_algo_ready(alg);
		if (!(ret == (int) ALG_READY || ret == (int) ALG_RUNNING) &&
			!(ret2 == (int) ALG_READY || ret2 == (int) ALG_RUNNING) &&
			!(ret3 == (int) ALG_READY || ret3 == (int) ALG_RUNNING))
			charger_dev_enable(info->chg1_dev, true);
	}

	if (info->chg1_dev != NULL) {
		charger_dev_dump_registers(info->chg1_dev);
		charger_dev_kick_wdt(info->chg1_dev);
	}

	if (info->chg2_dev != NULL) {
		charger_dev_dump_registers(info->chg2_dev);
		charger_dev_kick_wdt(info->chg2_dev);
	}

	if (info->bkbstchg_dev != NULL)
		charger_dev_dump_registers(info->bkbstchg_dev);

	return 0;
}

static int enable_charging(struct mtk_charger *info,
						bool en)
{
	int i;
	struct chg_alg_device *alg;


	chr_err("%s %d\n", __func__, en);

	if (en == false) {
		for (i = 0; i < MAX_ALG_NO; i++) {
			alg = info->alg[i];
			if (alg == NULL)
				continue;
			chg_alg_stop_algo(alg);
		}
		charger_dev_enable(info->chg1_dev, false);
		charger_dev_do_event(info->chg1_dev, EVENT_DISCHARGE, 0);
	} else {
		charger_dev_enable(info->chg1_dev, true);
		charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
	}

	return 0;
}

static int charger_dev_event(struct notifier_block *nb, unsigned long event,
				void *v)
{
	struct chg_alg_device *alg;
	struct chg_alg_notify notify;
	struct mtk_charger *info =
			container_of(nb, struct mtk_charger, chg1_nb);
	struct chgdev_notify *data = v;
	int ret = 0, vbat_min = 0, vbat_max = 0, vbat_cs = 0;
	int i;

	chr_err("%s %lu\n", __func__, event);

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		info->stop_6pin_re_en = 1;
		notify.evt = EVT_FULL;
		notify.value = 0;
		for (i = 0; i < 10; i++) {
			alg = info->alg[i];
			chg_alg_notifier_call(alg, &notify);
		}

		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		if (info->cschg1_dev && info->dual_chg_stat == BOTH_EOC) {	// mt6375_2p version not in mt6379_2p
			ret = charger_dev_get_adc(info->chg1_dev,
				ADC_CHANNEL_VBAT, &vbat_min, &vbat_max);
			if (ret < 0)
				chr_err("%s: failed to get vbat, recharge mode\n", __func__);
			else {
				vbat_min = vbat_min / 1000;
				ret = charger_dev_get_vbat(info->cschg1_dev, &vbat_cs);
				if ( abs( vbat_min - vbat_cs ) <= V_BATT_EXTRA_DIFF
				&& info->cs_hw_disable) {
					chr_err("%s: opening cs\n", __func__);
					charger_cs_status_control(info->cschg1_dev, 1);
					info->cs_hw_disable = false;
					info->dual_chg_stat = STILL_CHG;
				}
			}
		}
		pr_info("%s: recharge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT:
		info->safety_timeout = true;
		pr_info("%s: safety timer timeout\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		info->vbusov_stat = data->vbusov_stat;
		pr_info("%s: vbus ovp = %d\n", __func__, info->vbusov_stat);
		break;
	case CHARGER_DEV_NOTIFY_BATPRO_DONE:
		info->batpro_done = true;
		info->setting.vbat_mon_en = 0;
		notify.evt = EVT_BATPRO_DONE;
		notify.value = 0;
		for (i = 0; i < 10; i++) {
			alg = info->alg[i];
			chg_alg_notifier_call(alg, &notify);
		}
		pr_info("%s: batpro_done = %d\n", __func__, info->batpro_done);
		break;
	case CHARGER_DEV_NOTIFY_DPDM_OVP:
		info->dpdmov_stat = data->dpdmov_stat;
		pr_info("%s: DPDM ovp = %d\n", __func__, info->dpdmov_stat);
		break;
	default:
		return NOTIFY_DONE;
	}

	if (info->chg1_dev->is_polling_mode == false)
		_wake_up_charger(info);

	return NOTIFY_DONE;
}

static int to_alg_notify_evt(unsigned long evt)
{
	switch (evt) {
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		return EVT_VBUSOVP;
	case CHARGER_DEV_NOTIFY_IBUSOCP:
		return EVT_IBUSOCP;
	case CHARGER_DEV_NOTIFY_IBUSUCP_FALL:
		return EVT_IBUSUCP_FALL;
	case CHARGER_DEV_NOTIFY_BAT_OVP:
		return EVT_VBATOVP;
	case CHARGER_DEV_NOTIFY_IBATOCP:
		return EVT_IBATOCP;
	case CHARGER_DEV_NOTIFY_VBATOVP_ALARM:
		return EVT_VBATOVP_ALARM;
	case CHARGER_DEV_NOTIFY_VBUSOVP_ALARM:
		return EVT_VBUSOVP_ALARM;
	case CHARGER_DEV_NOTIFY_VOUTOVP:
		return EVT_VOUTOVP;
	case CHARGER_DEV_NOTIFY_VDROVP:
		return EVT_VDROVP;
	default:
		return -EINVAL;
	}
}

static int dvchg1_dev_event(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct mtk_charger *info =
		container_of(nb, struct mtk_charger, dvchg1_nb);
	int alg_evt = to_alg_notify_evt(event);

	chr_info("%s %ld", __func__, event);
	if (alg_evt < 0)
		return NOTIFY_DONE;
	mtk_chg_alg_notify_call(info, alg_evt, 0);
	return NOTIFY_OK;
}

static int dvchg2_dev_event(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct mtk_charger *info =
		container_of(nb, struct mtk_charger, dvchg2_nb);
	int alg_evt = to_alg_notify_evt(event);

	chr_info("%s %ld", __func__, event);
	if (alg_evt < 0)
		return NOTIFY_DONE;
	mtk_chg_alg_notify_call(info, alg_evt, 0);
	return NOTIFY_OK;
}

static int hvdvchg1_dev_event(struct notifier_block *nb, unsigned long event,
			      void *data)
{
	struct mtk_charger *info =
		container_of(nb, struct mtk_charger, hvdvchg1_nb);
	int alg_evt = to_alg_notify_evt(event);

	chr_info("%s %ld", __func__, event);
	if (alg_evt < 0)
		return NOTIFY_DONE;
	mtk_chg_alg_notify_call(info, alg_evt, 0);
	return NOTIFY_OK;
}

static int hvdvchg2_dev_event(struct notifier_block *nb, unsigned long event,
			      void *data)
{
	struct mtk_charger *info =
		container_of(nb, struct mtk_charger, hvdvchg2_nb);
	int alg_evt = to_alg_notify_evt(event);

	chr_info("%s %ld", __func__, event);
	if (alg_evt < 0)
		return NOTIFY_DONE;
	mtk_chg_alg_notify_call(info, alg_evt, 0);
	return NOTIFY_OK;
}

int mtk_basic_charger_init(struct mtk_charger *info)
{

	info->algo.do_algorithm = do_algorithm;
	info->algo.enable_charging = enable_charging;
	info->algo.do_event = charger_dev_event;
	info->algo.do_dvchg1_event = dvchg1_dev_event;
	info->algo.do_dvchg2_event = dvchg2_dev_event;
	info->algo.do_hvdvchg1_event = hvdvchg1_dev_event;
	info->algo.do_hvdvchg2_event = hvdvchg2_dev_event;
	info->lst_rnd_alg_idx = -1;
	//info->change_current_setting = mtk_basic_charging_current;
	return 0;
}
