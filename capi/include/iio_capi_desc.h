/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * How an adapter presents a channel where its CAPI class does not say:
 * capi_adc and capi_dac channels have a number (chan_id) and a code
 * layout, not what they measure, whether they are differential or what
 * they are called. An application that knows (the part's datasheet, the
 * board) describes each channel; without, an adapter makes it a voltage
 * channel numbered by its chan_id.
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_DESC_H_
#define CAPI_INCLUDE_IIO_CAPI_DESC_H_

#include <iio_capi_table.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A channel as iio_capi_chan has it (iio_capi_table.h): its type, number
 * (-1 for none), modifier, differential pair (channel the positive input,
 * channel2 the negative one), name, label, address (the part's own, for
 * its attributes' callbacks) and the part's own attributes of this channel
 * only (attrs, nb_attrs: iio_capi_chan's; they get the ext's priv, so they
 * need the adapter's ext). The adapter fills the rest: direction, values,
 * scan layout.
 *
 * A channel's values keep the unit the driver gives them in: capi_adc's
 * scale is mV per code, IIO's unit for a voltage. A channel of another type
 * (current: mA, capacitance: pF, resistance: ohms ...) needs a driver that
 * gives its scale in that type's IIO unit.
 */
struct iio_capi_chan_desc {
	enum iio_capi_chan_type type;
	int channel;
	enum iio_capi_chan_modifier modifier;
	bool differential;
	int channel2;
	const char *name;
	const char *label;
	uintptr_t address;
	const struct iio_capi_attr *attrs;
	unsigned int nb_attrs;
};

/* Put what desc says into chan. */
static inline void iio_capi_chan_describe(struct iio_capi_chan *chan,
		const struct iio_capi_chan_desc *desc)
{
	chan->type = desc->type;
	chan->channel = desc->channel;
	chan->modifier = desc->modifier;
	chan->differential = desc->differential;
	chan->channel2 = desc->channel2;
	chan->name = desc->name;
	chan->label = desc->label;
	chan->address = desc->address;
	chan->attrs = desc->attrs;
	chan->nb_attrs = desc->nb_attrs;
}

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_DESC_H_ */
