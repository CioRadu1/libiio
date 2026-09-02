/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_TESTS_TEST_CTX_H
#define NOOS_TESTS_TEST_CTX_H

#include <iio/iio.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iio_adc.h"
#include "iio_device.h"

#define TEST_INT_EQ(actual, expected, message)				\
	do {								\
		long long test_val = (long long)(actual);		\
									\
		TEST_ASSERT_INT_EQUAL(test_val, (expected), message);	\
	} while (0)

#define TEST_LONG_EQ(actual, expected, message)				\
	do {								\
		long long test_val = (long long)(actual);		\
									\
		TEST_ASSERT_EQ(test_val, (expected), message);		\
	} while (0)

#define TEST_STR_EQ(actual, expected, message)				\
	do {								\
		const char *test_val = (actual);			\
									\
		TEST_ASSERT_STR_EQ(test_val, (expected), message);	\
	} while (0)

#define TEST_DEVICE_NAME	"iio-adc"

static const struct iio_context_params test_ctx_params = {
	.log_level = LEVEL_WARNING,
};

static inline struct iio_context *test_ctx_create(void)
{
	static bool registered;
	struct noos_iio_device_info adc_info;
	int ret;

	if (!registered) {
		ret = iio_adc_init();
		if (ret) {
			fprintf(stderr, "iio_adc_init failed: %d\n", ret);
			return iio_ptr(ret);
		}

		ret = iio_adc_get_device_info(&adc_info);
		if (ret) {
			fprintf(stderr, "iio_adc_get_device_info failed: %d\n",
				ret);
			return iio_ptr(ret);
		}

		ret = noos_iio_register_device(&adc_info);
		if (ret) {
			fprintf(stderr, "noos_iio_register_device failed: %d\n",
				ret);
			return iio_ptr(ret);
		}

		registered = true;
	}

	return iio_create_context(&test_ctx_params, "no-os:");
}

static inline struct iio_context *test_ctx_require(void)
{
	struct iio_context *ctx = test_ctx_create();
	int err = iio_err(ctx);

	if (err) {
		fprintf(stderr, "unable to create the no-os context: %d\n",
			err);
		exit(EXIT_FAILURE);
	}

	return ctx;
}

#endif /* NOOS_TESTS_TEST_CTX_H */
