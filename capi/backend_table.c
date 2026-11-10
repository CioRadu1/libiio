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

/* Values read_scans fills at once: a block is read in chunks of as many
 * scans as fit. */
#define TABLE_CHUNK_VALUES 192

/* Long enough for a channel id: a differential one of the longest type,
 * humidityrelative<int>-humidityrelative<int> */
#define TABLE_ID_LEN 64

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
	[IIO_CAPI_ALTVOLTAGE] = "altvoltage",
	[IIO_CAPI_INCLI] = "incli",
	[IIO_CAPI_VELOCITY] = "velocity",
	[IIO_CAPI_ANGL] = "angl",
	[IIO_CAPI_ROT] = "rot",
	[IIO_CAPI_DELTA_ANGL] = "deltaangl",
	[IIO_CAPI_DELTA_VELOCITY] = "deltavelocity",
	[IIO_CAPI_PHASE] = "phase",
	/* no-OS's; Linux has no weight type */
	[IIO_CAPI_WEIGHT] = "weight",
	[IIO_CAPI_FLAGS] = "flags",
};

/* The axis suffixes of the channel ids, as IIO names them. */
static const char *const chan_modifier_names[] = {
	[IIO_CAPI_MOD_NONE] = NULL,
	[IIO_CAPI_MOD_X] = "x",
	[IIO_CAPI_MOD_Y] = "y",
	[IIO_CAPI_MOD_Z] = "z",
	[IIO_CAPI_MOD_TEMP_AMBIENT] = "ambient",
	[IIO_CAPI_MOD_TEMP_OBJECT] = "object",
	[IIO_CAPI_MOD_PITCH] = "pitch",
	[IIO_CAPI_MOD_YAW] = "yaw",
	[IIO_CAPI_MOD_ROLL] = "roll",
	[IIO_CAPI_MOD_I] = "i",
	[IIO_CAPI_MOD_Q] = "q",
};

/* The channel attribute each value is, as Linux names it. */
static const char *const info_names[] = {
	[IIO_CAPI_INFO_RAW] = "raw",
	[IIO_CAPI_INFO_PROCESSED] = "input",
	[IIO_CAPI_INFO_SCALE] = "scale",
	[IIO_CAPI_INFO_OFFSET] = "offset",
	[IIO_CAPI_INFO_SAMP_FREQ] = "sampling_frequency",
	[IIO_CAPI_INFO_CALIBBIAS] = "calibbias",
	[IIO_CAPI_INFO_HPF_3DB] = "filter_high_pass_3db_frequency",
	[IIO_CAPI_INFO_OVERSAMPLING_RATIO] = "oversampling_ratio",
	[IIO_CAPI_INFO_THERMOCOUPLE_TYPE] = "thermocouple_type",
	[IIO_CAPI_INFO_CALIBSCALE] = "calibscale",
	[IIO_CAPI_INFO_CALIBPHASE] = "calibphase",
	[IIO_CAPI_INFO_LPF_3DB] = "filter_low_pass_3db_frequency",
	[IIO_CAPI_INFO_FREQUENCY] = "frequency",
	[IIO_CAPI_INFO_PHASE] = "phase",
	[IIO_CAPI_INFO_HARDWAREGAIN] = "hardwaregain",
	[IIO_CAPI_INFO_POWERDOWN] = "powerdown",
	[IIO_CAPI_INFO_EN] = "en",
};

/* The suffix of the attribute that lists a value's values. */
#define TABLE_AVAIL_SUFFIX "_available"

/* Long enough for an attribute name: filter_high_pass_3db_frequency_available */
#define TABLE_ATTR_LEN 48

/* Values an _available attribute lists at most. */
#define TABLE_AVAIL_MAX 16

#define ARRAY_LEN(_a) (sizeof(_a) / sizeof((_a)[0]))

/* chan_types has a bit per type, and all_types needs one more. */
_Static_assert(ARRAY_LEN(chan_type_names) < 32, "too many channel types for chan_types");

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

