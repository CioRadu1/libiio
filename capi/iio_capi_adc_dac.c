/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * An ADC and a DAC of one part as one table device: iio_capi_adc and
 * iio_capi_dac fill a table each, not registered; this table has the ADC's
 * channels, then the DAC's, and hands each call to the half the channel is
 * of, with that half's own channel.
 */

#include <errno.h>
#include <iio_capi_adc_dac.h>
#include <iio_capi_table.h>
#include <stdbool.h>
#include <stdint.h>
#include "iio_capi_priv.h"

/* The half chan is of, and its own channel there; false for the DAC. */
static bool ad_half(struct iio_capi_adc_dac *ad, const struct iio_capi_chan *chan,
		const struct iio_capi_chan **own)
{
	unsigned int i = (unsigned int)(chan - ad->chans);
	unsigned int nb_adc = ad->adc.table.nb_channels;

	if (i < nb_adc) {
		*own = &ad->adc.chans[i];
		return true;
	}

	*own = &ad->dac.chans[i - nb_adc];
	return false;
}

static int ad_read_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		int32_t *val, int32_t *val2)
{
	struct iio_capi_adc_dac *ad = dev;
	const struct iio_capi_chan *own;

	/* The device has no values of its own */
	if (!chan)
		return -EINVAL;

	if (ad_half(ad, chan, &own))
		return ad->adc.ops.read_raw(&ad->adc, own, info, val, val2);

	return ad->dac.ops.read_raw(&ad->dac, own, info, val, val2);
}

static int ad_write_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		const struct capi_value *value)
{
	struct iio_capi_adc_dac *ad = dev;
	const struct iio_capi_chan *own;

	if (!chan)
		return -EPERM;

	if (ad_half(ad, chan, &own))
		return ad->adc.ops.write_raw(&ad->adc, own, info, value);

	return ad->dac.ops.write_raw(&ad->dac, own, info, value);
}

/* Only the ADC has lists */
static int ad_read_avail(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		struct capi_value *values, uint32_t *nb)
{
	struct iio_capi_adc_dac *ad = dev;
	const struct iio_capi_chan *own;

	if (!chan || !ad_half(ad, chan, &own))
		return -EINVAL;

	return ad->adc.ops.read_avail(&ad->adc, own, info, values, nb);
}

/* The buffer is the ADC's: its channels come first, so mask is the ADC's
 * mask as it is. */
static int ad_read_scan(void *dev, uint32_t mask, int32_t *values)
{
	struct iio_capi_adc_dac *ad = dev;

	return ad->adc.ops.read_scan(&ad->adc, mask, values);
}

static int ad_read_scans(void *dev, uint32_t mask, int32_t *values, uint32_t nb_scans)
{
	struct iio_capi_adc_dac *ad = dev;

	return ad->adc.ops.read_scans(&ad->adc, mask, values, nb_scans);
}

/* One part: its registers are the ADC's, or else the DAC's */
static int ad_reg_access(void *dev, uint32_t reg, uint32_t writeval, uint32_t *readval)
{
	struct iio_capi_adc_dac *ad = dev;

	if (ad->adc.ops.reg_access)
		return ad->adc.ops.reg_access(&ad->adc, reg, writeval, readval);

	return ad->dac.ops.reg_access(&ad->dac, reg, writeval, readval);
}

static int ad_enable_buffer(void *dev, uint32_t mask, bool enable)
{
	struct iio_capi_adc_dac *ad = dev;

	return ad->adc.ops.enable_buffer(&ad->adc, mask, enable);
}

int iio_capi_adc_dac_register(struct iio_capi_adc_dac *ad, const char *name,
		const struct iio_capi_adc_dac_config *config, const struct iio_capi_ext *ext)
{
	unsigned int i, nb_adc, nb_dac;
	int ret;

	if (!ad || !name || !config || !config->nb_adc_chans || !config->nb_dac_chans)
		return -EINVAL;

	ret = iio_capi_adc_prepare(&ad->adc, name, config->adc_chans, config->adc_descs,
			config->nb_adc_chans, NULL);
	if (ret)
		return ret;

	ret = iio_capi_dac_prepare(&ad->dac, name, config->dac_chans, config->dac_descs,
			config->nb_dac_chans, config->dac_resolution, NULL);
	if (ret)
		return ret;

	nb_adc = config->nb_adc_chans;
	nb_dac = config->nb_dac_chans;

	for (i = 0; i < nb_adc; i++)
		ad->chans[i] = ad->adc.chans[i];

	/* Not in scans: the buffer is the ADC's */
	for (i = 0; i < nb_dac; i++) {
		ad->chans[nb_adc + i] = ad->dac.chans[i];
		ad->chans[nb_adc + i].scan_index = -1;
	}

	ad->ops = (struct iio_capi_table_ops){
		.read_raw = ad_read_raw,
		.write_raw = ad_write_raw,
		.read_avail = ad_read_avail,
		.read_scan = ad->adc.ops.read_scan ? ad_read_scan : NULL,
		.read_scans = ad->adc.ops.read_scans ? ad_read_scans : NULL,
		.enable_buffer = ad->adc.ops.enable_buffer ? ad_enable_buffer : NULL,
		.reg_access = ad->adc.ops.reg_access || ad->dac.ops.reg_access ?
				ad_reg_access : NULL,
	};

	ad->table = (struct iio_capi_table_device){
		.name = name,
		.dev = ad,
		.channels = ad->chans,
		.nb_channels = nb_adc + nb_dac,
		.ops = &ad->ops,
		.ext = ext,
	};

	return iio_capi_table_register(&ad->table);
}
