// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for LX Semicon SW82907 touchscreen over I2C
 *
 * Copyright (c) 2024 LX Semicon Co., Ltd.
 * Copyright (c) 2026 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 */

#include <linux/unaligned.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

/* commands */
#define SPR_CHIP_ID				0x000
#define SPR_RST_CTL				0x004
#define SPR_BOOT_CTL				0x00F
#define SPR_SRAM_CTL				0x010
#define SPR_BOOT_STS				0x011
#define TC_IC_STATUS				(0x600)
#define TC_FW_STATUS				(0x601)
#define TC_VERSION				(0x642)
#define TC_PRODUCT_ID1				(0x644)
#define TC_PRODUCT_ID2				(0x645)
#define TC_DEVICE_CTL				(0xC00)
#define TC_DRIVE_CTL				(0xC03)

/* events */

/* info */

/* firmware status */

/* touchscreen functionalities */
#define RES_INFO				0xD00
#define CHANNEL_INFO				0xD01
#define T_FRAME_RATE				0xD03
#define CHARGER_INFO				0xD05
#define FRAME_RATE				0xD06
#define GAMING_MODE				0xD07
#define GLOVE_MODE				0xD08
#define IME_STATE				0xD09
#define CALL_STATE				0xD0A
#define GRAB_MODE				0xD0B

/* boot status (BS) */

/* event id */
#define CMD_U0					BIT(0)
#define CMD_U3					0x300 | CMD_U0
#define CMD_STOP				BIT(2)

/* event register masks */

/* event touch state values */
#define SW82907_TS_NONE			0x00
#define SW82907_TS_PRESS		0x01
#define SW82907_TS_MOVE			0x02
#define SW82907_TS_RELEASE		0x03

/* application modes */

#define SW82907_MAX_FINGER	10
#define SW82907_MAX_XFER	0x8000
#define SW82907_DEV_NAME	"sw82907"

struct sw82907_touch_data {
	u8 tool_type:4;
	u8 event:4;
	u8 track_id;
	u16 x;
	u16 y;
	u8 pressure;
	u8 angle;
	u16 width_major;
	u16 width_minor;
} __packed;

struct lxs_hal_touch_info {
	u32 ic_status;
	u32 device_status;
	//
	u32 wakeup_type:8;
	u32 touch_cnt:5;
	u32 button_cnt:3;
	u32 palm_bit:16;
	//
	struct sw82907_touch_data data[SW82907_MAX_FINGER];
} __packed;

struct sw82907_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct touchscreen_properties prop;

	struct gpio_desc *reset_gpio;

	char tx_buf[SW82907_MAX_XFER];
	char rx_buf[SW82907_MAX_XFER];

	u16 devid;
	u8 tx_channel;

	struct lxs_hal_touch_info info;
};

