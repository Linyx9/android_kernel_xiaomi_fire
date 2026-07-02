/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __SITUTATION_HUB_H__
#define __SITUTATION_HUB_H__

#include <linux/module.h>

#if IS_ENABLED(CONFIG_MTK_INPKHUB)
#include "inpocket/inpocket.h"
#endif

#if IS_ENABLED(CONFIG_MTK_STATHUB)
#include "stationary/stationary.h"
#endif

#if IS_ENABLED(CONFIG_MTK_WAKEHUB)
#include "wake_gesture/wake_gesture.h"
#endif

#if IS_ENABLED(CONFIG_MTK_GLGHUB)
#include "glance_gesture/glance_gesture.h"
#endif

#if IS_ENABLED(CONFIG_MTK_PICKUPHUB)
#include "pickup_gesture/pickup_gesture.h"
#endif

#if IS_ENABLED(CONFIG_MTK_ANSWER_CALL_HUB)
#include "answercall/ancallhub.h"
#endif

#if IS_ENABLED(CONFIG_MTK_DEVICE_ORIENTATION_HUB)
#include "device_orientation/device_orientation.h"
#endif

#if IS_ENABLED(CONFIG_MTK_MOTION_DETECT_HUB)
#include "motion_detect/motion_detect.h"
#endif

#if IS_ENABLED(CONFIG_MTK_TILTDETECTHUB)
#include "tilt_detector/tiltdetecthub.h"
#endif

#if IS_ENABLED(CONFIG_MTK_FLAT_HUB)
#include "flat/flat.h"
#endif

#if IS_ENABLED(CONFIG_MTK_SAR_HUB)
#include "sar/sarhub.h"
#endif

#if IS_ENABLED(CONFIG_MTK_REAR_ALS_HUB)
#include "rear_als/rearals_hub.h"
#endif

#if IS_ENABLED(CONFIG_MTK_REAR_FLK_HUB)
#include "rear_flk/rearflk_hub.h"
#endif

/*TN Begin modified by jiawei.zou 20220825 EKLAMU-207 end*/
#if IS_ENABLED(CONFIG_MTK_FLIPTWIST_HUB)
#include "flip_twist/flip_twist.h"
#endif

#if IS_ENABLED(CONFIG_MTK_CHOPCHOP_HUB)
#include "chop_chop/chop_chop.h"
#endif

#if IS_ENABLED(CONFIG_MTK_SIGMOVE_HUB)
#include "sig_move/sig_move.h"
#endif

#if IS_ENABLED(CONFIG_MTK_FLIP_HUB)
#include "flip/flip.h"
#endif

#if IS_ENABLED(CONFIG_MTK_TAP_TAP_HUB)
#include "tap_tap/tap_tap.h"
#endif
/*TN Begin modified by jiawei.zou 20220825 EKLAMU-207 end*/

#endif
