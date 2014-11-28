/*
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "binloader.h"
#include "compat.h"
#include "dynclock.h"
#include "logging.h"
#include "miner.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "util.h"

#define BITSTREAM_FILENAME "fpgaminer_x6500-overclocker-0402.bit"
#define BISTREAM_USER_ID "\2\4$B"

#define MODMINER_MAX_CLOCK 250
#define MODMINER_DEF_CLOCK 190
#define MODMINER_MIN_CLOCK   2

// Commands
#define MODMINER_PING "\x00"
#define MODMINER_GET_VERSION "\x01"
#define MODMINER_FPGA_COUNT "\x02"
// Commands + require FPGAid
#define MODMINER_GET_IDCODE '\x03'
#define MODMINER_GET_USERCODE '\x04'
#define MODMINER_PROGRAM '\x05'
#define MODMINER_SET_CLOCK '\x06'
#define MODMINER_READ_CLOCK '\x07'
#define MODMINER_SEND_WORK '\x08'
#define MODMINER_CHECK_WORK '\x09'
// One byte temperature reply
#define MODMINER_TEMP1 '\x0a'

#define FPGAID_ALL 4

BFG_REGISTER_DRIVER(modminer_drv)

struct modminer_fpga_state {
	bool work_running;
	struct work running_work;
	struct work last_work;
	struct timeval tv_workstart;
	uint32_t hashes;

	char next_work_cmd[46];

	struct dclk_data dclk;
	uint8_t freqMaxMaxM;
	// Number of nonces didn't meet pdiff 1, ever
	int bad_share_counter;
	// Number of nonces did meet pdiff 1, ever
	int good_share_counter;
	// Time the clock was last reduced due to temperature
	struct timeval tv_last_cutoff_reduced;

	unsigned char temp;

	unsigned char pdone;
};

static inline bool _bailout(int fd, struct cgpu_info*modminer, int prio, const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 4, 5);

static inline bool
_bailout(int fd, struct cgpu_info*modminer, int prio, const char *fmt, ...)
{
	if (fd != -1)
		serial_close(fd);
	if (modminer) {
		pthread_mutex_t *mutexp = &modminer->device->device_mutex;
		modminer->device->device_fd = -1;
		mutex_unlock(mutexp);
	}

	va_list ap;
	char buf[LOGBUFSIZ];
	
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	_applog(prio, buf);
	return false;
}
#define bailout(...)  return _bailout(fd, NULL, __VA_ARGS__);

// 45 noops sent when detecting, in case the device was left in "start job" reading
static const char NOOP[] = MODMINER_PING "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";

static
bool modminer_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_product(info, "ModMiner");
}

static bool
modminer_detect_one(const char *devpath)
{
	int fd = serial_open(devpath, 0, 10, true);
	if (unlikely(fd == -1))
		bailout(LOG_DEBUG, "ModMiner detect: failed to open %s", devpath);

	char buf[0x100];
	ssize_t len;

	// Sending a "ping" first, to workaround bug in new firmware betas (see issue #62)
	// Sending 45 noops, just in case the device was left in "start job" reading
	(void)(write(fd, NOOP, sizeof(NOOP)) ?:0);
	while (serial_read(fd, buf, sizeof(buf)) > 0)
		;

	if (1 != write(fd, MODMINER_GET_VERSION, 1))
		bailout(LOG_DEBUG, "ModMiner detect: write failed on %s (get version)", devpath);
	len = serial_read(fd, buf, sizeof(buf)-1);
	if (len < 1)
		bailout(LOG_DEBUG, "ModMiner detect: no response to version request from %s", devpath);
	buf[len] = '\0';
	if (strncasecmp(buf, "ModMiner", 8))
		bailout(LOG_DEBUG, "%s: %s: response did not begin with 'ModMiner'", __func__, devpath);
	applog(LOG_DEBUG, "ModMiner identified as: %s", buf);

	if (serial_claim_v(devpath, &modminer_drv))
	{
		serial_close(fd);
		return false;
	}
	
	char*devname = strdup(buf);
	if (1 != write(fd, MODMINER_FPGA_COUNT, 1))
		bailout(LOG_DEBUG, "ModMiner detect: write failed on %s (get FPGA count)", devpath);
	len = read(fd, buf, 1);
	if (len < 1)
		bailout(LOG_ERR, "ModMiner detect: timeout waiting for FPGA count from %s", devpath);
	if (!buf[0])
		bailout(LOG_ERR, "ModMiner detect: zero FPGAs reported on %s", devpath);
	applog(LOG_DEBUG, "ModMiner %s has %u FPGAs", devname, buf[0]);

	serial_close(fd);

	struct cgpu_info *modminer;
	modminer = calloc(1, sizeof(*modminer));
	modminer->drv = &modminer_drv;
	mutex_init(&modminer->device_mutex);
	modminer->device_path = strdup(devpath);
	modminer->device_fd = -1;
	modminer->deven = DEV_ENABLED;
	modminer->procs = buf[0];
	modminer->threads = buf[0];
	modminer->name = devname;
	modminer->cutofftemp = 85;

	return add_cgpu(modminer);
}

#undef bailout

static
bool modminer_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, modminer_detect_one);
}

#define bailout(...)  return _bailout(-1, modminer, __VA_ARGS__);
#define bailout2(...)  return _bailout(fd, modminer, __VA_ARGS__);
#define bailout3(...)  _bailout(fd, modminer, __VA_ARGS__);

static bool
modminer_reopen(struct cgpu_info*modminer)
{
	serial_close(modminer->device->device_fd);
	int fd = serial_open(modminer->device_path, 0, 10, true);
	if (unlikely(-1 == fd)) {
		applog(LOG_ERR, "%s: Failed to reopen %s", modminer->dev_repr, modminer->device_path);
		return false;
	}
	modminer->device->device_fd = fd;
	return true;
}
#define safebailout() do {  \
	bool _safebailoutrv;  \
	state->work_running = false;  \
	_safebailoutrv = modminer_reopen(modminer);  \
	mutex_unlock(mutexp);  \
	return _safebailoutrv ? 0 : -1;  \
} while(0)

#define check_magic(L)  do {  \
	if (1 != fread(buf, 1, 1, f))  \
		bailout(LOG_ERR, "Error reading ModMiner bitstream ('%c')", L);  \
	if (buf[0] != L)  \
		bailout(LOG_ERR, "ModMiner bitstream has wrong magic ('%c')", L);  \
} while(0)

#define read_str(eng)  do {  \
	if (1 != fread(buf, 2, 1, f))  \
		bailout(LOG_ERR, "Error reading ModMiner bitstream (" eng " len)");  \
	len = (ubuf[0] << 8) | ubuf[1];  \
	if (len >= sizeof(buf))  \
		bailout(LOG_ERR, "ModMiner bitstream " eng " too long");  \
	if (1 != fread(buf, len, 1, f))  \
		bailout(LOG_ERR, "Error reading ModMiner bitstream (" eng ")");  \
	buf[len] = '\0';  \
} while(0)

#define status_read(eng)  do {  \
FD_ZERO(&fds); \
FD_SET(fd, &fds);  \
select(fd+1, &fds, NULL, NULL, NULL);  \
	if (1 != read(fd, buf, 1))  \
		bailout2(LOG_ERR, "%s: Error programming %s (" eng ")", modminer->dev_repr, modminer->device_path);  \
	if (buf[0] != 1)  \
		bailout2(LOG_ERR, "%s: Wrong " eng " programming %s", modminer->dev_repr, modminer->device_path);  \
} while(0)

static bool
modminer_fpga_upload_bitstream(struct cgpu_info*modminer)
{
	struct modminer_fpga_state *state = modminer->thr[0]->cgpu_data;
	fd_set fds;
	char buf[0x100];
	unsigned long len, flen;
	char fpgaid = FPGAID_ALL;
	FILE *f = open_xilinx_bitstream(modminer->drv->dname, modminer->dev_repr, BITSTREAM_FILENAME, &len);
	if (!f)
		return false;

	flen = len;
	int fd = modminer->device->device_fd;

	applog(LOG_WARNING, "%s: Programming %s... DO NOT EXIT UNTIL COMPLETE", modminer->dev_repr, modminer->device_path);
	buf[0] = MODMINER_PROGRAM;
	buf[1] = fpgaid;
	buf[2] = (len >>  0) & 0xff;
	buf[3] = (len >>  8) & 0xff;
	buf[4] = (len >> 16) & 0xff;
	buf[5] = (len >> 24) & 0xff;
	if (6 != write(fd, buf, 6))
		bailout2(LOG_ERR, "%s: Error programming %s (cmd)", modminer->dev_repr, modminer->device_path);
	status_read("cmd reply");
	ssize_t buflen;
	char nextstatus = 10;
	while (len) {
		buflen = len < 32 ? len : 32;
		if (fread(buf, buflen, 1, f) != 1)
			bailout2(LOG_ERR, "%s: File underrun programming %s (%lu bytes left)", modminer->dev_repr, modminer->device_path, len);
		if (write(fd, buf, buflen) != buflen)
			bailout2(LOG_ERR, "%s: Error programming %s (data)", modminer->dev_repr,  modminer->device_path);
		state->pdone = 100 - ((len * 100) / flen);
		if (state->pdone >= nextstatus)
		{
			nextstatus += 10;
			applog(LOG_WARNING, "%s: Programming %s... %d%% complete...", modminer->dev_repr, modminer->device_path, state->pdone);
		}
		status_read("status");
		len -= buflen;
	}
	status_read("final status");
	applog(LOG_WARNING, "%s: Done programming %s", modminer->dev_repr, modminer->device_path);

	return true;
}

static bool
modminer_device_prepare(struct cgpu_info *modminer)
{
	int fd = serial_open(modminer->device_path, 0, 10, true);
	if (unlikely(-1 == fd))
		bailout(LOG_ERR, "%s: Failed to open %s", modminer->dev_repr, modminer->device_path);

	modminer->device->device_fd = fd;
	applog(LOG_INFO, "%s: Opened %s", modminer->dev_repr, modminer->device_path);

	return true;
}

#undef bailout

static bool
modminer_fpga_prepare(struct thr_info *thr)
{
	struct cgpu_info *proc = thr->cgpu;
	struct cgpu_info *modminer = proc->device;

	// Don't need to lock the mutex here, since prepare runs from the main thread before the miner threads start
	if (modminer->device->device_fd == -1 && !modminer_device_prepare(modminer))
		return false;

	struct modminer_fpga_state *state;
	state = thr->cgpu_data = calloc(1, sizeof(struct modminer_fpga_state));
	dclk_prepare(&state->dclk);
	state->dclk.freqMinM = MODMINER_MIN_CLOCK / 2;
	state->next_work_cmd[0] = MODMINER_SEND_WORK;
	state->next_work_cmd[1] = proc->proc_id;  // FPGA id
	
	proc->status = LIFE_INIT2;

	return true;
}

static bool
modminer_change_clock(struct thr_info*thr, bool needlock, signed char delta)
{
	struct cgpu_info*modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	char fpgaid = modminer->proc_id;
	pthread_mutex_t *mutexp = &modminer->device->device_mutex;
	int fd;
	unsigned char cmd[6], buf[1];
	unsigned char clk;

	clk = (state->dclk.freqM * 2) + delta;

	cmd[0] = MODMINER_SET_CLOCK;
	cmd[1] = fpgaid;
	cmd[2] = clk;
	cmd[3] = cmd[4] = cmd[5] = '\0';

	if (needlock)
		mutex_lock(mutexp);
	fd = modminer->device->device_fd;
	if (6 != write(fd, cmd, 6))
		bailout2(LOG_ERR, "%s: Error writing (set frequency)", modminer->proc_repr);
	if (serial_read(fd, &buf, 1) != 1)
		bailout2(LOG_ERR, "%s: Error reading (set frequency)", modminer->proc_repr);
	if (needlock)
		mutex_unlock(mutexp);

	if (buf[0])
		state->dclk.freqM = clk / 2;
	else
		return false;

	return true;
}

static bool modminer_dclk_change_clock(struct thr_info*thr, int multiplier)
{
	struct cgpu_info *modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	uint8_t oldFreq = state->dclk.freqM;
	signed char delta = (multiplier - oldFreq) * 2;
	if (unlikely(!modminer_change_clock(thr, true, delta)))
		return false;

	dclk_msg_freqchange(modminer->proc_repr, oldFreq * 2, state->dclk.freqM * 2, NULL);
	return true;
}

static bool
modminer_reduce_clock(struct thr_info*thr, bool needlock)
{
	struct modminer_fpga_state *state = thr->cgpu_data;

	if (state->dclk.freqM <= MODMINER_MIN_CLOCK / 2)
		return false;

	return modminer_change_clock(thr, needlock, -2);
}

static bool _modminer_get_nonce(struct cgpu_info*modminer, char fpgaid, uint32_t*nonce)
{
	int fd = modminer->device->device_fd;
	char cmd[2] = {MODMINER_CHECK_WORK, fpgaid};
	
	if (write(fd, cmd, 2) != 2) {
		applog(LOG_ERR, "%s: Error writing (get nonce)", modminer->proc_repr);
		return false;
	}
	if (4 != serial_read(fd, nonce, 4)) {
		applog(LOG_ERR, "%s: Short read (get nonce)", modminer->proc_repr);
		return false;
	}
	
	return true;
}

static bool
modminer_fpga_init(struct thr_info *thr)
{
	struct cgpu_info *modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	int fd;
	char fpgaid = modminer->proc_id;
	pthread_mutex_t *mutexp = &modminer->device->device_mutex;
	uint32_t nonce;

	unsigned char cmd[2], buf[4];

	mutex_lock(mutexp);
	fd = modminer->device->device_fd;
	if (fd == -1) {
		// Died in another thread...
		mutex_unlock(mutexp);
		return false;
	}

	cmd[0] = MODMINER_GET_USERCODE;
	cmd[1] = fpgaid;
	if (write(fd, cmd, 2) != 2)
		bailout2(LOG_ERR, "%s: Error writing (read USER code)", modminer->proc_repr);
	if (serial_read(fd, buf, 4) != 4)
		bailout2(LOG_ERR, "%s: Error reading (read USER code)", modminer->proc_repr);

	if (memcmp(buf, BISTREAM_USER_ID, 4)) {
		applog(LOG_ERR, "%s: FPGA not programmed", modminer->proc_repr);
		if (!modminer_fpga_upload_bitstream(modminer))
			return false;
	} else if (opt_force_dev_init && !((struct modminer_fpga_state *)modminer->device->thr[0]->cgpu_data)->pdone) {
		applog(LOG_DEBUG, "%s: FPGA is already programmed, but --force-dev-init is set",
		       modminer->proc_repr);
		if (!modminer_fpga_upload_bitstream(modminer))
			return false;
	}
	else
		applog(LOG_DEBUG, "%s: FPGA is already programmed :)", modminer->proc_repr);
	state->pdone = 101;

	state->dclk.freqM = MODMINER_MAX_CLOCK / 2 + 1;  // Will be reduced immediately
	while (1) {
		if (state->dclk.freqM <= MODMINER_MIN_CLOCK / 2)
			bailout2(LOG_ERR, "%s: Hit minimum trying to find acceptable frequencies", modminer->proc_repr);
		--state->dclk.freqM;
		if (!modminer_change_clock(thr, false, 0))
			// MCU rejected assignment
			continue;
		if (!_modminer_get_nonce(modminer, fpgaid, &nonce))
			bailout2(LOG_ERR, "%s: Error detecting acceptable frequencies", modminer->proc_repr);
		if (!memcmp(&nonce, "\x00\xff\xff\xff", 4))
			// MCU took assignment, but disabled FPGA
			continue;
		break;
	}
	state->freqMaxMaxM =
	state->dclk.freqMaxM = state->dclk.freqM;
	if (MODMINER_DEF_CLOCK / 2 < state->dclk.freqM) {
		if (!modminer_change_clock(thr, false, -(state->dclk.freqM * 2 - MODMINER_DEF_CLOCK)))
			applog(LOG_WARNING, "%s: Failed to set desired initial frequency of %u", modminer->proc_repr, MODMINER_DEF_CLOCK);
	}
	state->dclk.freqMDefault = state->dclk.freqM;
	applog(LOG_WARNING, "%s: Frequency set to %u MHz (range: %u-%u)", modminer->proc_repr, state->dclk.freqM * 2, MODMINER_MIN_CLOCK, state->dclk.freqMaxM * 2);

	mutex_unlock(mutexp);

	thr->primary_thread = true;

	return true;
}

static
bool get_modminer_upload_percent(char *buf, size_t bufsz, struct cgpu_info *modminer, __maybe_unused bool per_processor)
{
	char pdone = ((struct modminer_fpga_state*)(modminer->device->thr[0]->cgpu_data))->pdone;
	if (pdone != 101) {
		tailsprintf(buf, bufsz, "%3d%% ", pdone);
		return true;
	}
	return false;
}

static void modminer_get_temperature(struct cgpu_info *modminer, struct thr_info *thr)
{
	struct modminer_fpga_state *state = thr->cgpu_data;

#ifdef WIN32
	/* Workaround for bug in Windows driver */
	if (!modminer_reopen(modminer))
		return;
