#ifndef CX2560X_CHARGER_H
#define CX2560X_CHARGER_H

#include <linux/bits.h>
#include <linux/thermal.h>
#include <linux/iio/consumer.h>

#define R_VBUS_CHARGER_1   330
#define R_VBUS_CHARGER_2   39

#define CX2560X_TEMP_INVALID   1000

#define CX2560X_CHG_DISABLED     0
#define CX2560X_PRECHARGE        1
#define CX2560X_FAST_CHARGE      2
#define CX2560X_TERM_CHARGE      3

#define QC_TYPE_AUTO_CHECK   0
#define QC_TYPE_FORCE_1P0    1
#define QC_TYPE_FORCE_2P0    2
#define QC_TYPE_FORCE_3P0    3
#define QUICK_CHARGE_1P0     4
#define QUICK_CHARGE_2P0     5
#define QUICK_CHARGE_3P0     6

#define CX2560X_USB_NONE       0
#define CX2560X_USB_SDP        1
#define CX2560X_USB_CDP        2
#define CX2560X_USB_DCP        3
#define CX2560X_USB_UNKNOWN    5
#define CX2560X_USB_NSTANDA    6

#define CX2560X_CHG_DISABLE      1
#define CX2560X_STATE_COLD       2
#define CX2560X_STATE_COOL       3
#define CX2560X_STATE_NORMAL     4
#define CX2560X_STATE_WARM       5

/*define register*/
#define CX2560X_CHRG_CTRL_0    0x00
#define CX2560X_CHRG_CTRL_1    0x01
#define CX2560X_CHRG_CTRL_2    0x02
#define CX2560X_CHRG_CTRL_3    0x03
#define CX2560X_CHRG_CTRL_4    0x04
#define CX2560X_CHRG_CTRL_5    0x05
#define CX2560X_CHRG_CTRL_6    0x06
#define CX2560X_CHRG_CTRL_7    0x07
#define CX2560X_CHRG_CTRL_8    0x08
#define CX2560X_CHRG_CTRL_9    0x09
#define CX2560X_CHRG_CTRL_A    0x0a
#define CX2560X_CHRG_CTRL_B    0x0b
#define CX2560X_CHRG_CTRL_C    0x0c
#define CX2560X_CHRG_CTRL_D    0x0d
#define CX2560X_CHRG_CTRL_E    0x0e
#define CX2560X_CHRG_CTRL_F    0x0f
#define CX2560X_CHRG_CTRL_10   0x10
#define CX2560X_CHRG_CTRL_11   0x11



/* charge status flags  */
#define CX2560X_CHRG_EN         BIT(4)
#define CX2560X_HIZ_EN          BIT(7)
#define CX2560X_TERM_EN         BIT(7)
#define CX2560X_VAC_OVP_MASK    GENMASK(7, 6)
#define CX2560X_DPDM_ONGOING    BIT(7)
#define CX2560X_VBUS_GOOD       BIT(7)

#define CX2560X_OTG_EN          BIT(5)
/*lizhihua-20220721-add-for-shipmode*/
#define CX2560X_shipmode_EN          BIT(5)

/* Part ID  */
#define CX2560X_PN_MASK         0x78
#define CX2560X_PN_ID           (BIT(5)| BIT(4)| BIT(3))

/* register reset*/
#define CX2560X_REG_RESET       BIT(7)

/* WDT TIMER SET  */
#define CX2560X_WDT_TIMER_MASK       GENMASK(5, 4)
#define CX2560X_WDT_TIMER_DISABLE    0
#define CX2560X_WDT_TIMER_40S        BIT(4)
#define CX2560X_WDT_TIMER_80S        BIT(5)
#define CX2560X_WDT_TIMER_160S       (BIT(5)| BIT(4))

#define CX2560X_WDT_RST_MASK         BIT(6)

/* boost current set */
#define CX2560X_BOOST_CUR_05A        0
#define CX2560X_BOOST_CUR_1A2        1
#define CX2560X_BOOST_CUR_MASK		0x80


