#include <linux/init.h> /* For init/exit macros */
#include <linux/module.h> /* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
//#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/thermal.h>

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include "../../../oem/devinfo/dev_info.h"
#endif /* CONFIG_OEM_DEVINFO */
extern int mtktsusb_get_hw_temp(void);
#define USB_FAKE_TEMP_INVALID_NUM  0xFFFF
#define USB_HIGH_TEMP_THRES        70            //unit: 1C
#define USB_INPUT_RESUME_TEMP      60            //unit: 1C
#define USB_TEMP_CHECK_TRIGGER_INTERVAL_MS 1000  //1s
#define USB_TEMP_CHECK_INTERVAL_MS 10000         //10s

struct usb_hightemp_prot {
	int usb_overtemp_en_gpio;
	bool usb_input_off;
	int fake_temp;
};

struct charge_misc {
	struct platform_device *pdev;
	struct usb_hightemp_prot uhp;
	struct delayed_work usb_temp_check_dwork;
};

unsigned int charge_misc_get_hwsku(void)
{
#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	return oem_hw_sku();
#endif /* CONFIG_OEM_DEVINFO */
}
void charge_misc_usb_input_off(struct charge_misc *info)
{
	gpio_direction_output(info->uhp.usb_overtemp_en_gpio, 1);
}

void charge_misc_usb_input_on(struct charge_misc *info)
{
	gpio_direction_output(info->uhp.usb_overtemp_en_gpio, 0);
}

static ssize_t usb_input_off_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct charge_misc *info = dev->driver_data;
	return sprintf(buf, "%d\n", info->uhp.usb_input_off);
}

static ssize_t usb_input_off_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
#ifdef USB_TEMP_DEBUG
	struct charge_misc *info = dev->driver_data;
	int val;

	if (kstrtoint(buf, 10, &val) == 0) {
		info->uhp.usb_input_off = val;
		if (info->uhp.usb_input_off)
			charge_misc_usb_input_off(info);
		else
			charge_misc_usb_input_on(info);
	} else
		pr_err("%s: format error!\n", __func__);
#endif

	return size;
}

static DEVICE_ATTR_RW(usb_input_off);

static ssize_t usb_temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct charge_misc *info = dev->driver_data;
	int temp = mtktsusb_get_hw_temp();

	if (info->uhp.fake_temp != USB_FAKE_TEMP_INVALID_NUM)
		return sprintf(buf, "%d\n", info->uhp.fake_temp);
	else {
		return sprintf(buf, "%d\n", temp);
	}
}

static ssize_t usb_temp_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t size)
{
#ifdef USB_TEMP_DEBUG
	struct charge_misc *info = dev->driver_data;
	int val;

	if (kstrtoint(buf, 10, &val) == 0)
		info->uhp.fake_temp = val;
	else
		pr_err("%s: format error!\n", __func__);
#endif

	return size;
}

static DEVICE_ATTR_RW(usb_temp);

static int charge_misc_setup_files(struct platform_device *pdev)
{
	int ret;

	ret = device_create_file(&(pdev->dev), &dev_attr_usb_input_off);
	if (ret)
		return ret;

	ret = device_create_file(&(pdev->dev), &dev_attr_usb_temp);
	if (ret)
		return ret;

	return 0;
}

static int usb_high_temp_protect_parse_dt(struct device_node *np, struct charge_misc *info)
{
	int overtemp_en_gpio = 0;

	overtemp_en_gpio = of_get_named_gpio(np, "usb-overtemp-en-gpio", 0);
	if (!gpio_is_valid(overtemp_en_gpio)) {
		pr_err("%s: usb overtemp en gpio get failed\n", __func__);
		return -1;
	} else {
		pr_err("%s: get usb overtemp en gpio -> %d\n", __func__, overtemp_en_gpio);
		info->uhp.usb_overtemp_en_gpio = overtemp_en_gpio;
	}

	return 0;
}

static int charge_misc_parse_dt(struct charge_misc *info, struct device *dev)
{
	struct device_node *np = dev->of_node;

	if (usb_high_temp_protect_parse_dt(np, info))
		return -1;

	return 0;
}

