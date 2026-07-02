/* SGM7220 USB Type-C Configuration Channel Logic and Port Control */
/* Copyright (c) 2009-2019, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "tcpc_sgm7220.h"
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <uapi/linux/sched/types.h> // struct sched_param
#include <linux/sched/types.h> // sched_setscheduler
#endif

#define I2C_RETRIES            2
#define I2C_RETRY_DELAY        5     /* ms */
#define SGM7220_IRQ_WAKE_TIME  500   /* ms */

#if defined(UNUSED_CODE)
static int sgm7220_read_regs(struct i2c_client *client, uint8_t reg, uint8_t *readbuf, int len)
{
	struct sgm7220_chip *chip = i2c_get_clientdata(client);
	int err = 0;
	int tries = 0;
	int error = 0;

	struct i2c_msg msgs[] = {
		{
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.flags = I2C_M_RD,
			.len = len,
			.buf = readbuf,
		},
	};
	msgs[0].addr = client->addr;
	msgs[1].addr = client->addr;

	__pm_stay_awake(chip->i2c_wake_lock);
	down(&chip->suspend_lock);

	do {
		err = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (err != ARRAY_SIZE(msgs))
			msleep_interruptible(I2C_RETRY_DELAY);
	} while((err != ARRAY_SIZE(msgs)) && (++tries < I2C_RETRIES));

	if (err != ARRAY_SIZE(msgs)) {
		pr_err("%s: i2c_read error, %d\n", __func__, err);
		error = -1;
	}

	up(&chip->suspend_lock);
	__pm_relax(chip->i2c_wake_lock);

	return error;
}

static int sgm7220_write_regs(struct i2c_client *client, uint8_t reg, uint8_t *writebuf, int len)
{
	uint8_t *b = kzalloc(len+1, GFP_KERNEL);
	int ret = 0;
	struct sgm7220_chip *chip = i2c_get_clientdata(client);

	__pm_stay_awake(chip->i2c_wake_lock);
	down(&chip->suspend_lock);

	b[0] = reg;
	memcpy(&b[1], writebuf, len);
	if (len > 0) {
		struct i2c_msg msgs[] = {
			{
				.addr = client->addr,
				.flags = 0,
				.len = len + 1,
				.buf = b,
			},
		};
		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0)
			pr_err("%s: i2c_write error, ret=%d\n", __func__, ret);
	}

	up(&chip->suspend_lock);
	__pm_relax(chip->i2c_wake_lock);
	kfree(b);
	return ret;
}
#endif