/* The channel's id, as IIO names a channel: its type; with its number when
 * it is numbered, then its modifier; or the two inputs of a differential
 * one (voltage0-voltage1). */
static void chan_id(const struct iio_capi_chan *chan, char *id, size_t len)
{
	const char *type = chan_type_names[chan->type];

	if (chan->differential)
		snprintf(id, len, "%s%d-%s%d", type, chan->channel, type, chan->channel2);
	else if (chan->modifier != IIO_CAPI_MOD_NONE && chan->channel >= 0)
		snprintf(id, len, "%s%d_%s", type, chan->channel,
				chan_modifier_names[chan->modifier]);
	else if (chan->modifier != IIO_CAPI_MOD_NONE)
		snprintf(id, len, "%s_%s", type, chan_modifier_names[chan->modifier]);
	else if (chan->channel < 0)
		snprintf(id, len, "%s", type);
	else
		snprintf(id, len, "%s%d", type, chan->channel);
}

/* Whether the part's own attribute a is one of scope's: for a channel one,
 * on chan, a table channel. */
static bool ext_is_on(const struct iio_capi_attr *a, enum iio_capi_attr_scope scope,
		const struct iio_capi_chan *chan)
{
	if (a->scope != scope)
		return false;

	return scope != IIO_CAPI_ATTR_CHANNEL || !a->chan_types ||
	       (a->chan_types & IIO_CAPI_CHAN_TYPE_BIT(chan->type));
}

/* Whether name is "<base>_available". */
static bool is_avail_of(const char *name, const char *base)
{
	size_t n = strlen(base);

	return !strncmp(name, base, n) && !strcmp(name + n, TABLE_AVAIL_SUFFIX);
}

/* An attribute named name where scope's go: on chn, the device, or buf. */
static int ext_add_one(struct iio_device *iio_device, struct iio_channel *chn,
		struct iio_buffer *buf, enum iio_capi_attr_scope scope, const char *name)
{
	switch (scope) {
	case IIO_CAPI_ATTR_CHANNEL:
		return iio_channel_add_attr(chn, name, IIO_ATTR_TYPE_CHANNEL, NULL);
	case IIO_CAPI_ATTR_BUFFER:
		return iio_buffer_add_attr(buf, name);
	case IIO_CAPI_ATTR_DEBUG:
		return iio_device_add_attr(iio_device, name, IIO_ATTR_TYPE_DEBUG);
	default:
		return iio_device_add_attr(iio_device, name, IIO_ATTR_TYPE_DEVICE);
	}
}

/* The part's own attributes of scope, after the values, each followed by
 * its _available list when it has one: on chan, as chn, for a channel;
 * on buf for the buffer; on the device else. */
static int table_add_ext(const struct iio_capi_table_device *tdev, struct iio_device *iio_device,
		enum iio_capi_attr_scope scope, const struct iio_capi_chan *chan,
		struct iio_channel *chn, struct iio_buffer *buf)
{
	char name[TABLE_ATTR_LEN];
	unsigned int i;
	int ret;

	if (!tdev->ext)
		return 0;

	for (i = 0; i < tdev->ext->nb_attrs; i++) {
		const struct iio_capi_attr *a = &tdev->ext->attrs[i];

		if (!ext_is_on(a, scope, chan))
			continue;

		ret = ext_add_one(iio_device, chn, buf, scope, a->name);
		if (ret)
			return ret;

		if (!a->read_avail)
			continue;

		/* libiio copies the name; table_check_ext() made sure it fits */
		snprintf(name, sizeof(name), "%s" TABLE_AVAIL_SUFFIX, a->name);
		ret = ext_add_one(iio_device, chn, buf, scope, name);
		if (ret)
			return ret;
	}

	return 0;
}

static int table_add_buffer_attrs(const struct iio_capi_device *dev, struct iio_buffer *buf)
{
	return table_add_ext(table_of(dev), NULL, IIO_CAPI_ATTR_BUFFER, NULL, NULL, buf);
}

