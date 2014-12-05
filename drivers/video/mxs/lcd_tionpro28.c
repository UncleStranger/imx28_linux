/*
 *   LCD panel driver
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/notifier.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>

#include <mach/device.h>
#include <mach/lcdif.h>
#include <mach/pinctrl.h>
#include <mach/regs-pwm.h>
#include <mach/system.h>

struct dot_clk {
	unsigned int dac_power_en;	/* power for onboard DAC IC */
	unsigned int lcd_power_en;	/* power for lcd */
	unsigned int cycle_time_ns;
	unsigned int h_active;
	unsigned int h_pulse_width;
	unsigned int hf_porch;
	unsigned int hb_porch;
	unsigned int v_active;
	unsigned int v_pulse_width;
	unsigned int vf_porch;
	unsigned int vb_porch;
};

static const struct dot_clk dot_clks[] = {
/* 0 */	{	/* VGA */
		1, 0, 40,
		640, 96, 16, 48,
		480, 2, 13, 31,
	},
/* 1 */	{	/* TFT 5.7'' 640x480 */
		1, 1, 40,
		640, 10, 12, 138,
		480, 10, 14, 21,
	},
/* 2 */	{	/* TFT 3.5'' 320x240 */
		1, 1, 116,	/* 116 ns for 72 Hz */
		320, 32, 32, 96,
		240, 3, 3, 4,
	}
};

static struct mxs_platform_bl_data bl_data;
static struct clk *lcd_clk;

static const unsigned int gpio_vga_dac_pwr_ena = MXS_PIN_TO_GPIO(MXS_PIN_ENCODE(1, 25));
static const unsigned int gpio_lcd_pwr_ena = MXS_PIN_TO_GPIO(MXS_PIN_ENCODE(3, 30));

static const unsigned int gpio_pwm = MXS_PIN_TO_GPIO(MXS_PIN_ENCODE(3, 28));
static const int backlight_pwm_num = 3;

static int dac_power_en = 0;
static int lcd_power_en = 0;

static void pwm_set(int pwm_num, int active, int period);

static const struct dot_clk *get_video_by_tag(void)
{
	const struct dot_clk *dot_clk;
	const char tag_str[] = "tag=";
	char *opt, *pos;

	dot_clk = dot_clks + 1; //default setting

	/* Select display type from cmdline "video=mxs-fb:tag=vga" */
	if (fb_get_options("mxs-fb", &opt) == 0
	&& opt != NULL && *opt != '\0') {
		pos = strstr(opt, tag_str);
		if (pos != NULL) {
			pos += strlen(tag_str);

			if (NULL != strstr(pos, "vga")) {
				dot_clk = dot_clks + 0;
			} else if (NULL != strstr(pos, "3.5")) {
				dot_clk = dot_clks + 2;
			} else if (NULL != strstr(pos, "5.7")) {
				dot_clk = dot_clks + 1;
			}
		}
	}

	return dot_clk;
}

static int init_panel(struct device *dev, dma_addr_t phys, int memsize,
		      struct mxs_platform_fb_entry *pentry)
{
	int ret = 0;
	const struct dot_clk *dot_clk;
	unsigned int dotclk_h_wait_cnt, dotclk_h_period, dotclk_v_wait_cnt, dotclk_v_period;

	gpio_request(gpio_lcd_pwr_ena, "lcd-pwr-ena");
	gpio_direction_output(gpio_lcd_pwr_ena, 0);

	gpio_request(gpio_vga_dac_pwr_ena, "vga-dac-pwr-ena");
	gpio_direction_output(gpio_vga_dac_pwr_ena, 0);

	dot_clk = get_video_by_tag();
	if (!dot_clk) {
		dev_warn(dev, "no panel tag specified\n");

		return -ENODEV;
	}

	dac_power_en = dot_clk->dac_power_en;
	lcd_power_en = dot_clk->lcd_power_en;
	

	lcd_clk = clk_get(dev, "dis_lcdif");
	if (IS_ERR(lcd_clk)) {
		ret = PTR_ERR(lcd_clk);
		goto out;
	}
	ret = clk_enable(lcd_clk);
	if (ret) {
		clk_put(lcd_clk);
		goto out;
	}

