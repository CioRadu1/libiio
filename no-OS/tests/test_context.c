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

	TEST_ASSERT_PTR_NOT_NULL(name, "context has a name");
#ifdef TEST_CTX_NOOS
	if (name)
		TEST_STR_EQ(name, "no-os", "backend name is no-os");
#else
	if (name)
		TEST_ASSERT(name[0] != '\0',
			    "the transport backend reports a name");
#endif

	TEST_ASSERT_PTR_NOT_NULL(descr, "context has a description");
	if (descr)
		TEST_ASSERT(strstr(descr, "no-OS") != NULL,
			    "description mentions no-OS");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(context_version)
{
	struct iio_context *ctx = test_ctx_require();

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

	second = test_ctx_create();
	TEST_INT_EQ(iio_err(second), 0,
			      "second context created after destroy");
	if (iio_err(second))
		return;

	TEST_LONG_EQ(iio_context_get_devices_count(second), count,
		       "second context sees the same device count");

	iio_context_destroy(second);
}

TEST_FUNCTION(context_bad_uri)
{
	struct iio_context *ctx = iio_create_context(&test_ctx_params,
						     "no-such-backend:");

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