static int sgm7220_read_byte_data(struct i2c_client *client, uint8_t reg, uint8_t *readbuf)
{
	struct sgm7220_chip *chip = i2c_get_clientdata(client);
	int ret = 0;

#if 1
	__pm_stay_awake(chip->i2c_wake_lock);
	down(&chip->suspend_lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	up(&chip->suspend_lock);
	__pm_relax(chip->i2c_wake_lock);

	if (ret < 0) {
		pr_err("%s: (0x%02x) error, ret(%d)\n", __func__, reg, ret);
		return ret;
	}
	ret &= 0xff;
	*readbuf = ret;
#else
	sgm7220_read_regs(client, reg, readbuf, 1);
#endif

	return 0;
}

static int sgm7220_write_byte_data(struct i2c_client *client, uint8_t reg, uint8_t value)
{
	struct sgm7220_chip *chip = i2c_get_clientdata(client);
	int ret = 0;

#if 1
	__pm_stay_awake(chip->i2c_wake_lock);
	down(&chip->suspend_lock);
	ret = i2c_smbus_write_byte_data(client, reg, value);
	up(&chip->suspend_lock);
	__pm_relax(chip->i2c_wake_lock);
#else
	uint8_t buf = 0;
	buf = value;
	ret = sgm7220_write_regs(client, reg, &buf, 1);
#endif

	if (ret < 0)
		pr_err("%s: (0x%02x) error, ret(%d)\n", __func__, reg, ret);

	return ret;
}

static int sgm7220_read_config(struct i2c_client *client, uint8_t reg, uint8_t mask, uint8_t shift, uint8_t *bitval)
{
//	struct sgm7220_chip *chip = i2c_get_clientdata(client);
	int ret = 0;
	uint8_t regval;

	ret = sgm7220_read_byte_data(client, reg, &regval);
	if (ret < 0)
		pr_err("%s: (0x%02x) error, ret(%d)\n", __func__, reg, ret);

	regval &= (mask << shift);
	*bitval = (regval >> shift);

	return ret;
}

static int sgm7220_update_config(struct i2c_client *client, uint8_t reg, uint8_t mask, uint8_t shift, uint8_t bitval)
{
//	struct sgm7220_chip *chip = i2c_get_clientdata(client);
	int ret = 0;
	uint8_t regval;

	sgm7220_read_byte_data(client, reg, &regval);

	regval &= ~(mask << shift);
	regval |= (bitval << shift);

	ret = sgm7220_write_byte_data(client, reg, regval);
	if (ret < 0)
		pr_err("%s: (0x%02x) error, ret(%d)\n", __func__, reg, ret);

	return ret;
}

/* Detect non-DFP -> DFP changes that happen more than 3 times within 10s */
static void sgm7220_state_disorder_detect(struct sgm7220_chip *chip)
{
	unsigned long timeout;

	/* count the (non-DFP -> DFP) changes */
	if ((chip->monitor.former_state != chip->type_c_param.attached_state)
		&& (chip->type_c_param.attached_state == ATTACHED_DFP)) {
		if (!chip->monitor.count) {
			chip->monitor.time_before = jiffies;
		}
		chip->monitor.count++;
	}

	/* store the state */
	chip->monitor.former_state = chip->type_c_param.attached_state;

	if (chip->monitor.count > 3) {
		timeout = msecs_to_jiffies(10 * 1000);  /* 10 seconds */
		if (time_before(jiffies, chip->monitor.time_before + timeout)) {
			chip->monitor.err_detected = 1;
			/* disable id irq before qpnp react to cc chip's id output */
			// interfere_id_irq_from_usb(0);
		}
		chip->monitor.count = 0;
	}

	if ((chip->type_c_param.attached_state == NO_ATTACHED)
		&& chip->monitor.err_detected) {
		/* enable id irq */
		// interfere_id_irq_from_usb(1);
		chip->monitor.err_detected = 0;
	}
}

static int sgm7220_process_interrupt_register(struct sgm7220_chip *chip)
{
	struct tcpc_device *tcpc = chip->tcpc;
	uint8_t bitval = 0;
	int ret = 0;

	/* check attach state */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR, ATTACHED_STATE_MASK,
		ATTACHED_STATE_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read attach_state fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.attached_state = bitval;

	/* when as DFP, check whether cable is active */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR, ACTIVE_CABLE_MASK,
		ACTIVE_CABLE_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read active_cable fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.active_cable_attach = bitval;

	/* when as UFP, check the current detection */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR, CURRENT_MODE_DETECT_MASK,
		CURRENT_MODE_DETECT_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read current_detection fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.current_detect = bitval;

	/* when connect accessory, check the type of accessory */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR, ACCESSORY_CONNECTED_MASK,
		ACCESSORY_CONNECTED_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read accessory_connected fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.accessory_connected = bitval;

	/* in case configured as DRP may detect some non-standard SDP */
	/* chargers as UFP, which may lead to a cyclic switching of DFP */
	/* and UFP on state dedtection result. */
	sgm7220_state_disorder_detect(chip);

	/* check cable dir */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR, CABLE_DIR_MASK,
		CABLE_DIR_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}
	chip->type_c_param.cable_dir = bitval;
	tcpc->typec_polarity = chip->type_c_param.cable_dir;
	pr_info("%s: type=%d, cable_dir(%d)\n", __func__, chip->type_c_param.attached_state, tcpc->typec_polarity);
	pr_info("%s: attach_new=%d, attach_old=%d\n", __func__, tcpc->typec_attach_new, tcpc->typec_attach_old);

	switch (chip->type_c_param.attached_state) {
		case NO_ATTACHED:
			tcpc->typec_attach_new = TYPEC_UNATTACHED;
			if (tcpc->typec_attach_old == TYPEC_ATTACHED_SRC) {
				tcpci_source_vbus(tcpc, TCP_VBUS_CTRL_TYPEC, TCPC_VBUS_SOURCE_0V, 0);
			}
			tcpci_notify_typec_state(tcpc);
			tcpc->typec_attach_old = TYPEC_UNATTACHED;

			ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR, MODE_SELECT_MASK,
				MODE_SELECT_SHIFT, MODE_DRP);
			if (ret < 0) {
				pr_err("%s: force DRP mode fail!\n", __func__);
			}
			break;

		case ATTACHED_DFP:
			if (tcpc->typec_attach_new != TYPEC_ATTACHED_SRC) {
				tcpc->typec_attach_new = TYPEC_ATTACHED_SRC;
				if (chip->type_c_param.active_cable_attach == CABLE_ATTACHED) {
					tcpc->typec_local_rp_level = TYPEC_CC_RP_3_0;
					chip->tcpc_desc->rp_lvl = TYPEC_CC_RP_3_0;
					tcpci_source_vbus(tcpc, TCP_VBUS_CTRL_TYPEC, TCPC_VBUS_SOURCE_5V, -1);
				} else {
					tcpc->typec_local_rp_level = TYPEC_CC_RP_DFT;
					chip->tcpc_desc->rp_lvl = TYPEC_CC_RP_DFT;
					tcpci_source_vbus(tcpc, TCP_VBUS_CTRL_TYPEC, TCPC_VBUS_SOURCE_5V, -1);
				}
				tcpci_notify_typec_state(tcpc);
				tcpc->typec_attach_old = TYPEC_ATTACHED_SRC;
			}
			break;

		case ATTACHED_UFP:
			if (tcpc->typec_attach_new != TYPEC_ATTACHED_SNK) {
				tcpc->typec_attach_new = TYPEC_ATTACHED_SNK;
				if (chip->type_c_param.current_detect == DET_CUR_DEFAULT)
					tcpc->typec_remote_rp_level = TYPEC_CC_VOLT_SNK_DFT;
				else if (chip->type_c_param.current_detect == DET_CUR_1P5)
					tcpc->typec_remote_rp_level = TYPEC_CC_VOLT_SNK_1_5;
				else if (chip->type_c_param.current_detect == DET_CUR_3A)
					tcpc->typec_remote_rp_level = TYPEC_CC_VOLT_SNK_3_0;
				else
					tcpc->typec_remote_rp_level = TYPEC_CC_VOLT_SNK_DFT;
				// tcpci_sink_vbus(tcpc, TCP_VBUS_CTRL_TYPEC, TCPC_VBUS_SINK_5V, -1);
				tcpci_notify_typec_state(tcpc);
				tcpc->typec_attach_old = TYPEC_ATTACHED_SNK;
			}
			break;

		case ATTACHED_TO_ACCESSORY:
			if (tcpc->typec_attach_new != TYPEC_ATTACHED_AUDIO &&
				(chip->type_c_param.accessory_connected == AUDIO_CHARGED_THRU_ACCESSORY ||
				chip->type_c_param.accessory_connected == AUDIO_ACCESSOYR)) {
				if (chip->type_c_param.accessory_connected == AUDIO_CHARGED_THRU_ACCESSORY &&
					chip->type_c_param.current_detect == DET_CUR_ACCESSORY) {
					/* this moment, sink device could pull a maximum current of 500mA  */
					// tcpci_sink_vbus(tcpc, TCP_VBUS_CTRL_TYPEC, TCPC_VBUS_SINK_5V, 500);
				}
				tcpc->typec_attach_new = TYPEC_ATTACHED_AUDIO;
				tcpci_notify_typec_state(tcpc);
				tcpc->typec_attach_old = TYPEC_ATTACHED_AUDIO;
			} else if (tcpc->typec_attach_new != TYPEC_ATTACHED_DEBUG &&
					chip->type_c_param.accessory_connected == DEBUG_ACCESSORY) {
				tcpc->typec_attach_new = TYPEC_ATTACHED_DEBUG;
				tcpci_notify_typec_state(tcpc);
				tcpc->typec_attach_old = TYPEC_ATTACHED_DEBUG;
			}
			break;

		default:
			pr_err("%s: Unknown type[0x%02x]\n", __func__, chip->type_c_param.attached_state);
			break;
	}
	return 0;
}

