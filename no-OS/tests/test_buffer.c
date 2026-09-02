/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"

#define TEST_SAMPLES	16

#ifdef TEST_CTX_NOOS
static const uint16_t sine_lut[32] = {
	0x8000, 0x98f8, 0xb0fb, 0xc71c, 0xda82, 0xea6d, 0xf641, 0xfd89,
	0xffff, 0xfd89, 0xf641, 0xea6d, 0xda82, 0xc71c, 0xb0fb, 0x98f8,
	0x8000, 0x6707, 0x4f04, 0x38e3, 0x257d, 0x1592, 0x09be, 0x0276,
	0x0000, 0x0276, 0x09be, 0x1592, 0x257d, 0x38e3, 0x4f04, 0x6707,
};
#endif

struct test_stream {
	struct iio_context *ctx;
	struct iio_device *dev;
	struct iio_channel *chn;
	struct iio_buffer *buf;
	struct iio_channels_mask *mask;
	struct iio_buffer_stream *bs;
	struct iio_block *block;
	ssize_t stride;
};

static void stream_close(struct test_stream *s)
{
	if (s->block)
		iio_block_destroy(s->block);
	if (s->bs)
		iio_buffer_close(s->bs);
	if (s->mask)
		iio_channels_mask_destroy(s->mask);
	if (s->ctx)
		iio_context_destroy(s->ctx);

	memset(s, 0, sizeof(*s));
}

static bool stream_open(struct test_stream *s)
{
	unsigned int nb;
	int ret;

	memset(s, 0, sizeof(*s));

	s->ctx = test_ctx_require();
	s->dev = test_dev_require(s->ctx);
	nb = iio_device_get_channels_count(s->dev);

	s->chn = iio_device_get_channel(s->dev, 0);
	if (!s->chn) {
		TEST_ASSERT(false, "the device has a first channel");
		goto err;
	}

	s->buf = iio_device_get_buffer(s->dev, 0);
	if (iio_err(s->buf)) {
		TEST_ASSERT(false, "the device has a registered buffer");
		goto err;
	}

	s->mask = iio_create_channels_mask(nb);
	if (!s->mask) {
		TEST_ASSERT(false, "a channels mask can be allocated");
		goto err;
	}

	iio_channel_enable(s->chn, s->mask);

	s->stride = iio_device_get_sample_size(s->dev, s->mask);
	if (s->stride <= 0) {
		TEST_ASSERT(false, "the sample size is positive");
		goto err;
	}

	s->bs = iio_buffer_open(s->buf, s->mask);
	ret = iio_err(s->bs);
	if (ret) {
		s->bs = NULL;
		TEST_INT_EQ(ret, 0, "the buffer opens");
		goto err;
	}

	s->block = iio_buffer_stream_create_block(s->bs,
						  TEST_SAMPLES * s->stride);
	ret = iio_err(s->block);
	if (ret) {
		s->block = NULL;
		TEST_INT_EQ(ret, 0, "a block can be created");
		goto err;
	}

	ret = iio_buffer_stream_start(s->bs);
	if (ret) {
		TEST_INT_EQ(ret, 0, "the buffer stream starts");
		goto err;
	}

	return true;

err:
	stream_close(s);

	return false;
}

TEST_FUNCTION(buffer_registered)
{
	struct iio_context *ctx = test_ctx_require();
	struct iio_device *dev = test_dev_require(ctx);
	struct iio_buffer *buf;

	TEST_INT_EQ(iio_device_get_buffers_count(dev), 1,
			      "the device has exactly one buffer");

	buf = iio_device_get_buffer(dev, 0);
	TEST_INT_EQ(iio_err(buf), 0, "buffer 0 is reachable");

	if (!iio_err(buf)) {
		TEST_ASSERT(!iio_buffer_is_output(buf),
			    "the buffer is an input buffer");
		TEST_ASSERT(iio_buffer_get_device(buf) == dev,
			    "the buffer points back at its device");
		TEST_INT_EQ(
			iio_buffer_get_scan_elements_count(buf),
			iio_device_get_channels_count(dev),
			"every channel is a scan element of the buffer");
	}

	iio_context_destroy(ctx);
}

