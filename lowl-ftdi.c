/*
 * Copyright 2012-2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <libusb.h>

#include "compat.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-ftdi.h"
#include "miner.h"

#define FT232R_IDVENDOR   0x0403
#define FT232R_IDPRODUCT  0x6001
#define FT232H_IDPRODUCT  0x6014

#define FT232H_LATENCY_MS  2

static
void ft232r_devinfo_free(struct lowlevel_device_info * const info)
{
	libusb_device * const dev = info->lowl_data;
	if (dev)
		libusb_unref_device(dev);
}

static
bool _ft232r_devinfo_scan_cb(struct lowlevel_device_info * const usbinfo, void * const userp)
{
	struct lowlevel_device_info **devinfo_list_p = userp, *info;
	
	info = malloc(sizeof(*info));
	*info = (struct lowlevel_device_info){
		.lowl = &lowl_ft232r,
		.lowl_data = libusb_ref_device(usbinfo->lowl_data),
	};
	lowlevel_devinfo_semicpy(info, usbinfo);
	LL_PREPEND(*devinfo_list_p, info);
	
	// Never *consume* the lowl_usb entry - especially since this is during the scan!
	return false;
}

static
struct lowlevel_device_info *ft232r_devinfo_scan()
{
	struct lowlevel_device_info *devinfo_list = NULL;
	
	lowlevel_detect_id(_ft232r_devinfo_scan_cb, &devinfo_list, &lowl_usb, FT232R_IDVENDOR, FT232H_IDPRODUCT);
	lowlevel_detect_id(_ft232r_devinfo_scan_cb, &devinfo_list, &lowl_usb, FT232R_IDVENDOR, FT232R_IDPRODUCT);
	
	return devinfo_list;
}

#define FTDI_REQTYPE  (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE)
#define FTDI_REQTYPE_IN   (FTDI_REQTYPE | LIBUSB_ENDPOINT_IN)
#define FTDI_REQTYPE_OUT  (FTDI_REQTYPE | LIBUSB_ENDPOINT_OUT)

#define FTDI_REQUEST_RESET           0
#define FTDI_REQUEST_SET_BAUDRATE    3
#define FTDI_REQUEST_SET_EVENT_CHAR     0x06
#define FTDI_REQUEST_SET_ERROR_CHAR     0x07
#define FTDI_REQUEST_SET_LATENCY_TIMER  0x09
#define FTDI_REQUEST_SET_BITMODE  0x0b
#define FTDI_REQUEST_GET_PINS     0x0c
#define FTDI_REQUEST_GET_BITMODE  0x0c

#define FTDI_RESET_SIO  0

#define FTDI_BAUDRATE_3M  0,0

#define FTDI_BITMODE_MPSSE  0x02

#define FTDI_INDEX       1
#define FTDI_TIMEOUT  1000

// http://www.ftdichip.com/Support/Documents/AppNotes/AN_108_Command_Processor_for_MPSSE_and_MCU_Host_Bus_Emulation_Modes.pdf
#define FTDI_ADBUS_SET         0x80
#define FTDI_ACBUS_SET         0x82
#define FTDI_LOOPBACK_DISABLE  0x85
#define FTDI_TCK_DIVISOR       0x86
// Divide-by-five clock prescaler
#define FTDI_DIV5_ENABLE       0x8b

struct ft232r_device_handle {
	libusb_device_handle *h;
	uint8_t i;
	uint8_t o;
	int iPktSz;
	unsigned char ibuf[0x400];
	int ibufLen;
	uint16_t osz;
	unsigned char *obuf;
	uint16_t obufsz;
	bool mpsse;
};

static
struct ft232r_device_handle *ftdi_common_open(const struct lowlevel_device_info * const info)
{
	libusb_device * const dev = info->lowl_data;
	
	// FIXME: Cleanup on errors
	libusb_device_handle *devh;
	struct ft232r_device_handle *ftdi;

	if (libusb_open(dev, &devh)) {
		applog(LOG_ERR, "ft232r_open: Error opening device");
		return NULL;
	}
	libusb_reset_device(devh);
	libusb_detach_kernel_driver(devh, 0);
	if (libusb_set_configuration(devh, 1)) {
		applog(LOG_ERR, "ft232r_open: Error setting configuration");
		return NULL;
	}
	if (libusb_claim_interface(devh, 0)) {
		applog(LOG_ERR, "ft232r_open: Error claiming interface");
		return NULL;
	}

	struct libusb_config_descriptor *cfg;
	if (libusb_get_config_descriptor(dev, 0, &cfg)) {
		applog(LOG_ERR, "ft232r_open: Error getting config descriptor");
		return NULL;
	}
	const struct libusb_interface_descriptor *altcfg = &cfg->interface[0].altsetting[0];
	if (altcfg->bNumEndpoints < 2) {
		applog(LOG_ERR, "ft232r_open: Too few endpoints");
		return NULL;
	}
	ftdi = calloc(1, sizeof(*ftdi));
	ftdi->h = devh;
	ftdi->i = altcfg->endpoint[0].bEndpointAddress;
	ftdi->iPktSz = altcfg->endpoint[0].wMaxPacketSize;
	ftdi->o = altcfg->endpoint[1].bEndpointAddress;
	ftdi->osz = 0x1000;
	ftdi->obuf = malloc(ftdi->osz);
	libusb_free_config_descriptor(cfg);

	return ftdi;
}

struct ft232r_device_handle *ft232r_open(const struct lowlevel_device_info * const info)
{
	struct ft232r_device_handle * const ftdi = ftdi_common_open(info);
	if (!ftdi)
		return NULL;
	
	if (libusb_control_transfer(ftdi->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_BAUDRATE, FTDI_BAUDRATE_3M, NULL, 0, FTDI_TIMEOUT) < 0) {
		applog(LOG_ERR, "ft232r_open: Error performing control transfer");
		ft232r_close(ftdi);
		return NULL;
	}
	
	return ftdi;
}

static
void ft232h_mpsse_clock_divisor(uint8_t * const buf, const unsigned long clock, const unsigned long freq)
{
	const uint16_t divisor = (clock / freq / 2) - 1;
	buf[0] = divisor & 0xff;
	buf[1] = divisor >> 8;
}

static ssize_t ft232r_readwrite(struct ft232r_device_handle *, unsigned char, void *, size_t);

struct ft232r_device_handle *ft232h_open_mpsse(const struct lowlevel_device_info * const info)
{
	if (info->pid != FT232H_IDPRODUCT)
		return NULL;
	
	struct ft232r_device_handle * const ftdi = ftdi_common_open(info);
	uint8_t buf[3];
	if (!ftdi)
		return NULL;
	
	if (libusb_control_transfer(ftdi->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_RESET, FTDI_RESET_SIO, 1, NULL, 0, FTDI_TIMEOUT) < 0)
	{
		applog(LOG_ERR, "%s: Error requesting %s", __func__, "SIO reset");
		goto err;
	}
	
	if (libusb_control_transfer(ftdi->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_LATENCY_TIMER, FT232H_LATENCY_MS, 1, NULL, 0, FTDI_TIMEOUT) < 0)
	{
		applog(LOG_ERR, "%s: Error setting %s", __func__, "latency timer");
		goto err;
	}
	
	if (libusb_control_transfer(ftdi->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_EVENT_CHAR, 0, 1, NULL, 0, FTDI_TIMEOUT) < 0)
	{
		applog(LOG_ERR, "%s: Error setting %s", __func__, "event char");
		goto err;
	}
	
	if (libusb_control_transfer(ftdi->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_ERROR_CHAR, 0, 1, NULL, 0, FTDI_TIMEOUT) < 0)
	{
		applog(LOG_ERR, "%s: Error setting %s", __func__, "error char");
		goto err;
	}
	
	if (!ft232r_set_bitmode(ftdi, 0, FTDI_BITMODE_MPSSE))
	{
		applog(LOG_ERR, "%s: Error setting %s", __func__, "MPSSE bitmode");
		goto err;
	}
	
	buf[0] = FTDI_DIV5_ENABLE;
	if (ft232r_readwrite(ftdi, ftdi->o, buf, 1) != 1)
	{
		applog(LOG_ERR, "%s: Error requesting %s", __func__, "divide-by-five clock prescaler");
		goto err;
	}
	
	buf[0] = FTDI_TCK_DIVISOR;
	ft232h_mpsse_clock_divisor(&buf[1], 12000000, 200000);
	if (ft232r_readwrite(ftdi, ftdi->o, buf, 3) != 3)
	{
		applog(LOG_ERR, "%s: Error setting %s", __func__, "MPSSE clock divisor");
		goto err;
	}
	
	buf[0] = FTDI_LOOPBACK_DISABLE;
	if (ft232r_readwrite(ftdi, ftdi->o, buf, 1) != 1)
		applog(LOG_WARNING, "%s: Error disabling loopback", __func__);
	
	ftdi->mpsse = true;
	
	return ftdi;

err:
	ft232r_close(ftdi);
	return NULL;
}

void ft232r_close(struct ft232r_device_handle *dev)
{
	libusb_release_interface(dev->h, 0);
	libusb_reset_device(dev->h);
	libusb_close(dev->h);
	free(dev);
}

bool ft232r_purge_buffers(struct ft232r_device_handle *dev, enum ft232r_reset_purge purge)
{
	if (ft232r_flush(dev) < 0)
		return false;

	if (purge & FTDI_PURGE_RX) {
		if (libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_RESET, FTDI_PURGE_RX, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT))
			return false;
		dev->ibufLen = 0;
	}
	if (purge & FTDI_PURGE_TX)
		if (libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_RESET, FTDI_PURGE_TX, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT))
			return false;
	return true;
}

bool ft232r_set_bitmode(struct ft232r_device_handle *dev, uint8_t mask, uint8_t mode)
{
	if (ft232r_flush(dev) < 0)
		return false;
	if (libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_BITMODE, mask, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT))
		return false;
	return !libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_BITMODE, (mode << 8) | mask, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT);
}

static ssize_t ft232r_readwrite(struct ft232r_device_handle *dev, unsigned char endpoint, void *data, size_t count)
{
	int transferred;
	switch (libusb_bulk_transfer(dev->h, endpoint, data, count, &transferred, FTDI_TIMEOUT)) {
		case LIBUSB_ERROR_TIMEOUT:
			if (!transferred) {
				errno = ETIMEDOUT;
				return -1;
			}
			// fallthru
		case 0:
			if (opt_dev_protocol)
			{
				char x[(transferred * 2) + 1];
				bin2hex(x, data, transferred);
				applog(LOG_DEBUG, "ft232r %p: %s: %s",
				       dev,
				       (endpoint & LIBUSB_ENDPOINT_IN) ? "RECV" : "SEND",
				       x);
			}
			
			return transferred;
		default:
			errno = EIO;
			return -1;
	}
}

ssize_t ft232r_flush(struct ft232r_device_handle *dev)
{
	if (!dev->obufsz)
		return 0;
	ssize_t r = ft232r_readwrite(dev, dev->o, dev->obuf, dev->obufsz);
	if (r == dev->obufsz) {
		dev->obufsz = 0;
	} else if (r > 0) {
		dev->obufsz -= r;
		memmove(dev->obuf, &dev->obuf[r], dev->obufsz);
	}
	return r;
}

ssize_t ft232r_write(struct ft232r_device_handle * const dev, const void * const data, const size_t count)
{
	uint16_t bufleft;
	ssize_t r;
	
	bufleft = dev->osz - dev->obufsz;
	
	if (count < bufleft) {
		// Just add to output buffer
		memcpy(&dev->obuf[dev->obufsz], data, count);
		dev->obufsz += count;
		return count;
	}
	
	// Fill up buffer and flush
	memcpy(&dev->obuf[dev->obufsz], data, bufleft);
	dev->obufsz += bufleft;
	r = ft232r_flush(dev);
	
	if (unlikely(r <= 0)) {
		// In this case, no bytes were written supposedly, so remove this data from buffer
		dev->obufsz -= bufleft;
		return r;
	}
	
	// Even if not all <bufleft> bytes from this write got out, the remaining are still buffered
	return bufleft;
}

typedef ssize_t (*ft232r_rwfunc_t)(struct ft232r_device_handle *, void*, size_t);

static
ssize_t ft232r_rw_all(const void * const rwfunc_p, struct ft232r_device_handle * const dev, void * const data, size_t count)
{
	ft232r_rwfunc_t rwfunc = rwfunc_p;
	char *p = data;
	ssize_t writ = 0, total = 0;

	while (count && (writ = rwfunc(dev, p, count)) > 0) {
		p += writ;
		count -= writ;
		total += writ;
	}
	return total ?: writ;
}

ssize_t ft232r_write_all(struct ft232r_device_handle * const dev, const void * const data_p, size_t count)
{
	const uint8_t *data = data_p;
	if (dev->mpsse)
	{
		ssize_t e;
		while (count > 0x10000)
		{
			e = ft232r_write_all(dev, data, 0x10000);
			if (e != 0x10000)
				return e;
			data += 0x10000;
			count -= 0x10000;
		}
		
		const uint16_t ftdilen = count - 1;
		const uint8_t cmd[] = { 0x11, ftdilen & 0xff, ftdilen >> 8 };
		e = ft232r_rw_all(ft232r_write, dev, (void*)cmd, 3);
		if (e != 3)
			return e;
	}
	return ft232r_rw_all(ft232r_write, dev, (void*)data, count) + (data - (uint8_t*)data_p);
}

ssize_t ft232r_read(struct ft232r_device_handle *dev, void *data, size_t count)
{
	ssize_t r;
	int adj;
	
	// Flush any pending output before reading
	r = ft232r_flush(dev);
	if (r < 0)
		return r;
	
	// First 2 bytes of every packet are FTDI status or something
	while (dev->ibufLen <= 2) {
		// TODO: Implement a timeout for status byte repeating
		int transferred = ft232r_readwrite(dev, dev->i, dev->ibuf, sizeof(dev->ibuf));
		if (transferred <= 0)
			return transferred;
		dev->ibufLen = transferred;
		for (adj = dev->iPktSz; dev->ibufLen > adj; adj += dev->iPktSz - 2) {
			dev->ibufLen -= 2;
			memmove(&dev->ibuf[adj], &dev->ibuf[adj+2], dev->ibufLen - adj);
		}
	}
	unsigned char *ibufs = &dev->ibuf[2];
	size_t ibufsLen = dev->ibufLen - 2;
	
	if (count > ibufsLen)
		count = ibufsLen;
	memcpy(data, ibufs, count);
	dev->ibufLen -= count;
	ibufsLen -= count;
	if (ibufsLen) {
		memmove(ibufs, &ibufs[count], ibufsLen);
		applog(LOG_DEBUG, "ft232r_read: %"PRIu64" bytes extra", (uint64_t)ibufsLen);
	}
	return count;
}

ssize_t ft232r_read_all(struct ft232r_device_handle *dev, void *data, size_t count)
{
	return ft232r_rw_all(ft232r_read, dev, data, count);
}

bool ft232r_get_pins(struct ft232r_device_handle *dev, uint8_t *pins)
{
	return libusb_control_transfer(dev->h, FTDI_REQTYPE_IN, FTDI_REQUEST_GET_PINS, 0, FTDI_INDEX, pins, 1, FTDI_TIMEOUT) == 1;
}

bool ft232r_get_bitmode(struct ft232r_device_handle *dev, uint8_t *out_mode)
{
	return libusb_control_transfer(dev->h, FTDI_REQTYPE_IN, FTDI_REQUEST_GET_BITMODE, 0, FTDI_INDEX, out_mode, 1, FTDI_TIMEOUT) == 1;
}

bool ft232r_set_cbus_bits(struct ft232r_device_handle *dev, bool sc, bool cs)
{
	uint8_t pin_state = (cs ? (1<<2) : 0)
	                  | (sc ? (1<<3) : 0);
	return ft232r_set_bitmode(dev, 0xc0 | pin_state, 0x20);
}

bool ft232r_get_cbus_bits(struct ft232r_device_handle *dev, bool *out_sio0, bool *out_sio1)
{
	uint8_t data;
	if (!ft232r_get_bitmode(dev, &data))
		return false;
	*out_sio0 = data & 1;
	*out_sio1 = data & 2;
	return true;
}

bool ft232h_mpsse_set_axbus(struct ft232r_device_handle * const ftdi, const uint8_t value, const uint8_t directions, const bool adbus)
{
	if (ft232r_flush(ftdi))
		cgsleep_ms(1);
	const uint8_t buf[] = { adbus ? FTDI_ADBUS_SET : FTDI_ACBUS_SET, value, directions };
	return (ft232r_write(ftdi, buf, 3) == 3) && (ft232r_flush(ftdi) == 3);
}

ssize_t ft232h_mpsse_readwrite_all(struct ft232r_device_handle * const dev, void * const read_data_p, const void * const write_data_p, size_t count)
{
	uint8_t *read_data = read_data_p;
	const uint8_t *write_data = write_data_p;
	
	while (count > 0x10000)
	{
		ft232h_mpsse_readwrite_all(dev, read_data, write_data, 0x10000);
		read_data += 0x10000;
		write_data += 0x10000;
		count -= 0x10000;
	}
	
	const uint16_t ftdilen = count - 1;
	const uint8_t cmd[] = { 0x31, ftdilen & 0xff, ftdilen >> 8 };
	ssize_t e;
	
	e = ft232r_rw_all(ft232r_write, dev, (void*)cmd, 3);
	if (e != 3)
		return e;
	
	e = ft232r_rw_all(ft232r_write, dev, (void*)write_data, count);
	if (e != count)
		return e;
	
	return ft232r_read_all(dev, read_data, count) + (read_data - (uint8_t*)read_data_p);
}

struct lowlevel_driver lowl_ft232r = {
	.dname = "ft232r",
	.devinfo_scan = ft232r_devinfo_scan,
	.devinfo_free = ft232r_devinfo_free,
};

#if 0
int main() {
	libusb_init(NULL);
	ft232r_scan();
	ft232r_scan_free();
	libusb_exit(NULL);
}
void applog(int prio, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	puts("");
	va_end(ap);
}
#endif
