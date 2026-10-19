/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <iio_device.h>

/*
 * The devices come from the board file (boards/<BOARD>.c), so all that is
 * left here is to start IIOD on this build's transport.
 */
int main(void)
{
	return noos_iiod_run();
}
