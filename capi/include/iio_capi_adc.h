/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Serves an ADC behind capi_adc (any driver's capi_adc_ops) as an IIO
 * device: one input voltage<chan_id> channel per CAPI channel, or as the
 * part describes it (descs), with raw and,
 * given the reference voltage, scale, in a buffer of whole scans.
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_ADC_H_
#define CAPI_INCLUDE_IIO_CAPI_ADC_H_

#include <capi_adc.h>
#include <iio_capi_desc.h>
#include <iio_capi_table.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IIO_CAPI_ADC_MAX_CHANNELS
#define IIO_CAPI_ADC_MAX_CHANNELS 16
#endif

/* One ADC served. Filled by iio_capi_adc_register(); keep it, the channel
 * handles and their configurations for as long as the context exists. */
struct iio_capi_adc {
	struct iio_capi_table_device table;
	struct iio_capi_chan chans[IIO_CAPI_ADC_MAX_CHANNELS];
	struct iio_capi_table_ops ops;
	struct capi_adc_chan_handle **handles;
	/* The stream's channels, in table order */
	struct capi_adc_chan_handle *stream_chans[IIO_CAPI_ADC_MAX_CHANNELS];
	uint32_t stream_nb_chans;
};

/*
 * Add an ADC to the context. chans are the handles capi_adc_chan_init()
 * gave, all of one ADC, in the order of the channels: their chan_id numbers
 * the channels (voltage<chan_id>), and their resolution, is_signed, shift
 * and big_endian, as the driver set them, are a sample's layout in scans.
 *
 * What a channel has follows from the driver's ops, as for every CAPI
 * adapter: scale (mV per code), offset (codes: (raw + offset) * scale is
 * the input in mV) and sampling_frequency (Hz) when the driver gives them,
 * scale and sampling_frequency writable when it sets them and with an
 * _available list when it lists them.
 *
 * With a driver that streams (stream_enable, stream_disable, read_samples),
 * the buffer's scans are the ADC's conversions, one per sample period,
 * each following the previous one; where scans were lost (-EOVERFLOW: the
 * driver could not keep them while a block was being sent), the stream is
 * restarted, so a capture has a gap there. Without, a scan is a
 * capi_adc_chan_read() of each channel, as fast as the buffer asks.
 *
 * ext is the part's own attributes (on the channels, the device, its buffer,
 * or debug ones), iio_capi_table.h, served after these; NULL for none. The
 * driver's reg_access op, if it has one, is the debug attribute
 * direct_reg_access (ext's reg_access, given, is served instead).
 *
 * descs says what each channel is (iio_capi_desc.h), descs[i] for chans[i]:
 * its type, number, modifier, differential pair, name, label and address,
 * where capi_adc does not say; NULL for voltage<chan_id> channels. A
 * channel's label, where descs gives none, is the one its driver set on
 * its handle (capi_adc_chan_handle.label), if any.
 *
 * Returns 0, -EINVAL for missing arguments, no channels or more than
 * IIO_CAPI_ADC_MAX_CHANNELS, channels of different ADCs, a resolution of 0
 * or one that does not fit 32 bits with its shift, descriptions
 * iio_capi_table_register() refuses (two channels of the same id, a
 * differential one without numbers ...), or what iio_capi_table_register()
 * returns.
 */
int iio_capi_adc_register(struct iio_capi_adc *adc, const char *name,
		struct capi_adc_chan_handle **chans, const struct iio_capi_chan_desc *descs,
		unsigned int nb_chans, const struct iio_capi_ext *ext);

/*
 * What iio_capi_adc_register() does but the registering: adc's table
 * filled (descs NULL for voltage<chan_id> channels), for the ADC of a part
 * that is more classes (an AD5592R's, beside its DAC: iio_capi_part.h).
 * Same errors, but those of iio_capi_table_register(), which
 * iio_capi_part_register() returns.
 */
int iio_capi_adc_init(struct iio_capi_adc *adc, struct capi_adc_chan_handle **chans,
		const struct iio_capi_chan_desc *descs, unsigned int nb_chans);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_ADC_H_ */