static int table_add_channels(const struct iio_capi_device *dev, struct iio_device *iio_device)
{
	const struct iio_capi_table_device *tdev = table_of(dev);
	struct iio_data_format fmt;
	char id[TABLE_ID_LEN], attr[TABLE_ATTR_LEN];
	struct iio_channel *chn;
	unsigned int i, info;
	int ret;

	for (i = 0; i < tdev->nb_channels; i++) {
		const struct iio_capi_chan *chan = &tdev->channels[i];

		chan_id(chan, id, sizeof(id));

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
				is_scan_element(chan) ? chan->scan_index : -1, id, chan->name,
				chan->label, chan->output, is_scan_element(chan), &fmt);
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

			if (!(chan->info_avail & IIO_CAPI_INFO_BIT(info)))
				continue;

			/* libiio copies the name */
			snprintf(attr, sizeof(attr), "%s" TABLE_AVAIL_SUFFIX, info_names[info]);
			ret = iio_channel_add_attr(chn, attr, IIO_ATTR_TYPE_CHANNEL, NULL);
			if (ret)
				return ret;
		}

		ret = table_add_ext(tdev, iio_device, IIO_CAPI_ATTR_CHANNEL, chan, chn, NULL);
		if (ret)
			return ret;
	}

	/* The device's own values, as device attributes */
	for (info = 0; info < ARRAY_LEN(info_names); info++) {
		if (!(tdev->info & IIO_CAPI_INFO_BIT(info)))
			continue;

		ret = iio_device_add_attr(iio_device, info_names[info], IIO_ATTR_TYPE_DEVICE);
		if (ret)
			return ret;

		if (!(tdev->info_avail & IIO_CAPI_INFO_BIT(info)))
			continue;

		snprintf(attr, sizeof(attr), "%s" TABLE_AVAIL_SUFFIX, info_names[info]);
		ret = iio_device_add_attr(iio_device, attr, IIO_ATTR_TYPE_DEVICE);
		if (ret)
			return ret;
	}

	ret = table_add_ext(tdev, iio_device, IIO_CAPI_ATTR_DEVICE, NULL, NULL, NULL);
	if (ret)
		return ret;

	/* beside direct_reg_access, which backend.c adds */
	return table_add_ext(tdev, iio_device, IIO_CAPI_ATTR_DEBUG, NULL, NULL, NULL);
}

/* The part's own attribute a libiio attribute is, its channel (NULL but
 * for a channel one), and whether it is the attribute's _available list;
 * NULL when it is not one of them. */
static const struct iio_capi_attr *table_find_ext(const struct iio_capi_table_device *tdev,
		const struct iio_attr *attr, const struct iio_capi_chan **chan, bool *avail)
{
	const char *name = iio_attr_get_name(attr);
	enum iio_capi_attr_scope scope;
	unsigned int i;
	int idx;

	if (!tdev->ext)
		return NULL;

	*chan = NULL;
	switch (attr->type) {
	case IIO_ATTR_TYPE_DEVICE:
		scope = IIO_CAPI_ATTR_DEVICE;
		break;
	case IIO_ATTR_TYPE_BUFFER:
		scope = IIO_CAPI_ATTR_BUFFER;
		break;
	case IIO_ATTR_TYPE_DEBUG:
		scope = IIO_CAPI_ATTR_DEBUG;
		break;
	case IIO_ATTR_TYPE_CHANNEL:
		if (!attr->iio.chn)
			return NULL;
		idx = table_chan_index(attr->iio.chn);
		if (idx < 0 || (unsigned int)idx >= tdev->nb_channels)
			return NULL;
		scope = IIO_CAPI_ATTR_CHANNEL;
		*chan = &tdev->channels[idx];
		break;
	default:
		return NULL;
	}

	for (i = 0; i < tdev->ext->nb_attrs; i++) {
		const struct iio_capi_attr *a = &tdev->ext->attrs[i];

		if (!ext_is_on(a, scope, *chan))
			continue;

		*avail = false;
		if (!strcmp(a->name, name))
			return a;

		*avail = true;
		if (a->read_avail && is_avail_of(name, a->name))
			return a;
	}

	return NULL;
}

