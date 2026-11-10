/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * An accelerometer behind capi_accel as a table device: the table has its
 * axes, the values come from capi_accel_read_raw() and the other capi_accel
 * calls; and the part's temperature sensor, a capi_temp, as iio_capi_temp.c
 * serves it.
 */

#include <capi_accel.h>
#include <errno.h>
#include <iio_capi_accel.h>
#include <iio_capi_table.h>
#include <iio_capi_temp.h>
#include <stdbool.h>
#include <stdint.h>

#define ACCEL_NB_AXES 3
#define ACCEL_TEMP    ACCEL_NB_AXES

static unsigned int accel_chan_index(const struct iio_capi_accel *accel,
		const struct iio_capi_chan *chan)
{
	return (unsigned int)(chan - accel->chans);
}

static int accel_read_axes(const struct iio_capi_accel *accel, int32_t *axes)
{
	return capi_accel_read_raw(accel->handle, &axes[0], &axes[1], &axes[2]);
}

static int accel_read_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		int32_t *val, int32_t *val2)
{
	const struct iio_capi_accel *accel = dev;
	unsigned int i;
	int32_t axes[ACCEL_NB_AXES];
	struct capi_value scale, hz;
	int ret;

	/* The device's own: sampling_frequency, with a device-wide ODR */
	if (!chan) {
		if (info != IIO_CAPI_INFO_SAMP_FREQ)
			return -EINVAL;
		ret = capi_accel_get_odr(accel->handle, &hz);
		return ret ? ret : iio_capi_value(&hz, val, val2);
	}

	i = accel_chan_index(accel, chan);
	if (i == ACCEL_TEMP)
		return iio_capi_temp_read(accel->temp, info, val, val2);

	switch (info) {
	case IIO_CAPI_INFO_RAW:
		if (i >= ACCEL_NB_AXES)
			return -EINVAL;
		ret = accel_read_axes(accel, axes);
		if (ret)
			return ret;
		*val = axes[i];
		return IIO_CAPI_VAL_INT;
	case IIO_CAPI_INFO_SCALE:
		/* m/s² per code, as the driver states it */
		ret = capi_accel_get_scale(accel->handle, &scale);
		return ret ? ret : iio_capi_value(&scale, val, val2);
	case IIO_CAPI_INFO_SAMP_FREQ:
		/* Hz, as the driver states it */
		ret = capi_accel_get_odr(accel->handle, &hz);
		return ret ? ret : iio_capi_value(&hz, val, val2);
	case IIO_CAPI_INFO_CALIBBIAS:
		if (i >= ACCEL_NB_AXES)
			return -EINVAL;
		ret = capi_accel_get_calibbias(accel->handle, (enum capi_accel_axis)i, val);
		return ret ? ret : IIO_CAPI_VAL_INT;
	case IIO_CAPI_INFO_HPF_3DB:
		/* Hz, as the driver states it */
		ret = capi_accel_get_hpf(accel->handle, &hz);
		return ret ? ret : iio_capi_value(&hz, val, val2);
	default:
		return -EINVAL;
	}
}

/* A value the driver has no setter for is read-only. */
/* Ranges a driver lists at most. */
#define ACCEL_MAX_RANGES 16

/* The range whose scale is the one written, found in the driver's list of
 * ranges and their scales. */
static int accel_write_scale(const struct iio_capi_accel *accel, const struct capi_value *scale)
{
	struct capi_value ranges[ACCEL_MAX_RANGES], scales[ACCEL_MAX_RANGES];
	uint32_t nb = ACCEL_MAX_RANGES, i;
	int ret;

	ret = capi_accel_get_range_available(accel->handle, ranges, scales, &nb);
	if (ret)
		return ret;

	for (i = 0; i < nb; i++)
		if (capi_value_nano(&scales[i]) == capi_value_nano(scale))
			return capi_accel_set_range(accel->handle, &ranges[i]);

	return -EINVAL;
}

/* A value the driver has no setter for is read-only. */
static int accel_write_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		const struct capi_value *value)
{
	const struct iio_capi_accel *accel = dev;
	const struct capi_accel_ops *ops = accel->handle->ops;
	unsigned int i;

	/* The device's own: sampling_frequency, with a device-wide ODR */
	if (!chan) {
		if (info != IIO_CAPI_INFO_SAMP_FREQ || !ops->set_odr)
			return -EPERM;
		return capi_accel_set_odr(accel->handle, value);
	}

	i = accel_chan_index(accel, chan);
	if (i == ACCEL_TEMP)
		return iio_capi_temp_write(accel->temp, info, value);
	if (i >= ACCEL_NB_AXES)
		return -EPERM;

	switch (info) {
	case IIO_CAPI_INFO_SAMP_FREQ:
		if (!ops->set_odr)
			return -EPERM;
		return capi_accel_set_odr(accel->handle, value);
	case IIO_CAPI_INFO_CALIBBIAS:
		if (!ops->set_calibbias)
			return -EPERM;
		/* A whole number: the fraction is ignored */
		return capi_accel_set_calibbias(accel->handle, (enum capi_accel_axis)i, value->val);
	case IIO_CAPI_INFO_HPF_3DB:
		if (!ops->set_hpf)
			return -EPERM;
		return capi_accel_set_hpf(accel->handle, value);
	case IIO_CAPI_INFO_SCALE:
		if (!ops->set_range || !ops->get_range_available)
			return -EPERM;
		return accel_write_scale(accel, value);
	default:
		return -EPERM;
	}
}

