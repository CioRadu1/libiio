/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef NO_OS_LWIP_NETWORKING

#include <string.h>
#include <errno.h>
#include <no_os_print_log.h>
#include <no_os_delay.h>
#include <no_os_spi.h>
#include <no_os_gpio.h>
#include <tinyiiod/tinyiiod.h>
#include "lwip_socket.h"
#include "lwip_adin1110.h"
#include "adin1110.h"
#include "tcp_socket.h"
#include "parameters.h"
#include "iio_adc.h"
#include "iio_device.h"

#define IIOD_PORT 30431
#define MAX_CLIENTS 4
#define NET_WRITE_TIMEOUT_S 5
#define NET_READ_TIMEOUT_S 3
#define POST_DISCONNECT_DELAY_MS 100

struct net_server {
	struct tcp_socket_desc *server_socket;
	struct lwip_network_desc *lwip;
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;
	unsigned int active_count;
};

static struct net_server g_server;

struct iiod_net_pdata {
	struct tcp_socket_desc *client;
	struct lwip_network_desc *lwip;
	struct net_server *server;
};

static ssize_t iiod_net_read(struct iiod_pdata *pdata, void *buf, size_t size);
static ssize_t iiod_net_write(struct iiod_pdata *pdata, const void *buf,
			      size_t size);

static void net_drain(struct lwip_network_desc *lwip)
{
	no_os_mdelay(POST_DISCONNECT_DELAY_MS);
	no_os_lwip_step(lwip, NULL);
}

static ssize_t iiod_net_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct iiod_net_pdata *np = (struct iiod_net_pdata *)pdata;
	uint8_t *dst = (uint8_t *)buf;
	struct no_os_time deadline;
	size_t total = 0;
	int32_t ret;

	deadline = no_os_get_time();
	deadline.s += NET_READ_TIMEOUT_S;

	while (total < size) {
		ret = no_os_lwip_step(np->lwip, NULL);
		if (ret)
			return -EIO;

		ret = socket_recv(np->client, dst + total,
				  (uint32_t)(size - total));

		if (ret > 0) {
			total += ret;
			deadline = no_os_get_time();
			deadline.s += NET_READ_TIMEOUT_S;
		} else if (ret == 0) {
			struct no_os_time now = no_os_get_time();

			if (now.s > deadline.s ||
			    (now.s == deadline.s && now.us >= deadline.us))
				return -ETIMEDOUT;
		} else {
			return -EIO;
		}
	}

	return (ssize_t)total;
}

static ssize_t iiod_net_write(struct iiod_pdata *pdata, const void *buf,
			      size_t size)
{
	struct iiod_net_pdata *np = (struct iiod_net_pdata *)pdata;
	const uint8_t *src = (const uint8_t *)buf;
	struct no_os_time deadline;
	size_t total = 0;
	int32_t ret;

	deadline = no_os_get_time();
	deadline.s += NET_WRITE_TIMEOUT_S;

	while (total < size) {
		ret = socket_send(np->client, src + total,
				  (uint32_t)(size - total));

		if (ret > 0) {
			total += ret;
			deadline = no_os_get_time();
			deadline.s += NET_WRITE_TIMEOUT_S;
		} else if (ret == 0) {
			struct no_os_time now = no_os_get_time();

			if (now.s > deadline.s ||
			    (now.s == deadline.s && now.us >= deadline.us))
				return -ETIMEDOUT;

			ret = no_os_lwip_step(np->lwip, NULL);
			if (ret)
				return -EIO;
		} else {
			return -EIO;
		}
	}

	no_os_lwip_step(np->lwip, NULL);

	return (ssize_t)total;
}

