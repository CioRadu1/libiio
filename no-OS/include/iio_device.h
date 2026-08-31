/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_INCLUDE_IIO_DEVICE_H_
#define NOOS_INCLUDE_IIO_DEVICE_H_

#include <iio/iio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of IIO devices that can be registered */
#define NOOS_IIO_MAX_DEVICES 16

/*
 * Callback types – mirror the Zephyr iio_device_driver_api but take
 * a generic void * instead of "const struct device *".
 */
typedef int (*noos_iio_add_channels_t)(void *dev,
		struct iio_device *iio_device);

typedef int (*noos_iio_read_attr_t)(void *dev,
		const struct iio_device *iio_device,
		const struct iio_attr *attr,
		char *dst, size_t len);

typedef int (*noos_iio_write_attr_t)(void *dev,
		const struct iio_device *iio_device,
		const struct iio_attr *attr,
		const char *src, size_t len);

typedef int (*noos_iio_read_samples_t)(void *dev, void *data, size_t bytes);
typedef int (*noos_iio_write_samples_t)(void *dev, const void *data,
					size_t bytes);

/**
 * struct noos_iio_device_info - describes one IIO device for the backend
 * @name:          human-readable device name
 * @dev:           pointer to the no-OS device instance (opaque)
 * @direction:     0 = input (RX), 1 = output (TX)
 * @add_channels:  populate channels on the iio_device (may be NULL)
 * @read_attr:     read an attribute value (may be NULL)
 * @write_attr:    write an attribute value (may be NULL)
 * @read_samples:  read sample data from hardware (RX, may be NULL)
 * @write_samples: write sample data to hardware (TX, may be NULL)
 */
struct noos_iio_device_info {
	const char *name;
	void *dev;
	int direction;
	noos_iio_add_channels_t add_channels;
	noos_iio_read_attr_t read_attr;
	noos_iio_write_attr_t write_attr;
	noos_iio_read_samples_t read_samples;
	noos_iio_write_samples_t write_samples;
};

/**
 * noos_iio_register_device() - register an IIO device before context creation
 * @info: pointer to a device info structure (contents are copied)
 *
 * Returns 0 on success, -ENOMEM if the device table is full.
 */
int noos_iio_register_device(const struct noos_iio_device_info *info);

/* Accessed by the backend – not for direct application use */
extern struct noos_iio_device_info noos_iio_devices[];
extern unsigned int noos_iio_device_count;

#ifdef __cplusplus
}
#endif

#endif /* NOOS_INCLUDE_IIO_DEVICE_H_ */
