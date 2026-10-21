/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * What the sample serves and how, as data only: the counterpart of a Zephyr
 * application's overlay, and of a no-OS project's common_data.c. The
 * platform values come from its parameters.h.
 *
 * Default: the part's own ADC (where the platform has one) and the timer0
 * trigger. With IIOD_IIO_DEMO:
 * no-OS's iio_demo project, its unmodified adc_demo and dac_demo drivers
 * with the same init parameters, served by libiio instead of iio_app.
 */

#include <no_os_uart.h>
#include <no_os_timer.h>
#include <no_os_irq.h>
#include <no_os_util.h>
#include <noos_iiod.h>
#include "parameters.h"

static struct no_os_irq_init_param timer_irq_ip = {
	.irq_ctrl_id = 0,
	.platform_ops = IRQ_OPS,
};

#ifdef IIOD_IIO_DEMO
#include <iio_noos_driver.h>
#include "iio_trigger.h"
#include "iio_adc_demo.h"
#include "iio_dac_demo.h"

/* As in no-OS's iio_demo: what the DAC is given, the ADC reads back. */
#define DEMO_CHANNELS		no_os_max(TOTAL_ADC_CHANNELS, TOTAL_DAC_CHANNELS)
#define SAMPLES_PER_CHANNEL	2000

static uint16_t loopback_buffs[DEMO_CHANNELS][SAMPLES_PER_CHANNEL];

static struct adc_demo_init_param adc_init_par = {
	.ext_buff_len = SAMPLES_PER_CHANNEL,
	.ext_buff = (uint16_t **)loopback_buffs,
	.dev_global_attr = 3333,
	.dev_ch_attr = {
		1111, 1112, 1113, 1114, 1115, 1116, 1117, 1118,
		1119, 1120, 1121, 1122, 1123, 1124, 1125, 1126
	}
};

static struct dac_demo_init_param dac_init_par = {
	.loopback_buffer_len = SAMPLES_PER_CHANNEL,
	.loopback_buffers = (uint16_t **)loopback_buffs,
	.dev_global_attr = 4444,
	.dev_ch_attr = {
		1111, 1112, 1113, 1114, 1115, 1116, 1117, 1118,
		1119, 1120, 1121, 1122, 1123, 1124, 1125, 1126
	}
};

/*
 * As iio_demo's timer-trigger example: each device has its own timer
 * trigger, from its driver's *_trig.c; the software triggers from the same
 * files can be picked instead (iio_rwdev -t adc_demo_sw_trig ...).
 */
static struct no_os_timer_init_param adc_timer_ip = {
	.id = TRIGGER_TIMER_ID,
	.freq_hz = TRIGGER_TIMER_FREQ_HZ,
	.ticks_count = DEMO_TIMER_TICKS,
	.platform_ops = TIMER_OPS,
};

static struct no_os_timer_init_param dac_timer_ip = {
	.id = DEMO_DAC_TIMER_ID,
	.freq_hz = TRIGGER_TIMER_FREQ_HZ,
	.ticks_count = DEMO_TIMER_TICKS,
	.platform_ops = TIMER_OPS,
};

static struct iio_hw_trig_init_param adc_trig_ip = {
	.irq_id = TRIGGER_IRQ_ID,
	.cb_info = {
		.event = NO_OS_EVT_TIM_ELAPSED,
		.peripheral = NO_OS_TIM_IRQ,
		.handle = TRIGGER_IRQ_HANDLE,
	},
};

static struct iio_hw_trig_init_param dac_trig_ip = {
	.irq_id = DEMO_DAC_IRQ_ID,
	.cb_info = {
		.event = NO_OS_EVT_TIM_ELAPSED,
		.peripheral = NO_OS_TIM_IRQ,
		.handle = DEMO_DAC_IRQ_HANDLE,
	},
};

NOOS_IIOD_TRIGGER_DEFINE(adc_timer_trig, "adc_demo_timer_trig",
			 &adc_iio_timer_trig_desc, &adc_trig_ip, &timer_irq_ip,
			 &adc_timer_ip, TIMER_IRQ_PRIORITY);
NOOS_IIOD_TRIGGER_DEFINE(dac_timer_trig, "dac_demo_timer_trig",
			 &dac_iio_timer_trig_desc, &dac_trig_ip, &timer_irq_ip,
			 &dac_timer_ip, TIMER_IRQ_PRIORITY);
NOOS_IIOD_TRIGGER_DEFINE(adc_sw_trig, "adc_demo_sw_trig",
			 &adc_iio_sw_trig_desc, NULL, NULL, NULL, 0);
