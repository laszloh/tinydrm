/*
 * TinyDRM driver for the ST7735R LCD Controller
 *
 * Copyright (C) 2013 Noralf Tronnes
 * Copyright (C) 2017 Laszlo Hegedüs
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <drm/tinydrm/ili9341.h>
#include <drm/tinydrm/mipi-dbi.h>
#include <drm/tinydrm/tinydrm-helpers.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

static int st7735r_init(struct mipi_dbi *mipi)
{
	struct tinydrm_device *tdev = &mipi->tinydrm;
	struct device *dev = tdev->drm->dev;
	u8 addr_mode;
	int ret;

	DRM_DEBUG_KMS("\n");

	ret = regulator_enable(mipi->regulator);
	if (ret) {
		dev_err(dev, "Failed to enable regulator %d\n", ret);
		return ret;
	}

	/* Avoid flicker by skipping setup if the bootloader has done it */
	if (mipi_dbi_display_is_on(mipi))
		return 0;

	mipi_dbi_hw_reset(mipi);
	ret = mipi_dbi_command(mipi, MIPI_DCS_SOFT_RESET);
	if (ret) {
		dev_err(dev, "Error sending command %d\n", ret);
		regulator_disable(mipi->regulator);
		return ret;
	}

	msleep(20);

	mipi_dbi_command(mipi, MIPI_DCS_SET_DISPLAY_OFF);

	/* Frame Rate */
	mipi_dbi_command(mipi, ILI9341_FRMCTR1, 0x01, 0x2c, 0x2d);
	mipi_dbi_command(mipi, ILI9341_FRMCTR2, 0x01, 0x2c, 0x2d);
	mipi_dbi_command(mipi, ILI9341_FRMCTR3, 0x01, 0x2c, 0x2d, 0x01, 0x2c, 0x2d);
	mipi_dbi_command(mipi, ILI9341_INVTR, 0x07);
	
	/* Power Control */
	mipi_dbi_command(mipi, ILI9341_PWCTRL1, 0xa2, 0x02, 0x84);
	mipi_dbi_command(mipi, ILI9341_PWCTRL2, 0xc5);
	mipi_dbi_command(mipi, 0xc2, 0x0a, 0x00);	// PWRCTR3
	mipi_dbi_command(mipi, 0xc3, 0x8a, 0x2a);	// PWRCTR4
	mipi_dbi_command(mipi, 0xc4, 0x8a, 0xee);	// PWRCTR5 
	mipi_dbi_command(mipi, ILI9341_VMCTRL1, 0x0e);
	
	/* Gamma */
	mipi_dbi_command(mipi, ILI9341_EN3GAM, 0x08);
	mipi_dbi_command(mipi, MIPI_DCS_SET_GAMMA_CURVE, 0x01);
	mipi_dbi_command(mipi, ILI9341_PGAMCTRL,
		       0x1f, 0x1a, 0x18, 0x0a, 0x0f, 0x06, 0x45, 0x87,
		       0x32, 0x0a, 0x07, 0x02, 0x07, 0x05, 0x00);
	mipi_dbi_command(mipi, ILI9341_NGAMCTRL,
		       0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3a, 0x78,
		       0x4d, 0x05, 0x18, 0x0d, 0x38, 0x3a, 0x1f);
	
	/* Display Mode */
	mipi_dbi_command(mipi, MIPI_DCS_EXIT_INVERT_MODE);
	mipi_dbi_command(mipi, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FMT_16BIT);
	mipi_dbi_command(mipi, MIPI_DCS_ENTER_NORMAL_MODE);
	
	switch (mipi->rotation) {
	default:
		addr_mode = ILI9341_MADCTL_MV | ILI9341_MADCTL_MY |
			    ILI9341_MADCTL_MX;
		break;
	case 90:
		addr_mode = ILI9341_MADCTL_MY;
		break;
	case 180:
		addr_mode = ILI9341_MADCTL_MV;
		break;
	case 270:
		addr_mode = ILI9341_MADCTL_MX;
		break;
	}
	addr_mode |= ILI9341_MADCTL_BGR;
	mipi_dbi_command(mipi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);

	/* Start display */
	mipi_dbi_command(mipi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(100);

	mipi_dbi_command(mipi, MIPI_DCS_SET_DISPLAY_ON);
	msleep(100);

	return 0;
}

static void st7735r_fini(void *data)
{
	struct mipi_dbi *mipi = data;

	DRM_DEBUG_KMS("\n");
	regulator_disable(mipi->regulator);
}

