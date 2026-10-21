/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Host unit test of the table layer (backend_table.c): table-driven devices
 * built in a context of a backend of their own, checked for the ids,
 * attribute names and file names Linux gives the same channel specs, for
 * the standard channels of iio_accel.h, iio_temp.h and iio_adc.h, for the
 * values the backend serves itself (struct noos_iio_value), and for each
 * NOOS_IIO_VAL_* printed and parsed as Linux does. Needs no board.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <iio/iio.h>
#include <iio/iio-backend.h>
#include <iio_channels.h>
#include <iio_device.h>

#include "test_framework.h"
#include "iio_noos_driver_desc.h"

/* The trigger wait is the firmware's (iio_noos_driver_desc.c); no device here
 * is paced, so these never run. */
int noos_trigger_wait(const char *name)
{
	return -ENOSYS;
}

void noos_trigger_stop(const char *name)
{
}

#define BIT(i)		NOOS_IIO_BIT(i)
#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))

/* ---- the device under test: hand-written specs and the standard ones ---- */

/* As Linux's ADXL355 has it, and a 12-bit ADC. */
#define ACCEL_SCAN_TYPE	{ .sign = 's', .realbits = 20, .storagebits = 32, \
			  .shift = 4, .endianness = NOOS_IIO_BE }
#define ADC_SCAN_TYPE	{ .sign = 'u', .realbits = 12, .storagebits = 16, \
			  .endianness = NOOS_IIO_LE }

static const struct noos_iio_chan dut_channels[] = {
	/* by hand: one of each sharing */
	{
		.type = NOOS_IIO_CURRENT, .indexed = true, .channel = 0,
		.scan_index = -1,
		.info_mask_separate = BIT(RAW),
		.info_mask_shared_by_type = BIT(SCALE),
		.info_mask_shared_by_dir = BIT(INT_TIME),
		.info_mask_shared_by_dir_available = BIT(INT_TIME),
	},
	{
		.type = NOOS_IIO_CURRENT, .indexed = true, .channel = 1,
		.scan_index = -1,
		.info_mask_separate = BIT(RAW),
		.info_mask_shared_by_type = BIT(SCALE),
		.info_mask_shared_by_dir = BIT(INT_TIME),
		.info_mask_shared_by_dir_available = BIT(INT_TIME),
	},
	{
		.type = NOOS_IIO_CURRENT, .indexed = true, .differential = true,
		.channel = 2, .channel2 = 3, .scan_index = -1,
		.info_mask_separate = BIT(RAW),
		.info_mask_shared_by_type = BIT(SCALE),
		.priv = 1,	/* a negative scale */
	},
	{
		.type = NOOS_IIO_ALTVOLTAGE, .indexed = true, .output = true,
		.scan_index = -1,
		.info_mask_separate = BIT(FREQUENCY) | BIT(PHASE) |
				      BIT(HARDWAREGAIN) | BIT(ENABLE),
	},
	{
		.type = NOOS_IIO_POWER, .id = "power9", .scan_index = -1,
		.info_mask_separate = BIT(RAW) | BIT(OFFSET) | BIT(TROUGH),
		.info_mask_shared_by_type = BIT(PEAK),
	},
	/* the standard ones */
	NOOS_IIO_ACCEL_XYZ(BIT(RAW) | BIT(CALIBBIAS) | BIT(SCALE) |
			   BIT(SAMP_FREQ),
			   BIT(SAMP_FREQ), ACCEL_SCAN_TYPE),
	NOOS_IIO_TEMP_CHANNEL(-1, BIT(RAW) | BIT(SCALE) | BIT(OFFSET), 0,
			      NOOS_IIO_NO_SCAN_TYPE),
	NOOS_IIO_VOLTAGE_CHANNEL(0, 3, BIT(RAW) | BIT(SCALE) | BIT(SAMP_FREQ) |
				 BIT(OVERSAMPLING_RATIO),
				 BIT(SAMP_FREQ), ADC_SCAN_TYPE),
	NOOS_IIO_VOLTAGE_DIFF_CHANNEL(1, 2, 4,
				      BIT(RAW) | BIT(SCALE) | BIT(SAMP_FREQ) |
				      BIT(OVERSAMPLING_RATIO),
				      BIT(SAMP_FREQ),
				      { .sign = 's', .realbits = 12,
					.storagebits = 16,
					.endianness = NOOS_IIO_CPU }),
};

