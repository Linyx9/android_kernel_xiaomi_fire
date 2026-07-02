// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <linux/delay.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include "../backlight_i2c_map.h"

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
//#include "../../../drivers/gpu/drm/mediatek/mediatek_v2/mtk_disp_aal.h"
#include "../../../drivers/gpu/drm/mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../../../drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../../../drivers/gpu/drm/mediatek/mediatek_v2/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include "../../devinfo/dev_info.h"
#endif

#define TINNO_LCM_OEM_CONFIG
#if defined(TINNO_LCM_OEM_CONFIG)
int djn_gesture_mode = 0;
EXPORT_SYMBOL(djn_gesture_mode);
#endif

void (*lcd_nvt_resume_nt36672s)(void);
EXPORT_SYMBOL(lcd_nvt_resume_nt36672s);
int nt36672s_lcd_id = 0;
EXPORT_SYMBOL(nt36672s_lcd_id);

int hbm;
bool is_hbm;
bool is_suspend;
int is_extra;
static unsigned char extra_buf[16] = {0};
static unsigned char hbm_buf[16] = {0};
struct djn *ptx;

struct djn {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vddio_gpio;
	struct gpio_desc *bias_pos, *bias_neg;
	struct gpio_desc *bl_en_gpio;

	bool prepared;
	bool enabled;

	int error;
};