static void sgm7220_irq_work_handler(struct kthread_work *work)
{
	struct sgm7220_chip *chip = container_of(work, struct sgm7220_chip, irq_work);
	uint8_t bitval = 0;
	int ret = 0;

	tcpci_lock_typec(chip->tcpc);

	pr_info("%s enter\n", __func__);

	/* Reset INT port */
	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR, INTERRUPT_STATUS_MASK,
		INTERRUPT_STATUS_SHIFT, &bitval);
	if (ret < 0)
		pr_err("%s read INT bit fail!\n", __func__);
	pr_info("%s Bit_INT = : 0x%x\n", __func__, bitval);

	sgm7220_process_interrupt_register(chip);

	ret = sgm7220_update_config(chip->client, SGM7220_Reg09_ADDR, INTERRUPT_STATUS_MASK,
		INTERRUPT_STATUS_SHIFT, 0x01);
	if (ret < 0)
		pr_err("%s: Reset INT bit fail!\n", __func__);

	tcpci_unlock_typec(chip->tcpc);
}

static irqreturn_t sgm7220_irq_handler(int irq, void *handle)
{
	struct sgm7220_chip *chip = (struct sgm7220_chip *)handle;

	__pm_wakeup_event(chip->irq_wake_lock, SGM7220_IRQ_WAKE_TIME);

	kthread_queue_work(&chip->irq_worker, &chip->irq_work);
	return IRQ_HANDLED;
}

