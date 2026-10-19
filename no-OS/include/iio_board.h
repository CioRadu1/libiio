/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NOOS_IIO_BOARD_H_
#define NOOS_IIO_BOARD_H_

struct noos_iio_device_info;

/**
 * struct noos_board_device - one row of a board's device list
 * @init:     bring the device up (may be NULL)
 * @get_info: fill the info the backend registers the device with
 *
 * A board lists its devices in boards/<BOARD>.c, or boards/default.c when
 * it has no file of its own: the no-OS stand-in for a Zephyr devicetree.
 * The backend runs every row once, when the first context is created.
 */
struct noos_board_device {
	int (*init)(void);
	int (*get_info)(struct noos_iio_device_info *info);
};

extern const struct noos_board_device noos_board_devices[];
extern const unsigned int noos_board_device_count;

#endif /* NOOS_IIO_BOARD_H_ */