/* The table channel and value a libiio attribute is (chan NULL for one of
 * the device's own), and whether it is the value's _available list. */
static int table_find(const struct iio_capi_table_device *tdev, const struct iio_attr *attr,
		const struct iio_capi_chan **chan, enum iio_capi_info *info, bool *avail)
{
	const char *name = iio_attr_get_name(attr);
	size_t n = strlen(name), sl = strlen(TABLE_AVAIL_SUFFIX);
	unsigned int i;
	int idx;

	*avail = n > sl && !strcmp(name + n - sl, TABLE_AVAIL_SUFFIX);
	if (*avail)
		n -= sl;

	if (attr->type == IIO_ATTR_TYPE_DEVICE) {
		*chan = NULL;
	} else if (attr->type == IIO_ATTR_TYPE_CHANNEL && attr->iio.chn) {
		idx = table_chan_index(attr->iio.chn);
		if (idx < 0 || (unsigned int)idx >= tdev->nb_channels)
			return -ENOENT;
		*chan = &tdev->channels[idx];
	} else {
		return -ENOENT;
	}

	for (i = 0; i < ARRAY_LEN(info_names); i++) {
		if (strlen(info_names[i]) == n && !strncmp(name, info_names[i], n)) {
			*info = (enum iio_capi_info)i;
			/* only the device's own values are device attributes */
			if (!*chan && !(tdev->info & IIO_CAPI_INFO_BIT(i)))
				return -ENOENT;
			return 0;
		}
	}

	return -ENOENT;
}

/* --- values as text --- */

int iio_capi_value(const struct capi_value *value, int32_t *val, int32_t *val2)
{
	*val = value->val;
	*val2 = value->val2;

	switch (value->type) {
	case CAPI_VALUE_INT:
		return IIO_CAPI_VAL_INT;
	case CAPI_VALUE_INT_PLUS_MICRO:
		return IIO_CAPI_VAL_INT_PLUS_MICRO;
	case CAPI_VALUE_INT_PLUS_NANO:
		return IIO_CAPI_VAL_INT_PLUS_NANO;
	case CAPI_VALUE_FRACTIONAL:
		return IIO_CAPI_VAL_FRACTIONAL;
	case CAPI_VALUE_FRACTIONAL_LOG2:
		return IIO_CAPI_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

int iio_capi_value_int(const struct capi_value *value, int32_t min, int32_t max, int32_t *out)
{
	int64_t nano = capi_value_nano(value);

	if (nano % 1000000000 || nano / 1000000000 < min || nano / 1000000000 > max)
		return -EINVAL;

	*out = (int32_t)(nano / 1000000000);

	return 0;
}

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
	int ret;

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
		/* truncated toward zero; past
		 * 62 bits, 1 << val2 would not fit */
		if (val2 < 0 || val2 > 62)
			return -EINVAL;
		nano = ((int64_t)val * 1000000000) / ((int64_t)1 << val2);
		return format_fixed(dst, len, nano / 1000000000, nano % 1000000000, 9);
	case IIO_CAPI_VAL_CHAR:
		return snprintf(dst, len, "%c", (char)val);
	case IIO_CAPI_VAL_INT_PLUS_MICRO_DB:
		ret = format_fixed(dst, len, val, val2, 6);
		if (ret < 0 || (size_t)ret >= len)
			return ret;
		return ret + snprintf(dst + ret, len - (size_t)ret, " dB");
	default:
		return -EINVAL;
	}
}

/* "<int>[.<frac>]" into val + val2 / 1000000000, digits past the ninth
 * dropped; "-0.5" is 0 and -500000000. With db, it may end with "dB", as
 * Linux takes a gain in dB. */
