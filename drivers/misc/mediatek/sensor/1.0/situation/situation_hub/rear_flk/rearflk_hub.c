// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) "[rearflk] " fmt

#include <hwmsensor.h>
#include "rearflk_hub.h"
#include <situation.h>
#include <SCP_sensorHub.h>
#include <linux/notifier.h>
#include "include/scp.h"
#include "rearflk_factory.h"

static struct situation_init_info rearflk_init_info;
static DEFINE_SPINLOCK(calibration_lock);
struct rearflk_ipi_data {
	bool factory_enable;

	int32_t cali_data[3];
	int8_t cali_status;
	struct completion calibration_done;
};
static struct rearflk_ipi_data *obj_ipi_data;

static int rearflk_factory_enable_sensor(bool enabledisable,
					 int64_t sample_periods_ms)
{
	int err = 0;
	struct rearflk_ipi_data *obj = obj_ipi_data;

	if (enabledisable == true)
		WRITE_ONCE(obj->factory_enable, true);
	else
		WRITE_ONCE(obj->factory_enable, false);
	if (enabledisable == true) {
		err = sensor_set_delay_to_hub(ID_REAR_FLICKER,
					      sample_periods_ms);
		if (err) {
			pr_err("sensor_set_delay_to_hub failed!\n");
			return -1;
		}
	}
	err = sensor_enable_to_hub(ID_REAR_FLICKER, enabledisable);
	if (err) {
		pr_err("sensor_enable_to_hub failed!\n");
		return -1;
	}
	return 0;
}

static int rearflk_factory_get_data(int32_t sensor_data[3])
{
	int err = 0;
	struct data_unit_t data;

	err = sensor_get_data_from_hub(ID_REAR_FLICKER, &data);
	if (err < 0) {
		pr_err_ratelimited("sensor_get_data_from_hub fail!!\n");
		return -1;
	}
	sensor_data[0] = data.rearflk;

	return err;
}

static int rearflk_factory_enable_calibration(void)
{
	return sensor_calibration_to_hub(ID_REAR_FLICKER);
}

static int rearflk_factory_get_cali(int32_t data[3])
{
	int err = 0;
	struct rearflk_ipi_data *obj = obj_ipi_data;
	int8_t status = 0;

	err = wait_for_completion_timeout(&obj->calibration_done,
					  msecs_to_jiffies(3000));
	if (!err) {
		pr_err("rearflk factory get cali fail!\n");
		return -1;
	}
	spin_lock(&calibration_lock);
	data[0] = obj->cali_data[0];
	data[1] = obj->cali_data[1];
	data[2] = obj->cali_data[2];
	status = obj->cali_status;
	spin_unlock(&calibration_lock);
	if (status != 0) {
		pr_debug("rearflk cali fail!\n");
		return -2;
	}
	return 0;
}


static struct rearflk_factory_fops rearflk_factory_fops = {
	.enable_sensor = rearflk_factory_enable_sensor,
	.get_data = rearflk_factory_get_data,
	.enable_calibration = rearflk_factory_enable_calibration,
	.get_cali = rearflk_factory_get_cali,
};

static struct rearflk_factory_public rearflk_factory_device = {
	.gain = 1,
	.sensitivity = 1,
	.fops = &rearflk_factory_fops,
};

static int rearflk_get_data(int *probability, int *status)
{
	int err = 0;
	struct data_unit_t data;
	uint64_t time_stamp = 0;

	err = sensor_get_data_from_hub(ID_REAR_FLICKER, &data);
	if (err < 0) {
		pr_err_ratelimited("sensor_get_data_from_hub fail!!\n");
		return -1;
	}
	time_stamp		= data.time_stamp;
	*probability	= data.rearflk;
	return 0;
}
static int rearflk_open_report_data(int open)
{
	int ret = 0;
#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	if (open == 1)
		ret = sensor_set_delay_to_hub(ID_REAR_FLICKER, 120);
#elif defined CONFIG_NANOHUB

#else

#endif
	ret = sensor_enable_to_hub(ID_REAR_FLICKER, open);
	return ret;
}
static int rearflk_batch(int flag,
	int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	return sensor_batch_to_hub(ID_REAR_FLICKER,
		flag, samplingPeriodNs, maxBatchReportLatencyNs);
}

static int rearflk_flush(void)
{
	return sensor_flush_to_hub(ID_REAR_FLICKER);
}

static int rearflk_recv_data(struct data_unit_t *event, void *reserved)
{
	struct rearflk_ipi_data *obj = obj_ipi_data;
	int err = 0;

	if (event->flush_action == FLUSH_ACTION)
		err = situation_flush_report(ID_REAR_FLICKER);
	else if (event->flush_action == DATA_ACTION) {
		printk("rearflk recv data: %d\n", event->rearflk);
		err = situation_data_report_t(ID_REAR_FLICKER, event->rearflk, (int64_t)event->time_stamp);
	} else if (event->flush_action == CALI_ACTION) {
		spin_lock(&calibration_lock);
		obj->cali_data[0] = event->rearflk;
		spin_unlock(&calibration_lock);
		complete(&obj->calibration_done);
	}
	return err;
}

static int rearflk_local_init(void)
{
	struct situation_control_path ctl = {0};
	struct situation_data_path data = {0};
	int err = 0;

	struct rearflk_ipi_data *obj;

	pr_debug("%s\n", __func__);
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		err = -ENOMEM;
		goto exit;
	}

	memset(obj, 0, sizeof(*obj));
	obj_ipi_data = obj;
	WRITE_ONCE(obj->factory_enable, false);
	init_completion(&obj->calibration_done);

	ctl.open_report_data = rearflk_open_report_data;
	ctl.batch = rearflk_batch;
	ctl.flush = rearflk_flush;
	ctl.is_support_wake_lock = true;
	ctl.is_support_batch = false;
	err = situation_register_control_path(&ctl, ID_REAR_FLICKER);
	if (err) {
		pr_err("register rearflk control path err\n");
		goto exit;
	}

	data.get_data = rearflk_get_data;
	err = situation_register_data_path(&data, ID_REAR_FLICKER);
	if (err) {
		pr_err("register rearflk data path err\n");
		goto exit;
	}

	err = rearflk_factory_device_register(&rearflk_factory_device);
	if (err) {
		pr_err("rearflk_factory_device register failed\n");
		goto exit;
	}

	err = scp_sensorHub_data_registration(ID_REAR_FLICKER,
		rearflk_recv_data);
	if (err) {
		pr_err("SCP_sensorHub_data_registration fail!!\n");
		goto exit;
	}
	return 0;
exit:
	return -1;
}
static int rearflk_local_uninit(void)
{
	return 0;
}

static struct situation_init_info rearflk_init_info = {
	.name = "rearflk_hub",
	.init = rearflk_local_init,
	.uninit = rearflk_local_uninit,
};

int __init rearflk_init(void)
{
	printk("wnn add %s\n", __func__);
	situation_driver_add(&rearflk_init_info, ID_REAR_FLICKER);
	return 0;
}

void __exit rearflk_exit(void)
{
	pr_debug("%s\n", __func__);
}

