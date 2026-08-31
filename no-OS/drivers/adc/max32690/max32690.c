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

#define ADC_POLL_TIMEOUT 1000000

static const char *const max32690_channels[] = {
	"voltage0",
};

static int max32690_adc_init(void)
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

static int max32690_adc_read_raw(unsigned int channel, int *value)
{
	mxc_adc_slot_req_t slot_req;
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

	if (channel >= NO_OS_ARRAY_SIZE(max32690_channels))
		return -EINVAL;

	slot_req.channel = MXC_ADC_CH_0 + channel;

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

	timeout = ADC_POLL_TIMEOUT;
	while (!(MXC_ADC_GetFlags() & MXC_F_ADC_INTFL_SEQ_DONE)) {
		if (--timeout == 0) {
			ret = -ETIMEDOUT;
			goto out_disable;
		}
	}

	MXC_ADC_ClearFlags(MXC_F_ADC_INTFL_SEQ_DONE);

	ret = MXC_ADC_GetData(value);
	if (ret > 0)
		ret = 0;

	*value &= 0xFFF;

out_disable:
	MXC_ADC_DisableConversion();

out:
	return ret;
}

const struct iio_adc_hal iio_adc_hal = {
	.channels        = max32690_channels,
	.num_channels    = NO_OS_ARRAY_SIZE(max32690_channels),
	.resolution_bits = 12,
	.ref_voltage_mv  = 1250,
	.init            = max32690_adc_init,
	.read_raw        = max32690_adc_read_raw,
};
