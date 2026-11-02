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
};

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
	/* Number among the channels of its type (voltage<channel>) */
	int channel;
	bool output;
	/* Its values, IIO_CAPI_INFO_BIT()s */
	uint32_t info;
	/* Place in a scan, rising with the table order; -1 for a channel that
	 * is not in scans */
	int scan_index;
	struct iio_capi_scan_format scan;
};

/*
 * What the table's device does, with dev its handle. All are optional; a
 * device with read_scan has an input buffer, one with write_scan an output
 * buffer. mask has bit n for channels[n]; values has one value per channel
 * in mask, in table order.
 */
struct iio_capi_table_ops {
	/* One value of a channel; returns its enum iio_capi_val or a negative
	 * error code */
	int (*read_raw)(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
			int32_t *val, int32_t *val2);
	/* Set one, given as val + val2 / 1000000 */
	int (*write_raw)(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
			int32_t val, int32_t val2);
	/* Buffer on, with these channels, or off */
	int (*enable_buffer)(void *dev, uint32_t mask, bool enable);
	/* Read or write one scan */
	int (*read_scan)(void *dev, uint32_t mask, int32_t *values);
	int (*write_scan)(void *dev, uint32_t mask, const int32_t *values);
};

/* A table device. Fill name, dev, channels, nb_channels and ops; the rest
 * is backend_table.c's. */
struct iio_capi_table_device {
	/* IIO device name the host sees */
	const char *name;
	void *dev;
	const struct iio_capi_chan *channels;
	unsigned int nb_channels;
	const struct iio_capi_table_ops *ops;

	struct iio_capi_device iio;
	struct iio_capi_device_api api;
	/* The buffer is on, with these channels */
	bool enabled;
	uint32_t mask;
};

/* Add the device to the context, as iio_capi_device_register(). Returns 0,
 * -EINVAL for a table it cannot serve (no name or ops, more than 32
 * channels, scan_index not rising, an unknown type, a storage that is not
 * 8, 16 or 32 bits), or -EEXIST for a device already registered. */
int iio_capi_table_register(struct iio_capi_table_device *tdev);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_TABLE_H_ */
