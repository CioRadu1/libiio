/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Which hardware timer each part of the port owns. Kept apart from
 * parameters.h so a driver can take its timer without the transport wiring,
 * and so the assignments sit side by side: TMR0 = USB tick, TMR1 = trigger.
 */

#ifndef IIOD_TIMERS_H
#define IIOD_TIMERS_H

#include "maxim_timer.h"
#include "maxim_irq.h"

/* ---------- Sampling trigger (timer0 IIO trigger) ---------- */
#define TRIGGER_TIMER_ID	1
#define TRIGGER_TIMER_FREQ_HZ	1000000
#define TRIGGER_TIMER_OPS	&max_timer_ops
#define TRIGGER_TIMER_EXTRA	NULL
#define TRIGGER_IRQ_OPS		&max_irq_ops
#define TRIGGER_IRQ_ID		TMR1_IRQn
#define TRIGGER_IRQ_HANDLE	MXC_TMR1
/*
 * Same as the USB tick: the timer ISRs share one callback-list iterator in
 * no-OS, so one must not preempt the other half way through its lookup.
 */
#define TRIGGER_IRQ_PRIORITY	3

/* ---------- USB service tick ---------- */
#ifdef NO_OS_USB_TRANSPORT

#define USB_TICK_TIMER_ID	0
#define USB_TICK_TIMER_FREQ_HZ	1000000
#define USB_TICK_TIMER_TICKS	100
#define USB_TICK_TIMER_OPS	&max_timer_ops
#define USB_TICK_TIMER_EXTRA	NULL
#define USB_TICK_IRQ_OPS	&max_irq_ops
#define USB_TICK_IRQ_ID		TMR0_IRQn
#define USB_TICK_IRQ_HANDLE	MXC_TMR0
#define USB_TICK_IRQ_PRIORITY	3

#endif /* NO_OS_USB_TRANSPORT */

#endif /* IIOD_TIMERS_H */
