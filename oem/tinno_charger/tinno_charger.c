#include <linux/platform_device.h>
#include <linux/of.h>
#include "tinno_charger.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include "charger_class.h"
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
#include <mt-plat/v1/charger_class.h>
#else
#include <mt-plat/charger_class.h>
#endif /* LINUX_VERSION_CODE */

struct switch_device {
	bool	is_enabled;
	bool	charger_exist;
	int	charger_type;
	int	charger_voltage;
	int	prop_status;
	int	boot_mode;
};

struct charger_pump_device {
	bool	is_enabled;
};

struct battery_device {
	int		temp;
	int		vbat;
	int		ibat;
	int		vbus;
	int		ibus;
	int		ui_soc;
	int		soc;

	int		batt_exist;
	int		batt_full;
	int		chging_on;
	int		in_rechging;
	int		charging_state;
};

struct tinno_charger_info {
	struct device			*dev;

	/* work */
	struct delayed_work		charger_monitor_work;

	/* charger device */
	struct charger_device		*sw_chg;
	struct charger_device		*cp_chg;

	struct switch_device		sw_dev;
	struct charger_pump_device	cp_dev;
	struct battery_device		batt_dev;

	/* power supply device */
	struct power_supply		*batt_psy;
	struct power_supply		*sw_psy;
	struct power_supply		*cp_psy;
};

static int log_level = TINNO_CHARGER_DBG_LEVEL;
module_param(log_level, int, 0644);

int tinno_charger_get_log_level(void)
{
	return log_level;
}

#define TINNO_CHARGER_DBG(fmt, ...) \
	do { \
		if (tinno_charger_get_log_level() >= TINNO_CHARGER_DBG_LEVEL) \
			pr_info("[TINNO_CHARGER]%s " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define TINNO_CHARGER_INFO(fmt, ...) \
	do { \
		if (tinno_charger_get_log_level() >= TINNO_CHARGER_INFO_LEVEL) \
			pr_info("[TINNO_CHARGER]%s " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define TINNO_CHARGER_ERR(fmt, ...) \
	do { \
		if (tinno_charger_get_log_level() >= TINNO_CHARGER_ERR_LEVEL) \
			pr_info("[TINNO_CHARGER]%s " fmt, __func__, ##__VA_ARGS__); \
	} while (0)


static ssize_t tinno_charger_show_log_level(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	//struct tinno_charger_info *info = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", log_level);
}

static ssize_t tinno_charger_store_log_level(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	//struct tinno_charger_info *info = dev_get_drvdata(dev);
	int val;

	sscanf(buf, "%d", &val);

	if ((val >= TINNO_CHARGER_ERR_LEVEL) && (val <= TINNO_CHARGER_DBG_LEVEL)) {
		log_level = val;
		TINNO_CHARGER_ERR("set log_level to %d\n", log_level);
	} else {
		TINNO_CHARGER_ERR("invalid value, keep log_level as %d\n", log_level);
	}

	return count;
}

static DEVICE_ATTR(log_level, 0660, tinno_charger_show_log_level, tinno_charger_store_log_level);

static int tinno_charger_create_device_node(struct device *dev)
{
	int ret = 0;

	ret = device_create_file(dev, &dev_attr_log_level);
	if (ret < 0) {
		TINNO_CHARGER_ERR("failed to create log_level attr\n");
		return -ENODEV;
	}

	return ret;
}

static int tinno_charger_get_sw_dev(struct tinno_charger_info *info)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(info->sw_chg)) {
		info->sw_chg = get_charger_by_name("primary_chg");
		if (IS_ERR_OR_NULL(info->sw_chg)) {
			TINNO_CHARGER_ERR("get primary_chg failed\n");
			ret = -ENODEV;
		}
	} else {
		TINNO_CHARGER_INFO("get primary_chg successfully\n");
	}

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static int tinno_charger_update_switch_charger_status(struct tinno_charger_info *info)
{
	int ret = 0;
	union charger_propval val = {0,};

	ret = charger_dev_get_property(info->sw_chg, CHARGER_PROP_CHARGER_ENABLED, &val);
	if (ret) {
		TINNO_CHARGER_ERR("get switch charger enabled failed(%d)\n", ret);
	} else {
		info->sw_dev.is_enabled = val.intval;
	}

	ret = charger_dev_get_property(info->sw_chg, CHARGER_PROP_CHARGER_EXIST, &val);
	if (ret) {
		TINNO_CHARGER_ERR("get switch charger exist failed(%d)\n", ret);
	} else {
		info->sw_dev.charger_exist = val.intval;
	}

	ret = charger_dev_get_property(info->sw_chg, CHARGER_PROP_CHARGER_VOLTAGE, &val);
	if (ret) {
		TINNO_CHARGER_ERR("get switch charger voltage failed(%d)\n", ret);
	} else {
		info->sw_dev.charger_voltage = val.intval;
	}

	ret = charger_dev_get_property(info->sw_chg, CHARGER_PROP_CHARGER_PROP_STATUS, &val);
	if (ret < 0) {
		TINNO_CHARGER_ERR("get switch charger prop status failed(%d)\n", ret);
	} else {
		info->sw_dev.prop_status = val.intval;
	}

	ret = charger_dev_get_property(info->sw_chg, CHARGER_PROP_CHARGER_TYPE, &val);
	if (ret) {
		TINNO_CHARGER_ERR("get switch charger type(%d)\n", ret);
	} else {
		info->sw_dev.charger_type = val.intval;
	}

	return ret;
}
#endif

static int tinno_charger_get_cp_dev(struct tinno_charger_info *info)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(info->cp_chg)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		info->cp_chg = get_charger_by_name("primary_dvchg");
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(4, 19, 0))
		info->cp_chg = get_charger_by_name("primary_divider_chg");
#endif
		if (IS_ERR_OR_NULL(info->cp_chg)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
			TINNO_CHARGER_ERR("get primary_dvchg failed\n");
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(4, 19, 0))
			TINNO_CHARGER_ERR("get primary_divider_chg failed\n");
#endif
			ret = -ENODEV;
		}
	} else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
			TINNO_CHARGER_INFO("get primary_dvchg successfully\n");
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(4, 19, 0))
			TINNO_CHARGER_INFO("get primary_divider_chg successfully\n");
