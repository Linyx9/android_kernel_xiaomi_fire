#
# Copyright (C) 2016 MediaTek Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See http://www.gnu.org/licenses/gpl-2.0.html for more details.
#

ifneq ($(KERNELRELEASE),)

ifeq ($(CONFIG_MTK_GCOV_KERNEL),y)
GCOV_PROFILE := y
endif

subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mtprof/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/aee/mrdump/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/aee/
subdir-ccflags-y += -I$(srctree)/kernel/
subdir-ccflags-y += -I$(srctree)/drivers/android/

ccflags-y += -DTEXT_OFFSET=$(TEXT_OFFSET)
obj-$(CONFIG_MTK_AEE_IPANIC)	+= mrdump.o

mrdump-y := \
	mboot_params.o \
	mrdump_control.o \
	mrdump_mini.o \
	mrdump_panic.o \
	mrdump_helper.o

mrdump-$(CONFIG_ARM) += mrdump_arm.o

mrdump-$(CONFIG_ARM64) += \
	mrdump_arm64.o

KASAN_SANITIZE := n

else
KERNELDIR ?= $(if $(KERNEL_SRC),$(KERNEL_SRC),/lib/modules/$(shell uname -r)/build)
M ?= $(shell pwd)

default: modules
modules modules_install:
	$(MAKE) -C $(KERNELDIR) O=$(O) M=$(M) $@
endif
