/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_TESTS_TEST_CTX_H
#define NOOS_TESTS_TEST_CTX_H

#include <iio/iio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void test_log(const char *tag, const char *fmt, ...)
{
	va_list ap;

	printf("    %-5s", tag);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

#ifdef TESTS_DEBUG
#define TEST_IN(...)	test_log("in:", __VA_ARGS__)
#define TEST_OUT(...)	test_log("out:", __VA_ARGS__)
#else
#define TEST_IN(...)	do { if (0) test_log("in:", __VA_ARGS__); } while (0)
#define TEST_OUT(...)	do { if (0) test_log("out:", __VA_ARGS__); } while (0)
#endif

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
#define TEST_DEVICE_ID		"iio:device0"

static const struct iio_context_params test_ctx_params = {
	.log_level = LEVEL_WARNING,
};

static inline const char *test_ctx_label(void)
{
	const char *uri = getenv("NOOS_TESTS_URI");

	return uri ? uri : "(unset)";
}

static inline struct iio_context *test_ctx_create(void)
{
	const char *uri = getenv("NOOS_TESTS_URI");

	if (!uri || !uri[0]) {
		fprintf(stderr,
			"NOOS_TESTS_URI is not set; expected something like "
			"serial:/dev/ttyACM0,115200, ip:192.0.2.1 or usb:\n");
		exit(EXIT_FAILURE);
	}

	return iio_create_context(&test_ctx_params, uri);
}

static inline struct iio_context *test_ctx_require(void)
{
	struct iio_context *ctx = test_ctx_create();
	int err = iio_err(ctx);

	if (err) {
		fprintf(stderr, "unable to create context %s: %d\n",
			test_ctx_label(), err);
		exit(EXIT_FAILURE);
	}

	return ctx;
}

static inline struct iio_device *test_dev_require(struct iio_context *ctx)
{
	struct iio_device *dev = iio_context_get_device(ctx, 0);

	if (!dev) {
		fprintf(stderr, "context %s has no device 0\n",
			test_ctx_label());
		iio_context_destroy(ctx);
		exit(EXIT_FAILURE);
	}

	return dev;
}

static inline unsigned int test_chan_bits(const struct iio_channel *chn)
{
	const struct iio_data_format *fmt = iio_channel_get_data_format(chn);

	return fmt ? fmt->bits : 0;
}

static inline long test_chan_raw_max(const struct iio_channel *chn)
{
	unsigned int bits = test_chan_bits(chn);

	if (!bits || bits > 31)
		return 0;

	return (1L << bits) - 1;
}

#endif /* NOOS_TESTS_TEST_CTX_H */
