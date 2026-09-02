/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"

static const char *const expected_chan_attrs[] = {
	"raw", "scale", "gain", "process", "reference", "differential",
};

TEST_FUNCTION(channel_identity)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	unsigned int i, nb = iio_device_get_channels_count(dev);

	for (i = 0; i < nb; i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		char expected[32];
		const char *id;

		TEST_ASSERT_PTR_NOT_NULL(chn, "channel is reachable");
		if (!chn)
			continue;

		snprintf(expected, sizeof(expected), "voltage%u", i);
		id = iio_channel_get_id(chn);

		TEST_ASSERT_PTR_NOT_NULL(id, "channel has an id");
		if (id)
			TEST_STR_EQ(id, expected, "channel id");

		TEST_ASSERT(!iio_channel_is_output(chn),
			    "channel is an input");
		TEST_ASSERT(iio_channel_is_scan_element(chn),
			    "channel is a scan element");
		TEST_LONG_EQ(iio_channel_get_type(chn), IIO_VOLTAGE,
			       "channel type is IIO_VOLTAGE");
		TEST_ASSERT(iio_channel_get_device(chn) == dev,
			    "channel points back at its device");
	}

	iio_context_destroy(ctx);
}

TEST_FUNCTION(channel_lookup)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);

	TEST_ASSERT(iio_device_find_channel(dev, "voltage0", false) ==
		    iio_device_get_channel(dev, 0),
		    "find_channel locates voltage0 as an input");
	TEST_ASSERT_PTR_NULL(iio_device_find_channel(dev, "voltage0", true),
			     "voltage0 is not an output channel");
	TEST_ASSERT_PTR_NULL(iio_device_find_channel(dev, "voltage99", false),
			     "find_channel rejects an unknown id");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(channel_attrs)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	size_t i;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_LONG_EQ(iio_channel_get_attrs_count(chn),
		       sizeof(expected_chan_attrs) /
		       sizeof(expected_chan_attrs[0]),
		       "channel exposes six attributes");

	for (i = 0; i < sizeof(expected_chan_attrs) /
	     sizeof(expected_chan_attrs[0]); i++) {
		const struct iio_attr *attr =
			iio_channel_find_attr(chn, expected_chan_attrs[i]);

		TEST_ASSERT_PTR_NOT_NULL(attr, expected_chan_attrs[i]);
	}

	TEST_ASSERT_PTR_NULL(iio_channel_find_attr(chn, "not_an_attr"),
			     "find_attr rejects an unknown name");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(channel_data_format)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	unsigned int i, nb = iio_device_get_channels_count(dev);

	for (i = 0; i < nb; i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		const struct iio_data_format *fmt;

		if (!chn)
			continue;

		fmt = iio_channel_get_data_format(chn);
		TEST_ASSERT_PTR_NOT_NULL(fmt, "channel has a data format");
		if (!fmt)
			continue;

		TEST_LONG_EQ(fmt->length, 16, "storage length is 16 bits");
		TEST_ASSERT(!fmt->is_signed, "samples are unsigned");
		TEST_ASSERT(fmt->bits >= 8 && fmt->bits <= 16,
			    "resolution is between 8 and 16 bits");

#ifdef TEST_CTX_NOOS
		TEST_LONG_EQ(fmt->bits, 16, "adc_demo reports 16 bits");
#endif
	}

	iio_context_destroy(ctx);
}

TEST_FUNCTION(channel_sample_size)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	struct iio_channels_mask *mask;

	mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	TEST_ASSERT_PTR_NOT_NULL(mask, "channels mask created");
	if (!mask || !chn) {
		iio_context_destroy(ctx);
		return;
	}

	iio_channel_enable(chn, mask);
	TEST_LONG_EQ(iio_device_get_sample_size(dev, mask), 2,
		       "one enabled channel is two bytes per sample");

	iio_channels_mask_destroy(mask);
	iio_context_destroy(ctx);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS channel tests (%s) ===\n", test_ctx_label());

	RUN_TEST(channel_identity);
	RUN_TEST(channel_lookup);
	RUN_TEST(channel_attrs);
	RUN_TEST(channel_data_format);
	RUN_TEST(channel_sample_size);

	TEST_SUMMARY();

	return 0;
}
