/*
 * Copyright 2012-2013 Andrew Smith
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef USBUTILS_H
#define USBUTILS_H

#include <libusb.h>

#define EPI(x) (LIBUSB_ENDPOINT_IN | (unsigned char)(x))
#define EPO(x) (LIBUSB_ENDPOINT_OUT | (unsigned char)(x))


// For 0x0403:0x6014/0x6001 FT232H (and possibly others?) - BFL, BAS, BLT, LLT, AVA
#define FTDI_TYPE_OUT (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT)
#define FTDI_TYPE_IN (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN)

#define FTDI_REQUEST_RESET ((uint8_t)0)
#define FTDI_REQUEST_MODEM ((uint8_t)1)
#define FTDI_REQUEST_FLOW ((uint8_t)2)
#define FTDI_REQUEST_BAUD ((uint8_t)3)
#define FTDI_REQUEST_DATA ((uint8_t)4)
#define FTDI_REQUEST_LATENCY ((uint8_t)9)

#define FTDI_VALUE_RESET 0
#define FTDI_VALUE_PURGE_RX 1
#define FTDI_VALUE_PURGE_TX 2
#define FTDI_VALUE_LATENCY 1

// Baud
#define FTDI_VALUE_BAUD_BFL 0xc068
#define FTDI_INDEX_BAUD_BFL 0x0200
#define FTDI_VALUE_BAUD_BAS FTDI_VALUE_BAUD_BFL
#define FTDI_INDEX_BAUD_BAS FTDI_INDEX_BAUD_BFL
// LLT = BLT (same code)
#define FTDI_VALUE_BAUD_BLT 0x001a
#define FTDI_INDEX_BAUD_BLT 0x0000

// Avalon
#define FTDI_VALUE_BAUD_AVA 0x001A
#define FTDI_INDEX_BAUD_AVA 0x0000

#define FTDI_VALUE_DATA_AVA 8

// CMR = 115200 & 57600
#define FTDI_VALUE_BAUD_CMR_115 0xc068
#define FTDI_INDEX_BAUD_CMR_115 0x0200

#define FTDI_VALUE_BAUD_CMR_57 0x80d0
#define FTDI_INDEX_BAUD_CMR_57 0x0200

// Data control
#define FTDI_VALUE_DATA_BFL 0
#define FTDI_VALUE_DATA_BAS FTDI_VALUE_DATA_BFL
// LLT = BLT (same code)
#define FTDI_VALUE_DATA_BLT 8

#define FTDI_VALUE_FLOW 0
#define FTDI_VALUE_MODEM 0x0303


// For 0x10c4:0xea60 USB cp210x chip - AMU
#define CP210X_TYPE_OUT 0x41

#define CP210X_REQUEST_IFC_ENABLE 0x00
#define CP210X_REQUEST_DATA 0x07
#define CP210X_REQUEST_BAUD 0x1e

#define CP210X_VALUE_UART_ENABLE 0x0001
#define CP210X_VALUE_DATA 0x0303
#define CP210X_DATA_BAUD 0x0001c200


// For 0x067b:0x2303 Prolific PL2303 - ICA
#define PL2303_CTRL_DTR 0x01
#define PL2303_CTRL_RTS 0x02

#define PL2303_CTRL_OUT 0x21
#define PL2303_VENDOR_OUT 0x40

#define PL2303_REQUEST_CTRL 0x22
#define PL2303_REQUEST_LINE 0x20
#define PL2303_REQUEST_VENDOR 0x01

#define PL2303_REPLY_CTRL 0x21

#define PL2303_VALUE_CTRL (PL2303_CTRL_DTR | PL2303_CTRL_RTS)
#define PL2303_VALUE_LINE 0
#define PL2303_VALUE_LINE0 0x0001c200
#define PL2303_VALUE_LINE1 0x080000
#define PL2303_VALUE_LINE_SIZE 7
#define PL2303_VALUE_VENDOR 0

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

enum sub_ident {
	IDENT_UNK = 0,
	IDENT_BAJ,
	IDENT_BAL,
	IDENT_BAS,
	IDENT_BAM,
	IDENT_BFL,
	IDENT_MMQ,
	IDENT_AVA,
	IDENT_ICA,
	IDENT_AMU,
	IDENT_BLT,
	IDENT_LLT,
	IDENT_CMR1,
	IDENT_CMR2,
	IDENT_ZTX
};

struct usb_find_devices {
	int drv;
	const char *name;
	enum sub_ident ident;
	uint16_t idVendor;
	uint16_t idProduct;
	char *iManufacturer;
	char *iProduct;
	int kernel;
	int config;
	int interface;
	unsigned int timeout;
	uint16_t wMaxPacketSize;
	uint16_t latency;
	int epcount;
	struct usb_endpoints *eps;
};

/* Latency is set to 32ms to prevent a transfer ever being more than 512 bytes
 * +2 bytes of status such as the ftdi chip, when the chips emulate a 115200
 * baud rate, to avoid status bytes being interleaved in larger transfers. */
#define LATENCY_UNUSED 0
#define LATENCY_STD 32

enum usb_types {
	USB_TYPE_STD = 0,
	USB_TYPE_FTDI
};

