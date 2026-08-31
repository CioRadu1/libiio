/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <errno.h>
#include <no_os_print_log.h>
#include <no_os_delay.h>
#include <tinyiiod/tinyiiod.h>

#include "mxc_sys.h"
#include "mxc_errors.h"
#include "mcr_regs.h"
#include "usb.h"
#include "usb_event.h"
#include "enumerate.h"
#include "nvic_table.h"

#define IIO_USB_EP_IN		1
#define IIO_USB_EP_OUT		2
#define IIO_USB_EP1_IN		3
#define IIO_USB_EP1_OUT		4
#define IIO_USB_NUM_PIPES	2

#define USB_IO_TIMEOUT_MS	10000

#define IIO_USD_CMD_RESET_PIPES	 0
#define IIO_USD_CMD_OPEN_PIPE	 1
#define IIO_USD_CMD_CLOSE_PIPE	 2

/* Device descriptor: VID=0x0456 (ADI), PID=0xB673 (IIO) */
static MXC_USB_device_descriptor_t __attribute__((aligned(4))) dev_desc = {
	0x12, 0x01, 0x0200, 0x02, 0x00, 0x00, 0x40,
	0x0456, 0xB673, 0x0100, 0x01, 0x02, 0x03, 0x01,
};

static MXC_USB_device_qualifier_descriptor_t __attribute__((aligned(4)))
dev_qual_desc = {
	0x0A, 0x06, 0x0200, 0x02, 0x00, 0x00, 0x40, 0x01, 0x00,
};

/* Full-speed config: 4 bulk endpoints (2 pipes), 64-byte packets */
static __attribute__((aligned(4))) struct __attribute__((packed)) {
	MXC_USB_configuration_descriptor_t	config;
	MXC_USB_interface_descriptor_t		iface;
	MXC_USB_endpoint_descriptor_t		ep_in;
	MXC_USB_endpoint_descriptor_t		ep_out;
	MXC_USB_endpoint_descriptor_t		ep1_in;
	MXC_USB_endpoint_descriptor_t		ep1_out;
} cfg_desc = {
	{ 0x09, 0x02, 0x002E, 0x01, 0x01, 0x00, 0x80, 0xFA },
	{ 0x09, 0x04, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x04 },
	{ 0x07, 0x05, 0x81, 0x02, 0x0040, 0x00 },
	{ 0x07, 0x05, 0x02, 0x02, 0x0040, 0x00 },
	{ 0x07, 0x05, 0x83, 0x02, 0x0040, 0x00 },
	{ 0x07, 0x05, 0x04, 0x02, 0x0040, 0x00 },
};

/* High-speed config: 4 bulk endpoints (2 pipes), 512-byte packets */
static __attribute__((aligned(4))) struct __attribute__((packed)) {
	MXC_USB_configuration_descriptor_t	config;
	MXC_USB_interface_descriptor_t		iface;
	MXC_USB_endpoint_descriptor_t		ep_in;
	MXC_USB_endpoint_descriptor_t		ep_out;
	MXC_USB_endpoint_descriptor_t		ep1_in;
	MXC_USB_endpoint_descriptor_t		ep1_out;
} cfg_desc_hs = {
	{ 0x09, 0x02, 0x002E, 0x01, 0x01, 0x00, 0x80, 0xFA },
	{ 0x09, 0x04, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x04 },
	{ 0x07, 0x05, 0x81, 0x02, 0x0200, 0x00 },
	{ 0x07, 0x05, 0x02, 0x02, 0x0200, 0x00 },
	{ 0x07, 0x05, 0x83, 0x02, 0x0200, 0x00 },
	{ 0x07, 0x05, 0x04, 0x02, 0x0200, 0x00 },
};

/* String descriptors (UTF-16LE) */
static __attribute__((aligned(4))) uint8_t str_lang[] = {
	0x04, 0x03, 0x09, 0x04
};

static __attribute__((aligned(4))) uint8_t str_mfg[] = {
	0x1E, 0x03,
	'A', 0, 'n', 0, 'a', 0, 'l', 0, 'o', 0, 'g', 0, ' ', 0,
	'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 's', 0,
};

