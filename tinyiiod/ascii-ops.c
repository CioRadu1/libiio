// SPDX-License-Identifier: TODO
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * ASCII (libiio v0.x) protocol command handlers for the Zephyr iiod server.
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "iio/iio.h"

#include "../iiod/ops.h"
#include "../iiod/parser.h"

int yyparse(yyscan_t scanner);

/* Identical to print_value() in ops.c. */
static void print_value(struct parser_pdata *pdata, long value)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "%li\n", value);
	output(pdata, buf);
}

/* Identical to iiod_htobe32() in ops.c. */
static inline uint32_t iiod_htobe32(uint32_t word)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return word;
#elif defined(__GNUC__)
	return __builtin_bswap32(word);
#else
	return ((word & 0xff) << 24) | ((word & 0xff00) << 8) | ((word >> 8) & 0xff00) |
	       ((word >> 24) & 0xff);
#endif
}

/* Identical to iiod_be32toh() in ops.c. */
static inline uint32_t iiod_be32toh(uint32_t word)
{
	return iiod_htobe32(word);
}

#define READ_ATTR_BUF_SIZE 1024

typedef struct iio_attr *(*rw_attr_cb_t)(const void *, unsigned int);

/* Identical to buffer_analyze() in ops.c. */
static int buffer_analyze(unsigned int nb, const char *src, size_t len)
{
	while (nb--) {
		int32_t val;

		if (len < 4)
			return -EINVAL;

		val = (int32_t)iiod_be32toh(*(uint32_t *)src);
		src += 4;
		len -= 4;

		if (val > 0) {
			if ((uint32_t)val > len)
				return -EINVAL;

			/* Align the length to 4 bytes */
			if (val & 3)
				val = ((val >> 2) + 1) << 2;
			len -= val;
			src += val;
		}
	}

	/* We should have analyzed the whole buffer by now */
	return !len ? 0 : -EINVAL;
}

/* Identical to read_each_attr() in ops.c. */
static ssize_t read_each_attr(
		const void *iio, char *buf, size_t len, unsigned int nb, rw_attr_cb_t cb)
{
	const struct iio_attr *attr;
	unsigned int i;
	char *ptr = buf;
	ssize_t ret;

	for (i = 0; len >= 4 && i < nb; i++) {
		attr = (*cb)(iio, i);
		if (!attr)
			ret = -ENOENT;
		else
			ret = iio_attr_read_raw(attr, ptr + 4, len - 4);
		*(uint32_t *)ptr = iiod_htobe32(ret);

		/* Align the length to 4 bytes */
		ret = ret < 0 ? 0 : (ret + 3) & ~0x3;
		ptr += 4 + ret;
		len -= 4 + ret;
	}

	return ptr - buf;
}

/* Identical to write_each_attr() in ops.c. */
static ssize_t write_each_attr(
		const void *iio, const char *buf, size_t len, unsigned int nb, rw_attr_cb_t cb)
{
	const struct iio_attr *attr;
	const char *ptr = buf;
	unsigned int i;
	ssize_t ret;
	int32_t val;

	ret = buffer_analyze(nb, buf, len);
	if (ret < 0)
		return ret;

	for (i = 0; i < nb; i++) {
		val = (int32_t)iiod_be32toh(*(uint32_t *)ptr);
		ptr += 4;

		if (val > 0) {
			attr = (*cb)(iio, i);
			if (!attr)
				continue;

			iio_attr_write_raw(attr, ptr, val);

			/* Align the length to 4 bytes */
			ptr += (val + 3) & ~0x3;
		}
	}

	return ptr - buf;
}

/* Functionally identical to read_dev_attr() in ops.c, except the value buffer
 * is heap-allocated (READ_ATTR_BUF_SIZE) instead of a large on-stack array */
ssize_t read_dev_attr(struct parser_pdata *pdata, struct iio_device *dev, const char *name,
		enum iio_attr_type type)
{
	const struct iio_attr *attr;
	struct iio_buffer *buffer;
	char *buf;
	ssize_t ret = -EINVAL;
	unsigned int nb;