static int sgm7220_init_alert(struct tcpc_device *tcpc)
{
	struct sgm7220_chip *chip = tcpc_get_dev_data(tcpc);
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
	int ret = 0;
	char *name;
	int len;

	len = strlen(chip->tcpc_desc->name);
	name = devm_kzalloc(chip->dev, len+5, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	snprintf(name, PAGE_SIZE, "%s-IRQ", chip->tcpc_desc->name);
	if (ret < 0 || ret >= PAGE_SIZE)
		pr_info("%s-%d, snprintf fail\n", __func__, __LINE__);

	pr_info("%s name = %s, gpio = %d\n", __func__, chip->tcpc_desc->name, chip->irq_gpio);

	if (!gpio_is_valid(chip->irq_gpio)) {
		pr_err("%s: irq gpio is invalid\n", __func__);
		return ret;
	}

	ret = devm_gpio_request(chip->dev, chip->irq_gpio, name);
	if (ret < 0) {
		pr_err("%s: irq gpio%d request failed (ret = %d)\n", __func__, chip->irq_gpio, ret);
		return ret;
	}

	ret = gpio_direction_input(chip->irq_gpio);
	if (ret < 0) {
		pr_err("%s: failes to set GPIO%d as input pin (ret = %d\n)", __func__, chip->irq_gpio, ret);
		goto err_irq_gpio_dir;
	}

	chip->irqnum = gpio_to_irq(chip->irq_gpio);
	if (chip->irqnum <= 0) {
		pr_err("%s gpio to irq fail, chip->irqnum = %d\n", __func__, chip->irqnum);
	}

	pr_info("%s : IRQ number = %d\n", __func__, chip->irqnum);

	kthread_init_worker(&chip->irq_worker);
	chip->irq_worker_task = kthread_run(kthread_worker_fn,
		&chip->irq_worker, "%s", chip->tcpc_desc->name);
	if (IS_ERR(chip->irq_worker_task))
		pr_err("%s: Could not create tcpc task\n", __func__);

	sched_setscheduler(chip->irq_worker_task, SCHED_FIFO, &param);
	kthread_init_work(&chip->irq_work, sgm7220_irq_work_handler);
	ret = request_irq(chip->irqnum, sgm7220_irq_handler, IRQF_TRIGGER_FALLING |
		IRQF_NO_THREAD |
		IRQF_NO_SUSPEND, name, chip);
	if (ret < 0)
		pr_err("%s: error failed to request IRQ (ret = %d)\n", __func__, chip->irqnum);

	enable_irq_wake(chip->irqnum);
	return 0;

err_irq_gpio_dir:
	if (gpio_is_valid(chip->irq_gpio))
		gpio_free(chip->irq_gpio);
	return ret;
}

static int sgm7220_config_initialization(struct sgm7220_chip *chip)
{
	int ret = 0;
	/* do initialization here, before enable irq,
	 * clear irq,
	 * config DRP/UFP/DFP mode,
	 * and etc..
	 */
	pr_info("%s enter \n", __func__);

	ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR, MODE_SELECT_MASK,
		MODE_SELECT_SHIFT,
		chip->type_c_param.mode_select);
	if (ret < 0) {
		pr_err("%s: init mode_select fail!\n", __func__);
		return ret;
	}

	ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR, SOURCE_PREF_MASK,
		SOURCE_PREF_SHIFT,
		chip->type_c_param.sourcec_pref);
	if (ret < 0) {
		pr_err("%s: init sourcec_pref fail!\n", __func__);
		return ret;
	}

	ret = sgm7220_update_config(chip->client, SGM7220_Reg08_ADDR, CURRENT_MODE_ADVERTISE_MASK,
		CURRENT_MODE_ADVERTISE_SHIFT,
		chip->type_c_param.current_advertise);
	if (ret < 0) {
		pr_err("%s: init current_advertise fail!\n", __func__);
		return ret;
	}

	sgm7220_read_byte_data(chip->client, SGM7220_Reg08_ADDR, &chip->Registers.Reg08.byte);
	sgm7220_read_byte_data(chip->client, SGM7220_Reg09_ADDR, &chip->Registers.Reg09.byte);
	sgm7220_read_byte_data(chip->client, SGM7220_Reg0A_ADDR, &chip->Registers.Reg0A.byte);
	sgm7220_read_byte_data(chip->client, SGM7220_Reg45_ADDR, &chip->Registers.Reg45.byte);
	pr_err("%s: sgm7220 config initatial finished! reg08:0x%02x, reg09:0x%02x, reg0A:0x%02x, reg45:0x%02x\n",
		__func__,
		chip->Registers.Reg08.byte,
		chip->Registers.Reg09.byte,
		chip->Registers.Reg0A.byte,
		chip->Registers.Reg45.byte);

	return 0;
}

/*================================ Encapsulate interface functions ===================================*/
static int sgm7220_tcpc_init(struct tcpc_device *tcpc, bool sw_reset)
{
	struct sgm7220_chip *chip = tcpc_get_dev_data(tcpc);
	int ret = 0;

	pr_info("%s enter \n", __func__);

	if (sw_reset) {
		ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR, I2C_SOFT_RESET_MASK,
			I2C_SOFT_RESET_SHIFT, 1);
		if (ret < 0)
			pr_err("%s: I2C_SOFT_RESET fail! (ret = %d)\n", __func__, ret);
	}

	switch (chip->tcpc_desc->role_def) {
		case TYPEC_ROLE_SNK:
			chip->type_c_param.mode_select = MODE_UFP;
			break;
		case TYPEC_ROLE_SRC:
			chip->type_c_param.mode_select = MODE_DFP;
			break;
		case TYPEC_ROLE_DRP:
			chip->type_c_param.mode_select = MODE_DFP;
			chip->type_c_param.sourcec_pref = STANDARD_DRP;
			break;
		case TYPEC_ROLE_TRY_SNK:
			chip->type_c_param.mode_select = MODE_DFP;
			chip->type_c_param.sourcec_pref = DRP_TRY_SINK;
			break;
		default:
			break;
	}

	switch (chip->tcpc_desc->rp_lvl) {
		case TYPEC_CC_RP_DFT:
			chip->type_c_param.current_advertise = ADV_CUR_DEFAULT;
			break;
		case TYPEC_CC_RP_1_5:
			chip->type_c_param.current_advertise = ADV_CUR_1P5;
			break;
		case TYPEC_CC_RP_3_0:
			chip->type_c_param.current_advertise = ADV_CUR_3A;
			break;
		default:
			break;
	}

	ret = sgm7220_config_initialization(chip);
	if (ret < 0) {
		pr_err("%s: fails to do initialization (ret = %d)\n", __func__, ret);
	}

	/* reset the INT port */
	// tcpci_alert_status_clear(tcpc, 0xffffffff);  /* the second param could be filled in with any value */

	return 0;
}

static int sgm7220_alert_status_clear(struct tcpc_device *tcpc, uint32_t mask)
{
	// struct sgm7220_chip *chip = tcpc_get_dev_data(tcpc);
	// int ret = 0;

	pr_info("%s enter \n", __func__);

	// ret = sgm7220_update_config(chip->client, SGM7220_Reg09_ADDR, INTERRUPT_STATUS_MASK,
	// 	INTERRUPT_STATUS_SHIFT, 0x01);
	// if (ret < 0)
	// 	pr_err("%s: update reg fail!\n", __func__);

	return 0;
}

