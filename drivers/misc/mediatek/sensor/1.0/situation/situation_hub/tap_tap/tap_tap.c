// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) "[tap_tap] " fmt

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
#include "tap_tap.h"
#include "situation.h"

#include <hwmsen_helper.h>

#include <SCP_sensorHub.h>
#include <linux/notifier.h>
#include "include/scp.h"

static struct situation_init_info tap_tap_hub_init_info;

static int tap_tap_get_data(int *probability, int *status)
{
	int err = 0;
	struct data_unit_t data;
	uint64_t time_stamp = 0;

	err = sensor_get_data_from_hub(ID_TAP_TAP, &data);
	if (err < 0) {
		pr_err("sensor_get_data_from_hub fail!!\n");
		return -1;
	}
	time_stamp = data.time_stamp;
	*probability = data.gesture_data_t.probability;
	return 0;
}

static int tap_tap_open_report_data(int open)
{
	int ret = 0;

#if IS_ENABLED(CONFIG_MTK_SCP_SENSORHUB_V1)
	if (open == 1)
		ret = sensor_set_delay_to_hub(ID_TAP_TAP, 120);
#elif defined CONFIG_NANOHUB

#else

#endif
	ret = sensor_enable_to_hub(ID_TAP_TAP, open);
	return ret;
}
/*native function
static int alshub_factory_set_cali(int32_t offset)
{
	struct alspshub_ipi_data *obj = obj_ipi_data;
	int err = 0;
	int32_t cfg_data;

	cfg_data = offset;
	err = sensor_cfg_to_hub(ID_LIGHT,
		(uint8_t *)&cfg_data, sizeof(cfg_data));
	if (err < 0)
		pr_err("sensor_cfg_to_hub fail\n");
	atomic_set(&obj->als_cali, offset);
	als_cali_report(&cfg_data);

	return err;

}*/

static int tap_tap_batch(int flag,
	int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	return sensor_batch_to_hub(ID_TAP_TAP,
		flag, samplingPeriodNs, maxBatchReportLatencyNs);
}

static int tap_tap_recv_data(struct data_unit_t *event,
	void *reserved)
{
	int err = 0;

	if (event->flush_action == FLUSH_ACTION)
		pr_err("tap_tap do not support flush\n");
	else if (event->flush_action == DATA_ACTION)
		err = situation_data_report_t(ID_TAP_TAP,event->taptap_t.state,
					(int64_t)event->time_stamp);
	else if(event->flush_action == CALI_ACTION){
		pr_err("taptap cali data = %d\n",event->data[0]);
		taptap_cali_report(event->data);
	}
	return err;
}

static int tap_tap_hub_local_init(void)
{
	struct situation_control_path ctl = {0};
	struct situation_data_path data = {0};
	int err = 0;

	ctl.open_report_data = tap_tap_open_report_data;
	ctl.batch = tap_tap_batch;
	ctl.is_support_wake_lock = true;
	err = situation_register_control_path(&ctl, ID_TAP_TAP);
	if (err) {
		pr_err("register tap_tap control path err\n");
		goto exit;
	}

	data.get_data = tap_tap_get_data;
	err = situation_register_data_path(&data, ID_TAP_TAP);
	if (err) {
		pr_err("register tap_tap data path err\n");
		goto exit;
	}
	err = scp_sensorHub_data_registration(ID_TAP_TAP,
		tap_tap_recv_data);
	if (err) {
		pr_err("SCP_sensorHub_data_registration fail!!\n");
		goto exit_create_attr_failed;
	}
	return 0;
exit:
exit_create_attr_failed:
	return -1;
}
static int tap_tap_hub_local_uninit(void)
{
	return 0;
}

static struct situation_init_info tap_tap_hub_init_info = {
	.name = "tap_tap_hub",
	.init = tap_tap_hub_local_init,
	.uninit = tap_tap_hub_local_uninit,
};

int __init tap_tap_hub_init(void)
{
	situation_driver_add(&tap_tap_hub_init_info, ID_TAP_TAP);
	return 0;
}

void __exit tap_tap_hub_exit(void)
{
	pr_err("%s\n", __func__);
}


