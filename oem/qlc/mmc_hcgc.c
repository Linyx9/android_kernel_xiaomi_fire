#include "mmc_hcgc.h"

/*
 * There is one mmc_blk_data per slot.
 */
struct mmc_blk_data 
{
	struct device	*parent;
	struct gendisk	*disk;
	struct mmc_queue queue;
	struct list_head part;
	struct list_head rpmbs;

	unsigned int	flags;
#define MMC_BLK_CMD23	(1 << 0)	/* Can do SET_BLOCK_COUNT for multiblock */
#define MMC_BLK_REL_WR	(1 << 1)	/* MMC Reliable write support */

	struct kref	kref;
	unsigned int	read_only;
	unsigned int	part_type;
	unsigned int	reset_done;
#define MMC_BLK_READ		BIT(0)
#define MMC_BLK_WRITE		BIT(1)
#define MMC_BLK_DISCARD		BIT(2)
#define MMC_BLK_SECDISCARD	BIT(3)
#define MMC_BLK_CQE_RECOVERY	BIT(4)
#define MMC_BLK_TRIM		BIT(5)

	/*
	 * Only set in main mmc_blk_data associated
	 * with mmc_card with dev_set_drvdata, and keeps
	 * track of the current selected device partition.
	 */
	unsigned int	part_curr;
#define MMC_BLK_PART_INVALID	UINT_MAX	/* Unknown partition active */
	int	area_type;

	/* debugfs files (only in main mmc_blk_data) */
	struct dentry *status_dentry;
	struct dentry *ext_csd_dentry;
};

struct mmc_blk_ioc_data {
	struct mmc_ioc_cmd ic;
	unsigned char *buf;
	u64 buf_bytes;
	unsigned int flags;
#define MMC_BLK_IOC_DROP	BIT(0)	/* drop this mrq */
#define MMC_BLK_IOC_SBC	BIT(1)	/* use mrq.sbc */

	struct mmc_rpmb_data *rpmb;
};


/*****************************Hcgc module******************************/
//#ifdef Hcgc_Module
#if  1

/******AES module*******/
#define BPOLY     0x1b					/*!< Lower 8 bits of (x^8+x^4+x^3+x+1), ie. (x^4+x^3+x+1).*/
#define BLOCKSIZE 16					/*!< Block size in number of bytes.*/
#define KEYLENGTH 16					/*!< Key length in number of bytes.*/
#define ROUNDS    10					/*!< Number of rounds.*/
#define CapUtilizationLimitLevel0 96	/*Space utilization limit level 0 */
#define CapUtilizationLimitLevel1 91	/*Space utilization limit level 1 */
#define CapUtilizationLimitLevel2 86	/*Space utilization limit level 2 */
#define CapUtilizationLevel0	  75	/*Space utilization level 0 */
#define CapUtilizationLevel1      80	/*Space utilization level 1 */
#define CapUtilizationLevel2      85	/*Space utilization level 2 */
#define Authorization_fail    0xFFFF	/*Hcgc authorization failed*/

static unsigned char *powTbl;			/*!< Final location of exponentiation lookup table.*/
static unsigned char *logTbl;			/*!< Final location of logarithm lookup table.*/
static unsigned char *sBox;				/*!< Final location of s-box.*/
// static unsigned char *sBoxInv;			/*!< Final location of inverse s-box.*/
static unsigned char *expandedKey;		/*!< Final location of expanded key.*/

static unsigned char block1[256];		/*!< Workspace 1.*/
static unsigned char block2[256];		/*!< Worksapce 2.*/
static unsigned char tempbuf[256];

unsigned int Hcgc_flag = HCGC_CHECK_MODE;

/******************Hcgc function related************************/
#define MMC_CMD_GEN_CMD 56   			   /*CMD56*/
#define Hcgc_AUTHORIZATION_READ  0x000005F1  /*Hcgc authorized read*/
#define Hcgc_AUTHORIZATION_WRITE 0x000005F0  /*Hcgc authorized write*/
#define Hcgc_PRIVATE_MODE_READ   0x300005F1  /*Hcgc private mode read*/
#define Hcgc_PRIVATE_MODE_WRITE  0x300005F0  /*Hcgc private mode write*/
#define Gc_switch		1				   /*on/off GC*/
#define Gc_Set_Pre		2				   /*Set GC Pre_dirty_Size*/
#define Gc_Set_Hcgc		3				   /*Set GC hcgc_size*/
#define Gc_Get_Status	4				   /*Get GC status*/
#define Gc_Get_Wb		5				   /*Get GC SLC buffer status*/
#define Gc_Get_Hcgc		6				   /*Get GC hcgc_size*/
#define Chack_Hcgc		7				   /*Chack GC hcgc*/
#define Gc_Get_Pre		8				   /*Get GC Pre_dirty_Size*/
#define Gc_Status		9				   /*Get GC status*/
#define Gc_Slc_status	10				   /*Get GC boot status*/
#define Gc_Slc_Free		11				   /*Get GC slc_free*/
#define Gc_Usage_Rate	12				   /*Get GC Gc Usage Rate*/
#define Gc_Current_Temperature			13	
#define Gc_Highest_Write_Temperature	14	
#define Gc_Lowest_Write_Temperature		15	
#define Gc_Slc_Write_Size				16	
#define Gc_Over_Temperature_Flag		17	

#define MMC_PART_USER   0
#define MMC_PART_RPMB   3

#define HCGC_CHACK_DELAY 100

#define STR_PRE_MANUAL_GC_SIZE 				"pre_manual_gc_size"
#define STR_MANUAL_GC          				"manual_gc"
#define STR_MANUAL_GC_SIZE     				"manual_gc_size"
#define STR_MANUAL_GC_STATUS   				"manual_gc_status"
#define STR_WB_AVAIL_BUF 					"wb_avail_buf"
#define STR_GC_STATUS 	        			"gc_status"
#define STR_GC_SLC_BOOST_STATUS 			"gc_slc_boost_status"
#define STR_SLC_FREE_SPACE 					"slc_free_space"
#define STR_CURRENT_TEMPERATURE 			"current_temperature"
#define STR_HIGHEST_WRITE_TEMPERATURE 		"highest_write_temperature"
#define STR_LOWEST_WRITE_TEMPERATURE 		"lowest_write_temperature"
#define STR_SLC_WRITE_SIZE 					"slc_write_size"
#define STR_OVER_TEMPERATURE_FLAG 			"over_temperature_flag"

#define mmcpath "/dev/block/mmcblk0"

static DEFINE_MUTEX(open_lock);
static int perdev_minors = CONFIG_MMC_BLOCK_MINORS;
static DEFINE_IDA(mmc_blk_ida);