#define djn_dcs_write_seq(ctx, seq...) \
({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	djn_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

#define djn_dcs_write_seq_static(ctx, seq...) \
({\
	static const u8 d[] = { seq };\
	djn_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static inline struct djn *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct djn, panel);
}

static void djn_dcs_write(struct djn *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

#ifdef PANEL_SUPPORT_READBACK
static int djn_dcs_read(struct djn *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %d reading dcs seq:(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void djn_panel_get_data(struct djn *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = djn_dcs_read(ctx,  0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
static struct regulator *disp_bias_pos;
static struct regulator *disp_bias_neg;


static int djn_panel_bias_regulator_init(void)
{
	static int regulator_inited;
	int ret = 0;

	if (regulator_inited)
		return ret;

	/* please only get regulator once in a driver */
	disp_bias_pos = regulator_get(NULL, "dsv_pos");
	if (IS_ERR(disp_bias_pos)) { /* handle return value */
		ret = PTR_ERR(disp_bias_pos);
		pr_info("get dsv_pos fail, error: %d\n", ret);
		return ret;
	}

	disp_bias_neg = regulator_get(NULL, "dsv_neg");
	if (IS_ERR(disp_bias_neg)) { /* handle return value */
		ret = PTR_ERR(disp_bias_neg);
		pr_info("get dsv_neg fail, error: %d\n", ret);
		return ret;
	}

	regulator_inited = 1;
	return ret; /* must be 0 */

}

static int djn_panel_bias_enable(void)
{
	int ret = 0;
	int retval = 0;

	djn_panel_bias_regulator_init();

	/* set voltage with min & max*/
	ret = regulator_set_voltage(disp_bias_pos, 5400000, 5400000);
	if (ret < 0)
		pr_info("set voltage disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_set_voltage(disp_bias_neg, 5400000, 5400000);
	if (ret < 0)
		pr_info("set voltage disp_bias_neg fail, ret = %d\n", ret);
	retval |= ret;

	/* enable regulator */
	ret = regulator_enable(disp_bias_pos);
	if (ret < 0)
		pr_info("enable regulator disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_enable(disp_bias_neg);
	if (ret < 0)
		pr_info("enable regulator disp_bias_neg fail, ret = %d\n", ret);
	retval |= ret;

	return retval;
}

static int djn_panel_bias_disable(void)
{
	int ret = 0;
	int retval = 0;

	djn_panel_bias_regulator_init();

	ret = regulator_disable(disp_bias_neg);
	if (ret < 0)
		pr_info("disable regulator disp_bias_neg fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_disable(disp_bias_pos);
	if (ret < 0)
		pr_info("disable regulator disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;

	return retval;
}
#endif

static void djn_panel_init(struct djn *ctx)
{
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return;
	}

	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(5 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(5 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(15 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X10);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);
	djn_dcs_write_seq_static(ctx, 0XB0, 0X00);
	djn_dcs_write_seq_static(ctx, 0XC0, 0X00);
	djn_dcs_write_seq_static(ctx, 0XC2, 0X00,0X00);
	djn_dcs_write_seq_static(ctx, 0XE9, 0X01);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X20);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);
	djn_dcs_write_seq_static(ctx, 0X01, 0X66);
	djn_dcs_write_seq_static(ctx, 0X07, 0X3C);
	djn_dcs_write_seq_static(ctx, 0X1B, 0X01);
	djn_dcs_write_seq_static(ctx, 0X69, 0XD0);
	djn_dcs_write_seq_static(ctx, 0X95, 0XE5);
	djn_dcs_write_seq_static(ctx, 0X96, 0XE5);
	djn_dcs_write_seq_static(ctx, 0XF2, 0X65);
	djn_dcs_write_seq_static(ctx, 0XF3, 0X54);
	djn_dcs_write_seq_static(ctx, 0XF4, 0X65);
	djn_dcs_write_seq_static(ctx, 0XF5, 0X54);
	djn_dcs_write_seq_static(ctx, 0XF6, 0X65);
	djn_dcs_write_seq_static(ctx, 0XF7, 0X54);
	djn_dcs_write_seq_static(ctx, 0XF8, 0X65);
	djn_dcs_write_seq_static(ctx, 0XF9, 0X54);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X21);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X24);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);
	djn_dcs_write_seq_static(ctx, 0X03, 0X17);
	djn_dcs_write_seq_static(ctx, 0X04, 0X15);
	djn_dcs_write_seq_static(ctx, 0X05, 0X13);
	djn_dcs_write_seq_static(ctx, 0X07, 0X2D);
	djn_dcs_write_seq_static(ctx, 0X08, 0X2C);
	djn_dcs_write_seq_static(ctx, 0X09, 0X2F);
	djn_dcs_write_seq_static(ctx, 0X0A, 0X2E);
	djn_dcs_write_seq_static(ctx, 0X0C, 0X24);
	djn_dcs_write_seq_static(ctx, 0X0D, 0X8C);
	djn_dcs_write_seq_static(ctx, 0X0F, 0X22);
	djn_dcs_write_seq_static(ctx, 0X11, 0X8B);
	djn_dcs_write_seq_static(ctx, 0X12, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X13, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X14, 0X29);
	djn_dcs_write_seq_static(ctx, 0X15, 0X1C);
	djn_dcs_write_seq_static(ctx, 0X16, 0X01);
	djn_dcs_write_seq_static(ctx, 0X17, 0XA3);
	djn_dcs_write_seq_static(ctx, 0X1B, 0X17);
	djn_dcs_write_seq_static(ctx, 0X1C, 0X15);
	djn_dcs_write_seq_static(ctx, 0X1D, 0X13);
	djn_dcs_write_seq_static(ctx, 0X1F, 0X2D);
	djn_dcs_write_seq_static(ctx, 0X20, 0X2C);
	djn_dcs_write_seq_static(ctx, 0X21, 0X2F);
	djn_dcs_write_seq_static(ctx, 0X22, 0X2E);
	djn_dcs_write_seq_static(ctx, 0X24, 0X24);
	djn_dcs_write_seq_static(ctx, 0X25, 0X8C);
	djn_dcs_write_seq_static(ctx, 0X27, 0X22);
	djn_dcs_write_seq_static(ctx, 0X29, 0X8B);
	djn_dcs_write_seq_static(ctx, 0X2A, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X2B, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X2D, 0X29);
	djn_dcs_write_seq_static(ctx, 0X2F, 0X1C);
	djn_dcs_write_seq_static(ctx, 0X30, 0X01);
	djn_dcs_write_seq_static(ctx, 0X31, 0XA3);
	djn_dcs_write_seq_static(ctx, 0X32, 0X44);
	djn_dcs_write_seq_static(ctx, 0X33, 0X02);
	djn_dcs_write_seq_static(ctx, 0X34, 0X00);
	djn_dcs_write_seq_static(ctx, 0X35, 0X01);
	djn_dcs_write_seq_static(ctx, 0X36, 0X35);
	djn_dcs_write_seq_static(ctx, 0X37, 0X01);
	djn_dcs_write_seq_static(ctx, 0X38, 0X10);
	djn_dcs_write_seq_static(ctx, 0X3B, 0X04);
	djn_dcs_write_seq_static(ctx, 0X4E, 0X32);
	djn_dcs_write_seq_static(ctx, 0X4F, 0X32);
	djn_dcs_write_seq_static(ctx, 0X53, 0X32);
	djn_dcs_write_seq_static(ctx, 0X7A, 0X83);
	djn_dcs_write_seq_static(ctx, 0X7B, 0X8D);
	djn_dcs_write_seq_static(ctx, 0X7D, 0X04);
	djn_dcs_write_seq_static(ctx, 0X80, 0X04);
	djn_dcs_write_seq_static(ctx, 0X81, 0X04);
	djn_dcs_write_seq_static(ctx, 0X82, 0X13);
	djn_dcs_write_seq_static(ctx, 0X84, 0X31);
	djn_dcs_write_seq_static(ctx, 0X85, 0X13);
	djn_dcs_write_seq_static(ctx, 0X86, 0X22);
	djn_dcs_write_seq_static(ctx, 0X87, 0X31);
	djn_dcs_write_seq_static(ctx, 0X90, 0X13);
	djn_dcs_write_seq_static(ctx, 0X92, 0X31);
	djn_dcs_write_seq_static(ctx, 0X93, 0X13);
	djn_dcs_write_seq_static(ctx, 0X94, 0X22);
	djn_dcs_write_seq_static(ctx, 0X95, 0X31);
	djn_dcs_write_seq_static(ctx, 0X9C, 0XF4);
	djn_dcs_write_seq_static(ctx, 0X9D, 0X01);
	djn_dcs_write_seq_static(ctx, 0XA0, 0X0D);
	djn_dcs_write_seq_static(ctx, 0XA2, 0X0D);
	djn_dcs_write_seq_static(ctx, 0XA3, 0X03);
	djn_dcs_write_seq_static(ctx, 0XA4, 0X04);
	djn_dcs_write_seq_static(ctx, 0XA5, 0X04);
	djn_dcs_write_seq_static(ctx, 0XC4, 0X80);
	djn_dcs_write_seq_static(ctx, 0XC6, 0XC0);
	djn_dcs_write_seq_static(ctx, 0XC9, 0X00);
	djn_dcs_write_seq_static(ctx, 0XD9, 0X80);
	djn_dcs_write_seq_static(ctx, 0XDB, 0X18);
	djn_dcs_write_seq_static(ctx, 0XE9, 0X03);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X25);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);
	djn_dcs_write_seq_static(ctx, 0X0F, 0X1B);
	djn_dcs_write_seq_static(ctx, 0X18, 0X22);
	djn_dcs_write_seq_static(ctx, 0X19, 0XE4);
	djn_dcs_write_seq_static(ctx, 0X58, 0X0C);
	djn_dcs_write_seq_static(ctx, 0X59, 0X0A);
	djn_dcs_write_seq_static(ctx, 0X5C, 0X05);
	djn_dcs_write_seq_static(ctx, 0X5F, 0X10);
	djn_dcs_write_seq_static(ctx, 0X66, 0X50);
	djn_dcs_write_seq_static(ctx, 0X67, 0X15);
	djn_dcs_write_seq_static(ctx, 0X68, 0X58);
	djn_dcs_write_seq_static(ctx, 0X69, 0X10);
	djn_dcs_write_seq_static(ctx, 0X6B, 0X00);
	djn_dcs_write_seq_static(ctx, 0X6C, 0X1D);
	djn_dcs_write_seq_static(ctx, 0X71, 0X1D);
	djn_dcs_write_seq_static(ctx, 0X77, 0X62);
	djn_dcs_write_seq_static(ctx, 0X7E, 0X15);
	djn_dcs_write_seq_static(ctx, 0X7F, 0X00);
	djn_dcs_write_seq_static(ctx, 0X84, 0X6D);
	djn_dcs_write_seq_static(ctx, 0X8D, 0X00);
	djn_dcs_write_seq_static(ctx, 0XC0, 0XD5);
	djn_dcs_write_seq_static(ctx, 0XC1, 0X19);
	djn_dcs_write_seq_static(ctx, 0XC3, 0X01);
	djn_dcs_write_seq_static(ctx, 0XC4, 0X11);
	djn_dcs_write_seq_static(ctx, 0XC5, 0X11);
	djn_dcs_write_seq_static(ctx, 0XC6, 0X11);
	djn_dcs_write_seq_static(ctx, 0XEF, 0X00);
	djn_dcs_write_seq_static(ctx, 0XF0, 0X05);
	djn_dcs_write_seq_static(ctx, 0XF1, 0X04);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X26);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);
	djn_dcs_write_seq_static(ctx, 0X00, 0X00);
	djn_dcs_write_seq_static(ctx, 0X01, 0XF4);
	djn_dcs_write_seq_static(ctx, 0X02, 0XF4);
	djn_dcs_write_seq_static(ctx, 0X04, 0XF1);
	djn_dcs_write_seq_static(ctx, 0X05, 0X08);
	djn_dcs_write_seq_static(ctx, 0X06, 0X13);
	djn_dcs_write_seq_static(ctx, 0X07, 0X13);
	djn_dcs_write_seq_static(ctx, 0X08, 0X13);
	djn_dcs_write_seq_static(ctx, 0X14, 0X06);
	djn_dcs_write_seq_static(ctx, 0X15, 0X01);
	djn_dcs_write_seq_static(ctx, 0X74, 0XAF);
	djn_dcs_write_seq_static(ctx, 0X81, 0X0D);
	djn_dcs_write_seq_static(ctx, 0X83, 0X03);
	djn_dcs_write_seq_static(ctx, 0X84, 0X03);
	djn_dcs_write_seq_static(ctx, 0X85, 0X01);
	djn_dcs_write_seq_static(ctx, 0X86, 0X03);
	djn_dcs_write_seq_static(ctx, 0X87, 0X01);
	djn_dcs_write_seq_static(ctx, 0X88, 0X02);
	djn_dcs_write_seq_static(ctx, 0X8A, 0X1A);
	djn_dcs_write_seq_static(ctx, 0X8B, 0X11);
	djn_dcs_write_seq_static(ctx, 0X8C, 0X24);
	djn_dcs_write_seq_static(ctx, 0X8E, 0X42);
	djn_dcs_write_seq_static(ctx, 0X8F, 0X11);
	djn_dcs_write_seq_static(ctx, 0X90, 0X11);
	djn_dcs_write_seq_static(ctx, 0X91, 0X11);
	djn_dcs_write_seq_static(ctx, 0X9A, 0X80);
	djn_dcs_write_seq_static(ctx, 0X9B, 0X08);
	djn_dcs_write_seq_static(ctx, 0X9C, 0X00);
	djn_dcs_write_seq_static(ctx, 0X9D, 0X00);
	djn_dcs_write_seq_static(ctx, 0X9E, 0X00);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X27);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);
	djn_dcs_write_seq_static(ctx, 0X01, 0X60);
	djn_dcs_write_seq_static(ctx, 0X20, 0X81);
	djn_dcs_write_seq_static(ctx, 0X21, 0X70);
	djn_dcs_write_seq_static(ctx, 0X25, 0X81);
	djn_dcs_write_seq_static(ctx, 0X26, 0XAF);
	djn_dcs_write_seq_static(ctx, 0X6E, 0X9A);
	djn_dcs_write_seq_static(ctx, 0X6F, 0X78);
	djn_dcs_write_seq_static(ctx, 0X70, 0X00);
	djn_dcs_write_seq_static(ctx, 0X71, 0X00);
	djn_dcs_write_seq_static(ctx, 0X72, 0X00);
	djn_dcs_write_seq_static(ctx, 0X73, 0X00);
	djn_dcs_write_seq_static(ctx, 0X74, 0X00);
	djn_dcs_write_seq_static(ctx, 0X75, 0X00);
	djn_dcs_write_seq_static(ctx, 0X76, 0X00);
	djn_dcs_write_seq_static(ctx, 0X77, 0X00);
	djn_dcs_write_seq_static(ctx, 0X7D, 0X09);
	djn_dcs_write_seq_static(ctx, 0X7E, 0X69);
	djn_dcs_write_seq_static(ctx, 0X7F, 0X03);
	djn_dcs_write_seq_static(ctx, 0X80, 0X23);
	djn_dcs_write_seq_static(ctx, 0X82, 0X09);
	djn_dcs_write_seq_static(ctx, 0X83, 0X69);
	djn_dcs_write_seq_static(ctx, 0X88, 0X02);
	djn_dcs_write_seq_static(ctx, 0XE3, 0X01);
	djn_dcs_write_seq_static(ctx, 0XE4, 0XEA);
	djn_dcs_write_seq_static(ctx, 0XE5, 0X02);
	djn_dcs_write_seq_static(ctx, 0XE6, 0XE0);
	djn_dcs_write_seq_static(ctx, 0XE9, 0X02);
	djn_dcs_write_seq_static(ctx, 0XEA, 0X3F);
	djn_dcs_write_seq_static(ctx, 0XEB, 0X03);
	djn_dcs_write_seq_static(ctx, 0XEC, 0X5E);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X2A);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);
	djn_dcs_write_seq_static(ctx, 0X00, 0X91);
	djn_dcs_write_seq_static(ctx, 0X03, 0X20);
	djn_dcs_write_seq_static(ctx, 0X06, 0X0C);
	djn_dcs_write_seq_static(ctx, 0X07, 0X50);
	djn_dcs_write_seq_static(ctx, 0X0A, 0X60);
	djn_dcs_write_seq_static(ctx, 0X0D, 0X40);
	djn_dcs_write_seq_static(ctx, 0X0E, 0X02);
	djn_dcs_write_seq_static(ctx, 0X11, 0XEF);
	djn_dcs_write_seq_static(ctx, 0X15, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X16, 0X3E);
	djn_dcs_write_seq_static(ctx, 0X19, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X1A, 0X12);
	djn_dcs_write_seq_static(ctx, 0X1B, 0X12);
	djn_dcs_write_seq_static(ctx, 0X1D, 0X38);
	djn_dcs_write_seq_static(ctx, 0X1E, 0X37);
	djn_dcs_write_seq_static(ctx, 0X1F, 0X47);
	djn_dcs_write_seq_static(ctx, 0X20, 0X37);
	djn_dcs_write_seq_static(ctx, 0X28, 0XBA);
	djn_dcs_write_seq_static(ctx, 0X29, 0X03);
	djn_dcs_write_seq_static(ctx, 0X2A, 0X8B);
	djn_dcs_write_seq_static(ctx, 0X2D, 0X04);
	djn_dcs_write_seq_static(ctx, 0X2F, 0X0E);
	djn_dcs_write_seq_static(ctx, 0X30, 0X06);
	djn_dcs_write_seq_static(ctx, 0X31, 0X80);
	djn_dcs_write_seq_static(ctx, 0X33, 0XA1);
	djn_dcs_write_seq_static(ctx, 0X34, 0XC1);
	djn_dcs_write_seq_static(ctx, 0X35, 0X36);
	djn_dcs_write_seq_static(ctx, 0X36, 0XC1);
	djn_dcs_write_seq_static(ctx, 0X37, 0XBC);
	djn_dcs_write_seq_static(ctx, 0X38, 0X3A);
	djn_dcs_write_seq_static(ctx, 0X39, 0XBD);
	djn_dcs_write_seq_static(ctx, 0X3A, 0X06);
	djn_dcs_write_seq_static(ctx, 0X3F, 0X80);
	djn_dcs_write_seq_static(ctx, 0X40, 0X88);
	djn_dcs_write_seq_static(ctx, 0X46, 0X40);
	djn_dcs_write_seq_static(ctx, 0X47, 0X02);
	djn_dcs_write_seq_static(ctx, 0X4A, 0XEF);
	djn_dcs_write_seq_static(ctx, 0X4E, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X4F, 0X3E);
	djn_dcs_write_seq_static(ctx, 0X52, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X53, 0X12);
	djn_dcs_write_seq_static(ctx, 0X54, 0X12);
	djn_dcs_write_seq_static(ctx, 0X56, 0X38);
	djn_dcs_write_seq_static(ctx, 0X57, 0X4F);
	djn_dcs_write_seq_static(ctx, 0X58, 0X65);
	djn_dcs_write_seq_static(ctx, 0X59, 0X4F);
	djn_dcs_write_seq_static(ctx, 0X60, 0X80);
	djn_dcs_write_seq_static(ctx, 0X61, 0XC9);
	djn_dcs_write_seq_static(ctx, 0X62, 0X30);
	djn_dcs_write_seq_static(ctx, 0X63, 0X3F);
	djn_dcs_write_seq_static(ctx, 0X65, 0X04);
	djn_dcs_write_seq_static(ctx, 0X66, 0X0A);
	djn_dcs_write_seq_static(ctx, 0X67, 0X32);
	djn_dcs_write_seq_static(ctx, 0X68, 0X9E);
	djn_dcs_write_seq_static(ctx, 0X6A, 0XC3);
	djn_dcs_write_seq_static(ctx, 0X6B, 0XCF);
	djn_dcs_write_seq_static(ctx, 0X6C, 0X28);
	djn_dcs_write_seq_static(ctx, 0X6D, 0XCF);
	djn_dcs_write_seq_static(ctx, 0X6E, 0XCB);
	djn_dcs_write_seq_static(ctx, 0X6F, 0X2B);
	djn_dcs_write_seq_static(ctx, 0X70, 0XCC);
	djn_dcs_write_seq_static(ctx, 0X71, 0X32);
	djn_dcs_write_seq_static(ctx, 0X74, 0X50);
	djn_dcs_write_seq_static(ctx, 0X7A, 0X09);
	djn_dcs_write_seq_static(ctx, 0X7B, 0X40);
	djn_dcs_write_seq_static(ctx, 0X7F, 0XEF);
	djn_dcs_write_seq_static(ctx, 0X83, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X84, 0X3E);
	djn_dcs_write_seq_static(ctx, 0X87, 0X0F);
	djn_dcs_write_seq_static(ctx, 0X88, 0X12);
	djn_dcs_write_seq_static(ctx, 0X89, 0X12);
	djn_dcs_write_seq_static(ctx, 0X8B, 0X38);
	djn_dcs_write_seq_static(ctx, 0X8C, 0X7F);
	djn_dcs_write_seq_static(ctx, 0X8D, 0XA5);
	djn_dcs_write_seq_static(ctx, 0X8E, 0X7F);
	djn_dcs_write_seq_static(ctx, 0X95, 0X80);
	djn_dcs_write_seq_static(ctx, 0X96, 0XD8);
	djn_dcs_write_seq_static(ctx, 0X97, 0X1D);
	djn_dcs_write_seq_static(ctx, 0X98, 0X11);
	djn_dcs_write_seq_static(ctx, 0X9A, 0X04);
	djn_dcs_write_seq_static(ctx, 0X9B, 0X06);
	djn_dcs_write_seq_static(ctx, 0X9C, 0X1E);
	djn_dcs_write_seq_static(ctx, 0X9D, 0XBC);
	djn_dcs_write_seq_static(ctx, 0X9F, 0XC8);
	djn_dcs_write_seq_static(ctx, 0XA0, 0XDD);
	djn_dcs_write_seq_static(ctx, 0XA2, 0X1B);
	djn_dcs_write_seq_static(ctx, 0XA3, 0XDD);
	djn_dcs_write_seq_static(ctx, 0XA4, 0XDA);
	djn_dcs_write_seq_static(ctx, 0XA5, 0X1D);
	djn_dcs_write_seq_static(ctx, 0XA6, 0XDB);
	djn_dcs_write_seq_static(ctx, 0XA7, 0X1E);
	djn_dcs_write_seq_static(ctx, 0XAB, 0X50);
	djn_dcs_write_seq_static(ctx, 0XEE, 0X11);
	djn_dcs_write_seq_static(ctx, 0XEF, 0X01);
	djn_dcs_write_seq_static(ctx, 0XF0, 0XF8);
	djn_dcs_write_seq_static(ctx, 0XF1, 0XF8);
	djn_dcs_write_seq_static(ctx, 0XF2, 0XF8);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X2C);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);
	djn_dcs_write_seq_static(ctx, 0X00, 0X03);
	djn_dcs_write_seq_static(ctx, 0X01, 0X03);
	djn_dcs_write_seq_static(ctx, 0X02, 0X03);
	djn_dcs_write_seq_static(ctx, 0X03, 0X13);
	djn_dcs_write_seq_static(ctx, 0X04, 0X13);
	djn_dcs_write_seq_static(ctx, 0X05, 0X13);
	djn_dcs_write_seq_static(ctx, 0X0D, 0X01);
	djn_dcs_write_seq_static(ctx, 0X0E, 0X4B);
	djn_dcs_write_seq_static(ctx, 0X17, 0X46);
	djn_dcs_write_seq_static(ctx, 0X18, 0X46);
	djn_dcs_write_seq_static(ctx, 0X19, 0X46);
	djn_dcs_write_seq_static(ctx, 0X2D, 0XAF);
	djn_dcs_write_seq_static(ctx, 0X2F, 0X00);
	djn_dcs_write_seq_static(ctx, 0X30, 0XF4);
	djn_dcs_write_seq_static(ctx, 0X31, 0XF4);
	djn_dcs_write_seq_static(ctx, 0X33, 0XF1);
	djn_dcs_write_seq_static(ctx, 0X35, 0X1B);
	djn_dcs_write_seq_static(ctx, 0X36, 0X1B);
	djn_dcs_write_seq_static(ctx, 0X37, 0X1B);
	djn_dcs_write_seq_static(ctx, 0X4D, 0X13);
	djn_dcs_write_seq_static(ctx, 0X4E, 0X03);
	djn_dcs_write_seq_static(ctx, 0X4F, 0X08);
	djn_dcs_write_seq_static(ctx, 0X53, 0X03);
	djn_dcs_write_seq_static(ctx, 0X54, 0X03);
	djn_dcs_write_seq_static(ctx, 0X55, 0X03);
	djn_dcs_write_seq_static(ctx, 0X56, 0X23);
	djn_dcs_write_seq_static(ctx, 0X58, 0X23);
	djn_dcs_write_seq_static(ctx, 0X59, 0X23);
	djn_dcs_write_seq_static(ctx, 0X62, 0X72);
	djn_dcs_write_seq_static(ctx, 0X6B, 0X74);
	djn_dcs_write_seq_static(ctx, 0X6C, 0X74);
	djn_dcs_write_seq_static(ctx, 0X6D, 0X74);
	djn_dcs_write_seq_static(ctx, 0X80, 0XAF);
	djn_dcs_write_seq_static(ctx, 0X81, 0X00);
	djn_dcs_write_seq_static(ctx, 0X82, 0XF4);
	djn_dcs_write_seq_static(ctx, 0X83, 0XF4);
	djn_dcs_write_seq_static(ctx, 0X85, 0XF1);
	djn_dcs_write_seq_static(ctx, 0X87, 0X2B);
	djn_dcs_write_seq_static(ctx, 0X88, 0X2B);
	djn_dcs_write_seq_static(ctx, 0X89, 0X2B);
	djn_dcs_write_seq_static(ctx, 0X9D, 0X23);
	djn_dcs_write_seq_static(ctx, 0X9E, 0X03);
	djn_dcs_write_seq_static(ctx, 0X9F, 0X08);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X2B);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);
	djn_dcs_write_seq_static(ctx, 0XB7, 0X46);
	djn_dcs_write_seq_static(ctx, 0XB8, 0X15);
	djn_dcs_write_seq_static(ctx, 0XC0, 0X01);

	djn_dcs_write_seq_static(ctx, 0xFF,0xF0);
	djn_dcs_write_seq_static(ctx, 0xFB,0x01);
	djn_dcs_write_seq_static(ctx, 0xD2,0x50);
	djn_dcs_write_seq_static(ctx, 0x27,0x09);
	djn_dcs_write_seq_static(ctx, 0xFF,0x23);
	djn_dcs_write_seq_static(ctx, 0xFB,0x01);
	djn_dcs_write_seq_static(ctx, 0x00,0x60);
	djn_dcs_write_seq_static(ctx, 0x07,0x00);
	djn_dcs_write_seq_static(ctx, 0x08,0x02);
	djn_dcs_write_seq_static(ctx, 0x09,0x55);
	djn_dcs_write_seq_static(ctx, 0x0A,0x00);
	djn_dcs_write_seq_static(ctx, 0x0B,0x00);
	djn_dcs_write_seq_static(ctx, 0x0C,0x00);
	djn_dcs_write_seq_static(ctx, 0x0D,0x00);
	djn_dcs_write_seq_static(ctx, 0x10,0x50);
	djn_dcs_write_seq_static(ctx, 0x11,0x01);
	djn_dcs_write_seq_static(ctx, 0x12,0x95);
	djn_dcs_write_seq_static(ctx, 0x15,0xCF);
	djn_dcs_write_seq_static(ctx, 0x16,0x0C);

	djn_dcs_write_seq_static(ctx, 0x19,0x20);
	djn_dcs_write_seq_static(ctx, 0x1A,0x3F);
	djn_dcs_write_seq_static(ctx, 0x1B,0x3F);
	djn_dcs_write_seq_static(ctx, 0x1C,0x3F);
	djn_dcs_write_seq_static(ctx, 0x1D,0x3C);
	djn_dcs_write_seq_static(ctx, 0x1E,0x3C);
	djn_dcs_write_seq_static(ctx, 0x1F,0x33);
	djn_dcs_write_seq_static(ctx, 0x20,0x33);
	djn_dcs_write_seq_static(ctx, 0x21,0x2F);
	djn_dcs_write_seq_static(ctx, 0x22,0x2E);
	djn_dcs_write_seq_static(ctx, 0x23,0x30);
	djn_dcs_write_seq_static(ctx, 0x24,0x37);
	djn_dcs_write_seq_static(ctx, 0x25,0x38);
	djn_dcs_write_seq_static(ctx, 0x26,0x2C);
	djn_dcs_write_seq_static(ctx, 0x27,0x24);
	djn_dcs_write_seq_static(ctx, 0x28,0x28);
	djn_dcs_write_seq_static(ctx, 0x29,0x20);
	djn_dcs_write_seq_static(ctx, 0x2A,0x3F);
	djn_dcs_write_seq_static(ctx, 0x2B,0x3F);

	djn_dcs_write_seq_static(ctx, 0x58,0xFF);
	djn_dcs_write_seq_static(ctx, 0x59,0xFB);
	djn_dcs_write_seq_static(ctx, 0x5A,0xF6);
	djn_dcs_write_seq_static(ctx, 0x5B,0xF1);
	djn_dcs_write_seq_static(ctx, 0x5C,0xED);
	djn_dcs_write_seq_static(ctx, 0x5D,0xE0);
	djn_dcs_write_seq_static(ctx, 0x5E,0xD6);
	djn_dcs_write_seq_static(ctx, 0x5F,0xD0);
	djn_dcs_write_seq_static(ctx, 0x60,0xC5);
	djn_dcs_write_seq_static(ctx, 0x61,0xBC);
	djn_dcs_write_seq_static(ctx, 0x62,0xB2);
	djn_dcs_write_seq_static(ctx, 0x63,0xA9);
	djn_dcs_write_seq_static(ctx, 0x64,0xA2);
	djn_dcs_write_seq_static(ctx, 0x65,0x9D);
	djn_dcs_write_seq_static(ctx, 0x66,0x9B);
	djn_dcs_write_seq_static(ctx, 0x67,0x96);

	djn_dcs_write_seq_static(ctx, 0XFF, 0X10);
	djn_dcs_write_seq_static(ctx, 0XFB, 0X01);

	djn_dcs_write_seq_static(ctx, 0XFF,0X10);
	djn_dcs_write_seq_static(ctx, 0XFB,0X01);
	djn_dcs_write_seq_static(ctx, 0XB0,0X00);
	djn_dcs_write_seq_static(ctx, 0XC2,0X1B,0XA0);
	djn_dcs_write_seq_static(ctx, 0XE9,0X01);

	djn_dcs_write_seq_static(ctx, 0xFF,0x25);
	djn_dcs_write_seq_static(ctx, 0xFB,0x01);
	djn_dcs_write_seq_static(ctx, 0x18,0x22);
	djn_dcs_write_seq_static(ctx, 0xFF,0x10);
	djn_dcs_write_seq_static(ctx, 0xFB,0x01);
	djn_dcs_write_seq_static(ctx, 0xC0,0x00);

	djn_dcs_write_seq_static(ctx, 0xFF,0x10);   //11bit/20KHZ CABC
	djn_dcs_write_seq_static(ctx, 0xFB,0x01);
	djn_dcs_write_seq_static(ctx, 0x51,0x00,0x00);
	djn_dcs_write_seq_static(ctx, 0x53,0x2C);
	djn_dcs_write_seq_static(ctx, 0x68,0x03,0x01);
	djn_dcs_write_seq_static(ctx, 0x55,0x01);
	djn_dcs_write_seq_static(ctx, 0x35,0x00);
	djn_dcs_write_seq_static(ctx, 0x3B,0x03,0x20);
	djn_dcs_write_seq_static(ctx, 0X11);
	msleep(120);
	djn_dcs_write_seq_static(ctx, 0X29);
	msleep(50);

	if(lcd_nvt_resume_nt36672s)
		lcd_nvt_resume_nt36672s();
}