TEST_FUNCTION(buffer_stride)
{
	struct test_stream s;

	if (!stream_open(&s))
		return;

	TEST_LONG_EQ(s.stride, 2,
		       "one enabled 16-bit channel gives a 2-byte stride");

	stream_close(&s);
}

TEST_FUNCTION(buffer_transfer)
{
	struct test_stream s;
	long raw_max;
	void *ptr, *end;
	unsigned int count = 0;
	int ret;

	if (!stream_open(&s))
		return;

	raw_max = test_chan_raw_max(s.chn);

	ret = iio_block_enqueue(s.block, 0, false);
	TEST_INT_EQ(ret, 0, "the block enqueues");
	if (ret) {
		stream_close(&s);
		return;
	}

	ret = iio_block_dequeue(s.block, false);
	TEST_ASSERT(ret >= 0, "the block dequeues");
	if (ret < 0) {
		stream_close(&s);
		return;
	}

	ptr = iio_block_first(s.block, s.chn);
	end = iio_block_end(s.block);

	TEST_ASSERT_PTR_NOT_NULL(ptr, "the channel has a first sample");
	TEST_ASSERT(ptr < end, "the block holds at least one sample");

	for (; ptr < end; ptr = (char *)ptr + s.stride) {
		uint16_t val;

		memcpy(&val, ptr, sizeof(val));
		if (val > raw_max) {
			TEST_ASSERT(false,
				    "every sample fits the channel resolution");
			break;
		}
		count++;
	}

	TEST_LONG_EQ(count, TEST_SAMPLES,
		       "the block holds the requested sample count");

	stream_close(&s);
}

TEST_FUNCTION(buffer_channel_read)
{
	struct test_stream s;
	uint16_t samples[TEST_SAMPLES];
	size_t bytes;
	int ret;

	if (!stream_open(&s))
		return;

	ret = iio_block_enqueue(s.block, 0, false);
	if (ret) {
		TEST_INT_EQ(ret, 0, "the block enqueues");
		stream_close(&s);
		return;
	}

	ret = iio_block_dequeue(s.block, false);
	if (ret < 0) {
		TEST_ASSERT(ret >= 0, "the block dequeues");
		stream_close(&s);
		return;
	}

	memset(samples, 0xa5, sizeof(samples));
	bytes = iio_channel_read(s.chn, s.block, samples, sizeof(samples),
				 true);

	TEST_LONG_EQ(bytes, sizeof(samples),
		       "iio_channel_read demuxes the whole block");

#ifdef TEST_CTX_NOOS
	{
		unsigned int i, offset = 0;
		bool matched = false;

		for (offset = 0; offset < 32 && !matched; offset++) {
			matched = true;

			for (i = 0; i < TEST_SAMPLES; i++) {
				if (samples[i] != sine_lut[(offset + i) % 32]) {
					matched = false;
					break;
				}
			}
		}

		TEST_ASSERT(matched,
			    "the samples are consecutive adc_demo sine entries");
	}
#endif

	stream_close(&s);
}

TEST_FUNCTION(buffer_reopen)
{
	struct test_stream s;

	if (!stream_open(&s))
		return;

	stream_close(&s);

	if (!stream_open(&s))
		return;

	TEST_INT_EQ(iio_block_enqueue(s.block, 0, false), 0,
			      "the block enqueues after a reopen");
	TEST_ASSERT(iio_block_dequeue(s.block, false) >= 0,
		    "the block dequeues after a reopen");

	stream_close(&s);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS buffer tests (%s) ===\n", test_ctx_label());

	RUN_TEST(buffer_registered);
	RUN_TEST(buffer_stride);
	RUN_TEST(buffer_transfer);
	RUN_TEST(buffer_channel_read);
	RUN_TEST(buffer_reopen);

	TEST_SUMMARY();

	return 0;
}