static const int accel_odrs[] = { 125, 0, 250, 0, 500, 0 };
static const int adc_rates[] = { 1000, 2000, 4000 };
static const int int_times[] = { 0, 100000, 0, 200000 };
static int accel_odr = 1, adc_rate = 1;

static int get_accel_odr(void *dev)
{
	return accel_odr;
}

static int set_accel_odr(void *dev, unsigned int idx)
{
	accel_odr = (int)idx;

	return 0;
}

static int get_adc_rate(void *dev)
{
	return adc_rate;
}

static int set_adc_rate(void *dev, unsigned int idx)
{
	adc_rate = (int)idx;

	return 0;
}

static const int *int_times_now(void *dev, unsigned int *nb)
{
	*nb = 2;

	return int_times;
}

static const struct noos_iio_value dut_values[] = {
	NOOS_IIO_CONST(ACCEL, SCALE, INT_PLUS_NANO, 0, 38245),
	NOOS_IIO_LIST(ACCEL, SAMP_FREQ, INT_PLUS_MICRO, accel_odrs,
		      get_accel_odr, set_accel_odr),
	NOOS_IIO_CONST(TEMP, SCALE, INT_PLUS_MICRO, -110, 497238),
	NOOS_IIO_CONST(TEMP, OFFSET, INT_PLUS_MICRO, -2111, 250000),
	NOOS_IIO_LIST(VOLTAGE, SAMP_FREQ, INT, adc_rates, get_adc_rate,
		      set_adc_rate),
	NOOS_IIO_CONST(VOLTAGE, OVERSAMPLING_RATIO, INT, 4),
	NOOS_IIO_LIST_FN(CURRENT, INT_TIME, INT_PLUS_MICRO, int_times_now,
			 NULL, NULL),
	NOOS_IIO_CONST(POWER, TROUGH, FRACTIONAL_LOG2, 1, 1),
};

static int written_info = -1, written_val, written_val2;
static const struct noos_iio_chan *written_ch;

static int dut_read_raw(void *dev, const struct noos_iio_chan *ch,
			enum noos_iio_info info, int *val, int *val2)
{
	switch (info) {
	case NOOS_IIO_INFO_RAW:
		*val = ch->type == NOOS_IIO_ACCEL ? 1000 + (int)ch->priv : -42;
		return NOOS_IIO_VAL_INT;
	case NOOS_IIO_INFO_CALIBBIAS:
		*val = 7;
		return NOOS_IIO_VAL_INT;
	case NOOS_IIO_INFO_SCALE:
		if (ch->type == NOOS_IIO_VOLTAGE) {
			*val = 2500;
			*val2 = 12;
			return NOOS_IIO_VAL_FRACTIONAL_LOG2;
		}
		*val = ch->priv ? -2500 : 2500;
		*val2 = 16;
		return NOOS_IIO_VAL_FRACTIONAL_LOG2;
	case NOOS_IIO_INFO_FREQUENCY:
		/* 6000000000 Hz */
		*val = (int)UINT32_C(0x65a0bc00);
		*val2 = 1;
		return NOOS_IIO_VAL_INT_64;
	case NOOS_IIO_INFO_PHASE:
		*val = -7;
		*val2 = 2;
		return NOOS_IIO_VAL_FRACTIONAL;
	case NOOS_IIO_INFO_HARDWAREGAIN:
		*val = -3;
		*val2 = -500000;
		return NOOS_IIO_VAL_INT_PLUS_MICRO_DB;
	case NOOS_IIO_INFO_ENABLE:
		*val = 1;
		return NOOS_IIO_VAL_INT;
	case NOOS_IIO_INFO_OFFSET:
		*val = 0;
		*val2 = -250000;
		return NOOS_IIO_VAL_INT_PLUS_MICRO;
	case NOOS_IIO_INFO_PEAK:
		*val = 1;
		*val2 = 5;
		return NOOS_IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int dut_write_raw(void *dev, const struct noos_iio_chan *ch,
			 enum noos_iio_info info, int val, int val2)
{
	written_ch = ch;
	written_info = (int)info;
	written_val = val;
	written_val2 = val2;

	return 0;
}

static int dut_write_raw_get_fmt(void *dev, const struct noos_iio_chan *ch,
				 enum noos_iio_info info)
{
	switch (info) {
	case NOOS_IIO_INFO_FREQUENCY:
		return NOOS_IIO_VAL_INT_64;
	case NOOS_IIO_INFO_HARDWAREGAIN:
		return NOOS_IIO_VAL_INT_PLUS_MICRO_DB;
	case NOOS_IIO_INFO_CALIBBIAS:
		return NOOS_IIO_VAL_INT;
	default:
		return NOOS_IIO_VAL_INT_PLUS_MICRO;
	}
}

static int dut_read_avail(void *dev, const struct noos_iio_chan *ch,
			  enum noos_iio_info info, const int **vals, int *type,
			  int *length)
{
	*vals = adc_rates;
	*type = NOOS_IIO_VAL_INT;
	*length = 3;

	return NOOS_IIO_AVAIL_LIST;
}

static int dut_read_scan(void *dev, uint32_t enabled, int64_t *vals)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dut_channels); i++)
		if (enabled & (UINT32_C(1) << i))
			vals[i] = (int64_t)i;

	return 0;
}