struct cg_usb_device {
	struct usb_find_devices *found;
	libusb_device_handle *handle;
	pthread_mutex_t *mutex;
	struct libusb_device_descriptor *descriptor;
	enum usb_types usb_type;
	enum sub_ident ident;
	uint16_t usbver;
	int cps;
	char *prod_string;
	char *manuf_string;
	char *serial_string;
	unsigned char fwVersion;	// ??
	unsigned char interfaceVersion;	// ??
	char *buffer;
	uint32_t bufsiz;
	uint32_t bufamt;
	uint16_t PrefPacketSize;
};

#define USB_NOSTAT 0

struct cg_usb_info {
	uint8_t bus_number;
	uint8_t device_address;
	int usbstat;
	bool nodev;
	int nodev_count;
	struct timeval last_nodev;
	uint32_t ioerr_count;
	uint32_t continuous_ioerr_count;

	/*
	 * for nodev and cgusb access (read and write)
	 * it's a pointer so MMQ can have it in multiple devices
	 *
	 * N.B. general mining code doesn't need to use the read
	 * lock for 'nodev' if it calls a usb_read/write/etc function
	 * that uses the lock - however, all usbutils code MUST use it
	 * to avoid devices disappearing while in use by multiple threads
	 */
	pthread_rwlock_t *devlock;

	time_t last_pipe;
	uint64_t pipe_count;
	uint64_t clear_err_count;
	uint64_t retry_err_count;
	uint64_t clear_fail_count;
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
	C_REQUESTVOLTS,
	C_SENDTESTWORK,
	C_LATENCY,
	C_SETLINE,
	C_VENDOR,
	C_SETFAN,
	C_FANREPLY,
	C_AVALON_TASK,
	C_AVALON_READ,
	C_GET_AVALON_READY,
	C_AVALON_RESET,
	C_GET_AVALON_RESET,
	C_FTDI_STATUS,
	C_ENABLE_UART,
	C_MAX
};

struct device_drv;
struct cgpu_info;

void usb_all(int level);
const char *usb_cmdname(enum usb_cmds cmd);
void usb_applog(struct cgpu_info *bflsc, enum usb_cmds cmd, char *msg, int amount, int err);
struct cgpu_info *usb_copy_cgpu(struct cgpu_info *orig);
struct cgpu_info *usb_alloc_cgpu(struct device_drv *drv, int threads);
struct cgpu_info *usb_free_cgpu_devlock(struct cgpu_info *cgpu, bool free_devlock);
#define usb_free_cgpu(cgpu) usb_free_cgpu_devlock(cgpu, true)
void usb_uninit(struct cgpu_info *cgpu);
bool usb_init(struct cgpu_info *cgpu, struct libusb_device *dev, struct usb_find_devices *found);
void usb_detect(struct device_drv *drv, bool (*device_detect)(struct libusb_device *, struct usb_find_devices *));
struct api_data *api_usb_stats(int *count);
void update_usb_stats(struct cgpu_info *cgpu);
int _usb_read(struct cgpu_info *cgpu, int ep, char *buf, size_t bufsiz, int *processed, unsigned int timeout, const char *end, enum usb_cmds cmd, bool readonce);
int _usb_write(struct cgpu_info *cgpu, int ep, char *buf, size_t bufsiz, int *processed, unsigned int timeout, enum usb_cmds);
int _usb_transfer(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint32_t *data, int siz, unsigned int timeout, enum usb_cmds cmd);
int _usb_transfer_read(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, char *buf, int bufsiz, int *amount, unsigned int timeout, enum usb_cmds cmd);
int usb_ftdi_cts(struct cgpu_info *cgpu);
int usb_ftdi_set_latency(struct cgpu_info *cgpu);
void usb_buffer_enable(struct cgpu_info *cgpu);
void usb_buffer_disable(struct cgpu_info *cgpu);
void usb_buffer_clear(struct cgpu_info *cgpu);
uint32_t usb_buffer_size(struct cgpu_info *cgpu);
void usb_set_dev_start(struct cgpu_info *cgpu);
void usb_cleanup();
void usb_initialise();
void *usb_resource_thread(void *userdata);

#define usb_read(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, false)

#define usb_read_once(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, NULL, cmd, true)

#define usb_read_once_timeout(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, timeout, NULL, cmd, true)

#define usb_read_nl(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, "\n", cmd, false)

#define usb_read_nl_timeout(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, timeout, "\n", cmd, false)

#define usb_read_ok(cgpu, buf, bufsiz, read, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, DEVTIMEOUT, "OK\n", cmd, false)

#define usb_read_ok_timeout(cgpu, buf, bufsiz, read, timeout, cmd) \
	_usb_read(cgpu, DEFAULT_EP_IN, buf, bufsiz, read, timeout, "OK\n", cmd, false)

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

#define usb_transfer(cgpu, typ, req, val, idx, cmd) \
	_usb_transfer(cgpu, typ, req, val, idx, NULL, 0, DEVTIMEOUT, cmd)

#define usb_transfer_data(cgpu, typ, req, val, idx, data, len, cmd) \
	_usb_transfer(cgpu, typ, req, val, idx, data, len, DEVTIMEOUT, cmd)

#define usb_transfer_read(cgpu, typ, req, val, idx, buf, bufsiz, read, cmd) \
	_usb_transfer_read(cgpu, typ, req, val, idx, buf, bufsiz, read, DEVTIMEOUT, cmd)

#endif