static int djn_disable(struct drm_panel *panel)
{
	struct djn *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int djn_unprepare(struct drm_panel *panel)
{
	struct djn *ctx = panel_to_lcm(panel);

	pr_info("%s +\n", __func__);

	if (!ctx->prepared)
		return 0;

	is_suspend = 1;

	djn_dcs_write_seq_static(ctx, 0xAC,0x0A,0x00);
	djn_dcs_write_seq_static(ctx, 0x51,0x00,0x00);
	djn_dcs_write_seq_static(ctx, 0x28);
	msleep(20);
	djn_dcs_write_seq_static(ctx, 0x10);
	msleep(120);

	ctx->error = 0;
	ctx->prepared = false;

#ifdef TINNO_LCM_OEM_CONFIG
	if(djn_gesture_mode && !in_esd_recovery_flg) {
		pr_info("nt36672s Skip Power Control !\n");
		return 0;
	}
#endif

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	djn_panel_bias_disable();
#else
#if 0
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	udelay(10000);
#endif

	ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
		"bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_info(ctx->dev, "%s: cannot get bias_neg %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	gpiod_set_value(ctx->bias_neg, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);

	udelay(1000);

	ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
		"bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_info(ctx->dev, "%s: cannot get bias_pos %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	gpiod_set_value(ctx->bias_pos, 0);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);
#endif

	udelay(5000);

	ctx->vddio_gpio =
		devm_gpiod_get(ctx->dev, "vddio", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddio_gpio)) {
		dev_info(ctx->dev, "%s: cannot get vddio_gpio %ld\n",
			__func__, PTR_ERR(ctx->vddio_gpio));
		return PTR_ERR(ctx->vddio_gpio);
	}
	gpiod_set_value(ctx->vddio_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->vddio_gpio);
	udelay(10 * 1000);

	pr_info("%s -\n", __func__);

	return 0;
}

