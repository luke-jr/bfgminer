/*
 * Copyright 2012-2014 Luke Dashjr
 * Copyright 2013 Nate Woolls
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include <libusb.h>

#include "binloader.h"
#include "compat.h"
#include "deviceapi.h"
#include "dynclock.h"
#include "jtag.h"
#include "logging.h"
#include "miner.h"
#include "lowlevel.h"
#include "lowl-ftdi.h"
#include "lowl-usb.h"

#define X6500_USB_PRODUCT "X6500 FPGA Miner"
#define X6500_BITSTREAM_FILENAME "fpgaminer_x6500-overclocker-0402.bit"
// NOTE: X6500_BITSTREAM_USERID is bitflipped
#define X6500_BITSTREAM_USERID "\x40\x20\x24\x42"
#define X6500_MINIMUM_CLOCK    2
#define X6500_DEFAULT_CLOCK  190
#define X6500_MAXIMUM_CLOCK  250

BFG_REGISTER_DRIVER(x6500_api)

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

static
bool x6500_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_lowlproduct(info, &lowl_ft232r, X6500_USB_PRODUCT);
}

static
bool x6500_lowl_probe(const struct lowlevel_device_info * const info)
{
	const char * const product = info->product;
	const char * const serial = info->serial;
	if (info->lowl != &lowl_ft232r)
	{
		bfg_probe_result_flags = BPR_WRONG_DEVTYPE;
		if (info->lowl != &lowl_usb)
			applog(LOG_DEBUG, "%s: Matched \"%s\" serial \"%s\", but lowlevel driver is not ft232r!",
			       __func__, product, serial);
		return false;
	}
	
	libusb_device * const dev = info->lowl_data;
	if (bfg_claim_libusb(&x6500_api, true, dev))
		return false;
	
	struct cgpu_info *x6500;
	x6500 = calloc(1, sizeof(*x6500));
	x6500->drv = &x6500_api;
	x6500->device_path = strdup(serial);
	x6500->deven = DEV_ENABLED;
	x6500->threads = 1;
	x6500->procs = 2;
	x6500->name = strdup(product);
	x6500->cutofftemp = 85;
	x6500->device_data = lowlevel_ref(info);
	cgpu_copy_libusb_strings(x6500, dev);

	return add_cgpu(x6500);
}

static bool x6500_prepare(struct thr_info *thr)
{
	struct cgpu_info *x6500 = thr->cgpu;
	
	if (x6500->proc_id)
		return true;
	
	struct ft232r_device_handle *ftdi = ft232r_open(x6500->device_data);
	lowlevel_devinfo_free(x6500->device_data);
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
	x6500->device_data = jtag_a;
	
	for (struct cgpu_info *slave = x6500->next_proc; slave; slave = slave->next_proc)
	{
		slave->device_ft232r = x6500->device_ft232r;
		slave->device_data = x6500->device_data;
	}
	
	return true;
}

struct x6500_fpga_data {
	struct jtag_port jtag;
	struct timeval tv_hashstart;
	int64_t hashes_left;

	struct dclk_data dclk;
	uint8_t freqMaxMaxM;

	// Time the clock was last reduced due to temperature
	struct timeval tv_last_cutoff_reduced;

	uint32_t prepwork_last_register;
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
	unsigned char *pdone = (unsigned char*)x6500->device_data - 1;
	struct ft232r_device_handle *ftdi = jp1->a->ftdi;

	FILE *f = open_xilinx_bitstream(x6500->drv->dname, x6500->dev_repr, X6500_BITSTREAM_FILENAME, &len);
	if (!f)
		return false;

	flen = len;

	applog(LOG_WARNING, "%s: Programming %s...",
	       x6500->dev_repr, x6500->device_path);
	x6500->status = LIFE_INIT2;
	
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
		applog(LOG_DEBUG, "%s%c: JPROGRAM ready",
		       x6500->dev_repr, 'a' + j);
	}
	x6500_jtag_set(jp, 0x11);
	jtag_write(jp, JTAG_REG_IR, "\xa0", 6);  // CFG_IN
	
	cgsleep_ms(1000);
	
	if (fread(buf, 32, 1, f) != 1)
		bailout2(LOG_ERR, "%s: File underrun programming %s (%lu bytes left)", x6500->dev_repr, x6500->device_path, len);
	jtag_swrite(jp, JTAG_REG_DR, buf, 256);
	len -= 32;
	
	// Put ft232r chip in asynchronous bitbang mode so we don't need to read back tdo
	// This takes upload time down from about an hour to about 3 minutes
	if (!ft232r_set_bitmode(ftdi, 0xee, 1))
		return false;
	if (!ft232r_purge_buffers(ftdi, FTDI_PURGE_BOTH))
		return false;
	jp->a->bufread = 0;
	jp->a->async = true;

	ssize_t buflen;
	char nextstatus = 25;
	while (len) {
		buflen = len < 32 ? len : 32;
		if (fread(buf, buflen, 1, f) != 1)
			bailout2(LOG_ERR, "%s: File underrun programming %s (%lu bytes left)", x6500->dev_repr, x6500->device_path, len);
		jtag_swrite_more(jp, buf, buflen * 8, len == (unsigned long)buflen);
		*pdone = 100 - ((len * 100) / flen);
		if (*pdone >= nextstatus)
		{
			nextstatus += 25;
			applog(LOG_WARNING, "%s: Programming %s... %d%% complete...", x6500->dev_repr, x6500->device_path, *pdone);
		}
		len -= buflen;
	}
	
	// Switch back to synchronous bitbang mode
	if (!ft232r_set_bitmode(ftdi, 0xee, 4))
		return false;
	if (!ft232r_purge_buffers(ftdi, FTDI_PURGE_BOTH))
		return false;
	jp->a->bufread = 0;
	jp->a->async = false;
	jp->a->bufread = 0;

	jtag_write(jp, JTAG_REG_IR, "\x30", 6);  // JSTART
	for (i=0; i<16; ++i)
		jtag_run(jp);
	i = 0xff;  // BYPASS
	jtag_read(jp, JTAG_REG_IR, &i, 6);
	if (!(i & 4))
		return false;
	
	applog(LOG_WARNING, "%s: Done programming %s", x6500->dev_repr, x6500->device_path);
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
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	uint8_t oldFreq = fpga->dclk.freqM;

	if (!x6500_change_clock(thr, multiplier)) {
		return false;
	}

	dclk_msg_freqchange(x6500->proc_repr, oldFreq * 2, fpga->dclk.freqM * 2, NULL);
	return true;
}

static bool x6500_thread_init(struct thr_info *thr)
{
	struct cgpu_info *x6500 = thr->cgpu;
	struct ft232r_device_handle *ftdi = x6500->device_ft232r;

	cgpu_setup_control_requests(x6500);
	
	// This works because x6500_thread_init is only called for the first processor now that they're all using the same thread
	for ( ; x6500; x6500 = x6500->next_proc)
	{
		thr = x6500->thr[0];

	struct x6500_fpga_data *fpga;
	struct jtag_port *jp;
	int fpgaid = x6500->proc_id;
	uint8_t pinoffset = fpgaid ? 0x10 : 1;
	unsigned char buf[4] = {0};
	int i;
	
	if (!ftdi)
		return false;
	
	fpga = calloc(1, sizeof(*fpga));
	jp = &fpga->jtag;
	jp->a = x6500->device_data;
	x6500_jtag_set(jp, pinoffset);
	thr->cgpu_data = fpga;
	x6500->status = LIFE_INIT2;
	
	if (!jtag_reset(jp)) {
		applog(LOG_ERR, "%s: JTAG reset failed",
		       x6500->dev_repr);
		return false;
	}
	
	i = jtag_detect(jp);
	if (i != 1) {
		applog(LOG_ERR, "%s: JTAG detect returned %d",
		       x6500->dev_repr, i);
		return false;
	}
	
	if (!(1
	 && jtag_write(jp, JTAG_REG_IR, "\x10", 6)
	 && jtag_read (jp, JTAG_REG_DR, buf, 32)
	 && jtag_reset(jp)
	)) {
		applog(LOG_ERR, "%s: JTAG error reading user code",
		       x6500->dev_repr);
		return false;
	}
	
	if (memcmp(buf, X6500_BITSTREAM_USERID, 4)) {
		applog(LOG_ERR, "%"PRIprepr": FPGA not programmed",
		       x6500->proc_repr);
		if (!x6500_fpga_upload_bitstream(x6500, jp))
			return false;
	} else if (opt_force_dev_init && x6500 == x6500->device) {
		applog(LOG_DEBUG, "%"PRIprepr": FPGA is already programmed, but --force-dev-init is set",
		       x6500->proc_repr);
		if (!x6500_fpga_upload_bitstream(x6500, jp))
			return false;
	} else
		applog(LOG_DEBUG, "%s"PRIprepr": FPGA is already programmed :)",
		       x6500->proc_repr);
	
	dclk_prepare(&fpga->dclk);
	fpga->dclk.freqMinM = X6500_MINIMUM_CLOCK / 2;
	x6500_change_clock(thr, X6500_DEFAULT_CLOCK / 2);
	for (i = 0; 0xffffffff != x6500_get_register(jp, 0xE); ++i)
	{}

	if (i)
		applog(LOG_WARNING, "%"PRIprepr": Flushed %d nonces from buffer at init",
		       x6500->proc_repr, i);

	fpga->dclk.minGoodSamples = 3;
	fpga->freqMaxMaxM =
	fpga->dclk.freqMaxM = X6500_MAXIMUM_CLOCK / 2;
	fpga->dclk.freqMDefault = fpga->dclk.freqM;
	applog(LOG_WARNING, "%"PRIprepr": Frequency set to %u MHz (range: %u-%u)",
	       x6500->proc_repr,
	       fpga->dclk.freqM * 2,
	       X6500_MINIMUM_CLOCK,
	       fpga->dclk.freqMaxM * 2);

	}

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

	x6500 = x6500->device;
	for (i = 0; i < 2; ++i, x6500 = x6500->next_proc) {
		struct thr_info *thr = x6500->thr[0];
		fpga = thr->cgpu_data;

		if (!fpga) continue;

		if (code[i] == 0xffff || !code[i]) {
			x6500->temp = 0;
			continue;
		}
		if ((code[i] >> 15) & 1)
			code[i] -= 0x10000;
		x6500->temp = (float)(code[i] >> 2) * 0.03125f;
		applog(LOG_DEBUG,"x6500_get_temperature: fpga[%d]->temp=%.1fC",
		       i, x6500->temp);

		int temperature = round(x6500->temp);
		if (temperature > x6500->targettemp + opt_hysteresis) {
			struct timeval now;
			cgtime(&now);
			if (timer_elapsed(&fpga->tv_last_cutoff_reduced, &now)) {
				fpga->tv_last_cutoff_reduced = now;
				int oldFreq = fpga->dclk.freqM;
				if (x6500_change_clock(thr, oldFreq - 1))
					applog(LOG_NOTICE, "%"PRIprepr": Frequency dropped from %u to %u MHz (temp: %.1fC)",
					       x6500->proc_repr,
					       oldFreq * 2, fpga->dclk.freqM * 2,
					       x6500->temp
					);
				fpga->dclk.freqMaxM = fpga->dclk.freqM;
			}
		}
		else
		if (fpga->dclk.freqMaxM < fpga->freqMaxMaxM && temperature < x6500->targettemp) {
			if (temperature < x6500->targettemp - opt_hysteresis) {
				fpga->dclk.freqMaxM = fpga->freqMaxMaxM;
			} else if (fpga->dclk.freqM == fpga->dclk.freqMaxM) {
				++fpga->dclk.freqMaxM;
			}
		}
	}

}

static
bool x6500_all_idle(struct cgpu_info *any_proc)
{
	for (struct cgpu_info *proc = any_proc->device; proc; proc = proc->next_proc)
		if (proc->thr[0]->tv_poll.tv_sec != -1 || proc->deven == DEV_ENABLED)
			return false;
	return true;
}

static bool x6500_get_stats(struct cgpu_info *x6500)
{
	if (x6500_all_idle(x6500)) {
		struct cgpu_info *cgpu = x6500->device;
		// Getting temperature more efficiently while running
		cgpu_request_control(cgpu);
		x6500_get_temperature(x6500);
		cgpu_release_control(cgpu);
	}

	return true;
}

static
bool get_x6500_upload_percent(char *buf, size_t bufsz, struct cgpu_info *x6500, __maybe_unused bool per_processor)
{
	unsigned char pdone = *((unsigned char*)x6500->device_data - 1);
	if (pdone != 101) {
		tailsprintf(buf, bufsz, "%3d%% ", pdone);
		return true;
	}
	return false;
}

static struct api_data*
get_x6500_api_extra_device_status(struct cgpu_info *x6500)
{
	struct api_data *root = NULL;
	struct thr_info *thr = x6500->thr[0];
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	double d;

	d = (double)fpga->dclk.freqM * 2;
	root = api_add_freq(root, "Frequency", &d, true);
	d = (double)fpga->dclk.freqMaxM * 2;
	root = api_add_freq(root, "Cool Max Frequency", &d, true);
	d = (double)fpga->freqMaxMaxM * 2;
	root = api_add_freq(root, "Max Frequency", &d, true);

	return root;
}

static
bool x6500_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info *x6500 = thr->cgpu;
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	struct jtag_port *jp = &fpga->jtag;
	
	for (int i = 1, j = 0; i < 9; ++i, j += 4)
		x6500_set_register(jp, i, fromlebytes(work->midstate, j));

	for (int i = 9, j = 64; i < 11; ++i, j += 4)
		x6500_set_register(jp, i, fromlebytes(work->data, j));

	x6500_get_temperature(x6500);
	
	ft232r_flush(jp->a->ftdi);
	
	fpga->prepwork_last_register = fromlebytes(work->data, 72);
	
	work->blk.nonce = 0xffffffff;
	
	return true;
}

static int64_t calc_hashes(struct thr_info *, struct timeval *);

static
void x6500_job_start(struct thr_info *thr)
{
	struct cgpu_info *x6500 = thr->cgpu;
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	struct jtag_port *jp = &fpga->jtag;
	struct timeval tv_now;

	if (thr->prev_work)
	{
		dclk_preUpdate(&fpga->dclk);
		dclk_updateFreq(&fpga->dclk, x6500_dclk_change_clock, thr);
	}

	x6500_set_register(jp, 11, fpga->prepwork_last_register);

	ft232r_flush(jp->a->ftdi);

	timer_set_now(&tv_now);
	if (!thr->prev_work)
		fpga->tv_hashstart = tv_now;
	else
	if (thr->prev_work != thr->work)
		calc_hashes(thr, &tv_now);
	fpga->hashes_left = 0x100000000;
	mt_job_transition(thr);
	
	if (opt_debug) {
		char xdata[161];
		bin2hex(xdata, thr->work->data, 80);
		applog(LOG_DEBUG, "%"PRIprepr": Started work: %s",
		       x6500->proc_repr, xdata);
	}

	uint32_t usecs = 0x80000000 / fpga->dclk.freqM;
	usecs -= 1000000;
	timer_set_delay(&thr->tv_morework, &tv_now, usecs);

	timer_set_delay(&thr->tv_poll, &tv_now, 10000);
	
	job_start_complete(thr);
}

static
int64_t calc_hashes(struct thr_info *thr, struct timeval *tv_now)
{
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	struct timeval tv_delta;
	int64_t hashes, hashes_left;

	timersub(tv_now, &fpga->tv_hashstart, &tv_delta);
	hashes = (((int64_t)tv_delta.tv_sec * 1000000) + tv_delta.tv_usec) * fpga->dclk.freqM * 2;
	hashes_left = fpga->hashes_left;
	if (unlikely(hashes > hashes_left))
		hashes = hashes_left;
	fpga->hashes_left -= hashes;
	hashes_done(thr, hashes, &tv_delta, NULL);
	fpga->tv_hashstart = *tv_now;
	return hashes;
}

static
int64_t x6500_process_results(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *x6500 = thr->cgpu;
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	struct jtag_port *jtag = &fpga->jtag;

	struct timeval tv_now;
	int64_t hashes;
	uint32_t nonce;
	bool bad;

	while (1) {
		timer_set_now(&tv_now);
		nonce = x6500_get_register(jtag, 0xE);
		if (nonce != 0xffffffff) {
			bad = !(work && test_nonce(work, nonce, false));
			if (!bad) {
				submit_nonce(thr, work, nonce);
				applog(LOG_DEBUG, "%"PRIprepr": Nonce for current  work: %08lx",
				       x6500->proc_repr,
				       (unsigned long)nonce);

				dclk_gotNonces(&fpga->dclk);
			} else if (likely(thr->prev_work) && test_nonce(thr->prev_work, nonce, false)) {
				submit_nonce(thr, thr->prev_work, nonce);
				applog(LOG_DEBUG, "%"PRIprepr": Nonce for PREVIOUS work: %08lx",
				       x6500->proc_repr,
				       (unsigned long)nonce);
			} else {
				inc_hw_errors(thr, work, nonce);

				dclk_gotNonces(&fpga->dclk);
				dclk_errorCount(&fpga->dclk, 1.);
			}
			// Keep reading nonce buffer until it's empty
			// This is necessary to avoid getting hw errors from Freq B after we've moved on to Freq A
			continue;
		}

		hashes = calc_hashes(thr, &tv_now);
		
		break;
	}

	return hashes;
}

static
void x6500_fpga_poll(struct thr_info *thr)
{
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	
	x6500_process_results(thr, thr->work);
	if (unlikely(!fpga->hashes_left))
	{
		mt_disable_start__async(thr);
		thr->tv_poll.tv_sec = -1;
	}
	else
		timer_set_delay_from_now(&thr->tv_poll, 10000);
}

static
void x6500_user_set_clock(struct cgpu_info *cgpu, const int val)
{
	struct thr_info * const thr = cgpu->thr[0];
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	const int multiplier = val / 2;
	fpga->dclk.freqMDefault = multiplier;
}

static
void x6500_user_set_max_clock(struct cgpu_info *cgpu, const int val)
{
	struct thr_info * const thr = cgpu->thr[0];
	struct x6500_fpga_data *fpga = thr->cgpu_data;
	const int multiplier = val / 2;
	fpga->freqMaxMaxM =
	fpga->dclk.freqMaxM = multiplier;
}

static
char *x6500_set_device(struct cgpu_info *cgpu, char *option, char *setting, char *replybuf)
{
	int val;
	
	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "clock: range %d-%d and a multiple of 2\nmaxclock: default %d, range %d-%d and a multiple of 2",
		        X6500_MINIMUM_CLOCK, X6500_MAXIMUM_CLOCK, X6500_MAXIMUM_CLOCK, X6500_MINIMUM_CLOCK, X6500_MAXIMUM_CLOCK);
		return replybuf;
	}
	
	if (strcasecmp(option, "clock") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing clock setting");
			return replybuf;
		}
		
		val = atoi(setting);
		if (val < X6500_MINIMUM_CLOCK || val > X6500_MAXIMUM_CLOCK || (val & 1) != 0) {
			sprintf(replybuf, "invalid clock: '%s' valid range %d-%d and a multiple of 2",
			        setting, X6500_MINIMUM_CLOCK, X6500_MAXIMUM_CLOCK);
			return replybuf;
		}
		
		x6500_user_set_clock(cgpu, val);
		
		return NULL;
	}
	
	if (strcasecmp(option, "maxclock") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing maxclock setting");
			return replybuf;
		}
		
		val = atoi(setting);
		if (val < X6500_MINIMUM_CLOCK || val > X6500_MAXIMUM_CLOCK || (val & 1) != 0) {
			sprintf(replybuf, "invalid maxclock: '%s' valid range %d-%d and a multiple of 2",
			        setting, X6500_MINIMUM_CLOCK, X6500_MAXIMUM_CLOCK);
			return replybuf;
		}
		
		x6500_user_set_max_clock(cgpu, val);
                
		applog(LOG_NOTICE, "%"PRIpreprv": Maximum frequency reset to %u MHz",
		       cgpu->proc_repr,
		       val
		);
		
		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

#ifdef HAVE_CURSES
static
void x6500_tui_wlogprint_choices(struct cgpu_info *cgpu)
{
	wlogprint("[C]lock speed ");
}

static
const char *x6500_tui_handle_choice(struct cgpu_info *cgpu, int input)
{
	static char buf[0x100];  // Static for replies
	
	switch (input)
	{
		case 'c': case 'C':
		{
			int val;
			char *intvar;
			
			sprintf(buf, "Set clock speed (range %d-%d, multiple of 2)", X6500_MINIMUM_CLOCK, X6500_MAXIMUM_CLOCK);
			intvar = curses_input(buf);
			if (!intvar)
				return "Invalid clock speed\n";
			val = atoi(intvar);
			free(intvar);
			if (val < X6500_MINIMUM_CLOCK || val > X6500_MAXIMUM_CLOCK || (val & 1) != 0)
				return "Invalid clock speed\n";
			
			x6500_user_set_clock(cgpu, val);
			return "Clock speed changed\n";
		}
	}
	return NULL;
}

static
void x6500_wlogprint_status(struct cgpu_info *cgpu)
{
	struct x6500_fpga_data *fpga = cgpu->thr[0]->cgpu_data;
	wlogprint("Clock speed: %d\n", (int)(fpga->dclk.freqM * 2));
}
#endif

struct device_drv x6500_api = {
	.dname = "x6500",
	.name = "XBS",
	.lowl_match = x6500_lowl_match,
	.lowl_probe = x6500_lowl_probe,
	.thread_prepare = x6500_prepare,
	.thread_init = x6500_thread_init,
	.get_stats = x6500_get_stats,
	.override_statline_temp2 = get_x6500_upload_percent,
	.get_api_extra_device_status = get_x6500_api_extra_device_status,
	.set_device = x6500_set_device,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = x6500_wlogprint_status,
	.proc_tui_wlogprint_choices = x6500_tui_wlogprint_choices,
	.proc_tui_handle_choice = x6500_tui_handle_choice,
#endif
	.poll = x6500_fpga_poll,
	.minerloop = minerloop_async,
	.job_prepare = x6500_job_prepare,
	.job_start = x6500_job_start,
// 	.thread_shutdown = x6500_fpga_shutdown,
};