/******************Hcgc function*************************/
typedef struct 
{
	unsigned char  bchecksum;               /*0x00 XOR check sum*/
	unsigned char  bFBAFun;                 /*0x01 SLC Boost,The default state is the same as IDA*/
 	unsigned char  bIdleGCFun;				/*0x02 IdleGC; It is on by default, which is the same as the default idlegc state of EMMC*/
	unsigned char  bRsv;					/*0x03 */

	unsigned short  wIdleGCStartTime;		/*0x04 Enter idle recovery time/ms;100 */
	unsigned short  wIdleGCEfficiency;		/*0x06 RO Current Idle GC Efficiency*/
	unsigned short  wIdleGCEfficiencyLimit; /*0x08 Current Idle GC Efficiency Limitation*/
	unsigned short  wIdleGCState;			/*0x0A RO*/
											/*The default value is 0xFFFE, which is set only after app starts idlegc */
											/*1:In progress*/
											/*2:Completed*/
											/*4:Idle GC stopped（LOW EFFICIENCY）*/
											/*8:No more space*/
	unsigned int dwSLCFBASize;				/*0x0C Current SLC free space*/
	unsigned int dwTLCFBASize;				/*0x10 Current TLC/MLC free space*/
	unsigned int dwValidData;				/*0x14 RO All valid user data in the eMMC product in MB*/
	unsigned int dwCollectedGarb;			/*0x18 DebugOnly*/
	unsigned int dwMaxUserSize;            	/*0x1C */
	unsigned char bCapUtilization;			/*0x20 Space utilization*/
	unsigned char bCapUtilizationLimit;		/*0x21 Space utilization limit*/
	unsigned short wCurrent_temperature;	/*0x22 eMMC Current temperature*/
	unsigned short wHighest_write_temperature;/*0x24 Flash maximum safe write temperature*/
	unsigned short wLowest_write_temperature;/*0x26 Flash minimum safe write temperature.*/
	unsigned int dwSLC_write_size;			/*0x28 Temperature protection maximum writable capacity*/
	unsigned char bOver_temperature_flag;	/*0x2C Indication of whether the current temperature is safe or not.*/

	unsigned char bRsvE[7];					/*0x2D Reserved*/
	unsigned int dwSLCFreeSpaceLimit;		/*0x34 Current SLC Free Space Limitation*/
	unsigned int dwPre_dirty_Size;			/*0x38 Current Pre-dirty Size */
	unsigned char bSLCFreeSpaceLimit;		/*0x3C Current SLC Usage Rate*/
	unsigned char bHCGCState;				/*0x3D 0x00: Clean 0x01: Pre-dirty 0x02: Dirty 0x03: Pause*/
	unsigned char bErrorFlag;				/*0x3E 0x00: Default Value 0x01: Checksum Error in last data block receive*/
}tyHcgcPram;

struct mmc_gc_cmd Hcgc_Result = {0};	/*Storage results*/

unsigned char AES_Key_Table[KEYLENGTH] =
	{
		0xd0, 0x94, 0x3f, 0x8c, 0x29, 0x76, 0x15, 0xd8,
		0x20, 0x40, 0xe3, 0x27, 0x45, 0xd8, 0x48, 0xad,
	};

char HcgcFun_AES_KEY[] = {"aLl7RzwY1KPeb8j9"};	/*Secret key  密钥需要与产品组沟通获取*/

/******AES module*******/
void CalcPowLog(unsigned char *powTbl, unsigned char *logTbl)
{
	unsigned char i = 0;
	unsigned char t = 1;

	do 
	{
		/*Use 0x03 as root for exponentiation and logarithms.*/
		powTbl[i] = t;
		logTbl[t] = i;
		i++;

		/* Muliply t by 3 in GF(2^8).*/
		t ^= (t << 1) ^ (t & 0x80 ? BPOLY : 0);
	}while( t != 1 );					/* Cyclic properties ensure that i < 255.*/

	powTbl[255] = powTbl[0];			/* 255 = '-0', 254 = -1, etc.*/
}

void CalcSBox( unsigned char * sBox )
{
	unsigned char i, rot;
	unsigned char temp;
	unsigned char result;

	/* Fill all entries of sBox[].*/
	i = 0;
	do 
	{
		/*Inverse in GF(2^8).*/
		if( i > 0 ) 
		{
			temp = powTbl[ 255 - logTbl[i] ];
		} 
		else 
		{
			temp = 0;
		}

		/* Affine transformation in GF(2). */
		result = temp ^ 0x63;				/* Start with adding a vector in GF(2). */
		for( rot = 0; rot < 4; rot++ )
		{
			/* Rotate left. */
			temp = (temp<<1) | (temp>>7);

			/* Add rotated byte in GF(2). */
			result ^= temp;
		}

		/* Put result in table. */
		sBox[i] = result;
	} while( ++i != 0 );
}

void CycleLeft( unsigned char * row )
{
	/* Cycle 4 bytes in an array left once.*/
	unsigned char temp = row[0];

	row[0] = row[1];
	row[1] = row[2];
	row[2] = row[3];
	row[3] = temp;
}

void SubBytes( unsigned char * bytes, unsigned char count )
{
	do 
	{
		*bytes = sBox[ *bytes ]; /* Substitute every byte in state. */
		bytes++;
	} while( --count );
}

void XORBytes( unsigned char * bytes1, unsigned char * bytes2, unsigned char count )
{
	do 
	{
		*bytes1 ^= *bytes2; /* Add in GF(2), ie. XOR. */
		bytes1++;
		bytes2++;
	} while( --count );
}

void KeyExpansion( unsigned char * expandedKey )
{
	unsigned char temp[4];
	unsigned char i;
	unsigned char Rcon[4] = { 0x01, 0x00, 0x00, 0x00 }; /* Round constant. */

	unsigned char * key = AES_Key_Table;

	/* Copy key to start of expanded key. */
	i = KEYLENGTH;
	do 
	{
		*expandedKey = *key;
		expandedKey++;
		key++;
	} while( --i );

	/* Prepare last 4 bytes of key in temp. */
	expandedKey -= 4;
	temp[0] = *(expandedKey++);
	temp[1] = *(expandedKey++);
	temp[2] = *(expandedKey++);
	temp[3] = *(expandedKey++);

	/* Expand key. */
	i = KEYLENGTH;
	while( i < BLOCKSIZE*(ROUNDS+1) ) 
	{
		/* Are we at the start of a multiple of the key size? */
		if( (i % KEYLENGTH) == 0 )
		{
			CycleLeft( temp ); 			/* Cycle left once. */
			SubBytes( temp, 4 ); 		/* Substitute each byte.*/
			XORBytes( temp, Rcon, 4 ); 	/* Add constant in GF(2).*/
			*Rcon = (*Rcon << 1) ^ (*Rcon & 0x80 ? BPOLY : 0);
		}

		/* Keysize larger than 24 bytes, ie. larger that 192 bits? */
		#if KEYLENGTH > 24
		/* Are we right past a block size? */
		else if( (i % KEYLENGTH) == BLOCKSIZE ) 
		{
			SubBytes( temp, 4 ); /* Substitute each byte. */
		}
		#endif

		/* Add bytes in GF(2) one KEYLENGTH away. */
		XORBytes( temp, expandedKey - KEYLENGTH, 4 );

		/* Copy result to current 4 bytes. */
		*(expandedKey++) = temp[ 0 ];
		*(expandedKey++) = temp[ 1 ];
		*(expandedKey++) = temp[ 2 ];
		*(expandedKey++) = temp[ 3 ];

		i += 4; /* Next 4 bytes. */
	}
}

void ShiftRows( unsigned char * state )
{
	unsigned char temp;

	/* Note: State is arranged column by column. */

	/* Cycle second row left one time. */
	temp = state[ 1 + 0*4 ];
	state[ 1 + 0*4 ] = state[ 1 + 1*4 ];
	state[ 1 + 1*4 ] = state[ 1 + 2*4 ];
	state[ 1 + 2*4 ] = state[ 1 + 3*4 ];
	state[ 1 + 3*4 ] = temp;

	/* Cycle third row left two times. */
	temp = state[ 2 + 0*4 ];
	state[ 2 + 0*4 ] = state[ 2 + 2*4 ];
	state[ 2 + 2*4 ] = temp;
	temp = state[ 2 + 1*4 ];
	state[ 2 + 1*4 ] = state[ 2 + 3*4 ];
	state[ 2 + 3*4 ] = temp;

	/* Cycle fourth row left three times, ie. right once. */
	temp = state[ 3 + 3*4 ];
	state[ 3 + 3*4 ] = state[ 3 + 2*4 ];
	state[ 3 + 2*4 ] = state[ 3 + 1*4 ];
	state[ 3 + 1*4 ] = state[ 3 + 0*4 ];
	state[ 3 + 0*4 ] = temp;
}