#endif

	int fd = modminer->device->device_fd;
	int fpgaid = modminer->proc_id;
	char cmd[2] = {MODMINER_TEMP1, fpgaid};
	char temperature;

	if (2 == write(fd, cmd, 2) && read(fd, &temperature, 1) == 1)
	{
		state->temp = temperature;
		if (temperature > modminer->targettemp + opt_hysteresis) {
			{
				struct timeval now;
				cgtime(&now);
				if (timer_elapsed(&state->tv_last_cutoff_reduced, &now)) {
					state->tv_last_cutoff_reduced = now;
					int oldFreq = state->dclk.freqM;
					if (modminer_reduce_clock(thr, false))
						applog(LOG_NOTICE, "%s: Frequency %s from %u to %u MHz (temp: %d)",
						       modminer->proc_repr,
						       (oldFreq > state->dclk.freqM ? "dropped" : "raised "),
						       oldFreq * 2, state->dclk.freqM * 2,
						       temperature
						);
					state->dclk.freqMaxM = state->dclk.freqM;
				}
			}
		}
		else
		if (state->dclk.freqMaxM < state->freqMaxMaxM && temperature < modminer->targettemp) {
			if (temperature < modminer->targettemp - opt_hysteresis) {
				state->dclk.freqMaxM = state->freqMaxMaxM;
			} else {
				++state->dclk.freqMaxM;
			}
		}
	}
}

