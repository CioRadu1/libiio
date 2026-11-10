/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * A device described by a table of channels, as a Linux IIO driver
 * describes its own: each channel's type, number, direction, scan format and
 * the values it has (raw, scale ...). Its callbacks deal in numbers only;
 * backend_table.c makes the libiio channels and attributes, the text of the
 * values and the samples of a buffer. The class adapters (iio_capi_adc ...)
 * build such a table from a CAPI handle.
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_TABLE_H_
#define CAPI_INCLUDE_IIO_CAPI_TABLE_H_

#include <capi_value.h>
#include <iio_device.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A channel's type: the prefix of its id (voltage0, temp ...). */
enum iio_capi_chan_type {
	IIO_CAPI_VOLTAGE,
	IIO_CAPI_CURRENT,
	IIO_CAPI_POWER,
	IIO_CAPI_TEMP,
	IIO_CAPI_ACCEL,
	IIO_CAPI_ANGL_VEL,
	IIO_CAPI_MAGN,
	IIO_CAPI_PRESSURE,
	IIO_CAPI_HUMIDITY,
	IIO_CAPI_RESISTANCE,
	IIO_CAPI_CAPACITANCE,
	IIO_CAPI_COUNT,
};

/* What a channel measures within its type, as IIO's modifiers name it:
 * an axis (accel_x ...), a temperature's ambient or object. */
enum iio_capi_chan_modifier {
	IIO_CAPI_MOD_NONE,
	IIO_CAPI_MOD_X,
	IIO_CAPI_MOD_Y,
	IIO_CAPI_MOD_Z,
	IIO_CAPI_MOD_TEMP_AMBIENT,
	IIO_CAPI_MOD_TEMP_OBJECT,
};

/* The values a channel can have, each an attribute of it. */
enum iio_capi_info {
	/* "raw": the converter's code */
	IIO_CAPI_INFO_RAW,
	/* "input": the value in its unit */
	IIO_CAPI_INFO_PROCESSED,
	/* "scale": raw times scale is the value in its unit */
	IIO_CAPI_INFO_SCALE,
	/* "offset": added to raw before the scale */
	IIO_CAPI_INFO_OFFSET,
	/* "sampling_frequency", in Hz */
	IIO_CAPI_INFO_SAMP_FREQ,
	/* "calibbias": the calibration offset the part applies */
	IIO_CAPI_INFO_CALIBBIAS,
	/* "filter_high_pass_3db_frequency", in Hz */
	IIO_CAPI_INFO_HPF_3DB,
	/* "oversampling_ratio": conversions averaged into a reading */
	IIO_CAPI_INFO_OVERSAMPLING_RATIO,
	/* "thermocouple_type": a letter, written and read as one
	 * (IIO_CAPI_VAL_CHAR) */
	IIO_CAPI_INFO_THERMOCOUPLE_TYPE,
};

#define IIO_CAPI_INFO_BIT(_info) (1u << (_info))

/* How a value read_raw gives is written as text, as in Linux. */
enum iio_capi_val {
	/* val */
	IIO_CAPI_VAL_INT,
	/* val + val2 / 1000000 */
	IIO_CAPI_VAL_INT_PLUS_MICRO,
	/* val + val2 / 1000000000 */
	IIO_CAPI_VAL_INT_PLUS_NANO,
	/* val / val2 */
	IIO_CAPI_VAL_FRACTIONAL,
	/* val / 2^val2 */
	IIO_CAPI_VAL_FRACTIONAL_LOG2,
	/* the character val */
	IIO_CAPI_VAL_CHAR,
};

/* A driver's capi_value as read_raw gives a value: val and val2 as they
 * are, and its enum iio_capi_val, or -EINVAL for an unknown type. */
int iio_capi_value(const struct capi_value *value, int32_t *val, int32_t *val2);

/* How a channel's sample is stored in a scan. */
struct iio_capi_scan_format {
	bool is_signed;
	/* bits of the value */
	uint8_t bits;
	/* bits the sample takes: 8, 16, 32 */
	uint8_t storage;
	/* the value sits this many bits up */
	uint8_t shift;
	bool is_big_endian;
};

