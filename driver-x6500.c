/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <libusb-1.0/libusb.h>

#include "dynclock.h"
#include "jtag.h"
#include "logging.h"
#include "miner.h"
#include "fpgautils.h"
#include "ft232r.h"

#define X6500_USB_PRODUCT "X6500 FPGA Miner"
#define X6500_BITSTREAM_FILENAME "fpgaminer_top_fixed7_197MHz.bit"
// NOTE: X6500_BITSTREAM_USERID is bitflipped
#define X6500_BITSTREAM_USERID "\x40\x20\x24\x42"
#define X6500_MINIMUM_CLOCK    2
#define X6500_DEFAULT_CLOCK  200
#define X6500_MAXIMUM_CLOCK  210

struct device_api x6500_api;

#define fromlebytes(ca, j)  (ca[j] | (((uint16_t)ca[j+1])<<8) | (((uint32_t)ca[j+2])<<16) | (((uint32_t)ca[j+3])<<24))

static
void int2bits(uint32_t n, uint8_t *b, uint8_t bits)
{
	uint8_t i;
	for (i = (bits + 7) / 8; i > 0; )
		b[--i] = 0;
	for (i = 0; i < bits; ++i) {
		if (n & 1)
			b[i/8] |= 0x80 >> (i % 8);
		n >>= 1;
	}
}

static
uint32_t bits2int(uint8_t *b, uint8_t bits)
{
	uint32_t n, i;
	n = 0;
	for (i = 0; i < bits; ++i)
		if (b[i/8] & (0x80 >> (i % 8)))
			n |= 1<<i;
	return n;
}

static
void bitendianflip(void *n, size_t bits)
{
	size_t i;
	uint8_t *b = n;
	// NOTE: this doesn't work with non-byte boundaries
	bits /= 8;
	for (i = 0; i < bits; ++i)
		b[i] = ((b[i] &    1) ? 0x80 : 0)
		     | ((b[i] &    2) ? 0x40 : 0)
		     | ((b[i] &    4) ? 0x20 : 0)
		     | ((b[i] &    8) ? 0x10 : 0)
		     | ((b[i] & 0x10) ?    8 : 0)
		     | ((b[i] & 0x20) ?    4 : 0)
		     | ((b[i] & 0x40) ?    2 : 0)
		     | ((b[i] & 0x80) ?    1 : 0);
}

static
void checksum(uint8_t *b, uint8_t bits)
{
	uint8_t i;
	uint8_t checksum = 1;
	for(i = 0; i < bits; ++i)
		checksum ^= (b[i/8] & (0x80 >> (i % 8))) ? 1 : 0;
	if (checksum)
		b[i/8] |= 0x80 >> (i % 8);
}

static
void x6500_set_register(struct jtag_port *jp, uint8_t addr, uint32_t nv)
{
	uint8_t buf[38];
	jtag_write(jp, JTAG_REG_IR, "\x40", 6);
	int2bits(nv, &buf[0], 32);
	int2bits(addr, &buf[4], 4);
	buf[4] |= 8;
	checksum(buf, 37);
	jtag_write(jp, JTAG_REG_DR, buf, 38);
	jtag_run(jp);
}

static
uint32_t x6500_get_register(struct jtag_port *jp, uint8_t addr)
{
	uint8_t buf[4];
	jtag_write(jp, JTAG_REG_IR, "\x40", 6);
	int2bits(addr, &buf[0], 4);
	checksum(buf, 5);
	jtag_write(jp, JTAG_REG_DR, buf, 6);
	jtag_read (jp, JTAG_REG_DR, buf, 32);
	jtag_reset(jp);
	bitendianflip(buf, 32);
	return bits2int(buf, 32);
}

static bool x6500_foundusb(libusb_device *dev, const char *product, const char *serial)
{
	struct cgpu_info *x6500;
	x6500 = calloc(1, sizeof(*x6500));
	x6500->api = &x6500_api;
	mutex_init(&x6500->device_mutex);
	x6500->device_path = strdup(serial);
	x6500->deven = DEV_ENABLED;
	x6500->threads = 2;
	x6500->name = strdup(product);
	x6500->cutofftemp = 85;
	x6500->cgpu_data = dev;

	return add_cgpu(x6500);
}

static bool x6500_detect_one(const char *serial)
{
	return ft232r_detect(X6500_USB_PRODUCT, serial, x6500_foundusb);
}

static int x6500_detect_auto()
{
	return ft232r_detect(X6500_USB_PRODUCT, NULL, x6500_foundusb);
}

static void x6500_detect()
{
	serial_detect_auto(&x6500_api, x6500_detect_one, x6500_detect_auto);
}

