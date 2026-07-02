// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 * MAIN AF voice coil motor driver
 *
 *
 */

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/sched.h>
#include <linux/kthread.h>
/* kernel standard */
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>

/* OIS/EIS Timer & Workqueue */
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/ktime.h>
/* ------------------------- */

#include "lens_info.h"
#include "lens_list.h"

#define AF_DRVNAME "MAINAF"

#if defined(CONFIG_MTK_LEGACY)
#define I2C_CONFIG_SETTING 1
#elif defined(CONFIG_OF)
#define I2C_CONFIG_SETTING 2 /* device tree */
#else

#define I2C_CONFIG_SETTING 1
#endif

#if I2C_CONFIG_SETTING == 1
#define LENS_I2C_BUSNUM 0
#define I2C_REGISTER_ID 0x28
#endif

#define PLATFORM_DRIVER_NAME "lens_actuator_main_af"
#define AF_DRIVER_CLASS_NAME "actuatordrv_main_af"

#if I2C_CONFIG_SETTING == 1
static struct i2c_board_info kd_lens_dev __initdata = {
	I2C_BOARD_INFO(AF_DRVNAME, I2C_REGISTER_ID)};
#endif

#define AF_DEBUG
#ifdef AF_DEBUG
#define LOG_INF(format, args...)                                               \
	pr_info(AF_DRVNAME " [%s] " format, __func__, ##args)
#else
#define LOG_INF(format, args...)
#endif

/* OIS/EIS Timer & Workqueue */
static struct workqueue_struct *ois_workqueue;
static struct work_struct ois_work;
static struct hrtimer ois_timer;

static DEFINE_MUTEX(ois_mutex);
static int g_EnableTimer;
static int g_GetOisInfoCnt;
static int g_OisPosIdx;
static struct stAF_OisPosInfo OisPosInfo;
/* ------------------------- */

static struct stAF_DrvList g_stAF_DrvList[MAX_NUM_OF_LENS] = {
	{1, AFDRV_GT9764AF, GT9764AF_SetI2Cclient, GT9764AF_Ioctl,
	GT9764AF_Release, GT9764AF_GetFileName, NULL, GT9764AF_GetCurrentPos},
};

static struct stAF_DrvList *g_pstAF_CurDrv;
static spinlock_t g_AF_SpinLock;
static int g_s4AF_Opened;
static struct i2c_client *g_pstAF_I2Cclient;

static dev_t g_AF_devno;
static struct cdev *g_pAF_CharDrv;
static struct class *actuator_class;
static struct device *lens_device;

static struct regulator *vcamaf_ldo;
static struct pinctrl *vcamaf_pio;
static struct pinctrl_state *vcamaf_pio_on;
static struct pinctrl_state *vcamaf_pio_off;

#define CAMAF_PMIC     "camaf_m1_pmic"
#define CAMAF_GPIO_ON  "camaf_m1_gpio_on"
#define CAMAF_GPIO_OFF "camaf_m1_gpio_off"

#define AF_RESONANCE_SCHEME
#ifdef AF_RESONANCE_SCHEME
static void camaf_power_init(void);
static void camaf_power_on(void);
static void camaf_power_off(void);
static int s4AF_WriteReg(u16 a_u2Data);
#define GT9764AF_SLAVE_ADDR 0x18
#define AF_NOISE_ELIMINATION_POS        0

static spinlock_t vibrate_lock;
static struct work_struct vibrate_work;
static void af_noise_vibrate_work(struct work_struct *work);
static struct work_struct vibrate_stop_work;
static void af_stop_vibrate_work(struct work_struct *work);

volatile static int g_VibeInfoCnt = 11;
volatile static int af_noise_start = 0, af_noise_stop = 0, af_noise_pownon_flag = 0;

static struct hrtimer my_timer;
static enum hrtimer_restart my_timer_func(struct hrtimer *timer)
{
	g_VibeInfoCnt--;
	//LOG_INF("my_timer_func.af_noise_stop=0x%x,g_VibeInfoCnt=%d\n",af_noise_stop,g_VibeInfoCnt);
	if ((g_VibeInfoCnt < 10) && (af_noise_stop == 0xaa)) {
		//if(af_noise_stop == 0xaa) {

			schedule_work(&vibrate_stop_work);
		//}
		return HRTIMER_NORESTART;
	}
	hrtimer_forward_now(timer, ktime_set(1000 / 1000,(1000 % 1000) * 1000000));
	return HRTIMER_RESTART;
}

static int my_timer_init(void)
{
	printk("Lamu af Resonance scheme %s enter!\n", __func__);
	hrtimer_init(&my_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	my_timer.function = my_timer_func;
	printk("Lamu af Resonance scheme %s exit!\n", __func__);
	return 0;
}

static void af_stop_vibrate_work(struct work_struct *work)
{
	spin_lock(&vibrate_lock);
	printk("Lamu af Resonance scheme %s enter!\n", __func__);
	camaf_power_off();
	af_noise_start = 0;
	printk("Lamu af Resonance scheme %s enter!\n", __func__);
	spin_unlock(&vibrate_lock);
}

static void af_noise_vibrate_work(struct work_struct *work)
{

	int i=0;
	int step = 1, temp1 = 512;

	printk("Lamu af Resonance scheme %s starting...!\n", __func__);
	for(i = 0; i < 200; i++) {
		if ((temp1 - step) > 400) {
			step = 20;
		} else {
			step = 10;
		}

		if((temp1 - step) < 10)
			break;
		mdelay(2);
		s4AF_WriteReg(temp1 - step);
		temp1 = temp1 - step;
		//printk("af_noise_vibrate_work temp1 value : %d\n", temp1);
	}

	mdelay(2);

	printk("Lamu af Resonance scheme value : %d\n", AF_NOISE_ELIMINATION_POS);
	s4AF_WriteReg(AF_NOISE_ELIMINATION_POS);

	spin_lock(&vibrate_lock);
	//af_noise_start = 0x00;
	g_VibeInfoCnt = 11;
	hrtimer_start(&my_timer,
			ktime_set(1000 / 1000,
			(1000 % 1000) * 1000000),
			HRTIMER_MODE_REL);
	printk("Lamu af Resonance scheme %s stoping...!\n", __func__);
	spin_unlock(&vibrate_lock);
}

static int s4AF_WriteReg(u16 a_u2Data)
{
	int i4RetValue = 0;

	char puSendCmd[3] = { 0x03, (char)(a_u2Data >> 8),
		(char)(a_u2Data & 0xFF) };

	g_pstAF_I2Cclient->addr = GT9764AF_SLAVE_ADDR;

	g_pstAF_I2Cclient->addr = g_pstAF_I2Cclient->addr >> 1;

	i4RetValue = i2c_master_send(g_pstAF_I2Cclient, puSendCmd, 3);

	if (i4RetValue < 0) {
		LOG_INF("I2C send failed!!\n");
		return -1;
	}

	return 0;
}

/******************************************************************************
 * SYSFS
 *****************************************************************************/
/* af_noise strobe sysfs */
static ssize_t af_noise_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	LOG_INF("Lamu af Resonance scheme show\n");
	return scnprintf(buf, PAGE_SIZE,"0->off : 1->on\n");
}

static ssize_t af_noise_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int temp;
	LOG_INF("Lamu af Resonance scheme store\n");

	g_VibeInfoCnt = 11;
	temp = simple_strtol(buf, NULL, 10);//char to int
	if(temp == 0) {
		LOG_INF("Lamu af Resonance scheme disable\n");
		spin_lock(&g_AF_SpinLock);
		if (g_s4AF_Opened) {
			spin_unlock(&g_AF_SpinLock);
			LOG_INF("The device is opened\n");
			return -1;
		}
		spin_unlock(&g_AF_SpinLock);

		af_noise_stop = 0xaa;
		if(af_noise_start != 0x00) return size;
		//camaf_power_off();
	} else if ((temp == 1) || (temp == 2)) {
		LOG_INF("Lamu af Resonance scheme enable\n");
		spin_lock(&g_AF_SpinLock);
		if (g_s4AF_Opened) {
			spin_unlock(&g_AF_SpinLock);
			LOG_INF("The device is opened\n");
			return -1;
		}
		spin_unlock(&g_AF_SpinLock);
		LOG_INF("af_noise_store.af_noise_start=%d\n",af_noise_start);
		if (af_noise_start == 0x55) {
			LOG_INF("...3355\n");
			return size;
		}
		camaf_power_init();
		camaf_power_on();
		af_noise_start = 0x55;
		schedule_work(&vibrate_work);
	} else {
		LOG_INF("Lamu af Resonance scheme val is unknow\n");
	}
	LOG_INF("Lamu af Resonance scheme store...finish...\n");
	return size;
}
static DEVICE_ATTR_RW(af_noise);
#endif

