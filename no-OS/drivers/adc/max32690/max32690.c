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
	bool is_temp;
};

static const struct adc_channel_map channel_map[] = {
	{"voltage0", MXC_ADC_CH_0, false},
};

#define NUM_CHANNELS (sizeof(channel_map) / sizeof(channel_map[0]))

#define ADC_POLL_TIMEOUT 1000000

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

int iio_adc_hal_read_raw(unsigned int channel, int *value)
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
	bool is_temp;
	int ret;

	if (channel >= NUM_CHANNELS)
		return -EINVAL;

	slot_req.channel = channel_map[channel].channel;
	is_temp = channel_map[channel].is_temp;

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
	if (is_temp)
		MXC_ADC_TS_SelectDisable();

	return ret;
}

int iio_adc_hal_init(void)
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
