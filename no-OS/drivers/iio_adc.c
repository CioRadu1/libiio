/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <no_os_print_log.h>
#include <no_os_util.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "iio_adc.h"
#include "iio_val.h"
#include <iio/iio-backend.h>
#include "adc.h"

struct adc_channel_map
{
	const char *id;
	mxc_adc_chsel_t channel;
	bool is_temp;
};

static const struct adc_channel_map channel_map[] = {
	{"voltage0", MXC_ADC_CH_0, false},
};

#define NUM_CHANNELS (sizeof(channel_map) / sizeof(channel_map[0]))

/*
 * String-valued attributes are stored as an index into a table of allowed
 * strings (mirrors the way Linux/Zephyr expose enumerated attributes).
 */
static const char *const gain_values[] = {
	"1/6", "1/5", "1/4", "2/7", "1/3", "2/5", "1/2", "2/3", "4/5",
	"1", "2", "3", "4", "6", "8", "12", "16", "24", "32", "64", "128",
};
#define GAIN_DEFAULT_IDX 9 /* "1" */

static const char *const reference_values[] = {
	"VDD", "VDD/2", "VDD/3", "VDD/4",
	"Internal", "External0", "External1",
};
#define REFERENCE_DEFAULT_IDX 4 /* "Internal" */

/* Internal reference voltage in mV (device-level, read-only). */
#define ADC_INTERNAL_REF_MV 1250

/*
 * Per-channel configuration state. Held locally instead of being pushed into
 * the ADC HAL; the values are reported/accepted through the attribute layer.
 */
struct adc_channel_state {
	int scale_val;		/* IIO_VAL_INT_PLUS_MICRO integer part  */
	int scale_val2;		/* IIO_VAL_INT_PLUS_MICRO fractional part */
	unsigned int gain;	/* index into gain_values[]             */
	unsigned int reference;	/* index into reference_values[]        */
	int differential;	/* 0 or 1                               */
};

static struct adc_channel_state chan_state[NUM_CHANNELS];

#define ADC_POLL_TIMEOUT	1000000

/*
 * iio_adc_get_fmt() - value type for a given attribute (Linux write_raw_get_fmt
 * analog). Drives how the attribute string is formatted and parsed.
 */
static enum iio_val_type iio_adc_get_fmt(const char *attr_name)
{
	if (strcmp(attr_name, "scale") == 0)
		return IIO_VAL_INT_PLUS_MICRO;

	if (strcmp(attr_name, "gain") == 0 ||
	    strcmp(attr_name, "reference") == 0)
		return IIO_VAL_CHAR;	/* string-valued */

	/* raw, process, differential, internal_ref_voltage, ... are integers */
	return IIO_VAL_INT;
}

/* Look up a string in a values[] table; returns index or -EINVAL. */
static int adc_lookup_str(const char *const *table, size_t count,
			  const char *src, size_t len)
{
	size_t i, slen = len;

	/* Trim a trailing newline / NUL from the wire value. */
	while (slen > 0 && (src[slen - 1] == '\n' || src[slen - 1] == '\0'))
		slen--;

	for (i = 0; i < count; i++) {
		if (table[i] && strlen(table[i]) == slen &&
		    strncmp(src, table[i], slen) == 0)
			return (int)i;
	}

	return -EINVAL;
}

static void iio_adc_state_init(void)
{
	unsigned int i;

	for (i = 0; i < NUM_CHANNELS; i++) {
		chan_state[i].scale_val = 1;
		chan_state[i].scale_val2 = 0;
		chan_state[i].gain = GAIN_DEFAULT_IDX;
		chan_state[i].reference = REFERENCE_DEFAULT_IDX;
		chan_state[i].differential = 0;
	}
}