static void camaf_power_init(void)
{
	int ret;
	struct device_node *node, *kd_node;

	/* check if customer camera node defined */
	node = of_find_compatible_node(
		NULL, NULL, "mediatek,camera_af_lens");

	if (node) {
		kd_node = lens_device->of_node;
		lens_device->of_node = node;

		if (vcamaf_ldo == NULL) {
			vcamaf_ldo = regulator_get(lens_device, CAMAF_PMIC);
			if (IS_ERR(vcamaf_ldo)) {
				ret = PTR_ERR(vcamaf_ldo);
				vcamaf_ldo = NULL;
				LOG_INF("cannot get regulator\n");
			}
		}

		if (vcamaf_pio == NULL) {
			vcamaf_pio = devm_pinctrl_get(lens_device);
			if (IS_ERR(vcamaf_pio)) {
				ret = PTR_ERR(vcamaf_pio);
				vcamaf_pio = NULL;
				pr_info("cannot get pinctrl\n");
			} else {
				vcamaf_pio_on = pinctrl_lookup_state(
					vcamaf_pio, CAMAF_GPIO_ON);

				if (IS_ERR(vcamaf_pio_on)) {
					ret = PTR_ERR(vcamaf_pio_on);
					vcamaf_pio_on = NULL;
					LOG_INF("cannot get vcamaf_pio_on\n");
				}

				vcamaf_pio_off = pinctrl_lookup_state(
					vcamaf_pio, CAMAF_GPIO_OFF);

				if (IS_ERR(vcamaf_pio_off)) {
					ret = PTR_ERR(vcamaf_pio_off);
					vcamaf_pio_off = NULL;
					LOG_INF("cannot get vcamaf_pio_off\n");
				}
			}
		}

		lens_device->of_node = kd_node;
	}
}

