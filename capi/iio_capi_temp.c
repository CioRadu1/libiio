/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Temperature sensors behind capi_temp as table channels: the values come
 * from capi_temp_get_raw(), _get_scale(), _get_offset() or _get_temp(); a
 * scan is the raw codes.
 */

#include <capi_temp.h>
#include <errno.h>
#include <iio_capi_table.h>
#include <iio_capi_temp.h>
#include <stdint.h>

static bool temp_is_linear(const struct capi_temp_ops *ops)
{
	return ops->get_raw && ops->get_scale && ops->get_offset;
}

bool iio_capi_temp_scannable(const struct capi_temp_handle *handle)
{
	return handle && handle->ops && handle->ops->get_raw && handle->resolution &&
	       handle->resolution + handle->shift <= 32;
}

/* The channel's modifier: the sensor's kind. */
static enum iio_capi_chan_modifier temp_modifier(enum capi_temp_kind kind)
{
	switch (kind) {
	case CAPI_TEMP_KIND_AMBIENT:
		return IIO_CAPI_MOD_TEMP_AMBIENT;
	case CAPI_TEMP_KIND_OBJECT:
		return IIO_CAPI_MOD_TEMP_OBJECT;
	default:
		return IIO_CAPI_MOD_NONE;
	}
}

int iio_capi_temp_chan(struct capi_temp_handle *handle, struct iio_capi_chan *chan,
		int scan_index)
{
	uint32_t info;

	if (!handle || !handle->ops || !chan || (unsigned int)handle->kind > CAPI_TEMP_KIND_OBJECT)
		return -EINVAL;

	if (temp_is_linear(handle->ops))
		info = IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_RAW) | IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SCALE) |
		       IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_OFFSET);
	else if (handle->ops->get_temp)
		info = IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_PROCESSED);
	else
		return -EINVAL;

	if (handle->ops->get_tc_type)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_THERMOCOUPLE_TYPE);
	if (handle->ops->get_oversampling)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_OVERSAMPLING_RATIO);

	*chan = (struct iio_capi_chan){
		.type = IIO_CAPI_TEMP,
		/* temp, temp<n>, temp_<kind>, temp<n>_<kind> */
		.channel = handle->indexed ? handle->channel : -1,
		.modifier = temp_modifier(handle->kind),
		.info = info,
		/* a list of the ratios a writable one takes */
		.info_avail = handle->ops->get_oversampling && handle->ops->set_oversampling &&
					      handle->ops->get_oversampling_available ?
				      IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_OVERSAMPLING_RATIO) :
				      0,
		.scan_index = -1,
	};

	if (scan_index < 0)
		return 0;

	/* In scans: the raw code, laid out as the part's registers hold it */
	if (!iio_capi_temp_scannable(handle))
		return -EINVAL;

	chan->scan_index = scan_index;
	chan->scan = iio_capi_scan_of(handle->resolution, handle->is_signed, handle->shift,
			handle->big_endian);

	return 0;
}

int iio_capi_temp_read(struct capi_temp_handle *handle, enum iio_capi_info info, int32_t *val,
		int32_t *val2)
{
	struct capi_value v;
	int ret;

	switch (info) {
	case IIO_CAPI_INFO_RAW:
		ret = capi_temp_get_raw(handle, val);
		return ret ? ret : IIO_CAPI_VAL_INT;
	case IIO_CAPI_INFO_SCALE:
		/* m°C per code, as the driver states it */
		ret = capi_temp_get_scale(handle, &v);
		return ret ? ret : iio_capi_value(&v, val, val2);
	case IIO_CAPI_INFO_OFFSET:
		/* codes, as the driver states it */
		ret = capi_temp_get_offset(handle, &v);
		return ret ? ret : iio_capi_value(&v, val, val2);
	case IIO_CAPI_INFO_PROCESSED:
		ret = capi_temp_get_temp(handle, val);
		return ret ? ret : IIO_CAPI_VAL_INT;
	case IIO_CAPI_INFO_THERMOCOUPLE_TYPE: {
		char type;

		ret = capi_temp_get_tc_type(handle, &type);
		if (ret)
			return ret;
		*val = (unsigned char)type;
		return IIO_CAPI_VAL_CHAR;
	}
	case IIO_CAPI_INFO_OVERSAMPLING_RATIO: {
		uint32_t ratio;

		ret = capi_temp_get_oversampling(handle, &ratio);
		if (ret)
			return ret;
		*val = (int32_t)ratio;
		return IIO_CAPI_VAL_INT;
	}
	default:
		return -EINVAL;
	}
}

int iio_capi_temp_write(struct capi_temp_handle *handle, enum iio_capi_info info,
		const struct capi_value *value)
{
	const struct capi_temp_ops *ops = handle->ops;

	switch (info) {
	case IIO_CAPI_INFO_THERMOCOUPLE_TYPE:
		if (!ops->set_tc_type)
			return -EPERM;
		/* the letter, as the table takes a character */
		return capi_temp_set_tc_type(handle, (char)value->val);
	case IIO_CAPI_INFO_OVERSAMPLING_RATIO:
		if (!ops->set_oversampling)
			return -EPERM;
		/* a whole number: the fraction is ignored */
		if (value->val < 0)
			return -EINVAL;
		return capi_temp_set_oversampling(handle, (uint32_t)value->val);
	default:
		return -EPERM;
	}
}

