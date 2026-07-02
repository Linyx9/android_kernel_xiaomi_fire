#ifndef __TCPC_SGM7220_H__
#define __TCPC_SGM7220_H__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/cpu.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/sched.h>

#include <linux/usb/typec.h>
#include "../../drivers/misc/mediatek/typec/tcpc/inc/tcpci.h"
#include "../../drivers/misc/mediatek/typec/tcpc/inc/pd_dbg_info.h"
#include "../../drivers/misc/mediatek/typec/tcpc/inc/tcpci_typec.h"


/* Private variables----------------------------------------------------------*/
/* SGM7220 Register Map */
#define  SGM7220_DEVICE_ID0_ADDR	0x00
#define  SGM7220_DEVICE_ID1_ADDR	0x01
#define  SGM7220_DEVICE_ID2_ADDR	0x02
#define  SGM7220_DEVICE_ID3_ADDR	0x03
#define  SGM7220_DEVICE_ID4_ADDR	0x04
#define  SGM7220_DEVICE_ID5_ADDR	0x05
#define  SGM7220_DEVICE_ID6_ADDR	0x06
#define  SGM7220_DEVICE_ID7_ADDR	0x07
#define  SGM7220_Reg08_ADDR		0x08
#define  SGM7220_Reg09_ADDR		0x09
#define  SGM7220_Reg0A_ADDR		0x0A
#define  SGM7220_Reg45_ADDR		0x45

/* for Register08 */
#define ACTIVE_CABLE_SHIFT		0
#define ACTIVE_CABLE_MASK		0x01
#define ACCESSORY_CONNECTED_SHIFT	1
#define ACCESSORY_CONNECTED_MASK	0x07
#define CURRENT_MODE_DETECT_SHIFT	4
#define CURRENT_MODE_DETECT_MASK	0x03
#define CURRENT_MODE_ADVERTISE_SHIFT	6
#define CURRENT_MODE_ADVERTISE_MASK	0x03
/* for Register09 */
#define DISABLE_UFP_ACCESSORY		0x01
#define DRP_DUTY_CYCLE_SHIFT		1
#define DRP_DUTY_CYCLE_MASK		0x03
#define INTERRUPT_STATUS_SHIFT		4
#define INTERRUPT_STATUS_MASK		0x01
#define CABLE_DIR_SHIFT			5
#define CABLE_DIR_MASK			0x01
#define ATTACHED_STATE_SHIFT		6
#define ATTACHED_STATE_MASK		0x03
/* for Register0A */
#define DISABLE_TERM_SHIFT		0
#define DISABLE_TERM_MASK		0x01
#define SOURCE_PREF_SHIFT		1
#define SOURCE_PREF_MASK		0x03
#define I2C_SOFT_RESET_SHIFT		3
#define I2C_SOFT_RESET_MASK		0x01
#define MODE_SELECT_SHIFT		4
#define MODE_SELECT_MASK		0x03
#define DEBOUNCE_SHIFT			6
#define DEBOUNCE_MASK			0x03
/* for Register45 */
#define DISABLE_RD_RP_SHIFT		2
#define DISABLE_RD_RP_MASK		0x01

/* constants */
enum current_advertise_type {
	ADV_CUR_DEFAULT = 0,  // default current: 500mA or 900mA
	ADV_CUR_1P5,          // 1.5A
	ADV_CUR_3A,           // 3A
};

enum current_detect_type {
	DET_CUR_DEFAULT = 0,  // default
	DET_CUR_1P5,          // Medium
	DET_CUR_ACCESSORY,    // Charge through accessory - 500mA
	DET_CUR_3A,           // High
};

enum accessory_connected_type {
	NO_ACCESSORY_ATTACHED = 0,
	AUDIO_ACCESSOYR = 4,
	AUDIO_CHARGED_THRU_ACCESSORY = 5,
	DEBUG_ACCESSORY = 6,
};

enum active_cable_attach_type {
	CABLE_NOT_ATTACHED = 0,
	CABLE_ATTACHED
};

