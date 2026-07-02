// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include "kd_imgsensor.h"
#include "imgsensor_sensor_list.h"

/* Add Sensor Init function here
 * Note:
 * 1. Add by the resolution from ""large to small"", due to large sensor
 *    will be possible to be main sensor.
 *    This can avoid I2C error during searching sensor.
 * 2. This should be the same as
 *     mediatek\custom\common\hal\imgsensor\src\sensorlist.cpp
 */
struct IMGSENSOR_INIT_FUNC_LIST kdSensorList[MAX_NUM_OF_SUPPORT_SENSOR] = {
	/*lamu*/
#if defined(S5KJNSSQ_MIPI_RAW)
	{S5KJNSSQ_SENSOR_ID,
	SENSOR_DRVNAME_S5KJNSSQ_MIPI_RAW,
	S5KJNSSQ_MIPI_RAW_SensorInit},
#endif
#if defined(GC08A8_MIPI_RAW)
	{GC08A8_SENSOR_ID,
	SENSOR_DRVNAME_GC08A8_MIPI_RAW,
	GC08A8_MIPI_RAW_SensorInit},
#endif
#if defined(GC08A8SPY_MIPI_RAW)
	{GC08A8SPY_SENSOR_ID,
	SENSOR_DRVNAME_GC08A8SPY_MIPI_RAW,
	GC08A8SPY_MIPI_RAW_SensorInit},
#endif
#if defined(SC820CS_MIPI_RAW)
	{SC820CS_SENSOR_ID,
	SENSOR_DRVNAME_SC820CS_MIPI_RAW,
	SC820CS_MIPI_RAW_SensorInit},
#endif
#if defined(OV08D10_MIPI_RAW)
	{OV08D10_SENSOR_ID,
	SENSOR_DRVNAME_OV08D10_MIPI_RAW,
	OV08D10_MIPI_RAW_SensorInit},
#endif
#if defined(SC520CS_MIPI_RAW)
	{SC520CS_SENSOR_ID,
	SENSOR_DRVNAME_SC520CS_MIPI_RAW,
	SC520CS_MIPI_RAW_SensorInit},
#endif
#if defined(GC05A2_MIPI_RAW)
	{GC05A2_SENSOR_ID,
	SENSOR_DRVNAME_GC05A2_MIPI_RAW,
	GC05A2_MIPI_RAW_SensorInit},
#endif
	/*lamu*/

	/*  ADD sensor driver before this line */
	{0, {0}, NULL}, /* end of list */
};
/* e_add new sensor driver here */

