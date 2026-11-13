/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * A part of several CAPI classes as one table device: each class's adapter
 * fills a table, not registered; this table has their channels, one
 * adapter's after the other's, and hands each call to the adapter the
 * channel is of, with that adapter's own channel and mask.
 */

#include <errno.h>
#include <iio_capi_part.h>
#include <iio_capi_table.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint32_t low_bits(unsigned int n)
{
	return n >= 32 ? ~0u : (1u << n) - 1;
}

/* The adapter chan is of, and its own channel there. */
static unsigned int part_member(const struct iio_capi_part *part, const struct iio_capi_chan *chan,
		const struct iio_capi_chan **own)
{
	unsigned int i = (unsigned int)(chan - part->chans), m = part->nb_members - 1;

	while (m && i < part->first[m])
		m--;

	*own = &part->members[m]->channels[i - part->first[m]];

	return m;
}

/* An adapter's own mask, from the part's. */
static uint32_t part_sub(const struct iio_capi_part *part, unsigned int m, uint32_t mask)
{
	return (mask >> part->first[m]) & low_bits(part->members[m]->nb_channels);
}

/* The adapter whose value of the device's own info is; -1 for none. */
static int part_dev_member(const struct iio_capi_part *part, enum iio_capi_info info)
{
	unsigned int m;

	for (m = 0; m < part->nb_members; m++)
		if (part->members[m]->info & IIO_CAPI_INFO_BIT(info))
			return (int)m;

	return -1;
}

static int part_read_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		int32_t *val, int32_t *val2)
{
	struct iio_capi_part *part = dev;
	const struct iio_capi_table_device *t;
	const struct iio_capi_chan *own = NULL;
	int m = chan ? (int)part_member(part, chan, &own) : part_dev_member(part, info);

	if (m < 0)
		return -EINVAL;

	t = part->members[m];
	if (!t->ops->read_raw)
		return -EINVAL;

	return t->ops->read_raw(t->dev, own, info, val, val2);
}

static int part_write_raw(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		const struct capi_value *value)
{
	struct iio_capi_part *part = dev;
	const struct iio_capi_table_device *t;
	const struct iio_capi_chan *own = NULL;
	int m = chan ? (int)part_member(part, chan, &own) : part_dev_member(part, info);

	if (m < 0)
		return -EPERM;

	t = part->members[m];
	if (!t->ops->write_raw)
		return -EPERM;

	return t->ops->write_raw(t->dev, own, info, value);
}

static int part_read_avail(void *dev, const struct iio_capi_chan *chan, enum iio_capi_info info,
		struct capi_value *values, uint32_t *nb)
{
	struct iio_capi_part *part = dev;
	const struct iio_capi_table_device *t;
	const struct iio_capi_chan *own = NULL;
	int m = chan ? (int)part_member(part, chan, &own) : part_dev_member(part, info);

	if (m < 0)
		return -EINVAL;

	t = part->members[m];
	if (!t->ops->read_avail)
		return -EINVAL;

	return t->ops->read_avail(t->dev, own, info, values, nb);
}

/* The part's own stream on, with its channels in mask, or off. */
static int part_stream_enable(struct iio_capi_part *part, uint32_t mask, bool enable)
{
	const struct iio_capi_part_stream *ps = part->part_stream;
	bool on = enable && (mask & ps->mask);
	int ret;

	if (!on && !part->stream_on)
		return 0;

	ret = ps->enable(ps->priv, mask & ps->mask, on);
	if (on && ret)
		return ret;
	part->stream_on = on;

	return on ? 0 : ret;
}

/* On: the part's own stream and each adapter with channels in mask
 * (those the part's stream does not carry), with its own; off: the others
 * (all of them, for the buffer off). */