/* A second device, read through read_raw_multi, with a CHAR attribute. */
static const struct noos_iio_chan multi_channels[] = {
	{
		.type = NOOS_IIO_ANGL_VEL, .indexed = true, .channel = 0,
		.modified = true, .channel2 = NOOS_IIO_MOD_Z, .scan_index = -1,
		.info_mask_separate = BIT(RAW),
	},
	{
		.type = NOOS_IIO_TEMP, .scan_index = -1,
		.info_mask_separate = BIT(THERMOCOUPLE_TYPE),
	},
};

static int multi_read_raw_multi(void *dev, const struct noos_iio_chan *ch,
				int max_len, int *vals, int *val_len,
				enum noos_iio_info info)
{
	if (info == NOOS_IIO_INFO_THERMOCOUPLE_TYPE) {
		vals[0] = 'K';
		*val_len = 1;
		return NOOS_IIO_VAL_CHAR;
	}

	vals[0] = 1;
	vals[1] = -2;
	vals[2] = 3;
	*val_len = 3;

	return NOOS_IIO_VAL_INT_MULTIPLE;
}

static int multi_write_raw_get_fmt(void *dev, const struct noos_iio_chan *ch,
				   enum noos_iio_info info)
{
	return NOOS_IIO_VAL_CHAR;
}

static struct noos_iio_device_info devices[2];

/* ---- a backend that serves them ---- */

static ssize_t test_read_attr(const struct iio_attr *attr, char *dst,
			      size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);
	const struct noos_iio_device_info *info =
		(const struct noos_iio_device_info *)iio_device_get_pdata(dev);

	return info->read_attr(info->dev, dev, attr, dst, len);
}

static ssize_t test_write_attr(const struct iio_attr *attr, const char *src,
			       size_t len)
{
	const struct iio_device *dev = iio_attr_get_device(attr);
	const struct noos_iio_device_info *info =
		(const struct noos_iio_device_info *)iio_device_get_pdata(dev);

	return info->write_attr(info->dev, dev, attr, src, len);
}

static const struct iio_backend_ops test_ops = {
	.read_attr = test_read_attr,
	.write_attr = test_write_attr,
};

static const struct iio_backend test_backend = {
	.name = "table-test",
	.api_version = IIO_BACKEND_API_V1,
	.ops = &test_ops,
};

static struct iio_device *iio_dev, *multi_dev;

static struct iio_device *add_device(struct iio_context *ctx, const char *id,
				     struct noos_iio_device_info *info)
{
	struct iio_device *dev;

	dev = iio_context_add_device(ctx, id, info->name, NULL);
	if (!dev)
		return NULL;

	iio_device_set_pdata(dev, (struct iio_device_pdata *)info);

	return info->add_channels(NULL, dev) ? NULL : dev;
}

