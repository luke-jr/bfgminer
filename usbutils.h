/*
 * Copyright 2012-2013 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef USBUTILS_H
#define USBUTILS_H

#include <libusb.h>

// for 0x0403/0x6014 FT232H (and possibly others?)
#define FTDI_TYPE_OUT (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT)

#define FTDI_REQUEST_RESET ((uint8_t)0)
#define FTDI_REQUEST_MODEM ((uint8_t)1)
#define FTDI_REQUEST_FLOW ((uint8_t)2)
#define FTDI_REQUEST_BAUD ((uint8_t)3)
#define FTDI_REQUEST_DATA ((uint8_t)4)

#define FTDI_VALUE_RESET 0
#define FTDI_VALUE_PURGE_RX 1
#define FTDI_VALUE_PURGE_TX 2

// baud with a 0 divisor is 120,000,000/10
//#define FTDI_VALUE_BAUD (0)
//#define FTDI_INDEX_BAUD (0)
#define FTDI_VALUE_BAUD 0xc068
#define FTDI_INDEX_BAUD 0x0200

#define FTDI_VALUE_DATA 0
#define FTDI_VALUE_FLOW 0
#define FTDI_VALUE_MODEM 0x0303

// Use the device defined timeout
#define DEVTIMEOUT 0

// For endpoints defined in usb_find_devices.eps,
// the first two must be the default IN and OUT
#define DEFAULT_EP_IN 0
#define DEFAULT_EP_OUT 1

struct usb_endpoints {
	uint8_t att;
	uint16_t size;
	unsigned char ep;
	bool found;
};

struct usb_find_devices {
	int drv;
	const char *name;
	uint16_t idVendor;
	uint16_t idProduct;
	int kernel;
	int config;
	int interface;
	unsigned int timeout;
	int epcount;
	struct usb_endpoints *eps;
};

struct cg_usb_device {
	struct usb_find_devices *found;
	libusb_device_handle *handle;
	pthread_mutex_t *mutex;
	struct libusb_device_descriptor *descriptor;
	uint16_t usbver;
	int speed;
	char *prod_string;
	char *manuf_string;
	char *serial_string;
	unsigned char fwVersion;	// ??
	unsigned char interfaceVersion;	// ??
};

struct cg_usb_info {
	uint8_t bus_number;
	uint8_t device_address;
	int usbstat;
	bool nodev;
	int nodev_count;
	struct timeval last_nodev;
};

enum usb_cmds {
	C_REJECTED = 0,
	C_PING,
	C_CLEAR,
	C_REQUESTVERSION,
	C_GETVERSION,
	C_REQUESTFPGACOUNT,
	C_GETFPGACOUNT,
	C_STARTPROGRAM,
	C_STARTPROGRAMSTATUS,
	C_PROGRAM,
	C_PROGRAMSTATUS,
	C_PROGRAMSTATUS2,
	C_FINALPROGRAMSTATUS,
	C_SETCLOCK,
	C_REPLYSETCLOCK,
	C_REQUESTUSERCODE,
	C_GETUSERCODE,
	C_REQUESTTEMPERATURE,
	C_GETTEMPERATURE,
	C_SENDWORK,
	C_SENDWORKSTATUS,
	C_REQUESTWORKSTATUS,
	C_GETWORKSTATUS,
	C_REQUESTIDENTIFY,
	C_GETIDENTIFY,
	C_REQUESTFLASH,
	C_REQUESTSENDWORK,
	C_REQUESTSENDWORKSTATUS,
	C_RESET,
	C_SETBAUD,
	C_SETDATA,
	C_SETFLOW,
	C_SETMODEM,
	C_PURGERX,
	C_PURGETX,
	C_FLASHREPLY,
	C_REQUESTDETAILS,
	C_GETDETAILS,
	C_REQUESTRESULTS,
	C_GETRESULTS,
	C_REQUESTQUEJOB,
	C_REQUESTQUEJOBSTATUS,
	C_QUEJOB,
	C_QUEJOBSTATUS,
	C_QUEFLUSH,
	C_QUEFLUSHREPLY,
	C_MAX
};

struct device_drv;
struct cgpu_info;

void usb_all(int level);
const char *usb_cmdname(enum usb_cmds cmd);
void usb_applog(struct cgpu_info *bflsc, enum usb_cmds cmd, char *msg, int amount, int err);
void usb_uninit(struct cgpu_info *cgpu);
bool usb_init(struct cgpu_info *cgpu, struct libusb_device *dev, struct usb_find_devices *found);
void usb_detect(struct device_drv *drv, bool (*device_detect)(struct libusb_device *, struct usb_find_devices *));
struct api_data *api_usb_stats(int *count);
void update_usb_stats(struct cgpu_info *cgpu);
int _usb_read(struct cgpu_info *cgpu, int ep, char *buf, size_t bufsiz, int *processed, unsigned int timeout, const char *end, enum usb_cmds cmd, bool ftdi);
int _usb_write(struct cgpu_info *cgpu, int ep, char *buf, size_t bufsiz, int *processed, unsigned int timeout, enum usb_cmds);
int _usb_transfer(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, unsigned int timeout, enum usb_cmds cmd);
void usb_cleanup();
void usb_initialise();

#define usb_read(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, false)

#define usb_read_nl(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, "\n", cmd, false)

#define usb_read_ep(cgpu, ep, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, ep, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, false)

#define usb_read_timeout(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, timeout, NULL, cmd, false)

#define usb_read_ep_timeout(cgpu, ep, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, ep, buf, bufsiz, read, timeout, NULL, cmd, false)

#define usb_write(cgpu, buf, bufsiz, wrote, cmd) \
	_usb_write(cgpu, DEFAULT_EP_OUT, buf, bufsiz, wrote, DEVTIMEOUT, cmd)

#define usb_write_ep(cgpu, ep, buf, bufsiz, wrote, cmd) \
	_usb_write(cgpu, ep, buf, bufsiz, wrote, DEVTIMEOUT, cmd)

#define usb_write_timeout(cgpu, buf, bufsiz, wrote, timeout, cmd) \
	_usb_write(cgpu, DEFAULT_EP_OUT, buf, bufsiz, wrote, timeout, cmd)

#define usb_write_ep_timeout(cgpu, ep, buf, bufsiz, wrote, timeout, cmd) \
	_usb_write(cgpu, ep, buf, bufsiz, wrote, timeout, cmd)

#define usb_ftdi_read_nl(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, "\n", cmd, true)

#define usb_ftdi_read_ok(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, "OK\n", cmd, true)

#define usb_transfer(cgpu, typ, req, val, idx, cmd) \
	_usb_transfer(cgpu, typ, req, val, idx, DEVTIMEOUT, cmd)

#endif
