// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/sysfs.h>
#include <linux/of_irq.h>
#include <linux/input.h>
#include "ant_det.h"
#include <linux/of_device.h>
#include <linux/device.h>
//#include <soc/qcom/cmd-db.h>

#include <linux/fs.h>


static struct ant_det_data *ant_pdata;
static struct platform_device *ant_pdev;
//static struct class *ant_det_class;
//static int ant_det_major;
//static int soc_id;
static struct class ant_det_class = {
	.name = "ant_det",
};
/*
static int configure_gpios(int on)
{
	int rc = 0;
	int gpio = ant_pdata->gpio;

	if (on) {
		gpio_set_value(gpio, 1);
	}
	else {
		gpio_set_value(gpio, 0);
		msleep(100);
	}

	pr_err("%s: ant_det gpio= %d on: %d\n", __func__, gpio, on);

	return rc;
}
 */
/*
static int ant_det_control(int on)
{
	int rc = 0;

	pr_err("%s: on: %d\n", __func__, on);

	if (on == 1) {
		rc = regulator_enable(ant_pdata->vdd);
		if (rc < 0) {
			pr_err("%s: ant_det regulators config failed\n", __func__);
		}
		rc = configure_gpios(on);
		if (rc < 0) {
			pr_err("%s: ant_det gpio config failed\n", __func__);
		}
	}
	else if(on ==0){
		rc= regulator_disable(ant_pdata->vdd);
		if (rc < 0) {
			pr_err("%s: ant_det regulators config failed\n",__func__);
		}
		rc=configure_gpios(on);
		if (rc < 0) {
			pr_err("%s: ant_det gpio config failed\n", __func__);
		}
	}

	return rc;
}
*/

static int ant_det_populate_dt_pinfo(struct platform_device *pdev)
{
	int ret = 0;
	pr_err("%s   \n", __func__);
	pr_err("%s  : ant_det_init\n", __func__);

	if (!ant_pdata)
		return -ENOMEM;

	ant_pdata->gpio =
		of_get_named_gpio(pdev->dev.of_node, "tinno,ant-det-gpio", 0);
//	ant_pdata->irq = gpio_to_irq(ant_pdata->gpio);
	pr_err(" ant_det gpio: %d\n", ant_pdata->gpio);
//	pr_err(" ant_det irq: %d\n", ant_pdata->irq);
	ret = gpio_get_value(ant_pdata->gpio);
	pr_err(" ant_det gpio_get_value: %d\n", ret);
#if 0
	ant_pdata->gpio = of_get_named_gpio(pdev->dev.of_node,
						"tinno,lna-en-gpio", 0);
	if (!gpio_is_valid(ant_pdata->gpio)) {
		pr_err( "ant_det gpio is not valid\n");
		return -EINVAL;
	} else
		pr_err("ant_det gpio= %d", ant_pdata->gpio);
#endif

	return 0;
}

static ssize_t gpio_level_show(const struct class *class,
			       const struct class_attribute *attr, char *buf)
{
	int ret;

	ret = gpio_get_value(ant_pdata->gpio);
	pr_info("get gpio level: %d\n", ret);
	return sprintf(buf, "%d\n", ret);
	//return 0;
}

static ssize_t gpio_level_store(const struct class *class,
				const struct class_attribute *attr,
				const char *buf, size_t size)
{
	int ret, val;
	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;
	val = !!val;
	pr_info("set gpio level: %d\n", val);
	gpio_direction_output(ant_pdata->gpio, val);
	return size;
}

static CLASS_ATTR_RW(gpio_level);
#if 0
static void ant_det_work_func(struct work_struct *work)
{
	int ret;

	pr_err(" %s\n", __func__);

	ret = gpio_get_value(ant_pdata->gpio);
	pr_err(" ant_det get gpio level: %x\n", ret);
	//TN Begin modified by bingtai.zou/860558 20230823 EKFOGO4G-1547 begin
	if (ret != 0) {
		input_report_key(ant_pdata->input, 1, 1);
		input_sync(ant_pdata->input);
		pr_info("TN ant_det get gpio level: %x\n", ret);
	} else {
		input_report_key(ant_pdata->input, 1, 0);
		input_sync(ant_pdata->input);
		pr_info("TN ant_det get gpio level: %x\n", ret);
	}
	//TN Begin modified by bingtai.zou/860558 20230823 EKFOGO4G-1547 end
}

static irqreturn_t ant_det_irq_handler(int irq, void *dev_id)
{
	pr_debug("ant_det irq handler\n");
	schedule_delayed_work(&ant_pdata->det_work, HZ);
	return IRQ_HANDLED;
}
#endif
static int ant_det_probe(struct platform_device *pdev)
{
	int ret = 0;

	pr_info("%s   \n", __func__);
	pr_err("%s:   ant_det_init\n", __func__);
	ant_pdev = pdev;
	ant_pdata = kzalloc(sizeof(*ant_pdata), GFP_KERNEL);
	if (!ant_pdata)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		ret = ant_det_populate_dt_pinfo(pdev);
		if (ret < 0) {
			pr_err("%s, Failed to populate device tree info\n",
			       __func__);
			goto free_pdata;
		}
		pdev->dev.platform_data = ant_pdata;
	}
