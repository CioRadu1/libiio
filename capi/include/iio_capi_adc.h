/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Serves an ADC behind capi_adc (any driver's capi_adc_ops) as an IIO
 * device: one input voltage<chan_id> channel per CAPI channel, with raw and,
 * given the reference voltage, scale, in a buffer of whole scans.
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_ADC_H_
#define CAPI_INCLUDE_IIO_CAPI_ADC_H_

#include <capi_adc.h>
#include <iio_capi_table.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IIO_CAPI_ADC_MAX_CHANNELS
#define IIO_CAPI_ADC_MAX_CHANNELS 16
#endif

/* Samples one capi_adc_stream_start() captures, over the enabled channels. */
#ifndef IIO_CAPI_ADC_STREAM_SAMPLES
#define IIO_CAPI_ADC_STREAM_SAMPLES 256
#endif

/* One ADC served. Filled by iio_capi_adc_register(); keep it, the channel
 * handles and their configurations for as long as the context exists. */
struct iio_capi_adc {
	struct iio_capi_table_device table;
	struct iio_capi_chan chans[IIO_CAPI_ADC_MAX_CHANNELS];
	struct capi_adc_chan_handle **handles;
	const struct capi_adc_chan_config *configs;
	uint32_t vref_mv;
	/* The driver reports each channel's scale (capi_adc_chan_get_scale()). */
	bool drv_scale;
	/* The capture: the enabled channels, and the scans of the last
	 * capi_adc_stream_start() not handed out yet. */
	struct capi_adc_chan_handle *stream_chans[IIO_CAPI_ADC_MAX_CHANNELS];
	uint32_t stream_buf[IIO_CAPI_ADC_STREAM_SAMPLES];
	uint32_t stream_nb_chans, stream_scans, stream_pos;
	/* The driver has no buffered capture: a scan is a capi_adc_chan_read()
	 * of each channel. */
	bool no_stream;
};

/*
 * Add an ADC to the context. chans are the handles capi_adc_chan_init()
 * gave for configs, in that order: their chan_id numbers the channels and
 * their resolution is the bits of a sample. scale is the driver's
 * (capi_adc_chan_get_scale()) when it reports one, else vref_mv / 2^resolution,
 * vref_mv being the reference voltage in mV; with neither, scale is left out.
 * offset and sampling_frequency are there when the driver reports a channel's
 * code offset and sample rate. A capture is paced by the
 * ADC (capi_adc_stream_start()) when the driver can, else it is a
 * capi_adc_chan_read() of each channel per scan. Returns 0, -EINVAL for
 * missing arguments, no channels or more than IIO_CAPI_ADC_MAX_CHANNELS, a
 * resolution of 0 or over 32, or what iio_capi_table_register() returns.
 */
int iio_capi_adc_register(struct iio_capi_adc *adc, const char *name,
		struct capi_adc_chan_handle **chans, const struct capi_adc_chan_config *configs,
		unsigned int nb_chans, uint32_t vref_mv);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_ADC_H_ */
