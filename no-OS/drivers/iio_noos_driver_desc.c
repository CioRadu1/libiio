/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Reads a no-OS driver's descriptor for iio_noos_driver.c: everything that
 * touches a no-OS struct
 * iio_device. It must not include libiio's iio.h (see iio_noos_driver_desc.h).
 *
 * Nothing from no-OS's own IIO server (iio/iio.c, iiod.c, iio_trigger.c)
 * or from iio_app is built; only its headers' types are used. What drivers
 * call into that server is provided here instead, under the same names:
 * the four iio_buffer_* functions, which fill the libiio block given to
 * noos_desc_transfer(), and the trigger functions their *_trig.c and the
 * project use (iio_hw_trig_init(), iio_trig_enable(), ...).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "iio_noos_driver_desc.h"
#include "iio_trigger.h"
#include "iio_trigger_timer.h"
#include "iio_types.h"
#include "no_os_alloc.h"
#include "no_os_circular_buffer.h"
#include "no_os_delay.h"
#include "no_os_irq.h"
#include "no_os_timer.h"
#include "no_os_util.h"

#define NOOS_IIOD_MAX_TRIGGERS 8
/*
 * A trigger that stops ticking (a software trigger nobody writes, a data
 * ready line that never fires) ends the capture after this long: the board
 * serves one request at a time, so it could not even take the trigger_now
 * that would unblock it.
 */
#define NOOS_IIOD_TRIGGER_TIMEOUT_S 5

/* The no-OS triggers in the device table, found by name or by instance. */
static struct noos_iio_trigger *triggers[NOOS_IIOD_MAX_TRIGGERS];
static unsigned int nb_triggers;

/* Same names as no-OS iio.c prints, so the host sees the same channel ids. */
static const char *const chan_type_names[] = {
	[IIO_VOLTAGE] = "voltage",
	[IIO_CURRENT] = "current",
	[IIO_ALTVOLTAGE] = "altvoltage",
	[IIO_ANGL_VEL] = "anglvel",
	[IIO_TEMP] = "temp",
	[IIO_CAPACITANCE] = "capacitance",
	[IIO_ACCEL] = "accel",
	[IIO_RESISTANCE] = "resistance",
	[IIO_MAGN] = "magn",
	[IIO_INCLI] = "incli",
	[IIO_VELOCITY] = "velocity",
	[IIO_ANGL] = "angl",
	[IIO_ROT] = "rot",
	[IIO_COUNT] = "count",
	[IIO_DELTA_ANGL] = "deltaangl",
	[IIO_DELTA_VELOCITY] = "deltavelocity",
	[IIO_WEIGHT] = "weight",
	[IIO_POWER] = "power",
};

static const char *const modifier_names[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
	[IIO_MOD_Z] = "z",
	[IIO_MOD_TEMP_AMBIENT] = "ambient",
	[IIO_MOD_PITCH] = "pitch",
	[IIO_MOD_YAW] = "yaw",
	[IIO_MOD_ROLL] = "roll",
};

/* The no-OS struct iio_device a served driver was declared with. */
static struct iio_device *desc_of(const struct noos_iio_driver *a)
{
	return (struct iio_device *)a->descriptor;
}

/* The channel type's name, as no-OS iio.c prints it. */
static const char *chan_type_name(enum iio_chan_type type)
{
	if ((unsigned int)type >= NO_OS_ARRAY_SIZE(chan_type_names) || !chan_type_names[type])
		return "unknown";

	return chan_type_names[type];
}

/* The modifier's name, as no-OS iio.c prints it. */
static const char *modifier_name(int mod)
{
	if (mod < 0 || (unsigned int)mod >= NO_OS_ARRAY_SIZE(modifier_names) ||
			!modifier_names[mod])
		return "unknown";

	return modifier_names[mod];
}

/* Channel id as no-OS iio.c prints it: voltage0, accel_x, voltage0-voltage1. */
static int chan_id(const struct iio_channel *ch, char *buf, size_t len)
{
	const char *type = chan_type_name(ch->ch_type);
	int ret;

	if (ch->modified)
		ret = snprintf(buf, len, "%s_%s", type, modifier_name(ch->channel2));
	else if (!ch->indexed)
		ret = snprintf(buf, len, "%s", type);
	else if (ch->diferential)
		ret = snprintf(buf, len, "%s%d-%s%d", type, ch->channel, type, ch->channel2);
	else
		ret = snprintf(buf, len, "%s%d", type, ch->channel);

	return (ret < 0 || (size_t)ret >= len) ? -EINVAL : 0;
}

