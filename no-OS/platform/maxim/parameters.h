/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IIOD_PARAMETERS_H
#define IIOD_PARAMETERS_H

/* ---------- UART ---------- */
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"

#define UART_DEVICE_ID		0
#define UART_BAUDRATE		115200
#define UART_OPS		&max_uart_ops

static struct max_uart_init_param iiod_uart_extra = {
	.flow = MAX_UART_FLOW_DIS,
};
#define UART_EXTRA		&iiod_uart_extra

/*
 * ---------- Timers: TMR1 = sampling trigger, TMR0 = USB tick ----------
 * (iio_demo: TMR1 = adc_demo's trigger, TMR2 = dac_demo's)
 * Same priority for both: no-OS walks every timer callback with one shared
 * iterator, so one timer ISR must not preempt the other half way through.
 */
#include "maxim_timer.h"
#include "maxim_irq.h"

#define TRIGGER_TIMER_ID	1
#define TRIGGER_TIMER_FREQ_HZ	1000000
#define TRIGGER_IRQ_ID		TMR1_IRQn
#define TRIGGER_IRQ_HANDLE	MXC_TMR1

#define DEMO_DAC_TIMER_ID	2
#define DEMO_DAC_IRQ_ID		TMR2_IRQn
#define DEMO_DAC_IRQ_HANDLE	MXC_TMR2
#define DEMO_TIMER_TICKS	2000	/* 500 Hz, as no-OS's iio_demo */

#define USB_TICK_TIMER_ID	0
#define USB_TICK_TIMER_FREQ_HZ	1000000
#define USB_TICK_TIMER_TICKS	100	/* 10 kHz */
#define USB_TICK_IRQ_ID		TMR0_IRQn
#define USB_TICK_IRQ_HANDLE	MXC_TMR0

#define TIMER_OPS		&max_timer_ops
#define IRQ_OPS			&max_irq_ops
#define TIMER_IRQ_PRIORITY	3

/* ---------- USB identity: libiio's ids; the host finds the "IIO" interface ---------- */
#define USB_VENDOR_ID		0x0456
#define USB_PRODUCT_ID		0xB673
#define USB_MANUFACTURER	"Analog Devices"
#define USB_SERIAL		"000000001"

/* ---------- Network (board wiring; chip binding in NETDEV_HEADER) ---------- */
#ifdef NO_OS_LWIP_NETWORKING

#include "maxim_spi.h"
#include "maxim_gpio.h"

static struct max_spi_init_param iiod_adin_spi_extra = {
	.num_slaves = 1,
	.polarity = SPI_SS_POL_LOW,
	.vssel = MXC_GPIO_VSSEL_VDDIOH,
};

static struct max_gpio_init_param iiod_adin_gpio_extra = {
	.vssel = MXC_GPIO_VSSEL_VDDIOH,
};

#define NET_MAC_ADDR	{ 0x00, 0x18, 0x80, 0x03, 0x25, 0x60 }

#include NETDEV_HEADER

#endif /* NO_OS_LWIP_NETWORKING */
#endif /* IIOD_PARAMETERS_H */
