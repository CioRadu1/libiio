/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"

#define NEWLIB_ENOTSUP	134

static int read_str(struct iio_channel *chn, const char *name, char *dst,
		    size_t len)
{
	const struct iio_attr *attr = iio_channel_find_attr(chn, name);
	ssize_t ret;

	if (!attr)
		return -ENOENT;

	ret = iio_attr_read_raw(attr, dst, len);
	if (ret < 0)
		return (int)ret;

	return 0;
}

static ssize_t write_str(struct iio_channel *chn, const char *name,
			 const char *val)
{
	const struct iio_attr *attr = iio_channel_find_attr(chn, name);

	if (!attr)
		return -ENOENT;

	return iio_attr_write_string(attr, val);
}

TEST_FUNCTION(rw_gain_round_trip)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char before[32], after[32];

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ(read_str(chn, "gain", before, sizeof(before)), 0,
			      "gain is readable before the write");
	TEST_ASSERT(write_str(chn, "gain", "2") > 0, "gain accepts \"2\"");
	TEST_INT_EQ(read_str(chn, "gain", after, sizeof(after)), 0,
			      "gain is readable after the write");
	TEST_STR_EQ(after, "2", "gain reads back what was written");

	TEST_ASSERT(write_str(chn, "gain", before) > 0,
		    "gain is restored to its previous value");
	TEST_INT_EQ(read_str(chn, "gain", after, sizeof(after)), 0,
			      "gain is readable after the restore");
	TEST_STR_EQ(after, before, "gain restore round-trips");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(rw_reference_round_trip)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char before[32], after[32];

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ(read_str(chn, "reference", before,
				       sizeof(before)), 0,
			      "reference is readable before the write");
	TEST_ASSERT(write_str(chn, "reference", "External0") > 0,
		    "reference accepts \"External0\"");
	TEST_INT_EQ(read_str(chn, "reference", after, sizeof(after)),
			      0, "reference is readable after the write");
	TEST_STR_EQ(after, "External0",
			      "reference reads back what was written");

	TEST_ASSERT(write_str(chn, "reference", before) > 0,
		    "reference is restored to its previous value");
	TEST_INT_EQ(read_str(chn, "reference", after, sizeof(after)),
			      0, "reference is readable after the restore");
	TEST_STR_EQ(after, before, "reference restore round-trips");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(rw_differential_round_trip)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	const struct iio_attr *attr;
	long long val = -1;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	attr = iio_channel_find_attr(chn, "differential");

	TEST_ASSERT(iio_attr_write_string(attr, "1") > 0,
		    "differential accepts 1");
	TEST_INT_EQ(iio_attr_read_longlong(attr, &val), 0,
			      "differential is readable after the write");
	TEST_LONG_EQ(val, 1, "differential reads back 1");

	TEST_ASSERT(iio_attr_write_string(attr, "0") > 0,
		    "differential accepts 0");
	TEST_INT_EQ(iio_attr_read_longlong(attr, &val), 0,
			      "differential is readable after the restore");
	TEST_LONG_EQ(val, 0, "differential reads back 0");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(rw_scale_round_trip)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	const struct iio_attr *attr;
	char before[32];
	double val = -1.0;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	attr = iio_channel_find_attr(chn, "scale");

	TEST_INT_EQ(read_str(chn, "scale", before, sizeof(before)), 0,
			      "scale is readable before the write");
	TEST_ASSERT(iio_attr_write_string(attr, "2.500000") > 0,
		    "scale accepts 2.500000");
	TEST_INT_EQ(iio_attr_read_double(attr, &val), 0,
			      "scale is readable after the write");
	TEST_ASSERT(val > 2.4999 && val < 2.5001,
		    "scale round-trips through IIO_VAL_INT_PLUS_MICRO");

	TEST_ASSERT(iio_attr_write_string(attr, before) > 0,
		    "scale is restored to its previous value");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(rw_zero_scale_zeroes_process)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	const struct iio_attr *scale, *process;
	char before[32];
	long long val = -1;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	scale = iio_channel_find_attr(chn, "scale");
	process = iio_channel_find_attr(chn, "process");

	TEST_INT_EQ(read_str(chn, "scale", before, sizeof(before)), 0,
			      "scale is readable before the write");
	TEST_ASSERT(iio_attr_write_string(scale, "0.000000") > 0,
		    "scale accepts zero");
	TEST_INT_EQ(iio_attr_read_longlong(process, &val), 0,
			      "process is readable with a zero scale");
	TEST_LONG_EQ(val, 0, "a zero scale yields a zero process value");

	TEST_ASSERT(iio_attr_write_string(scale, before) > 0,
		    "scale is restored to its previous value");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(rw_rejects_invalid_values)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char before[32], after[32];

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ(read_str(chn, "gain", before, sizeof(before)), 0,
			      "gain is readable before the rejected write");
	TEST_ASSERT(write_str(chn, "gain", "7/9") < 0,
		    "an unknown gain is rejected");
	TEST_INT_EQ(read_str(chn, "gain", after, sizeof(after)), 0,
			      "gain is still readable after the rejection");
	TEST_STR_EQ(after, before,
			      "a rejected gain write leaves the value alone");

	TEST_INT_EQ(read_str(chn, "reference", before,
				       sizeof(before)), 0,
			      "reference is readable before the rejected write");
	TEST_ASSERT(write_str(chn, "reference", "Bogus") < 0,
		    "an unknown reference is rejected");
	TEST_INT_EQ(read_str(chn, "reference", after, sizeof(after)),
			      0,
			      "reference is still readable after the rejection");
	TEST_STR_EQ(after, before,
			      "a rejected reference write leaves the value alone");

	TEST_INT_EQ((int)write_str(chn, "differential", "2"), -EINVAL,
			      "an out-of-range differential gives -EINVAL");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(rw_read_only_attrs)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ((int)write_str(chn, "raw", "1234"), -EPERM,
			      "writing raw gives -EPERM");
	TEST_INT_EQ((int)write_str(chn, "process", "1234"), -EPERM,
			      "writing process gives -EPERM");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(rw_unknown_attr)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_ASSERT_PTR_NULL(iio_channel_find_attr(chn, "no_such_attr"),
			     "an unknown channel attribute is not found");
	TEST_ASSERT_PTR_NULL(iio_device_find_attr(dev, "no_such_attr"),
			     "an unknown device attribute is not found");

	iio_context_destroy(ctx);
}