static int sgm7220_fault_status_clear(struct tcpc_device *tcpc, uint8_t status)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_get_alert_mask(struct tcpc_device *tcpc, uint32_t *mask)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgam7220_get_alert_status(struct tcpc_device *tcpc, uint32_t *alert)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_get_power_status(struct tcpc_device *tcpc, uint16_t *pwr_status)
{
	pr_info("%s enter \n", __func__);
	*pwr_status = 0;
	return 0;
}

static int sgm7220_get_fault_status(struct tcpc_device *tcpc, uint8_t *status)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_get_cc(struct tcpc_device *tcpc, int *cc1, int *cc2)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_set_cc(struct tcpc_device *tcpc, int pull)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_set_polarity(struct tcpc_device *tcpc, int polarity)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_set_low_rp_duty(struct tcpc_device *tcpc, bool low_rp)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_set_vconn(struct tcpc_device *tcpc, int enable)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_tcpc_deinit(struct tcpc_device *tcpc)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static int sgm7220_set_watchdog(struct tcpc_device *tcpc, bool en)
{
	pr_info("%s enter \n", __func__);
	return 0;
}

static struct tcpc_ops sgm7220_tcpc_ops = {
	.init = sgm7220_tcpc_init,
	//int (*init_alert_mask)(struct tcpc_device *tcpc),
	.alert_status_clear = sgm7220_alert_status_clear,
	.fault_status_clear = sgm7220_fault_status_clear,
	//int (*set_alert_mask)(struct tcpc_device *tcpc, uint32_t mask),
	.get_alert_mask = sgm7220_get_alert_mask,
	.get_alert_status = sgam7220_get_alert_status,
	.get_power_status = sgm7220_get_power_status,
	.get_fault_status = sgm7220_get_fault_status,
	.get_cc = sgm7220_get_cc,
	.set_cc = sgm7220_set_cc,
	.set_polarity = sgm7220_set_polarity,
	.set_low_rp_duty = sgm7220_set_low_rp_duty,
	.set_vconn = sgm7220_set_vconn,
	.deinit = sgm7220_tcpc_deinit,
	//int (*alert_vendor_defined_handler)(struct tcpc_device *tcpc),

	.set_watchdog = sgm7220_set_watchdog,
};

static int sgm7220_parse_dt(struct sgm7220_chip *chip, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret = 0;

	if (!np)
		return -EINVAL;

	pr_info("%s\n", __func__);

#if (!defined(CONFIG_MTK_GPIO) || defined(CONFIG_MTK_GPIOLIB_STAND))
	ret = of_get_named_gpio(np, "sgm7220_irq_gpio", 0);
	if (ret < 0) {
		pr_err("%s: error invalid irq gpio err (ret=%d)\n", __func__, ret);
		return ret;
	}
	chip->irq_gpio = ret;
#else
	ret = of_property_read_u32(np, "sgm7220_irq_gpio_num", &chip->irq_gpio);
	if (ret < 0) {
		pr_err("%s: error invalid irq gpio err: %d\n", __func__, ret);
		return ret;
	}
#endif
	return ret;
}