static int build_devices(void)
{
	const struct iio_context_params params = { 0 };
	struct iio_context *ctx;
	int ret;

	devices[0] = (struct noos_iio_device_info) {
		.name = "dut",
		.channels = dut_channels,
		.num_channels = ARRAY_SIZE(dut_channels),
		.values = dut_values,
		.nb_values = ARRAY_SIZE(dut_values),
		.read_raw = dut_read_raw,
		.write_raw = dut_write_raw,
		.write_raw_get_fmt = dut_write_raw_get_fmt,
		.read_scan = dut_read_scan,
	};
	devices[1] = (struct noos_iio_device_info) {
		.name = "multi",
		.channels = multi_channels,
		.num_channels = ARRAY_SIZE(multi_channels),
		.read_raw_multi = multi_read_raw_multi,
		.write_raw = dut_write_raw,
		.write_raw_get_fmt = multi_write_raw_get_fmt,
	};

	ret = noos_iio_table_install(&devices[0]);
	if (ret)
		return ret;

	ret = noos_iio_table_install(&devices[1]);
	if (ret)
		return ret;

	ctx = iio_context_create_from_backend(&params, &test_backend, "test",
					      1, 0, 0, "");
	if (iio_err(ctx))
		return iio_err(ctx);

	iio_dev = add_device(ctx, "iio:device0", &devices[0]);
	multi_dev = add_device(ctx, "iio:device1", &devices[1]);

	return iio_dev && multi_dev ? 0 : -EINVAL;
}

/* ---- helpers ---- */

static const struct iio_attr *chn_attr_of(const struct iio_device *dev,
					  const char *chn_id, bool output,
					  const char *name)
{
	const struct iio_channel *chn;

	chn = iio_device_find_channel(dev, chn_id, output);

	return chn ? iio_channel_find_attr(chn, name) : NULL;
}

static const struct iio_attr *chn_attr(const char *chn_id, bool output,
				       const char *name)
{
	return chn_attr_of(iio_dev, chn_id, output, name);
}

static const char *read_str(const struct iio_attr *attr)
{
	static char buf[128];
	ssize_t ret;

	if (!attr)
		return "(no attribute)";

	ret = iio_attr_read_raw(attr, buf, sizeof(buf));
	if (ret < 0)
		snprintf(buf, sizeof(buf), "(error %zd)", ret);

	return buf;
}

static const char *filename(const struct iio_attr *attr)
{
	return attr ? iio_attr_get_filename(attr) : "(no attribute)";
}

static ssize_t write_str(const struct iio_attr *attr, const char *src)
{
	return attr ? iio_attr_write_raw(attr, src, strlen(src) + 1) : -ENOENT;
}

/* ---- tests ---- */

TEST_FUNCTION(table_channel_ids)
{
	static const struct {
		const char *id;
		bool output;
	} ids[] = {
		{ "current0", false }, { "current1", false },
		{ "current2-current3", false }, { "altvoltage0", true },
		{ "power9", false }, { "accel_x", false }, { "accel_y", false },
		{ "accel_z", false }, { "temp", false }, { "voltage0", false },
		{ "voltage1-voltage2", false },
	};
	unsigned int i, nb = iio_device_get_channels_count(iio_dev);

	TEST_ASSERT_EQ(nb, ARRAY_SIZE(ids), "every channel");

	for (i = 0; i < ARRAY_SIZE(ids); i++)
		TEST_ASSERT_PTR_NOT_NULL(iio_device_find_channel(iio_dev,
								 ids[i].id,
								 ids[i].output),
					 ids[i].id);

	TEST_ASSERT_PTR_NOT_NULL(iio_device_find_channel(multi_dev,
							 "anglvel0_z", false),
				 "indexed and modified");
}

TEST_FUNCTION(table_linux_file_names)
{
	TEST_ASSERT_STR_EQ(filename(chn_attr("current0", false, "raw")),
			   "in_current0_raw", "separate");
	TEST_ASSERT_STR_EQ(filename(chn_attr("current0", false, "scale")),
			   "in_current_scale", "shared by type");
	TEST_ASSERT_STR_EQ(filename(chn_attr("current2-current3", false,
					     "raw")),
			   "in_current2-current3_raw", "differential, separate");
	TEST_ASSERT_STR_EQ(filename(chn_attr("current2-current3", false,
					     "scale")),
			   "in_current-current_scale", "differential, by type");
	TEST_ASSERT_STR_EQ(filename(chn_attr("altvoltage0", true, "en")),
			   "out_altvoltage0_en", "output");
	TEST_ASSERT_STR_EQ(filename(chn_attr_of(multi_dev, "anglvel0_z", false,
						"raw")),
			   "in_anglvel0_z_raw", "indexed and modified");
	TEST_ASSERT_STR_EQ(filename(chn_attr("power9", false, "offset")),
			   "in_power9_offset", "own id, separate");
	TEST_ASSERT_STR_EQ(filename(chn_attr("power9", false, "peak_raw")),
			   "peak_raw", "own id, by type: no type to name");
}

