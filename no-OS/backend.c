/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <iio/iio-backend.h>
#include <iio-private.h>
#include <errno.h>
#include <no_os_print_log.h>
#include <string.h>
#include <iio_device.h>

#define NOOS_BACKEND_VERSION "no-OS 1.0 " __DATE__ " " __TIME__

struct noos_iio_device_info noos_iio_devices[NOOS_IIO_MAX_DEVICES];
unsigned int noos_iio_device_count;

struct iio_buffer_pdata {
	const struct iio_device *dev;
};

struct iio_block_pdata {
	struct iio_buffer_pdata *buf;
	size_t size;
	void *data;
	size_t bytes_used;
	int error;
};

static struct iio_buffer_pdata *
noos_open_buffer(const struct iio_device *dev,
		 unsigned int idx,
		 struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata = zalloc(sizeof(*pdata));

	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->dev = dev;

	return pdata;
}

static void noos_close_buffer(struct iio_buffer_pdata *pdata)
{
	free(pdata);
}

static int noos_enable_buffer(struct iio_buffer_pdata *pdata,
			      size_t nb_samples, bool enable, bool cyclic)
{
	return 0;
}

static void noos_cancel_buffer(struct iio_buffer_pdata *pdata)
{
}

/*
 * readbuf – called by the core task worker (iio_block_io → iio_block_read)
 * when the responder enqueues a block for reading.  With NO_THREADS=1 the
 * task processes inline, so this runs synchronously within the
 * handle_transfer_block → buffer_enqueue_block → iio_block_enqueue chain.
 */
static ssize_t noos_readbuf(struct iio_buffer_pdata *pdata,
			    void *dst, size_t len)
{
	struct noos_iio_device_info *info;
	int ret;

	if (!pdata || !pdata->dev)
		return -EINVAL;

	info = (struct noos_iio_device_info *)iio_device_get_pdata(pdata->dev);
	if (!info || !info->read_samples)
		return -ENOSYS;

	ret = info->read_samples(info->dev, dst, len);
	if (ret)
		return ret;

	return (ssize_t)len;
}

static struct iio_block_pdata *
noos_create_block(struct iio_buffer_pdata *buf, size_t size, void **data)
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

static void noos_free_block(struct iio_block_pdata *pdata)
{
	free(pdata->data);
	free(pdata);
}

static int noos_enqueue_block(struct iio_block_pdata *pdata,
			      size_t bytes_used, bool cyclic)
{
	struct noos_iio_device_info *info;

	info = (struct noos_iio_device_info *)
		iio_device_get_pdata(pdata->buf->dev);
	if (!info || !info->read_samples)
		return -ENOSYS;

	pdata->bytes_used = bytes_used;
	pdata->error = info->read_samples(info->dev, pdata->data, bytes_used);
	return 0;
}

static int noos_dequeue_block(struct iio_block_pdata *pdata, bool nonblock)
{
	return pdata->error;
}

int noos_iio_register_device(const struct noos_iio_device_info *info)
{
	if (noos_iio_device_count >= NOOS_IIO_MAX_DEVICES)
		return -ENOMEM;

	noos_iio_devices[noos_iio_device_count++] = *info;
	return 0;
}

static ssize_t
noos_read_attr(const struct iio_attr *attr, char *dst, size_t len)
{
	const struct iio_device *iio_dev = iio_attr_get_device(attr);
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(iio_dev);

	/* Buffer attributes are handled internally */
	if (attr->type == IIO_ATTR_TYPE_BUFFER) {
		const char *name = iio_attr_get_name(attr);

		if (name && strcmp(name, "length") == 0)
			return snprintf(dst, len, "0") + 1;
		return -EINVAL;
	}

	if (!info || !info->read_attr)
		return -ENOSYS;

	return info->read_attr(info->dev, iio_dev, attr, dst, len);
}

static ssize_t
noos_write_attr(const struct iio_attr *attr, const char *src, size_t len)
{
	const struct iio_device *iio_dev = iio_attr_get_device(attr);
	struct noos_iio_device_info *info =
		(struct noos_iio_device_info *)iio_device_get_pdata(iio_dev);

	if (!info || !info->write_attr)
		return -ENOSYS;

	return info->write_attr(info->dev, iio_dev, attr, src, len);
}

static const struct iio_device *
noos_get_trigger(const struct iio_device *dev)
{
	return NULL;
}

static struct iio_context *
noos_create_context(const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	struct iio_device *iio_dev;
	char id[32];
	unsigned int i;

	ctx = iio_context_create_from_backend(params, &iio_external_backend,
										  NOOS_BACKEND_VERSION, 1, 0, "v1.0");
	if (iio_err(ctx))
		return iio_err_cast(ctx);

	for (i = 0; i < noos_iio_device_count; i++)
	{
		struct noos_iio_device_info *info = &noos_iio_devices[i];

		snprintf(id, sizeof(id), "iio:device%u", i);

		iio_dev = iio_context_add_device(ctx, id, info->name, NULL);
		if (!iio_dev)
			continue;

		iio_device_set_pdata(iio_dev,
							 (struct iio_device_pdata *)info);

		if (info->add_channels)
			info->add_channels(info->dev, iio_dev);

		{
			struct iio_buffer *buf;

			buf = iio_device_add_buffer(iio_dev, 0);
			if (buf)
				iio_buffer_add_attr(buf, "length");
		}
	}

	return ctx;
}

static const struct iio_backend_ops noos_ops = {
	.create = noos_create_context,
	.read_attr = noos_read_attr,
	.write_attr = noos_write_attr,
	.get_trigger = noos_get_trigger,

	.open_buffer = noos_open_buffer,
	.close_buffer = noos_close_buffer,
	.enable_buffer = noos_enable_buffer,
	.cancel_buffer = noos_cancel_buffer,

	.readbuf = noos_readbuf,

	.create_block = noos_create_block,
	.free_block = noos_free_block,
	.enqueue_block = noos_enqueue_block,
	.dequeue_block = noos_dequeue_block,
};

const struct iio_backend iio_external_backend = {
	.name = "no-os",
	.api_version = IIO_BACKEND_API_V1,
	.default_timeout_ms = 0,
	.uri_prefix = "no-os:",
	.ops = &noos_ops,
};
