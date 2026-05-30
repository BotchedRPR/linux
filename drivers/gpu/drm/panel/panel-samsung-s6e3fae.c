// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Samsung S6E3FAE display panel
 *
 * Copyright (c) 2026 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_panel.h>

struct s6e3fae {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data s6e3fae_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vci" },
};

static inline
struct s6e3fae *to_s6e3fae_amb646fj01_fhd(struct drm_panel *panel)
{
	return container_of(panel, struct s6e3fae, panel);
}

#define s6e3fae_test_key_on_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0x5a, 0x5a)
#define s6e3fae_test_key_off_lvl2(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf0, 0xa5, 0xa5)
#define s6e3fae_test_key_on_lvl3(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0x5a, 0x5a)
#define s6e3fae_test_key_off_lvl3(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0xa5, 0xa5)
#define s6e3fae_test_key_on_lvl1(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9f, 0xa5, 0xa5)
#define s6e3fae_test_key_off_lvl1(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0x9f, 0x5a, 0x5a)
#define s6e3fae_afc_off(ctx) \
	mipi_dsi_dcs_write_seq_multi(ctx, 0xe2, 0x00, 0x00)

static void s6e3fae_amb646fj01_fhd_reset(struct s6e3fae *priv)
{
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int s6e3fae_enter_lp1(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_usleep_range(ctx, 6000, 7000);
	s6e3fae_test_key_on_lvl2(ctx);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb9, 0x50);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf7, 0x2f);
	s6e3fae_test_key_off_lvl2(ctx);
	s6e3fae_test_key_on_lvl2(ctx);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x00, 0x10, 0xbd);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x83, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x00, 0x01, 0xbd);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbd, 0x81);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbd, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf7, 0x2f);
	s6e3fae_test_key_off_lvl2(ctx);
	mipi_dsi_dcs_set_display_off_multi(ctx);
	mipi_dsi_usleep_range(ctx, 10000, 11000);
	mipi_dsi_dcs_write_seq_multi(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_dcs_set_display_brightness_multi(ctx, 0x8002);
	s6e3fae_test_key_on_lvl2(ctx);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbd, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x00, 0x02, 0xbd);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x00, 0xe8, 0xbd);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbd, 0x40);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x00, 0x01, 0x83);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x83, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf7, 0x2f);
	s6e3fae_test_key_off_lvl2(ctx);
	mipi_dsi_dcs_set_display_on_multi(ctx);

	return ctx->accum_err;
}

static int s6e3fae_amb646fj01_fhd_on(struct s6e3fae *priv)
{
	struct mipi_dsi_device *dsi = priv->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	// Enter LP1
	s6e3fae_enter_lp1(&ctx);

	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);
	s6e3fae_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x01, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf2, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf7, 0x2f);
	s6e3fae_test_key_off_lvl2(&ctx);
	mipi_dsi_dcs_set_tear_on_multi(&ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_set_column_address_multi(&ctx, 0x0000, 0x0437);
	mipi_dsi_dcs_set_page_address_multi(&ctx, 0x0000, 0x0923);
	s6e3fae_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x10, 0xbd);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x83, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x01, 0xbd);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x81);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf7, 0x2f);
	s6e3fae_test_key_off_lvl2(&ctx);
	s6e3fae_test_key_on_lvl2(&ctx);
	s6e3fae_test_key_on_lvl3(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x3c, 0xc5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x43, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x36, 0xc5);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc5, 0x11, 0x10, 0x50, 0x05);
	s6e3fae_test_key_off_lvl3(&ctx);
	s6e3fae_test_key_off_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	s6e3fae_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x08, 0x68);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x68, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x03, 0x68);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x68, 0x25);
	s6e3fae_test_key_off_lvl2(&ctx);
	s6e3fae_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe5, 0x15);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x5f, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf4, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x67, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf4, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x17, 0xed);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xed, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xed, 0x0b, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x03, 0xed);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xed, 0x0d);
	s6e3fae_test_key_off_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x28);
	s6e3fae_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x17, 0x69);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x69, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf7, 0x2f);
	s6e3fae_test_key_off_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x9d, 0x01);
	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_msleep(&ctx, 20);
	s6e3fae_test_key_on_lvl2(&ctx);

	// Re-enter LP1
	s6e3fae_enter_lp1(&ctx);

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_usleep_range(&ctx, 10000, 11000);
	mipi_dsi_dcs_write_seq_multi(&ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_set_display_brightness_multi(&ctx, 0x0400);
	s6e3fae_test_key_on_lvl2(&ctx);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x10, 0xbd);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x83, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb0, 0x00, 0x01, 0xbd);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x81);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf7, 0x2f);
	s6e3fae_test_key_off_lvl2(&ctx);
	mipi_dsi_usleep_range(&ctx, 10000, 11000);

	return ctx.accum_err;
}

static int s6e3fae_enable(struct drm_panel *panel)
{
	struct s6e3fae *priv = to_s6e3fae_amb646fj01_fhd(panel);
	struct mipi_dsi_device *dsi = priv->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_on_multi(&ctx);

	return ctx.accum_err;
}

static int s6e3fae_disable(struct drm_panel *panel)
{
	struct s6e3fae *priv = to_s6e3fae_amb646fj01_fhd(panel);
	struct mipi_dsi_device *dsi = priv->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_msleep(&ctx, 20);

	s6e3fae_test_key_on_lvl2(&ctx);
	s6e3fae_afc_off(&ctx);
	s6e3fae_test_key_off_lvl2(&ctx);

	mipi_dsi_msleep(&ctx, 160);

	return ctx.accum_err;
}