	ret = clk_set_rate(lcd_clk, 1000000 / dot_clk->cycle_time_ns);	/* kHz */
	if (ret) {
		clk_disable(lcd_clk);
		clk_put(lcd_clk);
		goto out;
	}

	/*
	 * Make sure we do a high-to-low transition to reset the panel.
	 * First make it low for 100 msec, hi for 10 msec, low for 10 msec,
	 * then hi.
	 */
	__raw_writel(BM_LCDIF_CTRL1_RESET, REGS_LCDIF_BASE + HW_LCDIF_CTRL1_CLR);	/* low */
	mdelay(100);
	__raw_writel(BM_LCDIF_CTRL1_RESET, REGS_LCDIF_BASE + HW_LCDIF_CTRL1_SET);	/* high */
	mdelay(10);
	__raw_writel(BM_LCDIF_CTRL1_RESET, REGS_LCDIF_BASE + HW_LCDIF_CTRL1_CLR);	/* low */

	/* For the Samsung, Reset must be held low at least 30 uSec
	 * Therefore, we'll hold it low for about 10 mSec just to be sure.
	 * Then we'll wait 1 mSec afterwards.
	 */
	mdelay(10);
	__raw_writel(BM_LCDIF_CTRL1_RESET, REGS_LCDIF_BASE + HW_LCDIF_CTRL1_SET);	/* high */
	mdelay(1);

	dotclk_h_wait_cnt = dot_clk->h_pulse_width + dot_clk->hb_porch;
	dotclk_h_period = dotclk_h_wait_cnt + dot_clk->hf_porch + dot_clk->h_active;
	dotclk_v_wait_cnt = dot_clk->v_pulse_width + dot_clk->vb_porch;
	dotclk_v_period = dotclk_v_wait_cnt + dot_clk->vf_porch + dot_clk->v_active;

	setup_dotclk_panel(dot_clk->v_pulse_width, dotclk_v_period,
			dotclk_v_wait_cnt, dot_clk->v_active,
			dot_clk->h_pulse_width, dotclk_h_period,
			dotclk_h_wait_cnt, dot_clk->h_active, 0);

#warning TODO
	/* VSYNC & HSYNC polarity */
	/*__raw_writel(BM_LCDIF_VDCTRL0_VSYNC_POL,
		     REGS_LCDIF_BASE + HW_LCDIF_VDCTRL0_SET);	
	__raw_writel(BM_LCDIF_VDCTRL0_HSYNC_POL,
		     REGS_LCDIF_BASE + HW_LCDIF_VDCTRL0_SET);*/

	ret = mxs_lcdif_dma_init(dev, phys, memsize);
	if (ret)
		goto out;

	mxs_lcd_set_bl_pdata(pentry->bl_data);
	mxs_lcdif_notify_clients(MXS_LCDIF_PANEL_INIT, pentry);

	return 0;

out:
	return ret;
}

static void release_panel(struct device *dev,
			  struct mxs_platform_fb_entry *pentry)
{
	gpio_set_value(gpio_lcd_pwr_ena, 0);
	gpio_free(gpio_lcd_pwr_ena);

	gpio_set_value(gpio_vga_dac_pwr_ena, 0);
	gpio_free(gpio_vga_dac_pwr_ena);

	mxs_lcdif_notify_clients(MXS_LCDIF_PANEL_RELEASE, pentry);
	release_dotclk_panel();
	mxs_lcdif_dma_release();
	clk_disable(lcd_clk);
	clk_put(lcd_clk);
}

static int blank_panel(int blank)
{
	int ret = 0, count;

	switch (blank) {
	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		if (dac_power_en) {
			gpio_set_value(gpio_vga_dac_pwr_ena, 0);
		}
		if (lcd_power_en){
			gpio_set_value(gpio_lcd_pwr_ena, 0);
		}

		__raw_writel(BM_LCDIF_CTRL_BYPASS_COUNT,
			     REGS_LCDIF_BASE + HW_LCDIF_CTRL_CLR);
		for (count = 10000; count; count--) {
			if (__raw_readl(REGS_LCDIF_BASE + HW_LCDIF_STAT) &
			    BM_LCDIF_STAT_TXFIFO_EMPTY)
				break;
			udelay(1);
		}
		break;

	case FB_BLANK_UNBLANK:
		__raw_writel(BM_LCDIF_CTRL_BYPASS_COUNT,
			     REGS_LCDIF_BASE + HW_LCDIF_CTRL_SET);
		if (dac_power_en) {
			gpio_set_value(gpio_vga_dac_pwr_ena, 1);
		}
		if (lcd_power_en){
			gpio_set_value(gpio_lcd_pwr_ena, 1);
		}
		break;

	default:
		ret = -EINVAL;
	}
	return ret;
}

