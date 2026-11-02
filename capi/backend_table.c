/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Serves a table device (iio_capi_table.h) through the capi: backend: makes
 * its libiio channels and attributes, writes and parses the text of its
 * values, and packs the scans read_scan / write_scan deal in into libiio's
 * blocks.
 */

#include <capi_alloc.h>
#include <errno.h>
#include <iio-private.h>
#include <iio/iio-backend.h>
#include <iio_capi_table.h>
#include <iio_device.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The masks have a bit per table channel. */
#define TABLE_MAX_CHANNELS 32

/* Long enough for a channel id: capacitance<int> */
#define TABLE_ID_LEN 32

/* The prefixes of the channel ids, as Linux names them. */
static const char *const chan_type_names[] = {
	[IIO_CAPI_VOLTAGE] = "voltage",
	[IIO_CAPI_CURRENT] = "current",
	[IIO_CAPI_POWER] = "power",
	[IIO_CAPI_TEMP] = "temp",
	[IIO_CAPI_ACCEL] = "accel",
	[IIO_CAPI_ANGL_VEL] = "anglvel",
	[IIO_CAPI_MAGN] = "magn",
	[IIO_CAPI_PRESSURE] = "pressure",
	[IIO_CAPI_HUMIDITY] = "humidityrelative",
	[IIO_CAPI_RESISTANCE] = "resistance",
	[IIO_CAPI_CAPACITANCE] = "capacitance",
	[IIO_CAPI_COUNT] = "count",
};

/* The channel attribute each value is, as Linux names it. */
static const char *const info_names[] = {
	[IIO_CAPI_INFO_RAW] = "raw",
	[IIO_CAPI_INFO_PROCESSED] = "input",
	[IIO_CAPI_INFO_SCALE] = "scale",
	[IIO_CAPI_INFO_OFFSET] = "offset",
	[IIO_CAPI_INFO_SAMP_FREQ] = "sampling_frequency",
};

#define ARRAY_LEN(_a) (sizeof(_a) / sizeof((_a)[0]))

static struct iio_capi_table_device *table_of(const struct iio_capi_device *dev)
{
	return (struct iio_capi_table_device *)dev->handle;
}

/* libiio sorts channels by index, so each keeps its place in the table,
 * plus one, in its pdata. */
static int table_chan_index(const struct iio_channel *chn)
{
	return (int)(uintptr_t)iio_channel_get_pdata(chn) - 1;
}

static bool is_scan_element(const struct iio_capi_chan *chan)
{
	return chan->scan_index >= 0;
}

/* --- channels and attributes --- */

