ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sensor/1.0/hwmon/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sensor/1.0/situation
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sensor/1.0/sensorHub/inc_v1
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/

obj-$(CONFIG_MTK_SENSOR_SUPPORT)	+= situationhub.o
situationhub-y := situation_hub.o
situationhub-$(CONFIG_MTK_INPKHUB) += inpocket/inpocket.o
situationhub-$(CONFIG_MTK_STATHUB) += stationary/stationary.o
situationhub-$(CONFIG_MTK_WAKEHUB) += wake_gesture/wake_gesture.o
situationhub-$(CONFIG_MTK_GLGHUB) += glance_gesture/glance_gesture.o
situationhub-$(CONFIG_MTK_PICKUPHUB) += pickup_gesture/pickup_gesture.o
situationhub-$(CONFIG_MTK_ANSWER_CALL_HUB) += answercall/ancallhub.o
situationhub-$(CONFIG_MTK_DEVICE_ORIENTATION_HUB) += device_orientation/device_orientation.o
situationhub-$(CONFIG_MTK_MOTION_DETECT_HUB) += motion_detect/motion_detect.o
situationhub-$(CONFIG_MTK_TILTDETECTHUB) += tilt_detector/tiltdetecthub.o
situationhub-$(CONFIG_MTK_FLAT_HUB) += flat/flat.o
situationhub-$(CONFIG_MTK_SAR_HUB) += sar/sarhub.o sar/sar_factory.o
situationhub-$(CONFIG_MTK_REAR_ALS_HUB) += rear_als/rearals_hub.o rear_als/rearals_factory.o
situationhub-$(CONFIG_MTK_REAR_FLK_HUB) += rear_flk/rearflk_hub.o rear_flk/rearflk_factory.o
#TN Begin modified by jiawei.zou 20220825 EKLAMU-207 start
situationhub-$(CONFIG_MTK_FLIPTWIST_HUB) += flip_twist/flip_twist.o
situationhub-$(CONFIG_MTK_CHOPCHOP_HUB) += chop_chop/chop_chop.o
situationhub-$(CONFIG_MTK_SIGMOVE_HUB) += sig_move/sig_move.o
situationhub-$(CONFIG_MTK_FLIP_HUB) += flip/flip.o
situationhub-$(CONFIG_MTK_TAP_TAP_HUB) += tap_tap/tap_tap.o
#TN Begin modified by jiawei.zou 20220825 EKLAMU-207 end
