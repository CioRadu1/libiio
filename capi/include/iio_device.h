/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CAPI_INCLUDE_IIO_DEVICE_H_
#define CAPI_INCLUDE_IIO_DEVICE_H_

#include <iio/iio-backend.h>
#include <iio/iio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iio_capi_device;

typedef int (*iio_capi_device_add_channels_t)(
		const struct iio_capi_device *dev, struct iio_device *iio_device);

typedef int (*iio_capi_device_read_attr_t)(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr, char *dst,
		size_t len);

typedef int (*iio_capi_device_write_attr_t)(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_attr *attr, const char *src,
		size_t len);

/* Fill (read) or consume (write) bytes of samples, whole scans, packed as
 * libiio lays them out for the channels enabled in mask. */
typedef int (*iio_capi_device_read_samples_t)(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_channels_mask *mask,
		void *data, size_t bytes);

typedef int (*iio_capi_device_write_samples_t)(const struct iio_capi_device *dev,
		const struct iio_device *iio_device, const struct iio_channels_mask *mask,
		const void *data, size_t bytes);

/* Buffer enabled or disabled by the client. */
typedef int (*iio_capi_device_enable_buffer_t)(const struct iio_capi_device *dev, bool enable);

/* Debug register access (iio_reg), as no-OS's reg_read/reg_write. */
typedef int (*iio_capi_device_reg_read_t)(
		const struct iio_capi_device *dev, uint32_t reg, uint32_t *val);

typedef int (*iio_capi_device_reg_write_t)(
		const struct iio_capi_device *dev, uint32_t reg, uint32_t val);

/* What zephyr's struct iio_device_driver_api is to a Zephyr device. The
 * buffer ops are optional: with read_samples the device gets an input
 * buffer, with only write_samples an output one (as in no-OS). So are the
 * register ops. */
struct iio_capi_device_api {
	iio_capi_device_add_channels_t add_channels;
	iio_capi_device_read_attr_t read_attr;
	iio_capi_device_write_attr_t write_attr;
	iio_capi_device_read_samples_t read_samples;
	iio_capi_device_write_samples_t write_samples;
	iio_capi_device_enable_buffer_t enable_buffer;
	iio_capi_device_reg_read_t reg_read;
	iio_capi_device_reg_write_t reg_write;
};

/* One device served to libiio clients. CAPI has no device model, so the
 * application fills one of these per device and registers it; zephyr
 * builds the same list from its devicetree (struct iio_device_info). */
struct iio_capi_device {
	/* IIO device name */
	const char *name;
	/* What api works on, e.g. a struct iio_capi_adc * for iio_capi_adc_api */
	void *handle;
	const struct iio_capi_device_api *api;
	/* Set by iio_capi_device_register() */
	struct iio_capi_device *next;
};

/* Add a device to the context. Call before the context is created;
 * dev must stay valid for as long as the context exists. */
int iio_capi_device_register(struct iio_capi_device *dev);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_DEVICE_H_ */
