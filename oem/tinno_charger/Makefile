
MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/pmic/include
subdir-ccflags-y += -Werror -I$(srctree)/drivers/misc/mediatek/usb20
subdir-ccflags-y += -Werror -I$(srctree)/drivers/power/supply
subdir-ccflags-y += -Werror -I$(srctree)/$(DEVICE_MODULES_REL_DIR)/oem/devinfo
subdir-ccflags-y += -Werror -I$(srctree)/$(DEVICE_MODULES_REL_DIR)/oem/tinno_charger
subdir-ccflags-y += -Werror -I$(srctree)/$(DEVICE_MODULES_REL_DIR)/drivers/misc/mediatek/typec/tcpc
subdir-ccflags-y += -Werror -I$(srctree)/$(DEVICE_MODULES_REL_DIR)/drivers/power/supply
subdir-ccflags-y += -Werror -I$(srctree)/$(DEVICE_MODULES_REL_DIR)/drivers/misc/mediatek/usb/usb20

obj-$(CONFIG_OEM_TINNO_CHARGER) += oem_tinno_charger.o
oem_tinno_charger-objs := tinno_charger.o

obj-$(CONFIG_OEM_SWITCH_CHARGER) += switch_charger/
obj-$(CONFIG_OEM_CHARGER_PUMP) += charger_pump/
obj-$(CONFIG_OEM_TCPC) += typec/
obj-$(CONFIG_OEM_QC_LOGIC) += qc_logic/
obj-$(CONFIG_OEM_TURBO_CHARGER) += turbo_charger/
obj-$(CONFIG_OEM_FUEL_GAUGE) += fuel_gauge/
obj-$(CONFIG_OEM_BATTERY_SWITCH) += battery_switch/
obj-$(CONFIG_OEM_TINNO_CHARGE_MISC) += charge_misc/