static int table_add_channels(const struct iio_capi_device *dev, struct iio_device *iio_device)
{
	const struct iio_capi_table_device *tdev = table_of(dev);
	struct iio_data_format fmt;
	char id[TABLE_ID_LEN];
	struct iio_channel *chn;
	unsigned int i, info;
	int ret;

	for (i = 0; i < tdev->nb_channels; i++) {
		const struct iio_capi_chan *chan = &tdev->channels[i];

		snprintf(id, sizeof(id), "%s%d", chan_type_names[chan->type], chan->channel);

		fmt = (struct iio_data_format){
			.length = chan->scan.storage,
			.bits = chan->scan.bits,
			.shift = chan->scan.shift,
			.is_signed = chan->scan.is_signed,
			.is_be = chan->scan.is_big_endian,
		};

		/* Returns NULL, not an error pointer, when it fails. libiio
		 * ends a scan at the first channel with a negative index. */
		chn = iio_device_add_channel(iio_device,
				is_scan_element(chan) ? chan->scan_index : -1, id, NULL, NULL,
				chan->output, is_scan_element(chan), &fmt);
		if (!chn)
			return -ENOMEM;

		iio_channel_set_pdata(chn, (struct iio_channel_pdata *)(uintptr_t)(i + 1));

		for (info = 0; info < ARRAY_LEN(info_names); info++) {
			if (!(chan->info & IIO_CAPI_INFO_BIT(info)))
				continue;

			ret = iio_channel_add_attr(
					chn, info_names[info], IIO_ATTR_TYPE_CHANNEL, NULL);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/* The table channel and value a libiio attribute is. */
static int table_find(const struct iio_capi_table_device *tdev, const struct iio_attr *attr,
		const struct iio_capi_chan **chan, enum iio_capi_info *info)
{
	const char *name = iio_attr_get_name(attr);
	unsigned int i;
	int idx;

	if (attr->type != IIO_ATTR_TYPE_CHANNEL || !attr->iio.chn)
		return -ENOENT;

	idx = table_chan_index(attr->iio.chn);
	if (idx < 0 || (unsigned int)idx >= tdev->nb_channels)
		return -ENOENT;

	for (i = 0; i < ARRAY_LEN(info_names); i++) {
		if (!strcmp(name, info_names[i])) {
			*chan = &tdev->channels[idx];
			*info = (enum iio_capi_info)i;
			return 0;
		}
	}

	return -ENOENT;
}

/* --- values as text --- */

/* "<int>.<frac>", with frac of digits digits; the sign is taken from either
 * part, as Linux does for values between -1 and 0. */
static int format_fixed(char *dst, size_t len, int64_t whole, int64_t frac, unsigned int digits)
{
	bool neg = whole < 0 || frac < 0;

	if (whole < 0)
		whole = -whole;
	if (frac < 0)
		frac = -frac;

	return snprintf(dst, len, "%s%" PRId64 ".%0*" PRId64, neg ? "-" : "", whole, (int)digits,
			frac);
}

static int format_value(char *dst, size_t len, int fmt, int32_t val, int32_t val2)
{
	int64_t nano;

	switch (fmt) {
	case IIO_CAPI_VAL_INT:
		return snprintf(dst, len, "%" PRId32, val);
	case IIO_CAPI_VAL_INT_PLUS_MICRO:
		return format_fixed(dst, len, val, val2, 6);
	case IIO_CAPI_VAL_INT_PLUS_NANO:
		return format_fixed(dst, len, val, val2, 9);
	case IIO_CAPI_VAL_FRACTIONAL:
		if (!val2)
			return -EINVAL;
		nano = (int64_t)val * 1000000000 / val2;
		return format_fixed(dst, len, nano / 1000000000, nano % 1000000000, 9);
	case IIO_CAPI_VAL_FRACTIONAL_LOG2:
		if (val2 < 0 || val2 > 32)
			return -EINVAL;
		nano = ((int64_t)val * 1000000000) / ((int64_t)1 << val2);
		return format_fixed(dst, len, nano / 1000000000, nano % 1000000000, 9);
	default:
		return -EINVAL;
	}
}

/* "<int>[.<frac>]" into val + val2 / 1000000; "-0.5" is 0 and -500000. */
static int parse_value(const char *src, int32_t *val, int32_t *val2)
{
	const char *p = src;
	int32_t frac = 0, scale = 100000;
	bool neg = false;
	char *end;
	long whole;

	while (*p == ' ')
		p++;
	if (*p == '-')
		neg = true;

	whole = strtol(p, &end, 10);
	if (end == p || (end == p + 1 && neg))
		return -EINVAL;

	if (*end == '.') {
		for (end++; *end >= '0' && *end <= '9'; end++) {
			frac += (*end - '0') * scale;
			scale /= 10;
		}
	}

	while (*end == ' ' || *end == '\n')
		end++;
	if (*end)
		return -EINVAL;

	*val = (int32_t)whole;
	*val2 = (neg && !whole) ? -frac : frac;

	return 0;
}

static int table_read_attr(const struct iio_capi_device *dev, const struct iio_device *iio_device,
		const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_capi_table_device *tdev = table_of(dev);
	const struct iio_capi_chan *chan;
	enum iio_capi_info info;
	int32_t val = 0, val2 = 0;
	int ret;

	(void)iio_device;

	ret = table_find(tdev, attr, &chan, &info);
	if (ret)
		return ret;
	if (!tdev->ops->read_raw)
		return -ENOSYS;

	ret = tdev->ops->read_raw(tdev->dev, chan, info, &val, &val2);
	if (ret < 0)
		return ret;

	ret = format_value(dst, len, ret, val, val2);
	if (ret < 0)
		return ret;
	if ((size_t)ret >= len)
		return -ERANGE;

	/* With the NUL, as libiio wants it. */
	return ret + 1;
}

static int table_write_attr(const struct iio_capi_device *dev, const struct iio_device *iio_device,
		const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_capi_table_device *tdev = table_of(dev);
	const struct iio_capi_chan *chan;
	enum iio_capi_info info;
	int32_t val, val2;
	char *buf;
	int ret;

	(void)iio_device;

	ret = table_find(tdev, attr, &chan, &info);
	if (ret)
		return ret;
	if (!tdev->ops->write_raw)
		return -EPERM;

	len = strnlen(src, len);

	buf = capi_malloc(len + 1);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, src, len);
	buf[len] = '\0';

	ret = parse_value(buf, &val, &val2);
	capi_free(buf);
	if (ret)
		return ret;

	ret = tdev->ops->write_raw(tdev->dev, chan, info, val, val2);

	return ret < 0 ? ret : (int)len;
}

/* --- buffers --- */

/* The client's enabled channels as a table mask: bit n for channels[n]. */
static int table_mask(const struct iio_device *iio_device, const struct iio_channels_mask *mask,
		uint32_t *out)
{
	unsigned int i, nb = iio_device_get_channels_count(iio_device);
	int idx;

	*out = 0;

	for (i = 0; i < nb; i++) {
		const struct iio_channel *chn = iio_device_get_channel(iio_device, i);

		if (!iio_channel_is_scan_element(chn) || !iio_channel_is_enabled(chn, mask))
			continue;

		idx = table_chan_index(chn);
		if (idx < 0 || idx >= TABLE_MAX_CHANNELS)
			return -EINVAL;

		*out |= 1u << idx;
	}

	return *out ? 0 : -EINVAL;
}

/* Where each channel in mask sits in a scan, and the scan's size, by
 * libiio's rule: each sample aligned to its own size, the scan to the
 * largest one. */
static uint32_t scan_layout(
		const struct iio_capi_table_device *tdev, uint32_t mask, uint32_t *offsets)
{
	uint32_t size = 0, length, largest = 1;
	unsigned int i;

	for (i = 0; i < tdev->nb_channels; i++) {
		if (!(mask & (1u << i)))
			continue;

		length = tdev->channels[i].scan.storage / 8;
		if (length > largest)
			largest = length;

		size = (size + length - 1) / length * length;
		offsets[i] = size;
		size += length;
	}

	return (size + largest - 1) / largest * largest;
}

static void pack_sample(uint8_t *dst, const struct iio_capi_scan_format *scan, int32_t value)
{
	unsigned int i, n = scan->storage / 8;
	uint32_t raw = (uint32_t)value << scan->shift;

	for (i = 0; i < n; i++)
		dst[scan->is_big_endian ? n - 1 - i : i] = (uint8_t)(raw >> (8 * i));
}

static int32_t unpack_sample(const uint8_t *src, const struct iio_capi_scan_format *scan)
{
	unsigned int i, n = scan->storage / 8;
	uint32_t raw = 0;

	for (i = 0; i < n; i++)
		raw |= (uint32_t)src[scan->is_big_endian ? n - 1 - i : i] << (8 * i);

	raw >>= scan->shift;
	if (scan->bits < 32) {
		raw &= (1u << scan->bits) - 1;
		if (scan->is_signed && (raw & (1u << (scan->bits - 1))))
			raw |= ~((1u << scan->bits) - 1);
	}

	return (int32_t)raw;
}

/* Buffer on with mask, on the first transfer or when the channels
 * changed; off when the client disables it. */
static int table_set_enabled(struct iio_capi_table_device *tdev, bool enable, uint32_t mask)
{
	int ret = 0;

	if (enable && tdev->enabled && tdev->mask == mask)
		return 0;
	if (!enable && !tdev->enabled)
		return 0;

	if (tdev->ops->enable_buffer)
		ret = tdev->ops->enable_buffer(tdev->dev, mask, enable);
	if (ret)
		return ret;

	tdev->enabled = enable;
	tdev->mask = mask;

	return 0;
}

static int table_transfer(struct iio_capi_table_device *tdev, const struct iio_device *iio_device,
		const struct iio_channels_mask *mask, uint8_t *data, size_t bytes, bool output)
{
	uint32_t offsets[TABLE_MAX_CHANNELS];
	int32_t values[TABLE_MAX_CHANNELS];
	unsigned int i, k;
	uint32_t m, size;
	size_t pos;
	int ret;

	ret = table_mask(iio_device, mask, &m);
	if (ret)
		return ret;

	size = scan_layout(tdev, m, offsets);
	if (!size || iio_device_get_sample_size(iio_device, mask) != (ssize_t)size || bytes % size)
		return -EINVAL;

	ret = table_set_enabled(tdev, true, m);
	if (ret)
		return ret;

	for (pos = 0; pos < bytes; pos += size) {
		if (output) {
			for (i = 0, k = 0; i < tdev->nb_channels; i++)
				if (m & (1u << i))
					values[k++] = unpack_sample(data + pos + offsets[i],
							&tdev->channels[i].scan);

			ret = tdev->ops->write_scan(tdev->dev, m, values);
			if (ret)
				return ret;

			continue;
		}

		ret = tdev->ops->read_scan(tdev->dev, m, values);
		if (ret)
			return ret;

		/* The padding is not left as whatever the block held. */
		memset(data + pos, 0, size);

		for (i = 0, k = 0; i < tdev->nb_channels; i++)
			if (m & (1u << i))
				pack_sample(data + pos + offsets[i], &tdev->channels[i].scan,
						values[k++]);
	}

	return 0;
}

static int table_read_samples(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_channels_mask *mask,
		void *data, size_t bytes)
{
	return table_transfer(table_of(dev), iio_device, mask, data, bytes, false);
}

static int table_write_samples(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_channels_mask *mask,
		const void *data, size_t bytes)
{
	/* Only read: write_scan gets the unpacked values. */
	return table_transfer(table_of(dev), iio_device, mask, (uint8_t *)data, bytes, true);
}

/* On is done at the first transfer, which knows the channels. */
static int table_enable_buffer(const struct iio_capi_device *dev, bool enable)
{
	struct iio_capi_table_device *tdev = table_of(dev);

	if (enable)
		return 0;

	return table_set_enabled(tdev, false, tdev->mask);
}

/* --- registration --- */

static bool storage_ok(uint8_t storage)
{
	return storage == 8 || storage == 16 || storage == 32;
}

/* Whether the table can be served, and the direction of its buffer. */
static int table_check(const struct iio_capi_table_device *tdev, bool *has_scan, bool *output)
{
	int last = -1;
	unsigned int i;

	*has_scan = false;

	for (i = 0; i < tdev->nb_channels; i++) {
		const struct iio_capi_chan *chan = &tdev->channels[i];

		if ((unsigned int)chan->type >= ARRAY_LEN(chan_type_names) ||
				!chan_type_names[chan->type])
			return -EINVAL;

		if (!is_scan_element(chan))
			continue;

		/* libiio packs scans by index, the table by its order. One
		 * buffer has one direction. */
		if (chan->scan_index <= last || !storage_ok(chan->scan.storage) ||
				!chan->scan.bits || chan->scan.bits > chan->scan.storage ||
				(*has_scan && chan->output != *output))
			return -EINVAL;

		last = chan->scan_index;
		*output = chan->output;
		*has_scan = true;
	}

	return 0;
}

int iio_capi_table_register(struct iio_capi_table_device *tdev)
{
	bool has_scan, output = false, has_buffer;
	int ret;

	if (!tdev || !tdev->name || !tdev->ops || (tdev->nb_channels && !tdev->channels) ||
			tdev->nb_channels > TABLE_MAX_CHANNELS)
		return -EINVAL;

	ret = table_check(tdev, &has_scan, &output);
	if (ret)
		return ret;

	has_buffer = has_scan && (output ? !!tdev->ops->write_scan : !!tdev->ops->read_scan);

	tdev->api = (struct iio_capi_device_api){
		.add_channels = table_add_channels,
		.read_attr = table_read_attr,
		.write_attr = table_write_attr,
		.read_samples = has_buffer && !output ? table_read_samples : NULL,
		.write_samples = has_buffer && output ? table_write_samples : NULL,
		.enable_buffer = has_buffer ? table_enable_buffer : NULL,
	};

	/* Field by field: iio_capi_device_register() owns next. */
	tdev->iio.name = tdev->name;
	tdev->iio.handle = tdev;
	tdev->iio.api = &tdev->api;
	tdev->iio.output = output;

	tdev->enabled = false;
	tdev->mask = 0;

	return iio_capi_device_register(&tdev->iio);
}
