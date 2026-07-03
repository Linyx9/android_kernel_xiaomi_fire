# SPDX-License-Identifier: GPL-2.0
ifneq ($(KERNELRELEASE),)

ifneq ($(DEVICE_MODULES_PATH),)
LINUXINCLUDE := -I$(DEVICE_MODULES_PATH)/include $(LINUXINCLUDE)
endif

CFLAGS_core.o				:= -I$(srctree)/$(src)
mtk-icc-core-objs			:= core.o

obj-$(CONFIG_INTERCONNECT_MTK_EXTENSION)	+= mtk-icc-core.o

else
KERNELDIR ?= $(if $(KERNEL_SRC),$(KERNEL_SRC),/lib/modules/$(shell uname -r)/build)
M ?= $(shell pwd)

default: modules
modules modules_install:
	$(MAKE) -C $(KERNELDIR) O=$(O) M=$(M) $@
endif
