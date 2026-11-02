/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * An ADC behind capi_adc as a table device: the table has its channels, the
 * values come from capi_adc_chan_read().
 */

#include <capi_adc.h>
#include <errno.h>
#include <iio_capi_adc.h>
#include <iio_capi_table.h>
#include <stdbool.h>
#include <stdint.h>

static unsigned int adc_chan_index(const struct iio_capi_adc *adc, const struct iio_capi_chan *chan)
{
	return (unsigned int)(chan - adc->chans);
}

static int adc_read(const struct iio_capi_adc *adc, unsigned int i, int32_t *val)
{
	/* What capi_adc_chan_read() implementations write. */
	uint32_t raw;
	int ret;

	ret = capi_adc_chan_read(adc->handles[i], &raw);
	if (ret)
		return ret;

	*val = (int32_t)raw;

	return 0;
}

static int adc_read_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		int32_t *val, int32_t *val2)
{
	const struct iio_capi_adc *adc = dev;
	unsigned int i = adc_chan_index(adc, chan);
	int ret;

	switch (info) {
	case IIO_CAPI_INFO_RAW:
		ret = adc_read(adc, i, val);
		return ret ? ret : IIO_CAPI_VAL_INT;
	case IIO_CAPI_INFO_SCALE:
		/* mV per code, as Linux gives it: vref / 2^resolution */
		*val = (int32_t)adc->vref_mv;
		*val2 = adc->configs[i].resolution;
		return IIO_CAPI_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int adc_read_scan(void *dev, uint32_t mask, int32_t *values)
{
	const struct iio_capi_adc *adc = dev;
	unsigned int i, k = 0;
	int ret;

	for (i = 0; i < adc->table.nb_channels; i++) {
		if (!(mask & (1u << i)))
			continue;

		ret = adc_read(adc, i, &values[k++]);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct iio_capi_table_ops adc_ops = {
	.read_raw = adc_read_raw,
	.read_scan = adc_read_scan,
};

int iio_capi_adc_register(struct iio_capi_adc *adc, const char *name,
		struct capi_adc_chan_handle **chans, const struct capi_adc_chan_config *configs,
		unsigned int nb_chans, uint32_t vref_mv)
{
	uint32_t info = IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_RAW);
	unsigned int i;

	if (!adc || !name || !chans || !configs || !nb_chans ||
			nb_chans > IIO_CAPI_ADC_MAX_CHANNELS)
		return -EINVAL;

	if (vref_mv)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SCALE);

	for (i = 0; i < nb_chans; i++) {
		uint8_t bits = configs[i].resolution;

		if (!chans[i] || !bits || bits > 32)
			return -EINVAL;

		adc->chans[i] = (struct iio_capi_chan){
			.type = IIO_CAPI_VOLTAGE,
			.channel = (int)configs[i].chan_id,
			.info = info,
			.scan_index = (int)i,
			.scan = {
				.bits = bits,
				.storage = bits <= 16 ? 16 : 32,
			},
		};
	}

	adc->handles = chans;
	adc->configs = configs;
	adc->vref_mv = vref_mv;

	adc->table.name = name;
	adc->table.dev = adc;
	adc->table.channels = adc->chans;
	adc->table.nb_channels = nb_chans;
	adc->table.ops = &adc_ops;

	return iio_capi_table_register(&adc->table);
}