int iiod_network_run(struct lwip_network_desc *lwip_desc)
{
	struct iio_context_params ctx_params = {0};
	struct tcp_socket_init_param tcp_ip = { .max_buff_size = 0 };
	struct tcp_socket_desc *server_socket;
	struct tcp_socket_desc *client_socket;
	struct iiod_net_pdata np;
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

	tcp_ip.net = &lwip_desc->no_os_net;

	ret = socket_init(&server_socket, &tcp_ip);
	if (ret) {
		pr_err("socket_init failed: %d\n", ret);
		goto err_ctx;
	}

	ret = socket_bind(server_socket, IIOD_PORT);
	if (ret) {
		pr_err("socket_bind failed: %d\n", ret);
		goto err_server;
	}

	ret = socket_listen(server_socket, MAX_BACKLOG);
	if (ret) {
		pr_err("socket_listen failed: %d\n", ret);
		goto err_server;
	}

	g_server.server_socket = server_socket;
	g_server.lwip = lwip_desc;
	g_server.ctx = ctx;
	g_server.xml = xml;
	g_server.xml_len = xml_len;
	g_server.active_count = 0;

	pr_info("IIOD: listening on port %d\n", IIOD_PORT);

	while (1) {
		ret = socket_accept(server_socket, &client_socket);
		if (ret == -EAGAIN) {
			no_os_lwip_step(lwip_desc, NULL);
			continue;
		}
		if (ret) {
			pr_err("socket_accept failed: %d\n", ret);
			break;
		}

		pr_info("IIOD: client connected\n");

		np.client = client_socket;
		np.lwip = lwip_desc;
		np.server = &g_server;

		g_server.active_count++;

		ret = iiod_interpreter(ctx, (struct iiod_pdata *)&np,
				       iiod_net_read, iiod_net_write,
				       xml, xml_len);

		pr_info("IIOD: client disconnected (%d)\n", ret);

		socket_remove(client_socket);
		g_server.active_count--;

		net_drain(lwip_desc);
	}

err_server:
	socket_remove(server_socket);
err_ctx:
	iio_context_destroy(ctx);
	iiod_cleanup();
	return ret;
}

int noos_iiod_run(void)
{
	struct lwip_network_desc *lwip_desc;
	uint8_t mac[6] = ADIN_MAC;
	struct noos_iio_device_info adc_info;
	int ret;

	struct no_os_gpio_init_param adin_rst_gpio = {
		.port = ADIN_RST_GPIO_PORT,
		.number = ADIN_RST_GPIO_NUM,
		.pull = NO_OS_PULL_NONE,
		.platform_ops = ADIN_GPIO_OPS,
		.extra = ADIN_GPIO_EXTRA,
	};
	struct no_os_spi_init_param adin_spi = {
		.device_id = ADIN_SPI_DEVICE_ID,
		.max_speed_hz = ADIN_SPI_SPEED,
		.bit_order = NO_OS_SPI_BIT_ORDER_MSB_FIRST,
		.mode = NO_OS_SPI_MODE_0,
		.platform_ops = ADIN_SPI_OPS,
		.chip_select = ADIN_SPI_CS,
		.extra = ADIN_SPI_EXTRA,
	};
	struct adin1110_init_param adin_ip = {
		.chip_type = ADIN1110,
		.comm_param = adin_spi,
		.reset_param = adin_rst_gpio,
		.append_crc = true,
	};
	struct lwip_network_param lwip_param = {
		.platform_ops = &adin1110_lwip_ops,
		.mac_param = &adin_ip,
	};

	ret = iio_adc_init();
	if (ret)
		return ret;

	ret = iio_adc_get_device_info(&adc_info);
	if (ret)
		return ret;

	ret = noos_iio_register_device(&adc_info);
	if (ret)
		return ret;

	memcpy(adin_ip.mac_address, mac, 6);
	memcpy(lwip_param.hwaddr, mac, 6);

	ret = no_os_lwip_init(&lwip_desc, &lwip_param);
	if (ret) {
		pr_err("lwIP init failed: %d\n", ret);
		return ret;
	}

	ret = iiod_network_run(lwip_desc);
	no_os_lwip_remove(lwip_desc);

	return ret;
}

#endif /* NO_OS_LWIP_NETWORKING */
