/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"
#include "iio_adc_hal.h"
#include <iio/iio-backend.h>

#define FAKE_CHANNELS		3
#define FAKE_BITS		12
#define FAKE_REF_VOLTAGE_MV	1800

static const char *const fake_channel_names[FAKE_CHANNELS] = {
	"voltage0", "voltage1", "voltage2",
};

static struct {
	unsigned int init_calls;
	unsigned int read_raw_calls;
	unsigned int last_read_channel;
	unsigned int set_gain_calls;
	unsigned int last_gain_channel;
	unsigned int last_gain;
	unsigned int set_reference_calls;
	unsigned int last_reference_channel;
	unsigned int last_reference;
	int init_ret;
	int read_raw_ret;
	int set_gain_ret;
	int set_reference_ret;
	int next_raw;
} fake;

static int fake_init(void)
{
	fake.init_calls++;

	return fake.init_ret;
}

static int fake_read_raw(unsigned int channel, int *value)
{
	fake.read_raw_calls++;
	fake.last_read_channel = channel;

	if (fake.read_raw_ret)
		return fake.read_raw_ret;

	*value = fake.next_raw;

	return 0;
}

static int fake_set_gain(unsigned int channel, unsigned int gain)
{
	fake.set_gain_calls++;
	fake.last_gain_channel = channel;
	fake.last_gain = gain;

	return fake.set_gain_ret;
}

static int fake_set_reference(unsigned int channel, unsigned int reference)
{
	fake.set_reference_calls++;
	fake.last_reference_channel = channel;
	fake.last_reference = reference;

	return fake.set_reference_ret;
}

const struct iio_adc_hal iio_adc_hal = {
	.channels	 = fake_channel_names,
	.num_channels	 = FAKE_CHANNELS,
	.resolution_bits = FAKE_BITS,
	.ref_voltage_mv	 = FAKE_REF_VOLTAGE_MV,
	.init		 = fake_init,
	.read_raw	 = fake_read_raw,
	.set_gain	 = fake_set_gain,
	.set_reference	 = fake_set_reference,
};

static struct iio_context *adc_ctx;
static struct noos_iio_device_info adc_info;

static void fake_reset(void)
{
	memset(&fake, 0, sizeof(fake));
}

static struct iio_channel *adc_channel(unsigned int index)
{
	struct iio_device *dev = iio_context_get_device(adc_ctx, 0);

	if (!dev)
		return NULL;

	return iio_device_get_channel(dev, index);
}

static int adc_read_channel_attr(unsigned int index, const char *name,
				 char *dst, size_t len)
{
	struct iio_channel *chn = adc_channel(index);
	struct iio_attr attr;

	if (!chn)
		return -ENODEV;

	memset(&attr, 0, sizeof(attr));
	attr.iio.chn = chn;
	attr.type = IIO_ATTR_TYPE_CHANNEL;
	attr.name = name;

	return adc_info.read_attr(adc_info.dev, iio_context_get_device(adc_ctx, 0),
				  &attr, dst, len);
}

static int adc_write_channel_attr(unsigned int index, const char *name,
				  const char *value)
{
	struct iio_channel *chn = adc_channel(index);
	struct iio_attr attr;

	if (!chn)
		return -ENODEV;

	memset(&attr, 0, sizeof(attr));
	attr.iio.chn = chn;
	attr.type = IIO_ATTR_TYPE_CHANNEL;
	attr.name = name;

	return adc_info.write_attr(adc_info.dev,
				   iio_context_get_device(adc_ctx, 0),
				   &attr, value, strlen(value));
}

TEST_FUNCTION(adc_init_calls_the_hal)
{
	fake_reset();

	TEST_INT_EQ(iio_adc_init(), 0, "iio_adc_init succeeds");
	TEST_INT_EQ(fake.init_calls, 1, "iio_adc_init calls the hal init once");
}

TEST_FUNCTION(adc_init_propagates_a_hal_error)
{
	fake_reset();
	fake.init_ret = -EIO;

	TEST_INT_EQ(iio_adc_init(), -EIO,
		    "a failing hal init is reported to the caller");

	fake.init_ret = 0;
	TEST_INT_EQ(iio_adc_init(), 0, "the driver initialises after a retry");
}

