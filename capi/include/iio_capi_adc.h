/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_ADC_H_
#define CAPI_INCLUDE_IIO_CAPI_ADC_H_

#include <capi_adc.h>
#include <iio_device.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Any capi_adc driver as an IIO device, the CAPI counterpart of zephyr's
 * drivers/iio_device/io_channels.c. Each channel gets "raw" (one
 * capi_adc_chan_read()) and, when resolution and vref_mv are known,
 * "scale" in mV per LSB.
 *
 * What zephyr reads from the devicetree, the application gives here:
 * capi_adc has no call that returns a channel's resolution or the
 * reference voltage.
 */
struct iio_capi_adc_channel {
	/* IIO channel id, e.g. "voltage0" */
	const char *id;
	/* From capi_adc_chan_init() */
	struct capi_adc_chan_handle *handle;
	/* Bits per sample; 0 leaves out "scale" */
	uint8_t resolution;
	/* Reference voltage in mV; 0 leaves out "scale" */
	uint16_t vref_mv;
};

struct iio_capi_adc {
	const struct iio_capi_adc_channel *channels;
	size_t num_channels;
};

/* Use with struct iio_capi_device: .handle = a struct iio_capi_adc *,
 * .api = &iio_capi_adc_api. */
extern const struct iio_capi_device_api iio_capi_adc_api;

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_ADC_H_ */