static bool modminer_get_stats(struct cgpu_info *modminer)
{
	pthread_mutex_t *mutexp = &modminer->device->device_mutex;
	int hottest = 0;
	bool get_temp = (modminer->deven != DEV_ENABLED);
	// Getting temperature more efficiently while enabled
	for (int i = modminer->threads; i--; ) {
		struct thr_info*thr = modminer->thr[i];
		struct modminer_fpga_state *state = thr->cgpu_data;
		if (get_temp)
		{
			mutex_lock(mutexp);
			modminer_get_temperature(modminer, thr);
			mutex_unlock(mutexp);
		}
		int temp = state->temp;
		if (temp > hottest)
			hottest = temp;
	}

	modminer->temp = (float)hottest;

	return true;
}

static struct api_data*
get_modminer_drv_extra_device_status(struct cgpu_info*modminer)
{
	struct api_data*root = NULL;
	struct thr_info*thr = modminer->thr[0];
	struct modminer_fpga_state *state = thr->cgpu_data;
	double d;

	d = (double)state->dclk.freqM * 2;
	root = api_add_freq(root, "Frequency", &d, true);
	d = (double)state->dclk.freqMaxM * 2;
	root = api_add_freq(root, "Cool Max Frequency", &d, true);
	d = (double)state->freqMaxMaxM * 2;
	root = api_add_freq(root, "Max Frequency", &d, true);
	root = api_add_int(root, "Hardware Errors", &state->bad_share_counter, true);
	root = api_add_int(root, "Valid Nonces", &state->good_share_counter, true);

	return root;
}

