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

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../../../drivers/gpu/drm/mediatek/mediatek_v2/mtk_disp_aal.h"
#include "../../../drivers/gpu/drm/mediatek/mediatek_v2/mtk_panel_ext.h"
#include "../../../drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mediatek_v2/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif

/*#if IS_ENABLED(CONFIG_OEM_DEVINFO)
#include <dev_info.h>
#endif*/

#define TINNO_LCM_OEM_CONFIG
#if defined(TINNO_LCM_OEM_CONFIG)
#include <ilitek_v3.h>
int gesture_mode = -1;
#endif

extern bool tp_probe_finshed;

int hbm;
bool is_hbm;
bool is_suspend;
unsigned int dre_en;
static unsigned char dre_en_buf[16] = {0};
static unsigned char hbm_buf[16] = {0};
struct ili *ptx;

struct ili {
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

#define ili_dcs_write_seq(ctx, seq...) \
({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	ili_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

#define ili_dcs_write_seq_static(ctx, seq...) \
({\
	static const u8 d[] = { seq };\
	ili_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static inline struct ili *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct ili, panel);
}

static void ili_dcs_write(struct ili *ctx, const void *data, size_t len)
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
static int ili_dcs_read(struct ili *ctx, u8 cmd, void *data, size_t len)
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

static void ili_panel_get_data(struct ili *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = ili_dcs_read(ctx,  0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			 ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
static struct regulator *disp_bias_pos;
static struct regulator *disp_bias_neg;


static int ili_panel_bias_regulator_init(void)
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

static int ili_panel_bias_enable(void)
{
	int ret = 0;
	int retval = 0;

	ili_panel_bias_regulator_init();

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

static int ili_panel_bias_disable(void)
{
	int ret = 0;
	int retval = 0;

	ili_panel_bias_regulator_init();

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

static void ili_panel_init(struct ili *ctx)
{
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return;
	}

	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(10 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(5 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(10 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

#ifdef TINNO_LCM_OEM_CONFIG
	if (tp_probe_finshed)
		ili_resume_by_ddi();
#endif

	ili_dcs_write_seq_static(ctx, 0xFF,0x98,0x83,0x00);
	ili_dcs_write_seq_static(ctx, 0x35,0x00);

	ili_dcs_write_seq_static(ctx, 0x51,0x00,0x00);
	ili_dcs_write_seq_static(ctx, 0x53,0x2C);

	ili_dcs_write_seq_static(ctx, 0x11);
	msleep(80);

	ili_dcs_write_seq_static(ctx, 0x29);
	msleep(20);

}

static int ili_disable(struct drm_panel *panel)
{
	struct ili *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int ili_unprepare(struct drm_panel *panel)
{
	struct ili *ctx = panel_to_lcm(panel);

	pr_info("%s +\n", __func__);

	if (!ctx->prepared)
		return 0;

	is_suspend = 1;

	msleep(2);
	ili_dcs_write_seq_static(ctx, 0x28);
	msleep(40);
	ili_dcs_write_seq_static(ctx, 0x10);
	msleep(100);

	ctx->error = 0;
	ctx->prepared = false;

#ifdef TINNO_LCM_OEM_CONFIG
	gesture_mode = ili_lcd_gesture_control();
	if(gesture_mode) {
		pr_info("%s ili9883c Skip Power Control !\n", __func__);
		return 0;
	}
#endif

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	ili_panel_bias_disable();
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

	pr_info("%s -\n", __func__);

	return 0;
}

static int ili_prepare(struct drm_panel *panel)
{
	struct ili *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s +\n", __func__);

	if (ctx->prepared)
		return 0;

#ifdef TINNO_LCM_OEM_CONFIG
	gesture_mode = ili_lcd_gesture_control();
	if(gesture_mode) {
		udelay(10000);
		ili_panel_init(ctx);
		ret = ctx->error;
		if (ret < 0)
			ili_unprepare(panel);
		ctx->prepared = true;
#if defined(CONFIG_MTK_PANEL_EXT)
		mtk_panel_tch_rst(panel);
#endif
#ifdef PANEL_SUPPORT_READBACK
		lcm_panel_get_data(ctx);
#endif
		is_suspend = 0;
		pr_info("%s ili9883c Skip Power Control !\n", __func__);
		return ret;
	}
	else
#endif

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

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	ili_panel_bias_enable();
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

	udelay(2000);
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

	udelay(10000);
	ili_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		ili_unprepare(panel);

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif
#ifdef PANEL_SUPPORT_READBACK
	ili_panel_get_data(ctx);
#endif

	is_suspend = 0;
	pr_info("%s -\n", __func__);

	return ret;
}

static int ili_enable(struct drm_panel *panel)
{
	struct ili *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

#define HFP (20)
#define HSA (8)
#define HBP (12)
#define VFP_60 (1370)
#define VFP_90 (370)
#define VSA (2)
#define VBP (30)
#define VAC (1612)
#define HAC (720)
#define FPS (90)

static const struct drm_display_mode default_mode = {
	.clock = 137438,
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP,
	.vdisplay = VAC,
	.vsync_start = VAC + VFP_60,
	.vsync_end = VAC + VFP_60 + VSA,
	.vtotal = VAC + VFP_60 + VSA + VBP,
};

static const struct drm_display_mode performance_mode = {
	.clock = 137757,
	.hdisplay = HAC,
	.hsync_start = HAC + HFP,
	.hsync_end = HAC + HFP + HSA,
	.htotal = HAC + HFP + HSA + HBP,
	.vdisplay = VAC,
	.vsync_start = VAC + VFP_90,
	.vsync_end = VAC + VFP_90 + VSA,
	.vtotal = VAC + VFP_90 + VSA + VBP,
};


#if defined(CONFIG_MTK_PANEL_EXT)
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct ili *ctx = panel_to_lcm(panel);

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
	struct ili *ctx = panel_to_lcm(panel);
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

static int ili_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	char bl_tb0[] = {0x51, 0x07, 0xFF};

	if (!cb)
		return -1;

	if (is_hbm & (level > 0x6b8)) {
		pr_info("%s: Enter hbm mode,return 0! level=%x\n", __func__, level);
		return 0;
	}

	bl_tb0[1] = (u8)((level >> 8) & 0x07);
	bl_tb0[2] = (u8)(level & 0xFF);

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

static struct mtk_panel_params ext_params = {
	.pll_clk = 454,
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
		.hbp = 32,
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.pll_clk = 454,
	.vfp_low_power = 370,
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
		.hbp = 32,
	},
};

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
	else if (drm_mode_vrefresh(m) == 90)
		ext->params = &ext_params_90hz;
	else
		ret = 1;

	return ret;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = ili_setbacklight_cmdq,
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

static int ili_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode2;

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

	mode2 = drm_mode_duplicate(connector->dev, &performance_mode);
	if (!mode2) {
		dev_info(connector->dev->dev, "failed to add mode %ux%ux@%u\n",
			 performance_mode.hdisplay, performance_mode.vdisplay,
			 drm_mode_vrefresh(&performance_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode2);

	connector->display_info.width_mm = 68;
	connector->display_info.height_mm = 152;

	return 1;
}

static const struct drm_panel_funcs ili_drm_funcs = {
	.disable = ili_disable,
	.unprepare = ili_unprepare,
	.prepare = ili_prepare,
	.enable = ili_enable,
	.get_modes = ili_get_modes,
};

static struct proc_dir_entry *proc_dir_ilitek_lcd_info;

typedef struct {
	char *name;
	struct proc_dir_entry *node;
	struct proc_ops *fops;
	bool isCreated;
} ili_proc_node;

static ssize_t ili_disp_set_dre_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
{
	u32 len = 0;

	pr_info("%s enter!\n", __func__);

	if (*pos != 0)
		return 0;

	memset(dre_en_buf, 0, 16 * sizeof(unsigned char));
	len += snprintf(dre_en_buf + len, 16 - len, "%d\n", dre_en);

	if (copy_to_user((char *)buff, dre_en_buf, len))
		pr_err("Failed to copy data to user space\n");

	*pos += len;

	return len;
}

static ssize_t ili_disp_set_dre_write(struct file *filp, const char *buff, size_t size, loff_t *pos)
{

	char cmd[16] = { 0 };
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

	dre_en = simple_strtol(cmd, NULL, 0);
	disp_aal_set_dre_en(dre_en);

	pr_info("%s end! dre_en = %d\n", __func__, dre_en);

out:
	ret = size;
	return ret;
}

static ssize_t ili_hbm_read(struct file *filp, char __user *buff, size_t size, loff_t *pos)
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

static ssize_t ili_hbm_write(struct file *filp, const char *buff, size_t size, loff_t *pos)
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
		pr_err("%s Wrong hbm parameter hbm_en=%d !\n", __func__, hbm_en);
		goto out;
	}
	hbm = hbm_en;

	pr_info("%s end! hbm=%d\n", __func__, hbm);

out:
	ret =size;
	return ret;
}

static struct proc_ops proc_ili_hbm_fops = {
	.proc_read = ili_hbm_read,
	.proc_write = ili_hbm_write,
	.proc_lseek = default_llseek,
};

static struct proc_ops proc_ili_dre_fops = {
	.proc_read = ili_disp_set_dre_read,
	.proc_write = ili_disp_set_dre_write,
	.proc_lseek = default_llseek,
};

ili_proc_node lcd_info_proc[] = {
	{"backlight_hbm", NULL, &proc_ili_hbm_fops, false},
	{"disp_set_dre", NULL, &proc_ili_dre_fops, false},
};

static int ili_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ili *ctx;
	struct device_node *backlight;
	int ret;
	int i = 0;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;

	pr_info("ili9883c %s --- begin\n", __func__);
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

	ctx = devm_kzalloc(dev, sizeof(struct ili), GFP_KERNEL);
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

	drm_panel_init(&ctx->panel, dev, &ili_drm_funcs, DRM_MODE_CONNECTOR_DSI);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &ili_drm_funcs;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

	pr_info("lcd_info_node_init\n");
	proc_dir_ilitek_lcd_info = proc_mkdir("lcd_info", NULL);
	for (; i < ARRAY_SIZE(lcd_info_proc); i++) {
		lcd_info_proc[i].node = proc_create(lcd_info_proc[i].name, 0644,
					proc_dir_ilitek_lcd_info, lcd_info_proc[i].fops);
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

/*#if IS_ENABLED(CONFIG_OEM_DEVINFO)
	FULL_PRODUCT_DEVICE_INFO(ID_LCD, "BOE-ILI9883C-VDO");
#endif*/

	ptx = ctx;
	hbm = 0;

	pr_info("ili9883c %s --- end\n", __func__);

	return ret;
}

static int ili_remove(struct mipi_dsi_device *dsi)
{
	struct ili *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

#if 1
static void ili_shutdown(struct mipi_dsi_device *dsi)
{
	struct ili *ctx = mipi_dsi_get_drvdata(dsi);

	pr_notice("%s+\n", __func__);

	ctx->error = 0;
	ctx->prepared = false;

	if(gesture_mode) {
		pr_info("%s + ! ili9883c gesture on !\n", __func__);

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
		ili_panel_bias_disable();
#else
		ctx->reset_gpio =
			devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->reset_gpio)) {
			dev_info(ctx->dev, "%s: cannot get reset_gpio %ld\n",
				__func__, PTR_ERR(ctx->reset_gpio));
		}
		gpiod_set_value(ctx->reset_gpio, 0);
		devm_gpiod_put(ctx->dev, ctx->reset_gpio);

		udelay(10000);

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

		//ili_disable(&ctx->panel);
		pr_info("%s - ! ili9883c gesture on !\n", __func__);
	}
}
#endif

static const struct of_device_id ili_of_match[] = {
	{ .compatible = "boe,ili9883c,vdo", },
	{ }
};

MODULE_DEVICE_TABLE(of, ili_of_match);

static struct mipi_dsi_driver ili_driver = {
	.probe = ili_probe,
	.remove = ili_remove,
	.driver = {
		.name = "ili9883c_dsi_vdo_boe",
		.owner = THIS_MODULE,
		.of_match_table = ili_of_match,
	},
	.shutdown = ili_shutdown,
};

static int __init ili_drv_init(void)
{
	int ret = 0;

	pr_notice("%s+\n", __func__);
	mtk_panel_lock();
	ret = mipi_dsi_driver_register(&ili_driver);
	if (ret < 0)
		pr_notice("%s, Failed to register jdi driver: %d\n",
			__func__, ret);

	mtk_panel_unlock();
	pr_notice("%s- ret:%d\n", __func__, ret);
	return 0;
}

static void __exit ili_drv_exit(void)
{
	pr_notice("%s+\n", __func__);
	mtk_panel_lock();
	mipi_dsi_driver_unregister(&ili_driver);
	mtk_panel_unlock();
	pr_notice("%s-\n", __func__);
}
module_init(ili_drv_init);
module_exit(ili_drv_exit);

MODULE_AUTHOR("Ning Feng <Ning.Feng@mediatek.com>");
MODULE_DESCRIPTION("BOE ILI9883C VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");
