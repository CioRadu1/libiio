/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"

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
		TEST_IN("device attr \"%s\", buffer %zu bytes",
			iio_attr_get_name(attr), sizeof(buf));
		TEST_OUT("ret = %zd, value = \"%s\"", ret, ret > 0 ? buf : "");
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
			TEST_IN("channel %u attr \"%s\", buffer %zu bytes",
				i, iio_attr_get_name(attr), sizeof(buf));
			TEST_OUT("ret = %zd, value = \"%s\"", ret,
				 ret > 0 ? buf : "");
			TEST_ASSERT(ret > 0, iio_attr_get_name(attr));
			if (ret > 0)
				TEST_ASSERT(buf[ret - 1] == '\0',
					    "channel attribute is NUL terminated");
		}
	}

	iio_context_destroy(ctx);
}

/* scale is one LSB, the same the scan format carries: raw * scale is mV. */
TEST_FUNCTION(attr_scale_matches_format)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	const struct iio_data_format *fmt;
	double val = -1.0;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	fmt = iio_channel_get_data_format(chn);

	TEST_IN("read_double(\"scale\"), against the format's scale");
	TEST_INT_EQ(iio_attr_read_double(iio_channel_find_attr(chn, "scale"),
					 &val), 0,
		    "scale reads as a double");
	TEST_OUT("scale = %.9f, format scale = %.9f", val,
		 fmt ? fmt->scale : -1.0);
	TEST_ASSERT(fmt && fmt->with_scale, "the format has a scale");
	if (fmt)
		TEST_ASSERT(val > fmt->scale * 0.999999 &&
			    val < fmt->scale * 1.000001,
			    "the scale attribute is the format's");

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

	TEST_IN("read_longlong(\"raw\"), %u-bit channel, allowed range 0..%ld",
		test_chan_bits(chn), raw_max);
	TEST_ASSERT(raw_max > 0, "resolution yields a usable raw range");
	TEST_INT_EQ(iio_attr_read_longlong(attr, &val), 0,
			      "raw reads as an integer");
	TEST_OUT("raw = %lld", val);
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

	TEST_IN("read_double(\"scale\")");
	TEST_INT_EQ(iio_attr_read_double(attr, &val), 0,
			      "scale reads as a double");
	TEST_OUT("scale = %f", val);
	TEST_ASSERT(val > 0.0, "scale is positive");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(attr_enum_values)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	const struct iio_attr *attr;
	char buf[32];

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	attr = iio_channel_find_attr(chn, "reference");
	if (!attr) {
		DEBUG_PRINT("  SKIP: the part has no reference to switch\n");
		iio_context_destroy(ctx);
		return;
	}

	if (iio_attr_read_raw(attr, buf, sizeof(buf)) > 0) {
		TEST_IN("reference must be one of %zu known values "
			"(\"%s\"..\"%s\")",
			sizeof(reference_values) / sizeof(reference_values[0]),
			reference_values[0],
			reference_values[sizeof(reference_values) /
					 sizeof(reference_values[0]) - 1]);
		TEST_OUT("reference = \"%s\"", buf);
		TEST_ASSERT(in_table(reference_values,
				     sizeof(reference_values) /
				     sizeof(reference_values[0]), buf),
			    "reference reads back a known value");
	}

	iio_context_destroy(ctx);
}

TEST_FUNCTION(attr_short_buffer)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	const struct iio_attr *attr;
	char guarded[8];
	char full[32];
	ssize_t full_ret, short_ret, again;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	attr = iio_channel_find_attr(chn, "scale");
	memset(guarded, 'x', sizeof(guarded));
	memset(full, 'x', sizeof(full));

	full_ret = iio_attr_read_raw(attr, full, sizeof(full));
	TEST_IN("read \"scale\" into %zu bytes, then into only 4 bytes",
		sizeof(full));
	TEST_OUT("full read ret = %zd, value = \"%s\"", full_ret,
		 full_ret > 0 ? full : "");

	short_ret = iio_attr_read_raw(attr, guarded, 4);
	TEST_OUT("short read ret = %zd, buffer = [%c%c%c%c], guard = [%c%c%c%c]",
		 short_ret, guarded[0], guarded[1], guarded[2], guarded[3],
		 guarded[4], guarded[5], guarded[6], guarded[7]);

	TEST_ASSERT(full_ret > 0, "a full-size read succeeds");
	if (full_ret > 0)
		TEST_LONG_EQ(full[full_ret - 1], 0,
			     "a full-size read is NUL terminated");

	TEST_ASSERT(memcmp(guarded + 4, "xxxx", 4) == 0,
		    "a short read does not write past the buffer");

	memset(full, 'x', sizeof(full));
	again = iio_attr_read_raw(attr, full, sizeof(full));
	TEST_IN("re-read \"scale\" into %zu bytes, expecting ret %zd",
		sizeof(full), full_ret);
	TEST_OUT("ret = %zd, value = \"%s\"", again, again > 0 ? full : "");
	TEST_LONG_EQ(again, full_ret,
		     "the attribute is still readable after a short read");

	iio_context_destroy(ctx);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS attribute tests (%s) ===\n", test_ctx_label());

	RUN_TEST(attr_read_all);
	RUN_TEST(attr_scale_matches_format);
	RUN_TEST(attr_raw_in_range);
	RUN_TEST(attr_scale_is_double);
	RUN_TEST(attr_enum_values);
	RUN_TEST(attr_short_buffer);

	TEST_SUMMARY();

	return 0;
}
