/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <iio-private.h>
#include <iio/iio-backend.h>
#include <iio_device.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__DATE__) && defined(__TIME__)
#define BACKEND_VERSION(_ver) _ver " " __DATE__ " " __TIME__
#else
#define BACKEND_VERSION(_ver) _ver
#endif

static struct iio_capi_device *capi_devices;

struct iio_buffer_pdata {
	const struct iio_device *dev;
	/* Owned by the buffer stream, valid until the buffer is closed. */
	const struct iio_channels_mask *mask;
	bool enabled;
};

struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	size_t size;
	void *data;
	size_t bytes_used;
	int error;
};

static const struct iio_capi_device *capi_device_of(const struct iio_device *iio_device)
{
	return (const struct iio_capi_device *)iio_device_get_pdata(iio_device);
}

/* An output buffer for a device that can only take samples. */
static bool capi_device_is_output(const struct iio_capi_device *dev)
{
	return dev->api->write_samples && !dev->api->read_samples;
}

int iio_capi_device_register(struct iio_capi_device *dev)
{
	struct iio_capi_device **tail = &capi_devices;

	if (!dev || !dev->name || !dev->api || !dev->api->add_channels)
		return -EINVAL;

	/* Keep registration order: it gives the iio:deviceN numbering. */
	while (*tail) {
		if (*tail == dev)
			return -EEXIST;
		tail = &(*tail)->next;
	}

	dev->next = NULL;
	*tail = dev;

	return 0;
}

static ssize_t capi_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *iio_device = iio_attr_get_device(attr);
	const struct iio_capi_device *dev =
			(const struct iio_capi_device *)iio_device_get_pdata(iio_device);

	if (!dev->api->read_attr)
		return -ENOSYS;

	return dev->api->read_attr(dev, iio_device, attr, dst, len);
}

static ssize_t capi_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *iio_device = iio_attr_get_device(attr);
	const struct iio_capi_device *dev =
			(const struct iio_capi_device *)iio_device_get_pdata(iio_device);

	if (!dev->api->write_attr)
		return -ENOSYS;

	return dev->api->write_attr(dev, iio_device, attr, src, len);
}

/*
 * The buffer and block ops below are no-OS/backend.c's: libiio opens a
 * buffer, then streams through blocks, and every block transfer is done
 * synchronously by the device's read_samples/write_samples.
 */

static struct iio_buffer_pdata *capi_open_buffer(
		const struct iio_device *dev, unsigned int idx, struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata = zalloc(sizeof(*pdata));

	(void)idx;

	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->dev = dev;
	pdata->mask = mask;

	return pdata;
}

/* Calls the device's enable_buffer only on a change. */
static int capi_enable_buffer(
		struct iio_buffer_pdata *pdata, size_t nb_samples, bool enable, bool cyclic)
{
	const struct iio_capi_device *dev = capi_device_of(pdata->dev);
	int ret;

	(void)nb_samples;
	(void)cyclic;

	if (dev->api->enable_buffer && pdata->enabled != enable) {
		ret = dev->api->enable_buffer(dev, enable);
		if (ret)
			return ret;
	}

	pdata->enabled = enable;

	return 0;
}

static void capi_close_buffer(struct iio_buffer_pdata *pdata)
{
	/* A client that drops the connection never disables the buffer. */
	if (pdata->enabled)
		capi_enable_buffer(pdata, 0, false, false);

	free(pdata);
}

/* Nothing to cancel: every transfer here finishes before the call returns. */
static void capi_cancel_buffer(struct iio_buffer_pdata *pdata)
{
	(void)pdata;
}

static ssize_t capi_readbuf(struct iio_buffer_pdata *pdata, void *dst, size_t len)
{
	const struct iio_capi_device *dev = capi_device_of(pdata->dev);
	int ret;

	if (!dev->api->read_samples)
		return -ENOSYS;

	ret = dev->api->read_samples(dev, pdata->dev, pdata->mask, dst, len);
	if (ret)
		return ret;

	return (ssize_t)len;
}

static ssize_t capi_writebuf(struct iio_buffer_pdata *pdata, const void *src, size_t len)
{
	const struct iio_capi_device *dev = capi_device_of(pdata->dev);
	int ret;

	if (!dev->api->write_samples)
		return -ENOSYS;

	ret = dev->api->write_samples(dev, pdata->dev, pdata->mask, src, len);
	if (ret)
		return ret;

	return (ssize_t)len;
}

static struct iio_block_pdata *capi_create_block(
		struct iio_buffer_pdata *buf, size_t size, void **data)
{
	struct iio_block_pdata *pdata = zalloc(sizeof(*pdata));

	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->data = malloc(size);
	if (!pdata->data) {
		free(pdata);
		return iio_ptr(-ENOMEM);
	}

	pdata->buf = buf;
	pdata->size = size;
	*data = pdata->data;

	return pdata;
}

static void capi_free_block(struct iio_block_pdata *pdata)
{
	free(pdata->data);
	free(pdata);
}

