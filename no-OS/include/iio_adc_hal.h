/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIO_ADC_HAL_H
#define IIO_ADC_HAL_H

#define IIO_ADC_MAX_CHANNELS 8

int iio_adc_hal_init(void);
unsigned int iio_adc_hal_num_channels(void);
const char *iio_adc_hal_channel_id(unsigned int channel);
int iio_adc_hal_read_raw(unsigned int channel, int *value);

#endif /* IIO_ADC_HAL_H */