struct iio_capi_chan {
	enum iio_capi_chan_type type;
	/* Number among the channels of its type; -1 for none. With the
	 * modifier, the id is as IIO names a channel: <type><channel>
	 * (voltage0), <type> (temp), <type>_<modifier> (accel_x,
	 * temp_ambient), <type><channel>_<modifier> (temp1_object) */
	int channel;
	enum iio_capi_chan_modifier modifier;
	bool output;
	/* Its values, IIO_CAPI_INFO_BIT()s */
	uint32_t info;
	/* Those of them with a list of the values they take, as an
	 * <attribute>_available attribute (IIO_CAPI_INFO_BIT()s) */
	uint32_t info_avail;
	/* Place in a scan, rising with the table order; -1 for a channel that
	 * is not in scans */
	int scan_index;
	struct iio_capi_scan_format scan;
	/* A differential channel, as IIO names one: channel is its positive
	 * input, channel2 its negative one, the id <type><channel>-
	 * <type><channel2> (voltage0-voltage1). Numbered (channel and channel2
	 * from 0) and without a modifier. channel2 is ignored else. */
	bool differential;
	int channel2;
	/* What clients show as its name and label (iio_info's "voltage0:
	 * <name>", "label: ..."); NULL for none. name is no-OS's
	 * iio_channel.name, label Linux's in_<id>_label. */
	const char *name;
	const char *label;
	/* The part's own, for the callbacks of its attributes (chan->address),
	 * as no-OS's and Linux's channel address: a register, an input's
	 * number ...; the table does not use it. */
	uintptr_t address;
};

/*
 * What the table's device does, with dev its handle. read_raw, write_raw
 * and read_avail get chan NULL for a value of the device's own (info,
 * info_avail). All are optional; a
 * device with read_scan or read_scans has an input buffer, one with
 * write_scan an output buffer. mask has bit n for channels[n]; values has
 * one value per channel in mask, in table order.
 */
struct iio_capi_table_ops {
	/* One value of a channel; returns its enum iio_capi_val or a negative
	 * error code */
	int (*read_raw)(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
			int32_t *val, int32_t *val2);
	/* Set one, given as a CAPI_VALUE_INT_PLUS_NANO: the text written,
	 * with up to 9 decimals; for a value that is a character, its
	 * CAPI_VALUE_INT */
	int (*write_raw)(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
			const struct capi_value *value);
	/* The values one in info_avail takes: in: the room in values; out:
	 * how many there are */
	int (*read_avail)(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
			struct capi_value *values, uint32_t *nb);
	/* Buffer on, with these channels, or off */
	int (*enable_buffer)(void *dev, uint32_t mask, bool enable);
	/* Read or write one scan */
	int (*read_scan)(void *dev, uint32_t mask, int32_t *values);
	int (*write_scan)(void *dev, uint32_t mask, const int32_t *values);
	/* Read nb_scans consecutive scans, one after the other in values, as
	 * the device paces them (its sampling frequency); a buffer's scans
	 * then come from here rather than read_scan. Calls that follow each
	 * other while the buffer is on continue the same stream. */
	int (*read_scans)(void *dev, uint32_t mask, int32_t *values, uint32_t nb_scans);
	/* Register access, as iio_capi_ext's reg_access (which, given, is
	 * served instead): what a class adapter has from its CAPI class's
	 * reg_access op */
	int (*reg_access)(void *dev, uint32_t reg, uint32_t writeval, uint32_t *readval);
};

/* The scan format of bits-bit samples a part's registers hold shift bits
 * up, big-endian or not: stored in the smallest of 8, 16, 32 bits that
 * holds them. */
static inline struct iio_capi_scan_format iio_capi_scan_of(uint8_t bits, bool is_signed,
		uint8_t shift, bool big_endian)
{
	unsigned int used = (unsigned int)bits + shift;

	return (struct iio_capi_scan_format){
		.is_signed = is_signed,
		.bits = bits,
		.storage = used <= 8 ? 8 : used <= 16 ? 16 : 32,
		.shift = shift,
		.is_big_endian = big_endian,
	};
}

/* Where a part's own attribute is, as in Linux: on the table's channels
 * (those of chan_types); on the device; on its buffer (buffer0, for a table
 * that has one); or a debug attribute (Linux's debugfs ones, iio_attr -D),
 * beside direct_reg_access. */
enum iio_capi_attr_scope {
	IIO_CAPI_ATTR_CHANNEL,
	IIO_CAPI_ATTR_DEVICE,
	IIO_CAPI_ATTR_BUFFER,
	IIO_CAPI_ATTR_DEBUG,
};

/* A channel type's bit in iio_capi_attr.chan_types. */
#define IIO_CAPI_CHAN_TYPE_BIT(_type) (1u << (_type))

