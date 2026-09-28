/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_DRIVERS_IIO_TRIGGER_TIMER_H_
#define NOOS_DRIVERS_IIO_TRIGGER_TIMER_H_

#include "iio_device.h"

/* Name of the trigger device, for noos_iio_device_info.trigger. */
#define IIO_TRIGGER_TIMER_NAME		"timer0"

#define IIO_TRIGGER_TIMER_DEFAULT_HZ	1000
#define IIO_TRIGGER_TIMER_MAX_HZ	100000

/**
 * @brief Set up the trigger. The hardware timer only runs while a buffer
 *        is sampling.
 * @return 0 in case of success, negative error code otherwise.
 */
int iio_trigger_timer_init(void);

/**
 * @brief Wait for the next trigger tick, starting the timer on the first call.
 *        Returns at once when sampling_frequency is 0 (free running).
 * @return 0 in case of success, negative error code otherwise.
 */
int iio_trigger_timer_wait(void);

/**
 * @brief Stop the timer; the next iio_trigger_timer_wait() restarts it.
 */
void iio_trigger_timer_stop(void);

/**
 * @brief Describe the trigger device for noos_iio_register_device().
 * @param info - Filled with the trigger's callbacks.
 * @return 0 in case of success, -EINVAL if info is NULL.
 */
int iio_trigger_timer_get_device_info(struct noos_iio_device_info *info);

#endif /* NOOS_DRIVERS_IIO_TRIGGER_TIMER_H_ */
