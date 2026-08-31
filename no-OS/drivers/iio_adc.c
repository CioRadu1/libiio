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

struct adc_channel_map {
	const char *id;
	mxc_adc_chsel_t channel;
	bool is_temp;
};

static const struct adc_channel_map channel_map[] = {
	{ "voltage0", MXC_ADC_CH_0, false },
};

#define NUM_CHANNELS	(sizeof(channel_map) / sizeof(channel_map[0]))

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

	/* Wait for sequence to complete - StartConversion is non-blocking */
	while (!(MXC_ADC_GetFlags() & MXC_F_ADC_INTFL_SEQ_DONE))
		;

	/* GetData returns number of FIFO entries read (1 = success) */
	ret = MXC_ADC_GetData(value);
	if (ret > 0)
		ret = 0;

	MXC_ADC_DisableConversion();

out:
	if (is_temp)
		MXC_ADC_TS_SelectDisable();

	return ret;
}

static int iio_adc_add_channels(void *dev, struct iio_device *iio_dev)
{
	struct iio_channel *ch;
	unsigned int i;

	for (i = 0; i < NUM_CHANNELS; i++) {
		ch = iio_device_add_channel(iio_dev, (long)i,
					    channel_map[i].id,
					    NULL, NULL,
					    false, false, NULL);
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

	if (strcmp(attr_name, "scale") == 0)
		return snprintf(dst, len, "1") + 1;

	if (strcmp(attr_name, "raw") != 0)
		return -EINVAL;

	ch_id = iio_channel_get_id(attr->iio.chn);
	if (!ch_id)
		return -EINVAL;

	for (i = 0; i < NUM_CHANNELS; i++) {
		if (strcmp(ch_id, channel_map[i].id) == 0) {
			ret = adc_read_raw(channel_map[i].channel,
					   channel_map[i].is_temp,
					   &raw_value);
			if (ret)
				return ret;

			return snprintf(dst, len, "%d", raw_value) + 1;
		}
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
	info->write_attr = NULL;

	return 0;
}