/* safety timer set  */
#define CX2560X_SAFETY_TIMER_MASK       GENMASK(3, 3)
#define CX2560X_SAFETY_TIMER_DISABLE    0
#define CX2560X_SAFETY_TIMER_EN         BIT(3)
#define CX2560X_SAFETY_TIMER_5H         0
#define CX2560X_SAFETY_TIMER_10H        BIT(2)

/* recharge voltage  */
#define CX2560X_VRECHARGE            BIT(0)
#define CX2560X_VRECHRG_STEP_mV      100
#define CX2560X_VRECHRG_OFFSET_mV    100

/* charge status  */
#define CX2560X_VSYS_STAT        BIT(0)
#define CX2560X_THERM_STAT       BIT(1)
#define CX2560X_PG_STAT          BIT(2)
#define CX2560X_VBUS_STAT_OTG    GENMASK(7, 5)

/* charge status*/
#define CX2560X_VBUS_STAT_MASK    GENMASK(7, 5)
#define CX2560X_CHG_STAT_MASK     GENMASK(4, 3)
#define CX2560X_PG_GOOD_MASK      BIT(2)

/* termination current  */
#define CX2560X_TERMCHRG_CUR_MASK           GENMASK(3, 0)
#define CX2560X_TERMCHRG_CURRENT_STEP_MA    60
#define CX2560X_TERMCHRG_I_MIN_MA           60
#define CX2560X_TERMCHRG_I_MAX_MA           960
#define CX2560X_TERMCHRG_I_DEF_MA           180

/* precharge current  */
#define CX2560X_PRECHG_CUR_MASK           GENMASK(7, 4)
#define CX2560X_PRECHG_CURRENT_STEP_MA    34
#define CX2560X_PRECHG_I_MIN_MA           34

/* charge current  */
#define CX2560X_ICHRG_CUR_MASK           GENMASK(5, 0)
#define CX2560X_ICHRG_CUR_SHIFT          	0

#define CX2560X_ICHRG_CURRENT_STEP_MA    60
#define CX2560X_ICHRG_I_MIN_MA           0
#define CX2560X_ICHRG_I_MAX_MA           3780
#define CX2560X_ICHRG_I_DEF_MA           2040

/* charge voltage  */
#define CX2560X_VREG_V_MASK       GENMASK(7, 3)
#define CX2560X_VREG_V_MAX_MV     4624
#define CX2560X_VREG_V_MIN_MV     3856
#define CX2560X_VREG_V_DEF_MV     4208
#define CX2560X_VREG_V_STEP_MV    32

/* iindpm current  */
#define CX2560X_IINDPM_I_MASK      GENMASK(4, 0)
#define CX2560X_IINDPM_I_MIN_MA    100
#define CX2560X_IINDPM_I_MAX_MA    3200
#define CX2560X_IINDPM_STEP_MA     100
#define CX2560X_IINDPM_DEF_MA      2400

/* vindpm voltage  */
#define CX2560X_VINDPM_V_MASK      GENMASK(6, 0)
#define CX2560X_VINDPM_V_MIN_MV    3900
#define CX2560X_VINDPM_V_MAX_MV    12000
#define CX2560X_VINDPM_STEP_MV     100
#define CX2560X_VINDPM_DEF_MV      3600
#define CX2560X_VINDPM_OS_MASK     GENMASK(1, 0)

/* DP DM SEL  */
#define CX2560X_DP_VSEL_MASK    GENMASK(6, 4)
#define CX2560X_DM_VSEL_MASK    GENMASK(2, 0)
#define CX2560X_DPM_VSEL_MASK   0x77
#define CX2560X_DP_HIZ          0
#define CX2560X_DP_0V           BIT(4)
#define CX2560X_DP_0V6          BIT(5)
#define CX2560X_DP_3V3          (BIT(4) | BIT(5) | BIT(6))
#define CX2560X_DM_HIZ          0
#define CX2560X_DM_0V           BIT(0)
#define CX2560X_DM_0V6          BIT(1)
#define CX2560X_DM_3V3          (BIT(0) | BIT(1) | BIT(2))
#define CX2560X_FORCE_DPDM_MASK BIT(7)
#define CX2560X_FORCE_DPDM        BIT(7)
/* vreg fine tune */
#define CX2560X_VREG_FINE_TUNE_MASK         GENMASK(7, 6)
#define CX2560X_VREG_FINE_TUNE_DISABLE      0
#define CX2560X_VREG_FINE_TUNE_PLUS_8MV     BIT(6)
#define CX2560X_VREG_FINE_TUNE_MINUS_8MV    BIT(7)
#define CX2560X_VREG_FINE_TUNE_MINUS_16MV   (BIT(7) | BIT(6))

