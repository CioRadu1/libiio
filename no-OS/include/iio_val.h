/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_INCLUDE_IIO_VAL_H_
#define NOOS_INCLUDE_IIO_VAL_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IIO attribute value types.
 *
 * These mirror the kernel's IIO_VAL_* constants (industrialio-core.c,
 * __iio_format_value / iio_write_channel_info) so the on-the-wire string
 * representation of an attribute matches what a Linux IIO device produces.
 * A driver reports {vals[], type} and the helpers below turn that into /
 * parse it from the attribute string, replacing per-attribute hand-rolled
 * formatting.
 *
 * Only the common subset is implemented; extend as needed.
 */
enum iio_val_type {
	IIO_VAL_INT		= 1,	/* vals[0]                 "%d"       */
	IIO_VAL_INT_PLUS_MICRO	= 2,	/* vals[0].vals[1] (1e-6)  "%d.%06u"  */
	IIO_VAL_INT_PLUS_NANO	= 3,	/* vals[0].vals[1] (1e-9)  "%d.%09u"  */
	IIO_VAL_INT_64		= 6,	/* (vals[1]<<32)|vals[0]   "%lld"     */
	IIO_VAL_FRACTIONAL	= 10,	/* vals[0] / vals[1]       "%d.%09u"  */
	IIO_VAL_CHAR		= 12,	/* vals[0] as a character  "%c"       */
};

/**
 * iio_format_value() - format an IIO value into its string representation
 * @buf:  destination buffer
 * @len:  size of @buf
 * @type: one of enum iio_val_type; decides how @vals is formatted
 * @size: number of entries in @vals (only used by multi-value types)
 * @vals: pointer to the value(s); meaning depends on @type
 *
 * Returns the number of characters written (excluding the NUL terminator),
 * or a negative error code.
 */
int iio_format_value(char *buf, size_t len, enum iio_val_type type,
		     int size, const int *vals);

/**
 * iio_str_to_fixpoint() - parse a fixed-point number from a string
 * @str:        the string to parse
 * @fract_mult: multiplier for the first decimal place (power of 10); 0 parses
 *              a plain integer
 * @integer:    output for the integer part
 * @fract:      output for the fractional part
 *
 * Returns 0 on success, or a negative error code if @str could not be parsed.
 */
int iio_str_to_fixpoint(const char *str, int fract_mult,
			int *integer, int *fract);

/**
 * iio_val_fract_mult() - fractional multiplier to use when parsing @type
 * @type: one of enum iio_val_type
 *
 * Returns the fract_mult value to pass to iio_str_to_fixpoint() for @type
 * (0 for plain integers), or a negative error code for types that are not
 * parsed as fixed-point numbers (e.g. IIO_VAL_CHAR, IIO_VAL_INT_64).
 */
int iio_val_fract_mult(enum iio_val_type type);

#ifdef __cplusplus
}
#endif

#endif /* NOOS_INCLUDE_IIO_VAL_H_ */
