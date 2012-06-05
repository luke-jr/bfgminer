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

	// Sending 45 noops, just in case the device was left in "start job" reading
	(void)(write(fd, "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 45) ?:0);
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
	serial_autodetect_udev     (modminer_detect_one, "BTCFPGA*ModMiner") ?:
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
fd_set fds;
	char buf[0x100];
	unsigned char *ubuf = (unsigned char*)buf;
	unsigned long len;
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
	applog(LOG_DEBUG, "  Bitstream size: %lu", len);

	int fd = modminer->device_fd;

	applog(LOG_WARNING, "%s %u: Programming %s... DO NOT EXIT CGMINER UNTIL COMPLETE", modminer->api->name, modminer->device_id, modminer->device_path);
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
	while (len) {
		buflen = len < 32 ? len : 32;
		if (fread(buf, buflen, 1, f) != 1)
			bailout2(LOG_ERR, "%s %u: File underrun programming %s (%d bytes left)", modminer->api->name, modminer->device_id, modminer->device_path, len);
		if (write(fd, buf, buflen) != buflen)
			bailout2(LOG_ERR, "%s %u: Error programming %s (data)", modminer->api->name, modminer->device_id,  modminer->device_path);
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
	int fd = serial_open(modminer->device_path, 0, /*FIXME=-1*/3000, true);
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
	int fd = modminer->device_fd;
	unsigned char cmd[6], buf[1];

	if (state->clock <= 100)
		return false;

	cmd[0] = '\x06';  // set clock speed
	cmd[1] = fpgaid;
	cmd[2] = state->clock -= 2;
	cmd[3] = cmd[4] = cmd[5] = '\0';

	if (needlock)
		mutex_lock(&modminer->device_mutex);
	if (6 != write(fd, cmd, 6))
		bailout2(LOG_ERR, "%s %u.%u: Error writing (set clock speed)", modminer->api->name, modminer->device_id, fpgaid);
	if (serial_read(fd, &buf, 1) != 1)
		bailout2(LOG_ERR, "%s %u.%u: Error reading (set clock speed)", modminer->api->name, modminer->device_id, fpgaid);
	if (needlock)
		mutex_unlock(&modminer->device_mutex);

	applog(LOG_WARNING, "%s %u.%u: Setting clock speed to %u", modminer->api->name, modminer->device_id, fpgaid, state->clock);

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

	state->clock = 212;  // Will be reduced to 210 by modminer_reduce_clock
	modminer_reduce_clock(thr, false);

	mutex_unlock(&modminer->device_mutex);

	return true;
}

static void
get_modminer_statline_before(char *buf, struct cgpu_info *modminer)
{
	char info[18] = "               | ";
	int tc = modminer->threads;
	bool havetemp = false;
	int i;

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
	int fd = modminer->device_fd;

	char buf[1];

	mutex_lock(&modminer->device_mutex);
	if (46 != write(fd, state->next_work_cmd, 46))
		bailout2(LOG_ERR, "%s %u.%u: Error writing (start work)", modminer->api->name, modminer->device_id, fpgaid);
	gettimeofday(&state->tv_workstart, NULL);
	state->hashes = 0;
	status_read("start work");
	mutex_unlock(&modminer->device_mutex);

	return true;
}

#define work_restart(thr)  work_restart[thr->id].restart

static uint64_t
modminer_process_results(struct thr_info*thr)
{
	struct cgpu_info*modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	char fpgaid = thr->device_thread;
	int fd = modminer->device_fd;
	struct work *work = &state->running_work;

	char cmd[2], temperature;
	uint32_t nonce;
	long iter;
	bool bad;
	cmd[0] = '\x0a';
	cmd[1] = fpgaid;

	mutex_lock(&modminer->device_mutex);
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
				}
			}
		}
	}

	cmd[0] = '\x09';
	iter = 200;
	while (1) {
		if (write(fd, cmd, 2) != 2)
			bailout2(LOG_ERR, "%s %u.%u: Error reading (get nonce)", modminer->api->name, modminer->device_id, fpgaid);
		serial_read(fd, &nonce, 4);
		mutex_unlock(&modminer->device_mutex);
		if (memcmp(&nonce, "\xff\xff\xff\xff", 4)) {
			state->no_nonce_counter = 0;
			bad = !test_nonce(work, nonce);
			if (!bad)
				submit_nonce(thr, work, nonce);
			else {
				++hw_errors;
				if (++modminer->hw_errors * 100 > 1000 + state->good_share_counter)
					// Only reduce clocks if hardware errors are more than ~1% of results
					modminer_reduce_clock(thr, true);
			}
		}
		else
		if (++state->no_nonce_counter > 18000) {
			state->no_nonce_counter = 0;
			modminer_reduce_clock(thr, true);
		}
		if (work_restart(thr))
			break;
		usleep(10000);
		if (work_restart(thr) || !--iter)
			break;
		mutex_lock(&modminer->device_mutex);
	}

	struct timeval tv_workend, elapsed;
	gettimeofday(&tv_workend, NULL);
	timeval_subtract(&elapsed, &tv_workend, &state->tv_workstart);

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

static uint64_t
modminer_scanhash(struct thr_info*thr, struct work*work, uint64_t __maybe_unused max_nonce)
{
	struct modminer_fpga_state *state = thr->cgpu_data;
	uint64_t hashes = 1;
	bool startwork;

	startwork = modminer_prepare_next_work(state, work);
	if (state->work_running) {
		hashes = modminer_process_results(thr);
		if (work_restart(thr)) {
			state->work_running = false;
			return 1;
		}
	}
	else
		state->work_running = true;

	if (startwork) {
		if (!modminer_start_work(thr))
			return 0;
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
	.thread_prepare = modminer_fpga_prepare,
	.thread_init = modminer_fpga_init,
	.scanhash = modminer_scanhash,
	.thread_shutdown = modminer_fpga_shutdown,
};