/* recharge voltage */
#define CX2560X_RECHARGE_VOLTAGE_MASK    BIT(0)
#define CX2560X_RECHARGE_VOLTAGE_100MV   0
#define CX2560X_RECHARGE_VOLTAGE_200MV   BIT(0)

/* vindpm track set voltage */
#define CX2560X_VINDPM_TRACK_SET_MASK      GENMASK(1, 0)
#define CX2560X_VINDPM_TRACK_SET_DISABLE   0
#define CX2560X_VINDPM_TRACK_SET_200MV     BIT(0)
#define CX2560X_VINDPM_TRACK_SET_250MV     BIT(1)
#define CX2560X_VINDPM_TRACK_SET_300MV     (BIT(1) | BIT(0))

#define CX2560X_DPM_INT_MASK      GENMASK(1, 0)
#define CX2560X_IINDPM_INT_MASK   BIT(0)
#define CX2560X_VINDPM_INT_MASK   BIT(1)

/* PUMPX SET  */
#define CX2560X_EN_PUMPX    BIT(7)
#define CX2560X_PUMPX_UP    BIT(6)
#define CX2560X_PUMPX_DN    BIT(5)


struct cx2560x_state {
	u32 vbus_stat;
	u32 chrg_stat;
	u32 pg_stat;
	u32 therm_stat;
	u32 vsys_stat;
	u32 wdt_fault;
	u32 boost_fault;
	u32 chrg_fault;
	u32 bat_fault;
	u32 ntc_fault;
	bool online;
	bool vbus_gd;
};

struct cx2560x_init_data {
	u32 ichg;    /* charge current        */
	u32 ilim;    /* input current        */
	u32 vreg;    /* regulation voltage        */
	u32 iterm;    /* termination current        */
	u32 iprechg;    /* precharge current        */
	u32 vlim;    /* minimum system voltage limit */
	u32 max_ichg;
	u32 max_vreg;
};

struct cx2560x_device {
	int temp;
	int ibat;
	int en_gpio;
	int qc_type;
	int qc_force;
	u32 reg_addr;
	int irq_gpio;
	int temp_state;
	int chg_volt;
	int chg_curr;
	int otg_mode;
	int chg_en;
	int vtemp;
	int voltage_max;
	int current_max;
	int chg_type;
	int psy_usb_type;
	int batt_vol;
	int batt_curr;
	struct mutex i2c_rw_lock;
	bool charge_enabled;/* Register bit status */
	struct device *dev;
	struct iio_channel *vbus;
	struct i2c_client *client;
	struct cx2560x_init_data init_data;
	struct cx2560x_state state;
	struct delayed_work irq_work;
	struct delayed_work psy_work;
	struct delayed_work hvdcp_work;
	struct delayed_work charge_work;
	struct delayed_work vindpm_work;
	struct delayed_work charge_usb_detect_work;
	struct power_supply *ac_psy;
	struct power_supply *usb_psy;
	struct power_supply *charger_psy;
	struct power_supply *battery_psy;
	struct power_supply_desc *chg_desc;
	struct usb_phy *usb2_phy;
	struct regulator_dev *otg_rdev;
	struct charger_device *chg_dev;
	struct thermal_zone_device *tz_ap;
	struct thermal_zone_device *tz_rf;
	struct thermal_zone_device *tz_chg;
	struct thermal_zone_device *tz_batt;//lizhihua-20220122-add
/*TN Begin modified by mingming.zhang/20230913*/
	int force_detect_count;
	struct delayed_work force_detect_dwork;
/*TN end modified by mingming.zhang/20230913*/
	struct wakeup_source *charger_wakelock;
};

#endif