static int part_enable_buffer(void *dev, uint32_t mask, bool enable)
{
	struct iio_capi_part *part = dev;
	const struct iio_capi_part_stream *ps = part->part_stream;
	uint32_t own = ps ? mask & ~ps->mask : mask;
	unsigned int m;
	int ret, err = 0;

	if (ps && ps->enable) {
		err = part_stream_enable(part, mask, enable);
		if (enable && err)
			return err;
	}

	for (m = 0; m < part->nb_members; m++) {
		const struct iio_capi_table_device *t = part->members[m];
		uint32_t sub = part_sub(part, m, own), bit = 1u << m;
		bool on = enable && sub;

		if (!(part->enables & bit) || (!on && !(part->enabled & bit)))
			continue;

		ret = t->ops->enable_buffer(t->dev, sub, on);
		if (on && ret)
			return ret;

		if (on) {
			part->enabled |= bit;
		} else {
			part->enabled &= ~bit;
			if (ret)
				err = ret;
		}
	}

	return err;
}

/* One scan of each adapter with channels in mask, one after the other:
 * the part's scan. Returns how many values that is, or an error code. */
static int part_read_once(struct iio_capi_part *part, uint32_t mask, int32_t *values)
{
	unsigned int m, k = 0;
	int ret;

	for (m = 0; m < part->nb_members; m++) {
		const struct iio_capi_table_device *t = part->members[m];
		uint32_t sub = part_sub(part, m, mask);

		if (!sub)
			continue;

		ret = t->ops->read_scan(t->dev, sub, &values[k]);
		if (ret)
			return ret;
		k += (unsigned int)__builtin_popcount(sub);
	}

	return (int)k;
}

static int part_read_scan(void *dev, uint32_t mask, int32_t *values)
{
	int ret = part_read_once(dev, mask, values);

	return ret < 0 ? ret : 0;
}

/* nb scans of the stream's channels in mask (some of part->streamed): the
 * part's own stream's, or the streaming adapter's. */
static int part_stream_read(struct iio_capi_part *part, uint32_t mask, int32_t *values,
		uint32_t nb)
{
	const struct iio_capi_part_stream *ps = part->part_stream;
	const struct iio_capi_table_device *st;

	if (ps)
		return ps->read_scans(ps->priv, mask, values, nb);

	st = part->members[part->stream];

	return st->ops->read_scans(st->dev, part_sub(part, (unsigned int)part->stream, mask), values,
			nb);
}

/*
 * nb scans (up to IIO_CAPI_PART_CHUNK) into values: the stream's samples,
 * read into the end of values, then one scan of the channels it does not
 * give, put with each sample in its place. Scan j, written at
 * j * nb_values, ends where sample j + 1 starts at the earliest, so the
 * samples not yet placed are not written over.
 */
static int part_read_chunk(struct iio_capi_part *part, uint32_t mask, int32_t *values,
		uint32_t nb, uint32_t nb_values)
{
	uint32_t st_mask = mask & part->streamed;
	uint32_t nb_st = (uint32_t)__builtin_popcount(st_mask), j;
	int32_t others[IIO_CAPI_PART_MAX_CHANNELS], sample[IIO_CAPI_PART_MAX_CHANNELS];
	int32_t *samples = &values[nb * (nb_values - nb_st)];
	int ret;

	ret = part_stream_read(part, st_mask, samples, nb);
	if (ret)
		return ret;

	ret = part_read_once(part, mask & ~part->streamed, others);
	if (ret < 0)
		return ret;

	for (j = 0; j < nb; j++) {
		int32_t *scan = &values[j * nb_values];
		uint32_t bits = mask;
		unsigned int k = 0, ks = 0, ko = 0;

		memcpy(sample, &samples[j * nb_st], nb_st * sizeof(*sample));

		/* The channels in order, each from the stream or the others */
		while (bits) {
			unsigned int i = (unsigned int)__builtin_ctz(bits);

			scan[k++] = st_mask & (1u << i) ? sample[ks++] : others[ko++];
			bits &= bits - 1;
		}
	}

	return 0;
}

