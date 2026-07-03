#
# Copyright (C) 2023 MediaTek Inc.
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

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mme/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include

ifneq ($(COMMON_OUT_DIR),)
DEVICE_MODULES_SYMVERS := $(COMMON_OUT_DIR)/$(notdir $(DEVICE_MODULES_PATH))
KBUILD_EXTRA_SYMBOLS += $(wildcard $(DEVICE_MODULES_SYMVERS)/drivers/misc/mediatek/aee/mrdump/Module.symvers)
endif

mme-y := mmevent.o

obj-$(CONFIG_MMEVENT) += mme.o

else
KERNELDIR ?= $(if $(KERNEL_SRC),$(KERNEL_SRC),/lib/modules/$(shell uname -r)/build)
M ?= $(shell pwd)

default: modules
modules modules_install:
	$(MAKE) -C $(KERNELDIR) O=$(O) M=$(M) $@
endif
