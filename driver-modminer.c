/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include "fpgautils.h"
#include "logging.h"
#include "miner.h"

#define BITSTREAM_FILENAME "fpgaminer_top_fixed7_197MHz.ncd"
#define BISTREAM_USER_ID "\2\4$B"

struct device_api modminer_api;

struct modminer_fpga_state {
	bool work_running;
	struct work running_work;
	struct timeval tv_workstart;
	uint32_t hashes;

	char next_work_cmd[46];

	unsigned char clock;
	// Number of iterations since we last got a nonce
	int no_nonce_counter;
	// Number of nonces didn't meet pdiff 1, ever
	int bad_share_counter;
	// Number of nonces did meet pdiff 1, ever
	int good_share_counter;
	// Number of nonces didn't meet pdiff 1, since last clock change
	int bad_nonce_counter;
	// Number of nonces total, since last clock change
	int nonce_counter;
	// Time the clock was last reduced due to temperature
	time_t last_cutoff_reduced;

	unsigned char temp;

	unsigned char pdone;
};

static inline bool
_bailout(int fd, struct cgpu_info*modminer, int prio, const char *fmt, ...)
{
	if (fd != -1)
		serial_close(fd);
	if (modminer) {
		modminer->device_fd = -1;
		mutex_unlock(&modminer->device_mutex);
	}

	va_list ap;
	va_start(ap, fmt);
	vapplog(prio, fmt, ap);
	va_end(ap);
	return false;
}
#define bailout(...)  return _bailout(fd, NULL, __VA_ARGS__);

static bool
modminer_detect_one(const char *devpath)
{
	int fd = serial_open(devpath, 0, 10, true);
	if (unlikely(fd == -1))
		bailout(LOG_DEBUG, "ModMiner detect: failed to open %s", devpath);

	char buf[0x100];
	size_t len;

	// Sending a "ping" first, to workaround bug in new firmware betas (see issue #62)
	// Sending 45 noops, just in case the device was left in "start job" reading
	(void)(write(fd, "\0\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 46) ?:0);
	while (serial_read(fd, buf, sizeof(buf)) > 0)
		;

	if (1 != write(fd, "\x01", 1))  // Get version
		bailout(LOG_DEBUG, "ModMiner detect: write failed on %s (get version)", devpath);
	len = serial_read(fd, buf, sizeof(buf)-1);
	if (len < 1)
		bailout(LOG_DEBUG, "ModMiner detect: no response to version request from %s", devpath);
	buf[len] = '\0';
	char*devname = strdup(buf);
	applog(LOG_DEBUG, "ModMiner identified as: %s", devname);

	if (1 != write(fd, "\x02", 1))  // Get FPGA count
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
	modminer->api = &modminer_api;
	mutex_init(&modminer->device_mutex);
	modminer->device_path = strdup(devpath);
	modminer->device_fd = -1;
	modminer->deven = DEV_ENABLED;
	modminer->threads = buf[0];
	modminer->name = devname;
	modminer->cutofftemp = 85;

	return add_cgpu(modminer);
}

#undef bailout

static char
modminer_detect_auto()
{
	return
	serial_autodetect_udev     (modminer_detect_one, "*ModMiner*") ?:
	serial_autodetect_devserial(modminer_detect_one, "BTCFPGA_ModMiner") ?:
	0;
}

static void
modminer_detect()
{
	serial_detect_auto(modminer_api.dname, modminer_detect_one, modminer_detect_auto);
}

#define bailout(...)  return _bailout(-1, modminer, __VA_ARGS__);
#define bailout2(...)  return _bailout(fd, modminer, __VA_ARGS__);
#define bailout3(...)  _bailout(fd, modminer, __VA_ARGS__);

static bool
modminer_reopen(struct cgpu_info*modminer)
{
	close(modminer->device_fd);
	int fd = serial_open(modminer->device_path, 0, 10, true);
	if (unlikely(-1 == fd)) {
		applog(LOG_ERR, "%s %u: Failed to reopen %s", modminer->api->name, modminer->device_id, modminer->device_path);
		return false;
	}
	modminer->device_fd = fd;
	return true;
}
#define safebailout(...) do {  \
	bool _safebailoutrv;  \
	applog(__VA_ARGS__);  \
	state->work_running = false;  \
	_safebailoutrv = modminer_reopen(modminer);  \
	mutex_unlock(&modminer->device_mutex);  \
	return _safebailoutrv ? 0 : -1;  \
} while(0)

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

#define status_read(eng)  do {  \
FD_SET(fd, &fds);  \
select(fd+1, &fds, NULL, NULL, NULL);  \
	if (1 != read(fd, buf, 1))  \
		bailout2(LOG_ERR, "%s %u: Error programming %s (" eng ")", modminer->api->name, modminer->device_id, modminer->device_path);  \
	if (buf[0] != 1)  \
		bailout2(LOG_ERR, "%s %u: Wrong " eng " programming %s", modminer->api->name, modminer->device_id, modminer->device_path);  \
} while(0)

