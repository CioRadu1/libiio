/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Devices of a board without a boards/<BOARD>.c: the ADC HAL picked by the
 * build (ADC cache variable), the timer0 trigger that paces it and, with
 * IIOD_NOOS_ADC_DEMO, no-OS's own adc_demo driver through iio_noos_driver.c.
 */

#include <iio_board.h>
#include "iio_adc.h"
#include "iio_trigger_timer.h"

#ifdef IIOD_NOOS_ADC_DEMO
#include <iio_noos_driver.h>
#include "iio_adc_demo.h"

/*
 * An unmodified no-OS IIO driver: what a no-OS project would hand to
 * iio_app, it hands to iio_noos_driver.c instead.
 */
static struct noos_iio_driver adc_demo_driver = {
	.name = "adc_demo",
	.descriptor = &adc_demo_iio_descriptor,
};

static int adc_demo_board_init(void)
{
	struct adc_demo_init_param param = { 0 };
	struct adc_demo_desc *desc;
	int ret;

	ret = adc_demo_init(&desc, &param);
	if (ret)
		return ret;

	adc_demo_driver.instance = desc;
	return 0;
}

static int adc_demo_board_info(struct noos_iio_device_info *info)
{
	return noos_iio_driver_get_device_info(&adc_demo_driver, info);
}
#endif /* IIOD_NOOS_ADC_DEMO */

const struct noos_board_device noos_board_devices[] = {
	{ .init = iio_adc_init, .get_info = iio_adc_get_device_info },
	{ .get_info = iio_trigger_timer_get_device_info },
#ifdef IIOD_NOOS_ADC_DEMO
	{ .init = adc_demo_board_init, .get_info = adc_demo_board_info },
#endif
};

const unsigned int noos_board_device_count =
	sizeof(noos_board_devices) / sizeof(noos_board_devices[0]);
