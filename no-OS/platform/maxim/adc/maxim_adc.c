/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * The MAX32 parts' own ADC (every part the MSDK's adc.h covers, revA or
 * revB), as the standard ADC channels (iio_channels.h): voltage0 ...
 * voltage<IIO_ADC_NUM_CHANNELS - 1>, each with raw and scale, one LSB in mV
 * of the reference in use. On revB the reference can be switched
 * (reference, reference_available): the internal one, and the external one
 * when the board says its voltage (IIO_ADC_EXT_REF_VOLTAGE_MV).
 */

#include <errno.h>
#include <stdint.h>

#include <no_os_util.h>

#include <iio_channels.h>
#include <noos_iiod.h>

#include "maxim_adc.h"

/* The part's own ADC, e.g. "max32690-adc": platform.cmake sets it per TARGET. */
#ifndef IIO_ADC_NAME
#define IIO_ADC_NAME	"maxim-adc"
#endif

#define ADC_POLL_TIMEOUT 1000000

#if MAXIM_ADC_REVB

static int maxim_adc_init(void)
{
	mxc_adc_req_t adc_cfg = {
		.clock = IIO_ADC_CLOCK,
		.clkdiv = MXC_ADC_CLKDIV_4,
		.cal = MXC_ADC_SKIP_CAL,
		.ref = MXC_ADC_REF_INT_1V25,
		.trackCount = 4,
		.idleCount = 17,
	};

	return MXC_ADC_Init(&adc_cfg);
}

static int maxim_adc_convert(unsigned int channel, int *value)
{
	mxc_adc_slot_req_t slot_req = { 0 };
	mxc_adc_conversion_req_t conv_req = {
		.mode = MXC_ADC_ATOMIC_CONV,
		.trig = MXC_ADC_TRIG_SOFTWARE,
		.fifo_format = MXC_ADC_DATA,
		.fifo_threshold = 0,
		.avg_number = MXC_ADC_AVG_1,
		.num_slots = 1,
	};
	int fifo[MAX_ADC_FIFO_LEN];
	volatile uint32_t timeout;
	int ret;

	if (channel >= IIO_ADC_NUM_CHANNELS)
		return -EINVAL;

	slot_req.channel = MXC_ADC_CH_0 + channel;

	MXC_ADC_Clear_ChannelSelect();

	ret = MXC_ADC_SlotConfiguration(&slot_req, 0);
	if (ret)
		goto out;

	ret = MXC_ADC_Configuration(&conv_req);
	if (ret)
		goto out;

	ret = MXC_ADC_StartConversion();
	if (ret)
		goto out;

	timeout = ADC_POLL_TIMEOUT;
	while (!(MXC_ADC_GetFlags() & MXC_F_ADC_INTFL_SEQ_DONE)) {
		if (--timeout == 0) {
			ret = -ETIMEDOUT;
			goto out_disable;
		}
	}

	MXC_ADC_ClearFlags(MXC_F_ADC_INTFL_SEQ_DONE);
	MXC_ADC_DisableConversion();

	/*
	 * MXC_ADC_GetData() copies every word the FIFO holds, and it can hold
	 * more than the one conversion asked for: give it room for all of them
	 * and keep the first, or the extra words land past the caller's int.
	 * The conversion is stopped first so the level cannot grow meanwhile.
	 */
	if (MXC_ADC_FIFO_Level() > MAX_ADC_FIFO_LEN)
		return -EIO;

	ret = MXC_ADC_GetData(fifo);
	if (ret <= 0)
		return ret ? ret : -EIO;

	*value = fifo[0] & MAXIM_ADC_RAW_MASK;

	return 0;

out_disable:
	MXC_ADC_DisableConversion();
out:
	return ret;
}

static int maxim_adc_select_reference(bool external)
{
	if (MXC_ADC_ReferenceSelect(external ? MXC_ADC_REF_EXT :
				    MXC_ADC_REF_INT_1V25))
		return -EINVAL;

	return 0;
}

#else /* revA */

static int maxim_adc_init(void)
{
	int ret;

	ret = MXC_ADC_Init();
	if (ret)
		return ret;

	MXC_ADC_SetDataAlignment(0);

	return 0;
}

static int maxim_adc_convert(unsigned int channel, int *value)
{
	int raw;

	if (channel >= IIO_ADC_NUM_CHANNELS)
		return -EINVAL;

	raw = MXC_ADC_StartConversion(MXC_ADC_CH_0 + channel);
	if (raw < 0)
		return raw;

	*value = raw & MAXIM_ADC_RAW_MASK;

	return 0;
}

#endif /* MAXIM_ADC_REVB */

#if MAXIM_ADC_HAS_REFERENCE

/*
 * The shared names of the ADC references, by number, so that a reference
 * means the same to a host whatever ADC is behind it, on this port and on
 * Zephyr's;
 * the part has the ones its mask holds, the others are -ENOTSUP.
 */
enum maxim_adc_reference {
	MAXIM_ADC_REF_VDD,
	MAXIM_ADC_REF_VDD_1_2,
	MAXIM_ADC_REF_VDD_1_3,
	MAXIM_ADC_REF_VDD_1_4,
	MAXIM_ADC_REF_INTERNAL,
	MAXIM_ADC_REF_EXTERNAL0,
	MAXIM_ADC_REF_EXTERNAL1,
};

static const char *const maxim_adc_references[] = {
	[MAXIM_ADC_REF_VDD]       = "VDD",
	[MAXIM_ADC_REF_VDD_1_2]   = "VDD/2",
	[MAXIM_ADC_REF_VDD_1_3]   = "VDD/3",
	[MAXIM_ADC_REF_VDD_1_4]   = "VDD/4",
	[MAXIM_ADC_REF_INTERNAL]  = "Internal",
	[MAXIM_ADC_REF_EXTERNAL0] = "External0",
	[MAXIM_ADC_REF_EXTERNAL1] = "External1",
};

