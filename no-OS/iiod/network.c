/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef NO_OS_LWIP_NETWORKING

#include <string.h>
#include <errno.h>
#include <no_os_print_log.h>
#include <tinyiiod/tinyiiod.h>
#include "lwip_socket.h"
#include "tcp_socket.h"

#define IIOD_PORT 30431
#define MAX_CLIENTS 4

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

static void net_accept_new(struct net_server *srv)
{
	struct tcp_socket_desc *new_client;
	struct iiod_net_pdata np;
	int ret;

	if (srv->active_count >= MAX_CLIENTS)
		return;

	ret = socket_accept(srv->server_socket, &new_client);
	if (ret)
		return;

	pr_info("IIOD: client %u connected\n", srv->active_count + 1);

	np.client = new_client;
	np.lwip = srv->lwip;
	np.server = srv;

	srv->active_count++;

	ret = iiod_interpreter(srv->ctx, (struct iiod_pdata *)&np,
			       iiod_net_read, iiod_net_write,
			       srv->xml, srv->xml_len);

	pr_info("IIOD: client disconnected (%d), %u remaining\n",
		ret, srv->active_count - 1);
	socket_remove(new_client);
	srv->active_count--;
}

static ssize_t iiod_net_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	struct iiod_net_pdata *np = (struct iiod_net_pdata *)pdata;
	uint8_t *dst = (uint8_t *)buf;
	size_t total = 0;
	int32_t ret;

	while (total < size) {
		no_os_lwip_step(np->lwip, NULL);

		ret = socket_recv(np->client, dst + total,
				  (uint32_t)(size - total));
		if (ret > 0) {
			total += ret;
		} else if (ret == 0) {
			/* No data yet — accept new connections while waiting */
			if (np->server)
				net_accept_new(np->server);
		} else {
			return -1;
		}
	}

	return (ssize_t)total;
}

static ssize_t iiod_net_write(struct iiod_pdata *pdata, const void *buf,
			      size_t size)
{
	struct iiod_net_pdata *np = (struct iiod_net_pdata *)pdata;
	const uint8_t *src = (const uint8_t *)buf;
	size_t total = 0;
	int32_t ret;

	while (total < size) {
		ret = socket_send(np->client, src + total,
				  (uint32_t)(size - total));
		if (ret > 0) {
			total += ret;
		} else if (ret == 0) {
			no_os_lwip_step(np->lwip, NULL);
		} else {
			return -1;
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

	pr_info("IIOD: listening on port %d (xml %zu bytes)\n",
		IIOD_PORT, xml_len);

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
	}

err_server:
	socket_remove(server_socket);
err_ctx:
	iio_context_destroy(ctx);
	iiod_cleanup();
	return ret;
}

#endif /* NO_OS_LWIP_NETWORKING */