static int adc_read_raw(mxc_adc_chsel_t channel, bool is_temp, int *value)
{
	mxc_adc_slot_req_t slot_req = {
		.channel = channel,
	};
	mxc_adc_conversion_req_t conv_req = {
		.mode = MXC_ADC_ATOMIC_CONV,
		.trig = MXC_ADC_TRIG_SOFTWARE,
		.fifo_format = MXC_ADC_DATA,
		.fifo_threshold = 0,
		.avg_number = MXC_ADC_AVG_1,
		.num_slots = 1,
	};
	volatile uint32_t timeout;
	int ret;

	if (is_temp)
		MXC_ADC_TS_SelectEnable();

	MXC_ADC_Clear_ChannelSelect();

	ret = MXC_ADC_SlotConfiguration(&slot_req, 0);
	if (ret)
		goto out;

	ret = MXC_ADC_Configuration(&conv_req);
	if (ret)
		goto out;

	ret = MXC_ADC_StartConversion();
	if (ret)
		goto out;

	/* Wait for sequence to complete with timeout */
	timeout = ADC_POLL_TIMEOUT;
	while (!(MXC_ADC_GetFlags() & MXC_F_ADC_INTFL_SEQ_DONE)) {
		if (--timeout == 0) {
			ret = -ETIMEDOUT;
			goto out_disable;
		}
	}

	/* Clear the sequence-done flag before reading to prevent stale state */
	MXC_ADC_ClearFlags(MXC_F_ADC_INTFL_SEQ_DONE);

	/* GetData returns number of FIFO entries read (1 = success) */
	ret = MXC_ADC_GetData(value);
	if (ret > 0)
		ret = 0;

	/* FIFO returns data + status bits; mask to 12-bit ADC result */
	*value &= 0xFFF;

out_disable:
	MXC_ADC_DisableConversion();

out:
	if (is_temp)
		MXC_ADC_TS_SelectDisable();

	return ret;
}

static const struct iio_data_format adc_fmt = {
	.length = 16,
	.bits = 12,
	.is_signed = false,
};

static int iio_adc_read_samples(void *dev, void *data, size_t bytes)
{
	size_t num_samples = bytes / sizeof(uint16_t);
	uint16_t *buffer = (uint16_t *)data;
	int raw;

	for (size_t i = 0; i < num_samples; i++)
	{
		int ret = adc_read_raw(MXC_ADC_CH_0, false, &raw);
		if (ret)
			return ret;
		buffer[i] = raw & 0xFFF;
	}

	return 0;
}

static int iio_adc_add_channels(void *dev, struct iio_device *iio_dev)
{
	struct iio_channel *ch;
	unsigned int i;

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		ch = iio_device_add_channel(iio_dev, (long)i,
									channel_map[i].id,
									NULL, NULL,
									false, true, &adc_fmt);
		if (!ch)
			return -ENOMEM;

		iio_channel_add_attr(ch, "raw", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "scale", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "gain", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "process", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "reference", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "differential", IIO_ATTR_TYPE_CHANNEL, NULL);
	}

	/* Device-level attribute (read-only). */
	iio_device_add_attr(iio_dev, "internal_ref_voltage", IIO_ATTR_TYPE_DEVICE);

	return 0;
}

static int iio_adc_read_attr(void *dev,
							 const struct iio_device *iio_dev,
							 const struct iio_attr *attr,
							 char *dst, size_t len)
{
	const char *attr_name;
	const char *ch_id;
	unsigned int i;
	int raw_value;
	int ret;
	int vals[2];

	attr_name = iio_attr_get_name(attr);
	if (!attr_name)
		return -EINVAL;

	/* Device-level attributes. */
	if (attr->type == IIO_ATTR_TYPE_DEVICE) {
		if (strcmp(attr_name, "internal_ref_voltage") == 0) {
			vals[0] = ADC_INTERNAL_REF_MV;
			ret = iio_format_value(dst, len, IIO_VAL_INT, 1, vals);
			return (ret < 0) ? ret : ret + 1;
		}
		return -EINVAL;
	}

	if (attr->type != IIO_ATTR_TYPE_CHANNEL || !attr->iio.chn)
		return -EINVAL;

	ch_id = iio_channel_get_id(attr->iio.chn);
	if (!ch_id)
		return -EINVAL;

	for (i = 0; i < NUM_CHANNELS; i++) {
		if (strcmp(ch_id, channel_map[i].id) != 0)
			continue;

		if (strcmp(attr_name, "scale") == 0) {
			vals[0] = chan_state[i].scale_val;
			vals[1] = chan_state[i].scale_val2;
			ret = iio_format_value(dst, len, IIO_VAL_INT_PLUS_MICRO,
					       2, vals);
			return (ret < 0) ? ret : ret + 1;
		}

		if (strcmp(attr_name, "raw") == 0) {
			ret = adc_read_raw(channel_map[i].channel,
					   channel_map[i].is_temp,
					   &raw_value);
			if (ret)
				return ret;

			vals[0] = raw_value;
			ret = iio_format_value(dst, len, IIO_VAL_INT, 1, vals);
			return (ret < 0) ? ret : ret + 1;
		}

		if (strcmp(attr_name, "gain") == 0)
			return snprintf(dst, len, "%s",
					gain_values[chan_state[i].gain]) + 1;

		if (strcmp(attr_name, "reference") == 0)
			return snprintf(dst, len, "%s",
					reference_values[chan_state[i].reference]) + 1;

		if (strcmp(attr_name, "differential") == 0) {
			vals[0] = chan_state[i].differential;
			ret = iio_format_value(dst, len, IIO_VAL_INT, 1, vals);
			return (ret < 0) ? ret : ret + 1;
		}

		if (strcmp(attr_name, "process") == 0) {
			/* Convert a raw sample to millivolts using the channel
			 * scale (scale is mV per LSB in IIO_VAL_INT_PLUS_MICRO
			 * form). */
			int64_t scale_uv;

			ret = adc_read_raw(channel_map[i].channel,
					   channel_map[i].is_temp,
					   &raw_value);
			if (ret)
				return ret;

			scale_uv = (int64_t)chan_state[i].scale_val * 1000000 +
				   chan_state[i].scale_val2;
			vals[0] = (int)((raw_value * scale_uv) / 1000000);
			ret = iio_format_value(dst, len, IIO_VAL_INT, 1, vals);
			return (ret < 0) ? ret : ret + 1;
		}

		return -EINVAL;
	}

	return -EINVAL;
}

