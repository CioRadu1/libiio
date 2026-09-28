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
#include "timers.h"
#include "iio_trigger_timer.h"

/*
 * Timer trigger, the bare-metal counterpart of the Zephyr trigger_timer: a
 * hardware timer interrupt counts ticks, and a sampling driver calls
 * iio_trigger_timer_wait() once per scan. With no threads the wait spins, so
 * the transfer blocks for nb_samples / sampling_frequency seconds -- the
 * transfer is synchronous either way.
 */
struct iio_trigger_timer {
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
	ret = no_os_irq_register_callback(trig.irq, TRIGGER_IRQ_ID, &trig.cb);
	no_os_irq_global_enable(trig.irq);

	return ret;
}

static void trigger_cb_unregister(void)
{
	no_os_irq_global_disable(trig.irq);
	no_os_irq_unregister_callback(trig.irq, TRIGGER_IRQ_ID, &trig.cb);
	no_os_irq_global_enable(trig.irq);
}

int iio_trigger_timer_init(void)
{
	struct no_os_irq_init_param irq_ip = {
		.irq_ctrl_id = 0,
		.platform_ops = TRIGGER_IRQ_OPS,
		.extra = NULL,
	};
	int ret;

	if (trig.irq)
		return 0;

	ret = no_os_irq_ctrl_init(&trig.irq, &irq_ip);
	if (ret)
		return ret;

	trig.cb = (struct no_os_callback_desc) {
		.callback = trigger_timer_cb,
		.ctx = NULL,
		.event = NO_OS_EVT_TIM_ELAPSED,
		.peripheral = NO_OS_TIM_IRQ,
		.handle = TRIGGER_IRQ_HANDLE,
	};

	ret = no_os_irq_set_priority(trig.irq, TRIGGER_IRQ_ID,
				     TRIGGER_IRQ_PRIORITY);
	if (ret)
		/* The controller is shared with the other drivers: keep it. */
		trig.irq = NULL;

	return ret;
}

static int trigger_timer_start(void)
{
	struct no_os_timer_init_param timer_ip = {
		.id = TRIGGER_TIMER_ID,
		.freq_hz = TRIGGER_TIMER_FREQ_HZ,
		.ticks_count = TRIGGER_TIMER_FREQ_HZ / trig.freq_hz,
		.platform_ops = TRIGGER_TIMER_OPS,
		.extra = TRIGGER_TIMER_EXTRA,
	};
	int ret;

	if (!trig.irq)
		return -ENODEV;

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

	ret = no_os_irq_enable(trig.irq, TRIGGER_IRQ_ID);
	if (ret)
		goto unregister_cb;

	trig.seen = trig.ticks;

	ret = no_os_timer_start(trig.timer);
	if (ret)
		goto disable_irq;

	trig.running = true;

	return 0;

disable_irq:
	no_os_irq_disable(trig.irq, TRIGGER_IRQ_ID);
unregister_cb:
	trigger_cb_unregister();
remove_timer:
	no_os_timer_remove(trig.timer);
	trig.timer = NULL;

	return ret;
}

void iio_trigger_timer_stop(void)
{
	if (!trig.running)
		return;

	no_os_timer_stop(trig.timer);
	no_os_irq_disable(trig.irq, TRIGGER_IRQ_ID);
	trigger_cb_unregister();
	no_os_timer_remove(trig.timer);

	trig.timer = NULL;
	trig.running = false;
}

int iio_trigger_timer_wait(void)
{
	int ret;

	if (!trig.freq_hz)
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

static int iio_trigger_timer_add_attrs(void *dev, struct iio_device *iio_dev)
{
	return iio_device_add_attr(iio_dev, "sampling_frequency",
				   IIO_ATTR_TYPE_DEVICE);
}

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
