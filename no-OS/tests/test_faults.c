/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "test_ctx.h"
#include "test_framework.h"

#define IIOD_PORT		30431
#define BINARY_HDR		"BINARY\r\n"
#define STORM_ROUNDS		500
#define STORM_REPORT_EVERY	100
#define HEALTH_RETRIES		12
#define STREAM_SAMPLES		16
#define KILL_DELAY_US		200000
#define STREAM_READY_TIMEOUT_S	20
#define NOOS_MAX_CLIENTS	4

static char target_host[64];

static bool target_resolve(void)
{
	const char *uri = getenv("NOOS_TESTS_URI");

	if (!uri || strncmp(uri, "ip:", 3))
		return false;

	snprintf(target_host, sizeof(target_host), "%s", uri + 3);

	return target_host[0] != '\0';
}

static void settle(void)
{
	struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };

	nanosleep(&ts, NULL);
}

static int raw_connect(void)
{
	struct sockaddr_in addr;
	int fd;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(IIOD_PORT);

	if (inet_pton(AF_INET, target_host, &addr.sin_addr) != 1)
		return -1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
		close(fd);
		return -1;
	}

	return fd;
}

static void raw_abort(int fd)
{
	struct linger lg = { .l_onoff = 1, .l_linger = 0 };

	setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
	close(fd);
}

static bool raw_handshake(int fd)
{
	char ack[16];
	ssize_t ret;

	ret = send(fd, BINARY_HDR, sizeof(BINARY_HDR) - 1, MSG_NOSIGNAL);
	if (ret != (ssize_t)sizeof(BINARY_HDR) - 1)
		return false;

	settle();
	recv(fd, ack, sizeof(ack), MSG_DONTWAIT);

	return true;
}

static struct iio_context *health_ctx(unsigned int *tries)
{
	struct iio_context *ctx;
	unsigned int try;

	for (try = 0; ; try++) {
		ctx = test_ctx_create();
		if (!iio_err(ctx) || try == HEALTH_RETRIES) {
			*tries = try;
			return ctx;
		}

		settle();
	}
}

static bool server_healthy(const char *what)
{
	struct iio_context *ctx;
	struct iio_device *dev;
	const struct iio_channel *chn;
	const struct iio_attr *attr;
	unsigned int tries = 0;
	long long raw = 0;
	int err, ret;

	ctx = health_ctx(&tries);
	err = iio_err(ctx);
	TEST_OUT("%s: context err = %d after %u retr%s", what, err, tries,
		 tries == 1 ? "y" : "ies");

	if (err) {
		TEST_INT_EQ(err, 0, "the server still accepts a new client");
		return false;
	}

	dev = iio_context_find_device(ctx, TEST_DEVICE_NAME);
	if (!dev) {
		TEST_ASSERT(false, "the device is still present");
		iio_context_destroy(ctx);
		return false;
	}

	chn = iio_device_find_channel(dev, "voltage0", false);
	if (!chn) {
		TEST_ASSERT(false, "channel voltage0 is still present");
		iio_context_destroy(ctx);
		return false;
	}

	attr = iio_channel_find_attr(chn, "raw");
	if (!attr) {
		TEST_ASSERT(false, "attribute raw is still present");
		iio_context_destroy(ctx);
		return false;
	}

	ret = iio_attr_read_longlong(attr, &raw);
	TEST_OUT("%s: read raw ret = %d, value = %lld", what, ret, raw);
	TEST_INT_EQ(ret, 0, "the server still serves a read");

	iio_context_destroy(ctx);

	return ret == 0;
}

TEST_FUNCTION(fault_truncated_handshake)
{
	int fd;

	TEST_IN("send 3 bytes of the handshake, then close with a FIN");

	fd = raw_connect();
	if (fd < 0) {
		TEST_ASSERT(false, "a raw connection can be made");
		return;
	}

	TEST_INT_EQ(send(fd, BINARY_HDR, 3, MSG_NOSIGNAL), 3,
		    "a partial handshake is accepted by the transport");
	close(fd);

	settle();
	server_healthy("after a truncated handshake");
}

TEST_FUNCTION(fault_reset_before_handshake)
{
	int fd;

	TEST_IN("connect, then abort with an RST instead of a FIN");

	fd = raw_connect();
	if (fd < 0) {
		TEST_ASSERT(false, "a raw connection can be made");
		return;
	}

	raw_abort(fd);

	settle();
	server_healthy("after an RST before the handshake");
}

TEST_FUNCTION(fault_truncated_command)
{
	static const char partial[3] = { 0x01, 0x00, 0x00 };
	int fd;

	TEST_IN("complete the handshake, send 3 bytes of an 8-byte command, "
		"then abort with an RST");

	fd = raw_connect();
	if (fd < 0) {
		TEST_ASSERT(false, "a raw connection can be made");
		return;
	}

	if (!raw_handshake(fd)) {
		TEST_ASSERT(false, "the handshake can be sent");
		close(fd);
		return;
	}

	TEST_INT_EQ(send(fd, partial, sizeof(partial), MSG_NOSIGNAL),
		    (ssize_t)sizeof(partial),
		    "a partial command is accepted by the transport");
	raw_abort(fd);

	settle();
	server_healthy("after a truncated command");
}