static bool
modminer_prepare_next_work(struct modminer_fpga_state*state, struct work*work)
{
	char *midstate = state->next_work_cmd + 2;
	char *taildata = midstate + 32;
	if (!(memcmp(midstate, work->midstate, 32) || memcmp(taildata, work->data + 64, 12)))
		return false;
	memcpy(midstate, work->midstate, 32);
	memcpy(taildata, work->data + 64, 12);
	return true;
}

static bool
modminer_start_work(struct thr_info*thr)
{
fd_set fds;
	struct cgpu_info*modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	pthread_mutex_t *mutexp = &modminer->device->device_mutex;
	int fd;

	char buf[1];

	mutex_lock(mutexp);
	fd = modminer->device->device_fd;

	if (unlikely(fd == -1)) {
		if (!modminer_reopen(modminer)) {
			mutex_unlock(mutexp);
			return false;
		}
		fd = modminer->device->device_fd;
	}

	if (46 != write(fd, state->next_work_cmd, 46))
		bailout2(LOG_ERR, "%s: Error writing (start work)", modminer->proc_repr);
	timer_set_now(&state->tv_workstart);
	state->hashes = 0;
	status_read("start work");
	mutex_unlock(mutexp);
	if (opt_debug) {
		char xdata[161];
		bin2hex(xdata, state->running_work.data, 80);
		applog(LOG_DEBUG, "%s: Started work: %s",
		       modminer->proc_repr, xdata);
	}

	return true;
}

