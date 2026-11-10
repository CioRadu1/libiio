/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Serves an accelerometer behind capi_accel (any driver's capi_accel_ops)
 * as an IIO device: input channels
 * accel_x, accel_y and accel_z, with raw, scale (m/s^2 per code; writable
 * with a scale_available list when the driver lists its ranges, writing a
 * listed scale setting its range),
 * sampling_frequency (Hz), calibbias (the part's calibration offset of the
 * axis) and filter_high_pass_3db_frequency (Hz), each there when the driver has its getter and writable when it has
 * its setter, plus <attribute>_available lists for the writable ones the
 * driver lists (sampling_frequency, the high-pass filter); sampling_frequency
 * is the device's instead, as a device attribute, when the driver says its
 * ODR is device-wide (odr_device_wide: it paces temp too), in a buffer of whole
 * scans laid out as the part's registers hold the codes (the handle's
 * resolution, shift and big_endian: be:s20/32>>4 for the ADXL355,
 * temp be:u12/16>>0); and, given the part's temperature sensor (a capi_temp), a temp
 * channel as iio_capi_temp.h has it, in scans after the axes with its raw
 * code laid out as the temperature handle says, when the driver gives
 * those. The part samples temp apart from the axes: in a stream, each
 * chunk of up to 32 samples gets the reading taken after it.
 *
 * With a driver that streams (stream_enable, stream_disable, read_samples),
 * the buffer's scans are the part's samples, one per sampling_frequency
 * period, each following the previous one; where samples were lost (the
 * part's FIFO filled up while a block was being sent), the stream is
 * restarted, so a capture has a gap there. Without, a scan is the latest
 * sample (capi_accel_read_raw()), read as fast as the buffer asks.
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_ACCEL_H_
#define CAPI_INCLUDE_IIO_CAPI_ACCEL_H_

#include <capi_accel.h>
#include <capi_temp.h>
#include <iio_capi_table.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The channels: the three axes, then temp. */
#define IIO_CAPI_ACCEL_NB_CHANNELS 4

/* One accelerometer served. Filled by iio_capi_accel_register(); keep it
 * and the handle for as long as the context exists. */
struct iio_capi_accel {
	struct iio_capi_table_device table;
	struct iio_capi_chan chans[IIO_CAPI_ACCEL_NB_CHANNELS];
	struct iio_capi_table_ops ops;
	struct capi_accel_handle *handle;
	struct capi_temp_handle *temp;
};

/*
 * Add an accelerometer to the context. handle is what capi_accel_init()
 * gave; its resolution is the bits of a sample. scale and
 * sampling_frequency are there when the driver has get_scale and get_odr;
 * sampling_frequency is writable when it has set_odr. temp, what
 * capi_temp_init() gave for the part's temperature sensor, adds the temp
 * channel; NULL leaves it out. ext is the part's own attributes (on the
 * channels, the device, its buffer, or debug ones), iio_capi_table.h; NULL
 * for none. Returns 0,
 * -EINVAL for missing arguments, a
 * driver without read_raw, a resolution of 0 or over 32, or a temp that
 * iio_capi_temp_chan() refuses, or what iio_capi_table_register() returns.
 */
int iio_capi_accel_register(struct iio_capi_accel *accel, const char *name,
		struct capi_accel_handle *handle, struct capi_temp_handle *temp,
		const struct iio_capi_ext *ext);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_ACCEL_H_ */