static const struct drm_simple_display_pipe_funcs st7735r_pipe_funcs = {
	.enable = mipi_dbi_pipe_enable,
	.disable = mipi_dbi_pipe_disable,
	.update = tinydrm_display_pipe_update,
	.prepare_fb = tinydrm_display_pipe_prepare_fb,
};

static const struct drm_display_mode st7735r_mode = {
	TINYDRM_MODE(128, 160, 35, 28),
};

DEFINE_DRM_GEM_CMA_FOPS(st7735r_fops);

static struct drm_driver st7735r_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME |
				  DRIVER_ATOMIC,
	.fops			= &st7735r_fops,
	TINYDRM_GEM_DRIVER_OPS,
	.lastclose		= tinydrm_lastclose,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "st7735r",
	.desc			= "Sitronix ST7735R",
	.date			= "20171025",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id st7735r_of_match[] = {
	{ .compatible = "sitronix,st7735r" },
	{},
};
MODULE_DEVICE_TABLE(of, st7735r_of_match);

static const struct spi_device_id st7735r_id[] = {
	{ "st7735r", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, st7735r_id);

static int st7735r_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct tinydrm_device *tdev;
	struct mipi_dbi *mipi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;

	mipi = devm_kzalloc(dev, sizeof(*mipi), GFP_KERNEL);
	if (!mipi)
		return -ENOMEM;

	mipi->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(mipi->reset)) {
		dev_err(dev, "Failed to get gpio 'reset'\n");
		return PTR_ERR(mipi->reset);
	}

	dc = devm_gpiod_get_optional(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc)) {
		dev_err(dev, "Failed to get gpio 'dc'\n");
		return PTR_ERR(dc);
	}

	mipi->regulator = devm_regulator_get(dev, "power");
	if (IS_ERR(mipi->regulator))
		return PTR_ERR(mipi->regulator);

	mipi->backlight = tinydrm_of_find_backlight(dev);
	if (IS_ERR(mipi->backlight))
		return PTR_ERR(mipi->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, mipi, dc, &st7735r_pipe_funcs,
			    &st7735r_driver, &st7735r_mode, rotation);
	if (ret)
		return ret;

	/* use devres to fini after drm unregister (drv->remove is before) */
	ret = devm_add_action(dev, st7735r_fini, mipi);
	if (ret) {
		st7735r_fini(mipi);
		return ret;
	}

	tdev = &mipi->tinydrm;

	ret = devm_tinydrm_register(tdev);
	if (ret)
		return ret;

	spi_set_drvdata(spi, mipi);

	DRM_DEBUG_DRIVER("Initialized %s:%s @%uMHz on minor %d\n",
			 tdev->drm->driver->name, dev_name(dev),
			 spi->max_speed_hz / 1000000,
			 tdev->drm->primary->index);

	return 0;
}

static void st7735r_shutdown(struct spi_device *spi)
{
	struct mipi_dbi *mipi = spi_get_drvdata(spi);

	tinydrm_shutdown(&mipi->tinydrm);
}

static int __maybe_unused st7735r_pm_suspend(struct device *dev)
{
	struct mipi_dbi *mipi = dev_get_drvdata(dev);
	int ret;

	ret = tinydrm_suspend(&mipi->tinydrm);
	if (ret)
		return ret;

	st7735r_fini(mipi);

	return 0;
}

static int __maybe_unused st7735r_pm_resume(struct device *dev)
{
	struct mipi_dbi *mipi = dev_get_drvdata(dev);
	int ret;

	ret = st7735r_init(mipi);
	if (ret)
		return ret;

	return tinydrm_resume(&mipi->tinydrm);
}

static const struct dev_pm_ops st7735r_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st7735r_pm_suspend, st7735r_pm_resume)
};

static struct spi_driver st7735r_spi_driver = {
	.driver = {
		.name = "st7735r",
		.owner = THIS_MODULE,
		.of_match_table = st7735r_of_match,
		.pm = &st7735r_pm_ops,
	},
	.id_table = st7735r_id,
	.probe = st7735r_probe,
	.shutdown = st7735r_shutdown,
};
module_spi_driver(st7735r_spi_driver);

MODULE_DESCRIPTION("Sitronix ST7735R DRM driver");
MODULE_AUTHOR("Laszlo Hegedüs");
MODULE_LICENSE("GPL");
