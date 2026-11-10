/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/* Shared by the adapters; not for applications. */

#ifndef IIO_CAPI_PRIV_H_
#define IIO_CAPI_PRIV_H_

#include <iio_capi_adc.h>
#include <iio_capi_dac.h>

/* What iio_capi_adc_register_desc() and iio_capi_dac_register_desc() do,
 * but iio_capi_table_register(): the table filled, for an adapter that
 * serves it within its own (iio_capi_adc_dac). Same arguments and errors. */
int iio_capi_adc_prepare(struct iio_capi_adc *adc, const char *name,
		struct capi_adc_chan_handle **chans, const struct iio_capi_chan_desc *descs,
		unsigned int nb_chans, const struct iio_capi_ext *ext);
int iio_capi_dac_prepare(struct iio_capi_dac *dac, const char *name,
		struct capi_dac_chan_handle **chans, const struct iio_capi_chan_desc *descs,
		unsigned int nb_chans, uint8_t resolution, const struct iio_capi_ext *ext);

#endif /* IIO_CAPI_PRIV_H_ */
