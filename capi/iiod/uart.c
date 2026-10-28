/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <capi_uart.h>
#include <errno.h>
#include <iiod_uart.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <tinyiiod/tinyiiod.h>

/*
 * As zephyr/iiod/uart.c: received bytes go from the UART interrupt into a
 * ring buffer, so they are kept while the interpreter is busy (sending a
 * block, filling one from a driver) instead of overrunning the UART's own
 * FIFO. A platform whose capi_uart has no interrupts (register_callback or
 * set_irq_rx fails) is served with blocking capi_uart_receive() instead.
 * Transmit is blocking either way.
 */

/* A power of two; the ring is single producer (the interrupt), single
 * consumer (the interpreter). */
#ifndef IIOD_UART_RX_BUF_SIZE
#define IIOD_UART_RX_BUF_SIZE 256
#endif

static uint8_t rx_buf[IIOD_UART_RX_BUF_SIZE];
static volatile uint32_t rx_head, rx_tail;
static volatile uint32_t rx_dropped;
static bool rx_irq;

static void iiod_uart_irq(enum capi_uart_async_event event, void *arg, int event_extra)
{
	struct capi_uart_handle *uart = arg;
	uint32_t head;
	uint8_t byte;

	(void)event_extra;

	if (event != CAPI_UART_EVENT_INTERRUPT)
		return;

	while (capi_uart_read_byte(uart, &byte)) {
		head = rx_head;

		if (head - rx_tail == IIOD_UART_RX_BUF_SIZE) {
			/* Full: drop it, as zephyr does. */
			rx_dropped++;
			continue;
		}

		rx_buf[head % IIOD_UART_RX_BUF_SIZE] = byte;
		rx_head = head + 1;
	}
}

static ssize_t iiod_uart_read_ring(void *buf, size_t size)
{
	uint8_t *dst = buf;
	uint32_t tail = rx_tail;
	size_t len = 0;

	while (rx_head == tail)
		;

	while (len < size && rx_head != tail) {
		dst[len++] = rx_buf[tail % IIOD_UART_RX_BUF_SIZE];
		tail++;
	}

	rx_tail = tail;

	return len;
}

static ssize_t iiod_uart_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct capi_uart_handle *uart = (struct capi_uart_handle *)pdata;
	int ret;

	if (rx_irq)
		return iiod_uart_read_ring(buf, size);

	ret = capi_uart_receive(uart, buf, size);
	if (ret)
		return ret;

	return size;
}

static ssize_t iiod_uart_write(struct iiod_pdata *pdata, const void *buf, size_t size)
{
	struct capi_uart_handle *uart = (struct capi_uart_handle *)pdata;
	int ret;

	/* capi_uart_transmit() does not take a const buffer. */
	ret = capi_uart_transmit(uart, (uint8_t *)buf, size);
	if (ret)
		return ret;

	return size;
}

/* Drop whatever was received so far, then (re)start receiving. */
static void iiod_uart_rx_start(struct capi_uart_handle *uart)
{
	if (rx_irq)
		capi_uart_set_irq_rx(uart, false);

	/* Bytes left from before (or from reset) would offset every command
	 * header that follows. */
	capi_uart_flush_rx_fifo(uart);
	rx_head = 0;
	rx_tail = 0;

	if (rx_irq && capi_uart_set_irq_rx(uart, true))
		rx_irq = false;
}

int iiod_uart_run(struct capi_uart_handle *uart)
{
	struct iio_context_params ctx_params = { 0 };
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;
	int ret;

	if (!uart)
		return -EINVAL;

	ret = iiod_init();
	if (ret < 0)
		return ret;

	ctx = iio_create_context(&ctx_params, "capi:");
	if (iio_err(ctx)) {
		ret = iio_err(ctx);
		goto err_cleanup;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		ret = -ENOMEM;
		iio_context_destroy(ctx);
		goto err_cleanup;
	}

	xml_len = strlen(xml) + 1;

	rx_irq = !capi_uart_register_callback(uart, iiod_uart_irq, uart);

	/*
	 * A client that goes away and a new one that connects are handled
	 * inside the interpreter (the new client's "BINARY\r\n"). It only
	 * returns when a UART read or write fails: start over then, so a
	 * line error does not end serving.
	 */
	for (;;) {
		iiod_uart_rx_start(uart);

		iiod_interpreter(ctx, (struct iiod_pdata *)uart, iiod_uart_read, iiod_uart_write,
				xml, xml_len);
	}

err_cleanup:
	iiod_cleanup();

	return ret;
}