static int sw82907_read(struct sw82907_data *sdata, u32 addr, size_t len)
{
	struct i2c_msg msgs[2] = {
		{
			.addr  = sdata->client->addr,
			.flags = 0,
			.len   = 2,
			.buf   = sdata->tx_buf,
		},
		{
			.addr  = sdata->client->addr,
			.flags = I2C_M_RD,
			.len   = len,
			.buf   = sdata->rx_buf,
		},
	};
	int ret;

	/* Format command */
	sdata->tx_buf[0] = (len > 4) ? 0x20 : 0x00;
	sdata->tx_buf[0] |= ((addr >> 8) & 0x0f);
	sdata->tx_buf[1] = (addr & 0xff);

	ret = i2c_transfer(sdata->client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;

	return ret == ARRAY_SIZE(msgs) ? 0 : -EIO;
}

static int sw82907_write(struct sw82907_data *sdata, u32 addr, void* data, size_t len)
{
	struct i2c_msg msgs[1] = {
		{
			.addr  = sdata->client->addr,
			.flags = 0,
			.len   = 2 + len,
			.buf   = sdata->tx_buf,
		},
	};
	int ret;
	u8 *dbuf;

	/* Format command */
	sdata->tx_buf[0] = (len > 4) ? 0x60 : 0x40;
	sdata->tx_buf[0] |= ((addr >> 8) & 0x0f);
	sdata->tx_buf[1] = (addr & 0xff);

	/* Header... ??? */
	dbuf = &sdata->tx_buf[2];
	memcpy(dbuf, data, len);

	ret = i2c_transfer(sdata->client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;

	return ret == ARRAY_SIZE(msgs) ? 0 : -EIO;
}

static int sw82907_read_value(struct sw82907_data *sdata, u32 addr)
{
	return sw82907_read(sdata, addr, sizeof(u32));
}

static int sw82907_write_value(struct sw82907_data *sdata, u32 addr, u32 value)
{
	return sw82907_write(sdata, addr, &value, sizeof(u32));
}

static void sw82907_report_coordinates(struct sw82907_data *sdata,
				       struct sw82907_touch_data *event, u8 tid)
{
	input_mt_slot(sdata->input, tid);

	input_mt_report_slot_state(sdata->input, MT_TOOL_FINGER, true);
	input_report_abs(sdata->input, ABS_MT_POSITION_X, event->x);
	input_report_abs(sdata->input, ABS_MT_POSITION_Y, event->y);
	input_report_abs(sdata->input, ABS_MT_TOUCH_MAJOR, event->width_major);
	input_report_abs(sdata->input, ABS_MT_TOUCH_MINOR, event->width_minor);
	input_report_abs(sdata->input, ABS_MT_PRESSURE, event->pressure);

	input_sync(sdata->input);
}

static void sw82907_report_release(struct sw82907_data *sdata, u8 tid)
{
	input_mt_slot(sdata->input, tid);
	input_mt_report_slot_state(sdata->input, MT_TOOL_FINGER, false);

	input_sync(sdata->input);
}

static void sw82907_handle_coordinates(struct sw82907_data *sdata, struct sw82907_touch_data *event)
{
	switch (event->event) {

		case SW82907_TS_NONE:
			break;
		case SW82907_TS_RELEASE:
			sw82907_report_release(sdata, event->track_id);
			break;
		case SW82907_TS_PRESS:
		case SW82907_TS_MOVE:
			sw82907_report_coordinates(sdata, event, event->track_id);
			break;
	}
}

static void sw82907_handle_events(struct sw82907_data *sdata, u8 n_events)
{
	int i;

	for (i = 0; i < n_events; i++) {
		struct sw82907_touch_data *event = &sdata->info.data[i];
		if (!event)
			return;

		sw82907_handle_coordinates(sdata, event);
	}
}

static irqreturn_t sw82907_irq_handler(int irq, void *dev)
{
	struct sw82907_data *sdata = dev;
	int size = 0;
	int pkt_unit = 0;
	int pkt_cnt = 0;
	int wakeup_type = 0;
	int touch_cnt = 0;
	int ret = 0;
	u32 addr = TC_IC_STATUS;

	pkt_unit = sizeof(struct sw82907_touch_data);
	pkt_cnt = 1;
	touch_cnt = SW82907_MAX_FINGER - pkt_cnt;

	size = (3<<2);
	size += (pkt_unit * pkt_cnt);

	ret = sw82907_read(sdata, addr, size);

	memcpy(&sdata->info, sdata->rx_buf, size);

	wakeup_type = sdata->info.wakeup_type;
	touch_cnt = sdata->info.touch_cnt;

	if (wakeup_type != 0)
		return 0;

	sw82907_handle_events(sdata, touch_cnt +  1);

	return IRQ_HANDLED;
}

static int sw82907_input_open(struct input_dev *dev)
{
	return 0;
}

static void sw82907_input_close(struct input_dev *dev)
{
	return;
}

static int sw82907_hw_init(struct sw82907_data *sdata)
{
	// reset again...
	gpiod_set_value_cansleep(sdata->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(sdata->reset_gpio, 0);
	msleep(100);

	sw82907_write_value(sdata, TC_DRIVE_CTL, CMD_U3);
	msleep(20);

	sw82907_read_value(sdata, TC_FW_STATUS);

	sdata->tx_channel = 1;

	dev_err(&sdata->client->dev,
		"LXS Running Status: %x\n", sdata->tx_buf[0] & 0x1f);

	return 0;
}

static int sw82907_power_on(struct sw82907_data *sdata)
{
	int ret;

	gpiod_set_value_cansleep(sdata->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(sdata->reset_gpio, 0);
	msleep(100);

	ret = sw82907_read_value(sdata, 0x0);
	if (ret < 0)
		dev_err(&sdata->client->dev,
			"failed to read chipid: %d\n", ret);

	sdata->rx_buf[4] = '\0';

	dev_err(&sdata->client->dev, "Chip ID: %s\n", sdata->rx_buf);

	ret = sw82907_read_value(sdata, SPR_BOOT_STS);
	if (ret < 0)
		dev_err(&sdata->client->dev,
			"failed to read boot status: %d\n", ret);

	if (sdata->rx_buf[0] != 0x0)
		dev_err(&sdata->client->dev, "boot failed [%i]\n", sdata->rx_buf[0]);


	sw82907_hw_init(sdata);

	return 0;
}

static void sw82907_power_off(void *data)
{
	struct sw82907_data *sdata = data;

	disable_irq(sdata->client->irq);
}

static int sw82907_probe(struct i2c_client *client)
{
	struct sw82907_data *sdata;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
						I2C_FUNC_SMBUS_BYTE_DATA |
						I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;

	sdata = devm_kzalloc(&client->dev, sizeof(*sdata), GFP_KERNEL);
	if (!sdata)
		return -ENOMEM;

	i2c_set_clientdata(client, sdata);
	sdata->client = client;

	err = devm_add_action_or_reset(&client->dev, sw82907_power_off, sdata);
	if (err)
		return err;

	dev_err(&client->dev, "SW82907 Probe!\n");

	sdata->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sdata->reset_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(sdata->reset_gpio),
				     "Failed to request reset gpio\n");

	// Power on hardware
	sw82907_power_on(sdata);

	sdata->input = devm_input_allocate_device(&client->dev);
	if (!sdata->input)
		return -ENOMEM;

	sdata->input->name = SW82907_DEV_NAME;
	sdata->input->id.bustype = BUS_I2C;
	sdata->input->open = sw82907_input_open;
	sdata->input->close = sw82907_input_close;

	// TODO device tree
	input_set_abs_params(sdata->input, ABS_MT_POSITION_X, 0, 1080, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_POSITION_Y, 0, 2340, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_PRESSURE, 0, 255, 0, 0);

	__set_bit(INPUT_PROP_DIRECT, sdata->input->propbit);

	touchscreen_parse_properties(sdata->input, true, &sdata->prop);

	if (!input_abs_get_max(sdata->input, ABS_X) ||
	    !input_abs_get_max(sdata->input, ABS_Y))
	{
		dev_warn(&client->dev, "the axis have not been set\n");
	}

	err = input_mt_init_slots(sdata->input, sdata->tx_channel,
				INPUT_MT_DIRECT);
	if (err)
		return err;

	input_set_drvdata(sdata->input, sdata);

	err = input_register_device(sdata->input);
	if (err)
		return err;

	err = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					sw82907_irq_handler,
				 IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				 "sw82907_irq", sdata);
	if (err)
		return err;

	pm_runtime_enable(&client->dev);

	return 0;
}

static void sw82907_remove(struct i2c_client *client)
{
	pm_runtime_disable(&client->dev);
}

static int sw82907_runtime_suspend(struct device *dev)
{
	//struct sw82907_data *sdata = dev_get_drvdata(dev);

	return 0;
}

static int sw82907_runtime_resume(struct device *dev)
{
	//struct sw82907_data *sdata = dev_get_drvdata(dev);

	return 0;
}

static int sw82907_suspend(struct device *dev)
{
	struct sw82907_data *sdata = dev_get_drvdata(dev);

	sw82907_power_off(sdata);

	return 0;
}

static int sw82907_resume(struct device *dev)
{
	struct sw82907_data *sdata = dev_get_drvdata(dev);

	enable_irq(sdata->client->irq);

	return 0; //power on here
}

static const struct dev_pm_ops sw82907_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(sw82907_suspend, sw82907_resume)
	RUNTIME_PM_OPS(sw82907_runtime_suspend, sw82907_runtime_resume, NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id sw82907_of_match[] = {
	{ .compatible = "lxs,sw82907", },
	{ },
};
MODULE_DEVICE_TABLE(of, sw82907_of_match);
#endif

static const struct i2c_device_id sw82907_id[] = {
	{ "sw82907" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sw82907_id);

static struct i2c_driver sw82907_driver = {
	.driver = {
		.name = SW82907_DEV_NAME,
		.of_match_table = of_match_ptr(sw82907_of_match),
		.pm = pm_ptr(&sw82907_pm_ops),
	},
	.probe = sw82907_probe,
	.remove = sw82907_remove,
	.id_table = sw82907_id,
};

module_i2c_driver(sw82907_driver);

MODULE_AUTHOR("Igor Belwon <igor.belwon@mentallysanemainliners.org>");
MODULE_DESCRIPTION("LX Semicon SW82907 Touchscreen");
MODULE_LICENSE("GPL v2");