static int parse_value(const char *src, bool db, int32_t *val, int32_t *val2)
{
	const char *p = src;
	int32_t frac = 0, scale = 100000000;
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

	while (*end == ' ')
		end++;
	if (db && !strncmp(end, "dB", 2))
		end += 2;

	while (*end == ' ' || *end == '\n')
		end++;
	if (*end)
		return -EINVAL;

	*val = (int32_t)whole;
	*val2 = (neg && !whole) ? -frac : frac;

	return 0;
}

/* An _available list: the values, a space apart. */
static int format_list(const struct capi_value *values, uint32_t nb, char *dst, size_t len)
{
	uint32_t i;
	int32_t val, val2;
	size_t pos = 0;
	int ret, fmt;

	if (nb > TABLE_AVAIL_MAX)
		return -ERANGE;

	dst[0] = '\0';
	for (i = 0; i < nb; i++) {
		if (i) {
			if (pos + 1 >= len)
				return -ERANGE;
			dst[pos++] = ' ';
			dst[pos] = '\0';
		}

		fmt = iio_capi_value(&values[i], &val, &val2);
		if (fmt < 0)
			return fmt;

		ret = format_value(dst + pos, len - pos, fmt, val, val2);
		if (ret < 0)
			return ret;
		if ((size_t)ret >= len - pos)
			return -ERANGE;
		pos += (size_t)ret;
	}

	/* With the NUL, as libiio wants it. */
	return (int)pos + 1;
}

/* A value's _available list. */
static int table_read_avail(const struct iio_capi_table_device *tdev,
		const struct iio_capi_chan *chan, enum iio_capi_info info, char *dst, size_t len)
{
	struct capi_value values[TABLE_AVAIL_MAX];
	uint32_t nb = TABLE_AVAIL_MAX;
	int ret;

	if (!tdev->ops->read_avail)
		return -ENOSYS;

	ret = tdev->ops->read_avail(tdev->dev, chan, info, values, &nb);
	if (ret)
		return ret;

	return format_list(values, nb, dst, len);
}

/* The part's own attribute's _available list. */
static int ext_read_avail(const struct iio_capi_table_device *tdev, const struct iio_capi_attr *a,
		const struct iio_capi_chan *chan, char *dst, size_t len)
{
	struct capi_value values[TABLE_AVAIL_MAX];
	uint32_t nb = TABLE_AVAIL_MAX;
	int ret;

	ret = a->read_avail(tdev->ext->priv, chan, a->arg, values, &nb);
	if (ret)
		return ret;

	return format_list(values, nb, dst, len);
}

static int table_read_attr(const struct iio_capi_device *dev, const struct iio_device *iio_device,
		const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_capi_table_device *tdev = table_of(dev);
	const struct iio_capi_attr *ext;
	const struct iio_capi_chan *chan;
	struct capi_value value;
	enum iio_capi_info info;
	int32_t val = 0, val2 = 0;
	bool avail;
	int ret;

	(void)iio_device;

	ext = table_find_ext(tdev, attr, &chan, &avail);
	if (ext && avail)
		return ext_read_avail(tdev, ext, chan, dst, len);

	if (ext && ext->show) {
		ret = ext->show(tdev->ext->priv, chan, ext->arg, dst, len);
		if (ret < 0)
			return ret;
		if ((size_t)ret >= len)
			return -ERANGE;

		/* With the NUL, as libiio wants it. */
		return ret + 1;
	}

	if (ext) {
		/* A number, written as the values are */
		if (!ext->read)
			return -EPERM;

		ret = ext->read(tdev->ext->priv, chan, ext->arg, &value);
		if (ret < 0)
			return ret;

		ret = iio_capi_value(&value, &val, &val2);
	} else {
		ret = table_find(tdev, attr, &chan, &info, &avail);
		if (ret)
			return ret;
		if (avail)
			return table_read_avail(tdev, chan, info, dst, len);
		if (!tdev->ops->read_raw)
			return -ENOSYS;

		ret = tdev->ops->read_raw(tdev->dev, chan, info, &val, &val2);
	}
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
	const struct iio_capi_attr *ext;
	const struct iio_capi_chan *chan;
	enum iio_capi_info info = IIO_CAPI_INFO_RAW;
	struct capi_value value = {CAPI_VALUE_INT_PLUS_NANO, 0, 0};
	bool avail = false;
	char *buf;
	int ret;

	(void)iio_device;

	ext = table_find_ext(tdev, attr, &chan, &avail);
	if (ext) {
		if (avail || (!ext->store && !ext->write))
			return -EPERM;
	} else {
		ret = table_find(tdev, attr, &chan, &info, &avail);
		if (ret)
			return ret;
		if (avail || !tdev->ops->write_raw)
			return -EPERM;
	}

	len = strnlen(src, len);

	buf = capi_malloc(len + 1);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, src, len);
	buf[len] = '\0';

	/* The part's own attribute as text parses it itself */
	if (ext && ext->store) {
		ret = ext->store(tdev->ext->priv, chan, ext->arg, buf);
		capi_free(buf);

		return ret < 0 ? ret : (int)len;
	}

	/* A character (the first written); else a number */
	if (!ext && info == IIO_CAPI_INFO_THERMOCOUPLE_TYPE) {
		value = (struct capi_value){CAPI_VALUE_INT, (unsigned char)buf[0], 0};
		ret = len ? 0 : -EINVAL;
	} else {
		ret = parse_value(buf, !ext && info == IIO_CAPI_INFO_HARDWAREGAIN, &value.val,
				&value.val2);
	}
	capi_free(buf);
	if (ret)
		return ret;

	if (ext)
		ret = ext->write(tdev->ext->priv, chan, ext->arg, &value);
	else
		ret = tdev->ops->write_raw(tdev->dev, chan, info, &value);

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

