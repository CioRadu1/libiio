/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIO_ADC_H
#define IIO_ADC_H

struct noos_iio_device_info;

int iio_adc_init(void);
int iio_adc_get_device_info(struct noos_iio_device_info *info);

#endif /* IIO_ADC_H */