/* Number of channels in the descriptor. */
unsigned int noos_desc_num_channels(const struct noos_iio_driver *a)
{
	struct iio_device *desc = desc_of(a);

	return desc->channels ? desc->num_ch : 0;
}

/* Channel @ch of the descriptor, in plain types. */
int noos_desc_get_chan(const struct noos_iio_driver *a, unsigned int ch, struct noos_desc_chan *out)
{
	const struct iio_channel *c;

	if (ch >= noos_desc_num_channels(a))
		return -EINVAL;

	c = &desc_of(a)->channels[ch];
	*out = (struct noos_desc_chan){
		.output = c->ch_out,
		.scan_element = c->scan_type != NULL,
		.scan_index = c->scan_index,
	};

	if (c->scan_type) {
		out->bits = c->scan_type->realbits;
		out->length = c->scan_type->storagebits;
		out->shift = c->scan_type->shift;
		out->is_signed = c->scan_type->sign == 's';
		out->is_be = c->scan_type->is_big_endian;
	}

	return chan_id(c, out->id, sizeof(out->id));
}

/* As no-OS iio.c: shared-by-all channel attributes belong to the device. */
static bool is_shared(const struct iio_attribute *attr)
{
	return attr->shared == IIO_SHARED_BY_ALL;
}

/* First shared-by-all channel attribute called @name, NULL if none. */
static struct iio_attribute *find_shared(const struct noos_iio_driver *a, const char *name)
{
	struct iio_device *desc = desc_of(a);
	struct iio_attribute *attrs;
	unsigned int i, k;

	for (i = 0; i < noos_desc_num_channels(a); i++) {
		attrs = desc->channels[i].attributes;
		for (k = 0; attrs && attrs[k].name; k++)
			if (is_shared(&attrs[k]) && !strcmp(attrs[k].name, name))
				return &attrs[k];
	}

	return NULL;
}

/* k-th distinct shared-by-all attribute name, NULL past the end. */
static const char *shared_name(const struct noos_iio_driver *a, unsigned int k)
{
	struct iio_device *desc = desc_of(a);
	struct iio_attribute *attrs;
	unsigned int i, j;

	for (i = 0; i < noos_desc_num_channels(a); i++) {
		attrs = desc->channels[i].attributes;
		for (j = 0; attrs && attrs[j].name; j++) {
			if (!is_shared(&attrs[j]) || find_shared(a, attrs[j].name) != &attrs[j])
				continue;
			if (!k--)
				return attrs[j].name;
		}
	}

	return NULL;
}

/* k-th entry of a NULL-name-terminated array; @chan_only skips shared ones. */
static struct iio_attribute *nth_attr(struct iio_attribute *attrs, unsigned int k, bool chan_only)
{
	unsigned int i;

	for (i = 0; attrs && attrs[i].name; i++) {
		if (chan_only && is_shared(&attrs[i]))
			continue;
		if (!k--)
			return &attrs[i];
	}

	return NULL;
}

/* The descriptor's attribute array of one kind (device, debug, channel). */
static struct iio_attribute *attr_array(
		const struct noos_iio_driver *a, enum noos_desc_attr_kind kind, unsigned int ch)
{
	struct iio_device *desc = desc_of(a);

	switch (kind) {
	case NOOS_DESC_ATTR_DEVICE:
		return desc->attributes;
	case NOOS_DESC_ATTR_DEBUG:
		return desc->debug_attributes;
	case NOOS_DESC_ATTR_CHANNEL:
		if (ch >= noos_desc_num_channels(a))
			return NULL;
		return desc->channels[ch].attributes;
	default:
		return NULL;
	}
}

/* Name of the k-th attribute of one kind; see iio_noos_driver_desc.h. */
const char *noos_desc_attr_name(const struct noos_iio_driver *a, enum noos_desc_attr_kind kind,
		unsigned int ch, unsigned int k)
{
	struct iio_attribute *attr;

	if (kind == NOOS_DESC_ATTR_SHARED)
		return shared_name(a, k);

	attr = nth_attr(attr_array(a, kind, ch), k, kind == NOOS_DESC_ATTR_CHANNEL);

	return attr ? attr->name : NULL;
}

