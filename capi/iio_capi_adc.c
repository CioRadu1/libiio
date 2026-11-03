/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * An ADC behind capi_adc as a table device: the table has its channels, the
 * values come from capi_adc_chan_read(), and a capture from
 * capi_adc_stream_start() when the driver has it.
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

/* A driver's capi_value as read_raw returns it. */
static int adc_value(const struct capi_value *v, int32_t *val, int32_t *val2)
{
	*val = v->val;
	*val2 = v->val2;

	switch (v->type) {
	case CAPI_VAL_INT:
		return IIO_CAPI_VAL_INT;
	case CAPI_VAL_INT_PLUS_MICRO:
		return IIO_CAPI_VAL_INT_PLUS_MICRO;
	case CAPI_VAL_INT_PLUS_NANO:
		return IIO_CAPI_VAL_INT_PLUS_NANO;
	case CAPI_VAL_FRACTIONAL:
		return IIO_CAPI_VAL_FRACTIONAL;
	case CAPI_VAL_FRACTIONAL_LOG2:
		return IIO_CAPI_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int adc_read_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		int32_t *val, int32_t *val2)
{
	const struct iio_capi_adc *adc = dev;
	unsigned int i = adc_chan_index(adc, chan);
	struct capi_value v;
	int ret;

	switch (info) {
	case IIO_CAPI_INFO_RAW:
		ret = adc_read(adc, i, val);
		return ret ? ret : IIO_CAPI_VAL_INT;
	case IIO_CAPI_INFO_SCALE:
		if (adc->drv_scale) {
			ret = capi_adc_chan_get_scale(adc->handles[i], &v);
			return ret ? ret : adc_value(&v, val, val2);
		}

		/* mV per code, as Linux gives it: vref / 2^resolution */
		*val = (int32_t)adc->vref_mv;
		*val2 = adc->configs[i].resolution;
		return IIO_CAPI_VAL_FRACTIONAL_LOG2;
	case IIO_CAPI_INFO_OFFSET:
		ret = capi_adc_chan_get_code_offset(adc->handles[i], &v);
		return ret ? ret : adc_value(&v, val, val2);
	case IIO_CAPI_INFO_SAMP_FREQ: {
		uint32_t hz;

		ret = capi_adc_chan_get_samples_hz(adc->handles[i], &hz);
		if (ret)
			return ret;

		*val = (int32_t)hz;
		return IIO_CAPI_VAL_INT;
	}
	default:
		return -EINVAL;
	}
}

static int adc_write_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		int32_t val, int32_t val2)
{
	struct iio_capi_adc *adc = dev;
	int ret;

	(void)val2;

	/* Only the sample rate is writable, as before there was write_raw. */
	if (info != IIO_CAPI_INFO_SAMP_FREQ)
		return -EPERM;
	if (val <= 0)
		return -EINVAL;

	ret = capi_adc_chan_set_samples_hz(adc->handles[adc_chan_index(adc, chan)], (uint32_t)val);
	if (ret)
		return ret;

	/* What is left of the last capture was taken at the old rate. */
	adc->stream_scans = 0;

	return 0;
}

static int adc_enable_buffer(void *dev, uint32_t mask, bool enable)
{
	struct iio_capi_adc *adc = dev;
	unsigned int i;

	adc->stream_nb_chans = 0;
	adc->stream_scans = 0;
	adc->stream_pos = 0;

	if (!enable)
		return 0;

	for (i = 0; i < adc->table.nb_channels; i++)
		if (mask & (1u << i))
			adc->stream_chans[adc->stream_nb_chans++] = adc->handles[i];

	return 0;
}

/* One scan from the capture, starting the next one when the last is used up. */
static int adc_stream_scan(struct iio_capi_adc *adc, int32_t *values)
{
	const uint32_t *scan;
	unsigned int k;
	int ret;

	if (!adc->stream_nb_chans)
		return -EINVAL;

	if (adc->stream_pos >= adc->stream_scans) {
		struct capi_adc_stream_config config = {
			.chans = adc->stream_chans,
			.nb_chans = adc->stream_nb_chans,
			.buffer = adc->stream_buf,
			.nb_scans = IIO_CAPI_ADC_STREAM_SAMPLES / adc->stream_nb_chans,
		};

		ret = capi_adc_stream_start(adc->stream_chans[0]->adc_handle, &config);
		if (ret)
			return ret;

		adc->stream_scans = config.nb_scans;
		adc->stream_pos = 0;
	}

	scan = &adc->stream_buf[adc->stream_pos++ * adc->stream_nb_chans];
	for (k = 0; k < adc->stream_nb_chans; k++)
		values[k] = (int32_t)scan[k];

	return 0;
}

static int adc_read_scan(void *dev, uint32_t mask, int32_t *values)
{
	struct iio_capi_adc *adc = dev;
	unsigned int i, k = 0;
	int ret;

	if (!adc->no_stream) {
		ret = adc_stream_scan(adc, values);
		if (ret != -ENOSYS)
			return ret;

		adc->no_stream = true;
	}

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
	.write_raw = adc_write_raw,
	.enable_buffer = adc_enable_buffer,
	.read_scan = adc_read_scan,
};

int iio_capi_adc_register(struct iio_capi_adc *adc, const char *name,
		struct capi_adc_chan_handle **chans, const struct capi_adc_chan_config *configs,
		unsigned int nb_chans, uint32_t vref_mv)
{
	uint32_t info = IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_RAW);
	struct capi_value v;
	unsigned int i;
	uint32_t hz;

	if (!adc || !name || !chans || !configs || !nb_chans ||
			nb_chans > IIO_CAPI_ADC_MAX_CHANNELS || !chans[0])
		return -EINVAL;

	/* The driver's own scale when it reports one, else vref / 2^resolution. */
	adc->drv_scale = capi_adc_chan_get_scale(chans[0], &v) != -ENOSYS;
	if (adc->drv_scale || vref_mv)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SCALE);
	if (capi_adc_chan_get_code_offset(chans[0], &v) != -ENOSYS)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_OFFSET);
	if (capi_adc_chan_get_samples_hz(chans[0], &hz) != -ENOSYS)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SAMP_FREQ);

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
	adc->stream_nb_chans = 0;
	adc->stream_scans = 0;
	adc->stream_pos = 0;
	adc->no_stream = false;

	adc->table.name = name;
	adc->table.dev = adc;
	adc->table.channels = adc->chans;
	adc->table.nb_channels = nb_chans;
	adc->table.ops = &adc_ops;

	return iio_capi_table_register(&adc->table);
}
