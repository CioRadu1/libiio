/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_ctx.h"
#include "test_framework.h"
#include "iio_val.h"

TEST_FUNCTION(val_format_int)
{
	int vals[1] = { 42 };
	char buf[32];
	int ret;

	ret = iio_format_value(buf, sizeof(buf), IIO_VAL_INT, 1, vals);
	TEST_INT_EQ(ret, 2, "IIO_VAL_INT reports two characters");
	TEST_STR_EQ(buf, "42", "IIO_VAL_INT formats a positive integer");

	vals[0] = -7;
	ret = iio_format_value(buf, sizeof(buf), IIO_VAL_INT, 1, vals);
	TEST_INT_EQ(ret, 2, "a negative integer reports two characters");
	TEST_STR_EQ(buf, "-7", "IIO_VAL_INT formats a negative integer");
}

TEST_FUNCTION(val_format_int_plus_micro)
{
	int vals[2] = { 1, 500000 };
	char buf[32];

	iio_format_value(buf, sizeof(buf), IIO_VAL_INT_PLUS_MICRO, 2, vals);
	TEST_STR_EQ(buf, "1.500000", "IIO_VAL_INT_PLUS_MICRO pads to six digits");

	vals[0] = 0;
	vals[1] = -250000;
	iio_format_value(buf, sizeof(buf), IIO_VAL_INT_PLUS_MICRO, 2, vals);
	TEST_STR_EQ(buf, "-0.250000",
		    "a negative fraction carries the sign to the front");

	vals[0] = -2;
	vals[1] = -125000;
	iio_format_value(buf, sizeof(buf), IIO_VAL_INT_PLUS_MICRO, 2, vals);
	TEST_STR_EQ(buf, "-2.125000",
		    "a negative integer part is not double-signed");
}

TEST_FUNCTION(val_format_int_plus_nano)
{
	int vals[2] = { 3, 25 };
	char buf[32];

	iio_format_value(buf, sizeof(buf), IIO_VAL_INT_PLUS_NANO, 2, vals);
	TEST_STR_EQ(buf, "3.000000025",
		    "IIO_VAL_INT_PLUS_NANO pads to nine digits");
}

TEST_FUNCTION(val_format_fractional)
{
	int vals[2] = { 1, 4 };
	char buf[32];
	int ret;

	iio_format_value(buf, sizeof(buf), IIO_VAL_FRACTIONAL, 2, vals);
	TEST_STR_EQ(buf, "0.250000000", "IIO_VAL_FRACTIONAL divides 1/4");

	vals[0] = -1;
	iio_format_value(buf, sizeof(buf), IIO_VAL_FRACTIONAL, 2, vals);
	TEST_STR_EQ(buf, "-0.250000000",
		    "a negative fraction below one keeps its sign");

	vals[1] = 0;
	ret = iio_format_value(buf, sizeof(buf), IIO_VAL_FRACTIONAL, 2, vals);
	TEST_INT_EQ(ret, -EINVAL, "a zero denominator gives -EINVAL");
}

TEST_FUNCTION(val_format_int_64)
{
	int vals[2] = { 0, 1 };
	char buf[32];

	iio_format_value(buf, sizeof(buf), IIO_VAL_INT_64, 2, vals);
	TEST_STR_EQ(buf, "4294967296",
		    "IIO_VAL_INT_64 joins the low and high words");

	vals[0] = -1;
	vals[1] = -1;
	iio_format_value(buf, sizeof(buf), IIO_VAL_INT_64, 2, vals);
	TEST_STR_EQ(buf, "-1", "an all-ones pair is minus one");
}

TEST_FUNCTION(val_format_char)
{
	int vals[1] = { 'A' };
	char buf[32];
	int ret;

	ret = iio_format_value(buf, sizeof(buf), IIO_VAL_CHAR, 1, vals);
	TEST_INT_EQ(ret, 1, "IIO_VAL_CHAR reports one character");
	TEST_STR_EQ(buf, "A", "IIO_VAL_CHAR emits the character itself");
}

TEST_FUNCTION(val_format_rejects_bad_input)
{
	int vals[1] = { 1 };
	char buf[32];

	TEST_INT_EQ(iio_format_value(NULL, sizeof(buf), IIO_VAL_INT, 1, vals),
		    -EINVAL, "a NULL destination gives -EINVAL");
	TEST_INT_EQ(iio_format_value(buf, sizeof(buf), IIO_VAL_INT, 1, NULL),
		    -EINVAL, "a NULL value pointer gives -EINVAL");
	TEST_INT_EQ(iio_format_value(buf, 0, IIO_VAL_INT, 1, vals),
		    -EINVAL, "a zero-length buffer gives -EINVAL");
	TEST_INT_EQ(iio_format_value(buf, sizeof(buf), (enum iio_val_type)99,
				     1, vals),
		    -EINVAL, "an unknown value type gives -EINVAL");
}

TEST_FUNCTION(val_format_truncates)
{
	int vals[1] = { 1234 };
	char buf[2];
	int ret;

	ret = iio_format_value(buf, sizeof(buf), IIO_VAL_INT, 1, vals);
	TEST_INT_EQ(ret, 4, "a short buffer still reports the full length");
	TEST_STR_EQ(buf, "1", "a short buffer holds a NUL-terminated prefix");
}