/* Do the transfer now; the result is kept for the dequeue. */
static int capi_enqueue_block(struct iio_block_pdata *pdata, size_t bytes_used, bool cyclic)
{
	struct iio_buffer_pdata *buf = pdata->buf;
	const struct iio_capi_device *dev = capi_device_of(buf->dev);

	(void)cyclic;

	pdata->bytes_used = bytes_used;

	if (capi_device_is_output(dev))
		pdata->error = dev->api->write_samples(
				dev, buf->dev, buf->mask, pdata->data, bytes_used);
	else if (dev->api->read_samples)
		pdata->error = dev->api->read_samples(
				dev, buf->dev, buf->mask, pdata->data, bytes_used);
	else
		return -ENOSYS;

	return 0;
}

/* The block was done at enqueue time, so just report how it went. */
static int capi_dequeue_block(struct iio_block_pdata *pdata, bool nonblock)
{
	(void)nonblock;

	return pdata->error;
}

/* An input or output buffer with every scan element, as no-OS does. */
static int capi_add_buffer(const struct iio_capi_device *dev, struct iio_device *iio_device)
{
	struct iio_buffer *buf;
	unsigned int i, nb_channels;
	int ret;

	if (!dev->api->read_samples && !dev->api->write_samples)
		return 0;

	buf = iio_device_add_buffer(iio_device, 0);
	if (!buf)
		return -ENOMEM;

	iio_buffer_set_direction(buf, capi_device_is_output(dev) ? "out" : "in");

	nb_channels = iio_device_get_channels_count(iio_device);
	for (i = 0; i < nb_channels; i++) {
		struct iio_channel *chn = iio_device_get_channel(iio_device, i);

		if (!iio_channel_is_scan_element(chn))
			continue;

		ret = iio_buffer_add_scan_element(buf, chn, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

/* Debug register access (iio_reg), forwarded to the device, as no-OS does. */
static int capi_reg_read(const struct iio_device *iio_device, uint32_t address, uint32_t *value)
{
	const struct iio_capi_device *dev = capi_device_of(iio_device);

	if (!dev->api->reg_read)
		return -ENOSYS;

	return dev->api->reg_read(dev, address, value);
}

static int capi_reg_write(const struct iio_device *iio_device, uint32_t address, uint32_t value)
{
	const struct iio_capi_device *dev = capi_device_of(iio_device);

	if (!dev->api->reg_write)
		return -ENOSYS;

	return dev->api->reg_write(dev, address, value);
}

static const struct iio_device *capi_get_trigger(const struct iio_device *dev)
{
	(void)dev;

	return NULL;
}

static struct iio_context *capi_create_context(
		const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	struct iio_capi_device *dev;
	struct iio_device *iio_device;
	unsigned int i = 0;
	char id[32];
	int ret;

	const char *description = "CAPI " BACKEND_VERSION(LIBIIO_VERSION_GIT);

	(void)args;

	ctx = iio_context_create_from_backend(params, &iio_external_backend, description,
			LIBIIO_VERSION_MAJOR, LIBIIO_VERSION_MINOR, LIBIIO_VERSION_PATCH,
			LIBIIO_VERSION_GIT);
	if (iio_err(ctx)) {
		return iio_err_cast(ctx);
	}

	for (dev = capi_devices; dev; dev = dev->next) {
		snprintf(id, sizeof(id), "iio:device%u", i);

		/* Returns NULL, not an error pointer, when it fails. */
		iio_device = iio_context_add_device(ctx, id, dev->name, NULL);
		if (!iio_device) {
			ret = -ENOMEM;
			goto err_destroy;
		}

		iio_device_set_pdata(iio_device, (struct iio_device_pdata *)dev);

		ret = dev->api->add_channels(dev, iio_device);
		if (ret < 0)
			goto err_destroy;

		ret = capi_add_buffer(dev, iio_device);
		if (ret < 0)
			goto err_destroy;

		i++;
	}

	return ctx;

err_destroy:
	iio_context_destroy(ctx);
	return iio_ptr(ret);
}

static const struct iio_backend_ops capi_ops = {
	.create = capi_create_context,
	.read_attr = capi_read_attr,
	.write_attr = capi_write_attr,
	.get_trigger = capi_get_trigger,

	.open_buffer = capi_open_buffer,
	.close_buffer = capi_close_buffer,
	.enable_buffer = capi_enable_buffer,
	.cancel_buffer = capi_cancel_buffer,

	.readbuf = capi_readbuf,
	.writebuf = capi_writebuf,

	.create_block = capi_create_block,
	.free_block = capi_free_block,
	.enqueue_block = capi_enqueue_block,
	.dequeue_block = capi_dequeue_block,

	.reg_read = capi_reg_read,
	.reg_write = capi_reg_write,
};

const struct iio_backend iio_external_backend = {
	.name = "capi",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 5000,
	.uri_prefix = "capi:",
	.ops = &capi_ops,
};