NOOS_IIOD_TRIGGER_DEFINE(dac_sw_trig, "dac_demo_sw_trig",
			 &dac_iio_sw_trig_desc, NULL, NULL, NULL, 0);

NOOS_IIOD_DRIVER_DEFINE(adc_demo, "adc_demo", adc_demo_init,
			struct adc_demo_desc, &adc_init_par,
			&adc_demo_iio_descriptor, "adc_demo_timer_trig");
NOOS_IIOD_DRIVER_DEFINE(dac_demo, "dac_demo", dac_demo_init,
			struct dac_demo_desc, &dac_init_par,
			&dac_demo_iio_descriptor, "dac_demo_timer_trig");

static const struct noos_iiod_device iiod_devices[] = {
	NOOS_IIOD_DRIVER(adc_demo),
	NOOS_IIOD_DRIVER(dac_demo),
	NOOS_IIOD_TRIGGER(adc_timer_trig),
	NOOS_IIOD_TRIGGER(dac_timer_trig),
	NOOS_IIOD_TRIGGER(adc_sw_trig),
	NOOS_IIOD_TRIGGER(dac_sw_trig),
};
#else
#include "iio_trigger_timer.h"

static struct no_os_timer_init_param trigger_timer_ip = {
	.id = TRIGGER_TIMER_ID,
	.freq_hz = TRIGGER_TIMER_FREQ_HZ,
	.platform_ops = TIMER_OPS,
};

/* The timer0 trigger, which paces the on-chip ADC's buffer. */
static const struct noos_iiod_timer trigger_timer = {
	.timer = &trigger_timer_ip,
	.irq = &timer_irq_ip,
	.irq_id = TRIGGER_IRQ_ID,
	.irq_handle = TRIGGER_IRQ_HANDLE,
	.irq_priority = TIMER_IRQ_PRIORITY,
};

static const struct noos_iiod_device iiod_devices[] = {
#ifdef IIOD_HAS_ONCHIP_ADC
	NOOS_IIOD_ONCHIP_ADC(IIO_TRIGGER_TIMER_NAME),
#endif
	NOOS_IIOD_TIMER_TRIGGER(&trigger_timer),
};
#endif /* IIOD_IIO_DEMO */

static struct no_os_uart_init_param iiod_uart_ip = {
	.device_id = UART_DEVICE_ID,
	.baud_rate = UART_BAUDRATE,
	.size = NO_OS_UART_CS_8,
	.parity = NO_OS_UART_PAR_NO,
	.stop = NO_OS_UART_STOP_1_BIT,
	.platform_ops = UART_OPS,
	.extra = UART_EXTRA,
};

#ifdef IIOD_TRANSPORT_NETWORK
#include "lwip_socket.h"

static struct lwip_network_param iiod_lwip_ip = {
	.hwaddr = NET_MAC_ADDR,
	.platform_ops = NET_LWIP_OPS,
	.mac_param = NET_MAC_PARAM,
};
#endif

#ifdef IIOD_TRANSPORT_USB
static struct no_os_timer_init_param usb_tick_timer_ip = {
	.id = USB_TICK_TIMER_ID,
	.freq_hz = USB_TICK_TIMER_FREQ_HZ,
	.ticks_count = USB_TICK_TIMER_TICKS,
	.platform_ops = TIMER_OPS,
};

static struct no_os_irq_init_param usb_tick_irq_ip = {
	.irq_ctrl_id = 0,
	.platform_ops = IRQ_OPS,
};

static const struct noos_iiod_usb iiod_usb = {
	.tick = {
		.timer = &usb_tick_timer_ip,
		.irq = &usb_tick_irq_ip,
		.irq_id = USB_TICK_IRQ_ID,
		.irq_handle = USB_TICK_IRQ_HANDLE,
		.irq_priority = TIMER_IRQ_PRIORITY,
	},
	.vendor_id = USB_VENDOR_ID,
	.product_id = USB_PRODUCT_ID,
	.manufacturer = USB_MANUFACTURER,
	.product = IIOD_USB_PRODUCT,
	.serial = USB_SERIAL,
};
#endif

const struct noos_iiod_param iiod_param = {
	.devices = iiod_devices,
	.nb_devices = NO_OS_ARRAY_SIZE(iiod_devices),
#ifdef IIOD_TRANSPORT_UART
	.uart = &iiod_uart_ip,
#else
	/* The uart is free when it is not the transport: logs go there. */
	.console = &iiod_uart_ip,
#endif
#ifdef IIOD_TRANSPORT_NETWORK
	.lwip = &iiod_lwip_ip,
#endif
#ifdef IIOD_TRANSPORT_USB
	.usb = &iiod_usb,
#endif
};
