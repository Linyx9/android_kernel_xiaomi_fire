//  LCM power is provided by I2C
// ---------------------------------------------------------------------------

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h> /*lcm power is provided by i2c*/
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include "ocp2131_i2c.h"
/* Define -------------------------------------------------------------------*/
#define I2C_I2C_LCD_BIAS_CHANNEL 0  //for I2C channel 0
#define DCDC_I2C_BUSNUM  I2C_I2C_LCD_BIAS_CHANNEL  //for I2C channel 0
#define DCDC_I2C_ID_NAME "ocp2131"
#define DCDC_I2C_ADDR 0x3E

#define LCM_LOGD(fmt, arg...) \
	do { \
		printk("[LCM-ocp2131] " fmt, ##arg); \
	} while (0)


/*
static struct OCP2131_SETTING_TABLE ocp2131_cmd_data[5] = {
	{ 0x00, 0x12 }, //AVDD 5.8
	{ 0x01, 0x12 }, //AVEE 5.8
                { 0xFF, 0x80 }, //
};
*/
/* Variable -----------------------------------------------------------------*/
#if defined(CONFIG_MTK_LEGACY)
static struct i2c_board_info __initdata ocp2131_board_info = {I2C_BOARD_INFO(DCDC_I2C_ID_NAME, DCDC_I2C_ADDR)};
#else
static const struct of_device_id lcm_of_match[] = {
	{.compatible = "mediatek,I2C_LCD_BIAS_OCP2131"},
	{},
};
#endif

static struct i2c_client *lcm_bias_common_i2c_client = NULL;

/* Functions Prototype --------------------------------------------------------*/
static int ocp2131_probe(struct i2c_client *client);
static void ocp2131_remove(struct i2c_client *client);

/* Data Structure -------------------------------------------------------------*/
struct ocp2131_dev {
	struct i2c_client *client;
};

static const struct i2c_device_id ocp2131_id[] = {
	{ DCDC_I2C_ID_NAME, 0 },
	{ }
};

/* I2C Driver  ----------------------------------------------------------------*/
static struct i2c_driver ocp2131_iic_driver = {
	.id_table = ocp2131_id,
	.probe    = ocp2131_probe,
	.remove   = ocp2131_remove,
	.driver   = {
		.owner          = THIS_MODULE,
		.name           = "ocp2131",
#if !defined(CONFIG_MTK_LEGACY)
		.of_match_table = lcm_of_match,
#endif
	},
};

struct pinctrl* pinctrl_lcm;
struct pinctrl_state* lcm_enn_low, *lcm_enn_high, *lcm_enp_low, *lcm_enp_high;

/* Functions ------------------------------------------------------------------*/
int ocp2131_i2c_write_byte(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = lcm_bias_common_i2c_client;
	char write_data[2] = {0};

	LCM_LOGD("%s addr: %x, value: %x", __func__,addr,value);
	if(client == NULL)
	{
		LCM_LOGD("ERROR!! lcm_bias_common_i2c_client is null\n");
		return 0;
	}
	write_data[0] = addr;
	write_data[1] = value;
	ret=i2c_master_send(client, write_data, 2);
	if(ret<0)
		LCM_LOGD("ocp2131 write data fail !!\n");
	return ret ;
}
EXPORT_SYMBOL(ocp2131_i2c_write_byte);

static int ocp2131_probe(struct i2c_client *client)
{
	pinctrl_lcm = devm_pinctrl_get(&client->dev);
	lcm_enn_low = pinctrl_lookup_state(pinctrl_lcm, "lcm_enn_pin_out_low");
	lcm_enn_high = pinctrl_lookup_state(pinctrl_lcm, "lcm_enn_pin_out_high");
	lcm_enp_low = pinctrl_lookup_state(pinctrl_lcm, "lcm_enp_pin_out_low");
	lcm_enp_high = pinctrl_lookup_state(pinctrl_lcm, "lcm_enp_pin_out_high");

	LCM_LOGD("%s", __func__);
	lcm_bias_common_i2c_client = client;
	ocp2131_i2c_write_byte(0x00,0x14);
	udelay(1000);
	ocp2131_i2c_write_byte(0x01,0x14);
	return 0;
}

static void ocp2131_remove(struct i2c_client *client)
{
	LCM_LOGD("%s", __func__);
	lcm_bias_common_i2c_client = NULL;
	i2c_unregister_device(client);
}

int lcm_enn_set(int level)
{
	int ret = 0;

	pinctrl_select_state(pinctrl_lcm, (level == 0 ? lcm_enn_low : lcm_enn_high));

	return ret;
}

int lcm_enp_set(int level)
{
	int ret = 0;

	pinctrl_select_state(pinctrl_lcm, (level == 0 ? lcm_enp_low : lcm_enp_high));

	return ret;
}


static int __init ocp2131_iic_init(void)
{
	LCM_LOGD("%s", __func__);
#if defined(CONFIG_MTK_LEGACY)
	i2c_register_board_info(DCDC_I2C_BUSNUM, &ocp2131_board_info, 1);
#endif
	i2c_add_driver(&ocp2131_iic_driver);
	return 0;
}

static void __exit ocp2131_iic_exit(void)
{
	LCM_LOGD("%s", __func__);
	i2c_del_driver(&ocp2131_iic_driver);
}

module_init(ocp2131_iic_init);
module_exit(ocp2131_iic_exit);
MODULE_DESCRIPTION("OCP2131 I2C Driver");
MODULE_LICENSE("GPL");
