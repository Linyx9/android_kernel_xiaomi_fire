
subdir-ccflags-y += -Werror -I$(srctree)/drivers/gpu/drm/panel
subdir-ccflags-y += -Werror -I$(srctree)/drivers/gpu/drm/mediatek/mediatek_v2/

obj-$(CONFIG_OEM_LAMU_PANEL) += ocp2131_i2c.o

ifdef CONFIG_OEM_PANEL
	obj-y += panel_boe_ili9883c_vdo/
	obj-y += panel_djin_ft8057s_vdo/
endif

ifdef CONFIG_OEM_LAMU_PANEL
	obj-y += dsi_panel_boe_icnl9922c_672_1080x2400_vdo_60hz/
	obj-y += dsi_panel_tm_td4376_672_1080x2400_vdo_60hz/
	obj-y += dsi_panel_djn_nt36672s_672_1080x2400_vdo_60hz/
	obj-y += dsi_panel_txd_ili9883c_667_720x1604_vdo_90hz/
	obj-y += dsi_panel_tm_nt36528a_667_720x1604_vdo_90hz/
	obj-y += dsi_panel_djn_td4160_667_720x1604_vdo_90hz/
endif