#define work_restart(thr)  thr->work_restart

#define NONCE_CHARS(nonce)  \
	(int)((unsigned char*)&nonce)[3],  \
	(int)((unsigned char*)&nonce)[2],  \
	(int)((unsigned char*)&nonce)[1],  \
	(int)((unsigned char*)&nonce)[0]

static int64_t
modminer_process_results(struct thr_info*thr)
{
	struct cgpu_info*modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	char fpgaid = modminer->proc_id;
	pthread_mutex_t *mutexp = &modminer->device->device_mutex;
	struct work *work = &state->running_work;

	uint32_t nonce;
	long iter;
	int immediate_bad_nonces = 0, immediate_nonces = 0;
	bool bad;

	mutex_lock(mutexp);
	modminer_get_temperature(modminer, thr);

	iter = 200;
	while (1) {
		if (!_modminer_get_nonce(modminer, fpgaid, &nonce))
			safebailout();
		mutex_unlock(mutexp);
		if (memcmp(&nonce, "\xff\xff\xff\xff", 4)) {
			nonce = le32toh(nonce);
			bad = !test_nonce(work, nonce, false);
			++immediate_nonces;
			if (!bad)
				applog(LOG_DEBUG, "%s: Nonce for current  work: %02x%02x%02x%02x",
				       modminer->proc_repr,
				       NONCE_CHARS(nonce));
			else
			if (test_nonce(&state->last_work, nonce, false))
			{
				applog(LOG_DEBUG, "%s: Nonce for previous work: %02x%02x%02x%02x",
				       modminer->proc_repr,
				       NONCE_CHARS(nonce));
				work = &state->last_work;
				bad = false;
			}
			if (!bad)
			{
				++state->good_share_counter;
				submit_nonce(thr, work, nonce);
			}
			else {
				inc_hw_errors(thr, work, nonce);
				++state->bad_share_counter;
				++immediate_bad_nonces;
			}
		}
		if (work_restart(thr) || !--iter)
			break;
		cgsleep_ms(1);
		if (work_restart(thr))
			break;
		mutex_lock(mutexp);
	}

	struct timeval tv_workend, elapsed;
	timer_set_now(&tv_workend);
	timersub(&tv_workend, &state->tv_workstart, &elapsed);

	uint64_t hashes = (uint64_t)state->dclk.freqM * 2 * (((uint64_t)elapsed.tv_sec * 1000000) + elapsed.tv_usec);
	if (hashes > 0xffffffff)
	{
		applog(LOG_WARNING, "%s: Finished work before new one sent", modminer->proc_repr);
		hashes = 0xffffffff;
	}
	if (hashes <= state->hashes)
		hashes = 1;
	else
		hashes -= state->hashes;
	state->hashes += hashes;

	dclk_gotNonces(&state->dclk);
	if (immediate_bad_nonces)
		dclk_errorCount(&state->dclk, ((double)immediate_bad_nonces) / (double)immediate_nonces);
	dclk_preUpdate(&state->dclk);
	if (!dclk_updateFreq(&state->dclk, modminer_dclk_change_clock, thr))
		return -1;

	return hashes;
}

