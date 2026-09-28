/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <time.h>

#include "test_ctx.h"
#include "test_framework.h"

#define NOOS_MAX_SOCKETS	8
#define NOOS_MAX_CLIENTS	4
#define TEST_ROUNDS		4
#define TEST_SAMPLES		16
#define REFUSAL_BUDGET_S	20.0
#define OPEN_RETRIES		8

struct test_client {
	struct iio_context *ctx;
	struct iio_device *dev;
	struct iio_channel *chn;
};

static double elapsed_since(time_t start)
{
	return difftime(time(NULL), start);
}

static void settle(void)
{
	time_t start = time(NULL);

	while (difftime(time(NULL), start) < 1.0)
		;
}

static struct iio_context *ctx_create_retry(void)
{
	struct iio_context *ctx;
	unsigned int try;

	for (try = 0; ; try++) {
		ctx = test_ctx_create();
		if (!iio_err(ctx) || try == OPEN_RETRIES)
			return ctx;

		settle();
	}
}

static void clients_close(struct test_client *cl, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (cl[i].ctx)
			iio_context_destroy(cl[i].ctx);

		cl[i].ctx = NULL;
	}
}

static unsigned int clients_open(struct test_client *cl, unsigned int n)
{
	unsigned int i;

	memset(cl, 0, n * sizeof(*cl));

	for (i = 0; i < n; i++) {
		int err;

		cl[i].ctx = ctx_create_retry();
		err = iio_err(cl[i].ctx);

		TEST_OUT("client %u: iio_create_context() err = %d", i, err);
		if (err) {
			cl[i].ctx = NULL;
			return i;
		}

		cl[i].dev = iio_context_get_device(cl[i].ctx, 0);
		cl[i].chn = cl[i].dev ?
			    iio_device_get_channel(cl[i].dev, 0) : NULL;
	}

	return n;
}

static bool clients_open_all(struct test_client *cl, unsigned int n)
{
	unsigned int opened = clients_open(cl, n);

	if (opened == n)
		return true;

	TEST_INT_EQ(opened, n, "every client connects");
	clients_close(cl, opened);

	return false;
}

static int read_raw_ll(const struct iio_channel *chn, const char *name,
		       long long *val)
{
	const struct iio_attr *attr = iio_channel_find_attr(chn, name);

	if (!attr)
		return -ENOENT;

	return iio_attr_read_longlong(attr, val);
}

TEST_FUNCTION(concurrent_contexts_open)
{
	struct test_client cl[NOOS_MAX_CLIENTS];
	unsigned int i, opened;

	TEST_IN("open %d contexts on %s at the same time", NOOS_MAX_CLIENTS,
		test_ctx_label());
	opened = clients_open(cl, NOOS_MAX_CLIENTS);
	TEST_OUT("opened %u of %d", opened, NOOS_MAX_CLIENTS);

	TEST_INT_EQ(opened, NOOS_MAX_CLIENTS,
		    "all clients connect concurrently");

	for (i = 0; i < opened; i++) {
		const char *name = cl[i].dev ?
				   iio_device_get_name(cl[i].dev) : NULL;

		TEST_OUT("client %u: device 0 = \"%s\"", i,
			 name ? name : "(null)");
		TEST_ASSERT_PTR_NOT_NULL(cl[i].dev,
					 "each client sees device 0");
		if (name)
			TEST_STR_EQ(name, TEST_DEVICE_NAME,
				    "each client sees the same device");
	}

	clients_close(cl, opened);
}