static bool
modminer_fpga_upload_bitstream(struct cgpu_info*modminer)
{
	struct modminer_fpga_state *state = modminer->thr[0]->cgpu_data;
fd_set fds;
	char buf[0x100];
	unsigned char *ubuf = (unsigned char*)buf;
	unsigned long len, flen;
	char *p;
	const char *fwfile = BITSTREAM_FILENAME;
	char fpgaid = 4;  // "all FPGAs"

	FILE *f = open_bitstream("modminer", fwfile);
	if (!f)
		bailout(LOG_ERR, "Error opening ModMiner firmware file %s", fwfile);
	if (1 != fread(buf, 2, 1, f))
		bailout(LOG_ERR, "Error reading ModMiner firmware (magic)");
	if (buf[0] || buf[1] != 9)
		bailout(LOG_ERR, "ModMiner firmware has wrong magic (9)");
	if (-1 == fseek(f, 11, SEEK_CUR))
		bailout(LOG_ERR, "ModMiner firmware seek failed");
	check_magic('a');
	read_str("design name");
	applog(LOG_DEBUG, "ModMiner firmware file %s info:", fwfile);
	applog(LOG_DEBUG, "  Design name: %s", buf);
	p = strrchr(buf, ';') ?: buf;
	p = strrchr(buf, '=') ?: p;
	if (p[0] == '=')
		++p;
	unsigned long fwusercode = (unsigned long)strtoll(p, &p, 16);
	if (p[0] != '\0')
		bailout(LOG_ERR, "Bad usercode in ModMiner firmware file");
	if (fwusercode == 0xffffffff)
		bailout(LOG_ERR, "ModMiner firmware doesn't support user code");
	applog(LOG_DEBUG, "  Version: %u, build %u", (fwusercode >> 8) & 0xff, fwusercode & 0xff);
	check_magic('b');
	read_str("part number");
	applog(LOG_DEBUG, "  Part number: %s", buf);
	check_magic('c');
	read_str("build date");
	applog(LOG_DEBUG, "  Build date: %s", buf);
	check_magic('d');
	read_str("build time");
	applog(LOG_DEBUG, "  Build time: %s", buf);
	check_magic('e');
	if (1 != fread(buf, 4, 1, f))
		bailout(LOG_ERR, "Error reading ModMiner firmware (data len)");
	len = ((unsigned long)ubuf[0] << 24) | ((unsigned long)ubuf[1] << 16) | (ubuf[2] << 8) | ubuf[3];
	flen = len;
	applog(LOG_DEBUG, "  Bitstream size: %lu", len);

	int fd = modminer->device_fd;

	applog(LOG_WARNING, "%s %u: Programming %s... DO NOT EXIT UNTIL COMPLETE", modminer->api->name, modminer->device_id, modminer->device_path);
	buf[0] = '\x05';  // Program Bitstream
	buf[1] = fpgaid;
	buf[2] = (len >>  0) & 0xff;
	buf[3] = (len >>  8) & 0xff;
	buf[4] = (len >> 16) & 0xff;
	buf[5] = (len >> 24) & 0xff;
	if (6 != write(fd, buf, 6))
		bailout2(LOG_ERR, "%s %u: Error programming %s (cmd)", modminer->api->name, modminer->device_id, modminer->device_path);
	status_read("cmd reply");
	ssize_t buflen;
	char nextstatus = 10;
	while (len) {
		buflen = len < 32 ? len : 32;
		if (fread(buf, buflen, 1, f) != 1)
			bailout2(LOG_ERR, "%s %u: File underrun programming %s (%d bytes left)", modminer->api->name, modminer->device_id, modminer->device_path, len);
		if (write(fd, buf, buflen) != buflen)
			bailout2(LOG_ERR, "%s %u: Error programming %s (data)", modminer->api->name, modminer->device_id,  modminer->device_path);
		state->pdone = 100 - ((len * 100) / flen);
		if (state->pdone >= nextstatus)
		{
			nextstatus += 10;
			applog(LOG_WARNING, "%s %u: Programming %s... %d%% complete...", modminer->api->name, modminer->device_id, modminer->device_path, state->pdone);
		}
		status_read("status");
		len -= buflen;
	}
	status_read("final status");
	applog(LOG_WARNING, "%s %u: Done programming %s", modminer->api->name, modminer->device_id, modminer->device_path);

	return true;
}