static void camaf_power_on(void)
{
	int ret;
#ifdef AF_RESONANCE_SCHEME
	if (vcamaf_ldo && (af_noise_pownon_flag <= 0)) {
#else
	if (vcamaf_ldo) {
#endif
		ret = regulator_enable(vcamaf_ldo);
#ifdef AF_RESONANCE_SCHEME
	af_noise_pownon_flag = 1;
#endif
		LOG_INF("regulator enable (%d)\n", ret);
	}

	if (vcamaf_pio && vcamaf_pio_on) {
		ret = pinctrl_select_state(vcamaf_pio, vcamaf_pio_on);
		LOG_INF("pinctrl enable (%d)\n", ret);
	}
}

static void camaf_power_off(void)
{
	int ret;
	LOG_INF("ig_s4AF_Opened (%d)\n", g_s4AF_Opened);
	if (vcamaf_ldo && g_s4AF_Opened<=0) {
		ret = regulator_disable(vcamaf_ldo);
#ifdef AF_RESONANCE_SCHEME
	af_noise_pownon_flag = 0;
#endif
		LOG_INF("regulator disable (%d)\n", ret);
	}

	if (vcamaf_pio && vcamaf_pio_off) {
		ret = pinctrl_select_state(vcamaf_pio, vcamaf_pio_off);
		LOG_INF("pinctrl disable (%d)\n", ret);
	}
#ifdef AF_RESONANCE_SCHEME
	af_noise_stop = 0x55;
#endif
}

#ifdef CONFIG_MACH_MT6765
static int DrvPwrDn1 = 1;
static int DrvPwrDn2 = 1;
static int DrvPwrDn3 = 1;
#endif

void AF_PowerDown(void)
{
	if (g_pstAF_I2Cclient != NULL) {
		LOG_INF("+\n");
		LOG_INF("-\n");
	}
}
EXPORT_SYMBOL(AF_PowerDown);

static long AF_SetMotorName(__user struct stAF_MotorName *pstMotorName)
{
	long i4RetValue = -1;
	int i;
	struct stAF_MotorName stMotorName;

	if (copy_from_user(&stMotorName, pstMotorName,
			   sizeof(struct stAF_MotorName))) {
		LOG_INF("copy to user failed when getting motor information\n");
		return i4RetValue;
	}

	for (i = 0; i < MAX_NUM_OF_LENS; i++) {
		if (g_stAF_DrvList[i].uEnable != 1)
			break;

		LOG_INF("Search Motor Name : %s\n", g_stAF_DrvList[i].uDrvName);
		stMotorName.uMotorName[STRUCT_MOTOR_NAME - 1] = '\0';
		if (strcmp(stMotorName.uMotorName,
			   g_stAF_DrvList[i].uDrvName) == 0) {
			LOG_INF("Motor Name : %s\n", stMotorName.uMotorName);
			g_pstAF_CurDrv = &g_stAF_DrvList[i];
			i4RetValue = g_pstAF_CurDrv->pAF_SetI2Cclient(
				g_pstAF_I2Cclient, &g_AF_SpinLock,
				&g_s4AF_Opened);
			break;
		}
	}
	return i4RetValue;
}

static inline int64_t getCurNS(void)
{
	int64_t ns;
	struct timespec64 time;

	time.tv_sec = time.tv_nsec = 0;
	/* get_monotonic_boottime(&time); */
	ns = time.tv_sec * 1000000000LL + time.tv_nsec;

	return ns;
}

/* OIS/EIS Timer & Workqueue */
static void ois_pos_polling(struct work_struct *data)
{
	mutex_lock(&ois_mutex);
	if (g_pstAF_CurDrv) {
		if (g_pstAF_CurDrv->pAF_OisGetHallPos) {
			int PosX = 0, PosY = 0;

			g_pstAF_CurDrv->pAF_OisGetHallPos(&PosX, &PosY);
			if (g_OisPosIdx >= 0) {
				OisPosInfo.TimeStamp[g_OisPosIdx] = getCurNS();
				OisPosInfo.i4OISHallPosX[g_OisPosIdx] = PosX;
				OisPosInfo.i4OISHallPosY[g_OisPosIdx] = PosY;
				g_OisPosIdx++;
				g_OisPosIdx &= OIS_DATA_MASK;
			}
		}
	}
	mutex_unlock(&ois_mutex);
}

static enum hrtimer_restart ois_timer_func(struct hrtimer *timer)
{
	g_GetOisInfoCnt--;

	if (ois_workqueue != NULL && g_GetOisInfoCnt > 11)
		queue_work(ois_workqueue, &ois_work);

	if (g_GetOisInfoCnt < 10) {
		g_EnableTimer = 0;
		return HRTIMER_NORESTART;
	}

	hrtimer_forward_now(timer, ktime_set(0, 5000000));
	return HRTIMER_RESTART;
}
/* ------------------------- */

/* ////////////////////////////////////////////////////////////// */
static long AF_Ioctl(struct file *a_pstFile, unsigned int a_u4Command,
		     unsigned long a_u4Param)
{
	long i4RetValue = 0;

	switch (a_u4Command) {
	case AFIOC_S_SETDRVNAME:
		i4RetValue = AF_SetMotorName(
			(__user struct stAF_MotorName *)(a_u4Param));
		break;

	case AFIOC_G_GETDRVNAME:
		{
	/* Set Driver Name */
	int i;
	struct stAF_MotorName stMotorName = {'\0'};
	struct stAF_DrvList *pstAF_CurDrv = NULL;
	__user struct stAF_MotorName *pstMotorName =
			(__user struct stAF_MotorName *)a_u4Param;

	if (copy_from_user(&stMotorName, pstMotorName,
			   sizeof(struct stAF_MotorName))) {
		LOG_INF("copy to user failed when getting motor information\n");
		break;
	}

	for (i = 0; i < MAX_NUM_OF_LENS; i++) {
		if (g_stAF_DrvList[i].uEnable != 1)
			break;

		LOG_INF("Search Motor Name : %s\n", g_stAF_DrvList[i].uDrvName);
		stMotorName.uMotorName[STRUCT_MOTOR_NAME - 1] = '\0';
		if (strcmp(stMotorName.uMotorName,
			   g_stAF_DrvList[i].uDrvName) == 0) {
			LOG_INF("Motor Name : %s\n", stMotorName.uMotorName);
			pstAF_CurDrv = &g_stAF_DrvList[i];
			break;
		}
	}

	/* Get File Name */
	if (pstAF_CurDrv) {
		if (pstAF_CurDrv->pAF_GetFileName) {
			__user struct stAF_MotorName *pstMotorName =
			(__user struct stAF_MotorName *)a_u4Param;
			struct stAF_MotorName MotorFileName;

			pstAF_CurDrv->pAF_GetFileName(
					MotorFileName.uMotorName);
			i4RetValue = 1;
			LOG_INF("GETDRVNAME : get file name(%s)\n",
				MotorFileName.uMotorName);
			if (copy_to_user(
				    pstMotorName, &MotorFileName,
				    sizeof(struct stAF_MotorName)))
				LOG_INF("copy to user failed\n");
		}
	}
		}
		break;

	case AFIOC_S_SETDRVINIT:
		spin_lock(&g_AF_SpinLock);
		g_s4AF_Opened = 1;
		spin_unlock(&g_AF_SpinLock);
		break;

	case AFIOC_S_SETPOWERDOWN:
		AF_PowerDown();
		i4RetValue = 1;
		break;

	case AFIOC_G_OISPOSINFO:
		if (g_pstAF_CurDrv) {
			if (g_pstAF_CurDrv->pAF_OisGetHallPos) {
				__user struct stAF_OisPosInfo *pstOisPosInfo =
					(__user struct stAF_OisPosInfo *)
						a_u4Param;

				mutex_lock(&ois_mutex);

				if (copy_to_user(
					    pstOisPosInfo, &OisPosInfo,
					    sizeof(struct stAF_OisPosInfo)))
					LOG_INF("copy to user failed\n");

				g_OisPosIdx = 0;
				g_GetOisInfoCnt = 100;
				memset(&OisPosInfo, 0, sizeof(OisPosInfo));
				mutex_unlock(&ois_mutex);

				if (g_EnableTimer == 0) {
					/* Start Timer */
					hrtimer_start(&ois_timer,
						      ktime_set(0, 50000000),
						      HRTIMER_MODE_REL);
					g_EnableTimer = 1;
				}
			}
		}
		break;

	default:
		if (g_pstAF_CurDrv) {
			if (g_pstAF_CurDrv->pAF_Ioctl)
				i4RetValue = g_pstAF_CurDrv->pAF_Ioctl(
					a_pstFile, a_u4Command, a_u4Param);
		}
		break;
	}

	return i4RetValue;
}

#ifdef CONFIG_COMPAT
static long AF_Ioctl_Compat(struct file *a_pstFile, unsigned int a_u4Command,
			    unsigned long a_u4Param)
{
	long i4RetValue = 0;

	i4RetValue = AF_Ioctl(a_pstFile, a_u4Command,
			      (unsigned long)compat_ptr(a_u4Param));

	return i4RetValue;
}
#endif

/* Main jobs: */
/* 1.check for device-specified errors, device not ready. */
/* 2.Initialize the device if it is opened for the first time. */
/* 3.Update f_op pointer. */
/* 4.Fill data structures into private_data */
/* CAM_RESET */
static int AF_Open(struct inode *a_pstInode, struct file *a_pstFile)
{
	LOG_INF("Start\n");

	spin_lock(&g_AF_SpinLock);
	if (g_s4AF_Opened) {
		spin_unlock(&g_AF_SpinLock);
		LOG_INF("The device is opened\n");
		return -EBUSY;
	}
	g_s4AF_Opened = 1;
	spin_unlock(&g_AF_SpinLock);

	camaf_power_init();
	camaf_power_on();

	/* OIS/EIS Timer & Workqueue */
	/* init work queue */
	INIT_WORK(&ois_work, ois_pos_polling);

	/* init timer */
	hrtimer_init(&ois_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ois_timer.function = ois_timer_func;

	g_EnableTimer = 0;
	/* ------------------------- */

	LOG_INF("End\n");

	return 0;
}

/* Main jobs: */
/* 1.Deallocate anything that "open" allocated in private_data. */
/* 2.Shut down the device on last close. */
/* 3.Only called once on last time. */
/* Q1 : Try release multiple times. */
struct RemoveNoiseParam {
    struct inode *a_pstInode;
    struct file  *a_pstFile;
};

static int taskRemoveNoise(void *param)
{
    struct inode *a_pstInode = ((struct RemoveNoiseParam *)param)->a_pstInode;
    struct file  *a_pstFile  = ((struct RemoveNoiseParam *)param)->a_pstFile;
    unsigned long currentPos = g_pstAF_CurDrv->pAF_GetCurrentPos();

    LOG_INF("remove noise start");

    // multiple times
    while (currentPos > 500) {
        currentPos -= 50;
        g_pstAF_CurDrv->pAF_Ioctl(a_pstFile, AFIOC_T_MOVETO, currentPos);
        mdelay(4);
    }

    if (g_pstAF_CurDrv) {
        g_pstAF_CurDrv->pAF_Release(a_pstInode, a_pstFile);
        g_pstAF_CurDrv = NULL;
    } else {
        spin_lock(&g_AF_SpinLock);
        g_s4AF_Opened = 0;
        spin_unlock(&g_AF_SpinLock);
    }

    camaf_power_off();

    LOG_INF("remove noise end,g_s4AF_Opened;%d",g_s4AF_Opened);
    return 0;
}

static int AF_Release(struct inode *a_pstInode, struct file *a_pstFile)
{
	struct task_struct *pTask = NULL;
    struct RemoveNoiseParam param = {
        .a_pstInode = a_pstInode,
        .a_pstFile  = a_pstFile
    };

	LOG_INF("Start\n");
	if (g_pstAF_CurDrv && g_pstAF_CurDrv->pAF_GetCurrentPos != NULL) {
		pTask = kthread_run(taskRemoveNoise, &param, "taskRemoveNoise");
    } else {
		if (g_pstAF_CurDrv) {
			g_pstAF_CurDrv->pAF_Release(a_pstInode, a_pstFile);
			g_pstAF_CurDrv = NULL;
		} else {
			spin_lock(&g_AF_SpinLock);
			g_s4AF_Opened = 0;
			spin_unlock(&g_AF_SpinLock);
		}

	camaf_power_off();
	}
	/* OIS/EIS Timer & Workqueue */
	/* Cancel Timer */
	hrtimer_cancel(&ois_timer);

	/* flush work queue */
	flush_work(&ois_work);

	if (ois_workqueue) {
		flush_workqueue(ois_workqueue);
		destroy_workqueue(ois_workqueue);
		ois_workqueue = NULL;
	}
	/* ------------------------- */

	LOG_INF("End\n");

	return 0;
}

static const struct file_operations g_stAF_fops = {
	.owner = THIS_MODULE,
	.open = AF_Open,
	.release = AF_Release,
	.unlocked_ioctl = AF_Ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = AF_Ioctl_Compat,
#endif
};

static inline int Register_AF_CharDrv(void)
{
	LOG_INF("Start\n");

	/* Allocate char driver no. */
	if (alloc_chrdev_region(&g_AF_devno, 0, 1, AF_DRVNAME)) {
		LOG_INF("Allocate device no failed\n");

		return -EAGAIN;
	}
	/* Allocate driver */
	g_pAF_CharDrv = cdev_alloc();

	if (g_pAF_CharDrv == NULL) {
		unregister_chrdev_region(g_AF_devno, 1);

		LOG_INF("Allocate mem for kobject failed\n");

		return -ENOMEM;
	}
	/* Attatch file operation. */
	cdev_init(g_pAF_CharDrv, &g_stAF_fops);

	g_pAF_CharDrv->owner = THIS_MODULE;

	/* Add to system */
	if (cdev_add(g_pAF_CharDrv, g_AF_devno, 1)) {
		LOG_INF("Attatch file operation failed\n");

		unregister_chrdev_region(g_AF_devno, 1);

		return -EAGAIN;
	}

	actuator_class = class_create(AF_DRIVER_CLASS_NAME);
	if (IS_ERR(actuator_class)) {
		int ret = PTR_ERR(actuator_class);

		LOG_INF("Unable to create class, err = %d\n", ret);
		return ret;
	}

	lens_device = device_create(actuator_class, NULL, g_AF_devno, NULL,
				    AF_DRVNAME);

	if (lens_device == NULL)
		return -EIO;
#ifdef AF_RESONANCE_SCHEME
	if (device_create_file(lens_device, &dev_attr_af_noise)) {
		LOG_INF("Failed to create device file(strobe)\n");
		device_remove_file(lens_device, &dev_attr_af_noise);
	}
#endif
	LOG_INF("End\n");
	return 0;
}

static inline void Unregister_AF_CharDrv(void)
{
	LOG_INF("Start\n");
#ifdef AF_RESONANCE_SCHEME
	LOG_INF("device_remove_file\n");
	device_remove_file(lens_device, &dev_attr_af_noise);
#endif
	/* Release char driver */
	cdev_del(g_pAF_CharDrv);

	unregister_chrdev_region(g_AF_devno, 1);

	device_destroy(actuator_class, g_AF_devno);

	class_destroy(actuator_class);
#ifdef AF_RESONANCE_SCHEME
	cancel_work_sync(&vibrate_work);
	cancel_work_sync(&vibrate_stop_work);
	hrtimer_cancel(&my_timer);
#endif
	LOG_INF("End\n");
}

/* //////////////////////////////////////////////////////////////////// */

static int AF_i2c_probe(struct i2c_client *client);
static void AF_i2c_remove(struct i2c_client *client);
static const struct i2c_device_id AF_i2c_id[] = {{AF_DRVNAME, 0}, {} };

/* TOOL : kernel-3.10\tools\dct */
/* PATH : vendor\mediatek\proprietary\custom\#project#\kernel\dct\dct */
#if I2C_CONFIG_SETTING == 2
static const struct of_device_id MAINAF_of_match[] = {
	{.compatible = "mediatek,CAMERA_MAIN_AF"}, {},
};
#endif

static struct i2c_driver AF_i2c_driver = {
	.probe = AF_i2c_probe,
	.remove = AF_i2c_remove,
	.driver.name = AF_DRVNAME,
#if I2C_CONFIG_SETTING == 2
	.driver.of_match_table = MAINAF_of_match,
#endif
	.id_table = AF_i2c_id,
};

static void AF_i2c_remove(struct i2c_client *client)
{
	Unregister_AF_CharDrv();
}

/* Kirby: add new-style driver {*/
static int AF_i2c_probe(struct i2c_client *client)
{
	int i4RetValue = 0;

	LOG_INF("Start\n");

	/* Kirby: add new-style driver { */
	g_pstAF_I2Cclient = client;

	/* Register char driver */
	i4RetValue = Register_AF_CharDrv();

	if (i4RetValue) {

		LOG_INF(" register char device failed!\n");

		return i4RetValue;
	}
#ifdef AF_RESONANCE_SCHEME
	INIT_WORK(&vibrate_work, af_noise_vibrate_work);
	INIT_WORK(&vibrate_stop_work, af_stop_vibrate_work);
	my_timer_init();
	spin_lock_init(&vibrate_lock);
#endif
	spin_lock_init(&g_AF_SpinLock);

	LOG_INF("Attached!!\n");

	return 0;
}

static int AF_probe(struct platform_device *pdev)
{
	return i2c_add_driver(&AF_i2c_driver);
}

static int AF_remove(struct platform_device *pdev)
{
	i2c_del_driver(&AF_i2c_driver);
	return 0;
}

static int AF_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	return 0;
}

static int AF_resume(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gaf_of_device_id[] = {
	{.compatible = "mediatek,camera_af_lens",},
	{}
};
#endif

/* platform structure */
static struct platform_driver g_stAF_Driver = {
	.probe = AF_probe,
	.remove = AF_remove,
	.suspend = AF_suspend,
	.resume = AF_resume,
	.driver = {
		.name = PLATFORM_DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = gaf_of_device_id,
#endif
	} };

static struct platform_device g_stAF_device = {
	.name = PLATFORM_DRIVER_NAME, .id = 0, .dev = {} };

static int __init MAINAF_i2C_init(void)
{
#if I2C_CONFIG_SETTING == 1
	i2c_register_board_info(LENS_I2C_BUSNUM, &kd_lens_dev, 1);
#endif

	if (platform_device_register(&g_stAF_device)) {
		LOG_INF("failed to register AF driver\n");
		return -ENODEV;
	}

	if (platform_driver_register(&g_stAF_Driver)) {
		LOG_INF("Failed to register AF driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit MAINAF_i2C_exit(void)
{
	platform_driver_unregister(&g_stAF_Driver);
	platform_device_unregister(&g_stAF_device);
}
module_init(MAINAF_i2C_init);
module_exit(MAINAF_i2C_exit);

MODULE_DESCRIPTION("MAINAF lens module driver");
MODULE_AUTHOR("KY Chen <ky.chen@Mediatek.com>");
MODULE_LICENSE("GPL");