static int accel_read_avail(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		struct capi_value *values, uint32_t *nb)
{
	const struct iio_capi_accel *accel = dev;
	uint32_t room = *nb;

	if (chan && accel_chan_index(accel, chan) == ACCEL_TEMP)
		return iio_capi_temp_read_avail(accel->temp, info, values, nb);

	switch (info) {
	case IIO_CAPI_INFO_SCALE: {
		/* The scale of each range: what writing scale picks among */
		struct capi_value ranges[ACCEL_MAX_RANGES];
		uint32_t n = room < ACCEL_MAX_RANGES ? room : ACCEL_MAX_RANGES;
		int ret = capi_accel_get_range_available(accel->handle, ranges, values, &n);

		*nb = n;
		return ret;
	}
	case IIO_CAPI_INFO_SAMP_FREQ:
		return capi_accel_get_odr_available(accel->handle, values, nb);
	case IIO_CAPI_INFO_HPF_3DB:
		return capi_accel_get_hpf_available(accel->handle, values, nb);
	default:
		return -EINVAL;
	}
}

/* temp's bit in a table mask: it follows the axes. */
#define ACCEL_TEMP_BIT (1u << ACCEL_TEMP)

static int accel_read_scan(void *dev, uint32_t mask, int32_t *values)
{
	const struct iio_capi_accel *accel = dev;
	int32_t axes[ACCEL_NB_AXES];
	unsigned int i, k = 0;
	int ret;

	/* One sample of every axis; the scan has those in mask, then temp. */
	ret = accel_read_axes(accel, axes);
	if (ret)
		return ret;

	for (i = 0; i < ACCEL_NB_AXES; i++)
		if (mask & (1u << i))
			values[k++] = axes[i];

	if (mask & ACCEL_TEMP_BIT)
		return capi_temp_get_raw(accel->temp, &values[k]);

	return 0;
}

/* The capi_accel axes of a table mask: the axes are channels 0, 1, 2. */
static uint32_t accel_axes_of(uint32_t mask)
{
	return (mask & (1u << 0) ? CAPI_ACCEL_AXIS_X : 0) |
	       (mask & (1u << 1) ? CAPI_ACCEL_AXIS_Y : 0) |
	       (mask & (1u << 2) ? CAPI_ACCEL_AXIS_Z : 0);
}

/* nb_scans samples of the stream, the axes in mask of each: the table's
 * scan layout is capi_accel_read_samples()'s. */
/* Scans read at once with temp in them: the samples' axes, then temp. */
#define ACCEL_TEMP_CHUNK 32

static int accel_read_stream(struct iio_capi_accel *accel, uint32_t mask, int32_t *values,
		uint32_t nb_scans)
{
	int32_t axes[ACCEL_NB_AXES * ACCEL_TEMP_CHUNK], temp;
	uint32_t axes_mask = accel_axes_of(mask), nb_axes, done, nb, j, a, k = 0;
	int ret;

	if (!(mask & ACCEL_TEMP_BIT))
		return axes_mask ? capi_accel_read_samples(accel->handle, axes_mask, values, nb_scans) :
				   -EINVAL;

	/* temp alone: the reading, as often as scans are asked for */
	if (!axes_mask) {
		for (j = 0; j < nb_scans; j++) {
			ret = capi_temp_get_raw(accel->temp, &values[j]);
			if (ret)
				return ret;
		}
		return 0;
	}

	/* The part samples temp apart from the axes, which the stream has:
	 * each chunk of samples gets the reading that follows it. */
	nb_axes = (uint32_t)__builtin_popcount(axes_mask);
	for (done = 0; done < nb_scans; done += nb) {
		nb = nb_scans - done < ACCEL_TEMP_CHUNK ? nb_scans - done : ACCEL_TEMP_CHUNK;

		ret = capi_accel_read_samples(accel->handle, axes_mask, axes, nb);
		if (ret)
			return ret;

		ret = capi_temp_get_raw(accel->temp, &temp);
		if (ret)
			return ret;

		for (j = 0; j < nb; j++) {
			for (a = 0; a < nb_axes; a++)
				values[k++] = axes[j * nb_axes + a];
			values[k++] = temp;
		}
	}

	return 0;
}

