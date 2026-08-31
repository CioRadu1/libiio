/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include "iio_adc_hal.h"

static const uint16_t sine_lut[32] = {
	0x8000, 0x98f8, 0xb0fb, 0xc71c, 0xda82, 0xea6d, 0xf641, 0xfd89,
	0xffff, 0xfd89, 0xf641, 0xea6d, 0xda82, 0xc71c, 0xb0fb, 0x98f8,
	0x8000, 0x6707, 0x4f04, 0x38e3, 0x257d, 0x1592, 0x09be, 0x0276,
	0x0000, 0x0276, 0x09be, 0x1592, 0x257d, 0x38e3, 0x4f04, 0x6707,
};

#define NUM_CHANNELS 2

static const char *const channel_ids[NUM_CHANNELS] = {
	"voltage0",
	"voltage1",
};

static unsigned int lut_index[NUM_CHANNELS];

unsigned int iio_adc_hal_num_channels(void)
{
	return NUM_CHANNELS;
}

const char *iio_adc_hal_channel_id(unsigned int channel)
{
	if (channel >= NUM_CHANNELS)
		return NULL;

	return channel_ids[channel];
}

unsigned int iio_adc_hal_resolution_bits(void)
{
	return 16;
}

int iio_adc_hal_ref_voltage_mv(void)
{
	return 2500;
}

int iio_adc_hal_read_raw(unsigned int channel, int *value)
{
	if (channel >= NUM_CHANNELS)
		return -EINVAL;

	*value = sine_lut[lut_index[channel]];
	lut_index[channel] = (lut_index[channel] + 1) & 0x1F;

	return 0;
}

int iio_adc_hal_init(void)
{
	unsigned int i;

	for (i = 0; i < NUM_CHANNELS; i++)
		lut_index[i] = 0;

	return 0;
}
