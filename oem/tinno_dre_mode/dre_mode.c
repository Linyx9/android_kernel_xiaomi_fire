/*
* Copyright (c) TN Technologies Co., Ltd. 2022-2022. All rights reserved.
*
* Oem device info driver for all bsp module
*
* @Author oem
* @Since  2022/12/28
*/
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/moduleparam.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/proc_fs.h>

#include "mtk_disp_notify.h"
#include "mtk_panel_ext.h"
#include "dre_mode.h"

extern void disp_aal_set_dre_en(int enable);

#define dre_mode_DEVICE_INFO    "dre_mode_device"
static struct dre_mode_init *dre_init_set = NULL;
static struct proc_dir_entry *dre_procfs;

static ssize_t disp_set_dre_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
	u32 len = 0;

	pr_info("%s enter!\n", __func__);

	if (*pos != 0)
		return 0;

	memset(dre_init_set->dre_en_buf, 0,  sizeof(dre_init_set->dre_en_buf));
	len += snprintf(dre_init_set->dre_en_buf + len, 16 - len, "%d\n", dre_init_set->dre_en);

	if (copy_to_user((char *)buff, dre_init_set->dre_en_buf, len))
		pr_err("Failed to copy data to user space\n");

	*pos += len;

	return len;
}

static ssize_t disp_set_dre_write(struct file *filp, const char *buff, size_t size, loff_t *pos)
{
	char cmd[16] = { 0 };
	ssize_t ret;

	pr_info("%s enter! dre_init_set->suspend_flag = %d\n", __func__,dre_init_set->suspend_flag);

	if (dre_init_set->suspend_flag) {
		pr_info("In suspend, no write dre, return now");
		return -1;
	}
	if ((size - 1) > sizeof(cmd)) {
		pr_err("ERROR! input length is larger than local buffer\n");
		return -1;
	}
	if (buff != NULL) {
		if (copy_from_user(cmd, buff, size)) {
			pr_err("Failed to copy data from user space\n");
			size = -1;
			goto out;
		}
	}

	dre_init_set->dre_en = simple_strtol(cmd, NULL, 0);
	disp_aal_set_dre_en(dre_init_set->dre_en);

	pr_info("%s end! dre_en = %d\n", __func__, dre_init_set->dre_en);

out:
	ret = size;
	return ret;
}

static struct proc_ops proc_dre_fops = {
	.proc_read = disp_set_dre_read,
	.proc_write = disp_set_dre_write,
	.proc_lseek = default_llseek,
};

static int dre_mode_disp_notifier_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	int *blank = (int *)data;

	// pr_info("%s: event = %d ,black = %d \n", __func__,event,*blank);

	switch (event) {
	case MTK_DISP_EVENT_BLANK:
	pr_info("%s enter \n", __func__);
		if (*blank == MTK_DISP_BLANK_UNBLANK) {
			dre_init_set->suspend_flag = 0;
		} else if (*blank == MTK_DISP_BLANK_POWERDOWN) {
			dre_init_set->suspend_flag = 1;
		}
	break;

	default :
	break;
	}

	return 0;
}


static int dre_mode_probe(struct platform_device *pdev)
{
	int retval;

	dre_init_set = devm_kzalloc(&pdev->dev, sizeof(*dre_init_set), GFP_KERNEL);
	if(dre_init_set == NULL) {
		pr_err( "dre_mode_probe error!.\n");
		return -1;
	}

	dre_procfs = proc_mkdir("lcd_info", NULL);
  	if (!dre_procfs) {
  		pr_err("[%s %d]failed to create dir: /proc/dre_procfs\n",__func__, __LINE__);
  		return -1;
  	}

	if (!proc_create("disp_set_dre", 0644, dre_procfs, &proc_dre_fops)) {
	  	pr_err("[%s %d]failed to create dir: /proc/lcd_info/disp_set_dre \n",__func__, __LINE__);
  		return -1;
  	}

	dre_init_set->fb_notifier.notifier_call = dre_mode_disp_notifier_cb;
	retval = mtk_disp_notifier_register("dre_mode_display", &dre_init_set->fb_notifier);
	if (retval < 0) {
		pr_err("%s: Failed to register disp  notifier client\n", __func__);
	}
	dre_init_set->suspend_flag = 0;
	return 0;
}

static int dre_mode_suspend(struct device *dev)
{
	dre_init_set->suspend_flag = 1;
	return 0;
}
static int dre_mode_resume(struct device *dev)
{
	dre_init_set->suspend_flag = 0;
	return 0;
}
static int dre_mode_remove(struct platform_device *pdev)
{
	if(dre_init_set)
		kfree(dre_init_set);
	return 0;
}

static const struct of_device_id dre_mode_match_tbl[] = {
	{ .compatible = "tinno,dre_mode" },
	{ },
};

static const struct dev_pm_ops dre_pm_ops = {
	.suspend = dre_mode_suspend,
	.resume = dre_mode_resume,
};

static struct platform_driver dre_mode_driver = {
	.probe = dre_mode_probe,
	.remove = dre_mode_remove,
	.driver = {
		.name = dre_mode_DEVICE_INFO,
		.pm = &dre_pm_ops,
		.of_match_table = dre_mode_match_tbl,

	},
};


static int __init dre_mode_init(void)
{
	if (platform_driver_register(&dre_mode_driver) != 0) {
		pr_err( "dre_mode_init driver_register fail!.\n");
		return -1;
	}

	return 0;
}

static void __exit dre_mode_exit(void)
{
	platform_driver_unregister(&dre_mode_driver);
}

module_init(dre_mode_init);
module_exit(dre_mode_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("dre_mode");
MODULE_LICENSE("GPL");
