/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <no_os_uart.h>
#include <no_os_print_log.h>
#include "iio_adc.h"
#include "iio_device.h"

#ifdef MAX32690
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"
#endif

#if defined(NO_OS_USB_TRANSPORT)
extern int iiod_usb_run(void);
#elif defined(NO_OS_LWIP_NETWORKING)
#include "lwip_socket.h"
#include "lwip_adin1110.h"
#include "adin1110.h"
#include <no_os_spi.h>
#include <no_os_gpio.h>
#ifdef MAX32690
#include "maxim_spi.h"
#include "maxim_gpio.h"
#endif

extern int iiod_network_run(struct lwip_network_desc *lwip_desc);
#else
extern int iiod_uart_run(struct no_os_uart_desc *uart_desc);
#endif

int main(void)
{
	struct no_os_uart_desc *uart_desc;
#ifdef MAX32690
	struct max_uart_init_param uart_extra = {
		.flow = MAX_UART_FLOW_DIS,
	};
#endif
	struct no_os_uart_init_param uart_ip = {
		.device_id = 0,
		.baud_rate = 115200,
		.size = NO_OS_UART_CS_8,
		.parity = NO_OS_UART_PAR_NO,
		.stop = NO_OS_UART_STOP_1_BIT,
		.asynchronous_rx = true,
#ifdef MAX32690
		.platform_ops = &max_uart_ops,
		.extra = &uart_extra,
#else
		.platform_ops = NULL,
		.extra = NULL,
#endif
	};
	struct noos_iio_device_info adc_info;
	int ret;

	ret = no_os_uart_init(&uart_desc, &uart_ip);
	if (ret)
		return ret;

#if defined(NO_OS_USB_TRANSPORT) || defined(NO_OS_LWIP_NETWORKING)
	/* Use UART for debug console output in USB/network mode */
	no_os_uart_stdio(uart_desc);
#endif

	/* Init on-chip ADC and register as IIO device */
	ret = iio_adc_init();
	if (ret) {
		no_os_uart_remove(uart_desc);
		return ret;
	}

	ret = iio_adc_get_device_info(&adc_info);
	if (ret) {
		no_os_uart_remove(uart_desc);
		return ret;
	}

	ret = noos_iio_register_device(&adc_info);
	if (ret) {
		no_os_uart_remove(uart_desc);
		return ret;
	}

#if defined(NO_OS_USB_TRANSPORT)
	ret = iiod_usb_run();
#elif defined(NO_OS_LWIP_NETWORKING)
	{
		struct lwip_network_desc *lwip_desc;
		uint8_t mac[6] = {0x00, 0x18, 0x80, 0x03, 0x25, 0x60};

#ifdef MAX32690
		struct max_spi_init_param adin_spi_extra = {
			.num_slaves = 1,
			.polarity = SPI_SS_POL_LOW,
			.vssel = MXC_GPIO_VSSEL_VDDIOH,
		};
		struct max_gpio_init_param adin_gpio_extra = {
			.vssel = MXC_GPIO_VSSEL_VDDIOH,
		};
#endif
		struct no_os_gpio_init_param adin_rst_gpio = {
			.port = 0,
			.number = 15,
			.pull = NO_OS_PULL_NONE,
			.platform_ops = &max_gpio_ops,
			.extra = &adin_gpio_extra,
		};
		struct no_os_spi_init_param adin_spi = {
			.device_id = 3,
			.max_speed_hz = 15000000,
			.bit_order = NO_OS_SPI_BIT_ORDER_MSB_FIRST,
			.mode = NO_OS_SPI_MODE_0,
			.platform_ops = &max_spi_ops,
			.chip_select = 0,
			.extra = &adin_spi_extra,
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

		memcpy(adin_ip.mac_address, mac, 6);
		memcpy(lwip_param.hwaddr, mac, 6);

		pr_info("Initializing ADIN1110 + lwIP...\n");
		ret = no_os_lwip_init(&lwip_desc, &lwip_param);
		if (ret) {
			pr_err("lwIP init failed: %d\n", ret);
			no_os_uart_remove(uart_desc);
			return ret;
		}

		ret = iiod_network_run(lwip_desc);
		no_os_lwip_remove(lwip_desc);
	}
#else
	ret = iiod_uart_run(uart_desc);
#endif

	no_os_uart_remove(uart_desc);

	return ret;
}