TEST_FUNCTION(adc_device_info)
{
	struct noos_iio_device_info info;

	memset(&info, 0xa5, sizeof(info));

	TEST_INT_EQ(iio_adc_get_device_info(&info), 0,
		    "iio_adc_get_device_info succeeds");
	TEST_ASSERT_PTR_NOT_NULL(info.name, "the device has a name");
	if (info.name)
		TEST_STR_EQ(info.name, TEST_DEVICE_NAME, "the device name");
	TEST_INT_EQ(info.direction, 0, "the device is an input");
	TEST_ASSERT_PTR_NULL(info.dev, "the driver keeps no device handle");
	TEST_ASSERT(info.add_channels != NULL, "add_channels is provided");
	TEST_ASSERT(info.read_attr != NULL, "read_attr is provided");
	TEST_ASSERT(info.write_attr != NULL, "write_attr is provided");
	TEST_ASSERT(info.read_samples != NULL, "read_samples is provided");
	TEST_ASSERT(info.reg_read != NULL, "reg_read is provided");
	TEST_ASSERT(info.reg_write != NULL, "reg_write is provided");
	TEST_ASSERT_PTR_NULL(info.write_samples,
			     "an input-only device has no write_samples");
}

TEST_FUNCTION(adc_get_device_info_rejects_null)
{
	TEST_INT_EQ(iio_adc_get_device_info(NULL), -EINVAL,
		    "a NULL info pointer gives -EINVAL");
}

TEST_FUNCTION(adc_read_samples_uses_the_hal)
{
	uint16_t samples[4];
	int ret;

	fake_reset();
	fake.next_raw = 0x123;

	ret = adc_info.read_samples(adc_info.dev, samples, sizeof(samples));
	TEST_INT_EQ(ret, 0, "read_samples succeeds");
	TEST_INT_EQ(fake.read_raw_calls, 4,
		    "read_samples reads once per sample");
	TEST_INT_EQ(samples[0], 0x123, "the first sample comes from the hal");
	TEST_INT_EQ(samples[3], 0x123, "the last sample comes from the hal");
}

TEST_FUNCTION(adc_read_samples_masks_to_the_resolution)
{
	uint16_t samples[1];

	fake_reset();
	fake.next_raw = 0xffff;

	TEST_INT_EQ(adc_info.read_samples(adc_info.dev, samples,
					  sizeof(samples)),
		    0, "read_samples succeeds with a saturated reading");
	TEST_INT_EQ(samples[0], (1 << FAKE_BITS) - 1,
		    "samples are masked to the hal resolution");
}

TEST_FUNCTION(adc_read_samples_propagates_a_hal_error)
{
	uint16_t samples[2];

	fake_reset();
	fake.read_raw_ret = -EIO;

	TEST_INT_EQ(adc_info.read_samples(adc_info.dev, samples,
					  sizeof(samples)),
		    -EIO, "a failing hal read is reported to the caller");
}

TEST_FUNCTION(adc_read_attr_device)
{
	struct iio_attr attr;
	char buf[32];
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.type = IIO_ATTR_TYPE_DEVICE;
	attr.name = "internal_ref_voltage";

	ret = adc_info.read_attr(adc_info.dev,
				 iio_context_get_device(adc_ctx, 0),
				 &attr, buf, sizeof(buf));
	TEST_INT_EQ(ret, 5, "the length includes the NUL terminator");
	TEST_STR_EQ(buf, "1800",
		    "internal_ref_voltage comes from the hal");

	attr.name = "nonexistent";
	ret = adc_info.read_attr(adc_info.dev,
				 iio_context_get_device(adc_ctx, 0),
				 &attr, buf, sizeof(buf));
	TEST_INT_EQ(ret, -EINVAL, "an unknown device attribute gives -EINVAL");
}

TEST_FUNCTION(adc_read_attr_rejects_a_channel_less_attr)
{
	struct iio_attr attr;
	char buf[32];

	memset(&attr, 0, sizeof(attr));
	attr.type = IIO_ATTR_TYPE_CHANNEL;
	attr.name = "raw";

	TEST_INT_EQ(adc_info.read_attr(adc_info.dev,
				       iio_context_get_device(adc_ctx, 0),
				       &attr, buf, sizeof(buf)),
		    -EINVAL, "a channel attribute without a channel is refused");
}

TEST_FUNCTION(adc_read_attr_raw)
{
	char buf[32];
	int ret;

	fake_reset();
	fake.next_raw = 1234;

	ret = adc_read_channel_attr(0, "raw", buf, sizeof(buf));
	TEST_INT_EQ(ret, 5, "raw reports its length including the NUL");
	TEST_STR_EQ(buf, "1234", "raw is the value the hal returned");
	TEST_INT_EQ(fake.read_raw_calls, 1, "raw reads the hal exactly once");

	fake.read_raw_ret = -EIO;
	TEST_INT_EQ(adc_read_channel_attr(0, "raw", buf, sizeof(buf)), -EIO,
		    "a failing hal read is reported through raw");
}

TEST_FUNCTION(adc_read_attr_uses_the_channel_index)
{
	char buf[32];

	fake_reset();
	fake.next_raw = 7;

	adc_read_channel_attr(2, "raw", buf, sizeof(buf));
	TEST_INT_EQ(fake.last_read_channel, 2,
		    "raw asks the hal for the channel it was called on");
}