static bool
modminer_device_prepare(struct cgpu_info *modminer)
{
	int fd = serial_open(modminer->device_path, 0, 10, true);
	if (unlikely(-1 == fd))
		bailout(LOG_ERR, "%s %u: Failed to open %s", modminer->api->name, modminer->device_id, modminer->device_path);

	modminer->device_fd = fd;
	applog(LOG_INFO, "%s %u: Opened %s", modminer->api->name, modminer->device_id, modminer->device_path);

	struct timeval now;
	gettimeofday(&now, NULL);
	get_datestamp(modminer->init, &now);

	return true;
}

#undef bailout

static bool
modminer_fpga_prepare(struct thr_info *thr)
{
	struct cgpu_info *modminer = thr->cgpu;

	// Don't need to lock the mutex here, since prepare runs from the main thread before the miner threads start
	if (modminer->device_fd == -1 && !modminer_device_prepare(modminer))
		return false;

	struct modminer_fpga_state *state;
	state = thr->cgpu_data = calloc(1, sizeof(struct modminer_fpga_state));
	state->next_work_cmd[0] = '\x08';  // Send Job
	state->next_work_cmd[1] = thr->device_thread;  // FPGA id

	return true;
}

static bool
modminer_reduce_clock(struct thr_info*thr, bool needlock)
{
	struct cgpu_info*modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	char fpgaid = thr->device_thread;
	int fd;
	unsigned char cmd[6], buf[1];

	if (state->clock <= 100)
		return false;

	cmd[0] = '\x06';  // set clock speed
	cmd[1] = fpgaid;
	cmd[2] = state->clock -= 2;
	cmd[3] = cmd[4] = cmd[5] = '\0';

	if (needlock)
		mutex_lock(&modminer->device_mutex);
	fd = modminer->device_fd;
	if (6 != write(fd, cmd, 6))
		bailout2(LOG_ERR, "%s %u.%u: Error writing (set clock speed)", modminer->api->name, modminer->device_id, fpgaid);
	if (serial_read(fd, &buf, 1) != 1)
		bailout2(LOG_ERR, "%s %u.%u: Error reading (set clock speed)", modminer->api->name, modminer->device_id, fpgaid);
	if (needlock)
		mutex_unlock(&modminer->device_mutex);

	state->bad_nonce_counter = state->nonce_counter = 0;

	return true;
}

static bool
modminer_fpga_init(struct thr_info *thr)
{
	struct cgpu_info *modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	int fd;
	char fpgaid = thr->device_thread;

	unsigned char cmd[2], buf[4];

	mutex_lock(&modminer->device_mutex);
	fd = modminer->device_fd;
	if (fd == -1) {
		// Died in another thread...
		mutex_unlock(&modminer->device_mutex);
		return false;
	}

	cmd[0] = '\x04';  // Read USER code (bitstream id)
	cmd[1] = fpgaid;
	if (write(fd, cmd, 2) != 2)
		bailout2(LOG_ERR, "%s %u.%u: Error writing (read USER code)", modminer->api->name, modminer->device_id, fpgaid);
	if (serial_read(fd, buf, 4) != 4)
		bailout2(LOG_ERR, "%s %u.%u: Error reading (read USER code)", modminer->api->name, modminer->device_id, fpgaid);

	if (memcmp(buf, BISTREAM_USER_ID, 4)) {
		applog(LOG_ERR, "%s %u.%u: FPGA not programmed", modminer->api->name, modminer->device_id, fpgaid);
		if (!modminer_fpga_upload_bitstream(modminer))
			return false;
	}
	else
		applog(LOG_DEBUG, "%s %u.%u: FPGA is already programmed :)", modminer->api->name, modminer->device_id, fpgaid);
	state->pdone = 101;

	state->clock = 212;  // Will be reduced to 210 by modminer_reduce_clock
	modminer_reduce_clock(thr, false);
	applog(LOG_WARNING, "%s %u.%u: Setting clock speed to %u", modminer->api->name, modminer->device_id, fpgaid, state->clock);

	mutex_unlock(&modminer->device_mutex);

	thr->primary_thread = true;

	return true;
}

