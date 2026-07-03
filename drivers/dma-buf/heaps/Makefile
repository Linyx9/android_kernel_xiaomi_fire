# SPDX-License-Identifier: GPL-2.0
ifneq ($(KERNELRELEASE),)

ifneq ($(DEVICE_MODULES_PATH),)
LINUXINCLUDE := -I$(DEVICE_MODULES_PATH)/include $(LINUXINCLUDE)
endif

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/dma-buf/heaps

ifneq ($(COMMON_OUT_DIR),)
DEVICE_MODULES_SYMVERS := $(COMMON_OUT_DIR)/$(notdir $(DEVICE_MODULES_PATH))
KBUILD_EXTRA_SYMBOLS += $(wildcard $(DEVICE_MODULES_SYMVERS)/Module.symvers)
endif

# system heap
system_heap-objs := \
	mtk_system_heap.o \
	mtk_page_pool.o \
	mtk_cache_pool.o \
	mtk_dmabuf_monitor.o \
	mtk-deferred-free-helper.o

obj-$(CONFIG_DEVICE_MODULES_DMABUF_HEAPS_DEFERRED_FREE)	+= deferred-free-helper.o
obj-$(CONFIG_DEVICE_MODULES_DMABUF_HEAPS_PAGE_POOL)	+= page_pool.o
obj-$(CONFIG_DEVICE_MODULES_DMABUF_HEAPS_SYSTEM)	+= system_heap.o
obj-$(CONFIG_DEVICE_MODULES_DMABUF_HEAPS_REFILL)	+= mtk_heap_refill.o

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/iommu/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/trusted_mem/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/monitor_hang/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ifndef CONFIG_GRT_HYPERVISOR
obj-$(CONFIG_MTK_TRUSTED_MEMORY_SUBSYSTEM)	+= mtk_sec_heap.o
endif
obj-$(CONFIG_DMABUF_HEAPS_DEBUG) += mtk_heap_debug.o

else
KERNELDIR ?= $(if $(KERNEL_SRC),$(KERNEL_SRC),/lib/modules/$(shell uname -r)/build)
M ?= $(shell pwd)

default: modules
modules modules_install:
	$(MAKE) -C $(KERNELDIR) O=$(O) M=$(M) $@
endif
