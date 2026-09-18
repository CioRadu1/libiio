/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "iio_val.h"
#include "test_framework.h"

static const char *fmt_str(enum iio_val_type type, int v0, int v1)
{
	static char buf[64];
	int vals[2];
	int ret;

	vals[0] = v0;
	vals[1] = v1;

	ret = iio_format_value(buf, sizeof(buf), type, 2, vals);
	if (ret < 0)
		return NULL;

	return buf;
}

static int fmt_ret(enum iio_val_type type, int v0, int v1)
{
	char buf[64];
	int vals[2];

	vals[0] = v0;
	vals[1] = v1;

	return iio_format_value(buf, sizeof(buf), type, 2, vals);
}

TEST_FUNCTION(format_int)
{
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT, 0, 0), "0", "zero");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT, 42, 0), "42", "positive");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT, -42, 0), "-42", "negative");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT, 4095, 0), "4095",
			   "12-bit full scale");
	TEST_ASSERT_INT_EQUAL(fmt_ret(IIO_VAL_INT, 42, 0), 2, "return is length");
}

TEST_FUNCTION(format_int_plus_micro)
{
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT_PLUS_MICRO, 1, 500000),
			   "1.500000", "positive");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT_PLUS_MICRO, 0, 297619),
			   "0.297619", "zero integer part");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT_PLUS_MICRO, 1, 0),
			   "1.000000", "zero fractional part");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT_PLUS_MICRO, -1, -500000),
			   "-1.500000", "negative");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT_PLUS_MICRO, 0, -500000),
			   "-0.500000", "negative below one");
}

TEST_FUNCTION(format_int_plus_nano)
{
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT_PLUS_NANO, 2, 25),
			   "2.000000025", "positive");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT_PLUS_NANO, 0, -25),
			   "-0.000000025", "negative below one");
}

TEST_FUNCTION(format_fractional)
{
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_FRACTIONAL, 1, 4),
			   "0.250000000", "one quarter");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_FRACTIONAL, -1, 4),
			   "-0.250000000", "negative one quarter");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_FRACTIONAL, 5, 2),
			   "2.500000000", "above one");
	TEST_ASSERT_INT_EQUAL(fmt_ret(IIO_VAL_FRACTIONAL, 5, 0), -EINVAL,
			      "zero denominator rejected");
}

TEST_FUNCTION(format_int_64)
{
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT_64, 0, 1),
			   "4294967296", "high word only");
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_INT_64, -1, 0),
			   "4294967295", "low word all ones");
}

TEST_FUNCTION(format_char)
{
	TEST_ASSERT_STR_EQ(fmt_str(IIO_VAL_CHAR, 'A', 0), "A", "letter");
}

TEST_FUNCTION(format_rejects_bad_input)
{
	char buf[16];
	int vals[2] = { 1, 0 };

	TEST_ASSERT_INT_EQUAL(fmt_ret((enum iio_val_type)999, 1, 0), -EINVAL,
			      "unknown type rejected");
	TEST_ASSERT_INT_EQUAL(iio_format_value(NULL, sizeof(buf), IIO_VAL_INT,
					       1, vals),
			      -EINVAL, "NULL buffer rejected");
	TEST_ASSERT_INT_EQUAL(iio_format_value(buf, sizeof(buf), IIO_VAL_INT,
					       1, NULL),
			      -EINVAL, "NULL vals rejected");
	TEST_ASSERT_INT_EQUAL(iio_format_value(buf, 0, IIO_VAL_INT, 1, vals),
			      -EINVAL, "zero length rejected");
}

TEST_FUNCTION(format_truncation)
{
	char buf[8];
	int vals[2] = { 1234567890, 0 };
	int ret;

	ret = iio_format_value(buf, sizeof(buf), IIO_VAL_INT, 1, vals);

	TEST_ASSERT(ret < 0 || ret <= (int)sizeof(buf) - 1,
		    "return never exceeds what fits in the buffer");
	TEST_ASSERT_INT_EQUAL(ret, -ENOSPC, "truncation is reported");

	ret = iio_format_value(buf, sizeof(buf), IIO_VAL_INT_PLUS_MICRO, 2, vals);
	TEST_ASSERT_INT_EQUAL(ret, -ENOSPC, "truncated fixpoint is reported");

	vals[0] = 1234567;
	ret = iio_format_value(buf, sizeof(buf), IIO_VAL_INT, 1, vals);
	TEST_ASSERT_INT_EQUAL(ret, 7, "exact fit still succeeds");
}

static void check_parse(const char *str, int fract_mult, int want_ret,
			int want_int, int want_fract, const char *message)
{
	int integer = 0x5a5a5a5a;
	int fract = 0x5a5a5a5a;
	int ret;

	ret = iio_str_to_fixpoint(str, fract_mult, &integer, &fract);

	TEST_ASSERT_INT_EQUAL(ret, want_ret, message);
	if (ret || want_ret)
		return;

	TEST_ASSERT_INT_EQUAL(integer, want_int, message);
	TEST_ASSERT_INT_EQUAL(fract, want_fract, message);
}

TEST_FUNCTION(parse_integer)
{
	check_parse("0", 0, 0, 0, 0, "zero");
	check_parse("42", 0, 0, 42, 0, "positive");
	check_parse("-42", 0, 0, -42, 0, "negative");
	check_parse("2147483647", 0, 0, 2147483647, 0, "INT_MAX");
	check_parse("42\n", 0, 0, 42, 0, "one trailing newline");
}