unsigned char Multiply( unsigned char num, unsigned char factor )
{
	unsigned char mask = 1;
	unsigned char result = 0;

	while( mask != 0 ) 
	{
		/* Check bit of factor given by mask. */
		if( mask & factor ) 
		{
			/* Add current multiple of num in GF(2). */
			result ^= num;
		}

		/* Shift mask to indicate next bit. */
		mask <<= 1;

		/* Double num. */
		num = (num << 1) ^ (num & 0x80 ? BPOLY : 0);
	}

	return result;
}

unsigned char DotProduct( unsigned char * vector1, unsigned char * vector2 )
{
	unsigned char result = 0;

	result ^= Multiply( *vector1++, *vector2++ );
	result ^= Multiply( *vector1++, *vector2++ );
	result ^= Multiply( *vector1++, *vector2++ );
	result ^= Multiply( *vector1  , *vector2   );

	return result;
}

void MixColumn( unsigned char * column )
{
	unsigned char row[8] = {0x02, 0x03, 0x01, 0x01, 0x02, 0x03, 0x01, 0x01}; 
	/* Prepare first row of matrix twice, to eliminate need for cycling. */

	unsigned char result[4];

	/* Take dot products of each matrix row and the column vector. */
	result[0] = DotProduct( row+0, column );
	result[1] = DotProduct( row+3, column );
	result[2] = DotProduct( row+2, column );
	result[3] = DotProduct( row+1, column );

	/* Copy temporary result to original column. */
	column[0] = result[0];
	column[1] = result[1];
	column[2] = result[2];
	column[3] = result[3];
}

void MixColumns( unsigned char * state )
{
    MixColumn(state + 0 * 4);
    MixColumn(state + 1 * 4);
    MixColumn(state + 2 * 4);
    MixColumn(state + 3 * 4);
}

void Cipher( unsigned char * block, unsigned char * expandedKey )
{
	unsigned char round = ROUNDS-1;

	XORBytes( block, expandedKey, 16 );
	expandedKey += BLOCKSIZE;

	do 
	{
		SubBytes( block, 16 );
		ShiftRows( block );
		MixColumns( block );
		XORBytes( block, expandedKey, 16 );
		expandedKey += BLOCKSIZE;
	} while( --round );

	SubBytes( block, 16 );
	ShiftRows( block );
	XORBytes( block, expandedKey, 16 );
}

void CopyBytes( unsigned char * to, unsigned char * from, unsigned char count )
{
	do 
	{
		*to = *from;
		to++;
		from++;
	} while( --count );
}

void aesEncrypt( unsigned char * buffer, unsigned char * chainBlock )
{

	XORBytes( buffer, chainBlock, BLOCKSIZE );
	Cipher( buffer, expandedKey );
	CopyBytes( chainBlock, buffer, BLOCKSIZE );
}

void aesEncInit(unsigned char bKey[], unsigned char bKeyLength)
{
	int i;
	bKeyLength = bKeyLength > KEYLENGTH? KEYLENGTH : bKeyLength;
	for(i = 0; i < bKeyLength; i++) 
		AES_Key_Table[i] = bKey[i];		

	powTbl = block1;
	logTbl = tempbuf;
	CalcPowLog( powTbl, logTbl );

	sBox = block2;
	CalcSBox( sBox );

	expandedKey = block1;
	KeyExpansion( expandedKey );
}
/******AES module End*******/

/*********Send CMD*********/
static struct mmc_blk_ioc_data *mmc_blk_ioctl_copy(
	struct mmc_ioc_cmd  *user)
{
	struct mmc_blk_ioc_data *idata;
	int err;

	idata = kmalloc(sizeof(*idata), GFP_KERNEL);
	if (!idata) {
		err = -ENOMEM;
		goto out;
	}
	memcpy(&idata->ic, user, sizeof(idata->ic));
	if (!idata->ic.opcode) {
		err = -EFAULT;
		goto idata_err;
	}

	idata->buf_bytes = (u64) idata->ic.blksz * idata->ic.blocks;
	if (idata->buf_bytes > MMC_IOC_MAX_BYTES) {
		err = -EOVERFLOW;
		goto idata_err;
	}

	if (!idata->buf_bytes) {
		idata->buf = NULL;
		return idata;
	}

	idata->buf = kmalloc(idata->buf_bytes, GFP_KERNEL);
	if (!idata->buf) {
		err = -ENOMEM;
		goto idata_err;
	}

	memcpy(idata->buf, (void *)(unsigned long)idata->ic.data_ptr, idata->buf_bytes);
	if (!idata->buf_bytes) 
	{
		err = -EFAULT;
		goto copy_err;
	}

	return idata;

copy_err:
	kfree(idata->buf);
idata_err:
	kfree(idata);
out:
	return ERR_PTR(err);
}

static int Hcgc_mmc_blk_ioctl_cmd(struct mmc_blk_data *md,
				 struct mmc_ioc_cmd *ic_ptr,
				 struct mmc_rpmb_data *rpmb)
{
	struct mmc_blk_ioc_data *idata;
	struct mmc_blk_ioc_data *idatas[1];
	struct mmc_queue *mq;
	struct mmc_card *card;
	int err = 0, ioc_err = 0;
	struct request *req;

	idata = mmc_blk_ioctl_copy(ic_ptr);
	if (IS_ERR(idata))
		return PTR_ERR(idata);

	idata->rpmb = rpmb;
	
	card = md->queue.card;
	if (IS_ERR(card)) {
		err = PTR_ERR(card);
		goto cmd_done;
	}

	mq = &md->queue;
	req = blk_mq_alloc_request(mq->queue,
		idata->ic.write_flag ? REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		goto cmd_done;
	}
	idatas[0] = idata;
	req_to_mmc_queue_req(req)->drv_op = MMC_DRV_OP_IOCTL;
	req_to_mmc_queue_req(req)->drv_op_data = idatas;
	req_to_mmc_queue_req(req)->ioc_count = 1;
	// blk_execute_rq(mq->queue, NULL, req, 0);
	// blk_execute_rq(NULL, req, 0);
	blk_execute_rq(req, false);
	ioc_err = req_to_mmc_queue_req(req)->drv_op_result;

	memcpy(&(ic_ptr->response), idata->ic.response, sizeof(idata->ic.response));

	if (!idata->ic.write_flag)
	{
		memcpy((void *)ic_ptr->data_ptr, idata->buf, idata->buf_bytes);
	}

	blk_mq_free_request(req);

cmd_done:
	kfree(idata->buf);
	kfree(idata);
	return ioc_err ? ioc_err : err;
}

/******Send CMD End*******/

/************CMD************/

static int mmc_blk_part_switch_pre(struct mmc_card *card,
				   unsigned int part_type)
{
	int ret = 0;

	if (part_type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		if (card->ext_csd.cmdq_en) {
			ret = mmc_cmdq_disable(card);
			if (ret)
				return ret;
		}
		mmc_retune_pause(card->host);
	}

	return ret;
}

static int mmc_blk_part_switch_post(struct mmc_card *card,
				    unsigned int part_type)
{
	int ret = 0;

	if (part_type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		mmc_retune_unpause(card->host);
		if (card->reenable_cmdq && !card->ext_csd.cmdq_en)
			ret = mmc_cmdq_enable(card);
	}

	return ret;
}

