/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <sys/time.h>

#include <libusb-1.0/libusb.h>

#include "compat.h"
#include "dynclock.h"
#include "jtag.h"
#include "logging.h"
#include "miner.h"
#include "fpgautils.h"
#include "ft232r.h"

#define X6500_USB_PRODUCT "X6500 FPGA Miner"
#define X6500_BITSTREAM_FILENAME "x6500-overclocker-0402.bit"
// NOTE: X6500_BITSTREAM_USERID is bitflipped
#define X6500_BITSTREAM_USERID "\x40\x20\x24\x42"
#define X6500_MINIMUM_CLOCK    2
#define X6500_DEFAULT_CLOCK  190
#define X6500_MAXIMUM_CLOCK  250

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
void x6500_jtag_set(struct jtag_port *jp, uint8_t pinoffset)
{
	jp->tck = pinoffset << 3;
	jp->tms = pinoffset << 2;
	jp->tdi = pinoffset << 1;
	jp->tdo = pinoffset << 0;
	jp->ignored = ~(jp->tdo | jp->tdi | jp->tms | jp->tck);
}

static uint32_t x6500_get_register(struct jtag_port *jp, uint8_t addr);

static
void x6500_set_register(struct jtag_port *jp, uint8_t addr, uint32_t nv)
{
	uint8_t buf[38];
retry:
	jtag_write(jp, JTAG_REG_IR, "\x40", 6);
	int2bits(nv, &buf[0], 32);
	int2bits(addr, &buf[4], 4);
	buf[4] |= 8;
	checksum(buf, 37);
	jtag_write(jp, JTAG_REG_DR, buf, 38);
	jtag_run(jp);
#ifdef DEBUG_X6500_SET_REGISTER
	if (x6500_get_register(jp, addr) != nv)
#else
	if (0)
#endif
	{
		applog(LOG_WARNING, "x6500_set_register failed %x=%08x", addr, nv);
		goto retry;
	}
}

static
uint32_t x6500_get_register(struct jtag_port *jp, uint8_t addr)
{
	uint8_t buf[4] = {0};
	jtag_write(jp, JTAG_REG_IR, "\x40", 6);
	int2bits(addr, &buf[0], 4);
	checksum(buf, 5);
	jtag_write(jp, JTAG_REG_DR, buf, 6);
	jtag_read (jp, JTAG_REG_DR, buf, 32);
	jtag_reset(jp);
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
	unsigned char *pdone = calloc(1, sizeof(*jtag_a) + 1);
	*pdone = 101;
	jtag_a = (void*)(pdone + 1);
	jtag_a->ftdi = ftdi;
	x6500->cgpu_data = jtag_a;
	
	return true;
}

struct x6500_fpga_data {
	struct jtag_port jtag;
	struct work prevwork;
	struct timeval tv_workstart;
	struct dclk_data dclk;
	float temp;
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
	unsigned char *pdone = (unsigned char*)x6500->cgpu_data - 1;
	struct ft232r_device_handle *ftdi = jp1->a->ftdi;

	FILE *f = open_xilinx_bitstream(x6500, X6500_BITSTREAM_FILENAME, &len);
	if (!f)
		return false;

	flen = len;

	applog(LOG_WARNING, "%s %u: Programming %s...",
	       x6500->api->name, x6500->device_id, x6500->device_path);
	x6500->status = LIFE_INIT;
	
	// "Magic" jtag_port configured to access both FPGAs concurrently
	struct jtag_port jpt = {
		.a = jp1->a,
	};
	struct jtag_port *jp = &jpt;
	uint8_t i, j;
	x6500_jtag_set(jp, 0x11);
	
	// Need to reset here despite previous FPGA state, since we are programming all at once
	jtag_reset(jp);
	