TEST_FUNCTION(table_standard_accel)
{
	TEST_ASSERT_STR_EQ(filename(chn_attr("accel_x", false, "raw")),
			   "in_accel_x_raw", "raw is the axis'");
	TEST_ASSERT_STR_EQ(filename(chn_attr("accel_y", false, "calibbias")),
			   "in_accel_y_calibbias", "calibbias is the axis'");
	TEST_ASSERT_STR_EQ(filename(chn_attr("accel_z", false, "scale")),
			   "in_accel_scale", "scale is shared by type");
	TEST_ASSERT_STR_EQ(filename(chn_attr("accel_z", false,
					     "sampling_frequency_available")),
			   "in_accel_sampling_frequency_available",
			   "its list too");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("accel_y", false, "raw")),
			   "1001", "priv is the axis");
}

TEST_FUNCTION(table_standard_temp)
{
	TEST_ASSERT_STR_EQ(filename(chn_attr("temp", false, "offset")),
			   "in_temp_offset", "all its own");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("temp", false, "scale")),
			   "-110.497238", "a constant from the values");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("temp", false, "offset")),
			   "-2111.250000", "another");
	TEST_ASSERT_PTR_NULL(chn_attr("temp", false, "scale_available"),
			     "no list asked for");
}

TEST_FUNCTION(table_standard_adc)
{
	TEST_ASSERT_STR_EQ(filename(chn_attr("voltage0", false, "scale")),
			   "in_voltage0_scale", "an input's scale is its own");
	TEST_ASSERT_STR_EQ(filename(chn_attr("voltage1-voltage2", false,
					     "raw")),
			   "in_voltage1-voltage2_raw", "differential input");
	TEST_ASSERT_PTR_NULL(chn_attr("voltage0", false, "sampling_frequency"),
			     "the rate is not the input's");
	TEST_ASSERT_STR_EQ(read_str(iio_device_find_attr(iio_dev,
							 "sampling_frequency")),
			   "2000", "but the device's");
	TEST_ASSERT_STR_EQ(read_str(iio_device_find_attr(iio_dev,
							 "sampling_frequency_available")),
			   "1000 2000 4000", "with its list");
	TEST_ASSERT_STR_EQ(read_str(iio_device_find_attr(iio_dev,
							 "oversampling_ratio")),
			   "4", "oversampling_ratio too");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("voltage0", false, "scale")),
			   "0.610351562", "FRACTIONAL_LOG2 from read_raw");
}

TEST_FUNCTION(table_shared_device_attrs)
{
	unsigned int nb = iio_device_get_attrs_count(iio_dev);
	ssize_t ret;

	/* in_integration_time(_available), sampling_frequency(_available),
	 * oversampling_ratio */
	TEST_ASSERT_EQ(nb, 5, "each shared one added once");
	TEST_ASSERT_STR_EQ(read_str(iio_device_find_attr(iio_dev,
							 "in_integration_time")),
			   "0.100000", "shared by direction, from a list");
	TEST_ASSERT_STR_EQ(read_str(iio_device_find_attr(iio_dev,
							 "in_integration_time_available")),
			   "0.100000 0.200000", "a list that changes");
	ret = write_str(iio_device_find_attr(iio_dev, "in_integration_time"),
			"0.2");
	TEST_ASSERT_EQ(ret, -EPERM, "no set: read-only");
}