/* Ratios a driver lists at most. */
#define TEMP_MAX_RATIOS 16

int iio_capi_temp_read_avail(struct capi_temp_handle *handle, enum iio_capi_info info,
		struct capi_value *values, uint32_t *nb)
{
	uint32_t ratios[TEMP_MAX_RATIOS], n = TEMP_MAX_RATIOS, i;
	int ret;

	if (info != IIO_CAPI_INFO_OVERSAMPLING_RATIO)
		return -EINVAL;

	ret = capi_temp_get_oversampling_available(handle, ratios, &n);
	if (ret)
		return ret;
	if (n > *nb)
		return -ENOSPC;

	for (i = 0; i < n; i++)
		values[i] = (struct capi_value){CAPI_VALUE_INT, (int32_t)ratios[i], 0};
	*nb = n;

	return 0;
}

static struct capi_temp_handle *temp_handle_of(const struct iio_capi_temp *temp,
		const struct iio_capi_chan *chan)
{
	return temp->handles[chan - temp->chans];
}

static int temp_read_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		int32_t *val, int32_t *val2)
{
	const struct iio_capi_temp *temp = dev;

	/* no values of the device's own */
	if (!chan)
		return -EINVAL;

	return iio_capi_temp_read(temp_handle_of(temp, chan), info, val, val2);
}

static int temp_write_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		const struct capi_value *value)
{
	const struct iio_capi_temp *temp = dev;

	if (!chan)
		return -EPERM;

	return iio_capi_temp_write(temp_handle_of(temp, chan), info, value);
}

static int temp_read_avail(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		struct capi_value *values, uint32_t *nb)
{
	const struct iio_capi_temp *temp = dev;

	if (!chan)
		return -EINVAL;

	return iio_capi_temp_read_avail(temp_handle_of(temp, chan), info, values, nb);
}

/* One scan: the code of each sensor in mask, in table order. */
static int temp_read_scan(void *dev, uint32_t mask, int32_t *values)
{
	const struct iio_capi_temp *temp = dev;
	unsigned int i, k = 0;
	int ret;

	for (i = 0; i < temp->table.nb_channels; i++) {
		if (!(mask & (1u << i)))
			continue;

		ret = capi_temp_get_raw(temp->handles[i], &values[k++]);
		if (ret)
			return ret;
	}

	return 0;
}

/* Two channels would have the same id. */
static bool temp_same_name(const struct iio_capi_chan *a, const struct iio_capi_chan *b)
{
	return a->modifier == b->modifier && a->channel == b->channel;
}

/* direct_reg_access: the registers of the one sensor */
static int temp_reg_access(void *dev, uint32_t reg, uint32_t writeval, uint32_t *readval)
{
	struct iio_capi_temp *temp = dev;

	return capi_temp_reg_access(temp->handles[0], reg, writeval, readval);
}

int iio_capi_temp_register(struct iio_capi_temp *temp, const char *name,
		struct capi_temp_handle *const *handles, unsigned int nb_handles,
		const struct iio_capi_ext *ext)
{
	unsigned int i, j;
	int ret, scan = 0;

	if (!temp || !name || !handles || !nb_handles || nb_handles > IIO_CAPI_TEMP_MAX_CHANNELS)
		return -EINVAL;

	for (i = 0; i < nb_handles; i++) {
		/* in scans, in order, when its driver gives the raw code */
		ret = iio_capi_temp_chan(handles[i], &temp->chans[i],
				iio_capi_temp_scannable(handles[i]) ? scan : -1);
		if (ret)
			return ret;
		if (temp->chans[i].scan_index >= 0)
			scan++;

		for (j = 0; j < i; j++)
			if (temp_same_name(&temp->chans[i], &temp->chans[j]))
				return -EINVAL;

		temp->handles[i] = handles[i];
	}

	temp->ops = (struct iio_capi_table_ops){
		.read_raw = temp_read_raw,
		.write_raw = temp_write_raw,
		.read_avail = temp_read_avail,
		.read_scan = scan ? temp_read_scan : NULL,
	};
	/* One part's registers: with several sensors, whose would they be? */
	if (nb_handles == 1 && handles[0]->ops && handles[0]->ops->reg_access)
		temp->ops.reg_access = temp_reg_access;

	temp->table.name = name;
	temp->table.dev = temp;
	temp->table.channels = temp->chans;
	temp->table.nb_channels = nb_handles;
	temp->table.ops = &temp->ops;
	temp->table.info = 0;
	temp->table.info_avail = 0;
	temp->table.ext = ext;

	return iio_capi_table_register(&temp->table);
}