static __attribute__((aligned(4))) uint8_t str_prod[] = {
	0x1A, 0x03,
	'M', 0, 'A', 0, 'X', 0, '3', 0, '2', 0, '6', 0, '9', 0, '0', 0,
	' ', 0, 'I', 0, 'I', 0, 'O', 0,
};

static __attribute__((aligned(4))) uint8_t str_serial[] = {
	0x14, 0x03,
	'0', 0, '0', 0, '0', 0, '0', 0, '0', 0,
	'0', 0, '0', 0, '0', 0, '1', 0,
};

/* iInterface string — host matches on "IIO" to find this device */
static __attribute__((aligned(4))) uint8_t str_iio[] = {
	0x08, 0x03, 'I', 0, 'I', 0, 'O', 0,
};

static volatile int configured;
static volatile int suspended;
static volatile unsigned int event_flags;
static volatile int pipe_active;
static volatile int pipe1_opened;
static volatile int write_complete;
static volatile int write_error;

/*
 * Per-pipe read completion state.  When pipe 1 is open the interpreter
 * polls both OUT endpoints and processes whichever has data first.
 * current_read_pipe records which pipe delivered the last read so that
 * iiod_usb_write() sends the response on the matching IN endpoint.
 */
static volatile int read_complete_p0;
static volatile int read_error_p0;
static volatile int read_complete_p1;
static volatile int read_error_p1;
static volatile int current_read_pipe;

/*
 * When pipe_locked is 0, iiod_usb_read() multiplexes both pipes
 * (used for reading a new 8-byte command header).  After a command
 * header is read, pipe_locked is set to 1 so that subsequent data
 * reads for the same command stay on the same pipe.  It is cleared
 * back to 0 at the end of iiod_usb_write() — the response write
 * that always follows a command before the next header read.
 */
static volatile int pipe_locked;

static int usb_pdata;

static void delay_us(unsigned int usec)
{
	no_os_udelay((uint32_t)usec);
}

static void usb_read_cb_p0(void *cbdata)
{
	MXC_USB_Req_t *req = (MXC_USB_Req_t *)cbdata;

	read_error_p0 = req->error_code;
	read_complete_p0 = 1;
}

static void usb_read_cb_p1(void *cbdata)
{
	MXC_USB_Req_t *req = (MXC_USB_Req_t *)cbdata;

	read_error_p1 = req->error_code;
	read_complete_p1 = 1;
}

static void usb_write_cb(void *cbdata)
{
	MXC_USB_Req_t *req = (MXC_USB_Req_t *)cbdata;

	write_error = req->error_code;
	write_complete = 1;
}

static int usb_startup_cb(void)
{
	MXC_SYS_ClockSourceEnable(MXC_SYS_CLOCK_IPO);
	MXC_MCR->ldoctrl |= MXC_F_MCR_LDOCTRL_0P9EN;
	MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_USB);
	MXC_SYS_Reset_Periph(MXC_SYS_RESET0_USB);

	return E_NO_ERROR;
}

static int usb_shutdown_cb(void)
{
	MXC_SYS_ClockDisable(MXC_SYS_PERIPH_CLOCK_USB);

	return E_NO_ERROR;
}

static void usb_cancel_pending(void)
{
	MXC_USB_Req_t *r;

	r = MXC_USB_GetRequest(IIO_USB_EP_OUT);
	if (r)
		MXC_USB_RemoveRequest(r);

	r = MXC_USB_GetRequest(IIO_USB_EP_IN);
	if (r)
		MXC_USB_RemoveRequest(r);

	r = MXC_USB_GetRequest(IIO_USB_EP1_OUT);
	if (r)
		MXC_USB_RemoveRequest(r);

	r = MXC_USB_GetRequest(IIO_USB_EP1_IN);
	if (r)
		MXC_USB_RemoveRequest(r);
}

