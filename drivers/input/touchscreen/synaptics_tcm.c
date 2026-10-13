// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TCM Touchscreen Driver
 *
 * Copyright (C) 2017-2020 Synaptics Incorporated.
 * Copyright (C) 2026 Luka Panio <lukapanio@gmail.com>
 *
 * Based on synaptics_hbp downstream driver found in
 * various Android vendor kernels.
 */
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/input/mt.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "synaptics_tcm.h"

static int synaptics_tcm_power_on(struct synaptics_tcm_core *ts)
{
	int ret;

	ret = regulator_enable(ts->vdd);
	if (ret) {
		dev_err(ts->dev, "Failed to enable vddio: %d\n", ret);
		goto err_vdd_disable;
	}

	ret = regulator_enable(ts->avdd);
	if (ret) {
		dev_err(ts->dev, "Failed to enable avdd: %d\n", ret);
		goto err_avdd_disable;
	}

	/* Vendor waits 200ms for power to settle */
	msleep(200);

	/* Vendor reset seqence*/
	gpiod_set_value_cansleep(ts->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ts->reset_gpio, 0);
	msleep(80);

	return 0;

err_avdd_disable:
	regulator_disable(ts->avdd);
err_vdd_disable:
	regulator_disable(ts->vdd);
	return ret;
}

static int synaptics_tcm_read_header(struct synaptics_tcm_core *ts, unsigned char *buf) {
	int ret;
	ret = ts->bus->read(ts->bus, buf, 4);
	if (ret) {
		dev_err(ts->dev, "Failed to read header: %d\n", ret);
	}
	return ret;
}

