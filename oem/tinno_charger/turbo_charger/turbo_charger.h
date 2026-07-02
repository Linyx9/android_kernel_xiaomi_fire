#ifndef __MTK_TURBO_CHARGER_H
#define __MTK_TURBO_CHARGER_H

#include "../../drivers/power/supply/mtk_charger_algorithm_class.h"
#include <linux/power_supply.h>

#define TURBO_CHARGER_ERR_LEVEL				1
#define TURBO_CHARGER_INFO_LEVEL			2
#define TURBO_CHARGER_DBG_LEVEL				3
#define TURBO_CHARGER_ITA_GAP_WINDOW_SIZE		50
#define PRECISION_ENHANCE				5

#define DISABLE_VBAT_THRESHOLD				-1

#define CC_CURR_DEBOUNCE				20000
#define HEARTBEAT_NEXT_STATE_MS				1000
#define THERMAL_DELAY_MS				300
#define THERMAL_DELAY_LESS_MS				100
#define HEARTBEAT_SHORT_DELAY_MS			100
#define HEARTBEAT_SW_DELAY_MS				3000
#define STEP_FIRST					0

#define MIN_TEMP_C					-20
#define HYSTERESIS_DEGC					2

#define DEFAULT_TURBO_VOLT_STEPS			20000
#define DEFAULT_TURBO_CURR_STEPS			40000
#define DEFAULT_TURBO_VOLT_MAX				11000000
#define DEFAULT_TURBO_CURR_MAX				3000000
#define DEFAULT_BATT_CURR_BOOST				300000
#define DEFAULT_BATT_OVP_LIMIT				4550000
#define DEFAULT_TYPEC_MIDDLE_CURRENT			1500000
#define DEFAULT_PL_CHRG_VBATT_MIN			3600000
#define DEFAULT_STEP_FIRST_CURR_COMP			0
#define DEFAULT_TEMP_ZONES_NUM				6
#define DEFAULT_THERMAL_MIN_LEVEL			1500000
#define DEFAULT_CHARGING_CURR_MIN			2000000
#define DEFAULT_SW_CURR_LIMITED				100000
#define DEFAULT_TURBO_VOLT_COMP				200000
#define THERMAL_NOT_LIMIT				-1
#define THERMAL_TUNNING_CURR				40000


#define CV_TAPPER_COUNT					3
#define CV_DELTA_VOLT					100000

#if IS_ENABLED(CONFIG_FACTORY_BUILD)
#define CP_CHRG_SOC_LIMIT				70
#else
#define CP_CHRG_SOC_LIMIT				100
#endif

#define RETRY_TURBO_CHARGER				3000 // ms
#define NORMAL_TURBO_CHARGER				200  // ms

#define DEMO_SOC_LIMIT          			70

#define TURBO_RET_HISTORY_SIZE				5

#define BATT_MAX_CHG_VOLT				4450
#define BATT_FAST_CHG_CURR				10000
#define BUS_OVP_THRESHOLD				9500

#define BAT_VOLT_LOOP_LMT				BATT_MAX_CHG_VOLT
#define BAT_CURR_LOOP_LMT				BATT_FAST_CHG_CURR
#define BUS_VOLT_LOOP_LMT				BUS_OVP_THRESHOLD
/*TN Begin modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
#define MAX_INC_PULSE					300
#define CP_BUS_UVP_THRESHOLD				8000000
#define CURRENT_2000_MA					2000000
/*TN End modify vbus ovp by rongxing.li/860682 20231208 CR/EKFOGO4G-8986*/
#define BATTERY_TEMP_LOW_TURBO		15
#define BATTERY_TEMP_HIGH_TURBO		45