TEST_FUNCTION(val_parse_integer)
{
	int integer = -1, fract = -1;

	TEST_INT_EQ(iio_str_to_fixpoint("42", 0, &integer, &fract), 0,
		    "a plain integer parses");
	TEST_INT_EQ(integer, 42, "the integer part is 42");
	TEST_INT_EQ(fract, 0, "a plain integer has no fractional part");

	TEST_INT_EQ(iio_str_to_fixpoint("-13\n", 0, &integer, &fract), 0,
		    "a trailing newline is accepted");
	TEST_INT_EQ(integer, -13, "a negative integer parses");
}

TEST_FUNCTION(val_parse_fixpoint)
{
	int integer = -1, fract = -1;

	TEST_INT_EQ(iio_str_to_fixpoint("1.5", 100000, &integer, &fract), 0,
		    "a micro fixpoint parses");
	TEST_INT_EQ(integer, 1, "the integer part is 1");
	TEST_INT_EQ(fract, 500000, "one decimal digit scales to microunits");

	TEST_INT_EQ(iio_str_to_fixpoint("2.500000", 100000, &integer, &fract),
		    0, "a fully written micro fixpoint parses");
	TEST_INT_EQ(integer, 2, "the integer part is 2");
	TEST_INT_EQ(fract, 500000, "six decimal digits give half a unit");

	TEST_INT_EQ(iio_str_to_fixpoint("-0.25", 100000, &integer, &fract), 0,
		    "a negative fixpoint below one parses");
	TEST_INT_EQ(integer, 0, "the integer part stays zero");
	TEST_INT_EQ(fract, -250000,
		    "the sign moves to the fraction when the integer is zero");
}

TEST_FUNCTION(val_parse_rejects_bad_input)
{
	int integer = 0, fract = 0;

	TEST_INT_EQ(iio_str_to_fixpoint(NULL, 0, &integer, &fract), -EINVAL,
		    "a NULL string gives -EINVAL");
	TEST_INT_EQ(iio_str_to_fixpoint("42", 0, NULL, &fract), -EINVAL,
		    "a NULL integer output gives -EINVAL");
	TEST_INT_EQ(iio_str_to_fixpoint("42", 0, &integer, NULL), -EINVAL,
		    "a NULL fraction output gives -EINVAL");
	TEST_INT_EQ(iio_str_to_fixpoint("", 0, &integer, &fract), -EINVAL,
		    "an empty string gives -EINVAL");
	TEST_INT_EQ(iio_str_to_fixpoint("abc", 100000, &integer, &fract),
		    -EINVAL, "a non-numeric string gives -EINVAL");
	TEST_INT_EQ(iio_str_to_fixpoint("1.2.3", 100000, &integer, &fract),
		    -EINVAL, "a second decimal point gives -EINVAL");
}

TEST_FUNCTION(val_round_trip)
{
	int vals[2];
	char buf[32];
	int integer, fract;

	TEST_INT_EQ(iio_str_to_fixpoint("2.500000", 100000, &integer, &fract),
		    0, "the written scale parses");

	vals[0] = integer;
	vals[1] = fract;
	iio_format_value(buf, sizeof(buf), IIO_VAL_INT_PLUS_MICRO, 2, vals);
	TEST_STR_EQ(buf, "2.500000",
		    "a micro value survives parse and format");
}

TEST_FUNCTION(val_fract_mult)
{
	TEST_INT_EQ(iio_val_fract_mult(IIO_VAL_INT), 0,
		    "integers are not fixed-point");
	TEST_INT_EQ(iio_val_fract_mult(IIO_VAL_INT_PLUS_MICRO), 100000,
		    "micro values scale by 100000");
	TEST_INT_EQ(iio_val_fract_mult(IIO_VAL_INT_PLUS_NANO), 100000000,
		    "nano values scale by 100000000");
	TEST_INT_EQ(iio_val_fract_mult(IIO_VAL_CHAR), -EINVAL,
		    "characters are not fixed-point");
	TEST_INT_EQ(iio_val_fract_mult(IIO_VAL_INT_64), -EINVAL,
		    "64-bit integers are not fixed-point");
	TEST_INT_EQ(iio_val_fract_mult(IIO_VAL_FRACTIONAL), -EINVAL,
		    "fractions are not fixed-point");
}

int main(void)
{
	DEBUG_PRINT("=== no-OS iio_val tests ===\n");

	RUN_TEST(val_format_int);
	RUN_TEST(val_format_int_plus_micro);
	RUN_TEST(val_format_int_plus_nano);
	RUN_TEST(val_format_fractional);
	RUN_TEST(val_format_int_64);
	RUN_TEST(val_format_char);
	RUN_TEST(val_format_rejects_bad_input);
	RUN_TEST(val_format_truncates);
	RUN_TEST(val_parse_integer);
	RUN_TEST(val_parse_fixpoint);
	RUN_TEST(val_parse_rejects_bad_input);
	RUN_TEST(val_round_trip);
	RUN_TEST(val_fract_mult);

	TEST_SUMMARY();

	return 0;
}