static int usb_event_cb(maxusb_event_t evt, void *data)
{
	MXC_SETBIT(&event_flags, evt);

	switch (evt) {
	case MAXUSB_EVENT_NOVBUS:
		pr_info("USB: NOVBUS (cable disconnect)\n");
		MXC_USB_EventDisable(MAXUSB_EVENT_BRST);
		MXC_USB_EventDisable(MAXUSB_EVENT_SUSP);
		MXC_USB_EventDisable(MAXUSB_EVENT_DPACT);
		MXC_USB_Disconnect();
		configured = 0;
		pipe_active = 0;
		pipe1_opened = 0;
		usb_cancel_pending();
		enum_clearconfig();
		break;

	case MAXUSB_EVENT_VBUS:
		pr_info("USB: VBUS (cable connect)\n");
		MXC_USB_EventClear(MAXUSB_EVENT_BRST);
		MXC_USB_EventEnable(MAXUSB_EVENT_BRST, usb_event_cb, NULL);
		MXC_USB_EventClear(MAXUSB_EVENT_BRSTDN);
		MXC_USB_EventEnable(MAXUSB_EVENT_BRSTDN, usb_event_cb, NULL);
		MXC_USB_EventClear(MAXUSB_EVENT_SUSP);
		MXC_USB_EventEnable(MAXUSB_EVENT_SUSP, usb_event_cb, NULL);
		MXC_USB_Connect();
		break;

	case MAXUSB_EVENT_BRST:
		pr_info("USB: bus reset\n");
		enum_clearconfig();
		configured = 0;
		pipe_active = 0;
		pipe1_opened = 0;
		usb_cancel_pending();
		suspended = 0;
		break;

	case MAXUSB_EVENT_BRSTDN:
		if (MXC_USB_GetStatus() & MAXUSB_STATUS_HIGH_SPEED) {
			enum_register_descriptor(ENUM_DESC_CONFIG,
						 (uint8_t *)&cfg_desc_hs, 0);
			enum_register_descriptor(ENUM_DESC_OTHER,
						 (uint8_t *)&cfg_desc, 0);
		} else {
			enum_register_descriptor(ENUM_DESC_CONFIG,
						 (uint8_t *)&cfg_desc, 0);
			enum_register_descriptor(ENUM_DESC_OTHER,
						 (uint8_t *)&cfg_desc_hs, 0);
		}
		break;

	case MAXUSB_EVENT_SUSP:
		suspended = 1;
		break;

	case MAXUSB_EVENT_DPACT:
		suspended = 0;
		break;

	default:
		break;
	}

	return 0;
}

static int usb_setconfig_cb(MXC_USB_SetupPkt *sud, void *cbdata)
{
	if (sud->wValue == cfg_desc.config.bConfigurationValue) {
		unsigned int in_pkt = cfg_desc.ep_in.wMaxPacketSize;
		unsigned int out_pkt = cfg_desc.ep_out.wMaxPacketSize;

		if (MXC_USB_GetStatus() & MAXUSB_STATUS_HIGH_SPEED) {
			in_pkt = cfg_desc_hs.ep_in.wMaxPacketSize;
			out_pkt = cfg_desc_hs.ep_out.wMaxPacketSize;
		}

		MXC_USB_ConfigEp(IIO_USB_EP_IN, MAXUSB_EP_TYPE_IN, in_pkt);
		MXC_USB_ConfigEp(IIO_USB_EP_OUT, MAXUSB_EP_TYPE_OUT, out_pkt);
		MXC_USB_ConfigEp(IIO_USB_EP1_IN, MAXUSB_EP_TYPE_IN, in_pkt);
		MXC_USB_ConfigEp(IIO_USB_EP1_OUT, MAXUSB_EP_TYPE_OUT, out_pkt);

		configured = 1;
		return 0;
	} else if (sud->wValue == 0) {
		configured = 0;
		pipe_active = 0;
		pipe1_opened = 0;
		usb_cancel_pending();
		return 0;
	}

	return -1;
}