/*
 * An attribute a part has beyond its class's values (no-OS's and Linux's
 * ext_info), either as a number or as text:
 *
 * - read / write: a value, written and parsed as the class's values are
 *   (iio_capi_table_ops' read_raw, write_raw). read fills value, of any
 *   capi_value type; write gets the text written as a
 *   CAPI_VALUE_INT_PLUS_NANO, up to 9 decimals (iio_capi_value_int() for
 *   a whole number). Both return 0. With read_avail, it also has a
 *   <name>_available attribute, read-only: the values it takes, a space
 *   apart, as read_avail lists them (in: the room in values, out: how
 *   many there are; 0 or a negative error code).
 * - show / store: the text itself. show writes it into buf, len bytes, and
 *   returns its length; store gets the text written, NUL-terminated, and
 *   returns 0.
 *
 * One pair or the other: without the setter it is read-only, without the
 * getter write-only. chan is the channel, NULL for another scope's; arg
 * is the attribute's, so one pair of callbacks can serve several (as
 * no-OS's iio_attribute.priv). All return a negative error code.
 */
struct iio_capi_attr {
	const char *name;
	enum iio_capi_attr_scope scope;
	/* For a channel attribute, the types of the channels it is on
	 * (IIO_CAPI_CHAN_TYPE_BIT()s); 0 for every channel. Two of the same
	 * name are on channels of different types. 0 for another scope. */
	uint32_t chan_types;
	intptr_t arg;
	int (*read)(void *priv, const struct iio_capi_chan *chan, intptr_t arg,
			struct capi_value *value);
	int (*write)(void *priv, const struct iio_capi_chan *chan, intptr_t arg,
			const struct capi_value *value);
	int (*read_avail)(void *priv, const struct iio_capi_chan *chan, intptr_t arg,
			struct capi_value *values, uint32_t *nb);
	int (*show)(void *priv, const struct iio_capi_chan *chan, intptr_t arg, char *buf,
			size_t len);
	int (*store)(void *priv, const struct iio_capi_chan *chan, intptr_t arg, const char *buf);
};

/* The whole number a written value is (write's value), from min to max:
 * 0, or -EINVAL for one with a fraction or out of range. */
int iio_capi_value_int(const struct capi_value *value, int32_t min, int32_t max, int32_t *out);

/*
 * What a part adds to its class's device, as a Linux driver's iio_info
 * does: its own attributes and its register access, each optional; priv
 * is what they get.
 *
 * reg_access is Linux's debugfs_reg_access: the debug attribute
 * direct_reg_access (iio_reg) serves it. With readval, it reads reg into
 * *readval; without (NULL), it writes writeval to reg. It returns 0 or a
 * negative error code (-EPERM for a register it does not write ...). A
 * part with registers only fills priv and reg_access. A class adapter
 * already serves its CAPI class's reg_access op (ops' reg_access): this
 * one is for a part without it, or to serve another one instead.
 */
struct iio_capi_ext {
	void *priv;
	const struct iio_capi_attr *attrs;
	unsigned int nb_attrs;
	int (*reg_access)(void *priv, uint32_t reg, uint32_t writeval, uint32_t *readval);
};

/* A table device. Fill name, dev, channels, nb_channels and ops, and ext
 * for a part with attributes of its own; the rest is backend_table.c's. */
struct iio_capi_table_device {
	/* IIO device name the host sees */
	const char *name;
	void *dev;
	const struct iio_capi_chan *channels;
	unsigned int nb_channels;
	const struct iio_capi_table_ops *ops;
	/* The device's own values (IIO_CAPI_INFO_BIT()s), as device
	 * attributes: one that applies to all its channels; and those of
	 * them with an
	 * <attribute>_available list */
	uint32_t info;
	uint32_t info_avail;
	/* The part's own attributes and registers, after the values; NULL
	 * for none */
	const struct iio_capi_ext *ext;

	struct iio_capi_device iio;
	struct iio_capi_device_api api;
	/* The buffer is on, with these channels */
	bool enabled;
	uint32_t mask;
};

/* Add the device to the context, as iio_capi_device_register(). Returns 0,
 * -EINVAL for a table it cannot serve (no name or ops, more than 32
 * channels, scan_index not rising, an unknown type or modifier, a storage
 * that is not 8, 16 or 32 bits, a shift that leaves no room for the bits,
 * a differential channel without numbers or with a modifier, two channels
 * of the same id and direction,
 * an ext attribute without a name, with no callbacks, with both a number's
 * and text's, with read_avail on a text one, with chan_types of unknown
 * types or on another scope than a channel one, on the buffer of a table
 * without one, or whose name (or <name>_available) is one the channels or
 * the device it is on already have: a value's, another ext attribute's,
 * direct_reg_access), or -EEXIST for a device already registered. */
int iio_capi_table_register(struct iio_capi_table_device *tdev);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_TABLE_H_ */
