// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 */

#include "situation_hub.h"

static int __init situation_hub_init(void)
{
#if IS_ENABLED(CONFIG_MTK_INPKHUB)
	inpocket_init();
#endif

#if IS_ENABLED(CONFIG_MTK_STATHUB)
	stat_init();
#endif

#if IS_ENABLED(CONFIG_MTK_WAKEHUB)
	wakehub_init();
#endif

#if IS_ENABLED(CONFIG_MTK_GLGHUB)
	glghub_init();
#endif

#if IS_ENABLED(CONFIG_MTK_PICKUPHUB)
	pkuphub_init();
#endif

#if IS_ENABLED(CONFIG_MTK_ANSWER_CALL_HUB)
	ancallhub_init();
#endif

#if IS_ENABLED(CONFIG_MTK_DEVICE_ORIENTATION_HUB)
	device_orientation_init();
#endif

#if IS_ENABLED(CONFIG_MTK_MOTION_DETECT_HUB)
	motion_detect_init();
#endif

#if IS_ENABLED(CONFIG_MTK_TILTDETECTHUB)
	tiltdetecthub_init();
#endif

#if IS_ENABLED(CONFIG_MTK_FLAT_HUB)
	flat_init();
#endif

#if IS_ENABLED(CONFIG_MTK_SAR_HUB)
	sarhub_init();
#endif

#if IS_ENABLED(CONFIG_MTK_REAR_ALS_HUB)
	rearals_init();
#endif

#if IS_ENABLED(CONFIG_MTK_REAR_FLK_HUB)
	rearflk_init();
#endif

//TN Begin modified by bingtai.zou/860558 20220823 EKFOGO4G-1550 END
#if IS_ENABLED(CONFIG_MTK_FLIPTWIST_HUB)
	flip_twist_hub_init();
#endif

#if IS_ENABLED(CONFIG_MTK_CHOPCHOP_HUB)
	chop_chop_hub_init();
#endif

#if IS_ENABLED(CONFIG_MTK_SIGMOVE_HUB)
	sig_move_hub_init();
#endif

#if IS_ENABLED(CONFIG_MTK_FLIP_HUB)
	flip_hub_init();
#endif
#if IS_ENABLED(CONFIG_MTK_TAP_TAP_HUB)
	tap_tap_hub_init();
#endif
/*TN Begin modified by jiawei.zou 20220825 EKLAMU-207 end*/

	return 0;
}

static void __exit situation_hub_exit(void)
{
#if IS_ENABLED(CONFIG_MTK_INPKHUB)
	inpocket_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_STATHUB)
	stat_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_WAKEHUB)
	wakehub_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_GLGHUB)
	glghub_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_PICKUPHUB)
	pkuphub_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_ANSWER_CALL_HUB)
	ancallhub_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_DEVICE_ORIENTATION_HUB)
	device_orientation_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_MOTION_DETECT_HUB)
	motion_detect_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_TILTDETECTHUB)
	tiltdetecthub_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_FLAT_HUB)
	flat_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_SAR_HUB)
	sarhub_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_REAR_ALS_HUB)
	rearals_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_REAR_FLK_HUB)
	rearflk_exit();
#endif

/*TN Begin modified by jiawei.zou 20220825 EKLAMU-207 start*/
#if IS_ENABLED(CONFIG_MTK_FLIPTWIST_HUB)
	flip_twist_hub_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_CHOPCHOP_HUB)
	chop_chop_hub_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_SIGMOVE_HUB)
	sig_move_hub_exit();
#endif

#if IS_ENABLED(CONFIG_MTK_FLIP_HUB)
	flip_hub_exit();
#endif
#if IS_ENABLED(CONFIG_MTK_TAP_TAP_HUB)
	tap_tap_hub_exit();
#endif
/*TN Begin modified by jiawei.zou 20220825 EKLAMU-207 end*/

}

module_init(situation_hub_init);
module_exit(situation_hub_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("situtation hub driver");
MODULE_AUTHOR("Mediatek");