static int synaptics_tcm_continue_read(struct synaptics_tcm_core *ts, size_t len, unsigned char *buf) {
	int ret = 0;
	int i;
	size_t total_length, remaining_length, chunk_space, xfer_length, chunks;
	size_t offset, copy_len;
	unsigned char *tmp_buf;

	// Len is at least payload + padding of 1
	total_length = len + 1;

	if(ts->has_crc) {
		total_length += SYNAPTICS_TCM_MSG_CRC_LENGTH;
	}

	if(ts->has_rxtra_rc) {
		total_length += SYNAPTICS_TCM_EXTRA_RC_LENGTH + 1;
	}

	if(ts->has_crc || ts->has_rxtra_rc) {
		total_length += 1;
	}

	if(ts->max_rx_size == 0) {
		chunk_space = total_length;
	} else {
		chunk_space = ts->max_rx_size - 2;
	}

	chunks = max(DIV_ROUND_UP(total_length, chunk_space), 1U);
	remaining_length = total_length;
	tmp_buf = kzalloc(chunk_space + 2, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	for (i = 0; i < chunks; i++) {
		xfer_length = min(remaining_length, chunk_space);
		offset = total_length - remaining_length;

		// Special case, if only padding is left add it ourself
		if (xfer_length == 1) {
			buf[total_length - remaining_length] = SYNAPTICS_TCM_V1_MESSAGE_PADDING;
			remaining_length -= xfer_length;
			continue;
		}

		ret = ts->bus->read(ts->bus, tmp_buf, xfer_length + 2);

		if (ret) {
			dev_err(ts->dev, "Failed to continue read: %d\n", ret);
			goto exit;
		}

		copy_len = min(xfer_length, len - offset);
		memcpy(buf + offset, tmp_buf + 2, copy_len);

                remaining_length -= xfer_length;
	}

exit:
	kfree(tmp_buf);
	return ret;

}

static int synaptics_tcm_identify_chip(struct synaptics_tcm_core *ts)
{
	int ret;
	struct synaptics_tcm_v1_message_header header;
	struct synaptics_tcm_identification_info chipid;
	unsigned char cmd = 0x07;

	ret = ts->bus->write(ts->bus, &cmd, 1);
	if (ret) {
		dev_err(ts->dev, "Failed to write 0x07: %d\n", ret);
		return ret;
	}

	ret = synaptics_tcm_read_header(ts, (unsigned char*)&header);
	if (ret) {
		return ret;
	}

	if (header.marker != SYNAPTICS_TCM_V1_MESSAGE_MARKER) {
		dev_err(ts->dev, "Invalid header marker: %x\n", header.marker);
		return ENODEV;
        }

	if (header.code == REPORT_IDENTIFY) {
		ret = synaptics_tcm_continue_read(ts, get_unaligned_le16(header.length), (unsigned char *)&chipid);

		if (ret) {
			dev_err(ts->dev, "Failed to read IDENTIFY: %d\n", ret);
			return ret;
		}
	} else {
		dev_err(ts->dev, "Touchscreen in inconsistent state, verify reset\n");
		return ENODEV;
	}

	u32 build_id;

	build_id = get_unaligned_le32(chipid.build_id);

	if (chipid.mode != SYNAPTICS_TCM_MODE_APPLICATION_FIRMWARE) {
		dev_err(ts->dev, "Firmware update not supported\n");
		return ENODEV;
	}

	return ret;
}

static int synaptics_tcm_set_report(struct synaptics_tcm_core *ts, u8 report_code, bool enable)
{
	int ret;
	struct synaptics_tcm_v1_message_header resp_hdr;
	unsigned char cmd_buf[4];

	cmd_buf[0] = enable ? CMD_ENABLE_REPORT : CMD_DISABLE_REPORT;
	cmd_buf[1] = 0x01;
	cmd_buf[2] = 0x00;
	cmd_buf[3] = report_code;

	ret = ts->bus->write(ts->bus, cmd_buf, sizeof(cmd_buf));
	if (ret) {
		dev_err(ts->dev, "Failed to write %s for report 0x%02x: %d\n",
			enable ? "CMD_ENABLE_REPORT" : "CMD_DISABLE_REPORT", report_code, ret);
		return ret;
	}

	msleep(20);

	/* Read response from the device */
	ret = synaptics_tcm_read_header(ts, (unsigned char *)&resp_hdr);
	if (ret) {
		dev_err(ts->dev, "Failed to read response header for report 0x%02x: %d\n", report_code, ret);
		return ret;
	}

	/*
	 * The firmware may be in coord mode already, as such,
	 * this is non-critical.
	 */
	if (!resp_hdr.code)
		dev_warn(ts->dev, "Command to %s report 0x%02x returned status: 0x%02x\n",
			 enable ? "enable" : "disable", report_code, resp_hdr.code);

	return 0;
}

static int synaptics_tcm_get_fw_config(struct synaptics_tcm_core *ts)
{
	int ret;
	struct synaptics_tcm_v1_message_header header;
	struct synaptics_tcm_application_info appinfo;
	unsigned char cmd = CMD_GET_APPLICATION_INFO;

	ret = ts->bus->write(ts->bus, &cmd, 1);
	if (ret) {
		dev_err(ts->dev, "Failed to write CMD_GET_APPLICATION_INFO: %d\n", ret);
		return ret;
	}

	msleep(40);

	ret = synaptics_tcm_read_header(ts, (unsigned char*)&header);
	if (ret) {
		return ret;
	}

	if(get_unaligned_le16(header.length)+2 < sizeof(struct synaptics_tcm_application_info)) {
		dev_err(ts->dev, "Wrong message len for CMD_GET_APPLICATION_INFO\n");
		return ENODATA;
	}

	ret = synaptics_tcm_continue_read(ts, sizeof(struct synaptics_tcm_application_info), (unsigned char *)&appinfo);

	if (ret) {
		dev_err(ts->dev, "Failed to read CMD_GET_APPLICATION_INFO: %d\n", ret);
		return ret;
	}

	ts->max_x =  get_unaligned_le16(appinfo.max_x);
	ts->max_y =  get_unaligned_le16(appinfo.max_y);

	dev_info(ts->dev, "Synaptics TCM controller fimware version: %u status: %u, max_x: %u, max_y: %u\n", get_unaligned_le16(appinfo.version), get_unaligned_le16(appinfo.status), ts->max_x, ts->max_y);

	// Set controller into Coordinate mode
	ret = synaptics_tcm_set_report(ts, REPORT_THP, false);
	if (ret) {
		dev_warn(ts->dev, "Failed to disable THP report (0xC0): %d\n", ret);
	}

	ret = synaptics_tcm_set_report(ts, REPORT_TOUCH, true);
	if (ret) {
		dev_err(ts->dev, "Failed to enable touch report (0x11): %d\n", ret);
		return ret;
	}

	return ret;
}

#define REPORT_HDR_LEN 16
#define OBJ_LEN 8

static void synaptics_tcm_handle_touch_report(struct synaptics_tcm_core *ts,
					      const u8 *payload, u16 len)
{
	u8 num_active;
	const struct synaptics_tcm_report_point *pt;
	int i;
	int max_slots = 10;
	unsigned long active_mask = 0;

	if (len < OBJ_LEN)
		return;

	/* Byte 7 contains the active finger count */
	num_active = payload[7];

	for (i = 0; i < num_active; i++) {
		int offset = OBJ_LEN + (i * sizeof(struct synaptics_tcm_report_point));

		if (offset + sizeof(struct synaptics_tcm_report_point) > len)
			break;

		pt = (const struct synaptics_tcm_report_point *)(payload + offset);

		u8 slot = pt->id & 0x0f;
		u8 status = (pt->id >> 4) & 0x0f;
		u16 x = le16_to_cpu(pt->x);
		u16 y = le16_to_cpu(pt->y);
		u16 z = le16_to_cpu(pt->z);

		if (slot >= max_slots)
			continue;

		input_mt_slot(ts->input, slot);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, status == 1);

		if (status == 1) {
			input_report_abs(ts->input, ABS_MT_POSITION_X, x);
			input_report_abs(ts->input, ABS_MT_POSITION_Y, y);
			input_report_abs(ts->input, ABS_MT_PRESSURE, z);
			input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, pt->wx);
			input_report_abs(ts->input, ABS_MT_TOUCH_MINOR, pt->wy);
			__set_bit(slot, &active_mask);
		}
	}

	/* Lift all slots not present in this frame */
	for (i = 0; i < max_slots; i++) {
		if (!test_bit(i, &active_mask)) {
			input_mt_slot(ts->input, i);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
		}
	}

	input_report_key(ts->input, BTN_TOUCH, active_mask != 0);
	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static irqreturn_t synaptics_tcm_irq(int irq, void *data)
{
	int ret;
	unsigned int payload_length = 0;
	u8 payload_buf[128];
	struct synaptics_tcm_v1_message_header header;
	struct synaptics_tcm_core *ts = data;

	ret = synaptics_tcm_read_header(ts, (unsigned char *)&header);
	if (ret) {
		dev_err_ratelimited(ts->dev, "Failed to read header: %d\n", ret);
		return IRQ_HANDLED;
	}

	payload_length = get_unaligned_le16(header.length);

	if (payload_length > 0 && payload_length <= sizeof(payload_buf)) {
		ret = synaptics_tcm_continue_read(ts, payload_length, payload_buf);
		if (ret) {
			dev_err_ratelimited(ts->dev, "Failed to read payload: %d\n", ret);
			return IRQ_HANDLED;
		}

		// We do not support any input events beside touch
		if (header.code == REPORT_TOUCH) {
			synaptics_tcm_handle_touch_report(ts, payload_buf, payload_length);
		}
	}

	return IRQ_HANDLED;
}