static int usb_setfeature_cb(MXC_USB_SetupPkt *sud, void *cbdata)
{
	if (sud->wValue == FEAT_REMOTE_WAKE)
		return 0;

	return -1;
}

static int usb_clrfeature_cb(MXC_USB_SetupPkt *sud, void *cbdata)
{
	if (sud->wValue == FEAT_REMOTE_WAKE)
		return 0;

	return -1;
}

static int usb_vendor_req_cb(MXC_USB_SetupPkt *sud, void *cbdata)
{
	switch (sud->bRequest) {
	case IIO_USD_CMD_RESET_PIPES:
		pr_info("USB: RESET_PIPES\n");
		pipe_active = 0;
		pipe1_opened = 0;
		usb_cancel_pending();
		return 0;

	case IIO_USD_CMD_OPEN_PIPE:
		if (sud->wValue >= IIO_USB_NUM_PIPES)
			return -1;
		pr_info("USB: OPEN_PIPE(%u)\n", sud->wValue);
		if (sud->wValue == 0) {
			pipe_active = 1;
		} else {
			/*
			 * Don't kill the pipe 0 interpreter.  The
			 * multiplexed iiod_usb_read() will pick up
			 * pipe 1 data on the next read cycle.
			 */
			pipe1_opened = 1;
		}
		return 0;

	case IIO_USD_CMD_CLOSE_PIPE:
		if (sud->wValue >= IIO_USB_NUM_PIPES)
			return -1;
		pr_info("USB: CLOSE_PIPE(%u)\n", sud->wValue);
		if (sud->wValue == 0) {
			pipe_active = 0;
			usb_cancel_pending();
		} else {
			MXC_USB_Req_t *r;

			pipe1_opened = 0;

			r = MXC_USB_GetRequest(IIO_USB_EP1_OUT);
			if (r)
				MXC_USB_RemoveRequest(r);
			r = MXC_USB_GetRequest(IIO_USB_EP1_IN);
			if (r)
				MXC_USB_RemoveRequest(r);
		}
		return 0;

	default:
		return -1;
	}
}

static void usb_remove_request_safe(MXC_USB_Req_t *req)
{
	NVIC_DisableIRQ(USB_IRQn);
	MXC_USB_RemoveRequest(req);
	NVIC_EnableIRQ(USB_IRQn);
}

static ssize_t iiod_usb_read_single(void *buf, size_t size, int pipe)
{
	uint32_t total = 0;
	int ep = pipe ? IIO_USB_EP1_OUT : IIO_USB_EP_OUT;
	void (*cb)(void *) = pipe ? usb_read_cb_p1 : usb_read_cb_p0;
	volatile int *compl = pipe ? &read_complete_p1 : &read_complete_p0;
	volatile int *err   = pipe ? &read_error_p1   : &read_error_p0;

	while (total < size) {
		MXC_USB_Req_t req = {0};
		struct no_os_time start, now;
		int ret;

		req.ep = ep;
		req.data = (uint8_t *)buf + total;
		req.reqlen = size - total;
		req.callback = cb;
		req.cbdata = &req;
		req.type = MAXUSB_TYPE_TRANS;

		*compl = 0;
		*err = 0;

		ret = MXC_USB_ReadEndpoint(&req);
		if (ret)
			return -EIO;

		start = no_os_get_time();

		while (!*compl) {
			if (!configured || !pipe_active) {
				usb_remove_request_safe(&req);
				return -ENODEV;
			}

			now = no_os_get_time();
			if ((now.s - start.s) * 1000 +
			    (int)(now.us - start.us) / 1000
			    >= USB_IO_TIMEOUT_MS) {
				usb_remove_request_safe(&req);
				return -ETIMEDOUT;
			}
		}

		if (!configured || !pipe_active)
			return -ENODEV;
		if (*err || req.actlen == 0)
			return *err ? -EIO : -ENODEV;

		total += req.actlen;
	}

	return (ssize_t)size;
}

