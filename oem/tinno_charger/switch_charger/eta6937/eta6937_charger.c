#include <linux/types.h>
#include <linux/init.h>     /* For init/exit macros */
#include <linux/module.h>   /* For MODULE_ marcros  */
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#endif
#include <linux/power_supply.h>
#include "eta6937_reg.h"
#include "../../drivers/power/supply/charger_class.h"
#include "../../drivers/power/supply/mtk_charger.h"
#include "../../drivers/misc/mediatek/usb20/mtk_musb.h"
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#ifdef CONFIG_T_PRODUCT_INFO
    #include <dev_info.h>
#endif

static const unsigned int INPUT_CSTH[] =
{
	300000, 500000, 800000, 1200000,
	1500000, 2000000, 3000000, 5000000
};

static const unsigned int CS_VTH[] =
{
	550000, 650000, 750000, 850000,
	950000, 1050000, 1150000, 1250000,
	1350000, 1450000, 1550000, 1650000,
	1750000, 1850000, 1950000, 2050000,
	2150000, 2250000, 2350000, 2450000,
	2550000, 2650000, 2750000, 2850000,
	2950000, 3050000, 3050000, 3050000,
	3050000, 3050000, 3050000, 3050000
};

static const unsigned int IMCHRG_VTH[] =
{
	550000, 750000, 950000, 1150000,
	1350000, 1550000, 1750000, 1950000,
	2150000, 2350000, 2550000, 2750000,
	2950000, 3050000, 3050000, 3050000
};

#define CHG_DEBUG
#ifdef CHG_DEBUG
    #define CHARGER_DEBUG(fmt, args...) printk(KERN_ERR "[eta6937][%s]"fmt"\n", __func__, ##args)
#else
    #define CHARGER_DEBUG(fmt, args...)
#endif
#define CHARGER_ERR(fmt, args...) printk(KERN_ERR "[eta6937][%s]"fmt"\n", __func__, ##args)

struct eta6937_info
{
	struct device *dev;
	struct i2c_client *i2c;
	struct charger_device *chg_dev;
	u8 chip_rev;
	struct regulator_dev *otg_rdev;
	int en_gpio;
};

static const struct eta6937_platform_data eta6937_def_platform_data =
{
	.chg_name = "secondary_chg",
	.ichg = 1550000, /* unit: uA */
	.aicr = 500000, /* unit: uA */
	.mivr = 4500000, /* unit: uV */
	.ieoc = 150000, /* unit: uA */
	.voreg = 4350000, /* unit : uV */
	.vmreg = 4350000, /* unit : uV */
	.intr_gpio = 5,
};


static struct i2c_client *new_client;

#define GETARRAYNUM(array) (sizeof(array)/sizeof(array[0]))

/**********************************************************
  *
  *   [Global Variable]
  *
  *********************************************************/
unsigned char eta6937_reg[ETA6937_REG_NUM] = { 0 };

static DEFINE_MUTEX(eta6937_i2c_access);

bool g_eta6937_hw_exist = 0;

static u32 charging_parameter_to_value(const u32 *parameter, const u32 array_size, const u32 val)
{
	u32 i;

	for (i = 0; i < array_size; i++)
	{
		if (val == *(parameter + i))
			return i;
	}
	//chr_info("NO register value match \r\n");

	return 0;
}


static u32 bmt_find_closest_level(const u32 *pList, u32 number, u32 level)
{
	u32 i;
	u32 max_value_in_last_element;

	if (pList[0] < pList[1])
		max_value_in_last_element = true;
	else
		max_value_in_last_element = false;

	if (max_value_in_last_element == true)
	{
		for (i = (number - 1); i != 0; i--)  /* max value in the last element */
		{
			if (pList[i] <= level)
			{
				return pList[i];
			}
		}
		//chr_info("Can't find closest level, small value first \r\n");
		return pList[0];
		/* return CHARGE_CURRENT_0_00_MA; */
	}
	else
	{
		for (i = 0; i < number; i++)     /* max value in the first element */
		{
			if (pList[i] <= level)
			{
				return pList[i];
			}
		}
		//chr_info("Can't find closest level, large value first \r\n");
		return pList[number - 1];
		/* return CHARGE_CURRENT_0_00_MA; */
	}
}

/**********************************************************
  *
  *   [I2C Function For Read/Write eta6937]
  *
  *********************************************************/
