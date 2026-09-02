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
#include "iio_adc_hal.h"
#include "iio_val.h"
#include <iio/iio-backend.h>

static const char *const gain_values[] = {
	[IIO_ADC_GAIN_1_6] = "1/6",
	[IIO_ADC_GAIN_1_5] = "1/5",
	[IIO_ADC_GAIN_1_4] = "1/4",
	[IIO_ADC_GAIN_2_7] = "2/7",
	[IIO_ADC_GAIN_1_3] = "1/3",
	[IIO_ADC_GAIN_2_5] = "2/5",
	[IIO_ADC_GAIN_1_2] = "1/2",
	[IIO_ADC_GAIN_2_3] = "2/3",
	[IIO_ADC_GAIN_4_5] = "4/5",
	[IIO_ADC_GAIN_1]   = "1",
	[IIO_ADC_GAIN_2]   = "2",
	[IIO_ADC_GAIN_3]   = "3",
	[IIO_ADC_GAIN_4]   = "4",
	[IIO_ADC_GAIN_6]   = "6",
	[IIO_ADC_GAIN_8]   = "8",
	[IIO_ADC_GAIN_12]  = "12",
	[IIO_ADC_GAIN_16]  = "16",
	[IIO_ADC_GAIN_24]  = "24",
	[IIO_ADC_GAIN_32]  = "32",
	[IIO_ADC_GAIN_64]  = "64",
	[IIO_ADC_GAIN_128] = "128",
};
#define GAIN_DEFAULT_IDX IIO_ADC_GAIN_1

static const char *const reference_values[] = {
	[IIO_ADC_REF_VDD_1]     = "VDD",
	[IIO_ADC_REF_VDD_1_2]   = "VDD/2",
	[IIO_ADC_REF_VDD_1_3]   = "VDD/3",
	[IIO_ADC_REF_VDD_1_4]   = "VDD/4",
	[IIO_ADC_REF_INTERNAL]  = "Internal",
	[IIO_ADC_REF_EXTERNAL0] = "External0",
	[IIO_ADC_REF_EXTERNAL1] = "External1",
};
#define REFERENCE_DEFAULT_IDX IIO_ADC_REF_INTERNAL

struct adc_channel_state {
	int scale_val;
	int scale_val2;
	unsigned int gain;
	unsigned int reference;
	int differential;
};

static struct adc_channel_state chan_state[IIO_ADC_MAX_CHANNELS];

static enum iio_val_type iio_adc_get_fmt(const char *attr_name)
{
	if (strcmp(attr_name, "scale") == 0)
		return IIO_VAL_INT_PLUS_MICRO;

	if (strcmp(attr_name, "gain") == 0 ||
	    strcmp(attr_name, "reference") == 0)
		return IIO_VAL_CHAR;

	return IIO_VAL_INT;
}

static int adc_lookup_str(const char *const *table, size_t count,
			  const char *src, size_t len)
{
	size_t i, slen = len;

	while (slen > 0 && (src[slen - 1] == '\n' || src[slen - 1] == '\0'))
		slen--;

	for (i = 0; i < count; i++) {
		if (table[i] && strlen(table[i]) == slen &&
		    strncmp(src, table[i], slen) == 0)
			return (int)i;
	}

	return -EINVAL;
}

static int adc_channel_index(const char *id)
{
	unsigned int i, n = iio_adc_hal.num_channels;

	for (i = 0; i < n; i++) {
		const char *cid = iio_adc_hal.channels[i];

		if (cid && strcmp(cid, id) == 0)
			return (int)i;
	}

	return -EINVAL;
}

static void iio_adc_state_init(void)
{
	unsigned int i;

	for (i = 0; i < IIO_ADC_MAX_CHANNELS; i++) {
		chan_state[i].scale_val = 1;
		chan_state[i].scale_val2 = 0;
		chan_state[i].gain = GAIN_DEFAULT_IDX;
		chan_state[i].reference = REFERENCE_DEFAULT_IDX;
		chan_state[i].differential = 0;
	}
}