static int djn_prepare(struct drm_panel *panel)
{
	struct djn *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s djn nt36672s +\n", __func__);

	if (ctx->prepared)
		return 0;

#ifdef TINNO_LCM_OEM_CONFIG
	if(djn_gesture_mode && !in_esd_recovery_flg) {
		ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->reset_gpio)) {
			dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
			return PTR_ERR(ctx->reset_gpio);;
		}

		gpiod_set_value(ctx->reset_gpio, 0);
		udelay(5 * 1000);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);

		udelay(10000);
		djn_panel_init(ctx);
		ret = ctx->error;
		if (ret < 0)
			djn_unprepare(panel);
		ctx->prepared = true;
#if defined(CONFIG_MTK_PANEL_EXT)
		mtk_panel_tch_rst(panel);
#endif
#ifdef PANEL_SUPPORT_READBACK
		djn_panel_get_data(ctx);
#endif
		is_suspend = 0;
		pr_info("nt36672s Skip Power Control !\n");
		return ret;
	}
	else
#endif

#if 0
	ctx->vddio_gpio =
		devm_gpiod_get(ctx->dev, "vddio", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddio_gpio)) {
		dev_info(ctx->dev, "%s: cannot get vddio_gpio %ld\n",
			__func__, PTR_ERR(ctx->vddio_gpio));
		return PTR_ERR(ctx->vddio_gpio);
	}
	gpiod_set_value(ctx->vddio_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->vddio_gpio);

	udelay(5000);