static int iio_adc_write_attr(void *dev,
			      const struct iio_device *iio_dev,
			      const struct iio_attr *attr,
			      const char *src, size_t len)
{
	const char *attr_name;
	const char *ch_id;
	unsigned int i;

	attr_name = iio_attr_get_name(attr);
	if (!attr_name)
		return -EINVAL;

	/* Device-level attributes are all read-only. */
	if (attr->type == IIO_ATTR_TYPE_DEVICE)
		return -EPERM;

	if (attr->type != IIO_ATTR_TYPE_CHANNEL || !attr->iio.chn)
		return -EINVAL;

	ch_id = iio_channel_get_id(attr->iio.chn);
	if (!ch_id)
		return -EINVAL;

	for (i = 0; i < NUM_CHANNELS; i++) {
		int fract_mult;
		int integer, fract;
		int idx;
		int ret;

		if (strcmp(ch_id, channel_map[i].id) != 0)
			continue;

		if (strcmp(attr_name, "scale") == 0) {
			fract_mult = iio_val_fract_mult(iio_adc_get_fmt(attr_name));
			if (fract_mult < 0)
				return fract_mult;

			ret = iio_str_to_fixpoint(src, fract_mult,
						  &integer, &fract);
			if (ret)
				return ret;

			chan_state[i].scale_val = integer;
			chan_state[i].scale_val2 = fract;
			return len;
		}

		if (strcmp(attr_name, "gain") == 0) {
			idx = adc_lookup_str(gain_values,
					     NO_OS_ARRAY_SIZE(gain_values),
					     src, len);
			if (idx < 0)
				return idx;
			chan_state[i].gain = (unsigned int)idx;
			return len;
		}

		if (strcmp(attr_name, "reference") == 0) {
			idx = adc_lookup_str(reference_values,
					     NO_OS_ARRAY_SIZE(reference_values),
					     src, len);
			if (idx < 0)
				return idx;
			chan_state[i].reference = (unsigned int)idx;
			return len;
		}

		if (strcmp(attr_name, "differential") == 0) {
			fract_mult = iio_val_fract_mult(iio_adc_get_fmt(attr_name));
			if (fract_mult < 0)
				return fract_mult;

			ret = iio_str_to_fixpoint(src, fract_mult,
						  &integer, &fract);
			if (ret)
				return ret;

			if (integer != 0 && integer != 1)
				return -EINVAL;
			chan_state[i].differential = integer;
			return len;
		}

		/* raw and process are read-only. */
		if (strcmp(attr_name, "raw") == 0 ||
		    strcmp(attr_name, "process") == 0)
			return -EPERM;

		return -EINVAL;
	}

	return -EINVAL;
}

int iio_adc_init(void)
{
	mxc_adc_req_t adc_cfg = {
		.clock = MXC_ADC_CLK_IBRO,
		.clkdiv = MXC_ADC_CLKDIV_4,
		.cal = MXC_ADC_SKIP_CAL,
		.ref = MXC_ADC_REF_INT_1V25,
		.trackCount = 4,
		.idleCount = 17,
	};

	iio_adc_state_init();

	return MXC_ADC_Init(&adc_cfg);
}

int iio_adc_get_device_info(struct noos_iio_device_info *info)
{
	if (!info)
		return -EINVAL;

	info->name = "max32690-adc";
	info->dev = NULL;
	info->direction = 0;
	info->add_channels = iio_adc_add_channels;
	info->read_attr = iio_adc_read_attr;
	info->write_attr = iio_adc_write_attr;
	info->read_samples = iio_adc_read_samples;

	return 0;
}