/*int eta6937_read_byte(unsigned char cmd, unsigned char *returnData)
{
	char buf[1] = { 0x00 };
	char readData = 0;
	int ret = 0;
	mutex_lock(&eta6937_i2c_access);
	ret = i2c_smbus_read_i2c_block_data(new_client, cmd, 1, buf);
	if (ret < 0) {
		printk("eta6937 read byte error\n");
		mutex_unlock(&eta6937_i2c_access);
		return 0;
	}
	mutex_unlock(&eta6937_i2c_access);
	readData = buf[0];
	*returnData = readData;
	return 1;
}*/
int eta6937_read_byte(unsigned char cmd, unsigned char *returnData)
{
	int ret = 0;
	struct i2c_msg msgs[] =
	{
		{
			.addr = new_client->addr,
			.flags = 0,
			.len = 1,
			.buf = &cmd,
		},
		{
			.addr = new_client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = returnData,
		},
	};
	mutex_lock(&eta6937_i2c_access);
	ret = i2c_transfer(new_client->adapter, msgs, 2);
	if (ret < 0)
	{
		chr_err("%s: read 0x%x register failed\n", __func__, cmd);
	}
	mutex_unlock(&eta6937_i2c_access);
	return ret;
}


/*int eta6937_write_byte(unsigned char cmd, unsigned char writeData)
{
	char write_data[2] = { 0 };
	int ret = 0;

	mutex_lock(&eta6937_i2c_access);

	write_data[0] = cmd;
	write_data[1] = writeData;

	ret = i2c_master_send(new_client, write_data, 2);
	if (ret < 0) {
		printk("eta6937 write byte error\n");
		mutex_unlock(&eta6937_i2c_access);
		return 0;
	}

	mutex_unlock(&eta6937_i2c_access);
	return 1;
}*/

int eta6937_write_byte(unsigned char cmd, unsigned char writeData)
{
	char write_data[2] = { 0 };
	int ret = 0;
	struct i2c_msg msgs[] =
	{
		{
			.addr = new_client->addr,
			.flags = 0,
			.len = 2,
			.buf = write_data,
		},
	};

	write_data[0] = cmd;
	write_data[1] = writeData;
	mutex_lock(&eta6937_i2c_access);

	ret = i2c_transfer(new_client->adapter, msgs, 1);
	if (ret < 0)
	{
		pr_err("eta6937_write_byte failed\n");
		mutex_unlock(&eta6937_i2c_access);
		return ret;
	}
	mutex_unlock(&eta6937_i2c_access);
	return 1;
}


/**********************************************************
  *
  *   [Read / Write Function]
  *
  *********************************************************/
unsigned int eta6937_read_interface(unsigned char RegNum, unsigned char *val, unsigned char MASK,
                                    unsigned char SHIFT)
{
	unsigned char eta6937_reg = 0;
	int ret = 0;
	ret = eta6937_read_byte(RegNum, &eta6937_reg);
	//chr_info("[eta6937_read_interface] Reg[%x]=0x%x\n", RegNum, eta6937_reg);

	eta6937_reg &= (MASK << SHIFT);
	*val = (eta6937_reg >> SHIFT);

	//chr_info("[eta6937_read_interface] val=0x%x\n", *val);

	return ret;
}

int eta6937_config_interface(unsigned char RegNum, unsigned char val, unsigned char MASK, unsigned char SHIFT)
{
	unsigned char eta6937_reg = 0;
	int ret = 0;

	ret = eta6937_read_byte(RegNum, &eta6937_reg);
	if (ret < 0)
	{
		return ret;
	}

	eta6937_reg &= ~(MASK << SHIFT);
	eta6937_reg |= (val << SHIFT);
	if (RegNum == ETA6937_CON4 && val == 1 && MASK == CON4_RESET_MASK && SHIFT == CON4_RESET_SHIFT)
	{
		/* RESET bit */
	}
	else if (RegNum == ETA6937_CON4)
	{
		eta6937_reg &= ~0x80;   /* RESET bit read returs 1, so clear it */
	}

	ret = eta6937_write_byte(RegNum, eta6937_reg);

	return ret;
}

/* write one register directly */
unsigned int eta6937_reg_config_interface(unsigned char RegNum, unsigned char val)
{
	int ret = 0;

	ret = eta6937_write_byte(RegNum, val);

	return ret;
}

/**********************************************************
  *
  *   [Internal Function]
  *
  *********************************************************/
/* CON0 */

void eta6937_set_tmr_rst(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON0),
				(unsigned char)(val),
				(unsigned char)(CON0_TMR_RST_MASK),
				(unsigned char)(CON0_TMR_RST_SHIFT)
				);
}

unsigned int eta6937_get_otg_status(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = eta6937_read_interface((unsigned char)(ETA6937_CON0),
				(&val), (unsigned char)(CON0_OTG_MASK),
				(unsigned char)(CON0_OTG_SHIFT)
				);
	return val;
}

void eta6937_set_en_stat(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON0),
				(unsigned char)(val),
				(unsigned char)(CON0_EN_STAT_MASK),
				(unsigned char)(CON0_EN_STAT_SHIFT)
				);
}

unsigned int eta6937_get_chip_status(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = eta6937_read_interface((unsigned char)(ETA6937_CON0),
				(&val), (unsigned char)(CON0_STAT_MASK),
				(unsigned char)(CON0_STAT_SHIFT)
				);
	return val;
}