static int64_t
modminer_scanhash(struct thr_info*thr, struct work*work, int64_t __maybe_unused max_nonce)
{
	struct modminer_fpga_state *state = thr->cgpu_data;
	int64_t hashes = 0;
	bool startwork;

	startwork = modminer_prepare_next_work(state, work);
	if (startwork) {
		/* HACK: For some reason, this is delayed a bit
		 *       Let last_work handle the end of the work,
		 *       and start the next one immediately
		 */
	}
	else
	if (state->work_running) {
		hashes = modminer_process_results(thr);
		if (work_restart(thr)) {
			state->work_running = false;
			return hashes;
		}
	} else
		state->work_running = true;

	if (startwork) {
		__copy_work(&state->last_work, &state->running_work);
		__copy_work(&state->running_work, work);
		if (!modminer_start_work(thr))
			return -1;
	}

	// This is intentionally early
	work->blk.nonce += hashes;
	return hashes;
}

static void
modminer_fpga_shutdown(struct thr_info *thr)
{
	for (struct cgpu_info *proc = thr->cgpu->device; proc; proc = proc->next_proc)
		proc->status = LIFE_DEAD2;
	free(thr->cgpu_data);
	thr->cgpu_data = NULL;
}

static
bool modminer_user_set_clock(struct cgpu_info *cgpu, const int val)
{
	struct thr_info * const thr = cgpu->thr[0];
	struct modminer_fpga_state * const state = thr->cgpu_data;
	const int multiplier = val / 2;
	const uint8_t oldFreqM = state->dclk.freqM;
	const signed char delta = (multiplier - oldFreqM) * 2;
	state->dclk.freqMDefault = multiplier;
	const bool rv = modminer_change_clock(thr, true, delta);
	if (likely(rv))
		dclk_msg_freqchange(cgpu->proc_repr, oldFreqM * 2, state->dclk.freqM * 2, " on user request");
	return rv;
}