static void
get_modminer_statline_before(char *buf, struct cgpu_info *modminer)
{
	char info[18] = "               | ";
	int tc = modminer->threads;
	bool havetemp = false;
	int i;

	char pdone = ((struct modminer_fpga_state*)(modminer->thr[0]->cgpu_data))->pdone;
	if (pdone != 101) {
		sprintf(&info[1], "%3d%%", pdone);
		info[5] = ' ';
		strcat(buf, info);
		return;
	}

	if (tc > 4)
		tc = 4;

	for (i = tc - 1; i >= 0; --i) {
		struct thr_info*thr = modminer->thr[i];
		struct modminer_fpga_state *state = thr->cgpu_data;
		unsigned char temp = state->temp;

		info[i*3+2] = '/';
		if (temp) {
			havetemp = true;
			if (temp > 9)
				info[i*3+0] = 0x30 + (temp / 10);
			info[i*3+1] = 0x30 + (temp % 10);
		}
	}
	if (havetemp) {
		info[tc*3-1] = ' ';
		info[tc*3] = 'C';
		strcat(buf, info);
	}
	else
		strcat(buf, "               | ");
}

static struct api_data*
get_modminer_api_extra_device_status(struct cgpu_info*modminer)
{
	struct api_data*root = NULL;
	static char *k[4] = {"Board0", "Board1", "Board2", "Board3"};
	int i;

	for (i = modminer->threads - 1; i >= 0; --i) {
		struct thr_info*thr = modminer->thr[i];
		struct modminer_fpga_state *state = thr->cgpu_data;
		json_t *o = json_object();

		if (state->temp)
			json_object_set(o, "Temperature", json_integer(state->temp));
		json_object_set(o, "Frequency", json_real((double)state->clock * 1000000.));
		json_object_set(o, "Hardware Errors", json_integer(state->bad_share_counter));
		json_object_set(o, "Valid Nonces", json_integer(state->good_share_counter));

		root = api_add_json(root, k[i], o, false);
		json_decref(o);
	}

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
	char fpgaid = thr->device_thread;
	int fd;

	char buf[1];

	mutex_lock(&modminer->device_mutex);
	fd = modminer->device_fd;
	if (46 != write(fd, state->next_work_cmd, 46))
		bailout2(LOG_ERR, "%s %u.%u: Error writing (start work)", modminer->api->name, modminer->device_id, fpgaid);
	gettimeofday(&state->tv_workstart, NULL);
	state->hashes = 0;
	status_read("start work");
	mutex_unlock(&modminer->device_mutex);

	return true;
}

#define work_restart(thr)  thr->work_restart