#endif

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);;
	}

	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(5 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	djn_panel_bias_enable();
#else
	ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
		"bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_info(ctx->dev, "%s: cannot get bias_pos %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	gpiod_set_value(ctx->bias_pos, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_pos);

	udelay(5 * 1000);

	ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
		"bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_info(ctx->dev, "%s: cannot get bias_neg %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	gpiod_set_value(ctx->bias_neg, 1);
	devm_gpiod_put(ctx->dev, ctx->bias_neg);
#endif

	udelay(10 * 1000);
	djn_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		djn_unprepare(panel);

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif
#ifdef PANEL_SUPPORT_READBACK
	djn_panel_get_data(ctx);
#endif

	is_suspend = 0;
	pr_info("%s -\n", __func__);

	return ret;
}

static int djn_enable(struct drm_panel *panel)
{
	struct djn *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

#define HFP (50)
#define HSA (4)
#define HBP (40)
#define VFP_60 (56)
//#define VFP_90 (300)
#define VSA (8)
#define VBP (10)
#define VAC (2400)
#define HAC (1080)
#define FPS (60)

static const struct drm_display_mode default_mode = {
	.clock = (int)((HAC + HFP + HSA + HBP) * (VAC + VFP_60+ VSA + VBP) * 60 / 1000),
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP,
	.vdisplay = VAC,
	.vsync_start = VAC + VFP_60,
	.vsync_end = VAC + VFP_60 + VSA,
	.vtotal = VAC + VFP_60 + VSA + VBP,
};

/* static const struct drm_display_mode performance_mode = {
	.clock = 137660,
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP,
	.vdisplay = VAC,
	.vsync_start = VAC + VFP_90,
	.vsync_end = VAC + VFP_90 + VSA,
	.vtotal = VAC + VFP_90 + VSA + VBP,
};
 */
#if defined(CONFIG_MTK_PANEL_EXT)
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct djn *ctx = panel_to_lcm(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	struct djn *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	unsigned char data[3] = {0x00, 0x00, 0x00};
	unsigned char id[3] = {0x00, 0x80, 0x00};
	ssize_t ret;

	ret = mipi_dsi_dcs_read(dsi, 0x4, data, 3);
	if (ret < 0) {
		pr_info("%s error\n", __func__);
		return 0;
	}

	pr_info("ATA read data %x %x %x\n", data[0], data[1], data[2]);

	if (data[0] == id[0] &&
			data[1] == id[1] &&
			data[2] == id[2])
		return 1;

	pr_info("ATA expect read data is %x %x %x\n",
			id[0], id[1], id[2]);

	return 0;
}

static int djn_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	char bl_tb0[] = {0x51, 0x07,0xFF};
	unsigned int bl_lvl = 0x7FF;
	if (!cb)
		return -1;

	if (level > 255)
		level = 255;

	if (is_extra == 1 && level > 220) {
		level = 220;
	} else if (is_extra == 2 && level > 241) {
		level = 241;
	}

	bl_lvl = backlight_i2c_map3[level];

	pr_info("%s: level=%d, bl_lvl=%d, is_extra=%d, is_hbm=%d\n", __func__, level, bl_lvl, is_extra, is_hbm);

	bl_tb0[1] = (u8)((bl_lvl >> 8) & 0x0F);
	bl_tb0[2] = (u8)(bl_lvl & 0xFF);

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

static struct mtk_panel_params ext_params = {
	.pll_clk = 560,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},

	.ssc_enable = 0,

 	/*.dyn = {
		.switch_en = 1,
		.pll_clk = 553,
		.hfp = 34,
	},*/
};

