// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) "[rearals] " fmt

#include <hwmsensor.h>
#include "rearals_hub.h"
#include <situation.h>
#include <SCP_sensorHub.h>
#include <linux/notifier.h>
#include "include/scp.h"
#include "rearals_factory.h"

static struct situation_init_info rearals_init_info;
static DEFINE_SPINLOCK(calibration_lock);
struct rearals_ipi_data {
	bool factory_enable;

	int32_t	rearals_cali;
	struct completion calibration_done;
};
static struct rearals_ipi_data *obj_ipi_data;

int rearals_factory_enable_sensor(bool enabledisable,
					 int64_t sample_periods_ms)
{
	int err = 0;
	struct rearals_ipi_data *obj = obj_ipi_data;

	if (enabledisable == true)
		WRITE_ONCE(obj->factory_enable, true);
	else
		WRITE_ONCE(obj->factory_enable, false);
	if (enabledisable == true) {
		err = sensor_set_delay_to_hub(ID_REAR_ALS,
					      sample_periods_ms);
		if (err) {
			pr_err("sensor_set_delay_to_hub failed!\n");
			return -1;
		}
	}
	err = sensor_enable_to_hub(ID_REAR_ALS, enabledisable);
	if (err) {
		pr_err("sensor_enable_to_hub failed!\n");
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(rearals_factory_enable_sensor);

int rearals_factory_get_enable(void)
{
	struct rearals_ipi_data *obj = obj_ipi_data;
	return READ_ONCE(obj->factory_enable);
}
EXPORT_SYMBOL(rearals_factory_get_enable);

int rearals_factory_get_data(int32_t sensor_data[3])
{
	int err = 0;
	struct data_unit_t data;

	err = sensor_get_data_from_hub(ID_REAR_ALS, &data);
	if (err < 0) {
		pr_err_ratelimited("sensor_get_data_from_hub fail!!\n");
		return -1;
	}
	printk("lux:%d, ir:%d, clear:%d\n", data.rearals.als_lux, data.rearals.als_raw_data, data.rearals.ir_clr_data);
	sensor_data[0] = data.rearals.als_lux;
	sensor_data[1] = data.rearals.als_raw_data;
	sensor_data[2] = data.rearals.ir_clr_data;

	return err;
}
EXPORT_SYMBOL(rearals_factory_get_data);

static int rearals_factory_enable_calibration(void)
{
	return sensor_calibration_to_hub(ID_REAR_ALS);
}

/* +20240617 wnn add mtk sensor 1.0 flicker support start */
void rearals_set_caliobj_offset(int32_t offset)
{
	struct rearals_ipi_data *obj = obj_ipi_data;
	if (obj) {
		pr_err("erals_set_caliobj_offset %d\n", offset);
		obj->rearals_cali = offset;
	}
}
EXPORT_SYMBOL(rearals_set_caliobj_offset);
/* -20240617 wnn add mtk sensor 1.0 flicker support end */

static int rearals_factory_get_cali(int32_t *offset)
{
//	int err = 0; //TN modified by jiawei.zou 20240802 for rearals_cali
	struct rearals_ipi_data *obj = obj_ipi_data;


//TN Begin modified by jiawei.zou 20240802 for rearals_cali
/*
	err = wait_for_completion_timeout(&obj->calibration_done,
					  msecs_to_jiffies(3000));
	if (!err) {
		pr_err("rearals factory get cali fail!\n");
		return -1;
	}
*/
//TN End modified by jiawei.zou 20240802 for rearals_cali
	spin_lock(&calibration_lock);
	*offset = obj->rearals_cali;
	spin_unlock(&calibration_lock);
	return 0;
}

static int rearals_factory_set_cali(int32_t offset)
{
	struct rearals_ipi_data *obj = obj_ipi_data;
	int err = 0;
	int32_t cfg_data;

	spin_lock(&calibration_lock);
	cfg_data = offset;
	err = sensor_cfg_to_hub(ID_REAR_ALS,
		(uint8_t *)&cfg_data, sizeof(cfg_data));
	if (err < 0)
		pr_err("sensor_cfg_to_hub fail\n");
	obj->rearals_cali = offset;
	spin_unlock(&calibration_lock);
	rearals_cali_report(&cfg_data);//TN modified by jiawei.zou 20240802 for rearals_cali

	return err;

}

static struct rearals_factory_fops rearals_factory_fops = {
	.enable_sensor = rearals_factory_enable_sensor,
	.get_data = rearals_factory_get_data,
	.enable_calibration = rearals_factory_enable_calibration,
	.rearals_get_cali = rearals_factory_get_cali,
	.rearals_set_cali = rearals_factory_set_cali,
};

static struct rearals_factory_public rearals_factory_device = {
	.gain = 1,
	.sensitivity = 1,
	.fops = &rearals_factory_fops,
};

static int rearals_get_data(int *probability, int *status)
{
	int err = 0;
	struct data_unit_t data;
	uint64_t time_stamp = 0;

	err = sensor_get_data_from_hub(ID_REAR_ALS, &data);
	if (err < 0) {
		pr_err_ratelimited("sensor_get_data_from_hub fail!!\n");
		return -1;
	}
	time_stamp		= data.time_stamp;
	*probability	= data.rearals.als_lux;
	return 0;
}
static int rearals_open_report_data(int open)
{
	int ret = 0;
#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	if (open == 1)
		ret = sensor_set_delay_to_hub(ID_REAR_ALS, 120);
#elif defined CONFIG_NANOHUB

#else

#endif
	ret = sensor_enable_to_hub(ID_REAR_ALS, open);
	return ret;
}
static int rearals_batch(int flag,
	int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	return sensor_batch_to_hub(ID_REAR_ALS,
		flag, samplingPeriodNs, maxBatchReportLatencyNs);
}

static int rearals_flush(void)
{
	return sensor_flush_to_hub(ID_REAR_ALS);
}

static int rearals_recv_data(struct data_unit_t *event, void *reserved)
{
	struct rearals_ipi_data *obj = obj_ipi_data;
	int err = 0;

	if (event->flush_action == FLUSH_ACTION)
		err = situation_flush_report(ID_REAR_ALS);
	else if (event->flush_action == DATA_ACTION) {
		printk("rearals recv data: %d\n", event->rearals.als_lux);
		err = situation_data_report_t(ID_REAR_ALS, event->rearals.als_lux, (int64_t)event->time_stamp);
	} else if (event->flush_action == CALI_ACTION) {
		printk("rearals recv cali data: %d\n", event->data[0]);
		spin_lock(&calibration_lock);
		obj->rearals_cali = event->data[0];
		spin_unlock(&calibration_lock);
		rearals_cali_report(event->data);//TN modified by jiawei.zou 20240802 for rearals_cali
		complete(&obj->calibration_done);
	}
	return err;
}

static int rearals_local_init(void)
{
	struct situation_control_path ctl = {0};
	struct situation_data_path data = {0};
	int err = 0;

	struct rearals_ipi_data *obj;

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

	ctl.open_report_data = rearals_open_report_data;
	ctl.batch = rearals_batch;
	ctl.flush = rearals_flush;
	ctl.is_support_wake_lock = true;
	ctl.is_support_batch = false;
	err = situation_register_control_path(&ctl, ID_REAR_ALS);
	if (err) {
		pr_err("register rearals control path err\n");
		goto exit;
	}

	data.get_data = rearals_get_data;
	err = situation_register_data_path(&data, ID_REAR_ALS);
	if (err) {
		pr_err("register rearals data path err\n");
		goto exit;
	}

	err = rearals_factory_device_register(&rearals_factory_device);
	if (err) {
		pr_err("rearals_factory_device register failed\n");
		goto exit;
	}

	err = scp_sensorHub_data_registration(ID_REAR_ALS,
		rearals_recv_data);
	if (err) {
		pr_err("SCP_sensorHub_data_registration fail!!\n");
		goto exit;
	}
	return 0;
exit:
	return -1;
}
static int rearals_local_uninit(void)
{
	return 0;
}

static struct situation_init_info rearals_init_info = {
	.name = "rearals_hub",
	.init = rearals_local_init,
	.uninit = rearals_local_uninit,
};

int __init rearals_init(void)
{
	printk("wnn add %s\n", __func__);
	situation_driver_add(&rearals_init_info, ID_REAR_ALS);
	return 0;
}

void __exit rearals_exit(void)
{
	pr_debug("%s\n", __func__);
}

