/*****************************************************************************
 *
 * Filename:
 * ---------
 *     gc05a2mipi_Sensor.h
 *
 * Project:
 * --------
 *     ALPS
 *
 * Description:
 * ------------
 *     CMOS sensor header file
 *
 ****************************************************************************/
#ifndef __GC05A2MIPI_SENSOR_H__
#define __GC05A2MIPI_SENSOR_H__


#define MULTI_WRITE             1
#if MULTI_WRITE
#define I2C_BUFFER_LEN  765 /* Max is 255, each 3 bytes */
#else
#define I2C_BUFFER_LEN  3
#endif

/* SENSOR MIRROR FLIP INFO */
#define GC05A2_MIRROR_NORMAL    0
#define GC05A2_MIRROR_H         0
#define GC05A2_MIRROR_V         0
#define GC05A2_MIRROR_HV        1

#if GC05A2_MIRROR_NORMAL
#define GC05A2_MIRROR	        0x00
#elif GC05A2_MIRROR_H
#define GC05A2_MIRROR	        0x01
#elif GC05A2_MIRROR_V
#define GC05A2_MIRROR	        0x02
#elif GC05A2_MIRROR_HV
#define GC05A2_MIRROR	        0x03
#else
#define GC05A2_MIRROR	        0x00
#endif

#define MODULE_GROUP_FLAG 0x2000

#define MODULE_GROUP1_INFO_FLAG 0x2008
#define MODULE_GROUP1_CHECKSUM 0x2050
#define MODULE_GROUP2_INFO_FLAG 0x5CE0
#define MODULE_GROUP2_CHECKSUM 0x5D28
#define MODULE_GROUP3_INFO_FLAG 0x99B8
#define MODULE_GROUP3_CHECKSUM 0x9A00
#define MODULE_INFO_LENGTH 9

#define AWB_GROUP1_INFO_FLAG 0x2058
#define AWB_GROUP1_CHECKSUM 0x2138
#define AWB_GROUP2_INFO_FLAG 0x5D30
#define AWB_GROUP2_CHECKSUM 0x5E10
#define AWB_GROUP3_INFO_FLAG 0x9A08
#define AWB_GROUP3_CHECKSUM 0x9AE0
#define AWB_INFO_LENGTH 28

#define LSC_GROUP1_INFO_FLAG 0x2140
#define LSC_GROUP1_CHECKSUM 0x5BA0
#define LSC_GROUP2_INFO_FLAG 0x5E18
#define LSC_GROUP2_CHECKSUM 0x9878
#define LSC_GROUP3_INFO_FLAG 0x9AF0
#define LSC_GROUP3_CHECKSUM 0xD548
#define LSC_INFO_LENGTH 1868

// #define GROUP_LENGTH 1900
// #define MODULE_LENGTH 19

#define SENSOR_BASE_GAIN           0x400
#define SENSOR_MAX_GAIN            (16 * SENSOR_BASE_GAIN)

struct gc05a2_otp_t {
	kal_uint8  module_flag;
	kal_uint8  module_param[9];
    kal_uint8  module_checksum;
	kal_uint8  awb_param[28];
    kal_uint8  awb_checksum;
	kal_uint8  lsc_param[1868];
	kal_uint8  lsc_checksum;
};

enum{
	IMGSENSOR_MODE_INIT,
	IMGSENSOR_MODE_PREVIEW,
	IMGSENSOR_MODE_CAPTURE,
	IMGSENSOR_MODE_VIDEO,
	IMGSENSOR_MODE_HIGH_SPEED_VIDEO,
	IMGSENSOR_MODE_SLIM_VIDEO,
};

struct imgsensor_mode_struct {
	kal_uint32 pclk;
	kal_uint32 linelength;
	kal_uint32 framelength;
	kal_uint8 startx;
	kal_uint8 starty;
	kal_uint16 grabwindow_width;
	kal_uint16 grabwindow_height;
	kal_uint32 mipi_pixel_rate;
	kal_uint8 mipi_data_lp2hs_settle_dc;
	kal_uint16 max_framerate;
};

/* SENSOR PRIVATE STRUCT FOR VARIABLES */
struct imgsensor_struct {
	kal_uint8 mirror;
	kal_uint8 sensor_mode;
	kal_uint32 shutter;
	kal_uint16 gain;
	kal_uint32 pclk;
	kal_uint32 frame_length;
	kal_uint32 line_length;
	kal_uint32 min_frame_length;
	kal_uint16 dummy_pixel;
	kal_uint16 dummy_line;
	kal_uint16 current_fps;
	kal_bool   autoflicker_en;
	kal_bool   test_pattern;
	enum MSDK_SCENARIO_ID_ENUM current_scenario_id;
	kal_uint8  ihdr_en;
	kal_uint8 i2c_write_id;
};

/* SENSOR PRIVATE STRUCT FOR CONSTANT */
struct imgsensor_info_struct {
	kal_uint32 sensor_id;
	kal_uint32 checksum_value;
	struct imgsensor_mode_struct pre;
	struct imgsensor_mode_struct cap;

	struct imgsensor_mode_struct normal_video;
	struct imgsensor_mode_struct hs_video;
	struct imgsensor_mode_struct slim_video;
	kal_uint8  ae_shut_delay_frame;
	kal_uint8  ae_sensor_gain_delay_frame;
	kal_uint8  ae_ispGain_delay_frame;
	kal_uint8  ihdr_support;
	kal_uint8  ihdr_le_firstline;
	kal_uint8  sensor_mode_num;
	kal_uint8  cap_delay_frame;
	kal_uint8  pre_delay_frame;
	kal_uint8  video_delay_frame;
	kal_uint8  hs_video_delay_frame;
	kal_uint8  slim_video_delay_frame;
	kal_uint8  margin;
	kal_uint32 min_shutter;
	kal_uint32 max_frame_length;
	kal_uint8  isp_driving_current;
	kal_uint8  sensor_interface_type;
	kal_uint8  mipi_sensor_type;
	kal_uint8  mipi_settle_delay_mode;
	kal_uint8  sensor_output_dataformat;
	kal_uint8  mclk;
	kal_uint8  mipi_lane_num;
	kal_uint8  i2c_addr_table[5];
	kal_uint32 i2c_speed;
};

extern int iReadRegI2C(u8 *a_pSendData, u16 a_sizeSendData, u8 *a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
extern int iWriteRegI2C(u8 *a_pSendData, u16 a_sizeSendData, u16 i2cId);
extern int iWriteRegI2CTiming(u8 *a_pSendData, u16 a_sizeSendData, u16 i2cId, u16 timing);
extern int iBurstWriteReg_multi(u8 *pData, u32 bytes, u16 i2cId, u16 transfer_length, u16 timing);

#endif