static int part_read_scans(void *dev, uint32_t mask, int32_t *values, uint32_t nb_scans)
{
	struct iio_capi_part *part = dev;
	uint32_t nb_values = (uint32_t)__builtin_popcount(mask), done, nb, j;
	int ret;

	/* All of them from the stream: its scans as they are */
	if (!(mask & ~part->streamed))
		return part_stream_read(part, mask, values, nb_scans);

	/* None of them from the stream: the others' scans, as often as scans
	 * are asked for */
	if (!(mask & part->streamed)) {
		for (j = 0; j < nb_scans; j++) {
			ret = part_read_scan(part, mask, &values[j * nb_values]);
			if (ret)
				return ret;
		}
		return 0;
	}

	for (done = 0; done < nb_scans; done += nb) {
		nb = nb_scans - done < IIO_CAPI_PART_CHUNK ? nb_scans - done : IIO_CAPI_PART_CHUNK;

		ret = part_read_chunk(part, mask, &values[done * nb_values], nb, nb_values);
		if (ret)
			return ret;
	}

	return 0;
}

static int part_write_scan(void *dev, uint32_t mask, const int32_t *values)
{
	struct iio_capi_part *part = dev;
	unsigned int m, k = 0;
	int ret;

	for (m = 0; m < part->nb_members; m++) {
		const struct iio_capi_table_device *t = part->members[m];
		uint32_t sub = part_sub(part, m, mask);

		if (!sub)
			continue;

		ret = t->ops->write_scan(t->dev, sub, &values[k]);
		if (ret)
			return ret;
		k += (unsigned int)__builtin_popcount(sub);
	}

	return 0;
}

/* One part: its registers are those of the first adapter that has them. */
static int part_reg_access(void *dev, uint32_t reg, uint32_t writeval, uint32_t *readval)
{
	struct iio_capi_part *part = dev;
	unsigned int m;

	for (m = 0; m < part->nb_members; m++) {
		const struct iio_capi_table_device *t = part->members[m];

		if (t->ops->reg_access)
			return t->ops->reg_access(t->dev, reg, writeval, readval);
	}

	return -ENODEV;
}

static bool member_reads(const struct iio_capi_table_device *t)
{
	return t->ops->read_scan || t->ops->read_scans;
}

/* One buffer, of one direction: an input one when an adapter captures
 * channels of its own. */
static bool part_input(struct iio_capi_table_device *const *members, unsigned int nb_members)
{
	unsigned int m, i;

	for (m = 0; m < nb_members; m++) {
		if (!member_reads(members[m]))
			continue;
		for (i = 0; i < members[m]->nb_channels; i++)
			if (!members[m]->channels[i].output && members[m]->channels[i].scan_index >= 0)
				return true;
	}

	return false;
}

/* Which of the adapters' channels the part's scans have, as their masks,
 * which adapter streams (none with the part's own stream), and which of
 * the part's channels the stream gives. */
static void part_scans(struct iio_capi_part *part, bool input)
{
	const struct iio_capi_part_stream *ps = part->part_stream;
	unsigned int m, i;

	part->stream = -1;
	part->enables = 0;
	part->streamed = 0;

	for (m = 0; m < part->nb_members; m++) {
		const struct iio_capi_table_device *t = part->members[m];
		/* Its channels the part's stream carries */
		uint32_t carried = ps ? part_sub(part, m, ps->mask) : 0, own;
		bool takes;

		part->scanned[m] = 0;

		if (!ps && input && t->ops->read_scans && part->stream < 0)
			takes = true;
		else if (input)
			takes = !!t->ops->read_scan;
		else
			takes = !!t->ops->write_scan;

		for (i = 0; i < t->nb_channels; i++)
			if (t->channels[i].scan_index >= 0 && t->channels[i].output != input &&
					(takes || (carried & (1u << i))))
				part->scanned[m] |= 1u << i;

		if (!ps && part->scanned[m] && input && t->ops->read_scans && part->stream < 0) {
			part->stream = (int)m;
			part->streamed = part->scanned[m] << part->first[m];
		}
		part->streamed |= carried << part->first[m];

		/* Its enable_buffer starts what its scans come from: its stream,
		 * for the one that streams; not for another one that streams,
		 * whose scans are read_scan()'s; not for channels the part's
		 * stream carries */
		own = part->scanned[m] & ~carried;
		if (own && t->ops->enable_buffer &&
				(!input || (int)m == part->stream || !t->ops->read_scans))
			part->enables |= 1u << m;
	}
}

