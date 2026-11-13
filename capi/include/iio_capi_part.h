/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Serves a part that is more than one CAPI class as one IIO device, as
 * Linux has it: an ADXL355 is a capi_accel and a capi_temp (accel_x/y/z
 * and temp of one device), an AD5592R a capi_adc and a capi_dac (inputs
 * and outputs of one device). Each class is served by its own adapter
 * (iio_capi_accel, iio_capi_temp, iio_capi_adc, iio_capi_dac ...),
 * filled with its _init() call and not registered; the part's device has
 * their channels, in the order of the adapters, and hands each call to
 * the adapter its channel is of.
 *
 *	static struct iio_capi_accel accel;
 *	static struct iio_capi_temp temp;
 *	static struct iio_capi_part part;
 *	struct iio_capi_table_device *members[] = { &accel.table, &temp.table };
 *
 *	iio_capi_accel_init(&accel, accel_handle);
 *	iio_capi_temp_init(&temp, &temp_handle, 1);
 *	iio_capi_part_register(&part, "adxl355", members, 2, &ext);
 *
 * The device has one buffer (an IIO buffer has one direction): an input
 * one when an adapter captures (iio_capi_adc, iio_capi_accel ...), the
 * outputs then written through raw only; else an output one. Its scans
 * are those of the adapters, one after the other. The first adapter that
 * streams (read_scans: a driver with stream_enable ...) paces them; the
 * others are read once per chunk of up to IIO_CAPI_PART_CHUNK of its
 * samples, after it, each sample of the chunk getting that reading (the
 * ADXL355's temperature, which the part samples apart from its axes); an
 * other adapter that could stream is read that way too, its stream left
 * off. Without one that streams, a scan is a scan of each adapter, read
 * one after the other, as fast as the buffer asks.
 *
 * A part whose FIFO holds the data of several of its classes (accel and
 * temp in each entry ...) gives the part a stream of its own instead
 * (iio_capi_part_register_stream(), a struct iio_capi_part_stream from its
 * <part>_iio.c): it paces the scans, and the channels it carries come
 * from it, each scan's values from the same FIFO entry; no adapter
 * streams. Channels it does not carry are read as the others above, once
 * per chunk.
 *
 * A value of the device's own (an accelerometer's device-wide
 * sampling_frequency) is the adapter's that has it. The registers
 * (direct_reg_access) are the first adapter's that has them: the part's
 * classes share them.
 */

#ifndef CAPI_INCLUDE_IIO_CAPI_PART_H_
#define CAPI_INCLUDE_IIO_CAPI_PART_H_

#include <iio_capi_table.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Adapters a part has at most. */
#ifndef IIO_CAPI_PART_MAX_MEMBERS
#define IIO_CAPI_PART_MAX_MEMBERS 4
#endif

/* Channels a part has at most, all its adapters': a table's. */
#define IIO_CAPI_PART_MAX_CHANNELS 32

/* Samples of the adapter that streams read before the others are read. */
#define IIO_CAPI_PART_CHUNK 32

/*
 * A part's own stream: its FIFO, holding channels of several of its
 * adapters. A mask here has bit i for the part's channel i: the adapters'
 * channels, one adapter's after the other's, in the order of members.
 */
struct iio_capi_part_stream {
	void *priv;
	/* The channels the stream carries: input ones in scans (scan_index
	 * >= 0 in their adapter's table) */
	uint32_t mask;
	/* Start the stream with the channels of mask (some of the stream's),
	 * or stop it; NULL when it needs neither */
	int (*enable)(void *priv, uint32_t mask, bool enable);
	/* nb_scans consecutive scans of the channels of mask (some of the
	 * stream's), as the part paces them: each scan the values of those
	 * channels, in the part's channel order, of one FIFO entry. Calls
	 * that follow each other while the buffer is on continue the stream. */
	int (*read_scans)(void *priv, uint32_t mask, int32_t *values, uint32_t nb_scans);
};

/* One part served. Filled by iio_capi_part_register(); keep it, the
 * adapters and what they were given for as long as the context exists. */
struct iio_capi_part {
	struct iio_capi_table_device table;
	struct iio_capi_chan chans[IIO_CAPI_PART_MAX_CHANNELS];
	struct iio_capi_table_ops ops;
	struct iio_capi_table_device *members[IIO_CAPI_PART_MAX_MEMBERS];
	unsigned int nb_members;
	/* Each adapter's first channel in chans */
	unsigned int first[IIO_CAPI_PART_MAX_MEMBERS];
	/* Each adapter's channels in scans (its own mask) */
	uint32_t scanned[IIO_CAPI_PART_MAX_MEMBERS];
	/* The adapter that streams, or -1 */
	int stream;
	/* The part's own stream, or NULL */
	const struct iio_capi_part_stream *part_stream;
	/* The part's channels in scans that the stream (the part's or the
	 * adapter's) gives */
	uint32_t streamed;
	/* The part's own stream is on */
	bool stream_on;
	/* Adapters whose enable_buffer the buffer calls: the one that streams,
	 * and those whose scans need it (an output's); not another that
	 * streams, read a scan at a time */
	uint32_t enables;
	/* Adapters whose buffer is on */
	uint32_t enabled;
};

/*
 * Add the part to the context as the IIO device name: the channels of
 * members (adapters' tables, filled by their _init() calls, in the order
 * their channels are to have; the array is copied). ext is the part's own
 * attributes (on its channels, the device, its buffer, or debug ones),
 * iio_capi_table.h; NULL for none.
 *
 * Returns 0, -EINVAL for missing arguments, no adapter or more than
 * IIO_CAPI_PART_MAX_MEMBERS, more than IIO_CAPI_PART_MAX_CHANNELS
 * channels, two adapters with the same value of the device's own, or
 * what iio_capi_table_register() refuses (two channels of the same id and
 * direction: two temp ...). The adapters' own ext, if they have one, is
 * not served: the part's is.
 */
int iio_capi_part_register(struct iio_capi_part *part, const char *name,
		struct iio_capi_table_device *const *members, unsigned int nb_members,
		const struct iio_capi_ext *ext);

/*
 * iio_capi_part_register(), with the part's own stream: the scans of the
 * channels of stream->mask come from stream (kept as long as the context
 * exists), not from the adapters. Returns what iio_capi_part_register()
 * does, and -EINVAL for no stream->read_scans, no stream->mask, or a
 * channel in it that is not the part's or not an input one in scans.
 */
int iio_capi_part_register_stream(struct iio_capi_part *part, const char *name,
		struct iio_capi_table_device *const *members, unsigned int nb_members,
		const struct iio_capi_ext *ext, const struct iio_capi_part_stream *stream);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIO_CAPI_PART_H_ */