TEST_FUNCTION(table_values)
{
	const struct iio_attr *attr;
	ssize_t ret;

	TEST_ASSERT_STR_EQ(read_str(chn_attr("accel_x", false, "scale")),
			   "0.000038245", "a constant");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("accel_x", false,
					     "sampling_frequency")),
			   "250.000000", "the row in use");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("accel_x", false,
					     "sampling_frequency_available")),
			   "125.000000 250.000000 500.000000", "the table");

	attr = chn_attr("accel_y", false, "sampling_frequency");
	ret = write_str(attr, "500");
	TEST_ASSERT_EQ(ret, 4, "a row of the table");
	TEST_ASSERT_EQ(accel_odr, 2, "its index set");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("accel_z", false,
					     "sampling_frequency")),
			   "500.000000", "seen through another axis");

	ret = write_str(attr, "300");
	TEST_ASSERT_EQ(ret, -EINVAL, "not a row");
	TEST_ASSERT_EQ(accel_odr, 2, "unchanged");

	ret = write_str(chn_attr("accel_x", false, "scale"), "1");
	TEST_ASSERT_EQ(ret, -EPERM, "a constant is read-only");

	TEST_ASSERT_STR_EQ(read_str(chn_attr("power9", false, "trough_raw")),
			   "0.500000000", "a FRACTIONAL_LOG2 constant");
	ret = write_str(chn_attr("power9", false, "trough_raw"), "1");
	TEST_ASSERT_EQ(ret, -EPERM,
		       "read-only too, though no write parses as its type");

	written_info = -1;
	ret = write_str(iio_device_find_attr(iio_dev, "sampling_frequency"),
			"4000");
	TEST_ASSERT_EQ(ret, 5, "a device attribute from the values");
	TEST_ASSERT(adc_rate == 2 && written_info == -1,
		    "served by the values, not write_raw");
}

TEST_FUNCTION(table_value_types)
{
	TEST_ASSERT_STR_EQ(read_str(chn_attr("current0", false, "raw")),
			   "-42", "INT");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("current0", false, "scale")),
			   "0.038146972", "FRACTIONAL_LOG2");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("current2-current3", false,
					     "scale")),
			   "-0.038146972", "FRACTIONAL_LOG2, negative");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("altvoltage0", true, "phase")),
			   "-3.500000000", "FRACTIONAL, negative");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("altvoltage0", true,
					     "frequency")),
			   "6000000000", "INT_64");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("altvoltage0", true,
					     "hardwaregain")),
			   "-3.500000 dB", "INT_PLUS_MICRO_DB");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("power9", false, "offset")),
			   "-0.250000", "INT_PLUS_MICRO, negative fraction");
	TEST_ASSERT_STR_EQ(read_str(chn_attr("power9", false, "peak_raw")),
			   "1.000000005", "INT_PLUS_NANO");
	TEST_ASSERT_STR_EQ(read_str(chn_attr_of(multi_dev, "anglvel0_z", false,
						"raw")),
			   "1 -2 3 ", "INT_MULTIPLE, from read_raw_multi");
	TEST_ASSERT_STR_EQ(read_str(chn_attr_of(multi_dev, "temp", false,
						"thermocouple_type")),
			   "K", "CHAR");
}

TEST_FUNCTION(table_value_writes)
{
	const struct iio_attr *attr;
	ssize_t ret;

	attr = chn_attr("altvoltage0", true, "frequency");
	ret = write_str(attr, "6000000001");
	TEST_ASSERT_EQ(ret, 11, "INT_64 write");
	TEST_ASSERT(written_val == (int)UINT32_C(0x65a0bc01) &&
		    written_val2 == 1, "INT_64 as low and high halves");

	attr = chn_attr("altvoltage0", true, "hardwaregain");
	ret = write_str(attr, "-3.25 dB");
	TEST_ASSERT_EQ(ret, 9, "dB write");
	/* As Linux parses it: the sign is on the integer part when it has one. */
	TEST_ASSERT(written_val == -3 && written_val2 == 250000,
		    "dB parsed as micro");
	ret = write_str(attr, "6dB");
	TEST_ASSERT_EQ(ret, 4, "dB write without the space");
	TEST_ASSERT(written_val == 6 && written_val2 == 0, "6 dB");

	attr = chn_attr("accel_x", false, "calibbias");
	ret = write_str(attr, "-12");
	TEST_ASSERT_EQ(ret, 4, "calibbias goes to write_raw");
	TEST_ASSERT(written_ch == &dut_channels[5] && written_val == -12,
		    "on its axis");
	ret = write_str(attr, "1.5");
	TEST_ASSERT_EQ(ret, -EINVAL, "an INT is no fraction");

	attr = chn_attr_of(multi_dev, "temp", false, "thermocouple_type");
	ret = write_str(attr, "J");
	TEST_ASSERT_EQ(ret, 2, "CHAR write");
	TEST_ASSERT(written_val == 'J', "CHAR value");
}

TEST_FUNCTION(table_scan)
{
	unsigned int i, nb = iio_device_get_channels_count(iio_dev);
	unsigned int scans = 0;

	for (i = 0; i < nb; i++)
		scans += iio_channel_is_scan_element(
				iio_device_get_channel(iio_dev, i));

	TEST_ASSERT_EQ(scans, 5, "three axes and two inputs");
	TEST_ASSERT(devices[0].read_samples != NULL,
		    "read_samples from read_scan");
	TEST_ASSERT(devices[1].read_samples == NULL,
		    "none without read_scan");
}

