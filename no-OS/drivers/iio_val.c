/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Type-driven IIO attribute value formatting and parsing.
 *
 * Ported from the Linux kernel's drivers/iio/industrialio-core.c
 * (__iio_format_value / __iio_str_to_fixpoint), reduced to the common subset
 * and using snprintf() / plain integer arithmetic instead of the kernel's
 * sysfs_emit_at() and div_s64() helpers. The string representation is kept
 * identical so a no-OS device behaves like a Linux IIO device.
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "iio_val.h"

int iio_format_value(char *buf, size_t len, enum iio_val_type type,
		     int size, const int *vals)
{
	if (!buf || !vals || len == 0)
		return -EINVAL;

	switch (type) {
	case IIO_VAL_INT:
		return snprintf(buf, len, "%d", vals[0]);
	case IIO_VAL_INT_PLUS_MICRO:
		if (vals[1] < 0)
			return snprintf(buf, len, "-%d.%06u",
					abs(vals[0]), (unsigned int)-vals[1]);
		else
			return snprintf(buf, len, "%d.%06u",
					vals[0], (unsigned int)vals[1]);
	case IIO_VAL_INT_PLUS_NANO:
		if (vals[1] < 0)
			return snprintf(buf, len, "-%d.%09u",
					abs(vals[0]), (unsigned int)-vals[1]);
		else
			return snprintf(buf, len, "%d.%09u",
					vals[0], (unsigned int)vals[1]);
	case IIO_VAL_FRACTIONAL: {
		int64_t tmp2;
		int tmp0, tmp1;

		if (vals[1] == 0)
			return -EINVAL;
		tmp2 = (int64_t)vals[0] * 1000000000LL / vals[1];
		tmp0 = (int)(tmp2 / 1000000000);
		tmp1 = (int)(tmp2 % 1000000000);
		if (tmp2 < 0 && tmp0 == 0)
			return snprintf(buf, len, "-0.%09u",
					(unsigned int)abs(tmp1));
		else
			return snprintf(buf, len, "%d.%09u",
					tmp0, (unsigned int)abs(tmp1));
	}
	case IIO_VAL_INT_64: {
		int64_t tmp2 = (int64_t)((((uint64_t)vals[1]) << 32) |
					 (uint32_t)vals[0]);

		return snprintf(buf, len, "%lld", (long long)tmp2);
	}
	case IIO_VAL_CHAR:
		return snprintf(buf, len, "%c", (char)vals[0]);
	default:
		return -EINVAL;
	}
}

int iio_str_to_fixpoint(const char *str, int fract_mult,
			int *integer, int *fract)
{
	int i = 0, f = 0;
	bool integer_part = true, negative = false;

	if (!str || !integer || !fract)
		return -EINVAL;

	if (fract_mult == 0) {
		char *end;
		long val;

		*fract = 0;
		val = strtol(str, &end, 0);
		if (end == str)
			return -EINVAL;
		while (*end == '\n')
			end++;
		if (*end != '\0')
			return -EINVAL;
		*integer = (int)val;
		return 0;
	}

	if (str[0] == '-') {
		negative = true;
		str++;
	} else if (str[0] == '+') {
		str++;
	}

	while (*str) {
		if ('0' <= *str && *str <= '9') {
			if (integer_part) {
				i = i * 10 + (*str - '0');
			} else {
				f += fract_mult * (*str - '0');
				fract_mult /= 10;
			}
		} else if (*str == '\n') {
			if (*(str + 1) == '\0')
				break;
			return -EINVAL;
		} else if (*str == '.' && integer_part) {
			integer_part = false;
		} else {
			return -EINVAL;
		}
		str++;
	}

	if (negative) {
		if (i)
			i = -i;
		else
			f = -f;
	}

	*integer = i;
	*fract = f;

	return 0;
}

int iio_val_fract_mult(enum iio_val_type type)
{
	switch (type) {
	case IIO_VAL_INT:
		return 0;
	case IIO_VAL_INT_PLUS_MICRO:
		return 100000;
	case IIO_VAL_INT_PLUS_NANO:
		return 100000000;
	default:
		/* CHAR / INT_64 / FRACTIONAL are not parsed as fixed-point */
		return -EINVAL;
	}
}