static int sgm7220_tcpcdev_init(struct sgm7220_chip *chip, struct device *dev)
{
	struct tcpc_desc *desc;
	struct device_node *np = dev->of_node;
	uint32_t val, len;

	const char *name = "default";
	if (!np)
		return -EINVAL;

	desc = devm_kzalloc(dev, sizeof(struct tcpc_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	/* Set initial values in the device tree */
	/* check the CC Role */
	if (of_property_read_u32(np, "sgm7220-tcpc,role_def", &val) >= 0) {
		if (val >= TYPEC_ROLE_NR)
			desc->role_def = TYPEC_ROLE_DRP;
		else
			desc->role_def = val;
	} else {
		dev_info(dev, "use default Role DRP\n");
		desc->role_def = TYPEC_ROLE_DRP;
	}

	/* check the num of notifier */
	if (of_property_read_u32(np, "sgm7220-tcpc,notifier_supply_num", &val) >= 0) {
		if (val < 0)
			desc->notifier_supply_num = 0;
		else
			desc->notifier_supply_num = val;
	} else {
		desc->notifier_supply_num = val;
	}

	/* check the Rp level */
	if (of_property_read_u32(np, "sgm7220-tcpc,rp_level", &val) >= 0) {
		switch (val) {
		case 0:    /* RP Default */
			desc->rp_lvl = TYPEC_CC_RP_DFT;
			break;
		case 1:    /* RP 1.5A */
			desc->rp_lvl = TYPEC_CC_RP_1_5;
			break;
		case 2:    /* RP 3A */
			desc->rp_lvl = TYPEC_CC_RP_3_0;
			break;
		default:
			break;
		}
		dev_info(dev, "sgm7220_rp_level = %d\n", desc->rp_lvl);
	}
#ifdef CONFIG_TCPC_VCONN_SUPPLY_MODE
	if (of_property_read_u32(np, "sgm7220-tcpc,vconn_supply", &val) >= 0) {
		if (val >= TCPC_VCONN_SUPPLY_NR)
			desc->vconn_supply = TCPC_VCONN_SUPPLY_NEVER;
		else
			desc->vconn_supply = val;
	} else {
		dev_info(dev, "use never Vconn Supply\n");
		desc->vconn_supply = TCPC_VCONN_SUPPLY_NEVER;
	}
#endif  /* CONFIG_TCPC_VCONN_SUPPLY_MODE */

	chip->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (chip->tcpc != NULL) {
		dev_err(dev, "type_c_port0 been registed, exit sgm7220 probe!!!\n");
		return -EINVAL;
	}
	of_property_read_string(np, "sgm7220-tcpc,name", (char const **)&name);

	len = strlen(name);
	desc->name = kzalloc(len + 1, GFP_KERNEL);
	if (!desc->name)
		return -ENOMEM;

	strncpy((char *)desc->name, name, len + 1);

	chip->tcpc_desc = desc;
	chip->tcpc = tcpc_device_register(dev, desc, &sgm7220_tcpc_ops, chip);
	if (IS_ERR(chip->tcpc))
		return -EINVAL;

	chip->tcpc->typec_attach_old = TYPEC_UNATTACHED;
	chip->tcpc->typec_attach_new = TYPEC_UNATTACHED;
	tcpci_report_usb_port_changed(chip->tcpc);

	pr_info("%s end\n", __func__);
	return 0;
}
/*================================ Encapsulate interface functions (end)=================================*/
/*=======================================================================================================*/
/*===================================== create device attribution =======================================*/
static ssize_t current_advertise_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR, CURRENT_MODE_ADVERTISE_MASK,
		CURRENT_MODE_ADVERTISE_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (ADV_CUR_DEFAULT == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Default (500mA/900mA) initial value at startup");
	else if (ADV_CUR_1P5 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Medium (1.5A)");
	else if (ADV_CUR_3A == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "High (3A)");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "unknow");
}

static ssize_t current_advertise_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint bitval = 0;
	int ret = 0;

	if (kstrtouint(buf, 0, &bitval))
		return -EINVAL;

	ret = sgm7220_update_config(chip->client, SGM7220_Reg08_ADDR, CURRENT_MODE_ADVERTISE_MASK,
		CURRENT_MODE_ADVERTISE_SHIFT, (uint8_t)bitval);
	if (ret < 0) {
		pr_err("%s: update reg fail!\n", __func__);
	}
	return count;
}

static ssize_t current_detect_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR, CURRENT_MODE_DETECT_MASK,
		CURRENT_MODE_DETECT_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (DET_CUR_DEFAULT == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "default current, 500 or 900mA");
	else if (DET_CUR_1P5 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "middle current, 1.5A");
	else if (DET_CUR_3A == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "high current, 3A");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "unknow");
}

static ssize_t accessory_connected_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR, ACCESSORY_CONNECTED_MASK,
		ACCESSORY_CONNECTED_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (NO_ACCESSORY_ATTACHED == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "No accessory attached");
	else if (AUDIO_ACCESSOYR == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Audio accessory");
	else if (AUDIO_CHARGED_THRU_ACCESSORY == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Audio charged thru accessory");
	else if (DEBUG_ACCESSORY == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Debug accessory");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "unknow");
}

static ssize_t active_cable_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg08_ADDR, ACTIVE_CABLE_MASK,
		ACTIVE_CABLE_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (CABLE_NOT_ATTACHED == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Not cable attached");
	else if (CABLE_ATTACHED == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Active cable attached");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "unknow");
}

static ssize_t attached_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR, ATTACHED_STATE_MASK,
		ATTACHED_STATE_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (NO_ATTACHED == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Not attached (default)");
	else if (ATTACHED_DFP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Attached.SRC (DFP)");
	else if (ATTACHED_UFP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Attached.SNK (UFP)");
	else if (ATTACHED_TO_ACCESSORY == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Attached to an accessory");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "unknow");
}

static ssize_t cable_dir_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR, CABLE_DIR_MASK,
		CABLE_DIR_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (ORIENT_CC1 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "ORIENT_CC1");
	else if (ORIENT_CC2 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "ORIENT_CC2");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "unknow");
}

static ssize_t drp_duty_cycle_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg09_ADDR, DRP_DUTY_CYCLE_MASK,
		DRP_DUTY_CYCLE_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (CYCLE_30 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "DRP_CYCLE_30");
	else if (CYCLE_40 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "DRP_CYCLE_40");
	else if (CYCLE_50 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "DRP_CYCLE_50");
	else if (CYCLE_60 == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "DRP_CYCLE_60");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "unknow");
}

static ssize_t drp_duty_cycle_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint bitval = 0;
	int ret = 0;

	if (kstrtouint(buf, 0, &bitval))
		return -EINVAL;

	ret = sgm7220_update_config(chip->client, SGM7220_Reg09_ADDR, DRP_DUTY_CYCLE_MASK,
		DRP_DUTY_CYCLE_SHIFT, (uint8_t)bitval);
	if (ret < 0) {
		pr_err("%s: update reg fail!\n", __func__);
	}
	return count;
}

static ssize_t mode_select_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint8_t bitval = 0;
	int ret = 0;

	ret = sgm7220_read_config(chip->client, SGM7220_Reg0A_ADDR, MODE_SELECT_MASK,
		MODE_SELECT_SHIFT, &bitval);
	if (ret < 0) {
		pr_err("%s: read reg fail!\n", __func__);
		return ret;
	}

	if (ACCORDING_TO_PORT == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "Maintain mode according to PORT pin selection (default)");
	else if (MODE_UFP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "UFP mode (unattached.SNK)");
	else if (MODE_DFP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "DFP mode (unattached.SRC)");
	else if (MODE_DRP == bitval)
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "DRP mode (start from unattached.SNK)");
	else
		return snprintf(buf, PAGE_SIZE, "0x%02x - %s\n", bitval, "unknow");
}

