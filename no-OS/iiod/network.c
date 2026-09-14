/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef NO_OS_LWIP_NETWORKING

#include <string.h>
#include <errno.h>
#include <no_os_uart.h>
#include <no_os_print_log.h>
#include <no_os_alloc.h>
#include <no_os_delay.h>
#include <tinyiiod/tinyiiod.h>
#include "lwip_socket.h"
#include "tcp_socket.h"
#include "parameters.h"
#include "iio_adc.h"
#include "iio_device.h"

#define IIOD_PORT 30431
#define MAX_CLIENTS 8
#define NET_WRITE_TIMEOUT_S 5
#define NET_READ_TIMEOUT_S 3
#define NET_DRAIN_STEPS 16

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

struct net_client {
	struct tcp_socket_desc *sock;
	struct iiod_net_pdata np;
	struct iiod_interp *interp;
	bool used;
};

static struct net_client g_clients[MAX_CLIENTS];

static ssize_t iiod_net_read(struct iiod_pdata *pdata, void *buf, size_t size);
static ssize_t iiod_net_write(struct iiod_pdata *pdata, const void *buf,
			      size_t size);

static bool net_sock_live(const struct tcp_socket_desc *sock)
{
	if (!sock || sock->id >= NO_OS_MAX_SOCKETS)
		return false;

	return g_server.lwip->sockets[sock->id].state == SOCKET_CONNECTED;
}

static void net_drain(struct lwip_network_desc *lwip)
{
	unsigned int i;

	for (i = 0; i < NET_DRAIN_STEPS; i++)
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
			struct no_os_time now;

			if (!total)
				return -EAGAIN;

			now = no_os_get_time();

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

static int net_client_add(struct tcp_socket_desc *sock)
{
	struct net_client *cl = NULL;
	unsigned int i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (!g_clients[i].used) {
			cl = &g_clients[i];
			break;
		}
	}

	if (!cl)
		return -ENOSPC;

	cl->sock = sock;
	cl->np.client = sock;
	cl->np.lwip = g_server.lwip;
	cl->np.server = &g_server;

	cl->interp = iiod_interpreter_create(g_server.ctx,
					     (struct iiod_pdata *)&cl->np,
					     iiod_net_read, iiod_net_write,
					     g_server.xml, g_server.xml_len);
	if (!cl->interp)
		return -ENOMEM;

	cl->used = true;
	g_server.active_count++;

	pr_info("IIOD: client %u connected (%u active)\n", i,
		g_server.active_count);

	return 0;
}

static void net_client_del(unsigned int slot, int reason)
{
	struct net_client *cl = &g_clients[slot];

	iiod_interpreter_destroy(cl->interp);

	if (net_sock_live(cl->sock))
		socket_remove(cl->sock);
	else
		no_os_free(cl->sock);

	cl->interp = NULL;
	cl->sock = NULL;
	cl->used = false;
	g_server.active_count--;

	pr_info("IIOD: client %u disconnected (%d, %u active)\n", slot, reason,
		g_server.active_count);

	net_drain(g_server.lwip);
}

int iiod_network_run(struct lwip_network_desc *lwip_desc)
{
	struct iio_context_params ctx_params = {0};
	struct tcp_socket_init_param tcp_ip = { .max_buff_size = 0 };
	struct tcp_socket_desc *server_socket;
	struct tcp_socket_desc *client_socket;
	struct iio_context *ctx;
	unsigned int i;
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
		no_os_lwip_step(lwip_desc, NULL);

		for (i = 0; i < MAX_CLIENTS; i++) {
			if (!g_clients[i].used)
				continue;

			ret = iiod_interpreter_step(g_clients[i].interp);
			if (ret < 0)
				net_client_del(i, ret);
		}

		ret = socket_accept(server_socket, &client_socket);
		if (!ret) {
			ret = net_client_add(client_socket);
			if (ret) {
				pr_err("IIOD: client refused: %d\n", ret);
				socket_remove(client_socket);
				net_drain(lwip_desc);
			}
		} else if (ret != -EAGAIN) {
			pr_err("socket_accept failed: %d\n", ret);
			break;
		}
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
	struct no_os_uart_desc *console;
	struct no_os_uart_init_param console_ip = {
		.device_id = UART_DEVICE_ID,
		.baud_rate = UART_BAUDRATE,
		.size = NO_OS_UART_CS_8,
		.parity = NO_OS_UART_PAR_NO,
		.stop = NO_OS_UART_STOP_1_BIT,
		.platform_ops = UART_OPS,
		.extra = UART_EXTRA,
	};
	struct noos_net_config cfg = {
		.lwip_ops = NET_LWIP_OPS,
		.mac_param = NET_MAC_PARAM,
		.mac_addr = NET_MAC_ADDR,
	};
	struct lwip_network_param lwip_param = {
		.platform_ops = (const struct no_os_lwip_ops *)cfg.lwip_ops,
		.mac_param = cfg.mac_param,
	};
	struct noos_iio_device_info adc_info;
	int ret;

	ret = no_os_uart_init(&console, &console_ip);
	if (ret)
		return ret;

	no_os_uart_stdio(console);

	ret = iio_adc_init();
	if (ret)
		goto err_console;

	ret = iio_adc_get_device_info(&adc_info);
	if (ret)
		goto err_console;

	ret = noos_iio_register_device(&adc_info);
	if (ret)
		goto err_console;

	memcpy(lwip_param.hwaddr, cfg.mac_addr, 6);

	ret = no_os_lwip_init(&lwip_desc, &lwip_param);
	if (ret) {
		pr_err("lwIP init failed: %d\n", ret);
		goto err_console;
	}

	ret = iiod_network_run(lwip_desc);
	no_os_lwip_remove(lwip_desc);

err_console:
	no_os_uart_remove(console);

	return ret;
}

#endif /* NO_OS_LWIP_NETWORKING */
