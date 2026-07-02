# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2020 MediaTek Inc.

IMGSENSOR_DRIVER_OBJ += \
         $(subdrv-rpath)/ov08d10_mipi_raw_Sensor.o

ifeq ($(CONFIG_MTK_IMGSENSOR_ISP4_T), m)
obj-$(CONFIG_MTK_IMGSENSOR_ISP4_T) += $(MTK_PLATFORM_KO).o
$(eval $(MTK_PLATFORM_KO)-objs += $(IMGSENSOR_DRIVER_OBJ))
else ifeq ($(CONFIG_MTK_IMGSENSOR_ISP4_T), y)
obj-$(CONFIG_MTK_IMGSENSOR_ISP4_T) += $(IMGSENSOR_DRIVER_OBJ)
endif

