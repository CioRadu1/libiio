/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * An ADC behind capi_adc as a table device: the table has its channels, the
 * values come from capi_adc_chan_read() and the other capi_adc calls, and a
 * capture from the driver's stream (capi_adc_read_samples()) when it has one.
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

/* A code as the driver gives it (capi_adc_chan_read(), a stream's buffer),
 * sign extended when the channel's codes are signed. */
static int32_t adc_code(const struct capi_adc_chan_handle *h, uint32_t raw)
{
	unsigned int bits = h->resolution;

	if (!h->is_signed || bits >= 32)
		return (int32_t)raw;

	return (int32_t)(raw << (32 - bits)) >> (32 - bits);
}

static int adc_read(const struct iio_capi_adc *adc, unsigned int i, int32_t *val)
{
	/* What capi_adc_chan_read() implementations write. */
	uint32_t raw;
	int ret;

	ret = capi_adc_chan_read(adc->handles[i], &raw);
	if (ret)
		return ret;

	*val = adc_code(adc->handles[i], raw);

	return 0;
}

static int adc_read_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		int32_t *val, int32_t *val2)
{
	const struct iio_capi_adc *adc = dev;
	struct capi_adc_chan_handle *h = adc->handles[adc_chan_index(adc, chan)];
	struct capi_value v;
	int ret;

	switch (info) {
	case IIO_CAPI_INFO_RAW:
		ret = adc_read(adc, adc_chan_index(adc, chan), val);
		return ret ? ret : IIO_CAPI_VAL_INT;
	case IIO_CAPI_INFO_SCALE:
		/* mV per code, as the driver states it */
		ret = capi_adc_chan_get_scale(h, &v);
		break;
	case IIO_CAPI_INFO_OFFSET:
		ret = capi_adc_chan_get_code_offset(h, &v);
		break;
	case IIO_CAPI_INFO_SAMP_FREQ:
		/* Hz, as the driver states it */
		ret = capi_adc_chan_get_samples_hz(h, &v);
		break;
	default:
		return -EINVAL;
	}

	return ret ? ret : iio_capi_value(&v, val, val2);
}

/* A value the driver has no setter for is read-only. */
static int adc_write_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		const struct capi_value *value)
{
	const struct iio_capi_adc *adc = dev;
	struct capi_adc_chan_handle *h = adc->handles[adc_chan_index(adc, chan)];
	const struct capi_adc_ops *ops = h->adc_handle->adc_ops;

	switch (info) {
	case IIO_CAPI_INFO_SAMP_FREQ:
		return ops->chan_set_samples_hz ? capi_adc_chan_set_samples_hz(h, value) : -EPERM;
	case IIO_CAPI_INFO_SCALE:
		return ops->chan_set_scale ? capi_adc_chan_set_scale(h, value) : -EPERM;
	default:
		return -EPERM;
	}
}

static int adc_read_avail(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		struct capi_value *values, uint32_t *nb)
{
	const struct iio_capi_adc *adc = dev;
	struct capi_adc_chan_handle *h = adc->handles[adc_chan_index(adc, chan)];

	switch (info) {
	case IIO_CAPI_INFO_SCALE:
		return capi_adc_chan_get_scale_available(h, values, nb);
	case IIO_CAPI_INFO_SAMP_FREQ:
		return capi_adc_chan_get_samples_hz_available(h, values, nb);
	default:
		return -EINVAL;
	}
}

/* A scan without a stream: a capi_adc_chan_read() of each channel in mask,
 * as fast as the buffer asks. */
