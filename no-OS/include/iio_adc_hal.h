/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIO_ADC_HAL_H
#define IIO_ADC_HAL_H

#define IIO_ADC_MAX_CHANNELS 8

struct iio_adc_hal {
	const char *const *channels;
	unsigned int       num_channels;
	unsigned int       resolution_bits;
	int                ref_voltage_mv;
	int (*init)(void);
	int (*read_raw)(unsigned int channel, int *value);
};

extern const struct iio_adc_hal iio_adc_hal;

#endif /* IIO_ADC_HAL_H */