static ssize_t iiod_usb_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	(void)pdata;

	/*
	 * If the pipe is locked (we already read a command header and
	 * are now reading its payload data), read from the same pipe
	 * that delivered the command header.
	 */
	if (pipe_locked) {
		pr_info("USB RD: pipe%d (locked), %u bytes\n",
			 current_read_pipe, (unsigned)size);
		return iiod_usb_read_single(buf, size, current_read_pipe);
	}

	/*
	 * Multiplexed read: start reads on pipe 0 and (if open) pipe 1.
	 * The first pipe to deliver data wins.  If pipe 1 is opened by
	 * the IRQ handler while we are already waiting, start the pipe 1
	 * read on the fly.
	 */
	{
		MXC_USB_Req_t req0 = {0}, req1 = {0};
		struct no_os_time start, now;
		int ret, got_pipe = -1, p1_started = 0;

		req0.ep = IIO_USB_EP_OUT;
		req0.data = (uint8_t *)buf;
		req0.reqlen = size;
		req0.callback = usb_read_cb_p0;
		req0.cbdata = &req0;
		req0.type = MAXUSB_TYPE_TRANS;

		read_complete_p0 = 0;
		read_error_p0 = 0;

		ret = MXC_USB_ReadEndpoint(&req0);
		if (ret)
			return -EIO;

		if (pipe1_opened) {
			req1.ep = IIO_USB_EP1_OUT;
			req1.data = (uint8_t *)buf;
			req1.reqlen = size;
			req1.callback = usb_read_cb_p1;
			req1.cbdata = &req1;
			req1.type = MAXUSB_TYPE_TRANS;

			read_complete_p1 = 0;
			read_error_p1 = 0;

			ret = MXC_USB_ReadEndpoint(&req1);
			if (ret) {
				usb_remove_request_safe(&req0);
				return -EIO;
			}
			p1_started = 1;
		}

		start = no_os_get_time();

		while (got_pipe < 0) {
			if (!configured || !pipe_active) {
				usb_remove_request_safe(&req0);
				if (p1_started)
					usb_remove_request_safe(&req1);
				return -ENODEV;
			}

			if (pipe1_opened && !p1_started) {
				req1.ep = IIO_USB_EP1_OUT;
				req1.data = (uint8_t *)buf;
				req1.reqlen = size;
				req1.callback = usb_read_cb_p1;
				req1.cbdata = &req1;
				req1.type = MAXUSB_TYPE_TRANS;

				read_complete_p1 = 0;
				read_error_p1 = 0;

				ret = MXC_USB_ReadEndpoint(&req1);
				if (!ret)
					p1_started = 1;
			}

			if (read_complete_p0)
				got_pipe = 0;
			else if (p1_started && read_complete_p1)
				got_pipe = 1;

			if (got_pipe < 0) {
				now = no_os_get_time();
				if ((now.s - start.s) * 1000 +
				    (int)(now.us - start.us) / 1000
				    >= USB_IO_TIMEOUT_MS) {
					usb_remove_request_safe(&req0);
					if (p1_started)
						usb_remove_request_safe(&req1);
					return -ETIMEDOUT;
				}
			}
		}

		if (got_pipe == 0 && p1_started)
			usb_remove_request_safe(&req1);
		else if (got_pipe == 1)
			usb_remove_request_safe(&req0);

		if (!configured || !pipe_active)
			return -ENODEV;

		if (got_pipe == 0) {
			if (read_error_p0 || req0.actlen == 0)
				return read_error_p0 ? -EIO : -ENODEV;
			current_read_pipe = 0;
		} else {
			if (read_error_p1 || req1.actlen == 0)
				return read_error_p1 ? -EIO : -ENODEV;
			current_read_pipe = 1;
		}

		pr_info("USB RD: pipe%d (mux, p1_open=%d), %u bytes\n",
			 got_pipe, p1_started, (unsigned)size);

		/* Lock to this pipe until the response write unlocks it */
		pipe_locked = 1;

		/*
		 * If we got a partial read, finish reading the rest
		 * from the same pipe.
		 */
		if (got_pipe == 0 && req0.actlen < size)
			return iiod_usb_read_single(
				(uint8_t *)buf + req0.actlen,
				size - req0.actlen, 0);
		if (got_pipe == 1 && req1.actlen < size)
			return iiod_usb_read_single(
				(uint8_t *)buf + req1.actlen,
				size - req1.actlen, 1);

		return (ssize_t)size;
	}
}

