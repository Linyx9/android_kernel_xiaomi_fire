#ifndef __DRE_MODE_H__
#define __DRE_MODE_H__

#include <linux/types.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>

struct dre_mode_init {	
	/*int hbm_gpio;
	int hbm;*/
	unsigned int dre_en;
	unsigned char dre_en_buf[16];
	/*unsigned char hbm_buf[16];*/
	unsigned int suspend_flag;
	struct notifier_block fb_notifier;
};

/*int (*hbm_set_backlight_value)(int level)=NULL;*/

#endif /*__DRE_MODE_H__*/
