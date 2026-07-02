/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  * Copyright (C) 2019 MediaTek Inc.
 *   */

#ifndef __TMP_BTS_USB_H__
#define __TMP_BTS_USB_H__

/* chip dependent */

#define APPLY_PRECISE_NTC_TABLE
#define APPLY_AUXADC_CALI_DATA

#define AUX_IN2_NTC (3)
/* 390K, pull up resister */
/* libei.guo modify for USB NTC 100K pull up resister */
#define BTSUSB_RAP_PULL_UP_R		100000
/* base on 100K NTC temp
 *  * default value -40 deg
 *   */
#define BTSUSB_TAP_OVER_CRITICAL_LOW	4397119
/* 1.8V ,pull up voltage */
#define BTSUSB_RAP_PULL_UP_VOLTAGE	1800
/* default is TSM0A103F34D1RZ(10K) */
#define BTSUSB_RAP_NTC_TABLE		7

#define BTSUSB_RAP_ADC_CHANNEL		AUX_IN2_NTC /* default is 2 */
extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int *rawdata);
extern int IMM_IsAdcInitReady(void);

#endif	/* __TMP_BTS_USB_H__ */
