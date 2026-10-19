/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_IIO_DRIVER_H_
#define NOOS_IIO_DRIVER_H_

/*
 * Serves an existing no-OS IIO driver (iio_<part>.c, which describes its
 * device with a no-OS struct iio_device) through this libiio backend, with
 * no per-driver code.
 *
 * This header includes neither libiio's iio.h nor no-OS's iio_types.h, as
 * both define struct iio_device: a board file can include it next to the
 * no-OS driver headers.
 */

#include <stdbool.h>
#include <stdint.h>

struct noos_iio_device_info;

/**
 * struct noos_iio_driver - one no-OS driver instance to serve
 * @name:       IIO device name the host sees (e.g. "adc_demo")
 * @descriptor: the driver's no-OS struct iio_device (e.g.
 *              &adc_demo_iio_descriptor)
 * @instance:   the driver's device handle, what its show/store/submit get;
 *              set it after the driver's init, before the context is built
 * @enabled, @mask: kept by iio_noos_driver.c; leave them zero. pre_enable needs
 *              the channel mask, which is only known at the first transfer
 */
struct noos_iio_driver {
	const char *name;
	void *descriptor;
	void *instance;
	bool enabled;
	uint32_t mask;
};

/* Describe a served no-OS driver for noos_iio_register_device(). */
int noos_iio_driver_get_device_info(struct noos_iio_driver *drv,
				    struct noos_iio_device_info *info);

#endif /* NOOS_IIO_DRIVER_H_ */