unsigned int eta6937_get_boost_status(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = eta6937_read_interface((unsigned char)(ETA6937_CON0),
				(&val), (unsigned char)(CON0_BOOST_MASK),
				(unsigned char)(CON0_BOOST_SHIFT)
				);
	return val;
}

unsigned int eta6937_get_fault_status(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = eta6937_read_interface((unsigned char)(ETA6937_CON0),
				(&val), (unsigned char)(CON0_FAULT_MASK),
				(unsigned char)(CON0_FAULT_SHIFT)
				);
	return val;
}

/* CON1 */

void eta6937_set_input_charging_current(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON1),
				(unsigned char)(val),
				(unsigned char)(CON1_LIN_LIMIT_MASK),
				(unsigned char)(CON1_LIN_LIMIT_SHIFT)
				);
}
EXPORT_SYMBOL(eta6937_set_input_charging_current);

void eta6937_set_v_low(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON1),
				(unsigned char)(val),
				(unsigned char)(CON1_LOW_V_MASK),
				(unsigned char)(CON1_LOW_V_SHIFT)
				);
}

void eta6937_set_te(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON1),
				(unsigned char)(val),
				(unsigned char)(CON1_TE_MASK),
				(unsigned char)(CON1_TE_SHIFT)
				);
}

void eta6937_set_ce(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON1),
				(unsigned char)(val),
				(unsigned char)(CON1_CE_MASK),
				(unsigned char)(CON1_CE_SHIFT)
				);
}
EXPORT_SYMBOL(eta6937_set_ce);

unsigned int  eta6937_get_ce(void)
{
	unsigned int ret = 0;
	unsigned int val = 0;

	ret = eta6937_read_interface((unsigned char)(ETA6937_CON1),
				(unsigned char *)(&val),
				(unsigned char)(CON1_CE_MASK),
				(unsigned char)(CON1_CE_SHIFT)
				);
	return val;
}

void eta6937_set_hz_mode(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON1),
				(unsigned char)(val),
				(unsigned char)(CON1_HZ_MODE_MASK),
				(unsigned char)(CON1_HZ_MODE_SHIFT)
				);
}
EXPORT_SYMBOL_GPL(eta6937_set_hz_mode);

void eta6937_set_opa_mode(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON1),
				(unsigned char)(val),
				(unsigned char)(CON1_OPA_MODE_MASK),
				(unsigned char)(CON1_OPA_MODE_SHIFT)
				);
}
EXPORT_SYMBOL_GPL(eta6937_set_opa_mode);

/* CON2 */

void eta6937_set_oreg(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON2),
				(unsigned char)(val),
				(unsigned char)(CON2_OREG_MASK),
				(unsigned char)(CON2_OREG_SHIFT)
				);
}

void eta6937_set_otg_pl(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON2),
				(unsigned char)(val),
				(unsigned char)(CON2_OTG_PL_MASK),
				(unsigned char)(CON2_OTG_PL_SHIFT)
				);
}

void eta6937_set_otg_en(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON2),
				(unsigned char)(val),
				(unsigned char)(CON2_OTG_EN_MASK),
				(unsigned char)(CON2_OTG_EN_SHIFT)
				);
}

/* CON3 */

unsigned int eta6937_get_vender_code(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = eta6937_read_interface((unsigned char)(ETA6937_CON3),
				(&val), (unsigned char)(CON3_VENDER_CODE_MASK),
				(unsigned char)(CON3_VENDER_CODE_SHIFT)
				);
	return val;
}

unsigned int eta6937_get_pn(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = eta6937_read_interface((unsigned char)(ETA6937_CON3),
				(&val), (unsigned char)(CON3_PIN_MASK),
				(unsigned char)(CON3_PIN_SHIFT)
				);
	return val;
}

unsigned int eta6937_get_revision(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = eta6937_read_interface((unsigned char)(ETA6937_CON3),
				(&val), (unsigned char)(CON3_REVISION_MASK),
				(unsigned char)(CON3_REVISION_SHIFT)
				);
	return val;
}

/* CON4 */

void eta6937_set_reset(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON4),
				(unsigned char)(val),
				(unsigned char)(CON4_RESET_MASK),
				(unsigned char)(CON4_RESET_SHIFT)
				);
}

void eta6937_set_icharge(unsigned int val) // 400 200 100
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON4),
				(unsigned char)(val),
				(unsigned char)(CON4_I_CHR_MASK),
				(unsigned char)(CON4_I_CHR_SHIFT)
				);
}

void eta6937_set_icharge_offset(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON4),
				(unsigned char)(val),
				(unsigned char)(CON4_ICHR_OFFSET_MASK),
				(unsigned char)(CON4_ICHR_OFFSET_SHIFT)
				);
}

void eta6937_set_iterm(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON4),
				(unsigned char)(val),
				(unsigned char)(CON4_I_TERM_MASK),
				(unsigned char)(CON4_I_TERM_SHIFT)
				);
}

/* CON5 */

