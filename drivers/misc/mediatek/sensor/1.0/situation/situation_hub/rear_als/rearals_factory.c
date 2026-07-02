// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) "<REARALS_FAC>" fmt

#include "rearals_factory.h"

struct rearals_factory_private {
	uint32_t gain;
	uint32_t sensitivity;
	struct rearals_factory_fops *fops;
};

static struct rearals_factory_private rearals_factory;

static int rearals_factory_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int rearals_factory_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static long rearals_factory_unlocked_ioctl(struct file *file, unsigned int cmd,
					unsigned long arg)
{
	long err = 0;
	void __user *ptr = (void __user *)arg;
	int32_t data = 0;
	int als_cali = 0;
	uint32_t flag = 0;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void __user *)arg,
				 _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok((void __user *)arg,
				 _IOC_SIZE(cmd));

	if (err) {
		pr_err("access error: %08X, (%2d, %2d)\n", cmd,
			    _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch (cmd) {
	case REARALS_IOCTL_INIT:
		if (copy_from_user(&flag, ptr, sizeof(flag)))
			return -EFAULT;
		if (rearals_factory.fops != NULL &&
		    rearals_factory.fops->enable_sensor != NULL) {
			err = rearals_factory.fops->enable_sensor(flag, 200);
			if (err < 0) {
				pr_err("REARALS_IOCTL_INIT fail!\n");
				return -EINVAL;
			}
			pr_debug(
				"REARALS_IOCTL_INIT, enable: %d, sample_period:%dms\n",
				flag, 200);
		} else {
			pr_err("REARALS_IOCTL_INIT NULL\n");
			return -EINVAL;
		}
		return 0;
	case REARALS_IOCTL_READ_SENSORDATA:
		if (rearals_factory.fops != NULL &&
		    rearals_factory.fops->get_data != NULL) {
			err = rearals_factory.fops->get_data(&data);
			if (err < 0) {
				pr_err(
					"REARALS_IOCTL_READ_SENSORDATA read data fail!\n");
				return -EINVAL;
			}
			pr_debug("REARALS_IOCTL_READ_SENSORDATA: (%d)!\n",
				data);
			if (copy_to_user(ptr, &data,
							sizeof(data)))
				return -EFAULT;
		} else {
			pr_err("REARALS_IOCTL_READ_SENSORDATA NULL\n");
			return -EINVAL;
		}
		return 0;
	case REARALS_IOCTL_ENABLE_CALI:
		if (rearals_factory.fops != NULL &&
		    rearals_factory.fops->enable_calibration != NULL) {
			err = rearals_factory.fops->enable_calibration();
			if (err < 0) {
				pr_err(
					"REARALS_IOCTL_ENABLE_CALI fail!\n");
				return -EINVAL;
			}
		} else {
			pr_err("REARALS_IOCTL_ENABLE_CALI NULL\n");
			return -EINVAL;
		}
		return 0;
	case REARALS_IOCTL_GET_CALI:
		if (rearals_factory.fops != NULL &&
		    rearals_factory.fops->rearals_get_cali != NULL) {
			err = rearals_factory.fops->rearals_get_cali(&data);
			if (err < 0) {
				pr_err("REARALS_IOCTL_GET_CALI FAIL!\n");
				return -EINVAL;
			}
		} else {
			pr_err("REARALS_IOCTL_GET_CALI NULL\n");
			return -EINVAL;
		}

		pr_debug("REARALS_IOCTL_GET_CALI: (%d)!\n",
			data);
		if (copy_to_user(ptr, &data, sizeof(data)))
			return -EFAULT;
		return 0;
	case REARALS_ALS_SET_CALI:
		if (copy_from_user(&als_cali, ptr, sizeof(als_cali)))
			return -EFAULT;
		if (rearals_factory.fops != NULL &&
		    rearals_factory.fops->rearals_set_cali != NULL) {
			err = rearals_factory.fops->rearals_set_cali(als_cali);
			if (err < 0) {
				pr_err("REARALS_ALS_SET_CALI FAIL!\n");
				return -EINVAL;
			}
		} else {
			pr_err("REARALS_ALS_SET_CALI NULL\n");
			return -EINVAL;
		}
		return 0;
	default:
		pr_err("unknown IOCTL: 0x%08x\n", cmd);
		return -ENOIOCTLCMD;
	}
	return 0;
}

#if IS_ENABLED(CONFIG_COMPAT)
static long compat_rearals_factory_unlocked_ioctl(struct file *filp,
					       unsigned int cmd,
					       unsigned long arg)
{
	if (!filp->f_op || !filp->f_op->unlocked_ioctl) {
		pr_err(
			"compat_ion_ioctl file has no f_op or no f_op->unlocked_ioctl.\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case COMPAT_REARALS_IOCTL_INIT:
	case COMPAT_REARALS_IOCTL_READ_SENSORDATA:
	case COMPAT_REARALS_IOCTL_ENABLE_CALI:
	case COMPAT_REARALS_IOCTL_GET_CALI: {
		pr_debug(
			"compat_ion_ioctl : REARALS_IOCTL_XXX command is 0x%x\n",
			cmd);
		return filp->f_op->unlocked_ioctl(
			filp, cmd, (unsigned long)compat_ptr(arg));
	}
	default:
		pr_err("compat_ion_ioctl : No such command!! 0x%x\n", cmd);
		return -ENOIOCTLCMD;
	}
}
#endif
/*----------------------------------------------------------------------------*/
static const struct file_operations _rearals_factory_fops = {
	.open = rearals_factory_open,
	.release = rearals_factory_release,
	.unlocked_ioctl = rearals_factory_unlocked_ioctl,
#if IS_ENABLED(CONFIG_COMPAT)
	.compat_ioctl = compat_rearals_factory_unlocked_ioctl,
#endif
};

static struct miscdevice _rearals_factory_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rearals",
	.fops = &_rearals_factory_fops,
};

int rearals_factory_device_register(struct rearals_factory_public *dev)
{
	int err = 0;
	printk("wnn add %s\n", __func__);
	if (!dev || !dev->fops)
		return -1;
	rearals_factory.gain = dev->gain;
	rearals_factory.sensitivity = dev->sensitivity;
	rearals_factory.fops = dev->fops;
	err = misc_register(&_rearals_factory_device);
	if (err) {
		pr_err("rearals_factory_device register failed\n");
		err = -1;
	}
	return err;
}

int rearals_factory_device_deregister(struct rearals_factory_public *dev)
{
	rearals_factory.fops = NULL;
	misc_deregister(&_rearals_factory_device);
	return 0;
}