/* Attribute called @name in an array, NULL if none. */
static struct iio_attribute *find_attr(struct iio_attribute *attrs, const char *name)
{
	unsigned int i;

	for (i = 0; attrs && attrs[i].name; i++)
		if (!strcmp(attrs[i].name, name))
			return &attrs[i];

	return NULL;
}

/* Run an attribute's show or store; see iio_noos_driver_desc.h. */
int noos_desc_attr_rw(const struct noos_iio_driver *a, enum noos_desc_attr_kind kind,
		unsigned int ch, const char *name, char *buf, size_t len, bool write)
{
	struct iio_ch_info info, *pinfo = NULL;
	struct iio_attribute *attr;

	if (kind == NOOS_DESC_ATTR_SHARED) {
		attr = find_shared(a, name);
	} else {
		attr = find_attr(attr_array(a, kind, ch), name);
		if (kind == NOOS_DESC_ATTR_CHANNEL) {
			const struct iio_channel *c = &desc_of(a)->channels[ch];

			info = (struct iio_ch_info){
				.ch_num = c->channel,
				.ch_out = c->ch_out,
				.type = c->ch_type,
				.differential = c->diferential,
				.address = c->address,
			};
			pinfo = &info;
		}
	}

	if (!attr)
		return -ENOENT;

	if (write)
		return attr->store ? attr->store(a->instance, buf, len, pinfo, attr->priv) : -EPERM;

	return attr->show ? attr->show(a->instance, buf, len, pinfo, attr->priv) : -EPERM;
}

/* True if the driver gives register access. */
bool noos_desc_has_reg(const struct noos_iio_driver *a)
{
	return desc_of(a)->debug_reg_read != NULL;
}

/* Read a register through the driver's debug_reg_read. */
int noos_desc_reg_read(const struct noos_iio_driver *a, uint32_t reg, uint32_t *val)
{
	struct iio_device *desc = desc_of(a);

	if (!desc->debug_reg_read)
		return -ENOSYS;

	return desc->debug_reg_read(a->instance, reg, val);
}

/* Write a register through the driver's debug_reg_write. */
int noos_desc_reg_write(const struct noos_iio_driver *a, uint32_t reg, uint32_t val)
{
	struct iio_device *desc = desc_of(a);

	if (!desc->debug_reg_write)
		return -ENOSYS;

	return desc->debug_reg_write(a->instance, reg, val);
}

/* Same layout rule as no-OS iio.c, which is what the driver writes. */
size_t noos_desc_bytes_per_scan(const struct noos_iio_driver *a, uint32_t mask)
{
	struct iio_device *desc = desc_of(a);
	size_t cnt = 0, length, largest = 1;
	unsigned int i;

	for (i = 0; mask && i < noos_desc_num_channels(a); i++, mask >>= 1) {
		if (!(mask & 1) || !desc->channels[i].scan_type)
			continue;

		length = desc->channels[i].scan_type->storagebits / 8;
		if (!length)
			continue;
		if (length > largest)
			largest = length;

		if (cnt % length)
			cnt += 2 * length - (cnt % length);
		else
			cnt += length;
	}

	if (cnt % largest)
		cnt += largest - (cnt % largest);

	return cnt;
}

/* Buffer on (pre_enable with the mask) or off (post_disable). */
int noos_desc_enable(const struct noos_iio_driver *a, uint32_t mask, bool enable)
{
	struct iio_device *desc = desc_of(a);

	if (enable)
		return desc->pre_enable ? desc->pre_enable(a->instance, mask) : 0;

	return desc->post_disable ? desc->post_disable(a->instance) : 0;
}

/* The no-OS trigger of the device table called @name, or NULL. */
static struct noos_iio_trigger *trigger_by_name(const char *name)
{
	unsigned int i;

	for (i = 0; i < nb_triggers; i++)
		if (!strcmp(triggers[i]->name, name))
			return triggers[i];

	return NULL;
}

/* The no-OS trigger whose iio_hw_trig/iio_sw_trig is @instance. */
static struct noos_iio_trigger *trigger_by_instance(const void *instance)
{
	unsigned int i;

	for (i = 0; i < nb_triggers; i++)
		if (triggers[i]->instance == instance)
			return triggers[i];

	return NULL;
}

/*
 * Wait for the next tick of a no-OS trigger, enabling it on first use. The
 * interrupt only bumps @fired and only this loop bumps @seen, so neither
 * side needs a lock and no tick is lost.
 */