/* static struct mtk_panel_params ext_params_90hz = {
	.pll_clk = 454,
	.vfp_low_power = 300,
	.cust_esd_check = 1,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0a,
		.count = 1,
		.para_list[0] = 0x9c,
	},
	.dyn = {
		.switch_en = 1,
		.pll_clk = 465,
		.hfp = 56,
	},
};
 */
struct drm_display_mode *get_mode_by_id_hfp(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel,
			struct drm_connector *connector, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id_hfp(connector, mode);

	if (m == NULL) {
		pr_err("%s:%d invalid display_mode\n", __func__, __LINE__);
		return -1;
	}

	if (drm_mode_vrefresh(m) == 60)
		ext->params = &ext_params;
/* 	else if (drm_mode_vrefresh(m) == 90)
		ext->params = &ext_params_90hz; */
	else
		ret = 1;

	return ret;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = djn_setbacklight_cmdq,
	.ext_param_set = mtk_panel_ext_param_set,
	.ata_check = panel_ata_check,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int djn_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	//struct drm_display_mode *mode2;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

/* 	mode2 = drm_mode_duplicate(connector->dev, &performance_mode);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode.hdisplay, performance_mode.vdisplay,
			 drm_mode_vrefresh(&performance_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2); */

	connector->display_info.width_mm = 68;
	connector->display_info.height_mm = 152;

	return 1;
}

