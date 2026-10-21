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

	TEST_IN("read \"%s\" into %zu bytes", name, len);
	ret = iio_attr_read_raw(attr, dst, len);
	TEST_OUT("read \"%s\" -> ret %zd, value \"%s\"", name, ret,
		 ret > 0 ? dst : "");
	if (ret < 0)
		return (int)ret;

	return 0;
}

static ssize_t write_str(struct iio_channel *chn, const char *name,
			 const char *val)
{
	const struct iio_attr *attr = iio_channel_find_attr(chn, name);
	ssize_t ret;

	if (!attr)
		return -ENOENT;

	TEST_IN("write \"%s\" = \"%s\"", name, val);
	ret = iio_attr_write_string(attr, val);
	TEST_OUT("write \"%s\" -> ret %zd", name, ret);

	return ret;
}

/*
 * The sample's ADCs have raw and a read-only scale; a part whose reference
 * can be switched (the MAX32690) has reference too, the shared names of
 * which it takes those reference_available lists. The reference tests skip
 * a part without one.
 */

/* A reference the part has other than @cur, from reference_available. */
static bool other_reference(struct iio_channel *chn, const char *cur,
			    char *dst, size_t len)
{
	char avail[128], *tok, *save = NULL;

	if (read_str(chn, "reference_available", avail, sizeof(avail)))
		return false;

	for (tok = strtok_r(avail, " ", &save); tok;
	     tok = strtok_r(NULL, " ", &save)) {
		if (strcmp(tok, cur)) {
			snprintf(dst, len, "%s", tok);
			return true;
		}
	}

	return false;
}

TEST_FUNCTION(rw_reference_round_trip)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char before[32], other[32], after[32];

	if (!chn || !iio_channel_find_attr(chn, "reference")) {
		DEBUG_PRINT("  SKIP: the part has no reference to switch\n");
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ(read_str(chn, "reference", before, sizeof(before)), 0,
		    "reference is readable before the write");
	if (!other_reference(chn, before, other, sizeof(other))) {
		DEBUG_PRINT("  SKIP: the part has one reference only\n");
		iio_context_destroy(ctx);
		return;
	}

	TEST_ASSERT(write_str(chn, "reference", other) > 0,
		    "reference accepts another one it lists");
	TEST_INT_EQ(read_str(chn, "reference", after, sizeof(after)), 0,
		    "reference is readable after the write");
	TEST_STR_EQ(after, other, "reference reads back what was written");

	TEST_ASSERT(write_str(chn, "reference", before) > 0,
		    "reference is restored to its previous value");
	TEST_INT_EQ(read_str(chn, "reference", after, sizeof(after)), 0,
		    "reference is readable after the restore");
	TEST_STR_EQ(after, before, "reference restore round-trips");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(rw_rejects_invalid_values)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char before[32], after[32];

	if (!chn || !iio_channel_find_attr(chn, "reference")) {
		DEBUG_PRINT("  SKIP: the part has no reference to switch\n");
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ(read_str(chn, "reference", before, sizeof(before)), 0,
		    "reference is readable before the rejected write");
	TEST_IN("expecting -EINVAL (%d)", -EINVAL);
	TEST_INT_EQ((int)write_str(chn, "reference", "Bogus"), -EINVAL,
		    "an unknown reference gives -EINVAL");
	TEST_INT_EQ(read_str(chn, "reference", after, sizeof(after)), 0,
		    "reference is still readable after the rejection");
	TEST_STR_EQ(after, before,
		    "a rejected reference write leaves the value alone");

	iio_context_destroy(ctx);
}

TEST_FUNCTION(rw_read_only_attrs)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char before[32], after[32];

	if (!chn) {
		iio_context_destroy(ctx);
		return;
	}

	TEST_IN("expecting -EPERM (%d) for both", -EPERM);
	TEST_INT_EQ((int)write_str(chn, "raw", "1234"), -EPERM,
		    "writing raw gives -EPERM");

	TEST_INT_EQ(read_str(chn, "scale", before, sizeof(before)), 0,
		    "scale is readable before the write");
	TEST_INT_EQ((int)write_str(chn, "scale", "2.500000"), -EPERM,
		    "writing scale gives -EPERM: it follows the reference");
	TEST_INT_EQ(read_str(chn, "scale", after, sizeof(after)), 0,
		    "scale is readable after the write");
	TEST_STR_EQ(after, before, "the refused write leaves scale alone");

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

	TEST_IN("find_attr(\"no_such_attr\") on the channel and the device");
	TEST_OUT("channel = %p, device = %p",
		 (void *)iio_channel_find_attr(chn, "no_such_attr"),
		 (void *)iio_device_find_attr(dev, "no_such_attr"));
	TEST_ASSERT_PTR_NULL(iio_channel_find_attr(chn, "no_such_attr"),
			     "an unknown channel attribute is not found");
	TEST_ASSERT_PTR_NULL(iio_device_find_attr(dev, "no_such_attr"),
			     "an unknown device attribute is not found");

	iio_context_destroy(ctx);
}

/* A shared reference name the part lacks is refused as unsupported. */
TEST_FUNCTION(rw_unsupported_reference)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_channel *chn = iio_device_get_channel(dev, 0);
	char before[32], after[32];
	int ret;

	if (!chn || !iio_channel_find_attr(chn, "reference")) {
		DEBUG_PRINT("  SKIP: the part has no reference to switch\n");
		iio_context_destroy(ctx);
		return;
	}

	TEST_INT_EQ(read_str(chn, "reference", before, sizeof(before)), 0,
		    "reference is readable before the write");
	ret = (int)write_str(chn, "reference", "VDD/2");
	TEST_IN("expecting -ENOTSUP (%d) or newlib's (%d)", -ENOTSUP,
		-NEWLIB_ENOTSUP);
	TEST_OUT("ret = %d", ret);
	TEST_ASSERT(ret == -ENOTSUP || ret == -NEWLIB_ENOTSUP,
		    "a reference the hardware lacks is refused as unsupported");
	TEST_INT_EQ(read_str(chn, "reference", after, sizeof(after)), 0,
		    "reference is still readable after the rejection");
	TEST_STR_EQ(after, before,
		    "a rejected reference write leaves the value alone");

	iio_context_destroy(ctx);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS read/write tests (%s) ===\n", test_ctx_label());

	RUN_TEST(rw_reference_round_trip);
	RUN_TEST(rw_rejects_invalid_values);
	RUN_TEST(rw_read_only_attrs);
	RUN_TEST(rw_unknown_attr);
	RUN_TEST(rw_unsupported_reference);

	TEST_SUMMARY();

	return 0;
}