static int trigger_wait(struct noos_iio_trigger *t)
{
	struct iio_trigger *desc = t->descriptor;
	struct no_os_time now, deadline;
	int ret;

	if (!t->enabled) {
		/* trigger_now writes made before the capture still count. */
		if (t->hw)
			t->seen = t->fired;
		if (desc->enable) {
			ret = desc->enable(t->instance);
			if (ret)
				return ret;
		}
		t->enabled = true;
	}

	deadline = no_os_get_time();
	deadline.s += NOOS_IIOD_TRIGGER_TIMEOUT_S;

	while (t->fired == t->seen) {
		now = no_os_get_time();
		if (now.s > deadline.s || (now.s == deadline.s && now.us >= deadline.us))
			return -ETIMEDOUT;
	}
	t->seen++;

	return 0;
}

/* Wait for a tick of trigger @name; see iio_noos_driver_desc.h. */
int noos_trigger_wait(const char *name)
{
	struct noos_iio_trigger *t = name ? trigger_by_name(name) : NULL;

	return t ? trigger_wait(t) : iio_trigger_timer_wait();
}

/* Stop trigger @name; see iio_noos_driver_desc.h. */
void noos_trigger_stop(const char *name)
{
	struct noos_iio_trigger *t = name ? trigger_by_name(name) : NULL;
	struct iio_trigger *desc;

	if (!t) {
		iio_trigger_timer_stop();
		return;
	}

	desc = t->descriptor;
	if (t->enabled && desc->disable)
		desc->disable(t->instance);
	t->enabled = false;
}

/*
 * Triggered capture, as no-OS does it with a trigger assigned: one scan per
 * tick, which trigger_handler reads (or, for an output, writes) through
 * iio_buffer_push_scan() / iio_buffer_pop_scan().
 */
static int desc_triggered(const struct noos_iio_driver *a, struct iio_device *desc,
		struct iio_device_data *dev_data)
{
	uint32_t i;
	int ret;