static inline int mmc_blk_part_switch(struct mmc_card *card,
				      unsigned int part_type)
{
	int ret = 0;
	struct mmc_blk_data *main_md = dev_get_drvdata(&card->dev);

	if (main_md->part_curr == part_type)
		return 0;

	if (mmc_card_mmc(card)) {
		u8 part_config = card->ext_csd.part_config;

		ret = mmc_blk_part_switch_pre(card, part_type);
		if (ret)
			return ret;

		part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
		part_config |= part_type;

		ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
				 EXT_CSD_PART_CONFIG, part_config,
				 card->ext_csd.part_time);
		if (ret) {
			mmc_blk_part_switch_post(card, part_type);
			return ret;
		}

		card->ext_csd.part_config = part_config;

		ret = mmc_blk_part_switch_post(card, main_md->part_curr);
	}

	main_md->part_curr = part_type;
	return ret;
}


// static inline int mmc_blk_part_switch(struct mmc_card *card,
// 				      struct mmc_blk_data *md)
// {
// 	int ret;
// 	struct mmc_blk_data *main_md = dev_get_drvdata(&card->dev);

// 	if (main_md->part_curr == md->part_type)
// 		return 0;
	

// 	if (mmc_card_mmc(card)) {
// 		u8 part_config = card->ext_csd.part_config;

// 		part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
// 		part_config |= md->part_type;

// 		ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL,
// 				 EXT_CSD_PART_CONFIG, part_config,
// 				 card->ext_csd.part_time);
// 		if (ret)
// 		{
// 			printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,ret);
// 			return ret;
// 		}

// 		card->ext_csd.part_config = part_config;
// 	}

// 	main_md->part_curr = md->part_type;
// 	return 0;
// }

unsigned int ST_EMMC_Send_CMD13(struct mmc_blk_data *md)
{
    struct mmc_ioc_cmd idata;
    int err = 0;
    unsigned int response;

	memset(&idata, 0, sizeof(idata));
	idata.opcode = MMC_SEND_STATUS;
	idata.arg = (1 << 16);
	idata.flags = MMC_RSP_R1 | MMC_CMD_AC;

	err = Hcgc_mmc_blk_ioctl_cmd(md,&idata,NULL);
    if (err)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,err);
		return err;
	}

	else
	{
		response = idata.response[0];

		if ((idata.response[0] & (0xF << 9)) == (0x4 << 9))
		{
			return 0;
		}
	}

    return response;
}	


unsigned int ST_EMMC_Hcgc_Send_CMD56(struct mmc_blk_data *md, unsigned int dwArg, void *pdata)
{
    struct mmc_ioc_cmd idata;
    int err = 0;
    // unsigned int response;

    memset(&idata, 0, sizeof(idata));
    
    idata.opcode = MMC_CMD_GEN_CMD;
    idata.arg = dwArg;
    idata.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;
    idata.data_ptr = (__u64)pdata;
    idata.blksz = 512;
    idata.blocks = 1;

	if(dwArg & 0x01)
	{
		idata.write_flag = 0;/*Read*/
	}
	else 
	{
		idata.write_flag = 1;/*Write*/
	}

	// err = ST_EMMC_Send_CMD13(md);
	// if (err)
	// {
	// 	printk(KERN_ERR "%s %s %d Errnum:%x\n",__FILE__,__FUNCTION__,__LINE__,err);
	// 	return err;
	// }

    err = Hcgc_mmc_blk_ioctl_cmd(md,&idata,NULL);
    if (err)
	{
		printk(KERN_ERR "%s %s %d Errnum:%x\n",__FILE__,__FUNCTION__,__LINE__,err);
		return err;
	}
		
	
	return 0;
}

unsigned int MakeCheckSum8(unsigned char *pdata, unsigned short wDataLen)
{
	unsigned char bCheckSum = 0;
	unsigned short wIndex;

	/*Exclusive or in 1byte*/
	for(wIndex = 1; wIndex < wDataLen; wIndex++)
	{
		bCheckSum ^= pdata[wIndex];
	}

	return bCheckSum;
}

unsigned int HcgcFun_Authorize(struct mmc_blk_data *md)
{
	int i;
	unsigned int dwErr;
	unsigned long long int *pldwReceiveData = NULL;
	unsigned char bHcgcReceiveData[512] = {0};
	unsigned char bHcgcSendData[512] = {0};
	unsigned char bData[16] = {0};
	char bKey[16] = {0};
	unsigned char bChainCipherBlock[16] = {0};
 

	/*******Preparation before authorization*******/
	dwErr = ST_EMMC_Hcgc_Send_CMD56(md, Hcgc_AUTHORIZATION_READ, bHcgcReceiveData);
	if(dwErr)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
		printk(KERN_ERR "ST_EMMC_Hcgc_Send_CMD56 with Arg2 = 0x000005F1 fail, dwErr = %d\n", dwErr);
		Hcgc_Result.dwCmd_Result = ST_EMMC_Send_CMD13(md);
		printk(KERN_ERR "%s %s %d Errnum:%x\n",__FILE__,__FUNCTION__,__LINE__,Hcgc_Result.dwCmd_Result);
		return dwErr;
	}

	memcpy(bData, bHcgcReceiveData, 16);		/*Read back the first 16 bytes of 512byte data for AES calculation*/

	/*******Get the secret key*******/
	memcpy(bKey, HcgcFun_AES_KEY, strlen(HcgcFun_AES_KEY));

	/*******AES encryption********/
	aesEncInit(bKey, sizeof(bKey));        	/*Encryption initialization. And initialize AES_Key_Table key*/
    aesEncrypt(bData, bChainCipherBlock);   /*AES encryption, bData is the encrypted data*/

	/*****Start authorization*****/
	memcpy(bHcgcSendData, bData, 16);         
	dwErr = ST_EMMC_Hcgc_Send_CMD56(md, Hcgc_AUTHORIZATION_WRITE, bHcgcSendData);
	if(dwErr)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
		printk(KERN_ERR "ST_EMMC_Hcgc_Send_CMD56 with Arg3 = 0x000005F0 fail, dwErr = %d\n", dwErr);
		Hcgc_Result.dwCmd_Result = ST_EMMC_Send_CMD13(md);
		printk(KERN_ERR "%s %s %d Errnum:%x\n",__FILE__,__FUNCTION__,__LINE__,Hcgc_Result.dwCmd_Result);
		return dwErr;
	}

	/****Verify authorization****/
	dwErr = ST_EMMC_Hcgc_Send_CMD56(md, Hcgc_AUTHORIZATION_READ, bHcgcReceiveData);
	if(dwErr)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
		printk(KERN_ERR "ST_EMMC_Hcgc_Send_CMD56 with Arg2 = 0x000005F1 fail, dwErr = %d\n", dwErr);
		Hcgc_Result.dwCmd_Result = ST_EMMC_Send_CMD13(md);
		printk(KERN_ERR "%s %s %d Errnum:%x\n",__FILE__,__FUNCTION__,__LINE__,Hcgc_Result.dwCmd_Result);
		return dwErr;
	}

	pldwReceiveData = (unsigned long long int *)bHcgcReceiveData;
	for( i = 0; i < 64; i++)
	{
		if(0x55aa55aa55aa55aa != pldwReceiveData[i])
		{
			printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
			printk(KERN_ERR "Hcgc Authorization failure\n");
			return Authorization_fail;
		}
	}
	return 0;
}

