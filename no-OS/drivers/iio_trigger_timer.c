/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <no_os_timer.h>
#include <no_os_irq.h>
#include <no_os_print_log.h>
#include <iio/iio-backend.h>
#include "noos_iiod.h"
#include "iio_device.h"
#include "iio_trigger_timer.h"

/*
 * Timer trigger, the bare-metal counterpart of the Zephyr trigger_timer: a
 * hardware timer interrupt counts ticks, and a sampling driver calls
 * iio_trigger_timer_wait() once per scan. With no threads the wait spins, so
 * the transfer blocks for nb_samples / sampling_frequency seconds -- the
 * transfer is synchronous either way.
 */
struct iio_trigger_timer {
	const struct noos_iiod_timer *cfg;
	struct no_os_timer_desc *timer;
	struct no_os_irq_ctrl_desc *irq;
	struct no_os_callback_desc cb;
	volatile uint32_t ticks;
	uint32_t seen;
	uint32_t freq_hz;
	bool running;
};

static struct iio_trigger_timer trig = {
	.freq_hz = IIO_TRIGGER_TIMER_DEFAULT_HZ,
};

/* Timer interrupt: one tick per sample period. */
static void trigger_timer_cb(void *ctx)
{
	trig.ticks++;
}

/*
 * no-OS keeps the callbacks of every peripheral in one list with a single
 * shared iterator, which each timer interrupt walks to find its callback. A
 * register or unregister that such an interrupt splits acts on the node the
 * interrupt left the iterator on: the unregister can free the USB tick's
 * callback, and a timer whose callback is gone never has its flag cleared,
 * so its interrupt fires forever. Change the list with interrupts off.
 */
static int trigger_cb_register(void)
{
	int ret;

	no_os_irq_global_disable(trig.irq);
	ret = no_os_irq_register_callback(trig.irq, trig.cfg->irq_id, &trig.cb);
	no_os_irq_global_enable(trig.irq);

	return ret;
}

/* Remove the tick callback with interrupts off, see trigger_cb_register(). */
static void trigger_cb_unregister(void)
{
	no_os_irq_global_disable(trig.irq);
	no_os_irq_unregister_callback(trig.irq, trig.cfg->irq_id, &trig.cb);
	no_os_irq_global_enable(trig.irq);
}

/*
 * Device-table init: @param is the project's struct noos_iiod_timer. Sets up
 * the IRQ controller; the timer itself starts on the first wait.
 */
int iio_trigger_timer_init(const void *param)
{
	int ret;

	if (!param)
		return -EINVAL;

	if (trig.irq)
		return 0;

	trig.cfg = param;

	ret = no_os_irq_ctrl_init(&trig.irq, trig.cfg->irq);
	if (ret)
		return ret;

	trig.cb = (struct no_os_callback_desc) {
		.callback = trigger_timer_cb,
		.ctx = NULL,
		.event = NO_OS_EVT_TIM_ELAPSED,
		.peripheral = NO_OS_TIM_IRQ,
		.handle = trig.cfg->irq_handle,
	};

	ret = no_os_irq_set_priority(trig.irq, trig.cfg->irq_id,
				     trig.cfg->irq_priority);
	if (ret)
		/* The controller is shared with the other drivers: keep it. */
		trig.irq = NULL;

	return ret;
}

/* Program the timer for freq_hz and enable its interrupt. */
static int trigger_timer_start(void)
{
	struct no_os_timer_init_param timer_ip;
	int ret;

	if (!trig.irq)
		return -ENODEV;

	/* The project's timer, with the period sampling_frequency asks for. */
	timer_ip = *trig.cfg->timer;
	timer_ip.ticks_count = timer_ip.freq_hz / trig.freq_hz;

	ret = no_os_timer_init(&trig.timer, &timer_ip);
	if (ret)
		return ret;

	/*
	 * The callback is registered only now, as iiod/usb.c does for its tick:
	 * registering enables the timer's interrupt, and no_os_timer_init()
	 * resets the timer, so registering first leaves it without interrupts.
	 */
	ret = trigger_cb_register();
	if (ret)
		goto remove_timer;

	ret = no_os_irq_enable(trig.irq, trig.cfg->irq_id);
	if (ret)
		goto unregister_cb;

	trig.seen = trig.ticks;

	ret = no_os_timer_start(trig.timer);
	if (ret)
		goto disable_irq;

	trig.running = true;

	return 0;

disable_irq:
	no_os_irq_disable(trig.irq, trig.cfg->irq_id);
unregister_cb:
	trigger_cb_unregister();
remove_timer:
	no_os_timer_remove(trig.timer);
	trig.timer = NULL;

	return ret;
}

