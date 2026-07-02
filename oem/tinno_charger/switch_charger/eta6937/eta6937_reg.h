/*****************************************************************************
*
* Filename:
* ---------
*   eta6937.h
*
* Project:
* --------
*   Android
*
* Description:
* ------------
*   eta6937 header file
*
* Author:
* -------
*
****************************************************************************/

#ifndef _ETA6937_SW_H_
#define _ETA6937_SW_H_

#define ETA6937_CON0      0x00
#define ETA6937_CON1      0x01
#define ETA6937_CON2      0x02
#define ETA6937_CON3      0x03
#define ETA6937_CON4      0x04
#define ETA6937_CON5      0x05
#define ETA6937_CON6      0x06
#define ETA6937_CON7      0x07
#define ETA6937_REG_NUM 8

/**********************************************************
  *
  *   [MASK/SHIFT]
  *
  *********************************************************/
/* CON0 */
#define CON0_TMR_RST_MASK   0x01
#define CON0_TMR_RST_SHIFT  7

#define CON0_OTG_MASK       0x01
#define CON0_OTG_SHIFT      7

#define CON0_EN_STAT_MASK   0x01
#define CON0_EN_STAT_SHIFT  6

#define CON0_STAT_MASK      0x03
#define CON0_STAT_SHIFT     4

#define CON0_BOOST_MASK     0x01
#define CON0_BOOST_SHIFT    3

#define CON0_FAULT_MASK     0x07
#define CON0_FAULT_SHIFT    0

/* CON1 */
#define CON1_LIN_LIMIT_MASK     0x03
#define CON1_LIN_LIMIT_SHIFT    6

#define CON1_LOW_V_MASK     0x03
#define CON1_LOW_V_SHIFT    4

#define CON1_TE_MASK        0x01
#define CON1_TE_SHIFT       3

#define CON1_CE_MASK        0x01
#define CON1_CE_SHIFT       2

#define CON1_HZ_MODE_MASK   0x01
#define CON1_HZ_MODE_SHIFT  1

#define CON1_OPA_MODE_MASK  0x01
#define CON1_OPA_MODE_SHIFT 0

/* CON2 */
#define CON2_OREG_MASK    0x3F
#define CON2_OREG_SHIFT   2

#define CON2_OTG_PL_MASK    0x01
#define CON2_OTG_PL_SHIFT   1

#define CON2_OTG_EN_MASK    0x01
#define CON2_OTG_EN_SHIFT   0

/* CON3 */
#define CON3_VENDER_CODE_MASK   0x07
#define CON3_VENDER_CODE_SHIFT  5

#define CON3_PIN_MASK           0x03
#define CON3_PIN_SHIFT          3

#define CON3_REVISION_MASK      0x07
#define CON3_REVISION_SHIFT     0

/* CON4 */
#define CON4_RESET_MASK     0x01
#define CON4_RESET_SHIFT    7

#define CON4_I_CHR_MASK     0x07
#define CON4_I_CHR_SHIFT    4

#define CON4_ICHR_OFFSET_MASK     0x01
#define CON4_ICHR_OFFSET_SHIFT    3

#define CON4_I_TERM_MASK    0x07
#define CON4_I_TERM_SHIFT   0

/* CON5 */
#define CON5_ICHG_4_MASK      0x01
#define CON5_ICHG_4_SHIFT     7

#define CON5_ICHG_3_MASK      0x01
#define CON5_ICHG_3_SHIFT     6

#define CON5_LOW_CHG_MASK      0x01
#define CON5_LOW_CHG_SHIFT     5

#define CON5_DPM_STATUS_MASK     0x01
#define CON5_DPM_STATUS_SHIFT    4

#define CON5_CD_STATUS_MASK     0x01
#define CON5_CD_STATUS_SHIFT    3

#define CON5_VINDPM_MASK     0x07
#define CON5_VINDPM_SHIFT    0

/* CON6 */
#define CON6_IMCHRG_MASK     0x0F
#define CON6_IMCHRG_SHIFT    4