static ssize_t iiod_usb_write(struct iiod_pdata *pdata, const void *buf,
			      size_t size)
{
	uint32_t total = 0;
	int ep_in;

	(void)pdata;

	/* Route the response to the same pipe the command came from */
	ep_in = current_read_pipe ? IIO_USB_EP1_IN : IIO_USB_EP_IN;

	pr_info("USB WR: pipe%d, %u bytes\n",
		 current_read_pipe, (unsigned)size);

	while (total < size) {
		MXC_USB_Req_t req = {0};
		struct no_os_time start, now;
		int ret;

		req.ep = ep_in;
		req.data = (uint8_t *)buf + total;
		req.reqlen = size - total;
		req.callback = usb_write_cb;
		req.cbdata = &req;
		req.type = MAXUSB_TYPE_TRANS;

		write_complete = 0;
		write_error = 0;

		ret = MXC_USB_WriteEndpoint(&req);
		if (ret)
			return -EIO;

		start = no_os_get_time();

		while (!write_complete) {
			if (!configured || !pipe_active) {
				usb_remove_request_safe(&req);
				return -ENODEV;
			}

			now = no_os_get_time();
			if ((now.s - start.s) * 1000 +
			    (int)(now.us - start.us) / 1000 >= USB_IO_TIMEOUT_MS) {
				usb_remove_request_safe(&req);
				return -ETIMEDOUT;
			}
		}

		if (!configured || !pipe_active)
			return -ENODEV;
		if (write_error)
			return -EIO;
		if (req.actlen == 0)
			return -ENODEV;

		total += req.actlen;
	}

	/*
	 * After a response is fully written, unlock the pipe so
	 * the next iiod_usb_read() will multiplex both pipes again
	 * when reading the next command header.
	 */
	pipe_locked = 0;

	return (ssize_t)size;
}

static int iiod_usb_wait_for_handshake(void)
{
	static const char binary_hdr[] = "BINARY\r\n";
	static const uint8_t ok_resp[] = "0\r\n";
	uint8_t buf[8];
	ssize_t ret;

	ret = iiod_usb_read((struct iiod_pdata *)&usb_pdata, buf, 8);
	if (ret < 0)
		return (int)ret;

	if (memcmp(buf, binary_hdr, 8) != 0)
		return -EINVAL;

	ret = iiod_usb_write((struct iiod_pdata *)&usb_pdata, ok_resp, 3);
	if (ret < 0)
		return (int)ret;

	return 0;
}

static void iio_usb_irq_handler(void)
{
	MXC_USB_EventHandler();
}