static ssize_t mode_select_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct sgm7220_chip *chip = dev_get_drvdata(dev);
	uint bitval = 0;
	int ret = 0;

	if (kstrtouint(buf, 0, &bitval))
		return -EINVAL;

	ret = sgm7220_update_config(chip->client, SGM7220_Reg0A_ADDR, MODE_SELECT_MASK,
		MODE_SELECT_SHIFT, (uint8_t)bitval);
	if (ret < 0) {
		pr_err("%s: update reg fail!\n", __func__);
	}
	return count;
}

static DEVICE_ATTR(current_advertise, S_IRUGO | S_IWUSR, current_advertise_show, current_advertise_store);
static DEVICE_ATTR(current_detect, S_IRUGO, current_detect_show, NULL);
static DEVICE_ATTR(accessory_connected, S_IRUGO, accessory_connected_show, NULL);
static DEVICE_ATTR(active_cable_det, S_IRUGO, active_cable_det_show, NULL);
static DEVICE_ATTR(attached_state, S_IRUGO, attached_state_show, NULL);
static DEVICE_ATTR(cable_direction, S_IRUGO, cable_dir_show, NULL);
static DEVICE_ATTR(drp_duty_cycle, S_IRUGO | S_IWUSR, drp_duty_cycle_show, drp_duty_cycle_store);
static DEVICE_ATTR(mode_select, S_IRUGO | S_IWUSR, mode_select_show, mode_select_store);

static struct device_attribute *sgm7220_attributes[] = {
	&dev_attr_current_advertise,
	&dev_attr_current_detect,
	&dev_attr_accessory_connected,
	&dev_attr_active_cable_det,
	&dev_attr_attached_state,
	&dev_attr_cable_direction,
	&dev_attr_drp_duty_cycle,
	&dev_attr_mode_select,
	NULL
};

static int sgm7220_create_device(struct sgm7220_chip *chip)
{
	struct device_attribute **attrs = sgm7220_attributes;
	struct device_attribute *attr = kzalloc(16, GFP_KERNEL);
	int err;

	pr_debug("%s:\n", __func__);
	chip->device_class = class_create(THIS_MODULE, "typec_device");
	if (IS_ERR(chip->device_class))
		return PTR_ERR(chip->device_class);

	chip->dev = device_create(chip->device_class, NULL, 0, NULL, "cc_logic_sgm7220");
	if (IS_ERR(chip->dev))
		return PTR_ERR(chip->dev);

	dev_set_drvdata(chip->dev, chip);

	while ((attr = *attrs++) != NULL) {
		err = device_create_file(chip->dev, attr);
		if (err) {
			device_destroy(chip->device_class, 0);
			return err;
		}
	}
	kfree(attr);
	return 0;
}

static void sgm7220_destroy_device(struct sgm7220_chip *chip)
{
	struct device_attribute **attrs = sgm7220_attributes;
	struct device_attribute *attr = kzalloc(16, GFP_KERNEL);

	while ((attr = *attrs++) != NULL)
		device_remove_file(chip->dev, attr);

	kfree(attr);
	device_destroy(chip->device_class, 0);
	class_destroy(chip->device_class);
	chip->device_class = NULL;
}
/*====================================== create device attribution (end)=============================================*/

