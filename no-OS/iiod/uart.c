/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <errno.h>
#include <no_os_uart.h>
#include <no_os_print_log.h>
#include <tinyiiod/tinyiiod.h>

static ssize_t iiod_uart_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct no_os_uart_desc *uart = (struct no_os_uart_desc *)pdata;
	int32_t ret;

	ret = no_os_uart_read(uart, (uint8_t *)buf, (uint32_t)size);
	if (ret < 0)
		return ret;

	return (ssize_t)size;
}

static ssize_t iiod_uart_write(struct iiod_pdata *pdata, const void *buf,
			       size_t size)
{
	struct no_os_uart_desc *uart = (struct no_os_uart_desc *)pdata;
	int32_t ret;

	ret = no_os_uart_write(uart, (const uint8_t *)buf, (uint32_t)size);
	if (ret < 0)
		return ret;

	return (ssize_t)size;
}


int iiod_uart_run(struct no_os_uart_desc *uart_desc)
{
	struct iio_context_params ctx_params = {0};
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;
	int ret;

	ret = iiod_init();
	if (ret < 0) {
		pr_err("iiod_init failed: %d\n", ret);
		return ret;
	}

	ctx = iio_create_context(&ctx_params, "no-os:");
	if (iio_err(ctx)) {
		pr_err("iio_create_context failed\n");
		iiod_cleanup();
		return -1;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		pr_err("iio_context_get_xml failed\n");
		iio_context_destroy(ctx);
		iiod_cleanup();
		return -1;
	}

	xml_len = strlen(xml) + 1;

	pr_info("IIOD: starting interpreter (xml %zu bytes)\n", xml_len);

	ret = iiod_interpreter(ctx, (struct iiod_pdata *)uart_desc,
			       iiod_uart_read, iiod_uart_write,
			       xml, xml_len);

	iio_context_destroy(ctx);
	iiod_cleanup();

	return ret;
}
