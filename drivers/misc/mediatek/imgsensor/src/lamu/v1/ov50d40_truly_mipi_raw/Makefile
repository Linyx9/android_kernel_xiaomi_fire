#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

IMGSENSOR_DRIVER_OBJ += \
	$(subdrv-rpath)/ov50d40_truly_mipi_raw.o

ifeq ($(CONFIG_MTK_IMGSENSOR_ISP4_T), m)
obj-$(CONFIG_MTK_IMGSENSOR_ISP4_T) += $(MTK_PLATFORM_KO).o
$(eval $(MTK_PLATFORM_KO)-objs += $(IMGSENSOR_DRIVER_OBJ))
else ifeq ($(CONFIG_MTK_IMGSENSOR_ISP4_T), y)
obj-$(CONFIG_MTK_IMGSENSOR_ISP4_T) += $(IMGSENSOR_DRIVER_OBJ)
endif
