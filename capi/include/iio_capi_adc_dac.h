/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Serves a part that is an ADC and a DAC at once (no-OS's ad5592r,
 * ad74413r: inputs and outputs of one IIO device) from its capi_adc and
 * capi_dac channels: the ADC's channels as iio_capi_adc has them, then the
 * DAC's as iio_capi_dac has them, in one IIO device.
 *
 * The device has one buffer, the ADC's (an IIO buffer has one direction):
 * its scans are the ADC's channels, as iio_capi_adc captures them; the
 * DAC's channels are written through raw only.
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_ADC_DAC_H_
#define CAPI_INCLUDE_IIO_CAPI_ADC_DAC_H_

#include <iio_capi_adc.h>
#include <iio_capi_dac.h>
#include <iio_capi_desc.h>
#include <iio_capi_table.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The part's channels: those of its ADC (capi_adc_chan_init()'s handles,
 * of one ADC) and of its DAC (capi_dac_chan_init()'s, of one DAC, codes of
 * dac_resolution bits), each with its descriptions (iio_capi_desc.h, NULL
 * for voltage<chan_id>). */
struct iio_capi_adc_dac_config {
	struct capi_adc_chan_handle **adc_chans;
	const struct iio_capi_chan_desc *adc_descs;
	unsigned int nb_adc_chans;
	struct capi_dac_chan_handle **dac_chans;
	const struct iio_capi_chan_desc *dac_descs;
	unsigned int nb_dac_chans;
	uint8_t dac_resolution;
};

/* One part served. Filled by iio_capi_adc_dac_register(); keep it, the
 * channel handles and the descriptions for as long as the context exists. */
struct iio_capi_adc_dac {
	struct iio_capi_table_device table;
	struct iio_capi_chan chans[IIO_CAPI_ADC_MAX_CHANNELS + IIO_CAPI_DAC_MAX_CHANNELS];
	struct iio_capi_table_ops ops;
	/* What serves each half; their own tables are not registered */
	struct iio_capi_adc adc;
	struct iio_capi_dac dac;
};

/*
 * Add the part to the context as the IIO device name: its ADC's channels
 * (inputs), then its DAC's (outputs). Two channels may have the same id
 * when one is an input and the other an output (voltage0 of each), as in
 * Linux. ext is the part's own attributes, as for the other adapters; NULL
 * for none. The registers are the ADC driver's reg_access op, or else the
 * DAC driver's (ext's reg_access, given, is served instead).
 *
 * Returns 0, -EINVAL for missing arguments, no ADC or no DAC channels, or
 * what iio_capi_adc_register_desc(), iio_capi_dac_register_desc() and
 * iio_capi_table_register() refuse.
 */
int iio_capi_adc_dac_register(struct iio_capi_adc_dac *ad, const char *name,
		const struct iio_capi_adc_dac_config *config, const struct iio_capi_ext *ext);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_ADC_DAC_H_ */