extern int turbo_charger_get_log_level(void);
#define TURBO_CHARGER_DBG(fmt, ...) \
	do { \
		if (turbo_charger_get_log_level() >= TURBO_CHARGER_DBG_LEVEL) \
			pr_info("[TURBO_CHARGER]%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define TURBO_CHARGER_INFO(fmt, ...) \
	do { \
		if (turbo_charger_get_log_level() >= TURBO_CHARGER_INFO_LEVEL) \
			pr_info("[TURBO_CHARGER]%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define TURBO_CHARGER_ERR(fmt, ...) \
	do { \
		if (turbo_charger_get_log_level() >= TURBO_CHARGER_ERR_LEVEL) \
			pr_info("[TURBO_CHARGER]%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

struct turbo_charger_config {
	int	bat_volt_lp_lmt;	/*bat volt loop limit*/
	int	bat_curr_lp_lmt;
	int	bus_volt_lp_lmt;
	int	bus_curr_lp_lmt;

	int	fc2_taper_current;
	int	fc2_steps;

	int	min_adapter_volt_required;
	int	min_adapter_curr_required;

	int	min_vbat_for_cp;

	bool	cp_sec_enable;
	bool	fc2_disable_sw;		/* disable switching charger during turbo charge*/
};

struct sw_device {
	bool	charge_enabled;
	int	ibus_curr;
	int	charger_type;
	bool	usb_online;
};

struct cp_device {
	bool	charge_enabled;
	bool    adc_enabled;

	bool	batt_pres;
	bool	vbus_pres;

	/* alarm/fault status */
	bool	bat_ovp_fault;
	bool	bat_ocp_fault;
	bool	bus_ovp_fault;
	bool	bus_ocp_fault;

	bool	bat_ovp_alarm;
	bool	bat_ocp_alarm;
	bool	bus_ovp_alarm;
	bool	bus_ocp_alarm;

	bool	bat_ucp_alarm;

	bool	bat_therm_alarm;
	bool	bus_therm_alarm;
	bool	die_therm_alarm;

	bool	bat_therm_fault;
	bool	bus_therm_fault;
	bool	die_therm_fault;

	bool	therm_shutdown_flag;
	bool	therm_shutdown_stat;

	bool	vbat_reg;
	bool	ibat_reg;

	int	vout_volt;
	int	vbat_volt;
	int	vbus_volt;
	int	ibat_curr;
	int	ibus_curr;

	bool	vbus_error_low;
	bool	vbus_error_high;

	int	bat_temp;
	int	bus_temp;
	int	die_temp;
};

struct batt_device {
	int	soc;
	int	temp;
};

struct turbo_charger_step_power {
	int	chrg_step_curr;
	int	chrg_step_volt;
};

struct turbo_charger_step_zone {
	int	temp_c;
	struct	turbo_charger_step_power chrg_step_power[3];
};

struct turbo_charger_step_info {
	int	temp_c;
	int	chrg_step_cv_tapper_curr;
	int	chrg_step_cc_curr;
	int	chrg_step_cv_volt;
	int	pres_chrg_step;
	bool	last_step;
};

static const unsigned char *turbo_charger_state_str[] = {
	"TURBO_STATE_DISCONNECT",
	"TURBO_STATE_ENTRY",
	"TURBO_STATE_SW_ENTRY",
	"TURBO_STATE_SW_LOOP",
	"TURBO_STATE_CHRG_PUMP_ENTRY",
	"TURBO_STATE_SINGLE_CP_ENTRY",
	"TURBO_STATE_PPS_TUNNING_CURR",
	"TURBO_STATE_PPS_TUNNING_VOLT",
	"TURBO_STATE_CP_CC_LOOP",
	"TURBO_STATE_CP_CV_LOOP",
	"TURBO_STATE_CP_QUIT",
	"TURBO_STATE_RECOVERY_SW",
	"TURBO_STATE_STOP_CHARGE",
	"TURBO_STATE_COOLING_LOOP",
	"TURBO_STATE_POWER_LIMIT_LOOP",
};

enum turbo_charger_state_t {
	TURBO_STATE_DISCONNECT,
	TURBO_STATE_ENTRY,
	TURBO_STATE_SW_ENTRY,
	TURBO_STATE_SW_LOOP,
	TURBO_STATE_CHRG_PUMP_ENTRY,
	TURBO_STATE_SINGLE_CP_ENTRY,
	TURBO_STATE_PPS_TUNNING_CURR,
	TURBO_STATE_PPS_TUNNING_VOLT,
	TURBO_STATE_CP_CC_LOOP,
	TURBO_STATE_CP_CV_LOOP,
	TURBO_STATE_CP_QUIT,
	TURBO_STATE_RECOVERY_SW,
	TURBO_STATE_STOP_CHARGE,
	TURBO_STATE_COOLING_LOOP,
	TURBO_STATE_POWER_LIMIT_LOOP,
};

enum temp_zone {
	ZONE_FIRST = 0,
	ZONE_HOT = 10,
	ZONE_COLD,
	ZONE_NONE = 0xFF,
};

struct turbo_charger_algo_info {
	struct	device			*dev;

	struct	power_supply		*usb_psy;
	struct	power_supply		*cp_psy;
	struct	power_supply		*batt_psy;
	struct	power_supply		*qc_logic_psy;
	struct	cp_device		cp;
	struct	sw_device		sw;
	struct	batt_device		batt;
	struct	charger_device		*sw_chg;
	struct	charger_device		*cp_chg;

	enum	turbo_charger_state_t	state;
	struct	turbo_charger_step_zone *temp_zones;
	struct				turbo_charger_step_info chrg_step;
	u32				turbo_charger_temp_zones_num;
	u32				turbo_charger_step_nums;
	int				pres_temp_zone;
	bool				sys_therm_cooling;
	bool				batt_therm_cooling;
	bool				batt_thermal_cooling;
	int				batt_therm_cooling_cnt;
	int				system_thermal_level;
	int				turbo_charger_volt_comp;
	u32				batt_curr_boost;
	u32				batt_ovp_limit;
	u32				step_first_current_comp;
	bool				turbo_charger_cc_loop_stage;
	int				pl_chrg_vbatt_min;
	u32				thermal_min_level;
	u32				sw_charging_curr_limited;
	int				turbo_charger_sys_therm_volt;
	int				turbo_charger_sys_therm_curr;

	/* turbo charger */
	int				turbo_charger_request_volt_prev;
	int				turbo_charger_request_volt;
	int				turbo_charger_request_curr_prev;
	int				turbo_charger_request_curr;
	int				turbo_charger_volt_max;
	int				turbo_charger_curr_max;
	bool				turbo_charger_support;
	bool				turbo_charger_active;
	int				turbo_charging_curr_min;
	int				turbo_charger_vbatt_volt_prev;
	int				turbo_charger_ibatt_curr_prev;
	int				turbo_charger_therm_loop_cn;
	int				turbo_charger_volt_steps;
	int				turbo_charger_curr_steps;
	int				typec_middle_current;
	int				turbo_charger_result;
	int				turbo_result_history_idx;
	int				turbo_result_history_history[TURBO_RET_HISTORY_SIZE];

	int				turbo_charger_type;
	struct	notifier_block		nb;
	struct	delayed_work		turbo_charger_work;
	struct	mutex			turbo_charger_lock;
	struct	notifier_block		reboot_notifier;
	int				total_count;
	bool			qc_phy_z350;
	bool			qc_phy_wt6670f;
	bool			cp_chg_done;
};

#endif /* __MTK_TURBO_CHARGER_H */