	for (i = 0; i < dev_data->buffer->samples; i++) {
		ret = noos_trigger_wait(a->trigger);
		if (ret)
			return ret;

		ret = desc->trigger_handler(dev_data);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* One whole transfer through submit, trigger_handler or read/write_dev. */
int noos_desc_transfer(const struct noos_iio_driver *a, uint32_t mask, void *data, size_t bytes,
		bool output)
{
	struct iio_device *desc = desc_of(a);
	struct no_os_circular_buffer cb;
	struct iio_buffer buf = { 0 };
	struct iio_device_data dev_data;
	size_t bps = noos_desc_bytes_per_scan(a, mask);
	int ret;

	if (!bps || bytes % bps)
		return -EINVAL;

	ret = no_os_cb_cfg(&cb, (int8_t *)data, bytes);
	if (ret)
		return ret;

	/* An output block arrives full: the driver pops it scan by scan. */
	if (output)
		cb.write.spin_count = 1;

	buf = (struct iio_buffer){
		.active_mask = mask,
		.size = bytes,
		.bytes_per_scan = bps,
		.samples = bytes / bps,
		.dir = output ? IIO_DIRECTION_OUTPUT : IIO_DIRECTION_INPUT,
		.buf = &cb,
	};
	dev_data = (struct iio_device_data){
		.dev = a->instance,
		.buffer = &buf,
	};

	if (a->trigger && desc->trigger_handler)
		ret = desc_triggered(a, desc, &dev_data);
	else if (desc->submit)
		ret = desc->submit(&dev_data);
	else if (!output && desc->read_dev)
		ret = desc->read_dev(a->instance, data, buf.samples);
	else if (output && desc->write_dev)
		ret = desc->write_dev(a->instance, data, buf.samples);
	else
		ret = -ENOSYS;

	return ret < 0 ? ret : 0;
}

/* --- what drivers call from submit; see the comment at the top --- */

/* no-OS API for drivers: the block to fill or drain in one go. */
int iio_buffer_get_block(struct iio_buffer *buffer, void **addr)
{
	uint32_t size;

	if (!buffer)
		return -EINVAL;

	if (buffer->dir == IIO_DIRECTION_INPUT)
		return no_os_cb_prepare_async_write(buffer->buf, buffer->size, addr, &size);

	return no_os_cb_prepare_async_read(buffer->buf, buffer->size, addr, &size);
}

/* no-OS API for drivers: the block from iio_buffer_get_block() is done. */
int iio_buffer_block_done(struct iio_buffer *buffer)
{
	if (!buffer)
		return -EINVAL;

	if (buffer->dir == IIO_DIRECTION_INPUT)
		return no_os_cb_end_async_write(buffer->buf);

	return no_os_cb_end_async_read(buffer->buf);
}

/* no-OS API for drivers: append one scan to an input block. */
int iio_buffer_push_scan(struct iio_buffer *buffer, void *data)
{
	if (!buffer)
		return -EINVAL;

	return no_os_cb_write(buffer->buf, data, buffer->bytes_per_scan);
}

/* no-OS API for drivers: take one scan from an output block. */
int iio_buffer_pop_scan(struct iio_buffer *buffer, void *data)
{
	if (!buffer)
		return -EINVAL;

	return no_os_cb_read(buffer->buf, data, buffer->bytes_per_scan);
}

/* Name of a served trigger's k-th attribute, or NULL. */
const char *noos_desc_trig_attr_name(const struct noos_iio_trigger *t, unsigned int k)
{
	struct iio_attribute *attr = nth_attr(t->descriptor->attributes, k, false);

	return attr ? attr->name : NULL;
}

/* Run a served trigger attribute's show or store. */
int noos_desc_trig_attr_rw(const struct noos_iio_trigger *t, const char *name, char *buf,
		size_t len, bool write)
{
	struct iio_attribute *attr = find_attr(t->descriptor->attributes, name);

	if (!attr)
		return -ENOENT;

	if (write)
		return attr->store ? attr->store(t->instance, buf, len, NULL, attr->priv) : -EPERM;

	return attr->show ? attr->show(t->instance, buf, len, NULL, attr->priv) : -EPERM;
}

/*
 * Device-table init of a no-OS trigger: bring up its interrupt (and timer),
 * create it as a no-OS project would, then start the timer. The interrupt
 * stays off until a buffer waits on the trigger.
 */
int noos_iio_trigger_init(struct noos_iio_trigger *t)
{
	struct iio_hw_trig_init_param hw_ip;
	struct no_os_irq_ctrl_desc *irq;
	struct no_os_timer_desc *timer = NULL;
	int ret;

	if (!t || !t->name || !t->descriptor)
		return -EINVAL;
	if (nb_triggers >= NOOS_IIOD_MAX_TRIGGERS)
		return -ENOMEM;

	if (!t->hw) {
		struct iio_sw_trig_init_param sw_ip = { .name = t->name };

		ret = iio_sw_trig_init((struct iio_sw_trig **)&t->instance, &sw_ip);
		if (ret)
			return ret;
		goto add;
	}

	if (!t->irq)
		return -EINVAL;

	/*
	 * Timer first: no_os_timer_init() resets the timer, which on Maxim
	 * clears the interrupt enable that registering the callback sets.
	 */
	if (t->timer) {
		ret = no_os_timer_init(&timer, t->timer);
		if (ret)
			return ret;
	}

	/*
	 * The IRQ controller is not removed on the error paths below, on
	 * purpose: on Maxim it is one shared instance, and
	 * no_os_irq_ctrl_remove() would drop every peripheral's callbacks.
	 */
	ret = no_os_irq_ctrl_init(&irq, t->irq);
	if (ret)
		goto remove_timer;

	ret = no_os_irq_set_priority(irq, t->hw->irq_id, t->irq_priority);
	if (ret)
		goto remove_timer;

	hw_ip = *t->hw;
	hw_ip.irq_ctrl = irq;
	if (!hw_ip.name)
		hw_ip.name = t->name;

	ret = iio_hw_trig_init((struct iio_hw_trig **)&t->instance, &hw_ip);
	if (ret)
		goto remove_timer;

	if (timer) {
		ret = no_os_timer_start(timer);
		if (ret)
			goto remove_trig;
	}

	t->irq_desc = irq;
	t->timer_desc = timer;
add:
	triggers[nb_triggers++] = t;
	return 0;

remove_trig:
	iio_hw_trig_remove(t->instance);
	t->instance = NULL;
remove_timer:
	if (timer)
		no_os_timer_remove(timer);
	return ret;
}

/* --- no-OS's trigger API, which drivers' *_trig.c and projects call --- */

/* no-OS API: create a hardware trigger on an interrupt (no-OS name kept). */
int iio_hw_trig_init(struct iio_hw_trig **iio_trig, struct iio_hw_trig_init_param *init_param)
{
	struct iio_hw_trig *trig;
	int ret;

	if (!iio_trig || !init_param || !init_param->name || !init_param->irq_ctrl)
		return -EINVAL;

	trig = no_os_calloc(1, sizeof(*trig));
	if (!trig)
		return -ENOMEM;

	strncpy(trig->name, init_param->name, TRIG_MAX_NAME_SIZE);
	trig->irq_ctrl = init_param->irq_ctrl;
	trig->irq_id = init_param->irq_id;
	trig->irq_trig_lvl = init_param->irq_trig_lvl;
	trig->irq_cb = (struct no_os_callback_desc){
		.callback = iio_hw_trig_handler,
		.ctx = trig,
		.event = init_param->cb_info.event,
		.peripheral = init_param->cb_info.peripheral,
		.handle = init_param->cb_info.handle,
	};

	/*
	 * no-OS walks every callback with one shared iterator from the
	 * interrupts; change the list with them off (see iio_trigger_timer.c).
	 */
	no_os_irq_global_disable(trig->irq_ctrl);
	ret = no_os_irq_register_callback(trig->irq_ctrl, trig->irq_id, &trig->irq_cb);
	no_os_irq_global_enable(trig->irq_ctrl);
	if (ret)
		goto free_trig;

	if (init_param->cb_info.event == NO_OS_EVT_GPIO) {
		ret = no_os_irq_trigger_level_set(trig->irq_ctrl, trig->irq_id, trig->irq_trig_lvl);
		if (ret)
			goto unregister_cb;
	}

	*iio_trig = trig;
	return 0;

unregister_cb:
	no_os_irq_global_disable(trig->irq_ctrl);
	no_os_irq_unregister_callback(trig->irq_ctrl, trig->irq_id, &trig->irq_cb);
	no_os_irq_global_enable(trig->irq_ctrl);
free_trig:
	no_os_free(trig);
	return ret;
}

/* no-OS API, a *_trig.c's enable: turn the trigger's interrupt on. */
int iio_trig_enable(void *trig)
{
	struct iio_hw_trig *desc = trig;

	if (!desc)
		return -EINVAL;

	return no_os_irq_enable(desc->irq_ctrl, desc->irq_id);
}

/* no-OS API, a *_trig.c's disable: turn the trigger's interrupt off. */
int iio_trig_disable(void *trig)
{
	struct iio_hw_trig *desc = trig;

	if (!desc)
		return -EINVAL;

	return no_os_irq_disable(desc->irq_ctrl, desc->irq_id);
}

/* The trigger's interrupt: count one tick for the capture loop. */
void iio_hw_trig_handler(void *trig)
{
	struct noos_iio_trigger *t = trigger_by_instance(trig);

	if (t)
		t->fired++;
}

/* no-OS API: undo iio_hw_trig_init(). */
int iio_hw_trig_remove(struct iio_hw_trig *trig)
{
	if (!trig)
		return -EINVAL;

	no_os_irq_global_disable(trig->irq_ctrl);
	no_os_irq_unregister_callback(trig->irq_ctrl, trig->irq_id, &trig->irq_cb);
	no_os_irq_global_enable(trig->irq_ctrl);
	no_os_free(trig);

	return 0;
}

/* no-OS API: create a software trigger, fired by trigger_now. */
int iio_sw_trig_init(struct iio_sw_trig **iio_trig, struct iio_sw_trig_init_param *init_param)
{
	struct iio_sw_trig *trig;

	if (!iio_trig || !init_param || !init_param->name)
		return -EINVAL;

	trig = no_os_calloc(1, sizeof(*trig));
	if (!trig)
		return -ENOMEM;

	strncpy(trig->name, init_param->name, TRIG_MAX_NAME_SIZE);
	*iio_trig = trig;

	return 0;
}

/* trigger_now written by the host: count one tick for the capture loop. */
int iio_sw_trig_handler(void *trig, char *buf, uint32_t len, const struct iio_ch_info *channel,
		intptr_t priv)
{
	struct noos_iio_trigger *t = trigger_by_instance(trig);

	if (!t)
		return -EINVAL;

	t->fired++;
	return 0;
}

/* no-OS API: free a software trigger. */
int iio_trig_remove(struct iio_sw_trig *trig)
{
	if (!trig)
		return -EINVAL;

	no_os_free(trig);
	return 0;
}