/* A channel's format, as a client sees it: "le:s20/32>>4". */
static const char *format_of(const char *chn_id)
{
	const struct iio_channel *chn = iio_device_find_channel(iio_dev, chn_id,
								false);
	const struct iio_data_format *fmt;
	static char buf[32];

	if (!chn)
		return "(no channel)";

	fmt = iio_channel_get_data_format(chn);
	snprintf(buf, sizeof(buf), "%ce:%c%u/%u>>%u", fmt->is_be ? 'b' : 'l',
		 fmt->is_signed ? 's' : 'u', fmt->bits, fmt->length, fmt->shift);

	return buf;
}

TEST_FUNCTION(table_scan_types)
{
	const char *cpu = __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ ?
			  "be:s12/16>>0" : "le:s12/16>>0";

	TEST_ASSERT_STR_EQ(format_of("accel_x"), "be:s20/32>>4",
			   "Linux's ADXL355 scan_type");
	TEST_ASSERT_STR_EQ(format_of("voltage0"), "le:u12/16>>0",
			   "unsigned, little endian");
	TEST_ASSERT_STR_EQ(format_of("voltage1-voltage2"), cpu,
			   "NOOS_IIO_CPU is the host's order");
}

/* noos_iio_table_install() on one channel spec, with @values if given. */
static int install(const struct noos_iio_chan *ch, bool avail,
		   const struct noos_iio_value *values, unsigned int nb_values,
		   bool raw)
{
	struct noos_iio_device_info info = {
		.channels = ch,
		.num_channels = 1,
		.values = values,
		.nb_values = nb_values,
		.read_raw = raw ? dut_read_raw : NULL,
		.read_avail = avail ? dut_read_avail : NULL,
	};

	return noos_iio_table_install(&info);
}

