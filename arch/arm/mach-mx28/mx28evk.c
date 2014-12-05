/*
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/hardware.h>
#include <mach/device.h>
#include <mach/pinctrl.h>

#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

#include <linux/usb/g_hid.h>

#include "device.h"
#include "mx28evk.h"



/* hid descriptor for a keyboard */
static struct hidg_func_descriptor my_hid_data = {
	.subclass       = 0, /* No subclass */
	.protocol       = 1, /* Keyboard */
	.report_length      = 8,
	.report_desc_length = 63,
	.report_desc        = {
    0x05, 0x01, /* USAGE_PAGE (Generic Desktop)           */
    0x09, 0x06, /* USAGE (Keyboard)                       */
    0xa1, 0x01, /* COLLECTION (Application)               */
    0x05, 0x07, /*   USAGE_PAGE (Keyboard)                */
    0x19, 0xe0, /*   USAGE_MINIMUM (Keyboard LeftControl) */
    0x29, 0xe7, /*   USAGE_MAXIMUM (Keyboard Right GUI)   */
    0x15, 0x00, /*   LOGICAL_MINIMUM (0)                  */
    0x25, 0x01, /*   LOGICAL_MAXIMUM (1)                  */
    0x75, 0x01, /*   REPORT_SIZE (1)                      */
    0x95, 0x08, /*   REPORT_COUNT (8)                     */
    0x81, 0x02, /*   INPUT (Data,Var,Abs)                 */
    0x95, 0x01, /*   REPORT_COUNT (1)                     */
    0x75, 0x08, /*   REPORT_SIZE (8)                      */
    0x81, 0x03, /*   INPUT (Cnst,Var,Abs)                 */
    0x95, 0x05, /*   REPORT_COUNT (5)                     */
    0x75, 0x01, /*   REPORT_SIZE (1)                      */
    0x05, 0x08, /*   USAGE_PAGE (LEDs)                    */
    0x19, 0x01, /*   USAGE_MINIMUM (Num Lock)             */
    0x29, 0x05, /*   USAGE_MAXIMUM (Kana)                 */
    0x91, 0x02, /*   OUTPUT (Data,Var,Abs)                */
    0x95, 0x01, /*   REPORT_COUNT (1)                     */
    0x75, 0x03, /*   REPORT_SIZE (3)                      */
    0x91, 0x03, /*   OUTPUT (Cnst,Var,Abs)                */
    0x95, 0x06, /*   REPORT_COUNT (6)                     */
    0x75, 0x08, /*   REPORT_SIZE (8)                      */
    0x15, 0x00, /*   LOGICAL_MINIMUM (0)                  */
    0x25, 0x65, /*   LOGICAL_MAXIMUM (101)                */
    0x05, 0x07, /*   USAGE_PAGE (Keyboard)                */
    0x19, 0x00, /*   USAGE_MINIMUM (Reserved)             */
    0x29, 0x65, /*   USAGE_MAXIMUM (Keyboard Application) */
    0x81, 0x00, /*   INPUT (Data,Ary,Abs)                 */
    0xc0        /* END_COLLECTION                         */
	}
};

static struct platform_device my_hid = {
	.name           = "hidg",
	.id         = 0,
	.num_resources      = 0,
	.resource       = 0,
	.dev = {
	.platform_data  = &my_hid_data,
	}, 
};

static void __init hidg_device_init(void)
{
	int ret;

	ret = platform_device_register(&my_hid);

	if (ret)
    	printk("HID Gadget registration failed\n");

} 

static struct i2c_board_info __initdata mxs_i2c_device[] = {	
	{ I2C_BOARD_INFO("sgtl5000-i2c", 0xa), .flags = I2C_M_TEN },
	{ I2C_BOARD_INFO("m41t00", 0x68), }
	
};

