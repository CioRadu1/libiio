/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIOD_PARAMETERS_H
#define IIOD_PARAMETERS_H

#include "maxim_uart.h"
#include "maxim_uart_stdio.h"

/* ---------- UART ---------- */
#define UART_DEVICE_ID		0
#define UART_BAUDRATE		115200
#define UART_OPS		&max_uart_ops

static struct max_uart_init_param iiod_uart_extra = {
	.flow = MAX_UART_FLOW_DIS,
};
#define UART_EXTRA		&iiod_uart_extra

/* ---------- Network (ADIN1110) ---------- */
#ifdef NO_OS_LWIP_NETWORKING

#include "maxim_spi.h"
#include "maxim_gpio.h"

#define ADIN_SPI_DEVICE_ID	3
#define ADIN_SPI_CS		0
#define ADIN_SPI_SPEED		15000000
#define ADIN_SPI_OPS		&max_spi_ops

#define ADIN_RST_GPIO_PORT	0
#define ADIN_RST_GPIO_NUM	15
#define ADIN_GPIO_OPS		&max_gpio_ops

static struct max_spi_init_param iiod_adin_spi_extra = {
	.num_slaves = 1,
	.polarity = SPI_SS_POL_LOW,
	.vssel = MXC_GPIO_VSSEL_VDDIOH,
};
#define ADIN_SPI_EXTRA		&iiod_adin_spi_extra

static struct max_gpio_init_param iiod_adin_gpio_extra = {
	.vssel = MXC_GPIO_VSSEL_VDDIOH,
};
#define ADIN_GPIO_EXTRA		&iiod_adin_gpio_extra

#define ADIN_MAC		{ 0x00, 0x18, 0x80, 0x03, 0x25, 0x60 }

#endif /* NO_OS_LWIP_NETWORKING */

/* ---------- USB ---------- */
/* USB transport is self-contained in iiod/usb.c (Maxim-specific) */

#endif /* IIOD_PARAMETERS_H */