static struct iio_data_format adc_fmt = {
	.length = 16,
	.bits = 0,
	.is_signed = false,
};

static int iio_adc_read_samples(void *dev, void *data, size_t bytes)
{
	size_t num_samples = bytes / sizeof(uint16_t);
	uint16_t *buffer = (uint16_t *)data;
	int raw;

	for (size_t i = 0; i < num_samples; i++) {
		int ret = iio_adc_hal.read_raw(0, &raw);

		if (ret)
			return ret;
		buffer[i] = raw & ((1u << adc_fmt.bits) - 1);
	}

	return 0;
}

static int iio_adc_add_channels(void *dev, struct iio_device *iio_dev)
{
	struct iio_channel *ch;
	unsigned int i, n = iio_adc_hal.num_channels;

	for (i = 0; i < n; i++) {
		ch = iio_device_add_channel(iio_dev, (long)i,
					    iio_adc_hal.channels[i],
					    NULL, NULL,
					    false, true, &adc_fmt);
		if (!ch)
			return -ENOMEM;

		iio_channel_add_attr(ch, "raw", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "scale", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "gain", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "process", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "reference", IIO_ATTR_TYPE_CHANNEL, NULL);
		iio_channel_add_attr(ch, "differential", IIO_ATTR_TYPE_CHANNEL,
				     NULL);
	}

	iio_device_add_attr(iio_dev, "internal_ref_voltage",
			    IIO_ATTR_TYPE_DEVICE);

	return 0;
}