TEST_FUNCTION(adc_write_attr_gain_reaches_the_hal)
{
	char buf[32];

	fake_reset();

	TEST_INT_EQ(adc_write_channel_attr(1, "gain", "2"), 1,
		    "writing a gain returns the consumed length");
	TEST_INT_EQ(fake.set_gain_calls, 1, "the hal set_gain is called once");
	TEST_INT_EQ(fake.last_gain_channel, 1,
		    "the hal is told which channel changed");
	TEST_INT_EQ(fake.last_gain, IIO_ADC_GAIN_2,
		    "the gain string maps to IIO_ADC_GAIN_2");

	adc_read_channel_attr(1, "gain", buf, sizeof(buf));
	TEST_STR_EQ(buf, "2", "the written gain reads back");

	adc_write_channel_attr(1, "gain", "1");
}

TEST_FUNCTION(adc_write_attr_reference_reaches_the_hal)
{
	char buf[32];

	fake_reset();

	TEST_INT_EQ(adc_write_channel_attr(0, "reference", "External1"), 9,
		    "writing a reference returns the consumed length");
	TEST_INT_EQ(fake.set_reference_calls, 1,
		    "the hal set_reference is called once");
	TEST_INT_EQ(fake.last_reference, IIO_ADC_REF_EXTERNAL1,
		    "the reference string maps to IIO_ADC_REF_EXTERNAL1");

	adc_read_channel_attr(0, "reference", buf, sizeof(buf));
	TEST_STR_EQ(buf, "External1", "the written reference reads back");

	adc_write_channel_attr(0, "reference", "Internal");
}

TEST_FUNCTION(adc_write_attr_accepts_a_trailing_newline)
{
	fake_reset();

	TEST_INT_EQ(adc_write_channel_attr(0, "gain", "4\n"), 2,
		    "a newline-terminated gain is accepted");
	TEST_INT_EQ(fake.last_gain, IIO_ADC_GAIN_4,
		    "the newline is stripped before the lookup");

	adc_write_channel_attr(0, "gain", "1");
}

TEST_FUNCTION(adc_write_attr_keeps_state_when_the_hal_fails)
{
	char buf[32];

	fake_reset();
	adc_write_channel_attr(0, "gain", "8");
	fake.set_gain_ret = -EIO;

	TEST_INT_EQ(adc_write_channel_attr(0, "gain", "16"), -EIO,
		    "a failing hal set_gain is reported to the caller");

	fake.set_gain_ret = 0;
	adc_read_channel_attr(0, "gain", buf, sizeof(buf));
	TEST_STR_EQ(buf, "8",
		    "a rejected gain leaves the previous value in place");

	adc_write_channel_attr(0, "gain", "1");
}

TEST_FUNCTION(adc_write_attr_rejects_unknown_values)
{
	fake_reset();

	TEST_INT_EQ(adc_write_channel_attr(0, "gain", "7/9"), -EINVAL,
		    "an unknown gain gives -EINVAL");
	TEST_INT_EQ(fake.set_gain_calls, 0,
		    "an unknown gain never reaches the hal");
	TEST_INT_EQ(adc_write_channel_attr(0, "reference", "Bogus"), -EINVAL,
		    "an unknown reference gives -EINVAL");
	TEST_INT_EQ(fake.set_reference_calls, 0,
		    "an unknown reference never reaches the hal");
	TEST_INT_EQ(adc_write_channel_attr(0, "differential", "2"), -EINVAL,
		    "a differential outside 0 and 1 gives -EINVAL");
}

TEST_FUNCTION(adc_write_attr_read_only_and_unknown)
{
	struct iio_attr attr;

	fake_reset();

	TEST_INT_EQ(adc_write_channel_attr(0, "raw", "1"), -EPERM,
		    "writing raw gives -EPERM");
	TEST_INT_EQ(adc_write_channel_attr(0, "process", "1"), -EPERM,
		    "writing process gives -EPERM");
	TEST_INT_EQ(adc_write_channel_attr(0, "nonexistent", "1"), -EINVAL,
		    "writing an unknown attribute gives -EINVAL");

	memset(&attr, 0, sizeof(attr));
	attr.type = IIO_ATTR_TYPE_DEVICE;
	attr.name = "internal_ref_voltage";
	TEST_INT_EQ(adc_info.write_attr(adc_info.dev,
					iio_context_get_device(adc_ctx, 0),
					&attr, "1", 1),
		    -EPERM, "writing a device attribute gives -EPERM");
}