unsigned int HcgcFun_Authorization_Check(struct mmc_blk_data *md)
{
	int i;
	unsigned int dwErr;
	unsigned long long int *pldwReceiveData = NULL;
	unsigned char bHcgcReceiveData[512] = {0};

	/****Verify authorization****/
	dwErr = ST_EMMC_Hcgc_Send_CMD56(md, Hcgc_AUTHORIZATION_READ, bHcgcReceiveData);
	if(dwErr)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
		printk(KERN_ERR "ST_EMMC_Hcgc_Send_CMD56 with Arg2 = 0x200005F1 fail, dwErr = %d\n", dwErr);
		Hcgc_Result.dwCmd_Result = ST_EMMC_Send_CMD13(md);
		printk(KERN_ERR "%s %s %d Errnum:%x\n",__FILE__,__FUNCTION__,__LINE__,Hcgc_Result.dwCmd_Result);
		return dwErr;
	}

	pldwReceiveData = (unsigned long long int *)bHcgcReceiveData;
	for( i = 0; i < 64; i++)
	{
		if(0x55aa55aa55aa55aa != pldwReceiveData[i])
		{
			return Authorization_fail;
		}
	}
	return 0;
}

unsigned int HcgcFun_ReadData(struct mmc_blk_data *md, void *pReadBuff)
{
	unsigned int dwErr;

	dwErr = HcgcFun_Authorization_Check(md);
	if(dwErr == Authorization_fail)
	{
		dwErr = HcgcFun_Authorize(md);
		if(dwErr)
		{
			printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
			return dwErr;
		}
	}
	else if(dwErr)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
		return dwErr;
	}

	dwErr = ST_EMMC_Hcgc_Send_CMD56(md, Hcgc_PRIVATE_MODE_READ, pReadBuff);
	if(dwErr)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
		printk(KERN_ERR "ST_EMMC_Hcgc_Send_CMD56 with Arg1 = 0x300005F1 fail, dwErr = %d\n", dwErr);
		Hcgc_Result.dwCmd_Result = ST_EMMC_Send_CMD13(md);
		printk(KERN_ERR "%s %s %d Errnum:%x\n",__FILE__,__FUNCTION__,__LINE__,Hcgc_Result.dwCmd_Result);
		return dwErr;
	}

	return 0;
}

unsigned int HcgcFun_WriteData(struct mmc_blk_data *md, void *pWriteBuff)
{
	unsigned int dwErr;
	tyHcgcPram *ptyHcgcPram = (tyHcgcPram *)pWriteBuff;

	dwErr = HcgcFun_Authorization_Check(md);
	if(dwErr == Authorization_fail)
	{
		dwErr = HcgcFun_Authorize(md);
		if(dwErr)
		{
			printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
			return dwErr;
		}
	}
	else if(dwErr)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
		return dwErr;
	}

	/*********Start calculating checksum8*************/
	ptyHcgcPram->bchecksum = MakeCheckSum8((unsigned char *)pWriteBuff, 512);

	dwErr = ST_EMMC_Hcgc_Send_CMD56(md, Hcgc_PRIVATE_MODE_WRITE, pWriteBuff);
	if(dwErr)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
		printk(KERN_ERR "ST_EMMC_Hcgc_Send_CMD56 with Arg0 = 0x300005F0 fail, dwErr = %d\n", dwErr);
		Hcgc_Result.dwCmd_Result = ST_EMMC_Send_CMD13(md);
		printk(KERN_ERR "%s %s %d Errnum:%x\n",__FILE__,__FUNCTION__,__LINE__,Hcgc_Result.dwCmd_Result);
		return dwErr;
	}

	return 0;
}

unsigned int Hcgc_Get_Status(struct mmc_blk_data *md)
{
	int err;
	int i;
	unsigned char Buff_Result[512] = {0};
	tyHcgcPram *ptyHcgcPram = (tyHcgcPram *)Buff_Result;
	
	err = HcgcFun_ReadData(md, Buff_Result);

	if(err && (Hcgc_Result.dwGc_ID != Chack_Hcgc))
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,err);

		printk(KERN_ERR"ReadData:\n");
		for(i = 0; i < 512; i++)
		{
			if(0 == (i % 16))
			{
				printk(KERN_ERR"\n");
			}
			printk(KERN_ERR"%02X ", Buff_Result[i]);
		}
		printk(KERN_ERR"\n");
	}

	if(err)
	{
		Hcgc_Result.dwCmd_Result = err;
	}
	else
	{
		if (Hcgc_Result.dwGc_ID == Gc_Get_Status)
		{
			Hcgc_Result.bHCGCState = ptyHcgcPram->bHCGCState;
		}
		else if (Hcgc_Result.dwGc_ID == Gc_Get_Wb)
		{
			Hcgc_Result.bSLCFreeSpaceLimit = ptyHcgcPram->bSLCFreeSpaceLimit;
		}
		else if (Hcgc_Result.dwGc_ID == Gc_Get_Hcgc)
		{
			Hcgc_Result.dwGetSLCFreeSpaceLimit = ptyHcgcPram->dwSLCFreeSpaceLimit;
		}
		else if (Hcgc_Result.dwGc_ID == Gc_Get_Pre)
		{
			Hcgc_Result.dwGetPre_dirty_Size = ptyHcgcPram->dwPre_dirty_Size;
		}
		else if (Hcgc_Result.dwGc_ID == Gc_Status)
		{
			Hcgc_Result.dwGetStatus = ptyHcgcPram->wIdleGCState;
		}
		else if (Hcgc_Result.dwGc_ID == Gc_Slc_status)
		{
			Hcgc_Result.dwGCSlcBoostStatus = ptyHcgcPram->bFBAFun;
			Hcgc_Result.dwGCSlcBoostStatus |= ptyHcgcPram->bIdleGCFun << 8;
		}
		else if (Hcgc_Result.dwGc_ID == Gc_Slc_Free)
		{
			Hcgc_Result.dwSlcFreeSpace = ptyHcgcPram->dwSLCFBASize;
		}
		else if(Hcgc_Result.dwGc_ID == Gc_Current_Temperature)
		{
			Hcgc_Result.wCurrent_temperature = ptyHcgcPram->wCurrent_temperature;
		}
		else if(Hcgc_Result.dwGc_ID == Gc_Highest_Write_Temperature)
		{
			Hcgc_Result.wHighest_write_temperature = ptyHcgcPram->wHighest_write_temperature;
		}
		else if(Hcgc_Result.dwGc_ID == Gc_Lowest_Write_Temperature)
		{
			Hcgc_Result.wLowest_write_temperature = ptyHcgcPram->wLowest_write_temperature;
		}
		else if(Hcgc_Result.dwGc_ID == Gc_Slc_Write_Size)
		{
			Hcgc_Result.dwSLC_write_size = ptyHcgcPram->dwSLC_write_size;
		}
		else if(Hcgc_Result.dwGc_ID == Gc_Over_Temperature_Flag)
		{
			Hcgc_Result.bOver_temperature_flag = ptyHcgcPram->bOver_temperature_flag;
		}
	}

	return err;
}

