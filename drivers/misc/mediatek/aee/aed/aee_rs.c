// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 MediaTek Inc.
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/string.h>

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
#include <mtk_drm_assert_ext.h>
#endif

#include "aed.h"

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)
static int aed_rs_dal_printf(const char *msg)
{
	int (*dal_printf)(const char *fmt, ...);
	int ret;

	dal_printf = symbol_get(DAL_Printf);
	if (!dal_printf)
		return -ENODEV;

	ret = dal_printf("%s", msg);
	symbol_put(DAL_Printf);

	return ret;
}

static int aed_rs_dal_clean(unsigned int foreground, unsigned int background)
{
	int (*dal_set_color)(unsigned int fgColor, unsigned int bgColor);
	int (*dal_clean)(void);
	int ret;

	dal_set_color = symbol_get(DAL_SetColor);
	if (!dal_set_color)
		return -ENODEV;

	dal_clean = symbol_get(DAL_Clean);
	if (!dal_clean) {
		symbol_put(DAL_SetColor);
		return -ENODEV;
	}

	ret = dal_set_color(foreground, background);
	if (!ret)
		ret = dal_clean();

	symbol_put(DAL_Clean);
	symbol_put(DAL_SetColor);

	return ret;
}

static int aed_rs_dal_setcolor(unsigned int foreground, unsigned int background,
			       enum DAL_COLOR screen_color)
{
	int (*dal_set_color)(unsigned int fgColor, unsigned int bgColor);
	int (*dal_set_screen_color)(enum DAL_COLOR color);
	int ret;

	dal_set_color = symbol_get(DAL_SetColor);
	if (!dal_set_color)
		return -ENODEV;

	dal_set_screen_color = symbol_get(DAL_SetScreenColor);
	if (!dal_set_screen_color) {
		symbol_put(DAL_SetColor);
		return -ENODEV;
	}

	ret = dal_set_color(foreground, background);
	if (!ret)
		ret = dal_set_screen_color(screen_color);

	symbol_put(DAL_SetScreenColor);
	symbol_put(DAL_SetColor);

	return ret;
}
#else
static int aed_rs_dal_printf(const char *msg)
{
	return -ENODEV;
}

static int aed_rs_dal_clean(unsigned int foreground, unsigned int background)
{
	return -ENODEV;
}

static int aed_rs_dal_setcolor(unsigned int foreground, unsigned int background,
			       int screen_color)
{
	return -ENODEV;
}
#endif


static int aed_rs_open(struct inode *inode, struct file *filp)
{
	if (strncmp(current->comm, "aee_aed", 7))
		return -1;
	pr_debug("%s:%d:%d\n", __func__, MAJOR(inode->i_rdev),
						MINOR(inode->i_rdev));
	return 0;
}

static int aed_rs_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static unsigned int aed_rs_poll(struct file *file,
					struct poll_table_struct *ptable)
{
	return 0;
}

static ssize_t aed_rs_read(struct file *filp, char __user *buf,
						size_t count, loff_t *f_pos)
{
	return 0;
}

static ssize_t aed_rs_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	return 0;
}

DEFINE_SEMAPHORE(aed_rs_sem,1);
static long aedrs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct aee_dal_show *dal_show;
	struct aee_dal_setcolor dal_setcolor;

	if (aee_get_mode() >= AEE_MODE_CUSTOMER_ENG) {
		pr_info("cmd(%d) not allowed (mode %d)\n",
				cmd, aee_get_mode());
		return -EFAULT;
	}

	if (down_interruptible(&aed_rs_sem) < 0)
		return -ERESTARTSYS;

	switch (cmd) {
	case AEEIOCTL_DAL_SHOW:
		/* It's troublesome to allocate more than
		 * 1KB size on stack
		 */
		dal_show = kzalloc(sizeof(struct aee_dal_show), GFP_KERNEL);
		if (!dal_show) {
			ret = -EFAULT;
			goto EXIT;
		}

		if (copy_from_user(dal_show,
				(struct aee_dal_show __user *)arg,
				sizeof(struct aee_dal_show))) {
			ret = -EFAULT;
			goto OUT;
		}

		/* Try to prevent overrun */
		dal_show->msg[sizeof(dal_show->msg) - 1] = 0;
		pr_debug("AEE CALL DAL_Printf now\n");
		ret = aed_rs_dal_printf(dal_show->msg);
		if (ret)
			pr_info("AEE DAL_SHOW failed: drm DAL not available\n");

 OUT:
		kfree(dal_show);
		dal_show = NULL;
		goto EXIT;
	case AEEIOCTL_DAL_CLEAN:
		/* set default bgcolor to red,
		 * it will be used in DAL_Clean
		 */
		dal_setcolor.foreground = 0x00ff00;	/*green */
		dal_setcolor.background = 0xff0000;	/*red */

		pr_debug("AEE CALL DAL_SetColor now\n");
		pr_debug("AEE CALL DAL_Clean now\n");
		ret = aed_rs_dal_clean(dal_setcolor.foreground,
				       dal_setcolor.background);
		if (ret)
			pr_info("AEE DAL_CLEAN failed: drm DAL not available\n");
		break;
	case AEEIOCTL_SETCOLOR:
		if (copy_from_user(&dal_setcolor,
				(struct aee_dal_setcolor __user *)arg,
				sizeof(struct aee_dal_setcolor))) {
			ret = -EFAULT;
			goto EXIT;
		}
		pr_debug("AEE CALL DAL_SetColor now\n");
		pr_debug("AEE CALL DAL_SetScreenColor now\n");
		ret = aed_rs_dal_setcolor(dal_setcolor.foreground,
					  dal_setcolor.background,
					  dal_setcolor.screencolor);
		if (ret)
			pr_info("AEE DAL_SETCOLOR failed: drm DAL not available\n");
		break;
	default:
		ret = -EINVAL;
	}

 EXIT:
	up(&aed_rs_sem);
	return ret;
}

/******************************************************************************
 * Module related
 *****************************************************************************/
static const struct file_operations aed_rs_fops = {
	.owner = THIS_MODULE,
	.open = aed_rs_open,
	.release = aed_rs_release,
	.poll = aed_rs_poll,
	.read = aed_rs_read,
	.write = aed_rs_write,
	.unlocked_ioctl = aedrs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = aedrs_ioctl,
#endif
};

static struct miscdevice aed_rs_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "aed2",
	.fops = &aed_rs_fops,
};

static int __init aedrs_init(void)
{
	int err;

	err = misc_register(&aed_rs_dev);
	if (unlikely(err))
		pr_err("aee: failed to register aed2 device!\n");

	return err;
}

static void __exit aedrs_exit(void)
{
	misc_deregister(&aed_rs_dev);
}
module_init(aedrs_init);
module_exit(aedrs_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek AED Driver");
MODULE_AUTHOR("MediaTek Inc.");