void eta6937_set_icharge4(unsigned int val)// 1600 0
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON5),
				(unsigned char)(val),
				(unsigned char)(CON5_ICHG_4_MASK),// 1
				(unsigned char)(CON5_ICHG_4_SHIFT) //7
				);
}

void eta6937_set_icharge3(unsigned int val) // 800 0
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON5),
				(unsigned char)(val),
				(unsigned char)(CON5_ICHG_3_MASK),
				(unsigned char)(CON5_ICHG_3_SHIFT)
				);
}

void eta6937_set_low_charge(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON5),
				(unsigned char)(val),
				(unsigned char)(CON5_LOW_CHG_MASK),
				(unsigned char)(CON5_LOW_CHG_SHIFT)
				);
}

void eta6937_get_dpm_status(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON5),
				(unsigned char)(val),
				(unsigned char)(CON5_DPM_STATUS_MASK),
				(unsigned char)(CON5_DPM_STATUS_SHIFT)
				);
}

unsigned int eta6937_get_cd_status(void)
{
	unsigned int ret = 0;
	unsigned char val = 0;

	ret = eta6937_read_interface((unsigned char)(ETA6937_CON5),
				(&val), (unsigned char)(CON5_CD_STATUS_MASK),
				(unsigned char)(CON5_CD_STATUS_SHIFT)
				);
	return val;
}

void eta6937_set_vindpm(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON5),
				(unsigned char)(val),
				(unsigned char)(CON5_VINDPM_MASK),
				(unsigned char)(CON5_VINDPM_SHIFT)
				);
}
/* CON6 */

void eta6937_set_imchrg(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON6),
				(unsigned char)(val),
				(unsigned char)(CON6_IMCHRG_MASK),
				(unsigned char)(CON6_IMCHRG_SHIFT)
				);
}

void eta6937_set_vmreg(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON6),
				(unsigned char)(val),
				(unsigned char)(CON6_VMREG_MASK),
				(unsigned char)(CON6_VMREG_SHIFT)
				);
}

/* CON7 */

void eta6937_set_vindpm2(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON7),
				(unsigned char)(val),
				(unsigned char)(CON7_VINDPM_MASK),
				(unsigned char)(CON7_VINDPM_SHIFT)
				);
}

void eta6937_set_en_ilim2(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON7),
				(unsigned char)(val),
				(unsigned char)(CON7_EN_ILIM2_MASK),
				(unsigned char)(CON7_EN_ILIM2_SHIFT)
				);
}

void eta6937_set_iin_limit2(unsigned int val)
{
	unsigned int ret = 0;

	ret = eta6937_config_interface((unsigned char)(ETA6937_CON7),
				(unsigned char)(val),
				(unsigned char)(CON7_IIN_LIMIT_2_MASK),
				(unsigned char)(CON7_IIN_LIMIT_2_SHIFT)
				);
}
/**********************************************************
  *
  *   [Internal Function]
  *
  *********************************************************/
void eta6937_dump_register(void)
{
	int i = 0;

	//chr_info("[eta6937_dump_register] ");
	for (i = 0; i < ETA6937_REG_NUM; i++)
	{
		eta6937_read_byte(i, &eta6937_reg[i]);
		CHARGER_ERR("[0x%x]=0x%x", i, eta6937_reg[i]);
		//chr_info("[0x%x]=0x%x ", i, eta6937_reg[i]);
	}
	//chr_info("\n");
}

static void eta6937_hw_init(void)
{
	eta6937_reg_config_interface(0x00, 0xC0);   /* kick chip watch dog */
	eta6937_reg_config_interface(0x01, 0xf8);   /* TE=1, CE=0, HZ_MODE=0, OPA_MODE=0 */
	eta6937_reg_config_interface(0x02, 0xB6);//voreg:4.40V
	eta6937_reg_config_interface(0x04, 0x02);//iterm 150 mA
	eta6937_reg_config_interface(0x05, 0x03);//vindpm 4.2+0.16 +0.08 = 4.44
	eta6937_reg_config_interface(0x06, 0x4A);// IMCHARG 1150mA; VMREG 4.42V
	eta6937_reg_config_interface(0x07, 0x0B);//en iilim2:1 iin_limit_2:1200mA
}

static const unsigned long eta6937_support_iaicr[] = { 100, 500, 800, 1000};
#if 0
static  int eta6937_set_aicr(unsigned long uA)
{
	u8 iaicr = 0;
	unsigned long mA = uA / 1000;
	int i = 0;
	/* change by sw control */

	for (i = 0; i < ARRAY_SIZE(eta6937_support_iaicr); i++)
	{
		if (mA < eta6937_support_iaicr[i])
			break;
	}
	if (i == ARRAY_SIZE(eta6937_support_iaicr))
		pr_err("will config to no limit\n");
	if (i > 0)
		i--;
	switch (i)
	{
		case 0:
			iaicr = 0;//100 ma input current limit
			break;
		case 1:
			iaicr = 1;//500 ma input current limit
			break;
		case 2:
			iaicr = 2;//800 ma input current limit
			break;
		case 3:
			iaicr = 3;//No input current limit
			break;
		default:
			pr_err("illegal selection\n");
			return -EINVAL;
	}
	eta6937_set_input_charging_current(iaicr);
	return 0;
}
#endif
/* mV */
#define ETA6937_BAT_VOREG_NUM   64
#define ETA6937_BAT_VOREG_MIN   3500
#define ETA6937_BAT_VOREG_MAX   4440
#define ETA6937_BAT_VOREG_STEP  20