static char *modminer_set_device(struct cgpu_info *modminer, char *option, char *setting, char *replybuf)
{
	int val;

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "clock: range %d-%d and a multiple of 2",
					MODMINER_MIN_CLOCK, MODMINER_MAX_CLOCK);
		return replybuf;
	}

	if (strcasecmp(option, "clock") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing clock setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < MODMINER_MIN_CLOCK || val > MODMINER_MAX_CLOCK || (val & 1) != 0) {
			sprintf(replybuf, "invalid clock: '%s' valid range %d-%d and a multiple of 2",
						setting, MODMINER_MIN_CLOCK, MODMINER_MAX_CLOCK);
			return replybuf;
		}

		if (unlikely(!modminer_user_set_clock(modminer, val)))
		{
			sprintf(replybuf, "Set clock failed: %s",
			        modminer->proc_repr);
			return replybuf;
		}

		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

#ifdef HAVE_CURSES
static
void modminer_tui_wlogprint_choices(struct cgpu_info *cgpu)
{
	wlogprint("[C]lock speed ");
}

static
const char *modminer_tui_handle_choice(struct cgpu_info *cgpu, int input)
{
	static char buf[0x100];  // Static for replies
	
	switch (input)
	{
		case 'c': case 'C':
		{
			int val;
			char *intvar;
			
			sprintf(buf, "Set clock speed (range %d-%d, multiple of 2)", MODMINER_MIN_CLOCK, MODMINER_MAX_CLOCK);
			intvar = curses_input(buf);
			if (!intvar)
				return "Invalid clock speed\n";
			val = atoi(intvar);
			free(intvar);
			if (val < MODMINER_MIN_CLOCK || val > MODMINER_MAX_CLOCK || (val & 1) != 0)
				return "Invalid clock speed\n";
			
			if (unlikely(!modminer_user_set_clock(cgpu, val)))
				return "Set clock failed\n";
			return "Clock speed changed\n";
		}
	}
	return NULL;
}

static
void modminer_wlogprint_status(struct cgpu_info *cgpu)
{
	struct modminer_fpga_state *state = cgpu->thr[0]->cgpu_data;
	wlogprint("Clock speed: %d\n", (int)(state->dclk.freqM * 2));
}
#endif

struct device_drv modminer_drv = {
	.dname = "modminer",
	.name = "MMQ",
	.lowl_match = modminer_lowl_match,
	.lowl_probe = modminer_lowl_probe,
	.override_statline_temp2 = get_modminer_upload_percent,
	.get_stats = modminer_get_stats,
	.get_api_extra_device_status = get_modminer_drv_extra_device_status,
	.set_device = modminer_set_device,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = modminer_wlogprint_status,
	.proc_tui_wlogprint_choices = modminer_tui_wlogprint_choices,
	.proc_tui_handle_choice = modminer_tui_handle_choice,
#endif
	.thread_prepare = modminer_fpga_prepare,
	.thread_init = modminer_fpga_init,
	.scanhash = modminer_scanhash,
	.thread_shutdown = modminer_fpga_shutdown,
};
