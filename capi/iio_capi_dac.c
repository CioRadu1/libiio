/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * A DAC behind capi_dac as a table device: the table has its channels, a
 * raw write and an output buffer's scans go to capi_dac_chan_write(), and
 * the driver's stream is started and stopped with the buffer when it has
 * one.
 */

#include <capi_dac.h>
#include <errno.h>
#include <iio_capi_dac.h>
#include <iio_capi_table.h>
#include <stdbool.h>
#include <stdint.h>

static unsigned int dac_chan_index(const struct iio_capi_dac *dac, const struct iio_capi_chan *chan)
{
	return (unsigned int)(chan - dac->chans);
}

static int dac_write(struct iio_capi_dac *dac, unsigned int i, int32_t val)
{
	/* What capi_dac_chan_write() implementations take. */
	uint32_t code = (uint32_t)val;
	int ret;

	ret = capi_dac_chan_write(dac->handles[i], &code, 1);
	if (ret)
		return ret;

	dac->last[i] = code;

	return 0;
}

/* raw: the code the channel was last written, as the DAC has no read. */
static int dac_read_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		int32_t *val, int32_t *val2)
{
	const struct iio_capi_dac *dac = dev;

	(void)val2;

	if (info != IIO_CAPI_INFO_RAW)
		return -EINVAL;

	*val = (int32_t)dac->last[dac_chan_index(dac, chan)];

	return IIO_CAPI_VAL_INT;
}

static int dac_write_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		const struct capi_value *value)
{
	struct iio_capi_dac *dac = dev;

	if (info != IIO_CAPI_INFO_RAW)
		return -EPERM;

	/* A code is whole: what is written as INT_PLUS_NANO, its val. */
	return dac_write(dac, dac_chan_index(dac, chan), value->val);
}

/* The driver's stream of the channels in mask, when it has one. */
static int dac_enable_buffer(void *dev, uint32_t mask, bool enable)
{
	struct iio_capi_dac *dac = dev;
	const struct capi_dac_ops *ops = dac->handles[0]->dac_handle->dac_ops;
	unsigned int i;
	int ret;

	for (i = 0; i < dac->table.nb_channels; i++) {
		if (!(mask & (1u << i)))
			continue;

		if (enable && ops->chan_stream_enable)
			ret = capi_dac_chan_stream_enable(dac->handles[i]);
		else if (!enable && ops->chan_stream_disable)
			ret = capi_dac_chan_stream_disable(dac->handles[i]);
		else
			ret = 0;
		if (ret)
			return ret;
	}

	return 0;
}

/* A scan: a capi_dac_chan_write() of each channel in mask. */
static int dac_write_scan(void *dev, uint32_t mask, const int32_t *values)
{
	struct iio_capi_dac *dac = dev;
	unsigned int i, k = 0;
	int ret;

	for (i = 0; i < dac->table.nb_channels; i++) {
		if (!(mask & (1u << i)))
			continue;

		ret = dac_write(dac, i, values[k++]);
		if (ret)
			return ret;
	}

	return 0;
}

/* direct_reg_access: the DAC's registers */
static int dac_reg_access(void *dev, uint32_t reg, uint32_t writeval, uint32_t *readval)
{
	struct iio_capi_dac *dac = dev;

	return capi_dac_reg_access(dac->handles[0]->dac_handle, reg, writeval, readval);
}

int iio_capi_dac_register(struct iio_capi_dac *dac, const char *name,
		struct capi_dac_chan_handle **chans, unsigned int nb_chans, uint8_t resolution,
		const struct iio_capi_ext *ext)
{
	unsigned int i;

	if (!dac || !name || !chans || !nb_chans || nb_chans > IIO_CAPI_DAC_MAX_CHANNELS ||
			!resolution || resolution > 32 || !chans[0] || !chans[0]->dac_handle ||
			!chans[0]->dac_handle->dac_ops)
		return -EINVAL;

	for (i = 0; i < nb_chans; i++) {
		const struct capi_dac_chan_handle *h = chans[i];

		if (!h || h->dac_handle != chans[0]->dac_handle)
			return -EINVAL;

		dac->chans[i] = (struct iio_capi_chan){
			.type = IIO_CAPI_VOLTAGE,
			.channel = (int)h->chan_id,
			.output = true,
			.info = IIO_CAPI_INFO_BIT(IIO_CAPI_INFO_RAW),
			.scan_index = (int)i,
			.scan = iio_capi_scan_of(resolution, false, 0, false),
		};
		dac->last[i] = 0;
	}

	dac->handles = chans;

	dac->table.name = name;
	dac->table.dev = dac;
	dac->table.channels = dac->chans;
	dac->table.nb_channels = nb_chans;
	dac->ops = (struct iio_capi_table_ops){
		.read_raw = dac_read_raw,
		.write_raw = dac_write_raw,
		.enable_buffer = dac_enable_buffer,
		.write_scan = dac_write_scan,
	};
	if (chans[0]->dac_handle->dac_ops->reg_access)
		dac->ops.reg_access = dac_reg_access;
	dac->table.ops = &dac->ops;
	dac->table.info = 0;
	dac->table.info_avail = 0;
	dac->table.ext = ext;

	return iio_capi_table_register(&dac->table);
}
