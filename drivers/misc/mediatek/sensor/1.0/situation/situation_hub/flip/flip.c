// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) "[flip] " fmt

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/atomic.h>

#include <hwmsensor.h>
#include <sensors_io.h>
#include "flip.h"
#include "situation.h"

#include <hwmsen_helper.h>

#include <SCP_sensorHub.h>
#include <linux/notifier.h>
#include "include/scp.h"

static struct situation_init_info flip_hub_init_info;

//TN Begin modified by jiawei.zou 20241016 for flip onchanged report Begin
static int last_data = -1;
static int flip_data_report(int32_t value,int64_t time_stamp)
{
	pr_err("zjw enter %s value =%d last_data = %d !!\n",__func__,value,last_data);
	int err =0;
	if(value != last_data){
		err = situation_data_report_t(ID_FLIP, value, time_stamp);
		last_data = value;
	}
	//pr_err("%s last_data = %d \n",__func__,last_data);
	return err;
}
//TN Begin modified by jiawei.zou 20241016 for flip onchanged report End

static int flip_get_data(int *probability, int *status)
{
	int err = 0;
	struct data_unit_t data;
	uint64_t time_stamp = 0;

	err = sensor_get_data_from_hub(ID_FLIP, &data);
	if (err < 0) {
		pr_err("sensor_get_data_from_hub fail!!\n");
		return -1;
	}
	time_stamp = data.time_stamp;
	*probability = data.gesture_data_t.probability;
	return 0;
}
static int flip_open_report_data(int open)
{
	int ret = 0;

#if IS_ENABLED(CONFIG_MTK_SCP_SENSORHUB_V1)
	if (open == 1)
		ret = sensor_set_delay_to_hub(ID_FLIP, 120);
#elif defined CONFIG_NANOHUB

#else

#endif
	if(!open)
		last_data = -1;//disable reset to -1
	ret = sensor_enable_to_hub(ID_FLIP, open);
	return ret;
}

static int flip_batch(int flag,
	int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	return sensor_batch_to_hub(ID_FLIP,
		flag, samplingPeriodNs, maxBatchReportLatencyNs);
}

static int flip_flush(void)
{
	  pr_err("zjw enter flip_flush");
	return sensor_flush_to_hub(ID_FLIP);
}

static int flip_recv_data(struct data_unit_t *event,
	void *reserved)
{
	int err = 0;

	if (event->flush_action == FLUSH_ACTION)
	{
          err = situation_flush_report(ID_FLIP);
	  pr_err("zjw flip_flush recv");
	}
	else if (event->flush_action == DATA_ACTION)
		err = flip_data_report(event->flip_t.state,
			(int64_t)event->time_stamp);
	return err;
}

static int flip_hub_local_init(void)
{
	struct situation_control_path ctl = {0};
	struct situation_data_path data = {0};
	int err = 0;

	ctl.open_report_data = flip_open_report_data;
	ctl.batch = flip_batch;
	ctl.flush = flip_flush;
	ctl.is_support_wake_lock = true;
	err = situation_register_control_path(&ctl, ID_FLIP);
	if (err) {
		pr_err("register flip control path err\n");
		goto exit;
	}

	data.get_data = flip_get_data;
	err = situation_register_data_path(&data, ID_FLIP);
	if (err) {
		pr_err("register flip data path err\n");
		goto exit;
	}
	err = scp_sensorHub_data_registration(ID_FLIP,
		flip_recv_data);
	if (err) {
		pr_err("SCP_sensorHub_data_registration fail!!\n");
		goto exit_create_attr_failed;
	}
	return 0;
exit:
exit_create_attr_failed:
	return -1;
}
static int flip_hub_local_uninit(void)
{
	return 0;
}

static struct situation_init_info flip_hub_init_info = {
	.name = "flip_hub",
	.init = flip_hub_local_init,
	.uninit = flip_hub_local_uninit,
};

int __init flip_hub_init(void)
{
	situation_driver_add(&flip_hub_init_info, ID_FLIP);
	return 0;
}

void __exit flip_hub_exit(void)
{
	pr_err("%s\n", __func__);
}