#define ETA6937_BAT_VMREG_NUM   16
#define ETA6937_BAT_VMREG_MIN   4200
#define ETA6937_BAT_VMREG_MAX   4440
#define ETA6937_BAT_VMREG_STEP  20

static u8 eta6937_find_closest_reg_value(u32 min, u32 max, u32 step, u32 num,
        u32 target)
{
	u32 i = 0, cur_val = 0, next_val = 0;

	/* Smaller than minimum supported value, use minimum one */
	if (target < min)
		return 0;

	for (i = 0; i < num - 1; i++)
	{
		cur_val = min + i * step;
		next_val = cur_val + step;

		if (cur_val > max)
			cur_val = max;

		if (next_val > max)
			next_val = max;

		if (target >= cur_val && target < next_val)
			return i;
	}

	/* Greater than maximum supported value, use maximum one */
	return num - 1;
}

static  int eta6937_set_cvreg(unsigned long uV)
{
	u8 data = 0;

	data = eta6937_find_closest_reg_value(
					ETA6937_BAT_VOREG_MIN,
					ETA6937_BAT_VOREG_MAX,
					ETA6937_BAT_VOREG_STEP,
					ETA6937_BAT_VOREG_NUM,
					uV / 1000
				);

	eta6937_set_oreg(data);

	return 0;
}

static  int eta6937_set_cvmreg(unsigned long uV)
{
	u8 data = 0;

	data = eta6937_find_closest_reg_value(
					ETA6937_BAT_VMREG_MIN,
					ETA6937_BAT_VMREG_MAX,
					ETA6937_BAT_VMREG_STEP,
					ETA6937_BAT_VMREG_NUM,
					uV / 1000
				);

	eta6937_set_vmreg(data);

	return 0;
}

static int eta6937_set_ichg(u32 uA)
{
	u8 tmp = 0;
	u32 set_chr_current;
	u32 array_size;
	u32 register_value;
	u32 current_value = uA;

	array_size = GETARRAYNUM(CS_VTH);
	set_chr_current = bmt_find_closest_level(CS_VTH, array_size, current_value);
	register_value = charging_parameter_to_value(CS_VTH, array_size, set_chr_current);

#ifdef CONFIG_PROJECT_U5020F
	register_value = register_value - 0x1;
#endif

	tmp = 0;
	eta6937_set_icharge(tmp);
	tmp = 0;
	eta6937_set_icharge3(tmp);
	tmp = 1;
	eta6937_set_icharge4(tmp);

	return 0;
}

static int eta6937_set_imchg(u32 uA)
{
	//u8 tmp = 0;
	u32 set_chr_current;
	u32 array_size;
	u32 register_value;
	u32 current_value = uA;

	array_size = GETARRAYNUM(IMCHRG_VTH);
	set_chr_current = bmt_find_closest_level(IMCHRG_VTH, array_size, current_value);
	register_value = charging_parameter_to_value(IMCHRG_VTH, array_size, set_chr_current);

	eta6937_set_imchrg(register_value);

	return 0;
}

bool eta6937_hw_component_detect(void)
{
	if (eta6937_get_vender_code() == 0x02 && eta6937_get_pn() == 0x02)
		g_eta6937_hw_exist = 1;
	else
		g_eta6937_hw_exist = 0;
	pr_debug("[%s] g_eta6937_hw_exist = %d\n", __func__, g_eta6937_hw_exist);

	return g_eta6937_hw_exist;
}

