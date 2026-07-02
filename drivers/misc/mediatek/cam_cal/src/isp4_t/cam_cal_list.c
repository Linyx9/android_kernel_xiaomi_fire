// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/kernel.h>
#include "cam_cal_list.h"
#include "eeprom_i2c_common_driver.h"
#include "eeprom_i2c_custom_driver.h"
#include "kd_imgsensor.h"

struct stCAM_CAL_LIST_STRUCT g_camCalList[] = {
	/*Below is commom sensor */
	{S5KJNSSQ_SENSOR_ID, 0xA0, Common_read_region},
	{GC08A8_SENSOR_ID,   0x62, gc08a8_read_region},
	{OV08D10_SENSOR_ID,  0xA0, Common_read_region},
	{SC820CS_SENSOR_ID,  0x6C, sc820cs_sunwin_read_region},
	{SC820CS_SENSOR_ID,  0x20, sc820cs_sunwin_read_region},
	{GC05A2_SENSOR_ID,   0x7E, gc05a2sub_read_region},
	{SC520CS_SENSOR_ID,  0x6C, sc520cs_read_region},
	{GC08A8SPY_SENSOR_ID,0x62, gc08a8spy_read_region},
    /*  ADD before this line */
	{0, 0, 0}       /*end of list */
};

unsigned int cam_cal_get_sensor_list(
	struct stCAM_CAL_LIST_STRUCT **ppCamcalList)
{
	if (ppCamcalList == NULL)
		return 1;

	*ppCamcalList = &g_camCalList[0];
	return 0;
}


