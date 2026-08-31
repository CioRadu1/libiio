/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include "iio_adc_hal.h"
#include "adc.h"

struct adc_channel_map {
	const char *id;
	mxc_adc_chsel_t channel;
};

static const struct adc_channel_map channel_map[] = {
	{"voltage0", MXC_ADC_CH_0},
};

#define NUM_CHANNELS (sizeof(channel_map) / sizeof(channel_map[0]))

unsigned int iio_adc_hal_num_channels(void)
{
	return NUM_CHANNELS;
}

const char *iio_adc_hal_channel_id(unsigned int channel)
{
	if (channel >= NUM_CHANNELS)
		return NULL;

	return channel_map[channel].id;
}

unsigned int iio_adc_hal_resolution_bits(void)
{
	return 10;
}

int iio_adc_hal_ref_voltage_mv(void)
{
	return 1220;
}

int iio_adc_hal_read_raw(unsigned int channel, int *value)
{
	int raw;

	if (channel >= NUM_CHANNELS)
		return -EINVAL;

	raw = MXC_ADC_StartConversion(channel_map[channel].channel);
	if (raw < 0)
		return raw;

	*value = raw & 0x3FF;

	return 0;
}

int iio_adc_hal_init(void)
{
	return MXC_ADC_Init();
}