/* The value's bits only, shift up: the bits around them are 0, as in the
 * registers a part's samples come from. */
static void pack_sample(uint8_t *dst, const struct iio_capi_scan_format *scan, int32_t value)
{
	unsigned int i, n = scan->storage / 8;
	uint32_t bits = scan->bits < 32 ? ((uint32_t)value & ((1u << scan->bits) - 1)) :
					  (uint32_t)value;
	uint32_t raw = bits << scan->shift;

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

static void pack_scan(const struct iio_capi_table_device *tdev, uint32_t m,
		const uint32_t *offsets, uint32_t size, uint8_t *dst, const int32_t *values)
{
	unsigned int i, k;

	/* The padding is not left as whatever the block held. */
	memset(dst, 0, size);

	for (i = 0, k = 0; i < tdev->nb_channels; i++)
		if (m & (1u << i))
			pack_sample(dst + offsets[i], &tdev->channels[i].scan, values[k++]);
}

/* An input block from read_scans, a chunk of scans at a time. */
static int table_read_chunks(struct iio_capi_table_device *tdev, uint32_t m,
		const uint32_t *offsets, uint32_t size, uint8_t *data, size_t bytes)
{
	int32_t values[TABLE_CHUNK_VALUES];
	uint32_t nb_values = (uint32_t)__builtin_popcount(m);
	uint32_t chunk = TABLE_CHUNK_VALUES / nb_values, nb, j;
	size_t nb_scans = bytes / size, done = 0;
	int ret;

	while (done < nb_scans) {
		nb = nb_scans - done < chunk ? (uint32_t)(nb_scans - done) : chunk;

		ret = tdev->ops->read_scans(tdev->dev, m, values, nb);
		if (ret)
			return ret;

		for (j = 0; j < nb; j++, done++)
			pack_scan(tdev, m, offsets, size, data + done * size, &values[j * nb_values]);
	}

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

	if (!output && tdev->ops->read_scans)
		return table_read_chunks(tdev, m, offsets, size, data, bytes);

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

		pack_scan(tdev, m, offsets, size, data + pos, values);
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

/* --- registers --- */

/* direct_reg_access (backend.c) on the part's reg_access, or else the
 * device's (its class's), as Linux's debugfs_reg_access: a read with
 * readval, a write without. */
static int table_reg_access(const struct iio_capi_device *dev, uint32_t reg, uint32_t writeval,
		uint32_t *readval)
{
	const struct iio_capi_table_device *tdev = table_of(dev);
	const struct iio_capi_ext *ext = tdev->ext;

	if (ext && ext->reg_access)
		return ext->reg_access(ext->priv, reg, writeval, readval);

	return tdev->ops->reg_access(tdev->dev, reg, writeval, readval);
}

static int table_reg_read(const struct iio_capi_device *dev, uint32_t reg, uint32_t *val)
{
	if (!val)
		return -EINVAL;

	return table_reg_access(dev, reg, 0, val);
}

static int table_reg_write(const struct iio_capi_device *dev, uint32_t reg, uint32_t val)
{
	return table_reg_access(dev, reg, val, NULL);
}

/* --- registration --- */

static bool storage_ok(uint8_t storage)
{
	return storage == 8 || storage == 16 || storage == 32;
}

/* Whether name is an attribute the values in info make: one of them, or
 * the _available list of one in info_avail too. */
static bool is_value_of(uint32_t info, uint32_t info_avail, const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LEN(info_names); i++) {
		if (!(info & IIO_CAPI_INFO_BIT(i)))
			continue;
		if (!strcmp(name, info_names[i]))
			return true;
		if ((info_avail & IIO_CAPI_INFO_BIT(i)) && is_avail_of(name, info_names[i]))
			return true;
	}

	return false;
}

/* Whether two of the part's own attributes could be on the same channel,
 * or are of the same other scope. */
static bool ext_overlap(const struct iio_capi_attr *a, const struct iio_capi_attr *b)
{
	if (a->scope != b->scope)
		return false;
	if (a->scope != IIO_CAPI_ATTR_CHANNEL || !a->chan_types || !b->chan_types)
		return true;

	return a->chan_types & b->chan_types;
}

/* Whether name, one ext->attrs[i] would make (its own, or its _available
 * list), is taken where it would be: by a value of a channel it is on or
 * of the device, by direct_reg_access, or by an attribute before it on the
 * same channels. */
static bool ext_name_taken(const struct iio_capi_table_device *tdev, unsigned int i,
		const char *name)
{
	const struct iio_capi_ext *ext = tdev->ext;
	const struct iio_capi_attr *a = &ext->attrs[i];
	unsigned int j;

	switch (a->scope) {
	case IIO_CAPI_ATTR_CHANNEL:
		for (j = 0; j < tdev->nb_channels; j++) {
			const struct iio_capi_chan *chan = &tdev->channels[j];

			if (ext_is_on(a, IIO_CAPI_ATTR_CHANNEL, chan) &&
					is_value_of(chan->info, chan->info_avail, name))
				return true;
		}
		break;
	case IIO_CAPI_ATTR_DEVICE:
		if (is_value_of(tdev->info, tdev->info_avail, name))
			return true;
		break;
	case IIO_CAPI_ATTR_DEBUG:
		if (!strcmp(name, "direct_reg_access"))
			return true;
		break;
	default:
		break;
	}

	for (j = 0; j < i; j++) {
		const struct iio_capi_attr *b = &ext->attrs[j];

		if (!ext_overlap(a, b))
			continue;
		if (!strcmp(b->name, name) || (b->read_avail && is_avail_of(name, b->name)))
			return true;
	}

	return false;
}

/*
 * Whether the part's own attributes can be served: each named, readable or
 * writable as a number or as text (not both), an _available list only for
 * a number, in a scope there is (the buffer for a table with one), channel
 * types only for a channel one and of types there are, and no name of its
 * own (or <name>_available) that the channels or the device it is on
 * already have.
 */
static int table_check_ext(const struct iio_capi_table_device *tdev, bool has_buffer)
{
	const uint32_t all_types = (1u << ARRAY_LEN(chan_type_names)) - 1;
	const struct iio_capi_ext *ext = tdev->ext;
	char avail[TABLE_ATTR_LEN];
	unsigned int i;

	if (!ext)
		return 0;

	if (ext->nb_attrs && !ext->attrs)
		return -EINVAL;

	for (i = 0; i < ext->nb_attrs; i++) {
		const struct iio_capi_attr *a = &ext->attrs[i];
		bool number = a->read || a->write, text = a->show || a->store;

		if (!a->name || !a->name[0] || number == text || (a->read_avail && !number) ||
				(unsigned int)a->scope > IIO_CAPI_ATTR_DEBUG ||
				(a->scope == IIO_CAPI_ATTR_BUFFER && !has_buffer) ||
				(a->chan_types & ~all_types) ||
				(a->scope != IIO_CAPI_ATTR_CHANNEL && a->chan_types))
			return -EINVAL;

		if (ext_name_taken(tdev, i, a->name))
			return -EINVAL;

		if (!a->read_avail)
			continue;

		if (strlen(a->name) + strlen(TABLE_AVAIL_SUFFIX) >= sizeof(avail))
			return -EINVAL;
		snprintf(avail, sizeof(avail), "%s" TABLE_AVAIL_SUFFIX, a->name);
		if (ext_name_taken(tdev, i, avail))
			return -EINVAL;
	}

	return 0;
}

/* Whether the table can be served, and the direction of its buffer. */
static int table_check(const struct iio_capi_table_device *tdev, bool *has_scan, bool *output)
{
	char id[TABLE_ID_LEN], other[TABLE_ID_LEN];
	unsigned int i, j;
	int last = -1;

	*has_scan = false;

	for (i = 0; i < tdev->nb_channels; i++) {
		const struct iio_capi_chan *chan = &tdev->channels[i];

		if ((unsigned int)chan->type >= ARRAY_LEN(chan_type_names) ||
				!chan_type_names[chan->type] ||
				(unsigned int)chan->modifier >= ARRAY_LEN(chan_modifier_names))
			return -EINVAL;

		if (chan->differential && (chan->channel < 0 || chan->channel2 < 0 ||
				chan->modifier != IIO_CAPI_MOD_NONE))
			return -EINVAL;

		/* libiio tells channels apart by id and direction */
		chan_id(chan, id, sizeof(id));
		for (j = 0; j < i; j++) {
			if (tdev->channels[j].output != chan->output)
				continue;
			chan_id(&tdev->channels[j], other, sizeof(other));
			if (!strcmp(id, other))
				return -EINVAL;
		}

		if (!is_scan_element(chan))
			continue;

		/* libiio packs scans by index, the table by its order. One
		 * buffer has one direction. */
		if (chan->scan_index <= last || !storage_ok(chan->scan.storage) ||
				!chan->scan.bits ||
				chan->scan.shift + chan->scan.bits > chan->scan.storage ||
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
	bool has_scan, output = false, has_buffer, has_regs;
	int ret;

	if (!tdev || !tdev->name || !tdev->ops || (tdev->nb_channels && !tdev->channels) ||
			tdev->nb_channels > TABLE_MAX_CHANNELS)
		return -EINVAL;

	ret = table_check(tdev, &has_scan, &output);
	if (ret)
		return ret;

	has_buffer = has_scan && (output ? !!tdev->ops->write_scan :
					   (tdev->ops->read_scan || tdev->ops->read_scans));

	ret = table_check_ext(tdev, has_buffer);
	if (ret)
		return ret;

	has_regs = (tdev->ext && tdev->ext->reg_access) || tdev->ops->reg_access;

	tdev->api = (struct iio_capi_device_api){
		.add_channels = table_add_channels,
		.read_attr = table_read_attr,
		.write_attr = table_write_attr,
		.read_samples = has_buffer && !output ? table_read_samples : NULL,
		.write_samples = has_buffer && output ? table_write_samples : NULL,
		.enable_buffer = has_buffer ? table_enable_buffer : NULL,
		.reg_read = has_regs ? table_reg_read : NULL,
		.reg_write = has_regs ? table_reg_write : NULL,
		.add_buffer_attrs = has_buffer && tdev->ext ? table_add_buffer_attrs : NULL,
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