static int usb_hw_init(void)
{
	maxusb_cfg_options_t opts;
	int ret;

	configured = 0;
	suspended = 0;
	event_flags = 0;
	pipe_active = 0;

	opts.enable_hs = 1;
	opts.delay_us = delay_us;
	opts.init_callback = usb_startup_cb;
	opts.shutdown_callback = usb_shutdown_cb;

	ret = MXC_USB_Init(&opts);
	if (ret) {
		pr_err("MXC_USB_Init failed: %d\n", ret);
		return -EIO;
	}

	ret = enum_init();
	if (ret) {
		pr_err("enum_init failed: %d\n", ret);
		return -EIO;
	}

	enum_register_descriptor(ENUM_DESC_DEVICE, (uint8_t *)&dev_desc, 0);
	enum_register_descriptor(ENUM_DESC_CONFIG, (uint8_t *)&cfg_desc, 0);
	enum_register_descriptor(ENUM_DESC_OTHER, (uint8_t *)&cfg_desc_hs, 0);
	enum_register_descriptor(ENUM_DESC_QUAL, (uint8_t *)&dev_qual_desc, 0);

	enum_register_descriptor(ENUM_DESC_STRING, str_lang, 0);
	enum_register_descriptor(ENUM_DESC_STRING, str_mfg, 1);
	enum_register_descriptor(ENUM_DESC_STRING, str_prod, 2);
	enum_register_descriptor(ENUM_DESC_STRING, str_serial, 3);
	enum_register_descriptor(ENUM_DESC_STRING, str_iio, 4);

	enum_register_callback(ENUM_SETCONFIG, usb_setconfig_cb, NULL);
	enum_register_callback(ENUM_SETFEATURE, usb_setfeature_cb, NULL);
	enum_register_callback(ENUM_CLRFEATURE, usb_clrfeature_cb, NULL);
	enum_register_callback(ENUM_VENDOR_REQ, usb_vendor_req_cb, NULL);

	MXC_USB_EventEnable(MAXUSB_EVENT_NOVBUS, usb_event_cb, NULL);
	MXC_USB_EventEnable(MAXUSB_EVENT_VBUS, usb_event_cb, NULL);

	MXC_NVIC_SetVector(USB_IRQn, iio_usb_irq_handler);
	NVIC_EnableIRQ(USB_IRQn);

	return 0;
}

int iiod_usb_run(void)
{
	struct iio_context_params ctx_params = {0};
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;
	int ret;

	ret = usb_hw_init();
	if (ret)
		return ret;

	pr_info("USB initialized, waiting for host...\n");

	ret = iiod_init();
	if (ret < 0) {
		pr_err("iiod_init failed: %d\n", ret);
		return ret;
	}

	ctx = iio_create_context(&ctx_params, "no-os:");
	if (iio_err(ctx)) {
		pr_err("iio_create_context failed\n");
		iiod_cleanup();
		return -1;
	}

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		pr_err("iio_context_get_xml failed\n");
		iio_context_destroy(ctx);
		iiod_cleanup();
		return -1;
	}

	xml_len = strlen(xml) + 1;

	pr_info("IIO context ready (%u bytes XML)\n", (unsigned int)xml_len);

	while (1) {
		pr_info("USB: waiting for USB configured...\n");
		while (!configured)
			;

		pr_info("USB: configured, waiting for OPEN_PIPE(0)...\n");
		while (!pipe_active)
			;

		current_read_pipe = 0;
		pipe_locked = 0;
		read_complete_p0 = 0;
		read_complete_p1 = 0;
		read_error_p0 = 0;
		read_error_p1 = 0;
		write_complete = 0;
		write_error = 0;

		pr_info("USB: pipe 0 active, waiting for handshake...\n");
		ret = iiod_usb_wait_for_handshake();
		if (ret < 0) {
			if (ret != -ENODEV && ret != -ETIMEDOUT)
				pr_err("USB handshake failed: %d\n", ret);
			else
				pr_info("USB: handshake aborted: %d\n", ret);
			continue;
		}

		pr_info("USB IIO session started\n");

		/*
		 * A single interpreter handles both pipes.  When the
		 * host opens pipe 1, the multiplexed iiod_usb_read()
		 * picks up data from either endpoint and routes
		 * responses back via iiod_usb_write().  The pipe 1
		 * "BINARY\r\n" re-handshake is handled inline by
		 * iiod_responder_reader_worker().
		 */
		ret = iiod_interpreter(ctx,
				       (struct iiod_pdata *)&usb_pdata,
				       iiod_usb_read, iiod_usb_write,
				       xml, xml_len);

		pr_info("USB: interpreter exited, cleaning up "
			 "(pipe_active=%d, pipe1=%d)\n",
			 pipe_active, pipe1_opened);

		NVIC_DisableIRQ(USB_IRQn);
		usb_cancel_pending();
		pipe1_opened = 0;
		current_read_pipe = 0;
		pipe_locked = 0;
		NVIC_EnableIRQ(USB_IRQn);

		pr_info("USB IIO session ended: %d\n", ret);
	}

	iio_context_destroy(ctx);
	iiod_cleanup();

	return ret;
}