static void __init i2c_device_init(void)
{
	i2c_register_board_info(0, mxs_i2c_device, ARRAY_SIZE(mxs_i2c_device));	
}
#if defined(CONFIG_MTD_M25P80) || defined(CONFIG_MTD_M25P80_MODULE)
static struct flash_platform_data mx28_spi_flash_data = {
	.name = "m25p80",
	.type = "w25x80",
};
#endif

static struct spi_board_info spi_board_info[] __initdata = {
#if defined(CONFIG_MTD_M25P80) || defined(CONFIG_MTD_M25P80_MODULE)
	{
		/* the modalias must be the same as spi device driver name */
		.modalias = "m25p80", /* Name of spi_driver for this device */
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 1, /* Framework bus number */
		.chip_select = 0, /* Framework chip select. */
		.platform_data = &mx28_spi_flash_data,
	},
#endif
};

static void spi_device_init(void)
{
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
}

static void __init fixup_board(struct machine_desc *desc, struct tag *tags,
			       char **cmdline, struct meminfo *mi)
{
	mx28_set_input_clk(24000000, 24000000, 32000, 50000000);
}

#if defined(CONFIG_LEDS_MXS) || defined(CONFIG_LEDS_MXS_MODULE)
static struct mxs_pwm_led  mx28evk_led_pwm[1] = {
	[0] = {
		.name = "led-pwm4",
		.pwm = 4,
		},
};

struct mxs_pwm_leds_plat_data mx28evk_led_data = {
	.num = ARRAY_SIZE(mx28evk_led_pwm),
	.leds = mx28evk_led_pwm,
};

static struct resource mx28evk_led_res = {
	.flags = IORESOURCE_MEM,
	.start = PWM_PHYS_ADDR,
	.end   = PWM_PHYS_ADDR + 0x3FFF,
};

static void __init mx28evk_init_leds(void)
{
	struct platform_device *pdev;

	pdev = mxs_get_device("mxs-leds", 0);
	if (pdev == NULL || IS_ERR(pdev))
		return;

	pdev->resource = &mx28evk_led_res;
	pdev->num_resources = 1;
	pdev->dev.platform_data = &mx28evk_led_data;
	mxs_add_device(pdev, 3);
}
#else

static struct gpio_led tion_pro28_leds[] = {
	{
		.name		 = "led:green:1",
		.default_trigger = "heartbeat",
		.gpio		 = MXS_PIN_TO_GPIO(MXS_PIN_ENCODE(3, 29)),
		.active_low	 = 1,
	},
};

static struct gpio_led_platform_data tion_pro28_leds_info = {
	.leds		= tion_pro28_leds,
	.num_leds	= ARRAY_SIZE(tion_pro28_leds),
};

static struct platform_device tion_pro28_leds_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &tion_pro28_leds_info,
	},
};

static void __init mx28evk_init_leds(void)
{
	platform_device_register(&tion_pro28_leds_device);
}
#endif

static void __init mx28evk_device_init(void)
{
	/* Add mx28evk special code */
	i2c_device_init();
	spi_device_init();
	mx28evk_init_leds();
	hidg_device_init();
}

static void __init mx28evk_init_machine(void)
{
	mx28_pinctrl_init();
	/* Init iram allocate */
#ifdef CONFIG_VECTORS_PHY_ADDR
	/* reserve the first page for irq vector table*/
	iram_init(MX28_OCRAM_PHBASE + PAGE_SIZE, MX28_OCRAM_SIZE - PAGE_SIZE);
#else
	iram_init(MX28_OCRAM_PHBASE, MX28_OCRAM_SIZE);
#endif

	mx28_gpio_init();
	mx28evk_pins_init();
	mx28_device_init();
	mx28evk_device_init();
}

MACHINE_START(MX28EVK, "Freescale MX28EVK board")
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf0000000) >> 18) & 0xfffc,
	.boot_params	= 0x40000100,
	.fixup		= fixup_board,
	.map_io		= mx28_map_io,
	.init_irq	= mx28_irq_init,
	.init_machine	= mx28evk_init_machine,
	.timer		= &mx28_timer.timer,
MACHINE_END