#define CON6_VMREG_MASK     0x0F
#define CON6_VMREG_SHIFT    0

/* CON7 */
#define CON7_VINDPM_MASK     0x0F
#define CON7_VINDPM_SHIFT    4

#define CON7_EN_ILIM2_MASK     0x01
#define CON7_EN_ILIM2_SHIFT    3

#define CON7_IIN_LIMIT_2_MASK     0x07
#define CON7_IIN_LIMIT_2_SHIFT    0

enum ETA6937_charging_status
{
	eta6937_CHG_STATUS_READY = 0,
	eta6937_CHG_STATUS_PROGESS,
	eta6937_CHG_STATUS_DONE,
	eta6937_CHG_STATUS_FAULT,
	eta6937_CHG_STATUS_MAX,
};

struct eta6937_platform_data
{
	const char *chg_name;
	u32 ichg;
	u32 aicr;
	u32 mivr;
	u32 ieoc;
	u32 voreg;
	u32 vmreg;
	int intr_gpio;
	u8 enable_te: 1;
	u8 enable_eoc_shdn: 1;
};

/**********************************************************
  *
  *   [Extern Function]
  *
  *********************************************************/
/* CON0---------------------------------------------------- */
extern void eta6937_set_tmr_rst(unsigned int val);
extern unsigned int eta6937_get_otg_status(void);
extern void eta6937_set_en_stat(unsigned int val);
extern unsigned int eta6937_get_chip_status(void);
extern unsigned int eta6937_get_boost_status(void);
extern unsigned int eta6937_get_fault_status(void);
/* CON1---------------------------------------------------- */
extern void eta6937_set_input_charging_current(unsigned int val);
extern void eta6937_set_v_low(unsigned int val);
extern void eta6937_set_te(unsigned int val);
extern void eta6937_set_ce(unsigned int val);
extern void eta6937_set_hz_mode(unsigned int val);
extern void eta6937_set_opa_mode(unsigned int val);
/* CON2---------------------------------------------------- */
extern void eta6937_set_oreg(unsigned int val);
extern void eta6937_set_otg_pl(unsigned int val);
extern void eta6937_set_otg_en(unsigned int val);
/* CON3---------------------------------------------------- */
extern unsigned int eta6937_get_vender_code(void);
extern unsigned int eta6937_get_pn(void);
extern unsigned int eta6937_get_revision(void);
/* CON4---------------------------------------------------- */
extern void eta6937_set_reset(unsigned int val);
extern void eta6937_set_icharge(unsigned int val);
extern void eta6937_set_icharge_offset(unsigned int val);
extern void eta6937_set_iterm(unsigned int val);
/* CON5---------------------------------------------------- */
extern void eta6937_set_icharge4(unsigned int val);
extern void eta6937_set_icharge3(unsigned int val);
extern void eta6937_set_low_charge(unsigned int val);
extern void eta6937_get_dpm_status(unsigned int val);
extern unsigned int eta6937_get_cd_status(void);
extern void eta6937_set_vindpm(unsigned int val);
/* CON6---------------------------------------------------- */
extern void eta6937_set_imchrg(unsigned int val);
extern void eta6937_set_vmreg(unsigned int val);
/* --------------------------------------------------------- */
/* CON7----------------------------------------------------*/
extern void eta6937_set_vindpm2(unsigned int val);
extern void eta6937_set_en_ilim2(unsigned int val);
extern void eta6937_set_iin_limit2(unsigned int val);
/* --------------------------------------------------------- */
extern void eta6937_dump_register(void);
extern unsigned int eta6937_reg_config_interface(unsigned char RegNum, unsigned char val);

extern unsigned int eta6937_read_interface(unsigned char RegNum, unsigned char *val,
        unsigned char MASK, unsigned char SHIFT);
extern int eta6937_config_interface(unsigned char RegNum, unsigned char val,
                                    unsigned char MASK, unsigned char SHIFT);
#endif              /* _eta6937_SW_H_ */
