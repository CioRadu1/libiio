/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <no_os_util.h>
#include "iio_adc_hal.h"
#include "adc.h"

static const char *const max32655_channels[] = {
	"voltage0",
};

static int max32655_adc_init(void)
{
	return MXC_ADC_Init();
}

static int max32655_adc_read_raw(unsigned int channel, int *value)
{
	int raw;

	if (channel >= NO_OS_ARRAY_SIZE(max32655_channels))
		return -EINVAL;

	raw = MXC_ADC_StartConversion(MXC_ADC_CH_0 + channel);
	if (raw < 0)
		return raw;

	*value = raw & 0x3FF;

	return 0;
}

const struct iio_adc_hal iio_adc_hal = {
	.channels        = max32655_channels,
	.num_channels    = NO_OS_ARRAY_SIZE(max32655_channels),
	.resolution_bits = 10,
	.ref_voltage_mv  = 1220,
	.init            = max32655_adc_init,
	.read_raw        = max32655_adc_read_raw,
};