static int s6e3fae_amb646fj01_fhd_prepare(struct drm_panel *panel)
{
	struct s6e3fae *priv = to_s6e3fae_amb646fj01_fhd(panel);
	struct mipi_dsi_device *dsi = priv->dsi;
	struct mipi_dsi_multi_context ctx = { .dsi = dsi };
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e3fae_supplies), priv->supplies);
	if (ret < 0)
		return ret;

	mipi_dsi_msleep(&ctx, 120);

	s6e3fae_amb646fj01_fhd_reset(priv);

	ret = s6e3fae_amb646fj01_fhd_on(priv);
	if (ret < 0) {
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		return 0;
	}

	drm_dsc_pps_payload_pack(&pps, &priv->dsc);

	ret = mipi_dsi_picture_parameter_set(priv->dsi, &pps);
	if (ret < 0) {
		dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_compression_mode(priv->dsi, true);
	if (ret < 0) {
		dev_err(panel->dev, "failed to enable compression mode: %d\n", ret);
		return ret;
	}

	msleep(28);
	return 0;
}

static int s6e3fae_amb646fj01_fhd_unprepare(struct drm_panel *panel)
{
	struct s6e3fae *priv = to_s6e3fae_amb646fj01_fhd(panel);
	gpiod_set_value_cansleep(priv->reset_gpio, 1);

	return regulator_bulk_disable(ARRAY_SIZE(s6e3fae_supplies), priv->supplies);
}

static const struct drm_display_mode s6e3fae_amb646fj01_fhd_mode = {
	.clock = (1080 + 64 + 8 + 8) * (2340 + 121 + 8 + 8) * 120 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 64,
	.hsync_end = 1080 + 64 + 8,
	.htotal = 1080 + 64 + 8 + 8,
	.vdisplay = 2340,
	.vsync_start = 2340 + 121,
	.vsync_end = 2340 + 121 + 8,
	.vtotal = 2340 + 121 + 8 + 8,
	.width_mm = 69,
	.height_mm = 149,
};

static int s6e3fae_amb646fj01_fhd_get_modes(struct drm_panel *panel,
					     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &s6e3fae_amb646fj01_fhd_mode);
}

static const struct drm_panel_funcs s6e3fae_amb646fj01_fhd_panel_funcs = {
	.prepare = s6e3fae_amb646fj01_fhd_prepare,
	.unprepare = s6e3fae_amb646fj01_fhd_unprepare,
	.get_modes = s6e3fae_amb646fj01_fhd_get_modes,
	.enable = s6e3fae_enable,
	.disable = s6e3fae_disable,
};

static int panel_samsung_s6e3fae_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int panel_samsung_s6e3fae_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops panel_samsung_s6e3fae_bl_ops = {
	.update_status = panel_samsung_s6e3fae_bl_update_status,
	.get_brightness = panel_samsung_s6e3fae_bl_get_brightness,
};

static struct backlight_device *
panel_samsung_s6e3fae_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 400,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &panel_samsung_s6e3fae_bl_ops, &props);
}

static int s6e3fae_amb646fj01_fhd_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e3fae *priv;
	int ret;

	priv = devm_drm_panel_alloc(dev, struct s6e3fae, panel,
				    &s6e3fae_amb646fj01_fhd_panel_funcs,
				    DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(priv))
		return PTR_ERR(priv);


	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(s6e3fae_supplies),
				      s6e3fae_supplies,
				      &priv->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to get regulators: %d\n", ret);
		return ret;
	}

	priv->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio),
				     "Failed to get reset-gpios\n");

	priv->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, priv);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	priv->panel.prepare_prev_first = true;

	priv->panel.backlight = panel_samsung_s6e3fae_create_backlight(dsi);
	if (IS_ERR(priv->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(priv->panel.backlight),
				     "Failed to create backlight\n");


	drm_panel_add(&priv->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &priv->dsc;

	priv->dsc.dsc_version_major = 1;
	priv->dsc.dsc_version_minor = 1;

	priv->dsc.slice_height = 60;
	priv->dsc.slice_width = 540;
	WARN_ON(1080 % priv->dsc.slice_width);
	priv->dsc.slice_count = 1080 / priv->dsc.slice_width;
	priv->dsc.bits_per_component = 8;
	priv->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	priv->dsc.block_pred_enable = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&priv->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void s6e3fae_amb646fj01_fhd_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3fae *priv = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&priv->panel);
}

static const struct of_device_id s6e3fae_amb646fj01_fhd_of_match[] = {
	{ .compatible = "samsung,s6e3fae-amb646fj01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e3fae_amb646fj01_fhd_of_match);

static struct mipi_dsi_driver s6e3fae_amb646fj01_fhd_driver = {
	.probe = s6e3fae_amb646fj01_fhd_probe,
	.remove = s6e3fae_amb646fj01_fhd_remove,
	.driver = {
		.name = "panel-s6e3fae",
		.of_match_table = s6e3fae_amb646fj01_fhd_of_match,
	},
};
module_mipi_dsi_driver(s6e3fae_amb646fj01_fhd_driver);

MODULE_AUTHOR("Igor Belwon <igor.belwon@mentallysanemainliners.org>");
MODULE_DESCRIPTION("DRM driver for s6e3fae panel");
MODULE_LICENSE("GPL");
