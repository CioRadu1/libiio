/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"

TEST_FUNCTION(device_identity)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	const char *id = iio_device_get_id(dev);
	const char *name = iio_device_get_name(dev);

	TEST_ASSERT_PTR_NOT_NULL(id, "device has an id");
	if (id)
		TEST_STR_EQ(id, TEST_DEVICE_ID, "device id");

	TEST_ASSERT_PTR_NOT_NULL(name, "device has a name");
	if (name)
		TEST_STR_EQ(name, TEST_DEVICE_NAME, "device name");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(device_lookup)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);

	TEST_ASSERT(iio_context_find_device(ctx, TEST_DEVICE_ID) == dev,
		    "find_device by id returns device 0");
	TEST_ASSERT(iio_context_find_device(ctx, TEST_DEVICE_NAME) == dev,
		    "find_device by name returns device 0");
	TEST_ASSERT_PTR_NULL(iio_context_find_device(ctx, "not-a-device"),
			     "find_device rejects an unknown name");
	TEST_ASSERT_PTR_NULL(iio_context_get_device(ctx, 99),
			     "get_device rejects an out-of-range index");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(device_channels_registered)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	unsigned int nb = iio_device_get_channels_count(dev);

	TEST_ASSERT(nb >= 1, "device exposes at least one channel");
	TEST_ASSERT_PTR_NOT_NULL(iio_device_get_channel(dev, 0),
				 "channel 0 is reachable");
	TEST_ASSERT_PTR_NULL(iio_device_get_channel(dev, nb),
			     "get_channel rejects an out-of-range index");

#ifdef TEST_CTX_NOOS
	TEST_LONG_EQ(nb, 2, "adc_demo exposes two channels");
#endif

	iio_context_destroy(ctx);
}

TEST_FUNCTION(device_attrs)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	const struct iio_attr *attr;

	TEST_LONG_EQ(iio_device_get_attrs_count(dev), 1,
		       "device exposes one device-level attribute");

	attr = iio_device_get_attr(dev, 0);
	TEST_ASSERT_PTR_NOT_NULL(attr, "device attribute 0 is reachable");
	if (attr)
		TEST_STR_EQ(iio_attr_get_name(attr),
				   "internal_ref_voltage",
				   "device attribute name");

	TEST_ASSERT_PTR_NOT_NULL(iio_device_find_attr(dev,
						      "internal_ref_voltage"),
				 "find_attr locates internal_ref_voltage");
	TEST_ASSERT_PTR_NULL(iio_device_find_attr(dev, "not_an_attr"),
			     "find_attr rejects an unknown name");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(device_buffer_registered)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_buffer *buf;

	TEST_LONG_EQ(iio_device_get_buffers_count(dev), 1,
		       "device exposes one buffer");

	buf = iio_device_get_buffer(dev, 0);
	TEST_ASSERT_PTR_NOT_NULL(buf, "buffer 0 is reachable");
	if (buf) {
		TEST_ASSERT(!iio_buffer_is_output(buf),
			    "buffer direction is input");
		TEST_LONG_EQ(iio_buffer_get_scan_elements_count(buf),
			       iio_device_get_channels_count(dev),
			       "every channel is registered as a scan element");
	}

	iio_context_destroy(ctx);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS device tests (%s) ===\n", test_ctx_label());

	RUN_TEST(device_identity);
	RUN_TEST(device_lookup);
	RUN_TEST(device_channels_registered);
	RUN_TEST(device_attrs);
	RUN_TEST(device_buffer_registered);

	TEST_SUMMARY();

	return 0;
}