#endif
	}

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static int tinno_charger_update_charger_pump_status(struct tinno_charger_info *info)
{
	int ret = 0;
	union charger_propval val = {0,};

	ret = charger_dev_get_property(info->cp_chg, CHARGER_PROP_CHARGER_ENABLED, &val);
	if (ret) {
		TINNO_CHARGER_ERR("get cp status failed(%d)\n", ret);
	} else {
		info->cp_dev.is_enabled = val.intval;
		TINNO_CHARGER_INFO("charger pump is %s\n", info->cp_dev.is_enabled ? "enabled" : "disabled");
	}

	return ret;
}
#endif

static int tinno_charger_get_batt_psy(struct tinno_charger_info *info)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(info->batt_psy)) {
		TINNO_CHARGER_ERR("batt psy is NULL, try to find batt psy\n");
		info->batt_psy = power_supply_get_by_name("battery");
		if (IS_ERR_OR_NULL(info->batt_psy)) {
			TINNO_CHARGER_ERR("can't find batt psy\n");
			ret = -ENODEV;
		}
	} else {
		TINNO_CHARGER_INFO("get batt psy successfully\n");
	}

	return ret;
}

static int tinno_charger_update_battery_status(struct tinno_charger_info *info)
{
	int ret = 0;
	union power_supply_propval val = {0,};

	/* get batt temp from gauge */
	ret = power_supply_get_property(info->batt_psy,
				POWER_SUPPLY_PROP_TEMP, &val);
	if (ret) {
		TINNO_CHARGER_ERR("get batt temp failed(%d)\n", ret);
	} else {
		info->batt_dev.temp = val.intval;
	}
	/* get batt volt from gauge */
	ret = power_supply_get_property(info->batt_psy,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (ret) {
		TINNO_CHARGER_ERR("get batt volt failed(%d)\n", ret);
	} else {
		info->batt_dev.vbat = val.intval;
	}
	/* get batt curr from gauge */
	ret = power_supply_get_property(info->batt_psy,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (ret) {
		TINNO_CHARGER_ERR("get batt curr failed(%d)\n", ret);
	} else {
		info->batt_dev.ibat = val.intval;
	}
	/* get batt soc from gauge */
	ret = power_supply_get_property(info->batt_psy,
				POWER_SUPPLY_PROP_CAPACITY, &val);
	if (ret) {
		TINNO_CHARGER_ERR("get batt capacity failed(%d)\n", ret);
	} else {
		info->batt_dev.soc = val.intval;
	}

	return ret;
}

static void tinno_charger_update_status_work(struct work_struct *work)
{
	int ret = 0;
	struct tinno_charger_info *info = container_of(work,
				struct tinno_charger_info, charger_monitor_work.work);

	/* update info from charger */
	ret = tinno_charger_get_sw_dev(info);
	if (!ret) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		tinno_charger_update_switch_charger_status(info);
#endif
	}

	/* update info from charger pump */
	ret = tinno_charger_get_cp_dev(info);
	if (!ret) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		tinno_charger_update_charger_pump_status(info);
#endif
	}

	/* update info from gauge */
	ret = tinno_charger_get_batt_psy(info);
	if (!ret) {
		tinno_charger_update_battery_status(info);
	}

	TINNO_CHARGER_INFO("CHGR[ %d / %d / %d / %d / %d ], "
						"BAT[ %d / %d / %d ], "
						"GAUGE[ %d / %d / %d / %d / %d ] \n",
			info->sw_dev.charger_exist, info->sw_dev.charger_type, info->sw_dev.charger_voltage, info->sw_dev.is_enabled, info->sw_dev.prop_status,
			info->batt_dev.batt_exist, info->batt_dev.batt_full, info->batt_dev.in_rechging,
			info->batt_dev.vbat, info->batt_dev.ibat, info->batt_dev.temp, info->batt_dev.soc, info->batt_dev.ui_soc
	);

	/*re-schedule work*/
	schedule_delayed_work(&info->charger_monitor_work, msecs_to_jiffies(NORMAL_UPDATE_MS));

	return;
}