static int accel_read_scans(void *dev, uint32_t mask, int32_t *values, uint32_t nb_scans)
{
	struct iio_capi_accel *accel = dev;
	int ret;

	ret = accel_read_stream(accel, mask, values, nb_scans);
	if (ret != -EOVERFLOW)
		return ret;

	/* Samples were lost, mostly while the previous block was being sent:
	 * these scans start a new stream. Lost again, the part is faster than
	 * it can be read. */
	ret = capi_accel_stream_enable(accel->handle);
	if (ret)
		return ret;

	return accel_read_stream(accel, mask, values, nb_scans);
}

static int accel_enable_buffer(void *dev, uint32_t mask, bool enable)
{
	struct iio_capi_accel *accel = dev;

	(void)mask;

	return enable ? capi_accel_stream_enable(accel->handle) :
			capi_accel_stream_disable(accel->handle);
}

int iio_capi_accel_register(struct iio_capi_accel *accel, const char *name,
		struct capi_accel_handle *handle, struct capi_temp_handle *temp,
		const struct iio_capi_ext *ext)
{
	static const enum iio_capi_chan_modifier axes[ACCEL_NB_AXES] = {
		IIO_CAPI_MOD_X,
		IIO_CAPI_MOD_Y,
		IIO_CAPI_MOD_Z,
	};
	const struct capi_accel_ops *ops;
	unsigned int i, nb_chans = ACCEL_NB_AXES;
	uint32_t info = IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_RAW), avail = 0;
	uint8_t bits;
	int ret;

	if (!accel || !name || !handle || !handle->ops || !handle->ops->read_raw)
		return -EINVAL;

	ops = handle->ops;
	bits = handle->resolution;
	if (!bits || bits + handle->shift > 32)
		return -EINVAL;

	if (ops->get_scale)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SCALE);
	if (ops->get_odr)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SAMP_FREQ);
	if (ops->get_calibbias)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_CALIBBIAS);
	if (ops->get_hpf)
		info |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_HPF_3DB);

	/* A list of the values a writable one takes; the scale is written
	 * to choose a range */
	if (ops->get_scale && ops->set_range && ops->get_range_available)
		avail |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SCALE);
	if (ops->get_odr && ops->set_odr && ops->get_odr_available)
		avail |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SAMP_FREQ);
	if (ops->get_hpf && ops->set_hpf && ops->get_hpf_available)
		avail |= IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_HPF_3DB);

	for (i = 0; i < ACCEL_NB_AXES; i++) {
		accel->chans[i] = (struct iio_capi_chan){
			.type = IIO_CAPI_ACCEL,
			/* accel_x ...: not indexed */
			.channel = -1,
			.modifier = axes[i],
			.info = info,
			.info_avail = avail,
			.scan_index = (int)i,
			/* as the part's registers hold a code */
			.scan = iio_capi_scan_of(bits, true, handle->shift, handle->big_endian),
		};
	}

	/* temp follows the axes in scans, its raw code as the part's
	 * registers hold it, when the driver gives that; not in scans else. */
	if (temp) {
		ret = iio_capi_temp_chan(temp, &accel->chans[nb_chans],
				iio_capi_temp_scannable(temp) ? ACCEL_TEMP : -1);
		if (ret)
			return ret;
		nb_chans++;
	}

	accel->handle = handle;
	accel->temp = temp;

	accel->table.name = name;
	accel->table.dev = accel;
	accel->table.channels = accel->chans;
	accel->table.nb_channels = nb_chans;

	/* A device-wide ODR is the device's sampling_frequency, not its
	 * accel channels' */
	accel->table.info = 0;
	accel->table.info_avail = 0;
	if (handle->odr_device_wide) {
		accel->table.info = info & IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SAMP_FREQ);
		accel->table.info_avail = avail & IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SAMP_FREQ);
		for (i = 0; i < ACCEL_NB_AXES; i++) {
			accel->chans[i].info &= ~IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SAMP_FREQ);
			accel->chans[i].info_avail &= ~IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_SAMP_FREQ);
		}
	}
	accel->ops = (struct iio_capi_table_ops){
		.read_raw = accel_read_raw,
		.write_raw = accel_write_raw,
		.read_avail = accel_read_avail,
		.read_scan = accel_read_scan,
	};
	if (ops->stream_enable && ops->stream_disable && ops->read_samples) {
		accel->ops.read_scans = accel_read_scans;
		accel->ops.enable_buffer = accel_enable_buffer;
	}

	accel->table.ops = &accel->ops;
	accel->table.ext = ext;

	return iio_capi_table_register(&accel->table);
}