TEST_FUNCTION(parse_fixpoint)
{
	check_parse("1.5", 100000, 0, 1, 500000, "one and a half");
	check_parse("0.297619", 100000, 0, 0, 297619, "six decimals");
	check_parse("1", 100000, 0, 1, 0, "no decimal point");
	check_parse("1.5\n", 100000, 0, 1, 500000, "one trailing newline");
	check_parse("+1.5", 100000, 0, 1, 500000, "explicit plus");
	check_parse("2.000000025", 100000000, 0, 2, 25, "nano scale");
}

TEST_FUNCTION(parse_negative)
{
	check_parse("-1.5", 100000, 0, -1, 500000,
		    "sign carried by the integer part");
	check_parse("-0.5", 100000, 0, 0, -500000,
		    "sign carried by the fractional part");
}

TEST_FUNCTION(parse_rejects_garbage)
{
	check_parse("abc", 100000, -EINVAL, 0, 0, "letters");
	check_parse("1.2.3", 100000, -EINVAL, 0, 0, "two decimal points");
	check_parse("1x", 100000, -EINVAL, 0, 0, "trailing letter");
	check_parse("1.5x", 100000, -EINVAL, 0, 0, "trailing letter after fract");
	check_parse("1\n5", 100000, -EINVAL, 0, 0, "embedded newline");
	check_parse("abc", 0, -EINVAL, 0, 0, "letters, integer path");
	check_parse("42abc", 0, -EINVAL, 0, 0, "trailing letters, integer path");
	check_parse("", 0, -EINVAL, 0, 0, "empty string, integer path");
	check_parse(" 5", 0, -EINVAL, 0, 0, "leading whitespace rejected");
	check_parse("42\n\n", 0, -EINVAL, 0, 0,
		    "only one trailing newline allowed");
}

TEST_FUNCTION(parse_out_of_range)
{
	check_parse("99999999999999999999", 0, -ERANGE, 0, 0,
		    "far beyond long");
	check_parse("4294967296", 0, -ERANGE, 0, 0, "beyond int");
	check_parse("-4294967296", 0, -ERANGE, 0, 0, "below int");
}

TEST_FUNCTION(parse_rejects_null)
{
	int integer, fract;

	TEST_ASSERT_INT_EQUAL(iio_str_to_fixpoint(NULL, 0, &integer, &fract),
			      -EINVAL, "NULL string rejected");
	TEST_ASSERT_INT_EQUAL(iio_str_to_fixpoint("1", 0, NULL, &fract),
			      -EINVAL, "NULL integer rejected");
	TEST_ASSERT_INT_EQUAL(iio_str_to_fixpoint("1", 0, &integer, NULL),
			      -EINVAL, "NULL fract rejected");
}

TEST_FUNCTION(fract_mult_per_type)
{
	TEST_ASSERT_INT_EQUAL(iio_val_fract_mult(IIO_VAL_INT), 0,
			      "plain integer");
	TEST_ASSERT_INT_EQUAL(iio_val_fract_mult(IIO_VAL_INT_PLUS_MICRO),
			      100000, "micro");
	TEST_ASSERT_INT_EQUAL(iio_val_fract_mult(IIO_VAL_INT_PLUS_NANO),
			      100000000, "nano");
	TEST_ASSERT_INT_EQUAL(iio_val_fract_mult(IIO_VAL_CHAR), -EINVAL,
			      "char is not fixed-point");
	TEST_ASSERT_INT_EQUAL(iio_val_fract_mult(IIO_VAL_INT_64), -EINVAL,
			      "int64 is not fixed-point");
	TEST_ASSERT_INT_EQUAL(iio_val_fract_mult(IIO_VAL_FRACTIONAL), -EINVAL,
			      "fractional is not fixed-point");
}

static void check_roundtrip(const char *str, enum iio_val_type type)
{
	int integer = 0, fract = 0;
	int fract_mult;
	int ret;

	fract_mult = iio_val_fract_mult(type);
	if (fract_mult < 0) {
		TEST_ASSERT(false, "type is not fixed-point");
		return;
	}

	ret = iio_str_to_fixpoint(str, fract_mult, &integer, &fract);
	if (ret) {
		TEST_ASSERT_INT_EQUAL(ret, 0, str);
		return;
	}

	TEST_ASSERT_STR_EQ(fmt_str(type, integer, fract), str, str);
}

TEST_FUNCTION(roundtrip)
{
	check_roundtrip("1.500000", IIO_VAL_INT_PLUS_MICRO);
	check_roundtrip("0.297619", IIO_VAL_INT_PLUS_MICRO);
	check_roundtrip("-1.500000", IIO_VAL_INT_PLUS_MICRO);
	check_roundtrip("-0.500000", IIO_VAL_INT_PLUS_MICRO);
	check_roundtrip("0.000000", IIO_VAL_INT_PLUS_MICRO);
	check_roundtrip("2.000000025", IIO_VAL_INT_PLUS_NANO);
	check_roundtrip("42", IIO_VAL_INT);
	check_roundtrip("-42", IIO_VAL_INT);
}

int main(void)
{
	DEBUG_PRINT("=== no-OS iio_val tests (host) ===\n");

	RUN_TEST(format_int);
	RUN_TEST(format_int_plus_micro);
	RUN_TEST(format_int_plus_nano);
	RUN_TEST(format_fractional);
	RUN_TEST(format_int_64);
	RUN_TEST(format_char);
	RUN_TEST(format_rejects_bad_input);
	RUN_TEST(format_truncation);
	RUN_TEST(parse_integer);
	RUN_TEST(parse_fixpoint);
	RUN_TEST(parse_negative);
	RUN_TEST(parse_rejects_garbage);
	RUN_TEST(parse_out_of_range);
	RUN_TEST(parse_rejects_null);
	RUN_TEST(fract_mult_per_type);
	RUN_TEST(roundtrip);

	TEST_SUMMARY();

	return 0;
}