static struct mxs_platform_fb_entry fb_entry = {
	.name = "lcd_tion-pro28",
	.lcd_type = MXS_LCD_PANEL_DOTCLK,
	.init_panel = init_panel,
	.release_panel = release_panel,
	.blank_panel = blank_panel,
	.run_panel = mxs_lcdif_run,
	.stop_panel = mxs_lcdif_stop,
	.pan_display = mxs_lcdif_pan_display,
	.bl_data = &bl_data,
};

static struct clk *pwm_clk;

static int init_bl(struct mxs_platform_bl_data *data)
{
	int ret = 0;

	pwm_clk = clk_get(NULL, "pwm");
	if (IS_ERR(pwm_clk)) {
		ret = PTR_ERR(pwm_clk);
		return ret;
	}
	clk_enable(pwm_clk);
	mxs_reset_block(REGS_PWM_BASE, 1);


	__raw_writel(1<<backlight_pwm_num, REGS_PWM_BASE + HW_PWM_CTRL_SET);

	return 0;
}

static void free_bl(struct mxs_platform_bl_data *data)
{
	pwm_set(backlight_pwm_num, 0, 0);	/* Disable backlight */
	__raw_writel(1<<backlight_pwm_num, REGS_PWM_BASE + HW_PWM_CTRL_CLR);

	clk_disable(pwm_clk);
	clk_put(pwm_clk);
}

static void pwm_set(int pwm_num, int active, int period)
{
	/* Firslty set PWM pulse width, this _must_ be done firslty */
	__raw_writel(BF_PWM_ACTIVEn_ACTIVE(0)
			| BF_PWM_ACTIVEn_INACTIVE(active),
			REGS_PWM_BASE + HW_PWM_ACTIVEn(pwm_num));

	/* Secondly */
	__raw_writel(BF_PWM_PERIODn_CDIV(6) |		/* 5 -- divide 60kHz by 64 */
		     BF_PWM_PERIODn_INACTIVE_STATE(2) |	/* Inactive is low */
		     BF_PWM_PERIODn_ACTIVE_STATE(3) |	/* Active is high */
		     BF_PWM_PERIODn_PERIOD(period),
		     REGS_PWM_BASE + HW_PWM_PERIODn(pwm_num));
}

static int set_bl_intensity(struct mxs_platform_bl_data *pdata,
			    struct backlight_device *bd, int suspended)
{
	int bright;

	bright = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK
	|| bd->props.fb_blank != FB_BLANK_UNBLANK
	|| suspended) {
		bright = 0;
	}

	pwm_set(backlight_pwm_num, bright, pdata->bl_max_intensity);

	return 0;
}

static struct mxs_platform_bl_data bl_data = {
	.bl_max_intensity = 100,
	.bl_default_intensity = 50,
	.bl_cons_intensity = 50,
	.init_bl = init_bl,
	.free_bl = free_bl,
	.set_bl_intensity = set_bl_intensity,
};

static int __init register_devices(void)
{
	struct platform_device *pdev;
	const struct dot_clk *dot_clk;

	dot_clk = get_video_by_tag();
	if (!dot_clk) {
		/* Use first entry for something to register */
		dot_clk = dot_clks;
	}

	pdev = mxs_get_device("mxs-fb", 0);
	if (pdev == NULL || IS_ERR(pdev))
		return -ENODEV;

	fb_entry.x_res = dot_clk->v_active;
	fb_entry.y_res = dot_clk->h_active;
	fb_entry.bpp = 16;

	mxs_lcd_register_entry(&fb_entry, pdev->dev.platform_data);

	return 0;
}

subsys_initcall(register_devices);