#ifndef TEST_CTX_NOOS
TEST_FUNCTION(rw_unsupported_reference)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char before[32], after[32];
	int ret;

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ(read_str(chn, "reference", before,
				       sizeof(before)), 0,
			      "reference is readable before the write");
	ret = (int)write_str(chn, "reference", "VDD/2");
	TEST_ASSERT(ret == -ENOTSUP || ret == -NEWLIB_ENOTSUP,
		    "a reference the hardware lacks is refused as unsupported");
	TEST_INT_EQ(read_str(chn, "reference", after, sizeof(after)),
			      0,
			      "reference is still readable after the rejection");
	TEST_STR_EQ(after, before,
			      "a rejected reference write leaves the value alone");

	iio_context_destroy(ctx);
}
#endif

int main(void)
{
	DEBUG_PRINT("=== no-OS read/write tests (%s) ===\n", test_ctx_label());

	RUN_TEST(rw_gain_round_trip);
	RUN_TEST(rw_reference_round_trip);
	RUN_TEST(rw_differential_round_trip);
	RUN_TEST(rw_scale_round_trip);
	RUN_TEST(rw_zero_scale_zeroes_process);
	RUN_TEST(rw_rejects_invalid_values);
	RUN_TEST(rw_read_only_attrs);
	RUN_TEST(rw_unknown_attr);
#ifndef TEST_CTX_NOOS
	RUN_TEST(rw_unsupported_reference);
#endif

	TEST_SUMMARY();

	return 0;
}
