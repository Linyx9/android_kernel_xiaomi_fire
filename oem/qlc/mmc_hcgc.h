#ifndef _MMC_MMC_HCGC_H
#define _MMC_MMC_HCGC_H

#include <linux/moduleparam.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/string_helpers.h>
#include <linux/delay.h>
#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/pm_runtime.h>
#include <linux/idr.h>
#include <linux/debugfs.h>

#include <linux/mmc/ioctl.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
                      
#include <linux/kthread.h>      
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/time.h>

#ifdef CONFIG_MMC_FFU
#include <linux/mmc/ffu.h>
#endif

#ifdef CONFIG_MTK_MMC_PWR_WP
#include <mt-plat/mtk_partition.h>
#include <linux/types.h>
#include "mtk_emmc_write_protect.h"
#endif

#include <linux/uaccess.h>

#include "queue.h"
#include "block.h"
#include "core.h"
#include "card.h"
#include "host.h"
#include "bus.h"
#include "mmc_ops.h"
// #include "quirks.h"
#include "sd_ops.h"
#include "../host/cqhci.h"
#ifdef CONFIG_MTK_EMMC_HW_CQ
#include "dbg.h"
#endif


#define HCGC_Module

#ifdef HCGC_Module

#define HCGC_CHECK_MODE    2	/*Hcgc check mode*/
#define HCGC_OFF   		   1	/*Hcgc check mode*/
#define HCGC_ON   		   0	/*Hcgc check mode*/
struct mmc_queue;
struct request;

struct mmc_gc_cmd{
	unsigned char bHCGCState;
	unsigned char bOver_temperature_flag;
	unsigned char bSLCFreeSpaceLimit;
	unsigned short wCurrent_temperature;
	unsigned short wHighest_write_temperature;
	unsigned short wLowest_write_temperature;
	unsigned int dwCmd_Result;
	unsigned int dwGc_Result;
	unsigned int dwGc_Cmd;
	unsigned int dwGc_ID;
	unsigned int dwGetSLCFreeSpaceLimit;
	unsigned int dwSetSLCFreeSpaceLimit;
	unsigned int dwGetPre_dirty_Size;
	unsigned int dwSetPre_dirty_Size;
	unsigned int dwGetStatus;
	unsigned int dwGCSlcBoostStatus;
	unsigned int dwSlcUsageRate;
	unsigned int dwSlcFreeSpace;
	unsigned int dwSLC_write_size;
};
unsigned int Hcgc_Check(struct mmc_blk_data *md);
#define MMC_IOC_HCGC_CMD _IOWR(MMC_BLOCK_MAJOR, 2, struct mmc_gc_cmd)
#endif



#endif