static const struct drm_panel_funcs djn_drm_funcs = {
	.disable = djn_disable,
	.unprepare = djn_unprepare,
	.prepare = djn_prepare,
	.enable = djn_enable,
	.get_modes = djn_get_modes,
};

static struct proc_dir_entry *proc_dir_djn_lcd_info;

typedef struct {
	char *name;
	struct proc_dir_entry *node;
	struct proc_ops *fops;
	bool isCreated;
} djn_proc_node;

#if 1
static ssize_t djn_extra_brightness_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
	u32 len = 0;

	pr_info("%s enter!\n", __func__);

	if (*pos != 0)
		return 0;

	memset(extra_buf, 0, 16 * sizeof(unsigned char));
	len += snprintf(extra_buf + len, 16 - len, "%d\n", is_extra);

	if (copy_to_user((char *)buff, extra_buf, len))
		pr_err("Failed to copy data to user space\n");

	*pos += len;

	return len;
}

static ssize_t djn_extra_brightness_write(struct file *filp, const char *buff, size_t size, loff_t *pos)
{
	char cmd[16] = { 0 };
	ssize_t ret;

	pr_info("%s enter!\n", __func__);

	if (is_suspend) {
		pr_info("In suspend, no write node, return now");
		return -1;
	}

	if ((size - 1) > sizeof(cmd)) {
		pr_err("ERROR! input length is larger than local buffer\n");
		return -1;
	}
	if (buff != NULL) {
		if (copy_from_user(cmd, buff, size)) {
			pr_err("Failed to copy data from user space\n");
			size = -1;
			goto out;
		}
	}

	is_extra = simple_strtol(cmd, NULL, 0);

	pr_info("%s end! is_extra = %d\n", __func__, is_extra);

out:
	ret = size;
	return ret;
}


static ssize_t djn_hbm_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
	u32 len = 0;

	pr_info("%s enter! hbm=%d\n", __func__, hbm);

	if (*pos != 0)
		return 0;

	memset(hbm_buf, 0, 16 * sizeof(unsigned char));
	len += snprintf(hbm_buf + len, 16 - len, "%d\n", hbm);

	if (copy_to_user((char *)buff, hbm_buf, len))
		pr_err("Failed to copy data to user space\n");

	*pos += len;

	return len;
}

static ssize_t djn_hbm_write(struct file *filp, const char *buff, size_t size, loff_t *pos)
{

	char cmd[16] = { 0 };
	int hbm_en;
	ssize_t ret;

	pr_info("%s enter!\n", __func__);

	if (is_suspend) {
		pr_info("In suspend, no write hbm, return now");
		return -1;
	}

	if ((size - 1) > sizeof(cmd)) {
		pr_err("ERROR! input length is larger than local buffer\n");
		return -1;
	}
	if (buff != NULL) {
		if (copy_from_user(cmd, buff, size)) {
			pr_err("Failed to copy data from user space\n");
			size = -1;
			goto out;
		}
	}

	hbm_en = simple_strtol(cmd, NULL, 0);

	if (0 == hbm_en) {
		pr_info("%s Disable hbm!\n", __func__);

		ptx->bl_en_gpio =
				devm_gpiod_get(ptx->dev, "bl-enable", GPIOD_OUT_LOW);
		if (IS_ERR(ptx->bl_en_gpio)) {
			dev_info(ptx->dev, "%s: cannot get bl_en_gpio %ld\n",
							__func__, PTR_ERR(ptx->bl_en_gpio));
			return PTR_ERR(ptx->bl_en_gpio);
		}
		gpiod_set_value(ptx->bl_en_gpio, 0);
		devm_gpiod_put(ptx->dev, ptx->bl_en_gpio);

		is_hbm = false;
	} else if (1 == hbm_en) {
		pr_info("%s Enable hbm!\n", __func__);

		is_hbm = true;

		ptx->bl_en_gpio =
				devm_gpiod_get(ptx->dev, "bl-enable", GPIOD_OUT_LOW);
		if (IS_ERR(ptx->bl_en_gpio)) {
			dev_info(ptx->dev, "%s: cannot get bl_en_gpio %ld\n",
							__func__, PTR_ERR(ptx->bl_en_gpio));
			return PTR_ERR(ptx->bl_en_gpio);
		}
		gpiod_set_value(ptx->bl_en_gpio, 1);
		devm_gpiod_put(ptx->dev, ptx->bl_en_gpio);
	} else if (2 == hbm_en) {
		pr_info("%s Enable hbm & ultra hbm!\n", __func__);

		ptx->bl_en_gpio =
				devm_gpiod_get(ptx->dev, "bl-enable", GPIOD_OUT_LOW);
		if (IS_ERR(ptx->bl_en_gpio)) {
			dev_info(ptx->dev, "%s: cannot get bl_en_gpio %ld\n",
							__func__, PTR_ERR(ptx->bl_en_gpio));
			return PTR_ERR(ptx->bl_en_gpio);
		}
		gpiod_set_value(ptx->bl_en_gpio, 1);
		devm_gpiod_put(ptx->dev, ptx->bl_en_gpio);
	} else {
		pr_err("%s Wrong parameter hbm_en=%d, cmd=%s !\n", __func__, hbm_en, cmd);
		goto out;
	}
	hbm = hbm_en;

	pr_info("%s end! hbm=%d\n", __func__, hbm);

out:
	ret =size;
	return ret;
}

static struct proc_ops proc_djn_hbm_fops = {
	.proc_read = djn_hbm_read,
	.proc_write = djn_hbm_write,
	.proc_lseek = default_llseek,
};