enum attached_state_type {
	NO_ATTACHED = 0,
	ATTACHED_DFP,
	ATTACHED_UFP,
	ATTACHED_TO_ACCESSORY
};

enum cable_dir_type {
	ORIENT_CC1 = 0,
	ORIENT_CC2
};

enum drp_duty_cycle_type {
	CYCLE_30 = 0,
	CYCLE_40,
	CYCLE_50,
	CYCLE_60,
};

enum mode_select_type {
	ACCORDING_TO_PORT = 0,
	MODE_UFP,
	MODE_DFP,
	MODE_DRP,
};

enum sourcec_pref_type {
	STANDARD_DRP = 0,
	DRP_TRY_SINK,
};

/* Type-C Attrs */
struct type_c_parameters {
	enum current_advertise_type	current_advertise;
	enum current_detect_type	current_detect;
	enum accessory_connected_type	accessory_connected;
	enum active_cable_attach_type	active_cable_attach;
	enum attached_state_type	attached_state;
	enum cable_dir_type		cable_dir;
	enum drp_duty_cycle_type	drp_duty_cycle;
	enum mode_select_type		mode_select;
	enum sourcec_pref_type		sourcec_pref;
};

struct state_disorder_monitor {
	int				count;
	int				err_detected;
	enum attached_state_type	former_state;
	unsigned long			time_before;
};

/*============================================================================================*/
/* Register unions: You can operate the 'byte' directly or every bit of the related register */
typedef union {                               /* 0x08:  */
	uint8_t	byte;
	struct {
		uint8_t	Active_Cable_Detection    :1;
		uint8_t	Accessory_Connect         :3;
		uint8_t	Current_Mode_Detect       :2;
		uint8_t	Current_Mode_Advertise    :2;
	} Bits_Reg08;
} Reg08_t;

typedef union {                               /* 0x09 */
	uint8_t	byte;
	struct {
		uint8_t	Disable_UFP_Accessory     :1;
		uint8_t	DRP_Duty_Cycle            :2;
		uint8_t	NU                        :1;
		uint8_t	Interrupt_Status          :1;
		uint8_t	Cable_Dir                 :1;
		uint8_t	Attached_State            :2;
	} Bits_Reg09;
} Reg09_t;

typedef union {                               /* 0x0A */
	uint8_t	byte;
	struct {
		uint8_t	Disable_Termination       :1;
		uint8_t	Source_Perform            :2;
		uint8_t	IIC_Soft_Reset            :1;
		uint8_t	Mode_Select               :2;
		uint8_t	CC_Debounce               :2;
	} Bits_Reg0A;
} Reg0A_t;

typedef union {                              /* 0x45 */
	uint8_t	byte;
	struct {
		uint8_t	Reserved                  :2;
		uint8_t	Disable_RD_RP             :1;
		uint8_t	NU                        :5;
	} Bits_Reg45;
} Reg45_t;

typedef struct {
	Reg08_t	Reg08;
	Reg09_t	Reg09;
	Reg0A_t	Reg0A;
	Reg45_t	Reg45;
} Reg_Value_t;
/*============================================================================================*/

struct sgm7220_chip {
	struct i2c_client		*client;
	struct device			*dev;
	struct tcpc_desc		*tcpc_desc;
	struct tcpc_device		*tcpc;
	struct kthread_worker		irq_worker;
	struct kthread_work		irq_work;
	struct task_struct		*irq_worker_task;
	struct wakeup_source		*irq_wake_lock;
	struct wakeup_source		*i2c_wake_lock;
	struct semaphore		suspend_lock;
	int				irq_gpio;
	int				irqnum;
	Reg_Value_t			Registers;
	struct type_c_parameters	type_c_param;
	struct state_disorder_monitor	monitor;
	struct class			*device_class;
};

/* extern variables-----------------------------------------------------------*/

/* extern function prototypes-------------------------------------------------*/

#endif
/********************************************************
  End Of File
********************************************************/
