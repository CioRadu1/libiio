/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"

static const char *const gain_values[] = {
	"1/4", "2/7", "1/3", "2/5", "1/2", "2/3", "4/5", "1", "2", "3", "4",
	"6", "8", "12", "16", "24", "32", "64", "128",
};

static const char *const reference_values[] = {
	"VDD", "VDD/2", "VDD/3", "VDD/4", "Internal", "External0", "External1",
};

static bool in_table(const char *const *table, size_t count, const char *val)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (!strcmp(table[i], val))
			return true;
	}

	return false;
}

TEST_FUNCTION(attr_read_all)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	unsigned int i, nb_chn = iio_device_get_channels_count(dev);
	unsigned int nb_dev = iio_device_get_attrs_count(dev);
	char buf[64];
	ssize_t ret;

	for (i = 0; i < nb_dev; i++) {
		const struct iio_attr *attr = iio_device_get_attr(dev, i);

		ret = iio_attr_read_raw(attr, buf, sizeof(buf));
		TEST_ASSERT(ret > 0, iio_attr_get_name(attr));
		if (ret > 0)
			TEST_ASSERT(buf[ret - 1] == '\0',
				    "device attribute is NUL terminated");
	}

	for (i = 0; i < nb_chn; i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		unsigned int j, nb = iio_channel_get_attrs_count(chn);

		for (j = 0; j < nb; j++) {
			const struct iio_attr *attr =
				iio_channel_get_attr(chn, j);

			ret = iio_attr_read_raw(attr, buf, sizeof(buf));
			TEST_ASSERT(ret > 0, iio_attr_get_name(attr));
			if (ret > 0)
				TEST_ASSERT(buf[ret - 1] == '\0',
					    "channel attribute is NUL terminated");
		}
	}

	iio_context_destroy(ctx);
}

TEST_FUNCTION(attr_internal_ref_voltage)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	const struct iio_attr *attr =
		iio_device_find_attr(dev, "internal_ref_voltage");
	long long val = 0;

	if (!attr) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ(iio_attr_read_longlong(attr, &val), 0,
			      "internal_ref_voltage reads as an integer");
	TEST_ASSERT(val > 0, "internal_ref_voltage is positive");

#ifdef TEST_CTX_NOOS
	TEST_LONG_EQ(val, 2500, "adc_demo reference is 2500 mV");
#endif

	iio_context_destroy(ctx);
}

TEST_FUNCTION(attr_raw_in_range)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	const struct iio_attr *attr;
	long raw_max;
	long long val = -1;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	attr = iio_channel_find_attr(chn, "raw");
	raw_max = test_chan_raw_max(chn);

	TEST_ASSERT(raw_max > 0, "resolution yields a usable raw range");
	TEST_INT_EQ(iio_attr_read_longlong(attr, &val), 0,
			      "raw reads as an integer");
	TEST_ASSERT(val >= 0 && val <= raw_max,
		    "raw is within the channel resolution");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(attr_scale_is_double)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	const struct iio_attr *attr;
	double val = -1.0;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	attr = iio_channel_find_attr(chn, "scale");

	TEST_INT_EQ(iio_attr_read_double(attr, &val), 0,
			      "scale reads as a double");
	TEST_ASSERT(val > 0.0, "scale is positive");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(attr_enum_values)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char buf[32];
	long long diff = -1;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	if (iio_attr_read_raw(iio_channel_find_attr(chn, "gain"),
			      buf, sizeof(buf)) > 0)
		TEST_ASSERT(in_table(gain_values,
				     sizeof(gain_values) /
				     sizeof(gain_values[0]), buf),
			    "gain reads back a known value");

	if (iio_attr_read_raw(iio_channel_find_attr(chn, "reference"),
			      buf, sizeof(buf)) > 0)
		TEST_ASSERT(in_table(reference_values,
				     sizeof(reference_values) /
				     sizeof(reference_values[0]), buf),
			    "reference reads back a known value");

	TEST_INT_EQ(iio_attr_read_longlong(
				      iio_channel_find_attr(chn,
							    "differential"),
				      &diff), 0,
			      "differential reads as an integer");
	TEST_ASSERT(diff == 0 || diff == 1, "differential is 0 or 1");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(attr_short_buffer)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char buf[4];
	ssize_t ret;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	memset(buf, 'x', sizeof(buf));
	ret = iio_attr_read_raw(iio_channel_find_attr(chn, "reference"),
				buf, sizeof(buf));

	TEST_ASSERT(ret >= 0, "a short read does not fail");
	if (ret >= 0)
		TEST_ASSERT(memchr(buf, '\0', sizeof(buf)) != NULL,
			    "a short read stays NUL terminated");

	iio_context_destroy(ctx);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS attribute tests (%s) ===\n", test_ctx_label());

	RUN_TEST(attr_read_all);
	RUN_TEST(attr_internal_ref_voltage);
	RUN_TEST(attr_raw_in_range);
	RUN_TEST(attr_scale_is_double);
	RUN_TEST(attr_enum_values);
	RUN_TEST(attr_short_buffer);

	TEST_SUMMARY();

	return 0;
}