unsigned int Hcgc_Switch(struct mmc_blk_data *md)
{
	int err;
	int i,j;
	unsigned char bBuff_Result[512] = {0};

	tyHcgcPram *ptyHcgcPram = (tyHcgcPram *)bBuff_Result;

	memset(bBuff_Result, 0, strlen(bBuff_Result));

	/**********Initializes the corresponding structure member***********/
	if(Hcgc_Result.dwGc_Cmd)
	{
		ptyHcgcPram->bFBAFun = 0x1;
		ptyHcgcPram->bIdleGCFun = 0x1;
		ptyHcgcPram->bHCGCState = 0x0;
	}
	else
	{
		ptyHcgcPram->bFBAFun = 0x1;
		ptyHcgcPram->bIdleGCFun = 0x0;
	}
	ptyHcgcPram->dwSLCFreeSpaceLimit = Hcgc_Result.dwSetSLCFreeSpaceLimit;
	ptyHcgcPram->dwPre_dirty_Size = Hcgc_Result.dwSetPre_dirty_Size;

	err = HcgcFun_WriteData(md, bBuff_Result);

	if(err && (Hcgc_Result.dwGc_ID != Chack_Hcgc))
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,err);

		printk(KERN_ERR"WriteData:\n");
		for(i = 0; i < 512; i++)
		{
			if(0 == (i % 16))
			{
				printk(KERN_ERR"\n");
			}
			printk(KERN_ERR"%d: %02X ", i, bBuff_Result[i]);
		}

		HcgcFun_ReadData(md,bBuff_Result);
		printk(KERN_ERR"ReadData:\n");
		for(j = 0; j < 512; j++)
		{
			if(0 == (j % 16))
			{
				printk(KERN_ERR"\n");
			}
			printk(KERN_ERR"%d : %02X ", j, bBuff_Result[j]);
		}
		printk(KERN_ERR"\n");
	}

	Hcgc_Result.dwCmd_Result = err;

	return err;
}

unsigned int HcgcProcessing(struct mmc_blk_data *md)
{
	int err = 0;

	// printk(KERN_ERR "Kobject: %s %s %d %d\n",__FILE__,__FUNCTION__,__LINE__, Hcgc_Result.dwGc_ID);

	switch (Hcgc_Result.dwGc_ID) 
	{
		case Gc_Set_Pre:
		case Gc_Set_Hcgc:
		case Gc_switch:
			err = Hcgc_Switch(md);
			break;
		case Gc_Get_Status:
		case Gc_Get_Wb:
		case Gc_Get_Hcgc:
		case Gc_Get_Pre:
		case Gc_Status:
		case Gc_Slc_status:
		case Gc_Slc_Free:
		case Gc_Usage_Rate:
		case Gc_Current_Temperature:
		case Gc_Highest_Write_Temperature:
		case Gc_Lowest_Write_Temperature:
		case Gc_Slc_Write_Size:
		case Gc_Over_Temperature_Flag:
			err = Hcgc_Get_Status(md);
			break;
		case Chack_Hcgc:
			if(Hcgc_flag == HCGC_CHECK_MODE)
			{
				err = Hcgc_Check(md);
				if (err)
				{
					Hcgc_flag = HCGC_OFF;
				}
				else
				{
					Hcgc_flag = HCGC_ON;
				}
			}
			break;
		default:
			break;
		
	}

	return err;
}

unsigned int Hcgc_Check(struct mmc_blk_data *md)
{
	unsigned int dwErr;

	dwErr = HcgcFun_Authorization_Check(md);
	if(dwErr == Authorization_fail)
	{
		dwErr = HcgcFun_Authorize(md);
		if(dwErr)
		{
			printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
			return dwErr;
		}
	}
	else if(dwErr)
	{
		printk(KERN_ERR "%s %s %d Errnum:%d\n",__FILE__,__FUNCTION__,__LINE__,dwErr);
		return dwErr;
	}

	return 0;
}

#endif
/*****************************Hcgc module end**********************************/   

/****************************Added Hcgc node*********************************/
static struct kobject *kob;

static struct mmc_blk_data *mmc_blk_get(struct gendisk *disk)
{
	struct mmc_blk_data *md;

	mutex_lock(&open_lock);
	md = disk->private_data;
	if (md && !kref_get_unless_zero(&md->kref))
		md = NULL;
	mutex_unlock(&open_lock);

	return md;
}

static inline int mmc_get_devidx(struct gendisk *disk)
{
	int devidx = disk->first_minor / perdev_minors;
	return devidx;
}

static void mmc_blk_kref_release(struct kref *ref)
{
	struct mmc_blk_data *md = container_of(ref, struct mmc_blk_data, kref);
	int devidx;

	devidx = mmc_get_devidx(md->disk);
	ida_simple_remove(&mmc_blk_ida, devidx);

	mutex_lock(&open_lock);
	md->disk->private_data = NULL;
	mutex_unlock(&open_lock);

	put_disk(md->disk);
	kfree(md);
}

static void mmc_blk_put(struct mmc_blk_data *md)
{
	kref_put(&md->kref, mmc_blk_kref_release);
}

// static int mmc_blk_check_blkdev(struct block_device *bdev)
// {
	// /*
	 // * The caller must have CAP_SYS_RAWIO, and must be calling this on the
	 // * whole block device, not on a partition.  This prevents overspray
	 // * between sibling partitions.
	 // */
	// if (!capable(CAP_SYS_RAWIO) || bdev_is_partition(bdev))
		// return -EPERM;
	// return 0;
// }