TEST_FUNCTION(concurrent_interleaved_reads)
{
	struct test_client cl[NOOS_MAX_CLIENTS];
	unsigned int i, r;
	long raw_max;

	if (!clients_open_all(cl, NOOS_MAX_CLIENTS))
		return;

	raw_max = test_chan_raw_max(cl[0].chn);

	TEST_IN("%d rounds of \"raw\" reads round-robin over %d clients, "
		"expecting 0..%ld", TEST_ROUNDS, NOOS_MAX_CLIENTS, raw_max);

	for (r = 0; r < TEST_ROUNDS; r++) {
		for (i = 0; i < NOOS_MAX_CLIENTS; i++) {
			long long val = -1;
			int ret = read_raw_ll(cl[i].chn, "raw", &val);

			TEST_OUT("round %u client %u: ret = %d, raw = %lld",
				 r, i, ret, val);
			TEST_INT_EQ(ret, 0,
				    "an interleaved raw read succeeds");
			TEST_ASSERT(val >= 0 && val <= raw_max,
				    "an interleaved raw read is in range");
		}
	}

	clients_close(cl, NOOS_MAX_CLIENTS);
}

TEST_FUNCTION(concurrent_shared_device_state)
{
	struct test_client cl[2];
	const struct iio_attr *wattr, *rattr;
	char got[32] = "";
	ssize_t ret;

	if (!clients_open_all(cl, 2))
		return;

	wattr = iio_channel_find_attr(cl[0].chn, "gain");
	rattr = iio_channel_find_attr(cl[1].chn, "gain");

	TEST_ASSERT_PTR_NOT_NULL(wattr, "client 0 finds the gain attribute");
	TEST_ASSERT_PTR_NOT_NULL(rattr, "client 1 finds the gain attribute");
	if (!wattr || !rattr)
		goto out;

	TEST_IN("client 0 writes gain = \"2\", then client 1 reads it back");

	ret = iio_attr_write_string(wattr, "2");
	TEST_OUT("client 0 write ret = %zd", ret);
	TEST_LONG_EQ(ret > 0 ? 0 : ret, 0, "client 0 writes the gain");

	ret = iio_attr_read_raw(rattr, got, sizeof(got));
	TEST_OUT("client 1 read ret = %zd, value = \"%s\"", ret,
		 ret > 0 ? got : "");
	TEST_ASSERT(ret > 0, "client 1 reads the gain back");
	if (ret > 0)
		TEST_STR_EQ(got, "2",
			    "both clients share the one physical device");

	ret = iio_attr_write_string(wattr, "1");
	TEST_OUT("restore gain = \"1\" -> ret %zd", ret);
	TEST_ASSERT(ret > 0, "the gain is restored to its default");

out:
	clients_close(cl, 2);
}

TEST_FUNCTION(concurrent_stream_and_attrs)
{
	struct test_client cl[2];
	struct iio_channels_mask *mask = NULL;
	struct iio_buffer_stream *bs = NULL;
	struct iio_block *block = NULL;
	struct iio_buffer *buf;
	unsigned int nb, r;
	ssize_t stride;
	int ret;

	if (!clients_open_all(cl, 2))
		return;

	nb = iio_device_get_channels_count(cl[0].dev);

	buf = iio_device_get_buffer(cl[0].dev, 0);
	if (iio_err(buf)) {
		TEST_INT_EQ(iio_err(buf), 0, "client 0 reaches the buffer");
		goto out;
	}

	mask = iio_create_channels_mask(nb);
	if (!mask) {
		TEST_ASSERT(false, "a channels mask can be allocated");
		goto out;
	}

	iio_channel_enable(cl[0].chn, mask);

	stride = iio_device_get_sample_size(cl[0].dev, mask);
	if (stride <= 0) {
		TEST_ASSERT(false, "the sample size is positive");
		goto out;
	}

	bs = iio_buffer_open(buf, mask);
	ret = iio_err(bs);
	if (ret) {
		bs = NULL;
		TEST_INT_EQ(ret, 0, "client 0 opens the buffer");
		goto out;
	}

	block = iio_buffer_stream_create_block(bs, TEST_SAMPLES * stride);
	ret = iio_err(block);
	if (ret) {
		block = NULL;
		TEST_INT_EQ(ret, 0, "client 0 creates a block");
		goto out;
	}

	ret = iio_buffer_stream_start(bs);
	if (ret) {
		TEST_INT_EQ(ret, 0, "client 0 starts the stream");
		goto out;
	}

	TEST_IN("client 0 streams %d-sample blocks while client 1 reads "
		"\"raw\", for %d rounds", TEST_SAMPLES, TEST_ROUNDS);

	for (r = 0; r < TEST_ROUNDS; r++) {
		long long val = -1;

		ret = iio_block_enqueue(block, 0, false);
		TEST_OUT("round %u: client 0 enqueue ret = %d", r, ret);
		TEST_INT_EQ(ret, 0, "the streaming client enqueues a block");
		if (ret)
			break;

		ret = iio_block_dequeue(block, false);
		TEST_OUT("round %u: client 0 dequeue ret = %d", r, ret);
		TEST_ASSERT(ret >= 0, "the streaming client dequeues a block");
		if (ret < 0)
			break;

		ret = read_raw_ll(cl[1].chn, "raw", &val);
		TEST_OUT("round %u: client 1 raw ret = %d, value = %lld", r,
			 ret, val);
		TEST_INT_EQ(ret, 0,
			    "the other client reads while a stream is live");
	}

out:
	if (block)
		iio_block_destroy(block);
	if (bs)
		iio_buffer_close(bs);
	if (mask)
		iio_channels_mask_destroy(mask);

	clients_close(cl, 2);
}

