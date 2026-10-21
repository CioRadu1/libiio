/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_IIO_DRIVER_H_
#define NOOS_IIO_DRIVER_H_

/*
 * Serves an existing no-OS IIO driver (iio_<part>.c, which describes its
 * device with a no-OS struct iio_device) through this libiio backend, with
 * no per-driver code.
 *
 * This header includes neither libiio's iio.h nor no-OS's iio_types.h, as
 * both define struct iio_device: a board file can include it next to the
 * no-OS driver headers.
 */

#include <stdbool.h>
#include <stdint.h>

struct noos_iio_device_info;

/**
 * struct noos_iio_driver - one no-OS driver instance to serve
 * @name:       IIO device name the host sees (e.g. "adc_demo")
 * @descriptor: the driver's no-OS struct iio_device (e.g.
 *              &adc_demo_iio_descriptor)
 * @instance:   the driver's device handle, what its show/store/submit get;
 *              set it after the driver's init, before the context is built
 * @trigger:    name of the trigger device that paces it (e.g. "timer0"), or
 *              NULL. As in no-OS: with a trigger, each tick runs the
 *              driver's trigger_handler for one scan; without one, its
 *              submit fills the whole block
 * @enabled, @mask: kept by iio_noos_driver.c; leave them zero. pre_enable needs
 *              the channel mask, which is only known at the first transfer
 */
struct noos_iio_driver {
	const char *name;
	void *descriptor;
	void *instance;
	const char *trigger;
	bool enabled;
	uint32_t mask;
};

/* Describe a served no-OS driver for noos_iio_register_device(). */
int noos_iio_driver_get_device_info(struct noos_iio_driver *drv,
				    struct noos_iio_device_info *info);

/*
 * A device-table row for a no-OS driver, with the driver's own typed init:
 *
 *	NOOS_IIOD_DRIVER_DEFINE(adc, "adc_demo", adc_demo_init,
 *				struct adc_demo_desc, &adc_init_par,
 *				&adc_demo_iio_descriptor, "timer0");
 *	...
 *	{ NOOS_IIOD_DRIVER(adc), ... }
 *
 * The last argument is the trigger that paces it, or NULL (see @trigger).
 * The first expands, at file scope, to the driver's state and two small
 * functions; the second names them in a struct noos_iiod_device.
 */
#define NOOS_IIOD_DRIVER_DEFINE(_id, _name, _init, _desc_type, _init_param, \
				_descriptor, _trigger)			\
	static struct noos_iio_driver _id##_iiod_drv = {		\
		.name = _name,						\
		.descriptor = _descriptor,				\
		.trigger = _trigger,					\
	};								\
	static int _id##_iiod_init(const void *param)			\
	{								\
		_desc_type *desc;					\
		int ret = _init(&desc, _init_param);			\
									\
		if (!ret)						\
			_id##_iiod_drv.instance = desc;			\
		return ret;						\
	}								\
	static int _id##_iiod_info(struct noos_iio_device_info *info)	\
	{								\
		return noos_iio_driver_get_device_info(&_id##_iiod_drv, info); \
	}

#define NOOS_IIOD_DRIVER(_id) \
	{ .init = _id##_iiod_init, .get_info = _id##_iiod_info }

struct iio_trigger;
struct iio_hw_trig_init_param;
struct no_os_irq_init_param;
struct no_os_timer_init_param;

/**
 * struct noos_iio_trigger - a no-OS trigger served as a libiio trigger device
 * @name:         device name the host sees; drivers name it as their trigger
 * @descriptor:   the driver's no-OS struct iio_trigger (its *_trig.c, e.g.
 *                &adc_iio_timer_trig_desc)
 * @hw:           hardware trigger settings (irq_ctrl is filled in here), or
 *                NULL for a software trigger, fired by a trigger_now write
 * @irq:          the interrupt controller to bring up (hardware only)
 * @timer:        a timer to start for a timer trigger, NULL for a GPIO one
 * @irq_priority: interrupt priority (hardware only)
 * @instance, @irq_desc, @timer_desc, @fired, @seen, @enabled: kept by the
 *                port; leave them zero
 *
 * The interrupt (or the trigger_now write) only counts a tick: the capture
 * loop then runs the paced driver's trigger_handler once per tick, as for
 * the timer0 trigger.
 */
struct noos_iio_trigger {
	const char *name;
	struct iio_trigger *descriptor;
	struct iio_hw_trig_init_param *hw;
	struct no_os_irq_init_param *irq;
	struct no_os_timer_init_param *timer;
	uint32_t irq_priority;
	void *instance;
	void *irq_desc;
	void *timer_desc;
	volatile uint32_t fired;
	uint32_t seen;
	bool enabled;
};

int noos_iio_trigger_init(struct noos_iio_trigger *trig);
int noos_iio_trigger_get_device_info(struct noos_iio_trigger *trig,
				     struct noos_iio_device_info *info);

/*
 * A device-table row for a no-OS trigger, from the no-OS structs a project
 * already has for iio_hw_trig_init():
 *
 *	NOOS_IIOD_TRIGGER_DEFINE(adc_trig, "adc_demo_timer_trig",
 *				 &adc_iio_timer_trig_desc, &adc_trig_ip,
 *				 &timer_irq_ip, &adc_timer_ip, 3);
 *	...
 *	{ NOOS_IIOD_TRIGGER(adc_trig), ... }
 *
 * For a software trigger pass NULL for the last four but the priority.
 */
#define NOOS_IIOD_TRIGGER_DEFINE(_id, _name, _descriptor, _hw, _irq, _timer, \
				 _priority)				\
	static struct noos_iio_trigger _id##_iiod_trig = {		\
		.name = _name,						\
		.descriptor = _descriptor,				\
		.hw = _hw,						\
		.irq = _irq,						\
		.timer = _timer,					\
		.irq_priority = _priority,				\
	};								\
	static int _id##_iiod_init(const void *param)			\
	{								\
		return noos_iio_trigger_init(&_id##_iiod_trig);		\
	}								\
	static int _id##_iiod_info(struct noos_iio_device_info *info)	\
	{								\
		return noos_iio_trigger_get_device_info(&_id##_iiod_trig, info); \
	}

#define NOOS_IIOD_TRIGGER(_id)	NOOS_IIOD_DRIVER(_id)

#endif /* NOOS_IIO_DRIVER_H_ */