TEST_FUNCTION(adc_scale_drives_process)
{
	char buf[32];

	fake_reset();
	fake.next_raw = 100;

	TEST_INT_EQ(adc_write_channel_attr(0, "scale", "2.500000"), 8,
		    "a micro scale is accepted");
	adc_read_channel_attr(0, "scale", buf, sizeof(buf));
	TEST_STR_EQ(buf, "2.500000", "the scale reads back unchanged");

	adc_read_channel_attr(0, "process", buf, sizeof(buf));
	TEST_STR_EQ(buf, "250", "process is raw multiplied by the scale");

	adc_write_channel_attr(0, "scale", "0.000000");
	adc_read_channel_attr(0, "process", buf, sizeof(buf));
	TEST_STR_EQ(buf, "0", "a zero scale gives a zero process value");

	adc_write_channel_attr(0, "scale", "1.000000");
}

TEST_FUNCTION(adc_state_is_per_channel)
{
	char buf[32];

	fake_reset();

	adc_write_channel_attr(0, "gain", "2");
	adc_write_channel_attr(1, "gain", "8");

	adc_read_channel_attr(0, "gain", buf, sizeof(buf));
	TEST_STR_EQ(buf, "2", "channel 0 keeps its own gain");
	adc_read_channel_attr(1, "gain", buf, sizeof(buf));
	TEST_STR_EQ(buf, "8", "channel 1 keeps its own gain");

	adc_write_channel_attr(0, "gain", "1");
	adc_write_channel_attr(1, "gain", "1");
}

TEST_FUNCTION(adc_registers_round_trip)
{
	uint32_t val = 0;

	TEST_INT_EQ(adc_info.reg_write(adc_info.dev, 3, 0xdeadbeef), 0,
		    "a register write succeeds");
	TEST_INT_EQ(adc_info.reg_read(adc_info.dev, 3, &val), 0,
		    "a register read succeeds");
	TEST_LONG_EQ(val, 0xdeadbeef, "the register keeps what was written");

	TEST_INT_EQ(adc_info.reg_write(adc_info.dev, 16, 0), -EINVAL,
		    "a register write past the end gives -EINVAL");
	TEST_INT_EQ(adc_info.reg_read(adc_info.dev, 16, &val), -EINVAL,
		    "a register read past the end gives -EINVAL");
}

TEST_FUNCTION(adc_add_channels_matches_the_hal)
{
	struct iio_device *dev = iio_context_get_device(adc_ctx, 0);
	unsigned int i;

	TEST_ASSERT_PTR_NOT_NULL(dev, "the registered device is present");
	if (!dev)
		return;

	TEST_INT_EQ(iio_device_get_channels_count(dev), FAKE_CHANNELS,
		    "add_channels created one channel per hal channel");

	for (i = 0; i < FAKE_CHANNELS; i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);

		TEST_ASSERT_PTR_NOT_NULL(chn, "the channel is reachable");
		if (!chn)
			continue;

		TEST_STR_EQ(iio_channel_get_id(chn), fake_channel_names[i],
			    "the channel id comes from the hal");
		TEST_INT_EQ(iio_channel_get_data_format(chn)->bits, FAKE_BITS,
			    "the format resolution comes from the hal");
	}
}

int main(void)
{
	RUN_TEST(adc_init_calls_the_hal);
	RUN_TEST(adc_init_propagates_a_hal_error);
	RUN_TEST(adc_device_info);
	RUN_TEST(adc_get_device_info_rejects_null);

	adc_ctx = test_ctx_require();
	iio_adc_get_device_info(&adc_info);

	DEBUG_PRINT("=== no-OS iio_adc driver tests ===\n");

	RUN_TEST(adc_read_samples_uses_the_hal);
	RUN_TEST(adc_read_samples_masks_to_the_resolution);
	RUN_TEST(adc_read_samples_propagates_a_hal_error);
	RUN_TEST(adc_read_attr_device);
	RUN_TEST(adc_read_attr_rejects_a_channel_less_attr);
	RUN_TEST(adc_read_attr_raw);
	RUN_TEST(adc_read_attr_uses_the_channel_index);
	RUN_TEST(adc_write_attr_gain_reaches_the_hal);
	RUN_TEST(adc_write_attr_reference_reaches_the_hal);
	RUN_TEST(adc_write_attr_accepts_a_trailing_newline);
	RUN_TEST(adc_write_attr_keeps_state_when_the_hal_fails);
	RUN_TEST(adc_write_attr_rejects_unknown_values);
	RUN_TEST(adc_write_attr_read_only_and_unknown);
	RUN_TEST(adc_scale_drives_process);
	RUN_TEST(adc_state_is_per_channel);
	RUN_TEST(adc_registers_round_trip);
	RUN_TEST(adc_add_channels_matches_the_hal);

	iio_context_destroy(adc_ctx);

	TEST_SUMMARY();

	return 0;
}
