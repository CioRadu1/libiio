/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <no_os_print_log.h>
#include <string.h>
#include <errno.h>
#include "iio_adc.h"
#include <iio/iio-backend.h>
#include "adc.h"

struct adc_channel_map
{
	const char *id;
	mxc_adc_chsel_t channel;
	bool is_temp;
};

static const struct adc_channel_map channel_map[] = {
	{"voltage0", MXC_ADC_CH_0, false},
};

#define NUM_CHANNELS (sizeof(channel_map) / sizeof(channel_map[0]))

static char scale_values[NUM_CHANNELS][32] = { "1" };

/* Timeout for ADC conversion polling (~10 ms at 100 MHz) */
#define ADC_POLL_TIMEOUT	1000000

static int adc_read_raw(mxc_adc_chsel_t channel, bool is_temp, int *value)
{
	mxc_adc_slot_req_t slot_req = {
		.channel = channel,
	};
	mxc_adc_conversion_req_t conv_req = {
		.mode = MXC_ADC_ATOMIC_CONV,
		.trig = MXC_ADC_TRIG_SOFTWARE,
		.fifo_format = MXC_ADC_DATA,
		.fifo_threshold = 0,
		.avg_number = MXC_ADC_AVG_1,
		.num_slots = 1,
	};
	volatile uint32_t timeout;
	int ret;

	if (is_temp)
		MXC_ADC_TS_SelectEnable();

	MXC_ADC_Clear_ChannelSelect();

	ret = MXC_ADC_SlotConfiguration(&slot_req, 0);
	if (ret)
		goto out;

	ret = MXC_ADC_Configuration(&conv_req);
	if (ret)
		goto out;

	ret = MXC_ADC_StartConversion();
	if (ret)
		goto out;

	/* Wait for sequence to complete with timeout */
	timeout = ADC_POLL_TIMEOUT;
	while (!(MXC_ADC_GetFlags() & MXC_F_ADC_INTFL_SEQ_DONE)) {
		if (--timeout == 0) {
			ret = -ETIMEDOUT;
			goto out_disable;
		}
	}

	/* Clear the sequence-done flag before reading to prevent stale state */
	MXC_ADC_ClearFlags(MXC_F_ADC_INTFL_SEQ_DONE);

	/* GetData returns number of FIFO entries read (1 = success) */
	ret = MXC_ADC_GetData(value);
	if (ret > 0)
		ret = 0;

	/* FIFO returns data + status bits; mask to 12-bit ADC result */
	*value &= 0xFFF;

out_disable:
	MXC_ADC_DisableConversion();

out:
	if (is_temp)
		MXC_ADC_TS_SelectDisable();

	return ret;
}

static const struct iio_data_format adc_fmt = {
	.length = 16,
	.bits = 12,
	.is_signed = false,
};

static int iio_adc_read_samples(void *dev, void *data, size_t bytes)
{
	size_t num_samples = bytes / sizeof(uint16_t);
	uint16_t *buffer = (uint16_t *)data;
	int raw;

	for (size_t i = 0; i < num_samples; i++)
	{
		int ret = adc_read_raw(MXC_ADC_CH_0, false, &raw);
		if (ret)
			return ret;
		buffer[i] = raw & 0xFFF;
	}

	return 0;
}

static int iio_adc_add_channels(void *dev, struct iio_device *iio_dev)
{
	struct iio_channel *ch;
	unsigned int i;

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		ch = iio_device_add_channel(iio_dev, (long)i,
									channel_map[i].id,
									NULL, NULL,
									false, true, &adc_fmt);
		if (!ch)
			return -ENOMEM;

		iio_channel_add_attr(ch, "raw", NULL);
		iio_channel_add_attr(ch, "scale", NULL);
	}

	return 0;
}

static int iio_adc_read_attr(void *dev,
							 const struct iio_device *iio_dev,
							 const struct iio_attr *attr,
							 char *dst, size_t len)
{
	const char *attr_name;
	const char *ch_id;
	unsigned int i;
	int raw_value;
	int ret;

	if (attr->type != IIO_ATTR_TYPE_CHANNEL || !attr->iio.chn)
		return -EINVAL;

	attr_name = iio_attr_get_name(attr);
	if (!attr_name)
		return -EINVAL;

	ch_id = iio_channel_get_id(attr->iio.chn);
	if (!ch_id)
		return -EINVAL;

	for (i = 0; i < NUM_CHANNELS; i++) {
		if (strcmp(ch_id, channel_map[i].id) != 0)
			continue;

		if (strcmp(attr_name, "scale") == 0)
			return snprintf(dst, len, "%s", scale_values[i]) + 1;

		if (strcmp(attr_name, "raw") == 0) {
			ret = adc_read_raw(channel_map[i].channel,
					   channel_map[i].is_temp,
					   &raw_value);
			if (ret)
				return ret;

			return snprintf(dst, len, "%d", raw_value) + 1;
		}

		return -EINVAL;
	}

	return -EINVAL;
}

static int iio_adc_write_attr(void *dev,
			      const struct iio_device *iio_dev,
			      const struct iio_attr *attr,
			      const char *src, size_t len)
{
	const char *attr_name;
	const char *ch_id;
	unsigned int i;

	if (attr->type != IIO_ATTR_TYPE_CHANNEL || !attr->iio.chn)
		return -EINVAL;

	attr_name = iio_attr_get_name(attr);
	if (!attr_name)
		return -EINVAL;

	ch_id = iio_channel_get_id(attr->iio.chn);
	if (!ch_id)
		return -EINVAL;

	for (i = 0; i < NUM_CHANNELS; i++) {
		if (strcmp(ch_id, channel_map[i].id) != 0)
			continue;

		if (strcmp(attr_name, "scale") == 0) {
			if (len >= sizeof(scale_values[i]))
				return -EINVAL;
			memcpy(scale_values[i], src, len);
			scale_values[i][len] = '\0';
			return len;
		}

		if (strcmp(attr_name, "raw") == 0)
			return -EPERM;

		return -EINVAL;
	}

	return -EINVAL;
}

int iio_adc_init(void)
{
	mxc_adc_req_t adc_cfg = {
		.clock = MXC_ADC_CLK_IBRO,
		.clkdiv = MXC_ADC_CLKDIV_4,
		.cal = MXC_ADC_SKIP_CAL,
		.ref = MXC_ADC_REF_INT_1V25,
		.trackCount = 4,
		.idleCount = 17,
	};

	return MXC_ADC_Init(&adc_cfg);
}

int iio_adc_get_device_info(struct noos_iio_device_info *info)
{
	if (!info)
		return -EINVAL;

	info->name = "max32690-adc";
	info->dev = NULL;
	info->add_channels = iio_adc_add_channels;
	info->read_attr = iio_adc_read_attr;
	info->write_attr = iio_adc_write_attr;
	info->read_samples = iio_adc_read_samples;

	return 0;
}