TEST_FUNCTION(concurrent_client_limit)
{
	struct test_client cl[NOOS_MAX_SOCKETS];
	struct iio_context *extra;
	unsigned int opened;
	double secs;
	time_t start;
	int err;

	TEST_IN("open contexts on %s until the server refuses one",
		test_ctx_label());

	opened = clients_open(cl, NOOS_MAX_SOCKETS);
	TEST_OUT("opened %u, of %d slots (the rest of the lwIP pool may still "
		 "be in TIME_WAIT from earlier suites)", opened,
		 NOOS_MAX_SOCKETS);

	TEST_ASSERT(opened >= NOOS_MAX_CLIENTS,
		    "the server serves at least the advertised client count");

	if (!opened)
		return;

	TEST_IN("with %u clients connected, attempt one more context", opened);

	start = time(NULL);
	extra = test_ctx_create();
	err = iio_err(extra);
	secs = elapsed_since(start);

	TEST_OUT("context %u: err = %d after %.0f s", opened, err, secs);
	TEST_ASSERT(err != 0 || opened < NOOS_MAX_SOCKETS,
		    "the server refuses more clients than it has slots");
	TEST_ASSERT(secs < REFUSAL_BUDGET_S,
		    "the refusal is prompt rather than a hang");

	if (!err)
		iio_context_destroy(extra);

	clients_close(cl, opened);
}

TEST_FUNCTION(concurrent_slot_reuse)
{
	struct test_client cl[NOOS_MAX_CLIENTS];
	long long val = -1;
	unsigned int opened;

	if (!clients_open_all(cl, NOOS_MAX_CLIENTS))
		return;

	clients_close(cl, NOOS_MAX_CLIENTS);

	TEST_IN("close all %d clients, then reconnect all %d",
		NOOS_MAX_CLIENTS, NOOS_MAX_CLIENTS);

	opened = clients_open(cl, NOOS_MAX_CLIENTS);
	TEST_OUT("reconnected %u of %d", opened, NOOS_MAX_CLIENTS);
	TEST_INT_EQ(opened, NOOS_MAX_CLIENTS,
		    "every client slot is reusable after a disconnect");

	if (opened == NOOS_MAX_CLIENTS) {
		int ret = read_raw_ll(cl[opened - 1].chn, "raw", &val);
		TEST_OUT("last reconnected client: ret = %d, raw = %lld", ret,
			 val);
		TEST_INT_EQ(ret, 0, "a reused slot serves requests");
	}

	clients_close(cl, opened);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS concurrent client tests (%s) ===\n",
		    test_ctx_label());

	RUN_TEST(concurrent_contexts_open);
	RUN_TEST(concurrent_interleaved_reads);
	RUN_TEST(concurrent_shared_device_state);
	RUN_TEST(concurrent_stream_and_attrs);
	RUN_TEST(concurrent_client_limit);
	RUN_TEST(concurrent_slot_reuse);

	TEST_SUMMARY();

	return 0;
}