static int synaptics_tcm_input_config(struct synaptics_tcm_core *ts)
{
	int ret;

	ts->input = devm_input_allocate_device(ts->dev);
	if (!ts->input)
		return -ENOMEM;

	input_set_drvdata(ts->input, ts);

	ts->input->name = "Synaptics TCM Capacitive TouchScreen";
	ts->input->phys = "input/ts";

	ts->input->id.bustype = BUS_SPI;

	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, ts->max_x, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, ts->max_y, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);

	touchscreen_parse_properties(ts->input, true, &ts->props);

	ret = input_mt_init_slots(ts->input, 10, INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (ret)
		return ret;

	ret = input_register_device(ts->input);
	if (ret)
		return ret;

	return 0;
}

int synaptics_tcm_probe(struct device *dev, int irq, struct synaptics_tcm_bus_ops *bus_ops) {
	struct synaptics_tcm_core *ts;
	int ret;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!bus_ops)
		return -ENOMEM;

	ts->dev = dev;
	ts->irq = irq;
	ts->bus = bus_ops;

	ts->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->reset_gpio),
				     "Failed to request reset gpio\n");

	ts->avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ts->avdd))
		return dev_err_probe(dev, PTR_ERR(ts->avdd),
				     "Failed to request avdd regulator\n");

	ts->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ts->vdd))
		return dev_err_probe(dev, PTR_ERR(ts->vdd),
				     "Failed to request vdd regulator\n");

	synaptics_tcm_power_on(ts);

	ret = synaptics_tcm_identify_chip(ts);
	if (ret) {
		dev_err(dev, "failed to get chip id");
		return ret;
	}
synaptics_tcm_get_fw_config(ts);
	ret = devm_request_threaded_irq(dev, ts->irq, NULL, synaptics_tcm_irq,
					  IRQF_ONESHOT, "synaptics-tcm", ts);
	if (ret) {
		dev_err(dev, "request threaded irq failed: %d\n", ret);
		return ret;
	}

	ret = synaptics_tcm_input_config(ts);
	if (ret) {
		dev_err(dev, "input device setup failed: %d\n", ret);
		return ret;
	}

	unsigned char cmd = 0x05;

	ret = ts->bus->write(ts->bus, &cmd, 1);

	return ret;
}
EXPORT_SYMBOL_GPL(synaptics_tcm_probe);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synaptics TCM SPI Touchscreen driver");
MODULE_AUTHOR("Luka Panio <lukapanio@gmail.com>");