static struct proc_ops proc_djn_extra_fops = {
	.proc_read = djn_extra_brightness_read,
	.proc_write = djn_extra_brightness_write,
	.proc_lseek = default_llseek,
};

djn_proc_node lcd_info_proc[] = {
	{"backlight_hbm", NULL, &proc_djn_hbm_fops, false},
	{"extra_brightness", NULL, &proc_djn_extra_fops, false},
};
#endif

static int djn_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct djn *ctx;
	struct device_node *backlight;
	int ret;
	int i = 0;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;

	pr_info("nt36672s %s --- begin\n", __func__);
	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_info("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_info("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_info("%s+ skip probe due to not current lcm\n", __func__);
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(struct djn), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "%s: cannot get reset-gpios %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

	ctx->vddio_gpio = devm_gpiod_get(dev, "vddio", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddio_gpio)) {
		dev_info(dev, "%s: cannot get vddio-gpios %ld\n",
			__func__, PTR_ERR(ctx->vddio_gpio));
		return PTR_ERR(ctx->vddio_gpio);
	}
	devm_gpiod_put(dev, ctx->vddio_gpio);

	ctx->bias_pos = devm_gpiod_get_index(dev, "bias", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_pos)) {
		dev_info(dev, "%s: cannot get bias-pos 0 %ld\n",
			__func__, PTR_ERR(ctx->bias_pos));
		return PTR_ERR(ctx->bias_pos);
	}
	devm_gpiod_put(dev, ctx->bias_pos);

	ctx->bias_neg = devm_gpiod_get_index(dev, "bias", 1, GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->bias_neg)) {
		dev_info(dev, "%s: cannot get bias-neg 1 %ld\n",
			__func__, PTR_ERR(ctx->bias_neg));
		return PTR_ERR(ctx->bias_neg);
	}
	devm_gpiod_put(dev, ctx->bias_neg);

	ctx->bl_en_gpio = devm_gpiod_get(dev, "bl-enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->bl_en_gpio)) {
		dev_info(dev, "%s: cannot get bl-enable-gpios %ld\n",
			__func__, PTR_ERR(ctx->bl_en_gpio));
		return PTR_ERR(ctx->bl_en_gpio);
	}
	gpiod_set_value(ctx->bl_en_gpio, 0);
	devm_gpiod_put(dev, ctx->bl_en_gpio);

	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel, dev, &djn_drm_funcs, DRM_MODE_CONNECTOR_DSI);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &djn_drm_funcs;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

	pr_info("lcd_info_node_init\n");
	proc_dir_djn_lcd_info = proc_mkdir("lcd_info", NULL);
	for (; i < ARRAY_SIZE(lcd_info_proc); i++) {
		lcd_info_proc[i].node = proc_create(lcd_info_proc[i].name, 0644,
					proc_dir_djn_lcd_info, lcd_info_proc[i].fops);
		if (lcd_info_proc[i].node == NULL) {
			lcd_info_proc[i].isCreated = false;
			pr_err("Failed to create %s under /proc\n", lcd_info_proc[i].name);
		} else {
			lcd_info_proc[i].isCreated = true;
			pr_err("Succeed to create %s under /proc\n", lcd_info_proc[i].name);
		}
	}

#if defined(CONFIG_MTK_PANEL_EXT)
	//mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	FULL_PRODUCT_DEVICE_INFO(ID_LCD, "DIJIN-NT36672S-VDO");
#endif

	ptx = ctx;
	hbm = 0;
	is_extra = 0;

	nt36672s_lcd_id = 0x0093;
	pr_info("nt36672s %s --- end\n", __func__);

	return ret;
}

/* static int djn_remove(struct mipi_dsi_device *dsi)
{
	struct djn *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
} */

#if 1
static void djn_shutdown(struct mipi_dsi_device *dsi)
{
	struct djn *ctx = mipi_dsi_get_drvdata(dsi);

	pr_notice("%s+\n", __func__);

	ctx->error = 0;
	ctx->prepared = false;

	if(djn_gesture_mode) {
		pr_info("%s + ! nt36672s gesture on !\n", __func__);

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
		djn_panel_bias_disable();
#else
#if 0
		ctx->reset_gpio =
			devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->reset_gpio)) {
			dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
		}
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);

		udelay(10000);
#endif

		ctx->bias_neg = devm_gpiod_get_index(ctx->dev,
			"bias", 1, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg)) {
			dev_info(ctx->dev, "%s: cannot get bias_neg %ld\n",
				__func__, PTR_ERR(ctx->bias_neg));
		}
		gpiod_set_value(ctx->bias_neg, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_neg);

		udelay(1000);

		ctx->bias_pos = devm_gpiod_get_index(ctx->dev,
			"bias", 0, GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos)) {
			dev_info(ctx->dev, "%s: cannot get bias_pos %ld\n",
				__func__, PTR_ERR(ctx->bias_pos));
		}
		gpiod_set_value(ctx->bias_pos, 0);
		devm_gpiod_put(ctx->dev, ctx->bias_pos);
#endif

		udelay(5000);

		ctx->vddio_gpio =
			devm_gpiod_get(ctx->dev, "vddio", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->vddio_gpio)) {
			dev_info(ctx->dev, "%s: cannot get vddio_gpio %ld\n",
				__func__, PTR_ERR(ctx->vddio_gpio));
		}
		gpiod_set_value(ctx->vddio_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->vddio_gpio);

		//djn_disable(&ctx->panel);
		pr_info("%s - ! nt36672s gesture on !\n", __func__);
	}
}
#endif

static const struct of_device_id djn_of_match[] = {
	{ .compatible = "djn,nt36672s,vdo", },
	{ }
};

MODULE_DEVICE_TABLE(of, djn_of_match);

static struct mipi_dsi_driver djn_driver = {
	.probe = djn_probe,
	//.remove = djn_remove,
	.driver = {
		.name = "nt36672s_dsi_vdo_djn",
		.owner = THIS_MODULE,
		.of_match_table = djn_of_match,
	},
	.shutdown = djn_shutdown,
};

static int __init djn_drv_init(void)
{
	int ret = 0;

	pr_notice("%s+\n", __func__);
	//mtk_panel_lock();
	ret = mipi_dsi_driver_register(&djn_driver);
	if (ret < 0)
		pr_notice("%s, Failed to register jdi driver: %d\n",
			__func__, ret);

	//mtk_panel_unlock();
	pr_notice("%s- ret:%d\n", __func__, ret);
	return 0;
}

static void __exit djn_drv_exit(void)
{
	pr_notice("%s+\n", __func__);
	//mtk_panel_lock();
	mipi_dsi_driver_unregister(&djn_driver);
	//mtk_panel_unlock();
	pr_notice("%s-\n", __func__);
}
module_init(djn_drv_init);
module_exit(djn_drv_exit);

MODULE_AUTHOR("Ning Feng <Ning.Feng@mediatek.com>");
MODULE_DESCRIPTION("djn nt36672s VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");