/* Stop the timer, when streaming ends or the frequency changes. */
void iio_trigger_timer_stop(void)
{
	if (!trig.running)
		return;

	no_os_timer_stop(trig.timer);
	no_os_irq_disable(trig.irq, trig.cfg->irq_id);
	trigger_cb_unregister();
	no_os_timer_remove(trig.timer);

	trig.timer = NULL;
	trig.running = false;
}

/*
 * Called by a sampling driver before each scan: blocks until the next tick,
 * so the capture runs at sampling_frequency.
 */
int iio_trigger_timer_wait(void)
{
	int ret;

	/* Free running: no rate asked for, or no trigger in the device table. */
	if (!trig.freq_hz || !trig.irq)
		return 0;

	if (!trig.running) {
		ret = trigger_timer_start();
		if (ret)
			return ret;
	}

	while (trig.ticks == trig.seen)
		;

	/*
	 * A scan slower than the period missed ticks: resync to the latest
	 * one instead of bursting to catch up, so the spacing stays even.
	 */
	trig.seen = trig.ticks;

	return 0;
}

/* Read the trigger's sampling_frequency. */
static int iio_trigger_timer_read_attr(void *dev,
				       const struct iio_device *iio_dev,
				       const struct iio_attr *attr,
				       char *dst, size_t len)
{
	int ret;

	if (attr->type != IIO_ATTR_TYPE_DEVICE ||
	    strcmp(iio_attr_get_name(attr), "sampling_frequency"))
		return -EINVAL;

	ret = snprintf(dst, len, "%u", (unsigned int)trig.freq_hz);
	if (ret < 0 || (size_t)ret >= len)
		return -EINVAL;

	return ret + 1;
}

/*
 * Set sampling_frequency, e.g. iio_attr -u <uri> -d timer0
 * sampling_frequency 1000.
 */
static int iio_trigger_timer_write_attr(void *dev,
					const struct iio_device *iio_dev,
					const struct iio_attr *attr,
					const char *src, size_t len)
{
	unsigned long val;
	char *end;

	if (attr->type != IIO_ATTR_TYPE_DEVICE ||
	    strcmp(iio_attr_get_name(attr), "sampling_frequency"))
		return -EINVAL;

	if (*src < '0' || *src > '9')
		return -EINVAL;

	val = strtoul(src, &end, 10);
	if (*end == '\n')
		end++;
	if (end == src || *end != '\0' || val > IIO_TRIGGER_TIMER_MAX_HZ)
		return -EINVAL;

	/* The next wait restarts the timer at the new period. */
	iio_trigger_timer_stop();
	trig.freq_hz = (uint32_t)val;

	return (int)len;
}

/* Expose sampling_frequency on the trigger device. */
static int iio_trigger_timer_add_attrs(void *dev, struct iio_device *iio_dev)
{
	return iio_device_add_attr(iio_dev, "sampling_frequency",
				   IIO_ATTR_TYPE_DEVICE);
}

/* Describe the timer trigger for noos_iio_register_device(). */
int iio_trigger_timer_get_device_info(struct noos_iio_device_info *info)
{
	if (!info)
		return -EINVAL;

	*info = (struct noos_iio_device_info) {
		.name = IIO_TRIGGER_TIMER_NAME,
		.add_channels = iio_trigger_timer_add_attrs,
		.read_attr = iio_trigger_timer_read_attr,
		.write_attr = iio_trigger_timer_write_attr,
		.is_trigger = true,
	};

	return 0;
}