#if 0
	ant_pdata->input = devm_input_allocate_device(&pdev->dev);
	if (!ant_pdata->input) {
		pr_err("ant_det failed to allocate input device\n");
			goto free_pdata;
	}
	ant_pdata->input->name = "ant_det";
	//TN Begin modified by bingtai.zou/860558 20230823 EKFOGO4G-1547 begin
	ant_pdata->input->id.vendor = 6;
	//TN Begin modified by bingtai.zou/860558 20230823 EKFOGO4G-1547 end
	input_set_capability(ant_pdata->input, EV_KEY, 1);
	input_register_device(ant_pdata->input);

	INIT_DELAYED_WORK(&ant_pdata->det_work, ant_det_work_func);

	if (ant_pdata->irq) {
		ret = request_irq(ant_pdata->irq, ant_det_irq_handler,
				IRQ_TYPE_EDGE_BOTH, "ant_det", pdev);
	}
#endif
#if 0
#if 0
	fm_lna_pdata->lna_vdd = regulator_get(&pdev->dev, "lna_vdd");
#else
	fm_lna_pdata->lna_vdd = devm_regulator_get(&pdev->dev, "vio28");
#endif
	if (IS_ERR(fm_lna_pdata->lna_vdd)) {
		ret = PTR_ERR(fm_lna_pdata->lna_vdd);
		pr_err( "Regulator fm_lna_pdata get failed rc=%d\n", ret);
		goto free_pdata;
	} else {
		ret = regulator_set_voltage(fm_lna_pdata->lna_vdd,	2800000, 2800000);
		if (ret < 0) {
			pr_err("%s: regulator_set_voltage failed rc=%d\n",
					__func__,  ret);
			 goto free_pdata;
		}
#if 0
		ret = regulator_enable(fm_lna_pdata->lna_vdd);
		if (ret) {
			regulator_put(fm_lna_pdata->lna_vdd);
			pr_err("%s: Error %d enable regulator\n",
				__func__, ret);
			goto free_pdata;
		}
#endif
	}

	ret = gpio_request(ant_pdata->lna_gpio, "lna-en-gpio");
	if (ret) {
		pr_err("%s: unable to request gpio %d (%d)\n",
				__func__, fm_lna_pdata->lna_gpio, ret);
		return ret;
	}

	ret = gpio_direction_output(fm_lna_pdata->lna_gpio, 0);
	if (ret) {
		pr_err("%s: Unable to set direction\n", __func__);
		return ret;
	}
#endif

	ret = class_register(&ant_det_class);
	if (ret < 0) {
		pr_err("Create fsys class failed (%d)\n", ret);
		return ret;
	}

	ret = class_create_file(&ant_det_class, &class_attr_gpio_level);

	if (ret < 0) {
		pr_err("Create reset file failed (%d)\n", ret);
		return ret;
	}

	return 0;

free_pdata:
	kfree(ant_pdata);
	return ret;
}

static int ant_det_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);
	kfree(ant_pdata);
	return 0;
}
/*
static long ant_det_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret,pwr_cntrl = 0;

	switch (cmd) {
	case ANT_DET_CMD_PWR_CTRL:
		pwr_cntrl = (int)arg;
		pr_warn("%s: ANT_DET_CMD_PWR_CTRL pwr_cntrl: %d\n",
			__func__, pwr_cntrl);
		ret = ant_det_control(pwr_cntrl);

		if(ret) {
			pr_err("%s: ant det power comand  fail\n", __func__);
		}
		break;

	default:
		return -ENOIOCTLCMD;
	}
	return ret;
}
*/
static const struct of_device_id ant_det_match_table[] = {
	{ .compatible = "tinno,ant_det" },
	{},
};

static struct platform_driver ant_det_driver = {
	.probe = ant_det_probe,
	.remove = ant_det_remove,
	.driver = {
		.name = "ant_det",
		.of_match_table = ant_det_match_table,
	},
};

static int __init ant_det_init(void)
{
	int ret = 0;
	pr_err("%s:  ant_det_init\n", __func__);
	ret = platform_driver_register(&ant_det_driver);
	if (ret) {
		pr_err("%s: platform_driver_register error: %d\n", __func__,
		       ret);
		goto driver_err;
	}
	return 0;
driver_err:
	return ret;
}

static void __exit ant_det_exit(void)
{
	platform_driver_unregister(&ant_det_driver);
}

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("tinno ant det driver");
MODULE_AUTHOR("q.d@tinno");

module_init(ant_det_init);
module_exit(ant_det_exit);