static void usb_temp_check_dwork_handler(struct work_struct *work)
{
	struct charge_misc *info = container_of(work, struct charge_misc, usb_temp_check_dwork.work);

	int temp;
	temp = mtktsusb_get_hw_temp();
	if (info->uhp.fake_temp != USB_FAKE_TEMP_INVALID_NUM)
		temp = info->uhp.fake_temp;
	if (temp < USB_INPUT_RESUME_TEMP && info->uhp.usb_input_off) {
		charge_misc_usb_input_on(info);
		info->uhp.usb_input_off = false;
		pr_err("%s: temp = %d, restore the usb input\n", __func__, temp);
	} else if (temp > USB_HIGH_TEMP_THRES && !info->uhp.usb_input_off) {
		charge_misc_usb_input_off(info);
		info->uhp.usb_input_off = true;
		pr_err("%s: temp = %d, disable the usb input!!!\n", __func__, temp);
	}

	if (info->uhp.usb_input_off)
		schedule_delayed_work(&info->usb_temp_check_dwork, msecs_to_jiffies(USB_TEMP_CHECK_TRIGGER_INTERVAL_MS));
	else
		schedule_delayed_work(&info->usb_temp_check_dwork, msecs_to_jiffies(USB_TEMP_CHECK_INTERVAL_MS));
}

static int charge_misc_probe(struct platform_device *pdev)
{
	struct charge_misc *info = NULL;
	int hwsku = 0;

	pr_err("%s: start\n", __func__);
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->uhp.fake_temp = USB_FAKE_TEMP_INVALID_NUM;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	if (charge_misc_parse_dt(info, &(pdev->dev))) {
		pr_err("%s: parse dt failed!\n", __func__);
		goto err_out;
	}

	if (gpio_request(info->uhp.usb_overtemp_en_gpio, "usb_input_control")) {
		pr_err("%s: can't request gpio %d\n", __func__, info->uhp.usb_overtemp_en_gpio);
		goto err_out;
	}

	hwsku = charge_misc_get_hwsku();
	pr_err("%s: hwsku = %d\n", __func__, hwsku);
	if (hwsku == 5) {
		INIT_DELAYED_WORK(&info->usb_temp_check_dwork, usb_temp_check_dwork_handler);
		charge_misc_setup_files(pdev);
		schedule_delayed_work(&info->usb_temp_check_dwork, msecs_to_jiffies(USB_TEMP_CHECK_INTERVAL_MS));
	} else {
		pr_err("%s: hwsku is not sku5 \n", __func__);
		goto err_out;
	}
	pr_err("%s success\n", __func__);
	return 0;

err_out:
	devm_kfree(&(pdev->dev), info);
	pr_err("%s failed!\n", __func__);
	return -ENODEV;
}

static int charge_misc_remove(struct platform_device *pdev)
{
	struct charge_misc *info = platform_get_drvdata(pdev);

	cancel_delayed_work(&info->usb_temp_check_dwork);
	gpio_free(info->uhp.usb_overtemp_en_gpio);
	return 0;
}

static void charge_misc_shutdown(struct platform_device *pdev)
{
	struct charge_misc *info = platform_get_drvdata(pdev);

	cancel_delayed_work(&info->usb_temp_check_dwork);
	gpio_free(info->uhp.usb_overtemp_en_gpio);
}

static const struct of_device_id charge_misc_of_match[] = {
	{.compatible = "misc,charge",},
	{},
};

MODULE_DEVICE_TABLE(of, charge_misc_of_match);


static struct platform_driver charge_misc_driver = {
	.probe = charge_misc_probe,
	.remove = charge_misc_remove,
	.shutdown = charge_misc_shutdown,
	.driver = {
		.name = "charge_misc",
		.owner = THIS_MODULE,
		.of_match_table = charge_misc_of_match,
	},
};

static int __init charge_misc_init(void)
{
	return platform_driver_register(&charge_misc_driver);
}
late_initcall(charge_misc_init);

static void __exit charge_misc_exit(void)
{
	platform_driver_unregister(&charge_misc_driver);
}
module_exit(charge_misc_exit);

MODULE_DESCRIPTION("Misc Charge Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jerry");