static int tinno_charger_probe(struct platform_device *pdev)
{
	struct tinno_charger_info *info = NULL;
	int ret = 0;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		TINNO_CHARGER_ERR("malloc memory failed\n");
		return -ENOMEM;
	}

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	ret = tinno_charger_create_device_node(info->dev);
	if (ret < 0) {
		goto free_mem;
	}

	INIT_DELAYED_WORK(&info->charger_monitor_work, tinno_charger_update_status_work);
	schedule_delayed_work(&info->charger_monitor_work, msecs_to_jiffies(NORMAL_UPDATE_MS));

	TINNO_CHARGER_INFO("successfully\n");

	return 0;

free_mem:
	TINNO_CHARGER_ERR("failed, free malloc memory\n");
	devm_kfree(&pdev->dev, info);

	return ret;
}

static int tinno_charger_remove(struct platform_device *pdev)
{
	struct tinno_charger_info *info = platform_get_drvdata(pdev);
	TINNO_CHARGER_INFO("++\n");

	if (!IS_ERR_OR_NULL(info)) {
		cancel_delayed_work(&info->charger_monitor_work);
	}

	return 0;
}

static int tinno_charger_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tinno_charger_info *info = platform_get_drvdata(pdev);

	TINNO_CHARGER_INFO("++\n");

	if (!IS_ERR_OR_NULL(info)) {
		cancel_delayed_work(&info->charger_monitor_work);
	}

	return 0;
}

static int tinno_charger_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tinno_charger_info *info = platform_get_drvdata(pdev);

	TINNO_CHARGER_INFO("++\n");

	if (!IS_ERR_OR_NULL(info)) {
		schedule_delayed_work(&info->charger_monitor_work, msecs_to_jiffies(NORMAL_UPDATE_MS));
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(tinno_charger_pm_ops, tinno_charger_suspend, tinno_charger_resume);

static const struct of_device_id tinno_charger_of_match[] = {
	{ .compatible = "tinno,tinno_charger", },
	{},
};
MODULE_DEVICE_TABLE(of, tinno_charger_of_match);

static struct platform_driver tinno_charger_platdrv = {
	.probe = tinno_charger_probe,
	.remove = tinno_charger_remove,
	.driver = {
		.name = "tinno_charger",
		.owner = THIS_MODULE,
		.pm = &tinno_charger_pm_ops,
		.of_match_table = tinno_charger_of_match,
	},
};

static int __init tinno_charger_init(void)
{
	return platform_driver_register(&tinno_charger_platdrv);
}

static void __exit tinno_charger_exit(void)
{
	platform_driver_unregister(&tinno_charger_platdrv);
}

late_initcall(tinno_charger_init);
module_exit(tinno_charger_exit);

MODULE_AUTHOR("hao.jia <hao.jia@tinno.com>");
MODULE_DESCRIPTION("Tinno Charger Framework");
MODULE_LICENSE("GPL");
