/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Serves temperature sensors behind capi_temp (any driver's capi_temp_ops)
 * as IIO temp channels: raw, scale (millidegrees
 * Celsius per code) and offset (codes), (raw + offset) * scale being the
 * temperature in millidegrees Celsius, with a driver that has get_raw,
 * get_scale and get_offset; else input (millidegrees Celsius).
 *
 * With a driver that has them, a channel also has thermocouple_type (a
 * letter, writable when the driver sets it) and oversampling_ratio
 * (writable when the driver sets it, with a list when it gives one).
 *
 * A channel is named from its handle as IIO names it: its kind the
 * modifier (temp_ambient, temp_object), its number when it is indexed
 * (temp0, temp1_object), plain temp for a part's one sensor.
 *
 * The channels are a device of their own (iio_capi_temp_register()), whose
 * buffer has the raw code of each sensor whose driver gives it and its
 * layout (resolution ...); or one of another adapter's device, when the
 * sensor is part of that device's part (iio_capi_accel_register() takes
 * one).
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_TEMP_H_
#define CAPI_INCLUDE_IIO_CAPI_TEMP_H_

#include <capi_temp.h>
#include <iio_capi_table.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sensors a device serves at most. */
#ifndef IIO_CAPI_TEMP_MAX_CHANNELS
#define IIO_CAPI_TEMP_MAX_CHANNELS 8
#endif

/* Temperature sensors served as a device. Filled by
 * iio_capi_temp_register(); keep it and the handles for as long as the
 * context exists. */
struct iio_capi_temp {
	struct iio_capi_table_device table;
	struct iio_capi_chan chans[IIO_CAPI_TEMP_MAX_CHANNELS];
	struct iio_capi_table_ops ops;
	struct capi_temp_handle *handles[IIO_CAPI_TEMP_MAX_CHANNELS];
};

/*
 * Add temperature sensors to the context as a device, a channel each, in
 * the order of handles (what capi_temp_init() gave; the array is copied).
 * The device has a buffer when a sensor's driver gives its raw code and
 * layout; a scan is the codes of the sensors in it, read one after the
 * other, as fast as the buffer asks. ext is the part's own attributes (on
 * the channels, the device, its buffer, or debug ones), iio_capi_table.h;
 * NULL for none. Returns 0, -EINVAL for missing
 * arguments, no handle or more than IIO_CAPI_TEMP_MAX_CHANNELS, a driver
 * with neither get_temp nor get_raw, get_scale and get_offset, two sensors
 * that would have the same channel name, or what iio_capi_table_register()
 * returns.
 */
int iio_capi_temp_register(struct iio_capi_temp *temp, const char *name,
		struct capi_temp_handle *const *handles, unsigned int nb_handles,
		const struct iio_capi_ext *ext);

/*
 * For adapters with a temp channel in their table: fill chan for handle,
 * named from it, in scans at scan_index (the raw code, as the handle's
 * layout has it) or not (-1); 0, or -EINVAL for a driver with neither
 * get_temp nor get_raw, get_scale and get_offset, or for a scan_index with
 * a driver that gives no raw code or its resolution. And read one of its
 * values, as a table's read_raw does.
 */
int iio_capi_temp_chan(struct capi_temp_handle *handle, struct iio_capi_chan *chan,
		int scan_index);
int iio_capi_temp_read(struct capi_temp_handle *handle, enum iio_capi_info info, int32_t *val,
		int32_t *val2);

/* Set one of its values, or list the values one takes, as a table's
 * write_raw and read_avail do: -EPERM for a value the driver does not set. */
int iio_capi_temp_write(struct capi_temp_handle *handle, enum iio_capi_info info,
		const struct capi_value *value);
int iio_capi_temp_read_avail(struct capi_temp_handle *handle, enum iio_capi_info info,
		struct capi_value *values, uint32_t *nb);

/* Whether a driver gives what puts its sensor in scans: its raw code and
 * the code's layout. */
bool iio_capi_temp_scannable(const struct capi_temp_handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_TEMP_H_ */