int is_eta6937_exist(void)
{
	pr_debug("[%s] g_eta6937_hw_exist=%d\n", __func__, g_eta6937_hw_exist);

	return g_eta6937_hw_exist;
}
int eta6937_get_charging_status(void)
{
	unsigned char reg_val = 0;
	bool chg_en = false;
	int ret;

	reg_val = eta6937_get_chip_status();
	chg_en = eta6937_get_ce();

	switch (reg_val)
	{
		case eta6937_CHG_STATUS_READY:
		/* fallthrough */
		case eta6937_CHG_STATUS_PROGESS:
			if (!chg_en)
				ret = POWER_SUPPLY_STATUS_CHARGING;
			else
				ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		case eta6937_CHG_STATUS_DONE:
			ret = POWER_SUPPLY_STATUS_FULL;
			break;
		case eta6937_CHG_STATUS_FAULT:
			ret = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		default:
			ret = -ENODATA;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(eta6937_get_charging_status);

static int eta6937_charger_is_charging_done(struct charger_device *chg_dev,
        bool *done)
{
	int ret = 0;
	CHARGER_DEBUG();
	ret = eta6937_get_chip_status();
	if (ret < 0)
		return ret;

	if (ret == 0x02)
		*done = true;
	else
		*done = false;
	return 0;
}

static int eta6937_charger_enable_otg(struct charger_device *chg_dev, bool en)
{
	CHARGER_DEBUG();

	if (en)
	{
		eta6937_set_opa_mode(1);
		eta6937_set_otg_pl(1);
		eta6937_set_otg_en(1);
	}
	else
	{
		eta6937_set_opa_mode(0);
		eta6937_set_otg_pl(0);
		eta6937_set_otg_en(0);
	}

	return 0;
}

static int eta6937_charger_enable_te(struct charger_device *chg_dev, bool en)
{
	//do nothing
	CHARGER_DEBUG();
	return 0;
}

static int eta6937_charger_enable_timer(struct charger_device *chg_dev, bool en)
{
	CHARGER_DEBUG();
	eta6937_set_tmr_rst(1);
	return 0;
}

static int eta6937_charger_kick_wdt(struct charger_device *dev)
{
	CHARGER_DEBUG();
	eta6937_set_tmr_rst(1);
	return 0;
}
#if 0
static int eta6937_charger_is_timer_enabled(struct charger_device *chg_dev,
        bool *en)
{
	CHARGER_DEBUG();
	eta6937_set_tmr_rst(0);
	return 0;
}
#endif
static int eta6937_charger_set_mivr(struct charger_device *chg_dev, u32 uV)
{
	//do nothing
	CHARGER_DEBUG();
	return 0;
}
#if 0
static int eta6937_charger_get_min_aicr(struct charger_device *chg_dev, u32 *uA)
{
	//do nothing
	CHARGER_DEBUG();
	*uA = 100000;
	return 0;
}

static int eta6937_charger_set_aicr(struct charger_device *chg_dev, u32 uA)
{
	CHARGER_DEBUG("ua=%d", uA);
	return eta6937_set_aicr(uA);
}
#endif
static int eta6937_set_input_current(struct charger_device *chg_dev, u32 uA)
{
	unsigned int status = 0;
	unsigned int set_chr_current;
	unsigned int array_size;
	unsigned int register_value;

	CHARGER_DEBUG("uA=%d", uA);

	array_size = ARRAY_SIZE(INPUT_CSTH);
	set_chr_current = bmt_find_closest_level(INPUT_CSTH, array_size, uA);
	register_value = charging_parameter_to_value(INPUT_CSTH, array_size, set_chr_current);

	eta6937_set_en_ilim2(1);
	eta6937_set_iin_limit2(7);

	return status;
}

static int eta6937_charger_get_aicr(struct charger_device *chg_dev, u32 *uA)
{
	//do nothing
	CHARGER_DEBUG();
	return 0;
}

static int eta6937_charger_get_cv(struct charger_device *chg_dev, u32 *uV)
{
	//do nothing
	CHARGER_DEBUG();
	return 0;
}

static int eta6937_charger_set_cv(struct charger_device *chg_dev, u32 uV)
{
	CHARGER_DEBUG("uV=%d", uV);
	eta6937_set_cvreg(uV);
	eta6937_set_cvmreg(uV);

	return 0;
}

static int eta6937_charger_get_min_ichg(struct charger_device *chg_dev, u32 *uA)
{
	//do nothing
	CHARGER_DEBUG();
	*uA = 500000;
	return 0;
}

static int eta6937_charger_set_ichg(struct charger_device *chg_dev, u32 uA)
{
	CHARGER_DEBUG("current=%d", uA);
	eta6937_set_tmr_rst(1);

	eta6937_set_ichg(uA);
	eta6937_set_imchg(uA);
	return 0;
}

static int eta6937_charger_get_ichg(struct charger_device *chg_dev, u32 *uA)
{
	//do nothing
	CHARGER_DEBUG();
	return 0;
}

static int eta6937_charger_is_enabled(struct charger_device *chg_dev, bool *en)
{
	CHARGER_DEBUG();
	eta6937_set_ce(1);
	return 0;
}

static int eta6937_charger_enable(struct charger_device *chg_dev, bool en)
{
	CHARGER_DEBUG();
	if (en)
	{
		eta6937_set_ce(0);
		eta6937_set_hz_mode(0);
		eta6937_set_opa_mode(0);
	}
	else
	{
#if defined(CONFIG_USB_MTK_HDRC_HCD)
		if (mt_usb_is_device())
#endif
			eta6937_set_ce(1);
	}
	return 0;
}

static int eta6937_charger_plug_out(struct charger_device *chg_dev)
{
	//do nothing
	CHARGER_DEBUG();
	return 0;
}

static int eta6937_charger_plug_in(struct charger_device *chg_dev)
{
	//do nothing
	CHARGER_DEBUG();
	return 0;
}

static int eta6937_charger_dump_registers(struct charger_device *chg_dev)
{
	CHARGER_DEBUG();
	eta6937_dump_register();
	return 0;
}

static int eta6937_charger_do_event(struct charger_device *chg_dev, u32 event,
                                    u32 args)
{
	struct eta6937_info *ri = charger_get_data(chg_dev);
	struct power_supply *chg_psy = NULL;

	if (chg_dev == NULL)
		return -EINVAL;

	chg_psy = devm_power_supply_get_by_phandle(ri->dev, "charger");

	if (!chg_psy)
	{
		dev_notice(ri->dev, "%s: cannot get psy\n", __func__);
		return -ENODEV;
	}

	switch (event)
	{
		case EVENT_FULL:
		case EVENT_RECHARGE:
		case EVENT_DISCHARGE:
			power_supply_changed(chg_psy);
			break;
		default:
			break;
	}
	return 0;
}

static int eta6937_enable_vbus(struct regulator_dev *rdev)
{
	eta6937_set_opa_mode(1);
	eta6937_set_otg_pl(1);
	eta6937_set_otg_en(1);

	return 0;
}

static int eta6937_disable_vbus(struct regulator_dev *rdev)
{
	eta6937_set_opa_mode(0);
	eta6937_set_otg_pl(0);
	eta6937_set_otg_en(0);

	return 0;
}

static int eta6937_is_enabled_vbus(struct regulator_dev *rdev)
{
	return eta6937_get_boost_status();
}

static const struct regulator_ops eta6937_vbus_ops =
{
	.enable = eta6937_enable_vbus,
	.disable = eta6937_disable_vbus,
	.is_enabled = eta6937_is_enabled_vbus,
};

static const struct regulator_init_data eta6937_vbus_init_data =
{
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static const struct regulator_desc eta6937_otg_rdesc =
{
	.of_match = "usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &eta6937_vbus_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static const struct charger_ops eta6937_chg_ops =
{
	/* cable plug in/out */
	.plug_in = eta6937_charger_plug_in,
	.plug_out = eta6937_charger_plug_out,
	/* enable */
	.enable = eta6937_charger_enable,
	.is_enabled = eta6937_charger_is_enabled,
	/* charging current */
	.get_charging_current = eta6937_charger_get_ichg,
	.set_charging_current = eta6937_charger_set_ichg,
	.get_min_charging_current = eta6937_charger_get_min_ichg,
	/* charging voltage */
	.set_constant_voltage = eta6937_charger_set_cv,
	.get_constant_voltage = eta6937_charger_get_cv,
	/* charging input current */
	.get_input_current = eta6937_charger_get_aicr,
	.set_input_current = eta6937_set_input_current,
	/* charging mivr */
	.set_mivr = eta6937_charger_set_mivr,
	/* safety timer */
	//.is_safety_timer_enabled = eta6937_charger_is_timer_enabled,
	.enable_safety_timer = eta6937_charger_enable_timer,
	/*kick wdt*/
	.kick_wdt = eta6937_charger_kick_wdt,
	/* charing termination */
	.enable_termination = eta6937_charger_enable_te,
	/* OTG */
	.enable_otg = eta6937_charger_enable_otg,
	/* misc */
	.is_charging_done = eta6937_charger_is_charging_done,
	.dump_registers = eta6937_charger_dump_registers,
	/* event */
	.event = eta6937_charger_do_event,
};

static const struct charger_properties eta6937_chg_props =
{
	.alias_name = "eta6937",
};

static void bq_parse_dt(struct device *dev, struct eta6937_platform_data *pdata)
{
	/* just used to prevent the null parameter */
	if (!dev || !pdata)
		return;
	if (of_property_read_string(dev->of_node, "chg_name",
					&pdata->chg_name) < 0)
		dev_warn(dev, "not specified chg_name\n");
	if (of_property_read_u32(dev->of_node, "ichg", &pdata->ichg) < 0)
		dev_warn(dev, "not specified ichg value\n");
	if (of_property_read_u32(dev->of_node, "aicr", &pdata->aicr) < 0)
		dev_warn(dev, "not specified aicr value\n");
	if (of_property_read_u32(dev->of_node, "mivr", &pdata->mivr) < 0)
		dev_warn(dev, "not specified mivr value\n");
	if (of_property_read_u32(dev->of_node, "ieoc", &pdata->ieoc) < 0)
		dev_warn(dev, "not specified ieoc_value\n");
	if (of_property_read_u32(dev->of_node, "cv", &pdata->voreg) < 0)
		dev_warn(dev, "not specified cv value\n");
	if (of_property_read_u32(dev->of_node, "vmreg", &pdata->vmreg) < 0)
		dev_warn(dev, "not specified vmreg value\n");
	//pdata->enable_te = of_property_read_bool(dev->of_node, "enable_te");
	pdata->enable_eoc_shdn = of_property_read_bool(dev->of_node, "enable_eoc_shdn");
}

static int eta6937_i2c_probe(struct i2c_client *i2c,
                             const struct i2c_device_id *id)
{
	struct eta6937_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct eta6937_info *ri = NULL;
	u8 rev_id = 0;
	bool use_dt = i2c->dev.of_node;
	int ret = 0;
	struct regulator_config config = { };

	pr_err("%s start\n", __func__);

	/* if success, return value is revision id */
	rev_id = (u8)ret;
	/* driver data */
	ri = devm_kzalloc(&i2c->dev, sizeof(*ri), GFP_KERNEL);
	if (!ri)
	{
		return -ENOMEM;
	}
	/* platform data */
	if (use_dt)
	{
		pdata = devm_kzalloc(&i2c->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		memcpy(pdata, &eta6937_def_platform_data, sizeof(*pdata));
		i2c->dev.platform_data = pdata;
		bq_parse_dt(&i2c->dev, pdata);
	}
	else
	{
		if (!pdata)
		{
			pr_err("no pdata specify\n");
			return -EINVAL;
		}
	}

	new_client = i2c;
	//ret = eta6937_read_interface(0x03, &val, 0xFF, 0x0);;
	ri->dev = &i2c->dev;
	ri->i2c = i2c;
	ri->chip_rev = rev_id;
	i2c_set_clientdata(i2c, ri);

	if (!eta6937_hw_component_detect())
	{
		pr_err("eta6937 is not exist\n");
		return -ENODEV;
	}

	ri->en_gpio = of_get_named_gpio(ri->dev->of_node, "eta,en_gpio", 0);
	if (!gpio_is_valid(ri->en_gpio)) {
		dev_err(ri->dev, "eta,en_gpio");
		return -EINVAL;
   }
	ret = gpio_request_one(ri->en_gpio, GPIOF_OUT_INIT_HIGH, "eta_chg_en_pin");
	if (ret) {
		dev_err(ri->dev, "request eta,en_gpio failed");
		return -EPERM;
	}

	eta6937_hw_init();

	eta6937_dump_register();

	/* charger class register */
	ri->chg_dev = charger_device_register(pdata->chg_name, ri->dev, ri,
									&eta6937_chg_ops,
									&eta6937_chg_props);
	if (IS_ERR(ri->chg_dev))
	{
		pr_err("charger device register fail\n");
		return PTR_ERR(ri->chg_dev);
	}

	/* otg regulator */
	config.init_data = &eta6937_vbus_init_data;
	config.dev = ri->dev;
	config.driver_data = ri;
	ri->otg_rdev = devm_regulator_register(ri->dev, &eta6937_otg_rdesc, &config);
	if (IS_ERR(ri->otg_rdev))
	{
		ret = PTR_ERR(ri->otg_rdev);
		pr_info("%s: register otg regulator failed (%d)\n", __func__, ret);
		return ret;
	}

	//g_primary_chg_type = ETA6937;

#ifdef CONFIG_T_PRODUCT_INFO
	FULL_PRODUCT_DEVICE_INFO(ID_CHARGECHIP, "eta6937");
#endif

	pr_info("%s: probe set current = %d)\n", __func__, ret);
	eta6937_set_tmr_rst(1);
  eta6937_set_low_charge(0);
	eta6937_set_ce(1);//disable charging

	return 0;
}

static int eta6937_i2c_remove(struct i2c_client *i2c)
{
	//do nothing
	return 0;
}

static void eta6937_i2c_shutdown(struct i2c_client *i2c)
{
	//do nothing
}

static int eta6937_i2c_suspend(struct device *dev)
{
	//do nothing
	return 0;
}

static int eta6937_i2c_resume(struct device *dev)
{
	//do nothing
	return 0;
}

static SIMPLE_DEV_PM_OPS(eta6937_pm_ops, eta6937_i2c_suspend, eta6937_i2c_resume);

static const struct of_device_id of_id_table[] =
{
	{ .compatible = "mediatek,eta6937"},
	{},
};
MODULE_DEVICE_TABLE(of, of_id_table);

static const struct i2c_device_id i2c_id_table[] =
{
	{ "eta6937", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, i2c_id_table);

static struct i2c_driver eta6937_i2c_driver =
{
	.driver = {
		.name = "eta6937",
		.owner = THIS_MODULE,
		.pm = &eta6937_pm_ops,
		.of_match_table = of_match_ptr(of_id_table),
	},
	.probe = eta6937_i2c_probe,
	.remove = eta6937_i2c_remove,
	.shutdown = eta6937_i2c_shutdown,
	.id_table = i2c_id_table,
};
module_i2c_driver(eta6937_i2c_driver);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C eta6937 Driver");
MODULE_AUTHOR("James Lo<james.lo@mediatek.com>");
