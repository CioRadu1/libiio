/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"
#include <iio/iio-backend.h>

static char probe_name[] = "probe-device";

static int probe_add_channels(void *dev, struct iio_device *iio_dev)
{
	struct iio_data_format fmt = {
		.length = 16,
		.bits = 16,
		.is_signed = false,
	};

	if (!iio_device_add_channel(iio_dev, 0, "voltage0", NULL, NULL,
				    false, true, &fmt))
		return -ENOMEM;

	return iio_device_add_attr(iio_dev, "probe_attr", IIO_ATTR_TYPE_DEVICE);
}

static struct noos_iio_device_info probe_info = {
	.name = probe_name,
	.dev = NULL,
	.direction = 0,
	.add_channels = probe_add_channels,
};

TEST_FUNCTION(register_rejects_null)
{
	unsigned int before = noos_iio_device_count;

	TEST_INT_EQ(noos_iio_register_device(NULL), -EINVAL,
		    "registering a NULL device gives -EINVAL");
	TEST_INT_EQ(noos_iio_device_count, before,
		    "a rejected registration does not grow the table");
}

TEST_FUNCTION(register_appends_to_the_table)
{
	unsigned int before = noos_iio_device_count;
	struct noos_iio_device_info info = probe_info;

	info.dev = &probe_info;

	TEST_INT_EQ(noos_iio_register_device(&info), 0,
		    "a device registers successfully");
	TEST_INT_EQ(noos_iio_device_count, before + 1,
		    "the device count grows by one");
	TEST_STR_EQ(noos_iio_devices[before].name, probe_name,
		    "the table holds the registered name");
	TEST_ASSERT(noos_iio_devices[before].dev == &probe_info,
		    "the table holds the device handle");
	TEST_ASSERT(noos_iio_devices[before].add_channels == probe_add_channels,
		    "the table holds the add_channels callback");
}

TEST_FUNCTION(register_copies_the_info)
{
	unsigned int slot = noos_iio_device_count;
	struct noos_iio_device_info info = probe_info;

	TEST_INT_EQ(noos_iio_register_device(&info), 0,
		    "a device registers from a local structure");

	memset(&info, 0, sizeof(info));

	TEST_ASSERT_PTR_NOT_NULL(noos_iio_devices[slot].name,
				 "the stored name survives the caller");
	if (noos_iio_devices[slot].name)
		TEST_STR_EQ(noos_iio_devices[slot].name, probe_name,
			    "the registration copied the info, not the pointer");
	TEST_ASSERT(noos_iio_devices[slot].add_channels == probe_add_channels,
		    "the stored callback survives the caller");
}

TEST_FUNCTION(register_fills_the_table)
{
	unsigned int i;
	int ret = 0;

	for (i = noos_iio_device_count; i < NOOS_IIO_MAX_DEVICES; i++) {
		ret = noos_iio_register_device(&probe_info);
		if (ret)
			break;
	}

	TEST_INT_EQ(ret, 0, "the table accepts NOOS_IIO_MAX_DEVICES entries");
	TEST_INT_EQ(noos_iio_device_count, NOOS_IIO_MAX_DEVICES,
		    "the table is full");
	TEST_INT_EQ(noos_iio_register_device(&probe_info), -ENOMEM,
		    "one device too many gives -ENOMEM");
	TEST_INT_EQ(noos_iio_device_count, NOOS_IIO_MAX_DEVICES,
		    "a refused registration does not grow the table");
}

TEST_FUNCTION(backend_exposes_every_registered_device)
{
	struct iio_context *ctx;
	unsigned int i, count;

	ctx = iio_create_context(&test_ctx_params, "no-os:");
	TEST_INT_EQ(iio_err(ctx), 0, "a context is created from a full table");
	if (iio_err(ctx))
		return;

	count = iio_context_get_devices_count(ctx);
	TEST_INT_EQ(count, noos_iio_device_count,
		    "the context exposes every registered device");

	for (i = 0; i < count; i++) {
		struct iio_device *dev;
		char id[32];

		snprintf(id, sizeof(id), "iio:device%u", i);
		dev = iio_context_find_device(ctx, id);

		TEST_ASSERT_PTR_NOT_NULL(dev,
					 "the backend gives every device a sequential id");
		if (!dev)
			continue;

		TEST_STR_EQ(iio_device_get_name(dev), probe_name,
			    "the device name comes from the registration");
		TEST_INT_EQ(iio_device_get_channels_count(dev), 1,
			    "the registered add_channels callback ran");
	}

	iio_context_destroy(ctx);
}

TEST_FUNCTION(backend_reports_missing_callbacks)
{
	struct iio_context *ctx;
	struct iio_device *dev;
	const struct iio_attr *attr;
	char buf[32];
	int ret;

	ctx = iio_create_context(&test_ctx_params, "no-os:");
	if (iio_err(ctx))
		return;

	dev = iio_context_get_device(ctx, 0);
	if (!dev) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ(iio_device_get_attrs_count(dev), 1,
		    "the attribute added by add_channels is exposed");

	TEST_ASSERT_PTR_NULL(iio_device_find_attr(dev, "internal_ref_voltage"),
			     "an unregistered attribute is not found");

	attr = iio_device_find_attr(dev, "probe_attr");
	TEST_ASSERT_PTR_NOT_NULL(attr, "the registered attribute is found");
	if (!attr) {
		iio_context_destroy(ctx);
		return;
	}

	ret = (int)iio_attr_read_raw(attr, buf, sizeof(buf));
	TEST_INT_EQ(ret, -ENOSYS,
		    "a device without read_attr reports -ENOSYS");

	ret = (int)iio_attr_write_string(attr, "1");
	TEST_INT_EQ(ret, -ENOSYS,
		    "a device without write_attr reports -ENOSYS");

	iio_context_destroy(ctx);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS backend registration tests ===\n");

	RUN_TEST(register_rejects_null);
	RUN_TEST(register_appends_to_the_table);
	RUN_TEST(register_copies_the_info);
	RUN_TEST(register_fills_the_table);
	RUN_TEST(backend_exposes_every_registered_device);
	RUN_TEST(backend_reports_missing_callbacks);

	TEST_SUMMARY();

	return 0;
}