static bool x6500_prepare(struct thr_info *thr)
{
	if (thr->device_thread)
		return true;
	
	struct cgpu_info *x6500 = thr->cgpu;
	mutex_init(&x6500->device_mutex);
	struct ft232r_device_handle *ftdi = ft232r_open(x6500->cgpu_data);
	x6500->device_ft232r = NULL;
	if (!ftdi)
		return false;
	if (!ft232r_set_bitmode(ftdi, 0xee, 4))
		return false;
	if (!ft232r_purge_buffers(ftdi, FTDI_PURGE_BOTH))
		return false;
	x6500->device_ft232r = ftdi;
	
	struct jtag_port_a *jtag_a;
	jtag_a = calloc(1, sizeof(*jtag_a));
	jtag_a->ftdi = ftdi;
	x6500->cgpu_data = jtag_a;
	
	return true;
}

struct x6500_fpga_data {
	struct jtag_port jtag;
};

#define bailout2(...) do {  \
	applog(__VA_ARGS__);  \
	return false;  \
} while(0)

static bool
x6500_fpga_upload_bitstream(struct cgpu_info *x6500, struct jtag_port *jp1)
{
	char buf[0x100];
	unsigned long len, flen;
	char *pdone = (char*)&x6500->cgpu_data;
	struct ft232r_device_handle *ftdi = jp1->a->ftdi;

	FILE *f = open_xilinx_bitstream(x6500, X6500_BITSTREAM_FILENAME, &len);
	if (!f)
		return false;

	flen = len;

	applog(LOG_WARNING, "%s %u: Programming %s...",
	       x6500->api->name, x6500->device_id, x6500->device_path);
	
	// "Magic" jtag_port configured to access both FPGAs concurrently
	struct jtag_port jpt = {
		.a = jp1->a,
		.tck = 0x88,
		.tms = 0x44,
		.tdi = 0x22,
		.tdo = 0x11,
		.ignored = 0,
	};
	struct jtag_port *jp = &jpt;
	uint8_t i;
	
	// Need to reset here despite previous FPGA state, since we are programming all at once
	jtag_reset(jp);
	
	jtag_write(jp, JTAG_REG_IR, "\xd0", 6);  // JPROGRAM
	do {
		i = 0xd0;  // Re-set JPROGRAM while reading status
		jtag_read(jp, JTAG_REG_IR, &i, 6);
	} while (i & 8);
	jtag_write(jp, JTAG_REG_IR, "\xa0", 6);  // CFG_IN
	
	sleep(1);
	
	if (fread(buf, 32, 1, f) != 1)
		bailout2(LOG_ERR, "%s %u: File underrun programming %s (%d bytes left)", x6500->api->name, x6500->device_id, x6500->device_path, len);
	jtag_swrite(jp, JTAG_REG_DR, buf, 256);
	len -= 32;
	
	// Put ft232r chip in asynchronous bitbang mode so we don't need to read back tdo
	// This takes upload time down from about an hour to about 3 minutes
	if (!ft232r_set_bitmode(ftdi, 0xee, 1))
		return false;
	if (!ft232r_purge_buffers(ftdi, FTDI_PURGE_BOTH))
		return false;
	jp->a->async = true;

	ssize_t buflen;
	char nextstatus = 10;
	while (len) {
		buflen = len < 32 ? len : 32;
		if (fread(buf, buflen, 1, f) != 1)
			bailout2(LOG_ERR, "%s %u: File underrun programming %s (%d bytes left)", x6500->api->name, x6500->device_id, x6500->device_path, len);
		jtag_swrite_more(jp, buf, buflen * 8, len == (unsigned long)buflen);
		*pdone = 100 - ((len * 100) / flen);
		if (*pdone >= nextstatus)
		{
			nextstatus += 10;
			applog(LOG_WARNING, "%s %u: Programming %s... %d%% complete...", x6500->api->name, x6500->device_id, x6500->device_path, *pdone);
		}
		len -= buflen;
	}
	
	// Switch back to synchronous bitbang mode
	if (!ft232r_set_bitmode(ftdi, 0xee, 4))
		return false;
	if (!ft232r_purge_buffers(ftdi, FTDI_PURGE_BOTH))
		return false;
	jp->a->async = false;
	jp->a->bufread = 0;

	jtag_write(jp, JTAG_REG_IR, "\x30", 6);  // JSTART
	for (i=0; i<16; ++i)
		jtag_run(jp);
	i = 0xff;  // BYPASS
	jtag_read(jp, JTAG_REG_IR, &i, 6);
	if (!(i & 4))
		return false;
	
	applog(LOG_WARNING, "%s %u: Done programming %s", x6500->api->name, x6500->device_id, x6500->device_path);

	return true;
}

