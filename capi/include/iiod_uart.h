/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CAPI_INCLUDE_IIOD_UART_H_
#define CAPI_INCLUDE_IIOD_UART_H_

#include <capi_uart.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Serve the registered devices to a libiio client on an initialized UART
 * (URI serial:<port>,<baud> on the host). Zephyr starts the same loop in
 * a thread of its own; CAPI has no threads, so the application calls this
 * last. Only returns on error. Nothing else may use the UART meanwhile.
 *
 * Receive is interrupt driven when the capi_uart supports it: the
 * application then routes the UART's interrupt vector to capi_uart_isr()
 * (CAPI leaves the vectors to it). Otherwise it falls back to blocking
 * capi_uart_receive(), which can lose bytes under streaming. */
int iiod_uart_run(struct capi_uart_handle *uart);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_INCLUDE_IIOD_UART_H_ */