static ssize_t Hcgc_status_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret = 0, len = 0;
	int	old_area_type = 0, old_part_curr = 0, old_part_type = 0;
	struct mmc_blk_data *md;
	struct mmc_card *card = NULL;
	struct block_device *Hcgcbdev;
	dev_t dev = 0;

	Hcgc_Result.dwCmd_Result = 0;

	ret = lookup_bdev(mmcpath, &dev);
	if (ret)
	{
		printk(KERN_ERR "Kobject: could not find %s.\n", mmcpath);
		return -EIO;
	}

	// 获取块设备结构体
	Hcgcbdev = blkdev_get_by_dev(dev, FMODE_READ|FMODE_WRITE, NULL, NULL);
	if (IS_ERR(Hcgcbdev)) 
	{
		printk(KERN_ERR "Kobject: could not open %s.\n", "mmcblock0");
		return -EIO;
	}

	// ret = mmc_blk_check_blkdev(Hcgcbdev);
	// if (ret)
	// {
		// printk(KERN_ERR "Kobject: %s %s %d mmc_blk_check_blkdev fail\n",__FILE__,__FUNCTION__,__LINE__);
		// return ret;
	// }

	md = mmc_blk_get(Hcgcbdev->bd_disk);
	if (!md)
	{
		printk(KERN_ERR "Kobject: %s %s %d mmc_blk_get fail\n",__FILE__,__FUNCTION__,__LINE__);
		return -EINVAL;
	}

	if(attr == NULL)
	{
		goto check;
	}

	if (0 == memcmp(STR_GC_SLC_BOOST_STATUS, attr->attr.name, strlen(STR_GC_SLC_BOOST_STATUS)))
	{
		Hcgc_Result.dwGc_ID = Gc_Slc_status;
	}

	else if (0 == memcmp(STR_PRE_MANUAL_GC_SIZE, attr->attr.name, strlen(STR_PRE_MANUAL_GC_SIZE)))
	{
		Hcgc_Result.dwGc_ID = Gc_Set_Pre;
		if (buf != NULL)
		{
			// printk(KERN_ERR "Kobject: %s %s %d %s \n",__FILE__,__FUNCTION__,__LINE__, buf);
			len = strlen(buf);
			// printk(KERN_ERR "Kobject: %s %s %d %x \n",__FILE__,__FUNCTION__,__LINE__, buf[len-1]);
			if (buf[len - 1] == 0xa)
			{
				len = len - 1;
			}
			// 判断是不是全部为数字
			if (strspn(buf, "0123456789")==len)
			{
				Hcgc_Result.dwSetPre_dirty_Size = simple_strtoul(buf, NULL, 0);
				if (Hcgc_Result.dwSetPre_dirty_Size < 0)
				{
					printk(KERN_ERR "Kobject: pre_manual_gc_size simple_strtoul fail\n");
					printk(KERN_ERR "%s %s %d Errnum:0x%x\n",__FILE__,__FUNCTION__,__LINE__,buf[0]);
					goto err_md;
				}
				// printk(KERN_ERR "Kobject: %s %s %d %d \n",__FILE__,__FUNCTION__,__LINE__, Hcgc_Result.dwSetPre_dirty_Size);
			}
			else
			{
				printk(KERN_ERR "Not support cmd\n");
				printk(KERN_ERR "%s %s %d Errnum:0x%x\n",__FILE__,__FUNCTION__,__LINE__,buf[0]);
				goto err_md;
			}
		}
		else
		{
			Hcgc_Result.dwGc_ID = Gc_Get_Pre;
		}
	}

	else if (0 == memcmp(STR_MANUAL_GC_STATUS, attr->attr.name, strlen(STR_MANUAL_GC_STATUS)))
	{
		Hcgc_Result.dwGc_ID = Gc_Get_Status;
	}

	else if (0 == memcmp(STR_MANUAL_GC_SIZE, attr->attr.name, strlen(STR_MANUAL_GC_SIZE)))
	{
		Hcgc_Result.dwGc_ID = Gc_Set_Hcgc;
		if (buf != NULL)
		{
			len = strlen(buf);
			if (buf[len - 1] == 0xa)
			{
				len = len - 1;
			}
			// 判断是不是全部为数字
			if (strspn(buf, "0123456789") == len)
			{
				Hcgc_Result.dwSetSLCFreeSpaceLimit = simple_strtoul(buf, NULL, 0);
				if (Hcgc_Result.dwSetSLCFreeSpaceLimit < 0)
				{
					printk(KERN_ERR "Kobject: manual_gc_size simple_strtoul fail\n");
					printk(KERN_ERR "%s %s %d Errnum:%s\n",__FILE__,__FUNCTION__,__LINE__,buf);
					goto err_md;
				}
				// printk(KERN_ERR "Kobject: %s %s %d %d \n",__FILE__,__FUNCTION__,__LINE__, Hcgc_Result.dwSetSLCFreeSpaceLimit);
			}
			else
			{
				printk(KERN_ERR "Not support cmd\n");
				printk(KERN_ERR "%s %s %d Errnum:%s\n",__FILE__,__FUNCTION__,__LINE__,buf);
				goto err_md;
			}
		}
		else
		{
			Hcgc_Result.dwGc_ID = Gc_Get_Hcgc;
		}
		
	}
	else if (0 == memcmp(STR_SLC_FREE_SPACE, attr->attr.name, strlen(STR_SLC_FREE_SPACE)))
	{
		Hcgc_Result.dwGc_ID = Gc_Slc_Free;
	}

	else if (0 == memcmp(STR_WB_AVAIL_BUF, attr->attr.name, strlen(STR_WB_AVAIL_BUF)))
	{
		Hcgc_Result.dwGc_ID = Gc_Get_Wb;
	}

	else if (0 == memcmp(STR_MANUAL_GC, attr->attr.name, strlen(STR_MANUAL_GC)))
	{
		Hcgc_Result.dwGc_ID = Gc_switch;
		if (buf != NULL)
		{
			if (buf[0] >= '0' && buf[0] <= '1')
			{
				Hcgc_Result.dwGc_Cmd = buf[0] - '0';
			}
			else
			{
				printk(KERN_ERR "Not support cmd\n");
				printk(KERN_ERR "%s %s %d Errnum:0x%x\n",__FILE__,__FUNCTION__,__LINE__,buf[0]);
				goto err_md;
			}
		}
		else
		{
			Hcgc_Result.dwGc_ID = Gc_Get_Status;
		}
	}

	else if (0 == memcmp(STR_GC_STATUS, attr->attr.name, strlen(STR_GC_STATUS)))
	{
		Hcgc_Result.dwGc_ID = Gc_Status;
	}

	else if (0 == memcmp(STR_CURRENT_TEMPERATURE, attr->attr.name, strlen(STR_CURRENT_TEMPERATURE)))
	{
		Hcgc_Result.dwGc_ID = Gc_Current_Temperature;
	}
	
	else if (0 == memcmp(STR_HIGHEST_WRITE_TEMPERATURE, attr->attr.name, strlen(STR_HIGHEST_WRITE_TEMPERATURE)))
	{
		Hcgc_Result.dwGc_ID = Gc_Highest_Write_Temperature;
	}
	
	else if (0 == memcmp(STR_LOWEST_WRITE_TEMPERATURE, attr->attr.name, strlen(STR_LOWEST_WRITE_TEMPERATURE)))
	{
		Hcgc_Result.dwGc_ID = Gc_Lowest_Write_Temperature;
	}
	
	else if (0 == memcmp(STR_SLC_WRITE_SIZE, attr->attr.name, strlen(STR_SLC_WRITE_SIZE)))
	{
		Hcgc_Result.dwGc_ID = Gc_Slc_Write_Size;
	}

	else if (0 == memcmp(STR_OVER_TEMPERATURE_FLAG, attr->attr.name, strlen(STR_OVER_TEMPERATURE_FLAG)))
	{
		Hcgc_Result.dwGc_ID = Gc_Over_Temperature_Flag;
	}