static int iio_adc_read_attr(void *dev,
			     const struct iio_device *iio_dev,
			     const struct iio_attr *attr,
			     char *dst, size_t len)
{
	const char *attr_name;
	const char *ch_id;
	int idx;
	int raw_value;
	int ret;
	int vals[2];

	attr_name = iio_attr_get_name(attr);
	if (!attr_name)
		return -EINVAL;

	if (attr->type == IIO_ATTR_TYPE_DEVICE) {
		if (strcmp(attr_name, "internal_ref_voltage") == 0) {
			vals[0] = iio_adc_hal.ref_voltage_mv;
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

	idx = adc_channel_index(ch_id);
	if (idx < 0)
		return idx;

	if (strcmp(attr_name, "scale") == 0) {
		vals[0] = chan_state[idx].scale_val;
		vals[1] = chan_state[idx].scale_val2;
		ret = iio_format_value(dst, len, IIO_VAL_INT_PLUS_MICRO,
				       2, vals);
		return (ret < 0) ? ret : ret + 1;
	}

	if (strcmp(attr_name, "raw") == 0) {
		ret = iio_adc_hal.read_raw((unsigned int)idx, &raw_value);
		if (ret)
			return ret;

		vals[0] = raw_value;
		ret = iio_format_value(dst, len, IIO_VAL_INT, 1, vals);
		return (ret < 0) ? ret : ret + 1;
	}

	if (strcmp(attr_name, "gain") == 0)
		return snprintf(dst, len, "%s",
				gain_values[chan_state[idx].gain]) + 1;

	if (strcmp(attr_name, "reference") == 0)
		return snprintf(dst, len, "%s",
				reference_values[chan_state[idx].reference]) + 1;

	if (strcmp(attr_name, "differential") == 0) {
		vals[0] = chan_state[idx].differential;
		ret = iio_format_value(dst, len, IIO_VAL_INT, 1, vals);
		return (ret < 0) ? ret : ret + 1;
	}

	if (strcmp(attr_name, "process") == 0) {
		int64_t scale_uv;

		ret = iio_adc_hal.read_raw((unsigned int)idx, &raw_value);
		if (ret)
			return ret;

		scale_uv = (int64_t)chan_state[idx].scale_val * 1000000 +
			   chan_state[idx].scale_val2;
		vals[0] = (int)((raw_value * scale_uv) / 1000000);
		ret = iio_format_value(dst, len, IIO_VAL_INT, 1, vals);
		return (ret < 0) ? ret : ret + 1;
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
	int idx;
	int fract_mult;
	int integer, fract;
	int str_idx;
	int ret;

	attr_name = iio_attr_get_name(attr);
	if (!attr_name)
		return -EINVAL;

	if (attr->type == IIO_ATTR_TYPE_DEVICE)
		return -EPERM;

	if (attr->type != IIO_ATTR_TYPE_CHANNEL || !attr->iio.chn)
		return -EINVAL;

	ch_id = iio_channel_get_id(attr->iio.chn);
	if (!ch_id)
		return -EINVAL;

	idx = adc_channel_index(ch_id);
	if (idx < 0)
		return idx;

	if (strcmp(attr_name, "scale") == 0) {
		fract_mult = iio_val_fract_mult(iio_adc_get_fmt(attr_name));
		if (fract_mult < 0)
			return fract_mult;

		ret = iio_str_to_fixpoint(src, fract_mult, &integer, &fract);
		if (ret)
			return ret;

		chan_state[idx].scale_val = integer;
		chan_state[idx].scale_val2 = fract;
		return len;
	}

	if (strcmp(attr_name, "gain") == 0) {
		str_idx = adc_lookup_str(gain_values,
					 NO_OS_ARRAY_SIZE(gain_values),
					 src, len);
		if (str_idx < 0)
			return str_idx;

		if (iio_adc_hal.set_gain) {
			ret = iio_adc_hal.set_gain((unsigned int)idx,
						   (unsigned int)str_idx);
			if (ret)
				return ret;
		}

		chan_state[idx].gain = (unsigned int)str_idx;
		return len;
	}

	if (strcmp(attr_name, "reference") == 0) {
		str_idx = adc_lookup_str(reference_values,
					 NO_OS_ARRAY_SIZE(reference_values),
					 src, len);
		if (str_idx < 0)
			return str_idx;

		if (iio_adc_hal.set_reference) {
			ret = iio_adc_hal.set_reference((unsigned int)idx,
							(unsigned int)str_idx);
			if (ret)
				return ret;
		}

		chan_state[idx].reference = (unsigned int)str_idx;
		return len;
	}

	if (strcmp(attr_name, "differential") == 0) {
		fract_mult = iio_val_fract_mult(iio_adc_get_fmt(attr_name));
		if (fract_mult < 0)
			return fract_mult;

		ret = iio_str_to_fixpoint(src, fract_mult, &integer, &fract);
		if (ret)
			return ret;

		if (integer != 0 && integer != 1)
			return -EINVAL;
		chan_state[idx].differential = integer;
		return len;
	}

	if (strcmp(attr_name, "raw") == 0 ||
	    strcmp(attr_name, "process") == 0)
		return -EPERM;

	return -EINVAL;
}

#define ADC_NUM_REGS 16
static uint32_t adc_regs[ADC_NUM_REGS];

static int iio_adc_reg_read(void *dev, uint32_t reg, uint32_t *val)
{
	if (reg >= ADC_NUM_REGS)
		return -EINVAL;

	*val = adc_regs[reg];

	return 0;
}

static int iio_adc_reg_write(void *dev, uint32_t reg, uint32_t val)
{
	if (reg >= ADC_NUM_REGS)
		return -EINVAL;

	adc_regs[reg] = val;

	return 0;
}

int iio_adc_init(void)
{
	int ret;

	ret = iio_adc_hal.init();
	if (ret)
		return ret;

	adc_fmt.bits = iio_adc_hal.resolution_bits;

	iio_adc_state_init();

	return 0;
}

int iio_adc_get_device_info(struct noos_iio_device_info *info)
{
	if (!info)
		return -EINVAL;

	info->name = "iio-adc";
	info->dev = NULL;
	info->direction = 0;
	info->add_channels = iio_adc_add_channels;
	info->read_attr = iio_adc_read_attr;
	info->write_attr = iio_adc_write_attr;
	info->read_samples = iio_adc_read_samples;
	info->write_samples = NULL;
	info->reg_read = iio_adc_reg_read;
	info->reg_write = iio_adc_reg_write;

	return 0;
}
