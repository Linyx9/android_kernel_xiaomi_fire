// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) "<REARFLK_FAC>" fmt

#include "rearflk_factory.h"

struct rearflk_factory_private {
	uint32_t gain;
	uint32_t sensitivity;
	struct rearflk_factory_fops *fops;
};

static struct rearflk_factory_private rearflk_factory;

static int rearflk_factory_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int rearflk_factory_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static long rearflk_factory_unlocked_ioctl(struct file *file, unsigned int cmd,
					unsigned long arg)
{
	long err = 0;
	void __user *ptr = (void __user *)arg;
	int32_t data_buf[3] = {0};
	struct SENSOR_DATA sensor_data = {0};
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
	case REARFLK_IOCTL_INIT:
		if (copy_from_user(&flag, ptr, sizeof(flag)))
			return -EFAULT;
		if (rearflk_factory.fops != NULL &&
		    rearflk_factory.fops->enable_sensor != NULL) {
			err = rearflk_factory.fops->enable_sensor(flag, 200);
			if (err < 0) {
				pr_err("REARFLK_IOCTL_INIT fail!\n");
				return -EINVAL;
			}
			pr_debug(
				"REARFLK_IOCTL_INIT, enable: %d, sample_period:%dms\n",
				flag, 200);
		} else {
			pr_err("REARFLK_IOCTL_INIT NULL\n");
			return -EINVAL;
		}
		return 0;
	case REARFLK_IOCTL_READ_SENSORDATA:
		if (rearflk_factory.fops != NULL &&
		    rearflk_factory.fops->get_data != NULL) {
			err = rearflk_factory.fops->get_data(data_buf);
			if (err < 0) {
				pr_err(
					"REARFLK_IOCTL_READ_SENSORDATA read data fail!\n");
				return -EINVAL;
			}
			pr_debug("REARFLK_IOCTL_READ_SENSORDATA: (%d, %d, %d)!\n",
				data_buf[0], data_buf[1], data_buf[2]);
			sensor_data.x = data_buf[0];
			sensor_data.y = data_buf[1];
			sensor_data.z = data_buf[2];
			if (copy_to_user(ptr, &sensor_data,
							sizeof(sensor_data)))
				return -EFAULT;
		} else {
			pr_err("REARFLK_IOCTL_READ_SENSORDATA NULL\n");
			return -EINVAL;
		}
		return 0;
	case REARFLK_IOCTL_ENABLE_CALI:
		if (rearflk_factory.fops != NULL &&
		    rearflk_factory.fops->enable_calibration != NULL) {
			err = rearflk_factory.fops->enable_calibration();
			if (err < 0) {
				pr_err(
					"REARFLK_IOCTL_ENABLE_CALI fail!\n");
				return -EINVAL;
			}
		} else {
			pr_err("REARFLK_IOCTL_ENABLE_CALI NULL\n");
			return -EINVAL;
		}
		return 0;
	case REARFLK_IOCTL_GET_CALI:
		if (rearflk_factory.fops != NULL &&
		    rearflk_factory.fops->get_cali != NULL) {
			err = rearflk_factory.fops->get_cali(data_buf);
			if (err < 0) {
				pr_err("REARFLK_IOCTL_GET_CALI FAIL!\n");
				return -EINVAL;
			}
		} else {
			pr_err("REARFLK_IOCTL_GET_CALI NULL\n");
			return -EINVAL;
		}

		pr_debug("REARFLK_IOCTL_GET_CALI: (%d, %d, %d)!\n",
			data_buf[0], data_buf[1], data_buf[2]);
		sensor_data.x = data_buf[0];
		sensor_data.y = data_buf[1];
		sensor_data.z = data_buf[2];
		if (copy_to_user(ptr, &sensor_data, sizeof(sensor_data)))
			return -EFAULT;
		return 0;
	default:
		pr_err("unknown IOCTL: 0x%08x\n", cmd);
		return -ENOIOCTLCMD;
	}
	return 0;
}

#if IS_ENABLED(CONFIG_COMPAT)
static long compat_rearflk_factory_unlocked_ioctl(struct file *filp,
					       unsigned int cmd,
					       unsigned long arg)
{
	if (!filp->f_op || !filp->f_op->unlocked_ioctl) {
		pr_err(
			"compat_ion_ioctl file has no f_op or no f_op->unlocked_ioctl.\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case COMPAT_REARFLK_IOCTL_INIT:
	case COMPAT_REARFLK_IOCTL_READ_SENSORDATA:
	case COMPAT_REARFLK_IOCTL_ENABLE_CALI:
	case COMPAT_REARFLK_IOCTL_GET_CALI: {
		pr_debug(
			"compat_ion_ioctl : REARFLK_IOCTL_XXX command is 0x%x\n",
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
static const struct file_operations _rearflk_factory_fops = {
	.open = rearflk_factory_open,
	.release = rearflk_factory_release,
	.unlocked_ioctl = rearflk_factory_unlocked_ioctl,
#if IS_ENABLED(CONFIG_COMPAT)
	.compat_ioctl = compat_rearflk_factory_unlocked_ioctl,
#endif
};

static struct miscdevice _rearflk_factory_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rearflk",
	.fops = &_rearflk_factory_fops,
};

int rearflk_factory_device_register(struct rearflk_factory_public *dev)
{
	int err = 0;

	printk("wnn add %s\n", __func__);
	if (!dev || !dev->fops)
		return -1;
	rearflk_factory.gain = dev->gain;
	rearflk_factory.sensitivity = dev->sensitivity;
	rearflk_factory.fops = dev->fops;
	err = misc_register(&_rearflk_factory_device);
	if (err) {
		pr_err("rearflk_factory_device register failed\n");
		err = -1;
	}
	return err;
}

int rearflk_factory_device_deregister(struct rearflk_factory_public *dev)
{
	rearflk_factory.fops = NULL;
	misc_deregister(&_rearflk_factory_device);
	return 0;
}