static int adc_read_scan(void *dev, uint32_t mask, int32_t *values)
{
	struct iio_capi_adc *adc = dev;
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

/* The stream of the channels in mask, in table order: the order of a
 * scan's values. */
static int adc_stream_enable(struct iio_capi_adc *adc, uint32_t mask)
{
	unsigned int i;

	adc->stream_nb_chans = 0;
	for (i = 0; i < adc->table.nb_channels; i++)
		if (mask & (1u << i))
			adc->stream_chans[adc->stream_nb_chans++] = adc->handles[i];

	return capi_adc_stream_enable(adc->handles[0]->adc_handle, adc->stream_chans,
			adc->stream_nb_chans);
}

static int adc_enable_buffer(void *dev, uint32_t mask, bool enable)
{
	struct iio_capi_adc *adc = dev;

	return enable ? adc_stream_enable(adc, mask) :
			capi_adc_stream_disable(adc->handles[0]->adc_handle);
}

/* nb_scans scans of the stream, its codes as the channels lay them out. */
static int adc_read_stream(struct iio_capi_adc *adc, int32_t *values, uint32_t nb_scans)
{
	uint32_t i, n = nb_scans * adc->stream_nb_chans;
	int ret;

	/* The codes land in values, then are sign extended in place. */
	ret = capi_adc_read_samples(adc->handles[0]->adc_handle, (uint32_t *)values, nb_scans);
	if (ret)
		return ret;

	for (i = 0; i < n; i++)
		values[i] = adc_code(adc->stream_chans[i % adc->stream_nb_chans],
				(uint32_t)values[i]);

	return 0;
}

static int adc_read_scans(void *dev, uint32_t mask, int32_t *values, uint32_t nb_scans)
{
	struct iio_capi_adc *adc = dev;
	int ret;

	ret = adc_read_stream(adc, values, nb_scans);
	if (ret != -EOVERFLOW)
		return ret;

	/* Scans were lost, mostly while the previous block was being sent:
	 * these start a new stream. Lost again, the ADC is faster than it can
	 * be read. */
	ret = adc_stream_enable(adc, mask);
	if (ret)
		return ret;

	return adc_read_stream(adc, values, nb_scans);
}

/* direct_reg_access: the ADC's registers */
static int adc_reg_access(void *dev, uint32_t reg, uint32_t writeval, uint32_t *readval)
{
	struct iio_capi_adc *adc = dev;

	return capi_adc_reg_access(adc->handles[0]->adc_handle, reg, writeval, readval);
}

int iio_capi_adc_init(struct iio_capi_adc *adc, struct capi_adc_chan_handle **chans,
		const struct iio_capi_chan_desc *descs, unsigned int nb_chans)
{
	uint32_t info = IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_RAW), avail = 0;
	const struct capi_adc_ops *ops;
	unsigned int i;

	if (!adc || !chans || !nb_chans || nb_chans > IIO_CAPI_ADC_MAX_CHANNELS ||
			!chans[0] || !chans[0]->adc_handle || !chans[0]->adc_handle->adc_ops)
		return -EINVAL;

	/* What the driver has, from its ops: the channels are one ADC's. */
	ops = chans[0]->adc_handle->adc_ops;
	if (ops->chan_get_scale)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SCALE);
	if (ops->chan_get_code_offset)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_OFFSET);
	if (ops->chan_get_samples_hz)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SAMP_FREQ);

	/* A list of the values a writable one takes */
	if (ops->chan_get_scale && ops->chan_set_scale && ops->chan_get_scale_available)
		avail |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SCALE);
	if (ops->chan_get_samples_hz && ops->chan_set_samples_hz &&
			ops->chan_get_samples_hz_available)
		avail |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SAMP_FREQ);

	for (i = 0; i < nb_chans; i++) {
		const struct capi_adc_chan_handle *h = chans[i];

		if (!h || h->adc_handle != chans[0]->adc_handle || !h->resolution ||
				h->resolution + h->shift > 32)
			return -EINVAL;

		adc->chans[i] = (struct iio_capi_chan){
			.type = IIO_CAPI_VOLTAGE,
			.channel = (int)h->chan_id,
			.info = info,
			.info_avail = avail,
			.scan_index = (int)i,
			/* as the driver says its codes are laid out */
			.scan = iio_capi_scan_of(h->resolution, h->is_signed, h->shift,
					h->big_endian),
		};
		if (descs)
			iio_capi_chan_describe(&adc->chans[i], &descs[i]);
	}

	adc->handles = chans;
	adc->stream_nb_chans = 0;

	adc->table.name = NULL;
	adc->table.dev = adc;
	adc->table.channels = adc->chans;
	adc->table.nb_channels = nb_chans;
	adc->ops = (struct iio_capi_table_ops){
		.read_raw = adc_read_raw,
		.write_raw = adc_write_raw,
		.read_avail = adc_read_avail,
		.read_scan = adc_read_scan,
	};
	if (ops->stream_enable && ops->stream_disable && ops->read_samples) {
		adc->ops.read_scans = adc_read_scans;
		adc->ops.enable_buffer = adc_enable_buffer;
	}
	if (ops->reg_access)
		adc->ops.reg_access = adc_reg_access;
	adc->table.ops = &adc->ops;
	adc->table.info = 0;
	adc->table.info_avail = 0;
	adc->table.ext = NULL;

	return 0;
}

int iio_capi_adc_register(struct iio_capi_adc *adc, const char *name,
		struct capi_adc_chan_handle **chans, const struct iio_capi_chan_desc *descs,
		unsigned int nb_chans, const struct iio_capi_ext *ext)
{
	int ret;

	if (!name)
		return -EINVAL;

	ret = iio_capi_adc_init(adc, chans, descs, nb_chans);
	if (ret)
		return ret;

	adc->table.name = name;
	adc->table.ext = ext;

	return iio_capi_table_register(&adc->table);
}