#ifdef IIO_ADC_EXT_REF_VOLTAGE_MV
#define MAXIM_ADC_REFERENCES	(NO_OS_BIT(MAXIM_ADC_REF_INTERNAL) |	\
				 NO_OS_BIT(MAXIM_ADC_REF_EXTERNAL0))
#else
#define MAXIM_ADC_REFERENCES	NO_OS_BIT(MAXIM_ADC_REF_INTERNAL)
#endif

#endif /* MAXIM_ADC_HAS_REFERENCE */

#define MAXIM_ADC_SCAN_TYPE {						\
	.sign = 'u',							\
	.realbits = MAXIM_ADC_RESOLUTION_BITS,				\
	.storagebits = 16,						\
	.endianness = NOOS_IIO_BE,					\
}

struct maxim_adc {
	const char *trigger;
	bool external_ref;
	struct noos_iio_chan channels[IIO_ADC_NUM_CHANNELS];
};

static struct maxim_adc maxim_adc;

/* The reference in use, in mV. */
static int maxim_adc_ref_mv(const struct maxim_adc *adc)
{
#ifdef IIO_ADC_EXT_REF_VOLTAGE_MV
	if (adc->external_ref)
		return IIO_ADC_EXT_REF_VOLTAGE_MV;
#endif

	return IIO_ADC_REF_VOLTAGE_MV;
}

#if MAXIM_ADC_HAS_REFERENCE

static int maxim_adc_get_reference(void *dev, const struct noos_iio_chan *ch)
{
	struct maxim_adc *adc = dev;

	return adc->external_ref ? MAXIM_ADC_REF_EXTERNAL0 :
	       MAXIM_ADC_REF_INTERNAL;
}

/* One reference for the whole ADC, whichever input it is set through. */
static int maxim_adc_set_reference(void *dev, const struct noos_iio_chan *ch,
				   unsigned int idx)
{
	struct maxim_adc *adc = dev;
	bool external = idx == MAXIM_ADC_REF_EXTERNAL0;
	int ret;

	ret = maxim_adc_select_reference(external);
	if (ret)
		return ret;

	adc->external_ref = external;

	return 0;
}

static const struct noos_iio_enum maxim_adc_reference_enum = {
	.items = maxim_adc_references,
	.nb_items = NO_OS_ARRAY_SIZE(maxim_adc_references),
	.mask = MAXIM_ADC_REFERENCES,
	.get = maxim_adc_get_reference,
	.set = maxim_adc_set_reference,
};

static const struct noos_iio_ext_attr maxim_adc_ext[] = {
	NOOS_IIO_ENUM("reference", &maxim_adc_reference_enum),
	NOOS_IIO_ENUM_AVAILABLE("reference", &maxim_adc_reference_enum),
	{ 0 },
};

#endif /* MAXIM_ADC_HAS_REFERENCE */

static int maxim_adc_read_raw(void *dev, const struct noos_iio_chan *ch,
			      enum noos_iio_info info, int *val, int *val2)
{
	struct maxim_adc *adc = dev;
	int ret;

	switch (info) {
	case NOOS_IIO_INFO_RAW:
		ret = maxim_adc_convert((unsigned int)ch->priv, val);
		if (ret)
			return ret;

		return NOOS_IIO_VAL_INT;
	case NOOS_IIO_INFO_SCALE:
		/* One LSB in mV: the reference over 2^bits. */
		*val = maxim_adc_ref_mv(adc);
		*val2 = MAXIM_ADC_RESOLUTION_BITS;
		return NOOS_IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

/* One scan: the enabled inputs, converted one after the other. */
static int maxim_adc_read_scan(void *dev, uint32_t enabled, int64_t *vals)
{
	unsigned int i;
	int raw, ret;

	for (i = 0; i < IIO_ADC_NUM_CHANNELS; i++) {
		if (!(enabled & (UINT32_C(1) << i)))
			continue;

		ret = maxim_adc_convert(i, &raw);
		if (ret)
			return ret;

		vals[i] = raw;
	}

	return 0;
}

/*
 * Device-table init (NOOS_IIOD_ONCHIP_ADC()): @param is the name of the
 * trigger that paces the buffer, or NULL to sample as fast as it converts.
 */
int noos_onchip_adc_init(const void *param)
{
	unsigned int i;
	int ret;

	ret = maxim_adc_init();
	if (ret)
		return ret;

	maxim_adc = (struct maxim_adc) { .trigger = param };

	for (i = 0; i < IIO_ADC_NUM_CHANNELS; i++) {
		maxim_adc.channels[i] = (struct noos_iio_chan)
			NOOS_IIO_VOLTAGE_CHANNEL(i, i, NOOS_IIO_BIT(RAW) |
						 NOOS_IIO_BIT(SCALE), 0,
						 MAXIM_ADC_SCAN_TYPE);
#if MAXIM_ADC_HAS_REFERENCE
		maxim_adc.channels[i].ext = maxim_adc_ext;
#endif
	}

	return 0;
}

int noos_onchip_adc_get_device_info(struct noos_iio_device_info *info)
{
	if (!info)
		return -EINVAL;

	*info = (struct noos_iio_device_info) {
		.name = IIO_ADC_NAME,
		.dev = &maxim_adc,
		.channels = maxim_adc.channels,
		.num_channels = IIO_ADC_NUM_CHANNELS,
		.read_raw = maxim_adc_read_raw,
		.read_scan = maxim_adc_read_scan,
		.trigger = maxim_adc.trigger,
	};

	return 0;
}
