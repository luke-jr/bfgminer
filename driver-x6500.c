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
#define X6500_BITSTREAM_USERID "\2\4$B"
#define X6500_MINIMUM_CLOCK    2
#define X6500_DEFAULT_CLOCK  200
#define X6500_MAXIMUM_CLOCK  210

struct device_api x6500_api;

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
	x6500->cgpu_data = NULL;
	if (!ftdi)
		return false;
	if (!ft232r_set_bitmode(ftdi, 0xee, 4))
		return false;
	if (!ft232r_purge_buffers(ftdi, FTDI_PURGE_BOTH))
		return false;
	x6500->device_ft232r = ftdi;
	x6500->cgpu_data = calloc(1, 1);
	
	return true;
}

struct x6500_fpga_data {
	struct jtag_port jtag;
};

#define bailout(...) do{return false;}while(0)
#define bailout2(...) do{return false;}while(0)
#define check_magic(L)  do {  \
	if (1 != fread(buf, 1, 1, f))  \
		bailout(LOG_ERR, "Error reading ModMiner firmware ('%c')", L);  \
	if (buf[0] != L)  \
		bailout(LOG_ERR, "ModMiner firmware has wrong magic ('%c')", L);  \
} while(0)
#define read_str(eng)  do {  \
	if (1 != fread(buf, 2, 1, f))  \
		bailout(LOG_ERR, "Error reading ModMiner firmware (" eng " len)");  \
	len = (ubuf[0] << 8) | ubuf[1];  \
	if (len >= sizeof(buf))  \
		bailout(LOG_ERR, "ModMiner firmware " eng " too long");  \
	if (1 != fread(buf, len, 1, f))  \
		bailout(LOG_ERR, "Error reading ModMiner firmware (" eng ")");  \
	buf[len] = '\0';  \
} while(0)

static bool
x6500_fpga_upload_bitstream(struct cgpu_info *x6500, struct ft232r_device_handle *ftdi)
{
	char buf[0x100];
	unsigned long len, flen;
	char *pdone = (char*)&x6500->cgpu_data;

	FILE *f = open_xilinx_bitstream(x6500, X6500_BITSTREAM_FILENAME, &len);
	if (!f)
		return false;

	flen = len;

	applog(LOG_WARNING, "%s %u: Programming %s...",
	       x6500->api->name, x6500->device_id, x6500->device_path);
	
	// "Magic" jtag_port configured to access both FPGAs concurrently
	uint8_t dummyx;
	struct jtag_port jpt = {
		.ftdi = ftdi,
		.tck = 0x88,
		.tms = 0x44,
		.tdi = 0x22,
		.tdo = 0x11,
		.ignored = 0,
		.state = &dummyx,
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
	jp->async = true;

	ssize_t buflen;
	char nextstatus = 10;
	while (len) {
		buflen = len < 32 ? len : 32;
		if (fread(buf, buflen, 1, f) != 1)
			bailout2(LOG_ERR, "%s %u: File underrun programming %s (%d bytes left)", x6500->api->name, x6500->device_id, x6500->device_path, len);
		jtag_swrite_more(jp, buf, buflen * 8, len == buflen);
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
	jp->async = false;

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
	uint8_t pinoffset = thr->device_thread ? 0x10 : 1;
	unsigned char buf[4];
	int i;
	
	if (!ftdi)
		return false;
	
	fpga = calloc(1, sizeof(*fpga));
	jp = &fpga->jtag;
	jp->ftdi = ftdi;
	jp->tck = pinoffset << 3;
	jp->tms = pinoffset << 2;
	jp->tdi = pinoffset << 1;
	jp->tdo = pinoffset << 0;
	jp->ignored = ~(fpga->jtag.tdo | fpga->jtag.tdi | fpga->jtag.tms | fpga->jtag.tck);
	jp->state = x6500->cgpu_data;
	
	applog(LOG_ERR, "jtag pins: tck=%02x tms=%02x tdi=%02x tdo=%02x", pinoffset << 3, pinoffset << 2, pinoffset << 1, pinoffset << 0);
	
	mutex_lock(&x6500->device_mutex);
	if (!jtag_reset(jp)) {
		mutex_unlock(&x6500->device_mutex);
		applog(LOG_ERR, "jtag reset failed");
		return false;
	}
	
// 	while(gets(buf)) {
// 		jtag_clock(jp, buf[0]=='/', buf[1]=='/', &buf[2]);
// 		applog(LOG_ERR, "=> %d", (int)buf[2]);
// 	}
	
	
	i = jtag_detect(jp);
	if (i != 1) {
		mutex_unlock(&x6500->device_mutex);
		applog(LOG_ERR, "jtag detect returned %d", i);
		return false;
	}
	
		x6500_fpga_upload_bitstream(x6500, ftdi);
	if (!(1
	 && jtag_write(jp, JTAG_REG_IR, "\x10", 6)
	 && jtag_read (jp, JTAG_REG_DR, buf, 32)
	 && jtag_reset(jp)
	)) {
		mutex_unlock(&x6500->device_mutex);
		applog(LOG_ERR, "jtag error reading user code");
		return false;
	}
	
	if (memcmp(buf, X6500_BITSTREAM_USERID, 4)) {
	}
	applog(LOG_ERR, "userid: %s", bin2hex(buf, 4));
	
	mutex_unlock(&x6500->device_mutex);
	return false;
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

struct device_api x6500_api = {
	.dname = "x6500",
	.name = "XBS",
	.api_detect = x6500_detect,
	.thread_prepare = x6500_prepare,
	.thread_init = x6500_fpga_init,
	.get_statline_before = get_x6500_statline_before,
// 	.scanhash = x6500_fpga_scanhash,
// 	.thread_shutdown = x6500_fpga_shutdown,
};