check:
	card = md->queue.card;

	if (IS_ERR(card)) {
		ret = PTR_ERR(card);
		goto err_md;
	}

	mmc_get_card(card, NULL);

	// if (mmc_card_doing_bkops(card)) 
	// {
	// 	printk(KERN_ERR "Kobject: 1mmc_card_doing_bkops %s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	// 	goto err_card;
	// 	ret = mmc_stop_bkops(card);
	// 	if (ret) 
	// 	{
	// 		dev_err(mmc_dev(card->host),
	// 			"%s: stop_bkops failed %d\n", __func__, ret);
	// 		goto err_card;
	// 	}
	// }

	if ((md->area_type & MMC_BLK_DATA_AREA_RPMB) || (MMC_PART_RPMB == md->part_curr) || (MMC_PART_RPMB == md->part_type))
	{
		// printk(KERN_ERR "Kobject: UESR %s %s %d %d %d %d\n",__FILE__,__FUNCTION__,__LINE__, md->area_type, md->part_curr, md->part_type);
		old_area_type = md->area_type;
		old_part_curr = md->part_curr;
		old_part_type = md->part_type;
		md->part_type = MMC_PART_USER;
		ret = mmc_blk_part_switch(card, md->part_type);
		if (ret)
		{
			printk(KERN_ERR "Kobject: UESR %s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
			goto err_card;
		}
	}

	mmc_put_card(card, NULL);

	ret = HcgcProcessing(md);
	if (ret)
	{
		printk(KERN_ERR "Kobject: HcgcProcessing %s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
		goto err_md;
	}

	mmc_get_card(card, NULL);

	// if (mmc_card_doing_bkops(card)) 
	// {
	// 	printk(KERN_ERR "Kobject: 2mmc_card_doing_bkops %s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
	// 	goto err_card;
	// 	ret = mmc_stop_bkops(card);
	// 	if (ret) 
	// 	{
	// 		dev_err(mmc_dev(card->host),
	// 			"%s: stop_bkops failed %d\n", __func__, ret);
	// 		goto err_card;
	// 	}
	// }

	if (old_area_type)
	{ 
		md->area_type = old_area_type;
		md->part_curr = old_part_curr;
		md->part_type = old_part_type;
		ret = mmc_blk_part_switch(card, md->part_type);
		if (ret)
		{
			printk(KERN_ERR "Kobject: RBMB %s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
			goto err_card;
		}
	}
err_card:
	mmc_put_card(card, NULL);
err_md:
	mmc_blk_put(md);
    
    return count;
}

static ssize_t Hcgc_status_show(struct kobject* kobjs,struct kobj_attribute *attr,char *buf)
{
	int err = 0;
	// printk(KERN_ERR "Kobject: %s %s %d %s\n",__FILE__,__FUNCTION__,__LINE__, attr->attr.name);

	if (Hcgc_Result.dwCmd_Result)
	{
		return sprintf(buf,"error:%x\n",Hcgc_Result.dwCmd_Result);
	}

	if (0 == memcmp(STR_GC_SLC_BOOST_STATUS, attr->attr.name, strlen(STR_GC_SLC_BOOST_STATUS)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%x\n",Hcgc_Result.dwGCSlcBoostStatus);
	}
	else if (0 == memcmp(STR_PRE_MANUAL_GC_SIZE, attr->attr.name, strlen(STR_PRE_MANUAL_GC_SIZE)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.dwGetPre_dirty_Size);
	}
	else if (0 == memcmp(STR_MANUAL_GC_STATUS, attr->attr.name, strlen(STR_MANUAL_GC_STATUS)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.bHCGCState);
	}
	else if (0 == memcmp(STR_MANUAL_GC_SIZE, attr->attr.name, strlen(STR_MANUAL_GC_SIZE)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.dwGetSLCFreeSpaceLimit);
	}
	else if (0 == memcmp(STR_SLC_FREE_SPACE, attr->attr.name, strlen(STR_SLC_FREE_SPACE)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.dwSlcFreeSpace);
	}
	else if (0 == memcmp(STR_WB_AVAIL_BUF, attr->attr.name, strlen(STR_WB_AVAIL_BUF)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.bSLCFreeSpaceLimit);
	}
	else if (0 == memcmp(STR_MANUAL_GC, attr->attr.name, strlen(STR_MANUAL_GC)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.bHCGCState);
	}
	else if (0 == memcmp(STR_GC_STATUS, attr->attr.name, strlen(STR_GC_STATUS)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%x\n",Hcgc_Result.dwGetStatus);
	}
	else if (0 == memcmp(STR_CURRENT_TEMPERATURE, attr->attr.name, strlen(STR_CURRENT_TEMPERATURE)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.wCurrent_temperature);
	}
	else if (0 == memcmp(STR_HIGHEST_WRITE_TEMPERATURE, attr->attr.name, strlen(STR_HIGHEST_WRITE_TEMPERATURE)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.wHighest_write_temperature);
	}
	else if (0 == memcmp(STR_LOWEST_WRITE_TEMPERATURE, attr->attr.name, strlen(STR_LOWEST_WRITE_TEMPERATURE)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.wLowest_write_temperature);
	}
	else if (0 == memcmp(STR_SLC_WRITE_SIZE, attr->attr.name, strlen(STR_SLC_WRITE_SIZE)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%d\n",Hcgc_Result.dwSLC_write_size);
	}
	else if (0 == memcmp(STR_OVER_TEMPERATURE_FLAG, attr->attr.name, strlen(STR_OVER_TEMPERATURE_FLAG)))
	{
		err = Hcgc_status_store(kobjs, attr, NULL, 0);
		return sprintf(buf,"%x\n",Hcgc_Result.bOver_temperature_flag);
	}

    return sprintf(buf,"%08x%08x%08x\n",Hcgc_Result.dwGc_Cmd, Hcgc_Result.dwGc_Result, Hcgc_Result.dwCmd_Result);
}

// static struct kobj_attribute status_attr = __ATTR_RO(Hcgc);
static struct kobj_attribute Hcgc_manual_gc = __ATTR(manual_gc,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute Hcgc_pre_manual_gc_size = __ATTR(pre_manual_gc_size,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute Hcgc_manual_gc_size = __ATTR(manual_gc_size,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute Hcgc_manual_gc_status = __ATTR(manual_gc_status,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute Hcgc_wb_avail_buf = __ATTR(wb_avail_buf,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute Hcgc_get_gc_status = __ATTR(gc_status,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute gc_slc_boost_status = __ATTR(gc_slc_boost_status,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute slc_free_space = __ATTR(slc_free_space,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute current_temperature = __ATTR(current_temperature,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute highest_write_temperature = __ATTR(highest_write_temperature,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute lowest_write_temperature = __ATTR(lowest_write_temperature,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute slc_write_size = __ATTR(slc_write_size,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.
static struct kobj_attribute over_temperature_flag = __ATTR(over_temperature_flag,0660,Hcgc_status_show,Hcgc_status_store);  //Doesn't support 0666 in new version.


static struct attribute *Hcgc_attrs[] = {
    // &status_attr.attr,
    &Hcgc_manual_gc.attr,
	&Hcgc_pre_manual_gc_size.attr,
	&Hcgc_manual_gc_size.attr,
	&Hcgc_manual_gc_status.attr,
	&Hcgc_wb_avail_buf.attr,
	&Hcgc_get_gc_status.attr,
	&gc_slc_boost_status.attr,
	&slc_free_space.attr,
	&current_temperature.attr,
	&highest_write_temperature.attr,
	&lowest_write_temperature.attr,
	&slc_write_size.attr,
	&over_temperature_flag.attr,
    NULL,
};

static struct attribute_group attr_g = {
    .name = "Hcgc_Drive",
    .attrs = Hcgc_attrs,
};


int create_kobject(void)
{
    kob = kobject_create_and_add("Hcgc_node",kernel_kobj->parent);

    if(IS_ERR(kob))
    {
        printk(KERN_ERR "alloc failed!!\n");
        return -ENOMEM;
    }

    return 0;
}


struct delayed_work mhcgc;  

struct workqueue_struct *hcgc;
 
void delay_work_func(struct work_struct *work)  
{  
	int ret;

	Hcgc_Result.dwGc_ID = Chack_Hcgc;

	Hcgc_status_store(NULL, NULL, NULL, 0);

	if (HCGC_ON == Hcgc_flag)
	{
		ret = create_kobject();
		if (ret)
		{
			printk(KERN_ERR "Failed to create_kobject\n");
		}
		
		ret = sysfs_create_group(kob, &attr_g);
		if (ret)
		{
			printk(KERN_ERR "Failed to create directory\n");
		}
	}
	else
	{
		printk(KERN_INFO "Kobject: The eMMC firmware does not support HCGC or the key is incorrect\n");
		printk(KERN_INFO "Kobject: Uninstall the HCGC driver node!\n");
	}
	cancel_delayed_work(&mhcgc); 
	printk(KERN_INFO "Kobject: check the hcgc at the end!\n");
}

static int check_hcgc(void)  
{  
    int ret; 
	printk(KERN_INFO "Kobject: the delay queue init!\n");
    hcgc = create_workqueue("hcgc workqueue");  
    if (!hcgc) {  
        printk(KERN_ERR "Create workqueue failed!\n");  
        return 1;     
    }  
    INIT_DELAYED_WORK(&mhcgc, delay_work_func);  
      
    ret = queue_delayed_work(hcgc, &mhcgc, msecs_to_jiffies(HCGC_CHACK_DELAY));  

    return 0;  
}  

static int __init sysfs_ctrl_init(void)
{
    int ret = 0;
    printk(KERN_INFO "Kobject test!\n");

	ret = check_hcgc();

    return ret;
}

static void __exit sysfs_ctrl_exit(void)
{ 
	flush_workqueue(hcgc);
    destroy_workqueue(hcgc);
    kobject_put(kob);
    printk(KERN_ERR "Kobject: Goodbye!\n");
}

module_init(sysfs_ctrl_init);
module_exit(sysfs_ctrl_exit);

MODULE_LICENSE("GPL");                   
MODULE_DESCRIPTION("Hcgc Drive");  
MODULE_VERSION("1.1");  