static bool x6500_fpga_init(struct thr_info *thr)
{
	struct cgpu_info *x6500 = thr->cgpu;
	struct ft232r_device_handle *ftdi = x6500->device_ft232r;
	struct x6500_fpga_data *fpga;
	struct jtag_port *jp;
	int fpgaid = thr->device_thread;
	uint8_t pinoffset = fpgaid ? 0x10 : 1;
	unsigned char buf[4];
	int i;
	
	if (!ftdi)
		return false;
	
	fpga = calloc(1, sizeof(*fpga));
	jp = &fpga->jtag;
	jp->a = x6500->cgpu_data;
	jp->tck = pinoffset << 3;
	jp->tms = pinoffset << 2;
	jp->tdi = pinoffset << 1;
	jp->tdo = pinoffset << 0;
	jp->ignored = ~(fpga->jtag.tdo | fpga->jtag.tdi | fpga->jtag.tms | fpga->jtag.tck);
	
	mutex_lock(&x6500->device_mutex);
	if (!jtag_reset(jp)) {
		mutex_unlock(&x6500->device_mutex);
		applog(LOG_ERR, "%s %u: JTAG reset failed",
		       x6500->api->name, x6500->device_id);
		return false;
	}
	
	i = jtag_detect(jp);
	if (i != 1) {
		mutex_unlock(&x6500->device_mutex);
		applog(LOG_ERR, "%s %u: JTAG detect returned %d",
		       x6500->api->name, x6500->device_id, i);
		return false;
	}
	
	if (!(1
	 && jtag_write(jp, JTAG_REG_IR, "\x10", 6)
	 && jtag_read (jp, JTAG_REG_DR, buf, 32)
	 && jtag_reset(jp)
	)) {
		mutex_unlock(&x6500->device_mutex);
		applog(LOG_ERR, "%s %u: JTAG error reading user code",
		       x6500->api->name, x6500->device_id);
		return false;
	}
	
	if (memcmp(buf, X6500_BITSTREAM_USERID, 4)) {
		applog(LOG_ERR, "%s %u.%u: FPGA not programmed",
		       x6500->api->name, x6500->device_id, fpgaid);
		if (!x6500_fpga_upload_bitstream(x6500, jp))
			return false;
	} else
		applog(LOG_DEBUG, "%s %u.%u: FPGA is already programmed :)",
		       x6500->api->name, x6500->device_id, fpgaid);
	
	thr->cgpu_data = fpga;

	x6500_set_register(jp, 0xD, 200);  // Set clock speed

	mutex_unlock(&x6500->device_mutex);
	return true;
}

static void
get_x6500_statline_before(char *buf, struct cgpu_info *x6500)
{
	char info[18] = "               | ";

	char pdone = (char)(x6500->cgpu_data);
	if (pdone != 101) {
		sprintf(&info[1], "%3d%%", pdone);
		info[5] = ' ';
		strcat(buf, info);
		return;
	}
	strcat(buf, "               | ");
}

static
bool x6500_start_work(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *x6500 = thr->cgpu;
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	char fpgaid = thr->device_thread;

	mutex_lock(&x6500->device_mutex);

	for (int i = 1, j = 0; i < 9; ++i, j += 4)
		x6500_set_register(&fpga->jtag, i, fromlebytes(work->midstate, j));

	for (int i = 9, j = 64; i < 12; ++i, j += 4)
		x6500_set_register(&fpga->jtag, i, fromlebytes(work->data, j));

	//gettimeofday(&fpga->tv_workstart, NULL);
	mutex_unlock(&x6500->device_mutex);

	if (opt_debug) {
		char *xdata = bin2hex(work->data, 80);
		applog(LOG_DEBUG, "%s %u.%u: Started work: %s",
		       x6500->api->name, x6500->device_id, fpgaid, xdata);
		free(xdata);
	}

	return true;
}

static
int64_t x6500_process_results(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *x6500 = thr->cgpu;
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	struct jtag_port *jtag = &fpga->jtag;
	char fpgaid = thr->device_thread;

	uint32_t nonce;
	long iter;
	bool bad;

	iter = 200;
	while (1) {
		mutex_lock(&x6500->device_mutex);
		nonce = x6500_get_register(jtag, 0xE);
		mutex_unlock(&x6500->device_mutex);
		if (nonce != 0xffffffff) {
			bad = !test_nonce(work, nonce, false);
			if (!bad) {
				submit_nonce(thr, work, nonce);
				applog(LOG_DEBUG, "%s %u.%u: Nonce for current  work: %08lx",
				       x6500->api->name, x6500->device_id, fpgaid,
				       (unsigned long)nonce);
			} else {
				applog(LOG_DEBUG, "%s %u.%u: Nonce with H not zero  : %08lx",
				       x6500->api->name, x6500->device_id, fpgaid,
				       (unsigned long)nonce);
				++hw_errors;
				++x6500->hw_errors;
			}
		}
		if (thr->work_restart || !--iter)
			break;
		usleep(1000);
		if (thr->work_restart)
			break;
	}

	return 10000000;
}

static int64_t
x6500_scanhash(struct thr_info *thr, struct work *work, int64_t __maybe_unused max_nonce)
{
	if (!x6500_start_work(thr, work))
		return -1;

	int64_t hashes = x6500_process_results(thr, work);
	if (hashes > 0)
		work->blk.nonce += hashes;
	return hashes;
}

struct device_api x6500_api = {
	.dname = "x6500",
	.name = "XBS",
	.api_detect = x6500_detect,
	.thread_prepare = x6500_prepare,
	.thread_init = x6500_fpga_init,
	.get_statline_before = get_x6500_statline_before,
	.scanhash = x6500_scanhash,
// 	.thread_shutdown = x6500_fpga_shutdown,
};
