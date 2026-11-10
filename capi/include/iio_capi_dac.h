/*
 * Copyright (c) 2026 Analog Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

/*
 * A DAC behind capi_dac as an IIO device: one output voltage channel per
 * channel handle, with raw (written to the channel; read back as last
 * written) and an output buffer, its scans written channel by channel.
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_DAC_H_
#define CAPI_INCLUDE_IIO_CAPI_DAC_H_

#include <capi_dac.h>
#include <iio_capi_table.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IIO_CAPI_DAC_MAX_CHANNELS
#define IIO_CAPI_DAC_MAX_CHANNELS 16
#endif

struct iio_capi_dac {
	struct iio_capi_table_device table;
	struct iio_capi_chan chans[IIO_CAPI_DAC_MAX_CHANNELS];
	struct iio_capi_table_ops ops;
	struct capi_dac_chan_handle **handles;
	/* What each channel was last written */
	uint32_t last[IIO_CAPI_DAC_MAX_CHANNELS];
};

/* Serve the channels (one DAC's) as the IIO device name. resolution is the
 * bits of a code: capi_dac's channel handles do not say it. ext is the
 * part's own attributes (on the channels, the device, its buffer, or debug
 * ones), iio_capi_table.h; NULL for none. */
int iio_capi_dac_register(struct iio_capi_dac *dac, const char *name,
		struct capi_dac_chan_handle **chans, unsigned int nb_chans, uint8_t resolution,
		const struct iio_capi_ext *ext);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_DAC_H_ */