static int64_t
modminer_process_results(struct thr_info*thr)
{
	struct cgpu_info*modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	char fpgaid = thr->device_thread;
	int fd;
	struct work *work = &state->running_work;

	char cmd[2], temperature;
	uint32_t nonce;
	long iter;
	bool bad;
	cmd[0] = '\x0a';
	cmd[1] = fpgaid;

	mutex_lock(&modminer->device_mutex);
#ifdef WIN32
	/* Workaround for bug in Windows driver */
	if (!modminer_reopen(modminer))
		return -1;
#endif
	fd = modminer->device_fd;
	if (2 == write(fd, cmd, 2) && read(fd, &temperature, 1) == 1)
	{
		state->temp = temperature;
		if (!fpgaid)
			modminer->temp = (float)temperature;
		if (temperature > modminer->cutofftemp - 2) {
			if (temperature > modminer->cutofftemp) {
				applog(LOG_WARNING, "%s %u.%u: Hit thermal cutoff limit, disabling device!", modminer->api->name, modminer->device_id, fpgaid);
				modminer->deven = DEV_RECOVER;

				modminer->device_last_not_well = time(NULL);
				modminer->device_not_well_reason = REASON_DEV_THERMAL_CUTOFF;
				++modminer->dev_thermal_cutoff_count;
			} else {
				time_t now = time(NULL);
				if (state->last_cutoff_reduced != now) {
					state->last_cutoff_reduced = now;
					modminer_reduce_clock(thr, false);
					applog(LOG_WARNING, "%s %u.%u: Drop clock speed to %u (temp: %d)", modminer->api->name, modminer->device_id, fpgaid, state->clock, temperature);
				}
			}
		}
	}

	cmd[0] = '\x09';
	iter = 200;
	while (1) {
		if (write(fd, cmd, 2) != 2)
			safebailout(LOG_ERR, "%s %u: Error writing (get nonce %u)", modminer->api->name, modminer->device_id, fpgaid);
		if (4 != serial_read(fd, &nonce, 4))
			safebailout(LOG_ERR, "%s %u: Short read (get nonce %u)", modminer->api->name, modminer->device_id, fpgaid);
		mutex_unlock(&modminer->device_mutex);
		if (memcmp(&nonce, "\xff\xff\xff\xff", 4)) {
			state->no_nonce_counter = 0;
			++state->nonce_counter;
			bad = !test_nonce(work, nonce, false);
			if (!bad)
			{
				++state->good_share_counter;
				submit_nonce(thr, work, nonce);
			}
			else
			if (unlikely((!state->good_share_counter) && nonce == 0xffffff00))
			{
				// Firmware returns 0xffffff00 immediately if we set clockspeed too high; but it's not a hw error and shouldn't affect future downclocking
				modminer_reduce_clock(thr, true);
				applog(LOG_WARNING, "%s %u.%u: Drop clock speed to %u (init)", modminer->api->name, modminer->device_id, fpgaid, state->clock);
			}
			else {
				++hw_errors;
				++modminer->hw_errors;
				++state->bad_share_counter;
				++state->bad_nonce_counter;
				if (state->bad_nonce_counter * 50 > 500 + state->nonce_counter)
				{
					// Only reduce clocks if hardware errors are more than ~2% of results
					int pchwe = state->bad_nonce_counter * 100 / state->nonce_counter;
					modminer_reduce_clock(thr, true);
					applog(LOG_WARNING, "%s %u.%u: Drop clock speed to %u (%d%% hw err)", modminer->api->name, modminer->device_id, fpgaid, state->clock, pchwe);
				}
			}
		}
		else
		if (++state->no_nonce_counter > 0x20000) {
			state->no_nonce_counter = 0;
			modminer_reduce_clock(thr, true);
			applog(LOG_WARNING, "%s %u.%u: Drop clock speed to %u (no nonces)", modminer->api->name, modminer->device_id, fpgaid, state->clock);
		}
		if (work_restart(thr) || !--iter)
			break;
		usleep(1000);
		if (work_restart(thr))
			break;
		mutex_lock(&modminer->device_mutex);
		fd = modminer->device_fd;
	}

	struct timeval tv_workend, elapsed;
	gettimeofday(&tv_workend, NULL);
	timersub(&tv_workend, &state->tv_workstart, &elapsed);

	uint64_t hashes = (uint64_t)state->clock * (((uint64_t)elapsed.tv_sec * 1000000) + elapsed.tv_usec);
	if (hashes > 0xffffffff)
		hashes = 0xffffffff;
	else
	if (hashes <= state->hashes)
		hashes = 1;
	else
		hashes -= state->hashes;
	state->hashes += hashes;
	return hashes;
}

static int64_t
modminer_scanhash(struct thr_info*thr, struct work*work, int64_t __maybe_unused max_nonce)
{
	struct modminer_fpga_state *state = thr->cgpu_data;
	int64_t hashes = 0;
	bool startwork;

	startwork = modminer_prepare_next_work(state, work);
	if (state->work_running) {
		hashes = modminer_process_results(thr);
		if (work_restart(thr)) {
			state->work_running = false;
			return hashes;
		}
	} else
		state->work_running = true;

	if (startwork) {
		if (!modminer_start_work(thr))
			return -1;
		memcpy(&state->running_work, work, sizeof(state->running_work));
	}

	// This is intentionally early
	work->blk.nonce += hashes;
	return hashes;
}

static void
modminer_fpga_shutdown(struct thr_info *thr)
{
	free(thr->cgpu_data);
}

struct device_api modminer_api = {
	.dname = "modminer",
	.name = "MMQ",
	.api_detect = modminer_detect,
	.get_statline_before = get_modminer_statline_before,
	.get_api_extra_device_status = get_modminer_api_extra_device_status,
	.thread_prepare = modminer_fpga_prepare,
	.thread_init = modminer_fpga_init,
	.scanhash = modminer_scanhash,
	.thread_shutdown = modminer_fpga_shutdown,
};