TEST_FUNCTION(table_rejects_bad_specs)
{
	const struct noos_iio_chan twice = {
		.type = NOOS_IIO_VOLTAGE, .scan_index = -1,
		.info_mask_separate = BIT(SCALE),
		.info_mask_shared_by_all = BIT(SCALE),
	};
	const struct noos_iio_chan stray_avail = {
		.type = NOOS_IIO_VOLTAGE, .scan_index = -1,
		.info_mask_separate = BIT(RAW),
		.info_mask_separate_available = BIT(SCALE),
	};
	const struct noos_iio_chan past_count = {
		.type = NOOS_IIO_VOLTAGE, .scan_index = -1,
		.info_mask_separate = NOOS_IIO_INFO_BIT(NOOS_IIO_INFO_COUNT),
	};
	const struct noos_iio_chan diff_unindexed = {
		.type = NOOS_IIO_VOLTAGE, .differential = true, .scan_index = -1,
	};
	const struct noos_iio_chan no_modifier = {
		.type = NOOS_IIO_ACCEL, .modified = true, .scan_index = -1,
	};
	const struct noos_iio_chan bad_type = {
		.type = NOOS_IIO_CHAN_TYPE_COUNT, .scan_index = -1,
	};
	const struct noos_iio_chan listed = {
		.type = NOOS_IIO_ACCEL, .modified = true, .scan_index = -1,
		.channel2 = NOOS_IIO_MOD_X,
		.info_mask_shared_by_type = BIT(SAMP_FREQ),
		.info_mask_shared_by_type_available = BIT(SAMP_FREQ),
	};
	const struct noos_iio_value odr[] = {
		NOOS_IIO_LIST(ACCEL, SAMP_FREQ, INT_PLUS_MICRO, accel_odrs,
			      get_accel_odr, set_accel_odr),
	};
	const struct noos_iio_value twice_odr[] = {
		NOOS_IIO_LIST(ACCEL, SAMP_FREQ, INT_PLUS_MICRO, accel_odrs,
			      get_accel_odr, set_accel_odr),
		NOOS_IIO_CONST(ACCEL, SAMP_FREQ, INT, 1),
	};
	const struct noos_iio_value stray[] = {
		NOOS_IIO_LIST(ACCEL, SAMP_FREQ, INT_PLUS_MICRO, accel_odrs,
			      get_accel_odr, set_accel_odr),
		NOOS_IIO_CONST(TEMP, SCALE, INT, 1),
	};
	const struct noos_iio_chan no_sign = {
		.type = NOOS_IIO_VOLTAGE, .scan_index = 0,
		.scan_type = { .realbits = 12, .storagebits = 16 },
	};
	const struct noos_iio_chan odd_storage = {
		.type = NOOS_IIO_VOLTAGE, .scan_index = 0,
		.scan_type = { .sign = 'u', .realbits = 12, .storagebits = 12 },
	};
	const struct noos_iio_chan too_wide = {
		.type = NOOS_IIO_VOLTAGE, .scan_index = 0,
		.scan_type = { .sign = 's', .realbits = 20, .storagebits = 32,
			       .shift = 13 },
	};
	const struct noos_iio_chan scan_ok = {
		.type = NOOS_IIO_VOLTAGE, .scan_index = 0,
		.scan_type = { .sign = 's', .realbits = 20, .storagebits = 32,
			       .shift = 12 },
	};
	const struct noos_iio_value fractional_set[] = {
		NOOS_IIO_LIST(ACCEL, SAMP_FREQ, FRACTIONAL, accel_odrs,
			      get_accel_odr, set_accel_odr),
	};

	TEST_ASSERT_EQ(install(&twice, false, NULL, 0, true), -EINVAL,
		       "in two masks");
	TEST_ASSERT_EQ(install(&stray_avail, true, NULL, 0, true), -EINVAL,
		       "_available without the attribute");
	TEST_ASSERT_EQ(install(&past_count, false, NULL, 0, true), -EINVAL,
		       "no such attribute");
	TEST_ASSERT_EQ(install(&diff_unindexed, false, NULL, 0, true), -EINVAL,
		       "differential needs indexed");
	TEST_ASSERT_EQ(install(&no_modifier, false, NULL, 0, true), -EINVAL,
		       "modified needs a modifier");
	TEST_ASSERT_EQ(install(&bad_type, false, NULL, 0, true), -EINVAL,
		       "no such type");
	TEST_ASSERT_EQ(install(&listed, false, NULL, 0, true), -EINVAL,
		       "a list nothing serves");
	TEST_ASSERT_EQ(install(&listed, true, NULL, 0, true), 0,
		       "a list read_avail serves");
	TEST_ASSERT_EQ(install(&listed, false, NULL, 0, false), -EINVAL,
		       "an attribute nothing serves");
	TEST_ASSERT_EQ(install(&listed, false, odr, 1, false), 0,
		       "served by the values alone");
	TEST_ASSERT_EQ(install(&listed, false, twice_odr, 2, false), -EINVAL,
		       "two values for one attribute");
	TEST_ASSERT_EQ(install(&listed, false, stray, 2, false), -EINVAL,
		       "a value no channel has");
	TEST_ASSERT_EQ(install(&listed, false, fractional_set, 1, false),
		       -EINVAL, "a writable value a write cannot match");
	TEST_ASSERT_EQ(install(&no_sign, false, NULL, 0, true), -EINVAL,
		       "a scan element with no sign");
	TEST_ASSERT_EQ(install(&odd_storage, false, NULL, 0, true), -EINVAL,
		       "storage that is not whole bytes");
	TEST_ASSERT_EQ(install(&too_wide, false, NULL, 0, true), -EINVAL,
		       "data and shift wider than the storage");
	TEST_ASSERT_EQ(install(&scan_ok, false, NULL, 0, true), 0,
		       "data and shift that just fit");
}

int main(void)
{
	int ret;

	DEBUG_PRINT("=== no-OS table layer tests (host) ===\n");

	ret = build_devices();
	if (ret) {
		printf("building the devices failed: %d\n", ret);
		return 1;
	}

	RUN_TEST(table_channel_ids);
	RUN_TEST(table_linux_file_names);
	RUN_TEST(table_standard_accel);
	RUN_TEST(table_standard_temp);
	RUN_TEST(table_standard_adc);
	RUN_TEST(table_shared_device_attrs);
	RUN_TEST(table_values);
	RUN_TEST(table_value_types);
	RUN_TEST(table_value_writes);
	RUN_TEST(table_scan);
	RUN_TEST(table_scan_types);
	RUN_TEST(table_rejects_bad_specs);

	TEST_SUMMARY();

	return 0;
}
