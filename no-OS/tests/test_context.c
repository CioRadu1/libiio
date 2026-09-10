/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"

TEST_FUNCTION(context_create)
{
	struct iio_context *ctx = test_ctx_create();

	TEST_IN("uri = %s", test_ctx_label());
	TEST_OUT("iio_create_context() err = %d", iio_err(ctx));
	TEST_INT_EQ(iio_err(ctx), 0, "context created without error");
	if (iio_err(ctx))
		return;

	iio_context_destroy(ctx);
}

TEST_FUNCTION(context_identity)
{
	struct iio_context *ctx = test_ctx_require();
	const char *name = iio_context_get_name(ctx);
	const char *descr = iio_context_get_description(ctx);

	TEST_OUT("name = \"%s\"", name ? name : "(null)");
	TEST_OUT("description = \"%s\"", descr ? descr : "(null)");
	TEST_ASSERT_PTR_NOT_NULL(name, "context has a name");
	if (name)
		TEST_ASSERT(name[0] != '\0',
			    "the transport backend reports a name");

	TEST_ASSERT_PTR_NOT_NULL(descr, "context has a description");
	if (descr)
		TEST_ASSERT(strstr(descr, "no-OS") != NULL,
			    "description mentions no-OS");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(context_version)
{
	struct iio_context *ctx = test_ctx_require();
	const char *tag = iio_context_get_version_tag(ctx);

	TEST_OUT("version = %u.%u tag \"%s\"",
		 iio_context_get_version_major(ctx),
		 iio_context_get_version_minor(ctx), tag ? tag : "(null)");
	TEST_LONG_EQ(iio_context_get_version_major(ctx), 1,
		       "version major is 1");
	TEST_LONG_EQ(iio_context_get_version_minor(ctx), 0,
		       "version minor is 0");
	TEST_ASSERT_PTR_NOT_NULL(iio_context_get_version_tag(ctx),
				 "version tag present");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(context_device_count)
{
	struct iio_context *ctx = test_ctx_require();

	TEST_OUT("devices_count = %u", iio_context_get_devices_count(ctx));
	TEST_LONG_EQ(iio_context_get_devices_count(ctx), 1,
		       "context holds exactly one device");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(context_recreate)
{
	struct iio_context *first = test_ctx_require();
	unsigned int count = iio_context_get_devices_count(first);
	struct iio_context *second;

	iio_context_destroy(first);

	TEST_IN("first context saw %u device(s), destroyed, creating again", count);
	second = test_ctx_create();
	TEST_OUT("second context err = %d", iio_err(second));
	TEST_INT_EQ(iio_err(second), 0,
			      "second context created after destroy");
	if (iio_err(second))
		return;

	TEST_OUT("second devices_count = %u",
		 iio_context_get_devices_count(second));
	TEST_LONG_EQ(iio_context_get_devices_count(second), count,
		       "second context sees the same device count");

	iio_context_destroy(second);
}

TEST_FUNCTION(context_bad_uri)
{
	struct iio_context *ctx = iio_create_context(&test_ctx_params,
						     "no-such-backend:");

	TEST_IN("uri = \"no-such-backend:\"");
	TEST_OUT("err = %d", iio_err(ctx));
	TEST_ASSERT(iio_err(ctx) < 0,
		    "an unknown uri prefix is rejected with an error");
	if (!iio_err(ctx))
		iio_context_destroy(ctx);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS context tests (%s) ===\n", test_ctx_label());

	RUN_TEST(context_create);
	RUN_TEST(context_identity);
	RUN_TEST(context_version);
	RUN_TEST(context_device_count);
	RUN_TEST(context_recreate);
	RUN_TEST(context_bad_uri);

	TEST_SUMMARY();

	return 0;
}
