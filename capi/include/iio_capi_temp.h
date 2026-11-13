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
 * layout (resolution ...); or those of a part that is more classes, its
 * temperature sensor beside its accelerometer (an ADXL355:
 * iio_capi_temp_init(), then iio_capi_part.h).
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

/* Temperature sensors served. Filled by iio_capi_temp_init() or
 * iio_capi_temp_register(); keep it and the handles for as long as the
 * context exists. */
struct iio_capi_temp {
	struct iio_capi_table_device table;
	struct iio_capi_chan chans[IIO_CAPI_TEMP_MAX_CHANNELS];
	struct iio_capi_table_ops ops;
	struct capi_temp_handle *handles[IIO_CAPI_TEMP_MAX_CHANNELS];
};

/*
 * Fill temp's table for temperature sensors, a channel each, in the order
 * of handles (what capi_temp_init() gave; the array is copied), not
 * registered: for iio_capi_part_register(). The table has a buffer when a
 * sensor's driver gives its raw code and layout; a scan is the codes of
 * the sensors in it, read one after the other, as fast as the buffer
 * asks. With one sensor, its driver's reg_access op, if it has one, is
 * the debug attribute direct_reg_access (with several, none: whose would
 * it be?). Returns 0, or -EINVAL for missing arguments, no handle or more
 * than IIO_CAPI_TEMP_MAX_CHANNELS, a driver with neither get_temp nor
 * get_raw, get_scale and get_offset, or two sensors that would have the
 * same channel name.
 */
int iio_capi_temp_init(struct iio_capi_temp *temp, struct capi_temp_handle *const *handles,
		unsigned int nb_handles);

/*
 * iio_capi_temp_init(), and the sensors added to the context as the IIO
 * device name. ext is the part's own attributes (on the channels, the
 * device, its buffer, or debug ones), iio_capi_table.h; NULL for none
 * (ext's reg_access, given, is served instead of the driver's). Returns
 * what iio_capi_temp_init() does, -EINVAL for no name, or what
 * iio_capi_table_register() returns.
 */
int iio_capi_temp_register(struct iio_capi_temp *temp, const char *name,
		struct capi_temp_handle *const *handles, unsigned int nb_handles,
		const struct iio_capi_ext *ext);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_TEMP_H_ */