TEST_FUNCTION(fault_connect_close_storm)
{
	unsigned int i, made = 0, refused = 0;
	int fd;

	TEST_IN("%d connect/close cycles, alternating FIN and RST, with no "
		"protocol exchange", STORM_ROUNDS);

	for (i = 0; i < STORM_ROUNDS; i++) {
		fd = raw_connect();
		if (fd < 0) {
			refused++;
			settle();
			continue;
		}

		made++;

		if (i & 1)
			raw_abort(fd);
		else
			close(fd);

		if ((i + 1) % STORM_REPORT_EVERY == 0)
			TEST_OUT("%u cycles done (%u connected, %u refused)",
				 i + 1, made, refused);
	}

	TEST_OUT("%u of %d cycles connected, %u refused", made, STORM_ROUNDS,
		 refused);
	TEST_ASSERT(made > STORM_ROUNDS / 2,
		    "the server keeps accepting through the storm");

	settle();
	server_healthy("after a connect/close storm");
}

static void stream_child(int ready_fd)
{
	struct iio_context *ctx;
	struct iio_device *dev;
	struct iio_channel *chn;
	struct iio_buffer *buf;
	struct iio_channels_mask *mask;
	struct iio_buffer_stream *bs;
	struct iio_block *block;
	ssize_t stride;

	ctx = test_ctx_create();
	if (iio_err(ctx))
		_exit(2);

	dev = iio_context_find_device(ctx, TEST_DEVICE_NAME);
	chn = dev ? iio_device_get_channel(dev, 0) : NULL;
	buf = dev ? iio_device_get_buffer(dev, 0) : NULL;
	if (!chn || !buf || iio_err(buf))
		_exit(3);

	mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!mask)
		_exit(4);

	iio_channel_enable(chn, mask);

	stride = iio_device_get_sample_size(dev, mask);
	if (stride <= 0)
		_exit(5);

	bs = iio_buffer_open(buf, mask);
	if (iio_err(bs))
		_exit(6);

	block = iio_buffer_stream_create_block(bs, STREAM_SAMPLES * stride);
	if (iio_err(block))
		_exit(7);

	if (iio_buffer_stream_start(bs))
		_exit(8);

	while (1) {
		if (iio_block_enqueue(block, 0, false))
			_exit(9);
		if (iio_block_dequeue(block, false) < 0)
			_exit(10);

		if (ready_fd >= 0) {
			if (write(ready_fd, "x", 1) != 1)
				_exit(11);

			close(ready_fd);
			ready_fd = -1;
		}
	}
}

static bool stream_wait_ready(int fd)
{
	struct timeval tv = { .tv_sec = STREAM_READY_TIMEOUT_S, .tv_usec = 0 };
	fd_set rd;
	char b;

	FD_ZERO(&rd);
	FD_SET(fd, &rd);

	if (select(fd + 1, &rd, NULL, NULL, &tv) != 1)
		return false;

	return read(fd, &b, 1) == 1;
}

TEST_FUNCTION(fault_stream_client_killed)
{
	int fds[2], status = 0;
	bool streaming;
	pid_t pid;

	TEST_IN("SIGKILL a client in the middle of a buffer stream");

	if (pipe(fds)) {
		TEST_ASSERT(false, "a readiness pipe can be created");
		return;
	}

	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		TEST_ASSERT(false, "a streaming child can be forked");
		return;
	}

	if (!pid) {
		close(fds[0]);
		stream_child(fds[1]);
	}

	close(fds[1]);
	streaming = stream_wait_ready(fds[0]);
	close(fds[0]);
	TEST_ASSERT(streaming, "the client reaches a live stream");

	usleep(KILL_DELAY_US);

	TEST_INT_EQ(kill(pid, SIGKILL), 0, "the streaming client is killed");
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		TEST_OUT("the client exited on its own with status %d",
			 WEXITSTATUS(status));
	TEST_ASSERT(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
		    "the client died on SIGKILL rather than exiting");

	settle();
	server_healthy("after a stream client was killed");
}

TEST_FUNCTION(fault_silent_client_does_not_block)
{
	int fd;

	TEST_IN("hold a connection open that never speaks, then serve another "
		"client");

	fd = raw_connect();
	if (fd < 0) {
		TEST_ASSERT(false, "a raw connection can be made");
		return;
	}

	server_healthy("with a silent client attached");

	close(fd);
	settle();
}

TEST_FUNCTION(fault_slots_recovered)
{
	struct iio_context *ctx[NOOS_MAX_CLIENTS];
	unsigned int i, opened = 0, tries;

	TEST_IN("after every fault above, open %d clients at once",
		NOOS_MAX_CLIENTS);

	for (i = 0; i < NOOS_MAX_CLIENTS; i++) {
		ctx[i] = health_ctx(&tries);
		if (iio_err(ctx[i])) {
			TEST_OUT("client %u: err = %d after %u retries", i,
				 iio_err(ctx[i]), tries);
			ctx[i] = NULL;
			continue;
		}

		opened++;
	}

	TEST_OUT("opened %u of %d", opened, NOOS_MAX_CLIENTS);
	TEST_INT_EQ(opened, NOOS_MAX_CLIENTS,
		    "no client slot was leaked by the faults");

	for (i = 0; i < NOOS_MAX_CLIENTS; i++) {
		if (ctx[i])
			iio_context_destroy(ctx[i]);
	}
}

int main(void)
{
	DEBUG_PRINT("=== no-OS fault injection tests (%s) ===\n",
		    test_ctx_label());

	if (!target_resolve()) {
		DEBUG_PRINT("this suite needs an ip: uri, skipping %s\n",
			    test_ctx_label());
		return 0;
	}

	signal(SIGPIPE, SIG_IGN);

	RUN_TEST(fault_truncated_handshake);
	RUN_TEST(fault_reset_before_handshake);
	RUN_TEST(fault_truncated_command);
	RUN_TEST(fault_connect_close_storm);
	RUN_TEST(fault_stream_client_killed);
	RUN_TEST(fault_silent_client_does_not_block);
	RUN_TEST(fault_slots_recovered);

	TEST_SUMMARY();

	return 0;
}
