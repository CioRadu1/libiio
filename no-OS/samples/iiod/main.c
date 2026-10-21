/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <noos_iiod.h>

/* The devices and settings are in common_data.c. */
extern const struct noos_iiod_param iiod_param;

int main(void)
{
	return noos_iiod_run(&iiod_param);
}
