/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIOD_PARAMETERS_H
#define IIOD_PARAMETERS_H

/* ---------- UART ---------- */
#if !defined(NO_OS_USB_TRANSPORT) && !defined(NO_OS_LWIP_NETWORKING)

#include "maxim_uart.h"
#include "maxim_uart_stdio.h"

#define UART_DEVICE_ID		0
#define UART_BAUDRATE		115200
#define UART_OPS		&max_uart_ops

static struct max_uart_init_param iiod_uart_extra = {
	.flow = MAX_UART_FLOW_DIS,
};
#define UART_EXTRA		&iiod_uart_extra

#endif /* UART transport */

/* ---------- Network (chip config only lives here) ---------- */
#ifdef NO_OS_LWIP_NETWORKING

#include "maxim_spi.h"
#include "maxim_gpio.h"
#include "lwip_adin1110.h"
#include "adin1110.h"

static struct max_spi_init_param iiod_adin_spi_extra = {
	.num_slaves = 1,
	.polarity = SPI_SS_POL_LOW,
	.vssel = MXC_GPIO_VSSEL_VDDIOH,
};

static struct max_gpio_init_param iiod_adin_gpio_extra = {
	.vssel = MXC_GPIO_VSSEL_VDDIOH,
};

#define NET_MAC_ADDR	{ 0x00, 0x18, 0x80, 0x03, 0x25, 0x60 }

static struct adin1110_init_param iiod_adin_ip = {
	.chip_type = ADIN1110,
	.comm_param = {
		.device_id = 3,
		.max_speed_hz = 15000000,
		.bit_order = NO_OS_SPI_BIT_ORDER_MSB_FIRST,
		.mode = NO_OS_SPI_MODE_0,
		.platform_ops = &max_spi_ops,
		.chip_select = 0,
		.extra = &iiod_adin_spi_extra,
	},
	.reset_param = {
		.port = 0,
		.number = 15,
		.pull = NO_OS_PULL_NONE,
		.platform_ops = &max_gpio_ops,
		.extra = &iiod_adin_gpio_extra,
	},
	.mac_address = NET_MAC_ADDR,
	.append_crc = true,
};

#define NET_LWIP_OPS	(&adin1110_lwip_ops)
#define NET_MAC_PARAM	(&iiod_adin_ip)

#endif /* NO_OS_LWIP_NETWORKING */

/* ---------- USB ---------- */
/* USB transport is self-contained in iiod/usb.c (Maxim-specific) */

#endif /* IIOD_PARAMETERS_H */