	if (!dev) {
		print_value(pdata, -ENODEV);
		return -ENODEV;
	}

	buf = malloc(READ_ATTR_BUF_SIZE);
	if (!buf) {
		print_value(pdata, -ENOMEM);
		return -ENOMEM;
	}

	if (!name) {
		switch (type) {
		case IIO_ATTR_TYPE_DEVICE:
			nb = iio_device_get_attrs_count(dev);
			ret = read_each_attr(dev, buf, READ_ATTR_BUF_SIZE - 1, nb,
					(rw_attr_cb_t)iio_device_get_attr);
			break;
		case IIO_ATTR_TYPE_DEBUG:
			nb = iio_device_get_debug_attrs_count(dev);
			ret = read_each_attr(dev, buf, READ_ATTR_BUF_SIZE - 1, nb,
					(rw_attr_cb_t)iio_device_get_debug_attr);
			break;
		default:
			ret = -EINVAL;
			goto out_free_buffer;
		}

		goto out_print_value;
	}

	switch (type) {
	case IIO_ATTR_TYPE_DEVICE:
		attr = iio_device_find_attr(dev, name);
		if (attr)
			ret = iio_attr_read_raw(attr, buf, READ_ATTR_BUF_SIZE - 1);
		else
			ret = -ENOENT;
		break;
	case IIO_ATTR_TYPE_DEBUG:
		attr = iio_device_find_debug_attr(dev, name);
		if (attr)
			ret = iio_attr_read_raw(attr, buf, READ_ATTR_BUF_SIZE - 1);
		else
			ret = -ENOENT;
		break;
	case IIO_ATTR_TYPE_BUFFER:
		buffer = iio_device_get_buffer(dev, 0);
		if (buffer) {
			attr = iio_buffer_find_attr(buffer, name);
			if (attr)
				ret = iio_attr_read_raw(attr, buf, READ_ATTR_BUF_SIZE - 1);
			else
				ret = -ENOENT;
		} else {
			ret = -EBADF;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

out_print_value:
	print_value(pdata, ret);
	if (ret < 0)
		goto out_free_buffer;

	buf[ret] = '\n';
	ret = write_all(pdata, buf, ret + 1);

out_free_buffer:
	free(buf);
	return ret;
}

/* Functionally identical to write_dev_attr() in ops.c. Differs only on the
 * unsupported-attr-type path, where the allocated buffer is freed (via goto)
 * instead of returned past, avoiding a leak. TODO - check supposed leak */
ssize_t write_dev_attr(struct parser_pdata *pdata, struct iio_device *dev, const char *name,
		size_t len, enum iio_attr_type type)
{
	const struct iio_attr *attr;
	struct iio_buffer *buffer;
	unsigned int nb;
	ssize_t ret = -ENOMEM;
	char *buf;

	if (!dev) {
		ret = -ENODEV;
		goto out_print_value;
	}

	buf = malloc(len);
	if (!buf)
		goto out_print_value;

	ret = read_all(pdata, buf, len);
	if (ret < 0)
		goto out_free_buffer;

	if (!name) {
		switch (type) {
		case IIO_ATTR_TYPE_DEVICE:
			nb = iio_device_get_attrs_count(dev);
			ret = write_each_attr(
					dev, buf, len - 1, nb, (rw_attr_cb_t)iio_device_get_attr);
			break;
		case IIO_ATTR_TYPE_DEBUG:
			nb = iio_device_get_debug_attrs_count(dev);
			ret = write_each_attr(dev, buf, len - 1, nb,
					(rw_attr_cb_t)iio_device_get_debug_attr);
			break;
		default:
			ret = -EINVAL;
			goto out_free_buffer;
		}

		goto out_free_buffer;
	}

	switch (type) {
	case IIO_ATTR_TYPE_DEVICE:
		attr = iio_device_find_attr(dev, name);
		if (attr)
			ret = iio_attr_write_raw(attr, buf, len);
		else
			ret = -ENOENT;
		break;
	case IIO_ATTR_TYPE_DEBUG:
		attr = iio_device_find_debug_attr(dev, name);
		if (attr)
			ret = iio_attr_write_raw(attr, buf, len);
		else
			ret = -ENOENT;
		break;
	case IIO_ATTR_TYPE_BUFFER:
		buffer = iio_device_get_buffer(dev, 0);
		if (buffer) {
			attr = iio_buffer_find_attr(buffer, name);
			if (attr)
				ret = iio_attr_write_raw(attr, buf, len);
			else
				ret = -ENOENT;
		} else {
			ret = -EBADF;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

out_free_buffer:
	free(buf);
out_print_value:
	print_value(pdata, ret);
	return ret;
}

/* Functionally identical to read_chn_attr() in ops.c, except the value buffer
 * is heap-allocated (READ_ATTR_BUF_SIZE) instead of a large on-stack array */
ssize_t read_chn_attr(struct parser_pdata *pdata, struct iio_channel *chn, const char *name)
{
	char *buf;
	ssize_t ret = -ENODEV;
	const struct iio_attr *attr;
	unsigned int nb;

	if (!chn) {
		ret = pdata->dev ? -ENXIO : -ENODEV;
		print_value(pdata, ret);
		return ret;
	}

	buf = malloc(READ_ATTR_BUF_SIZE);
	if (!buf) {
		print_value(pdata, -ENOMEM);
		return -ENOMEM;
	}

	if (!name) {
		nb = iio_channel_get_attrs_count(chn);
		ret = read_each_attr(chn, buf, READ_ATTR_BUF_SIZE - 1, nb,
				(rw_attr_cb_t)iio_channel_get_attr);
	} else {
		attr = iio_channel_find_attr(chn, name);
		if (attr)
			ret = iio_attr_read_raw(attr, buf, READ_ATTR_BUF_SIZE - 1);
		else
			ret = -ENOENT;
	}

	print_value(pdata, ret);
	if (ret < 0)
		goto out_free_buffer;

	buf[ret] = '\n';
	ret = write_all(pdata, buf, ret + 1);

out_free_buffer:
	free(buf);
	return ret;
}

/* Identical to write_chn_attr() in ops.c. */
ssize_t write_chn_attr(
		struct parser_pdata *pdata, struct iio_channel *chn, const char *name, size_t len)
{
	const struct iio_attr *attr;
	ssize_t ret = -ENOMEM;
	unsigned int nb;
	char *buf;

	buf = malloc(len);
	if (!buf)
		goto err_print_value;

	ret = read_all(pdata, buf, len);
	if (ret < 0)
		goto err_free_buffer;

	if (!chn) {
		ret = pdata->dev ? -ENXIO : -ENODEV;
		goto err_free_buffer;
	}

	if (!name) {
		nb = iio_channel_get_attrs_count(chn);
		ret = write_each_attr(
				chn, buf, sizeof(buf) - 1, nb, (rw_attr_cb_t)iio_channel_get_attr);
	} else {
		attr = iio_channel_find_attr(chn, name);
		if (attr)
			ret = iio_attr_write_raw(attr, buf, len);
		else
			ret = -ENOENT;
	}

err_free_buffer:
	free(buf);
err_print_value:
	print_value(pdata, ret);
	return ret;
}

/* Identical to set_trigger() in ops.c. */
ssize_t set_trigger(struct parser_pdata *pdata, struct iio_device *dev, const char *trigger)
{
	struct iio_device *trig = NULL;
	ssize_t ret = -ENOENT;

	if (!dev) {
		ret = -ENODEV;
		goto err_print_value;
	}

	if (trigger) {
		trig = iio_context_find_device(pdata->ctx, trigger);
		if (!trig)
			goto err_print_value;
	}

	ret = iio_device_set_trigger(dev, trig);
err_print_value:
	print_value(pdata, ret);
	return ret;
}

/* Identical to get_trigger() in ops.c. */
ssize_t get_trigger(struct parser_pdata *pdata, struct iio_device *dev)
{
	const struct iio_device *trigger;
	ssize_t ret;

	if (!dev) {
		print_value(pdata, -ENODEV);
		return -ENODEV;
	}

	trigger = iio_device_get_trigger(dev);
	ret = iio_err(trigger);
	if (!ret) {
		const char *name = iio_device_get_name(trigger);
		char buf[256];

		ret = strlen(name);
		print_value(pdata, ret);

		snprintf(buf, sizeof(buf), "%s\n", name);
		ret = write_all(pdata, buf, ret + 1);
	} else {
		print_value(pdata, ret);
	}
	return ret;
}

/* Identical to set_timeout() in ops.c. */
int set_timeout(struct parser_pdata *pdata, int timeout)
{
	int translated_timeout = timeout;
	int ret;

	/* Translate v0 client timeout semantics to v1 semantics:
	 * - v0 clients (non-binary) use: 0 = infinite, positive = timeout
	 * - v1 clients (binary) use: -1 = infinite, 0 = backend default, positive = timeout
	 */
	if (!pdata->binary && timeout == 0) {
		/* v0 client sending 0 (infinite) -> translate to v1 infinite (-1) */
		translated_timeout = -1;
	}

	ret = iio_context_set_timeout(pdata->ctx, translated_timeout);
	print_value(pdata, ret);
	return ret;
}

/*
 * Differs from the ops.c streaming path: there is no per-device RW thread and
 * no client list. One device can be open at a time and each READBUF/WRITEBUF
 * is served synchronously on the caller's transport with a single block.
 */
struct v0_stream {
	struct iio_device *dev;
	struct iio_channels_mask *mask;
	struct iio_buffer_stream *buf_stream;
	struct iio_block *block;
	size_t sample_size;
	size_t block_size;
	bool is_output;
	bool cyclic;
	bool enqueued;
};

static struct v0_stream v0_stream;

#define V0_MIN(a, b)	((a) < (b) ? (a) : (b))

static void v0_stream_free(struct v0_stream *s)
{
	if (s->buf_stream)
		iio_buffer_stream_stop(s->buf_stream);
	if (s->block)
		iio_block_destroy(s->block);
	if (s->buf_stream)
		iio_buffer_close(s->buf_stream);
	if (s->mask)
		iio_channels_mask_destroy(s->mask);

	memset(s, 0, sizeof(*s));
}

/* Same format as get_mask() in ops.c: hex words, most significant first. */
static int v0_parse_mask(const struct iio_device *dev, const char *mask,
			 struct iio_channels_mask *chn_mask)
{
	unsigned int i, nb_channels = iio_device_get_channels_count(dev);
	size_t nb_words = (nb_channels + 31) / 32;
	unsigned int word = 0;
	char buf[9];

	if (strlen(mask) != nb_words * 8)
		return -EINVAL;

	for (i = 0; i < nb_channels; i++) {
		if (!(i % 32)) {
			/* Word n sits at offset (nb_words - 1 - n) * 8 */
			memcpy(buf, mask + (nb_words - 1 - i / 32) * 8, 8);
			buf[8] = '\0';
			if (sscanf(buf, "%08x", &word) != 1)
				return -EINVAL;
		}

		if (word & (1u << (i % 32)))
			iio_channel_enable(iio_device_get_channel(dev, i),
					   chn_mask);
	}

	return 0;
}

/* Same line as send_data() in ops.c writes ahead of the first chunk. */
static ssize_t v0_send_mask(struct parser_pdata *pdata, const struct v0_stream *s)
{
	unsigned int i, nb_channels = iio_device_get_channels_count(s->dev);
	unsigned int nb_words = (nb_channels + 31) / 32;
	char buf[8 * 4 + 2], *ptr = buf;

	if (nb_words > 4)
		return -ENOSPC;

	for (i = nb_words; i > 0; i--, ptr += 8) {
		unsigned int j, word = 0;

		for (j = 0; j < 32 && (i - 1) * 32 + j < nb_channels; j++) {
			const struct iio_channel *chn;

			chn = iio_device_get_channel(s->dev, (i - 1) * 32 + j);
			if (iio_channel_is_enabled(chn, s->mask))
				word |= 1u << j;
		}

		snprintf(ptr, 9, "%08x", word);
	}

	*ptr++ = '\n';

	return write_all(pdata, buf, ptr - buf);
}

/* Accepted for compatibility; the single-block stream ignores the count. */
int set_buffers_count(struct parser_pdata *pdata, struct iio_device *dev, long value)
{
	int ret = 0;

	if (value < 1)
		ret = -EINVAL;
	else if (!dev)
		ret = -ENODEV;

	print_value(pdata, ret);
	return ret;
}

int open_dev(struct parser_pdata *pdata, struct iio_device *dev, size_t samples_count,
		const char *mask, bool cyclic)
{
	struct v0_stream *s = &v0_stream;
	struct iio_buffer *buf;
	int ret;

	if (!dev) {
		ret = -ENODEV;
		goto out_print_value;
	}

	if (s->dev) {
		ret = -EBUSY;
		goto out_print_value;
	}

	buf = iio_device_get_buffer(dev, 0);
	if (!buf) {
		ret = -ENODEV;
		goto out_print_value;
	}

	s->dev = dev;
	s->cyclic = cyclic;
	s->is_output = iio_buffer_is_output(buf);

	s->mask = iio_create_channels_mask(iio_device_get_channels_count(dev));
	if (!s->mask) {
		ret = -ENOMEM;
		goto err_free_stream;
	}

	ret = v0_parse_mask(dev, mask, s->mask);
	if (ret)
		goto err_free_stream;

	ret = (int)iio_device_get_sample_size(dev, s->mask);
	if (ret <= 0) {
		ret = ret ? ret : -EINVAL;
		goto err_free_stream;
	}

	s->sample_size = (size_t)ret;
	s->block_size = samples_count * s->sample_size;

	s->buf_stream = iio_buffer_open(buf, s->mask);
	ret = iio_err(s->buf_stream);
	if (ret) {
		s->buf_stream = NULL;
		goto err_free_stream;
	}

	s->block = iio_buffer_stream_create_block(s->buf_stream, s->block_size);
	ret = iio_err(s->block);
	if (ret) {
		s->block = NULL;
		goto err_free_stream;
	}

	/* Input only: queue the empty block so the first READBUF has data */
	if (!s->is_output) {
		ret = iio_block_enqueue(s->block, 0, false);
		if (ret)
			goto err_free_stream;

		s->enqueued = true;
	}

	ret = iio_buffer_stream_start(s->buf_stream);
	if (ret)
		goto err_free_stream;

	goto out_print_value;

err_free_stream:
	v0_stream_free(s);
out_print_value:
	print_value(pdata, ret);
	return ret;
}

int close_dev(struct parser_pdata *pdata, struct iio_device *dev)
{
	int ret = 0;

	if (!dev)
		ret = -ENODEV;
	else if (v0_stream.dev != dev)
		ret = -ENXIO;
	else
		v0_stream_free(&v0_stream);

	print_value(pdata, ret);
	return ret;
}

/* Mirrors rw_thd()/send_data() in ops.c for one reader, no demux. */
static ssize_t v0_read(struct parser_pdata *pdata, struct v0_stream *s,
		       unsigned int nb)
{
	bool send_mask = true;
	ssize_t ret;

	while (nb >= s->sample_size) {
		size_t len;

		ret = iio_block_dequeue(s->block, false);
		if (ret < 0)
			return ret;

		s->enqueued = false;

		len = V0_MIN(s->block_size, nb);

		print_value(pdata, (long)len);

		if (send_mask) {
			ret = v0_send_mask(pdata, s);
			if (ret < 0)
				return ret;

			send_mask = false;
		}

		ret = write_all(pdata, iio_block_start(s->block), len);
		if (ret < 0)
			return ret;

		nb -= len;

		ret = iio_block_enqueue(s->block, 0, false);
		if (ret)
			return ret;

		s->enqueued = true;
	}

	return nb;
}

/* Mirrors rw_thd()/receive_data() in ops.c for one writer, no mux. */
static ssize_t v0_write(struct parser_pdata *pdata, struct v0_stream *s,
			unsigned int nb)
{
	ssize_t ret;

	/* Inform that no error occurred, and that we'll start reading data */
	print_value(pdata, 0);

	while (nb >= s->sample_size) {
		size_t len;

		/* A cyclic block is never handed back */
		if (s->enqueued && !s->cyclic) {
			ret = iio_block_dequeue(s->block, false);
			if (ret < 0)
				return ret;

			s->enqueued = false;
		}

		len = V0_MIN(s->block_size, nb);

		ret = read_all(pdata, iio_block_start(s->block), len);
		if (ret < 0)
			return ret;

		nb -= len;

		ret = iio_block_enqueue(s->block, len, s->cyclic);
		if (ret)
			return ret;

		s->enqueued = true;
	}

	return nb;
}

ssize_t rw_dev(struct parser_pdata *pdata, struct iio_device *dev, unsigned int nb, bool is_write)
{
	struct v0_stream *s = &v0_stream;
	ssize_t ret;

	if (!dev)
		ret = -ENODEV;
	else if (s->dev != dev)
		ret = -EBADF;
	else if (is_write != s->is_output)
		ret = -EINVAL;
	else if (nb < s->sample_size)
		ret = 0;
	else {
		ret = is_write ? v0_write(pdata, s, nb) : v0_read(pdata, s, nb);

		/* Same replies as rw_buffer() in ops.c; ret is what is left */
		if (ret > 0 && ret < (ssize_t)nb)
			print_value(pdata, 0);
		if (ret >= 0)
			ret = nb - ret;
	}

	if (ret <= 0 || is_write)
		print_value(pdata, ret);
	return ret;
}

/*
 * Differs from invalidate_sample_size_cache() in ops.c: reimplemented as a
 * no-op. Called from responder.c (binary path) when a channel's format changes.
 * The ops.c version signals the per-device RW thread to recompute the sample
 * size; there is no such thread here, so nothing needs to be done. Still
 * provided because responder.c references it whenever WITH_IIOD_V0_COMPAT is
 * set, so the symbol must exist at link time.
 */
void invalidate_sample_size_cache(const struct iio_device *dev)
{
	(void)dev;
}

/* Differs from read_line() in ops.c: reimplemented with only the byte-at-a-time
 * path. ops.c also has a socket branch (recv() with MSG_PEEK/MSG_TRUNC) and a
 * USB bulk branch, both of which can read past the newline; that would corrupt
 * the ASCII->binary handoff on the Zephyr transports. Here we read one byte at
 * a time until the newline, so nothing past the line is ever consumed. */
ssize_t read_line(struct parser_pdata *pdata, char *buf, size_t len)
{
	size_t bytes_read = 0;
	bool found;

	while (len) {
		ssize_t ret = pdata->readfd(pdata, buf, 1);
		if (ret < 0) {
			/* The transport timed out or went away: end the
			 * session, as EOF does, instead of answering -EINVAL
			 * forever on a dead link. */
			pdata->stop = true;
			return ret;
		}

		bytes_read++;

		if (*buf == '\n')
			break;

		len--;
		buf++;
	}

	found = !!len;

	return found ? (ssize_t)bytes_read : -EIO;
}

/* Identical to enable_binary() in ops.c. */
void enable_binary(struct parser_pdata *pdata)
{
	pdata->binary = true;

	print_value(pdata, 0);
}

/* Differs from ascii_interpreter() in ops.c: the yylex/yyparse loop is the
 * same, but the trailing cleanup only has the single v0 stream to tear down,
 * for a client that went away without a CLOSE. */
void ascii_interpreter(struct parser_pdata *pdata)
{
	yyscan_t scanner;
	int ret;

	yylex_init_extra(pdata, &scanner);

	do {
		ret = yyparse(scanner);
	} while (!pdata->stop && !pdata->binary && ret >= 0);

	yylex_destroy(scanner);

	if (v0_stream.dev)
		v0_stream_free(&v0_stream);
}