	jtag_write(jp, JTAG_REG_IR, "\xd0", 6);  // JPROGRAM
	// Poll each FPGA status individually since they might not be ready at the same time
	for (j = 0; j < 2; ++j) {
		x6500_jtag_set(jp, j ? 0x10 : 1);
		do {
			i = 0xd0;  // Re-set JPROGRAM while reading status
			jtag_read(jp, JTAG_REG_IR, &i, 6);
		} while (i & 8);
		applog(LOG_DEBUG, "%s %u.%u: JPROGRAM ready",
		       x6500->api->name, x6500->device_id, j);
	}
	x6500_jtag_set(jp, 0x11);
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
	char nextstatus = 25;
	while (len) {
		buflen = len < 32 ? len : 32;
		if (fread(buf, buflen, 1, f) != 1)
			bailout2(LOG_ERR, "%s %u: File underrun programming %s (%d bytes left)", x6500->api->name, x6500->device_id, x6500->device_path, len);
		jtag_swrite_more(jp, buf, buflen * 8, len == (unsigned long)buflen);
		*pdone = 100 - ((len * 100) / flen);
		if (*pdone >= nextstatus)
		{
			nextstatus += 25;
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
	*pdone = 101;

	return true;
}

static bool x6500_change_clock(struct thr_info *thr, int multiplier)
{
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	struct jtag_port *jp = &fpga->jtag;

	x6500_set_register(jp, 0xD, multiplier * 2);
	ft232r_flush(jp->a->ftdi);
	fpga->dclk.freqM = multiplier;

	return true;
}

static bool x6500_dclk_change_clock(struct thr_info *thr, int multiplier)
{
	struct cgpu_info *x6500 = thr->cgpu;
	char fpgaid = thr->device_thread;
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	uint8_t oldFreq = fpga->dclk.freqM;

	mutex_lock(&x6500->device_mutex);
	if (!x6500_change_clock(thr, multiplier)) {
		mutex_unlock(&x6500->device_mutex);
		return false;
	}
	mutex_unlock(&x6500->device_mutex);

	char repr[0x10];
	sprintf(repr, "%s %u.%u", x6500->api->name, x6500->device_id, fpgaid);
	dclk_msg_freqchange(repr, oldFreq * 2, fpga->dclk.freqM * 2, NULL);
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
	unsigned char buf[4] = {0};
	int i;
	
	if (!ftdi)
		return false;
	
	fpga = calloc(1, sizeof(*fpga));
	jp = &fpga->jtag;
	jp->a = x6500->cgpu_data;
	x6500_jtag_set(jp, pinoffset);
	
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

	dclk_prepare(&fpga->dclk);
	fpga->dclk.freqMaxM = X6500_MAXIMUM_CLOCK / 2;
	x6500_change_clock(thr, X6500_DEFAULT_CLOCK / 2);
	fpga->dclk.freqMDefault = fpga->dclk.freqM;
	applog(LOG_WARNING, "%s %u.%u: Frequency set to %u Mhz (range: %u-%u)",
	       x6500->api->name, x6500->device_id, fpgaid,
	       fpga->dclk.freqM * 2,
	       X6500_MINIMUM_CLOCK,
	       fpga->dclk.freqMaxM * 2);

	mutex_unlock(&x6500->device_mutex);
	return true;
}

static 
void x6500_get_temperature(struct cgpu_info *x6500)
{

	struct x6500_fpga_data *fpga = x6500->thr[0]->cgpu_data;
	struct jtag_port *jp = &fpga->jtag;
	struct ft232r_device_handle *ftdi = jp->a->ftdi;
	int i, code[2];
	bool sio[2];

	code[0] = 0;
	code[1] = 0;

	ft232r_flush(ftdi);


	if (!(ft232r_set_cbus_bits(ftdi, false, true))) return;
	if (!(ft232r_set_cbus_bits(ftdi, true, true))) return;
	if (!(ft232r_set_cbus_bits(ftdi, false, true))) return;
	if (!(ft232r_set_cbus_bits(ftdi, true, true))) return;
	if (!(ft232r_set_cbus_bits(ftdi, false, false))) return;

	for (i = 16; i--; ) {
		if (ft232r_set_cbus_bits(ftdi, true, false)) {
			if (!(ft232r_get_cbus_bits(ftdi, &sio[0], &sio[1]))) {
				return;
			}
		} else {
			return;
		}

		code[0] |= sio[0] << i;
		code[1] |= sio[1] << i;
		if (!ft232r_set_cbus_bits(ftdi, false, false)) {
			return;
		}
	}

	if (!(ft232r_set_cbus_bits(ftdi, false, true))) {
		return;
	}
	if (!(ft232r_set_cbus_bits(ftdi, true, true))) {
		return;
	}
	if (!(ft232r_set_cbus_bits(ftdi, false, true))) {
		return;
	}
	if (!ft232r_set_bitmode(ftdi, 0xee, 4)) {
		return;
	}
	ft232r_purge_buffers(jp->a->ftdi, FTDI_PURGE_BOTH);
	jp->a->bufread = 0;

	for (i = 0; i < 2; ++i) {
		fpga = x6500->thr[i]->cgpu_data;
		if (code[i] == 0xffff || !code[i]) {
			fpga->temp = 0;
			continue;
		}
		if ((code[i] >> 15) & 1)
			code[i] -= 0x10000;
		fpga->temp = (float)(code[i] >> 2) * 0.03125f;
		applog(LOG_DEBUG,"x6500_get_temperature: fpga[%d]->temp=%.1fC",i,fpga->temp);
	}

}

static bool x6500_get_stats(struct cgpu_info *x6500)
{
	float hottest = 0;
	if (x6500->deven != DEV_ENABLED) {
		// Getting temperature more efficiently while enabled
		// NOTE: Don't need to mess with mutex here, since the device is disabled
		x6500_get_temperature(x6500);
	} else {
		mutex_lock(&x6500->device_mutex);
		x6500_get_temperature(x6500);
		mutex_unlock(&x6500->device_mutex);
	}

	for (int i = x6500->threads; i--; ) {
		struct thr_info *thr = x6500->thr[i];
		struct x6500_fpga_data *fpga = thr->cgpu_data;
		if (!fpga)
			continue;
		float temp = fpga->temp;
		if (temp > hottest)
			hottest = temp;
	}

	x6500->temp = hottest;

	return true;
}

static void
get_x6500_statline_before(char *buf, struct cgpu_info *x6500)
{
	char info[18] = "               | ";
	struct x6500_fpga_data *fpga0 = x6500->thr[0]->cgpu_data;
	struct x6500_fpga_data *fpga1 = x6500->thr[1]->cgpu_data;

	unsigned char pdone = *((unsigned char*)x6500->cgpu_data - 1);
	if (pdone != 101) {
		sprintf(&info[1], "%3d%%", pdone);
		info[5] = ' ';
		strcat(buf, info);
		return;
	}
	if (x6500->temp) {
		sprintf(&info[1], "%.1fC/%.1fC", fpga0->temp, fpga1->temp);
		info[strlen(info)] = ' ';
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
	struct jtag_port *jp = &fpga->jtag;
	char fpgaid = thr->device_thread;

	mutex_lock(&x6500->device_mutex);

	for (int i = 1, j = 0; i < 9; ++i, j += 4)
		x6500_set_register(jp, i, fromlebytes(work->midstate, j));

	for (int i = 9, j = 64; i < 12; ++i, j += 4)
		x6500_set_register(jp, i, fromlebytes(work->data, j));

	ft232r_flush(jp->a->ftdi);

	gettimeofday(&fpga->tv_workstart, NULL);
	//x6500_get_temperature(x6500);
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
int64_t calc_hashes(struct x6500_fpga_data *fpga, struct timeval *tv_now)
{
	struct timeval tv_delta;
	int64_t hashes;

	timersub(tv_now, &fpga->tv_workstart, &tv_delta);
	hashes = (((int64_t)tv_delta.tv_sec * 1000000) + tv_delta.tv_usec) * fpga->dclk.freqM * 2;
	if (unlikely(hashes > 0x100000000))
		hashes = 0x100000000;
	return hashes;
}

static
int64_t x6500_process_results(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *x6500 = thr->cgpu;
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	struct jtag_port *jtag = &fpga->jtag;
	char fpgaid = thr->device_thread;

	struct timeval tv_now;
	int64_t hashes;
	uint32_t nonce;
	bool bad;
	int imm_bad_nonces = 0, imm_nonces = 0;

	while (1) {
		mutex_lock(&x6500->device_mutex);
		gettimeofday(&tv_now, NULL);
		nonce = x6500_get_register(jtag, 0xE);
		mutex_unlock(&x6500->device_mutex);
		if (nonce != 0xffffffff) {
			++imm_nonces;
			bad = !test_nonce(work, nonce, false);
			if (!bad) {
				submit_nonce(thr, work, nonce);
				applog(LOG_DEBUG, "%s %u.%u: Nonce for current  work: %08lx",
				       x6500->api->name, x6500->device_id, fpgaid,
				       (unsigned long)nonce);
			} else if (test_nonce(&fpga->prevwork, nonce, false)) {
				submit_nonce(thr, &fpga->prevwork, nonce);
				applog(LOG_DEBUG, "%s %u.%u: Nonce for PREVIOUS work: %08lx",
				       x6500->api->name, x6500->device_id, fpgaid,
				       (unsigned long)nonce);
			} else {
				applog(LOG_DEBUG, "%s %u.%u: Nonce with H not zero  : %08lx",
				       x6500->api->name, x6500->device_id, fpgaid,
				       (unsigned long)nonce);
				++hw_errors;
				++x6500->hw_errors;
				++imm_bad_nonces;
			}
		}

		hashes = calc_hashes(fpga, &tv_now);
		if (thr->work_restart || hashes >= 0xf0000000)
			break;
		usleep(10000);
		hashes = calc_hashes(fpga, &tv_now);
		if (thr->work_restart || hashes >= 0xf0000000)
			break;
	}

	dclk_gotNonces(&fpga->dclk);
	if (imm_bad_nonces)
		dclk_errorCount(&fpga->dclk, ((double)imm_bad_nonces) / (double)imm_nonces);
	dclk_preUpdate(&fpga->dclk);
	dclk_updateFreq(&fpga->dclk, x6500_dclk_change_clock, thr);

	clear_work(&fpga->prevwork);
	workcpy(&fpga->prevwork, work);

	return hashes;
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
	.get_stats = x6500_get_stats,
	.get_statline_before = get_x6500_statline_before,
	.scanhash = x6500_scanhash,
// 	.thread_shutdown = x6500_fpga_shutdown,
};