static int sgm7220_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct sgm7220_chip *chip = NULL;
	bool use_dt = client->dev.of_node;

	pr_info("%s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C | I2C_FUNC_SMBUS_BYTE)) {
		pr_err("%s: checkint_functionality failed\n", __func__);
		return -ENODEV;
	}

	chip = devm_kzalloc(&client->dev, sizeof(struct sgm7220_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	/* obtain the IRQ GPIO in the dts */
	if (use_dt)
		sgm7220_parse_dt(chip, &client->dev);
	else {
		dev_err(&client->dev, "no dts node\n");
		return -ENODEV;
	}
	chip->dev = &client->dev;
	chip->client = client;
	sema_init(&chip->suspend_lock, 1);
	i2c_set_clientdata(client, chip);

	chip->irq_wake_lock = wakeup_source_register(chip->dev, "sgm7220_irq_wakelock");
	chip->i2c_wake_lock = wakeup_source_register(chip->dev, "sgm7220_i2c_wakelock");

	ret = sgm7220_tcpcdev_init(chip, &client->dev);
	if (ret < 0) {
		dev_err(&client->dev, "sgm7229 tcpc dev init fail\n");
		goto err_tcpc_reg;
	}

	ret = sgm7220_init_alert(chip->tcpc);
	if (ret < 0) {
		pr_err("%s: fails to initialize sgm7220 alert (ret=%d)\n", __func__, ret);
		goto err_irq_init;
	}

	ret = sgm7220_create_device(chip);
	if (ret) {
		pr_err("%s: create device failed\n", __func__);
		goto err_device_create;
	}

	pr_info( "%s:sgm7220 CC logic probe finished!\n", __func__);
	return 0;

err_device_create:
	sgm7220_destroy_device(chip);
err_irq_init:
	tcpc_device_unregister(chip->dev, chip->tcpc);
err_tcpc_reg:
	wakeup_source_unregister(chip->i2c_wake_lock);
	wakeup_source_unregister(chip->irq_wake_lock);
	return ret;
}

static int sgm7220_remove(struct i2c_client *client)
{
	struct sgm7220_chip *chip = i2c_get_clientdata(client);

	if (chip->irqnum) {
		disable_irq_wake(chip->irqnum);
		free_irq(chip->irqnum, chip);
	}

	if (gpio_is_valid(chip->irq_gpio))
		gpio_free(chip->irq_gpio);

	if (chip) {
		sgm7220_destroy_device(chip);
		tcpc_device_unregister(chip->dev, chip->tcpc);
		wakeup_source_unregister(chip->i2c_wake_lock);
		wakeup_source_unregister(chip->irq_wake_lock);
	}

	dev_info(&client->dev, "sgm7220 CC logic remove finished\n");
	return 0;
}

#ifdef CONFIG_PM
static int SGM7220_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm7220_chip *chip = (struct sgm7220_chip *)i2c_get_clientdata(client);

	pr_err("[sgm7220] %s enter\n", __func__);
	if (!chip) {
		pr_err("[sgm7220] suspend: No device is available!\n");
		return -EINVAL;
	}

	down(&chip->suspend_lock);

	return 0;
}

static int SGM7220_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm7220_chip *chip = (struct sgm7220_chip *)i2c_get_clientdata(client);

	pr_err("[sgm7220] %s enter\n", __func__);
	if (!chip) {
		pr_err("[sgm7220] suspend: No device is available!\n");
		return -EINVAL;
	}

	up(&chip->suspend_lock);

	return 0;
}

/*static const struct dev_pm_ops sgm7220_dev_pm_ops = {
	.suspend = SGM7220_suspend,
	.resume  = SGM7220_resume,
};*/

static void sgm7220_shutdown(struct i2c_client *client)
{
	struct sgm7220_chip *chip = i2c_get_clientdata(client);

	/* reset the INT */
	sgm7220_update_config(chip->client, SGM7220_Reg09_ADDR, INTERRUPT_STATUS_MASK,
		INTERRUPT_STATUS_SHIFT, 0x01);
	if (chip != NULL) {
		if (chip->irqnum)
			disable_irq(chip->irqnum);
	}
}

#ifdef CONFIG_PM_RUNTIME
static int sgm7220_pm_suspend_runtime(struct device *device)
{
	dev_dbg(device, "pm_runtime: suspending...\n");
	return 0;
}

static int sgm7220_pm_resume_runtime(struct device *device)
{
	dev_dbg(device, "pm_runtime: resuming...\n");
	return 0;
}
SET_

#endif /* CONFIG_PM_RUNTIME */

static const struct dev_pm_ops sgm7220_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(SGM7220_suspend,
		SGM7220_resume)
#ifdef CONFIG_PM_RUNTIME
	SET_SYSTEM_SLEEP_PM_OPS(sgm7220_pm_suspend_runtime,
		sgm7220_pm_resume_runtime,
		NULL)
#endif /* CONFIG_PM_RUNTIME */
};
#define SGM7220_PM_OPS   (&sgm7220_pm_ops)
#else
#define SGM7220_PM_OPS   (NULL)
#endif /* CONFIG_PM */

static const struct i2c_device_id sgm7220_id_table[] = {
	{"sgm7220", 0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, sgm7220_id_table);

static const struct of_device_id of_sgm7220_match_table[] = {
	{.compatible = "sgm,usb_typec_sgm7220"},
	{ },
};
MODULE_DEVICE_TABLE(of, of_sgm7220_match_table);

static const unsigned short normal_i2c[] = {0x47, 0x67, I2C_CLIENT_END};
/* i2c driver */
static struct i2c_driver sgm7220_driver = {
	.driver = {
		.name = "usb_typec_sgm7220",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(of_sgm7220_match_table),
		.pm = SGM7220_PM_OPS,
	},
	.probe = sgm7220_probe,
	.remove = sgm7220_remove,
	.shutdown = sgm7220_shutdown,
	.id_table = sgm7220_id_table,
	.address_list = (const unsigned short *) normal_i2c,
};

static int __init sgm7220_init(void)
{
	int ret = 0;
	struct device_node *np;

	pr_info("%s: initializing...\n", __func__);

	np = of_find_node_by_name(NULL, "sgm7220");

	if (np != NULL) {
		pr_info("sgm7220 node has found...\n");
		ret = i2c_add_driver(&sgm7220_driver);
		if (ret != 0)
			pr_err("%s: sgm7220 i2c init failed!\n", __func__);
	} else
		pr_info("sgm7220 node not found...\n");

	return ret;
}

static void __exit sgm7220_exit(void)
{
	i2c_del_driver(&sgm7220_driver);
}


//module_i2c_driver(sgm7220_driver);

subsys_initcall(sgm7220_init);
module_exit(sgm7220_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("SGMICRO,jigao_dai");
MODULE_DESCRIPTION("SGM7220 TCPC Driver");
