/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <iio/iio-backend.h>
#include <iio_capi_adc.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *const raw_name = "raw";
static const char *const scale_name = "scale";

static bool iio_capi_adc_has_scale(const struct iio_capi_adc_channel *channel)
{
	return channel->resolution != 0 && channel->vref_mv != 0;
}

static int iio_capi_adc_add_channels(
		const struct iio_capi_device *dev, struct iio_device *iio_device)
{
	const struct iio_capi_adc *adc = dev->handle;
	struct iio_channel *iio_channel;
	enum iio_attr_type type = IIO_ATTR_TYPE_CHANNEL;
	size_t index;

	for (index = 0; index < adc->num_channels; index++) {
		const struct iio_capi_adc_channel *channel = &adc->channels[index];
		const struct iio_data_format fmt = {
			.length = channel->resolution > 16 ? 32 : 16,
			.bits = channel->resolution,
			.is_signed = false,
		};

		if (!channel->id || !channel->handle)
			return -EINVAL;

		iio_channel = iio_device_add_channel(iio_device, (long)index, channel->id, NULL,
				NULL, false, true, &fmt);
		if (iio_err(iio_channel))
			return iio_err(iio_channel);

		iio_channel_set_pdata(iio_channel, (struct iio_channel_pdata *)(uintptr_t)index);

		if (iio_channel_add_attr(iio_channel, raw_name, type, NULL))
			return -EINVAL;

		if (iio_capi_adc_has_scale(channel) &&
				iio_channel_add_attr(iio_channel, scale_name, type, NULL))
			return -EINVAL;
	}

	return 0;
}

static int iio_capi_adc_read_raw(const struct iio_capi_adc_channel *channel, char *dst, size_t len)
{
	/* capi_adc_chan_read() takes a void *: drivers fill a uint32_t. */
	uint32_t val = 0;
	int ret;

	ret = capi_adc_chan_read(channel->handle, &val);
	if (ret)
		return ret;

	return snprintf(dst, len, "%" PRIu32, val) + 1;
}

static int iio_capi_adc_read_scale(
		const struct iio_capi_adc_channel *channel, char *dst, size_t len)
{
	/* mV per LSB, in millionths */
	uint64_t scale = ((uint64_t)channel->vref_mv * 1000000u) >> channel->resolution;

	return snprintf(dst, len, "%" PRIu32 ".%06" PRIu32, (uint32_t)(scale / 1000000u),
			       (uint32_t)(scale % 1000000u)) +
	       1;
}

static int iio_capi_adc_read_attr(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr, char *dst,
		size_t len)
{
	const struct iio_capi_adc *adc = dev->handle;
	const struct iio_capi_adc_channel *channel;
	size_t index;

	(void)iio_device;

	if (attr->type != IIO_ATTR_TYPE_CHANNEL)
		return -EINVAL;

	index = (uintptr_t)iio_channel_get_pdata(attr->iio.chn);
	if (index >= adc->num_channels)
		return -EINVAL;

	channel = &adc->channels[index];

	if (!strcmp(attr->name, raw_name))
		return iio_capi_adc_read_raw(channel, dst, len);

	if (!strcmp(attr->name, scale_name) && iio_capi_adc_has_scale(channel))
		return iio_capi_adc_read_scale(channel, dst, len);

	return -EINVAL;
}

/*
 * Whole scans, each enabled channel in index order, every sample aligned
 * to its own storage size, as libiio lays them out. capi_adc reads one
 * channel at a time, so the samples of a scan are not simultaneous, and it
 * has no timing of its own: scans come as fast as the reads return.
 */
static int iio_capi_adc_read_samples(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_channels_mask *mask,
		void *data, size_t bytes)
{
	const struct iio_capi_adc *adc = dev->handle;
	ssize_t sample_size;
	uint8_t *scan = data;
	size_t nb_scans, n, index, offset;
	uint32_t val;
	int ret;

	sample_size = iio_device_get_sample_size(iio_device, mask);
	if (sample_size <= 0)
		return sample_size ? (int)sample_size : -EINVAL;

	if (bytes % (size_t)sample_size)
		return -EINVAL;

	nb_scans = bytes / (size_t)sample_size;

	for (n = 0; n < nb_scans; n++, scan += sample_size) {
		offset = 0;

		for (index = 0; index < adc->num_channels; index++) {
			const struct iio_channel *chn = iio_device_get_channel(iio_device, index);
			size_t storage;

			if (!iio_channel_is_enabled(chn, mask))
				continue;

			storage = iio_channel_get_data_format(chn)->length / 8;
			offset = (offset + storage - 1) / storage * storage;

			ret = capi_adc_chan_read(adc->channels[index].handle, &val);
			if (ret)
				return ret;

			if (storage == sizeof(uint16_t)) {
				uint16_t val16 = (uint16_t)val;

				memcpy(scan + offset, &val16, storage);
			} else {
				memcpy(scan + offset, &val, storage);
			}

			offset += storage;
		}
	}

	return 0;
}

const struct iio_capi_device_api iio_capi_adc_api = {
	.add_channels = iio_capi_adc_add_channels,
	.read_attr = iio_capi_adc_read_attr,
	.read_samples = iio_capi_adc_read_samples,
};
