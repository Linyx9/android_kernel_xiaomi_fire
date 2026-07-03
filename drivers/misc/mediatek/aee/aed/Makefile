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

ifneq ($(KERNELRELEASE),)

ifeq ($(CONFIG_MTK_GCOV_KERNEL),y)
GCOV_PROFILE := y
endif

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/aee/mrdump/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/aee/aed/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/aee/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmp/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/log_store/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/

obj-$(CONFIG_MTK_AEE_AED)	+= aee_aed.o
aee_aed-y	+= \
	aed-main.o \
	aed-debug.o \
	aee-common.o \
	slog.o

obj-$(CONFIG_MTK_AEE_FEATURE)	+= aee_rs.o

else
KERNELDIR ?= $(if $(KERNEL_SRC),$(KERNEL_SRC),/lib/modules/$(shell uname -r)/build)
M ?= $(shell pwd)

default: modules
modules modules_install:
	$(MAKE) -C $(KERNELDIR) O=$(O) M=$(M) $@
endif