/* The part's stream carries channels of the part that are input ones in
 * scans. */
static bool part_stream_valid(const struct iio_capi_part *part, unsigned int nb,
		const struct iio_capi_part_stream *ps)
{
	unsigned int m, i;

	if (!ps->read_scans || !ps->mask || (ps->mask & ~low_bits(nb)))
		return false;

	for (m = 0; m < part->nb_members; m++) {
		const struct iio_capi_table_device *t = part->members[m];
		uint32_t carried = part_sub(part, m, ps->mask);

		for (i = 0; i < t->nb_channels; i++)
			if ((carried & (1u << i)) &&
					(t->channels[i].scan_index < 0 || t->channels[i].output))
				return false;
	}

	return true;
}

int iio_capi_part_register(struct iio_capi_part *part, const char *name,
		struct iio_capi_table_device *const *members, unsigned int nb_members,
		const struct iio_capi_ext *ext)
{
	return iio_capi_part_register_stream(part, name, members, nb_members, ext, NULL);
}

int iio_capi_part_register_stream(struct iio_capi_part *part, const char *name,
		struct iio_capi_table_device *const *members, unsigned int nb_members,
		const struct iio_capi_ext *ext, const struct iio_capi_part_stream *stream)
{
	unsigned int m, i, nb = 0;
	uint32_t info = 0, info_avail = 0;
	bool input, scans = false, regs = false;
	int scan = 0;

	if (!part || !name || !members || !nb_members || nb_members > IIO_CAPI_PART_MAX_MEMBERS)
		return -EINVAL;

	for (m = 0; m < nb_members; m++) {
		const struct iio_capi_table_device *t = members[m];

		if (!t || !t->ops || (t->nb_channels && !t->channels) ||
				t->nb_channels > IIO_CAPI_PART_MAX_CHANNELS - nb)
			return -EINVAL;

		/* A value of the device's own is one adapter's */
		if (info & t->info)
			return -EINVAL;
		info |= t->info;
		info_avail |= t->info_avail;

		part->members[m] = members[m];
		part->first[m] = nb;
		nb += t->nb_channels;
	}
	part->nb_members = nb_members;
	part->enabled = 0;
	part->part_stream = stream;
	part->stream_on = false;

	if (stream && !part_stream_valid(part, nb, stream))
		return -EINVAL;

	input = stream || part_input(members, nb_members);
	part_scans(part, input);

	/* The adapters' channels, in order, numbered again in scans */
	for (m = 0; m < nb_members; m++) {
		const struct iio_capi_table_device *t = members[m];

		for (i = 0; i < t->nb_channels; i++) {
			struct iio_capi_chan *chan = &part->chans[part->first[m] + i];

			*chan = t->channels[i];
			chan->scan_index = part->scanned[m] & (1u << i) ? scan++ : -1;
		}

		if (part->scanned[m])
			scans = true;
		regs |= !!t->ops->reg_access;
	}

	part->ops = (struct iio_capi_table_ops){
		.read_raw = part_read_raw,
		.write_raw = part_write_raw,
		.read_avail = part_read_avail,
		.enable_buffer = part->enables || (stream && stream->enable) ? part_enable_buffer : NULL,
		.read_scan = scans && input && part->stream < 0 && !stream ? part_read_scan : NULL,
		.read_scans = part->stream >= 0 || stream ? part_read_scans : NULL,
		.write_scan = scans && !input ? part_write_scan : NULL,
		.reg_access = regs ? part_reg_access : NULL,
	};

	part->table = (struct iio_capi_table_device){
		.name = name,
		.dev = part,
		.channels = part->chans,
		.nb_channels = nb,
		.ops = &part->ops,
		.info = info,
		.info_avail = info_avail,
		.ext = ext,
	};

	return iio_capi_table_register(&part->table);
}
