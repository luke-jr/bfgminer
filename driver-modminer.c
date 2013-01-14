/*
 * Copyright 2012-2013 Andrew Smith
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
#include <math.h>

#include "logging.h"
#include "miner.h"
#include "usbutils.h"
#include "fpgautils.h"
#include "util.h"

#define BITSTREAM_FILENAME "fpgaminer_top_fixed7_197MHz.ncd"
#define BISTREAM_USER_ID "\2\4$B"

#define BITSTREAM_MAGIC_0 0
#define BITSTREAM_MAGIC_1 9

#define MODMINER_CUTOFF_TEMP 60.0
#define MODMINER_OVERHEAT_TEMP 50.0
#define MODMINER_RECOVER_TEMP 46.5
#define MODMINER_TEMP_UP_LIMIT 47.0

#define MODMINER_HW_ERROR_PERCENT 0.75

// How many seconds of no nonces means there's something wrong
// First time - drop the clock and see if it revives
// Second time - (and it didn't revive) disable it
#define ITS_DEAD_JIM 300

// N.B. in the latest firmware the limit is 250
// however the voltage/temperature risks preclude that
#define MODMINER_MAX_CLOCK 230
#define MODMINER_DEF_CLOCK 200
#define MODMINER_MIN_CLOCK 160

#define MODMINER_CLOCK_UP 2
#define MODMINER_CLOCK_SET 0
#define MODMINER_CLOCK_DOWN -2
// = 0 means OVERHEAT doesn't affect the clock
#define MODMINER_CLOCK_OVERHEAT 0
#define MODMINER_CLOCK_DEAD -6
#define MODMINER_CLOCK_CUTOFF -10

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
// Two byte temperature reply
#define MODMINER_TEMP2 '\x0d'

// +6 bytes
#define MODMINER_SET_REG '\x0b'
// +2 bytes
#define MODMINER_GET_REG '\x0c'

#define FPGAID_ALL 4

// Maximum how many good shares in a row means clock up
// 96 is ~34m22s at 200MH/s
#define MODMINER_TRY_UP 96
// Initially how many good shares in a row means clock up
// This is doubled each down clock until it reaches MODMINER_TRY_UP
// 6 is ~2m9s at 200MH/s
#define MODMINER_EARLY_UP 6
// Limit when reducing shares_to_good
#define MODMINER_MIN_BACK 12

struct device_drv modminer_drv;

// 45 noops sent when detecting, in case the device was left in "start job" reading
static const char NOOP[] = MODMINER_PING "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";

static void do_ping(struct cgpu_info *modminer)
{
	char buf[0x100+1];
	int err, amount;

	// Don't care if it fails
	err = usb_write(modminer, (char *)NOOP, sizeof(NOOP)-1, &amount, C_PING);
	applog(LOG_DEBUG, "%s%u: flush noop got %d err %d",
		modminer->drv->name, modminer->fpgaid, amount, err);

	// Clear any outstanding data
	while ((err = usb_read(modminer, buf, sizeof(buf)-1, &amount, C_CLEAR)) == 0 && amount > 0)
		applog(LOG_DEBUG, "%s%u: clear got %d",
			modminer->drv->name, modminer->fpgaid, amount);

	applog(LOG_DEBUG, "%s%u: final clear got %d err %d",
		modminer->drv->name, modminer->fpgaid, amount, err);
}

static bool modminer_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	char buf[0x100+1];
	char *devname = NULL;
	char devpath[20];
	int err, i, amount;
	bool added = false;

	struct cgpu_info *modminer = NULL;
	modminer = calloc(1, sizeof(*modminer));
	modminer->drv = &modminer_drv;
	modminer->modminer_mutex = calloc(1, sizeof(*(modminer->modminer_mutex)));
	mutex_init(modminer->modminer_mutex);
	modminer->fpgaid = (char)0;

	if (!usb_init(modminer, dev, found))
		goto shin;

	do_ping(modminer);

	if ((err = usb_write(modminer, MODMINER_GET_VERSION, 1, &amount, C_REQUESTVERSION)) < 0 || amount != 1) {
		applog(LOG_ERR, "ModMiner detect: send version request failed (%d:%d)", amount, err);
		goto unshin;
	}

	if ((err = usb_read(modminer, buf, sizeof(buf)-1, &amount, C_GETVERSION)) < 0 || amount < 1) {
		if (err < 0)
			applog(LOG_ERR, "ModMiner detect: no version reply (%d)", err);
		else
			applog(LOG_ERR, "ModMiner detect: empty version reply (%d)", amount);

		applog(LOG_DEBUG, "ModMiner detect: check the firmware");

		goto unshin;
	}
	buf[amount] = '\0';
	devname = strdup(buf);
	applog(LOG_DEBUG, "ModMiner identified as: %s", devname);

	if ((err = usb_write(modminer, MODMINER_FPGA_COUNT, 1, &amount, C_REQUESTFPGACOUNT) < 0 || amount != 1)) {
		applog(LOG_ERR, "ModMiner detect: FPGA count request failed (%d:%d)", amount, err);
		goto unshin;
	}

	if ((err = usb_read(modminer, buf, 1, &amount, C_GETFPGACOUNT)) < 0 || amount != 1) {
		applog(LOG_ERR, "ModMiner detect: no FPGA count reply (%d:%d)", amount, err);
		goto unshin;
	}

	// TODO: flag it use 1 byte temp if it is an old firmware
	// can detect with modminer->cgusb->serial ?

	if (buf[0] == 0) {
		applog(LOG_ERR, "ModMiner detect: zero FPGA count from %s", devname);
		goto unshin;
	}

	if (buf[0] < 1 || buf[0] > 4) {
		applog(LOG_ERR, "ModMiner detect: invalid FPGA count (%u) from %s", buf[0], devname);
		goto unshin;
	}

	applog(LOG_DEBUG, "ModMiner %s has %u FPGAs", devname, buf[0]);

	modminer->name = devname;

	// TODO: test with 1 board missing in the middle and each end
	// to see how that affects the sequence numbers
	for (i = 0; i < buf[0]; i++) {
		struct cgpu_info *tmp = calloc(1, sizeof(*tmp));

		tmp->drv = copy_drv(modminer->drv);
		tmp->name = devname;

		sprintf(devpath, "%d:%d:%d",
			(int)(modminer->usbdev->bus_number),
			(int)(modminer->usbdev->device_address),
			i);

		tmp->device_path = strdup(devpath);
		tmp->usbdev = modminer->usbdev;
		// Only the first copy gets the already used stats
		if (!added)
			tmp->usbstat = modminer->usbstat;
		tmp->fpgaid = (char)i;
		tmp->modminer_mutex = modminer->modminer_mutex;
		tmp->deven = DEV_ENABLED;
		tmp->threads = 1;

		if (!add_cgpu(tmp)) {
			free(tmp->device_path);
			if (tmp->drv->copy)
				free(tmp->drv);
			free(tmp);
			goto unshin;
		}

		update_usb_stats(tmp);

		added = true;
	}

	if (modminer->drv->copy)
		free(modminer->drv);

	free(modminer);

	return true;

unshin:
	if (!added)
		usb_uninit(modminer);

shin:
	if (!added)
		free(modminer->modminer_mutex);

	if (modminer->drv->copy)
		free(modminer->drv);

	free(modminer);

	if (added)
		return true;
	else
		return false;
}

static void modminer_detect()
{
	usb_detect(&modminer_drv, modminer_detect_one);
}

static bool get_expect(struct cgpu_info *modminer, FILE *f, char c)
{
	char buf;

	if (fread(&buf, 1, 1, f) != 1) {
		applog(LOG_ERR, "%s%u: Error (%d) reading bitstream (%c)",
				modminer->drv->name, modminer->device_id, errno, c);
		return false;
	}

	if (buf != c) {
		applog(LOG_ERR, "%s%u: firmware code mismatch (%c)",
				modminer->drv->name, modminer->device_id, c);
		return false;
	}

	return true;
}

static bool get_info(struct cgpu_info *modminer, FILE *f, char *buf, int bufsiz, const char *name)
{
	unsigned char siz[2];
	int len;

	if (fread(siz, 2, 1, f) != 1) {
		applog(LOG_ERR, "%s%u: Error (%d) reading bitstream '%s' len",
			modminer->drv->name, modminer->device_id, errno, name);
		return false;
	}

	len = siz[0] * 256 + siz[1];

	if (len >= bufsiz) {
		applog(LOG_ERR, "%s%u: Bitstream '%s' len too large (%d)",
			modminer->drv->name, modminer->device_id, name, len);
		return false;
	}

	if (fread(buf, len, 1, f) != 1) {
		applog(LOG_ERR, "%s%u: Error (%d) reading bitstream '%s'", errno,
			modminer->drv->name, modminer->device_id, errno, name);
		return false;
	}

	buf[len] = '\0';

	return true;
}

#define USE_DEFAULT_TIMEOUT 0

// mutex must always be locked before calling
static bool get_status_timeout(struct cgpu_info *modminer, char *msg, unsigned int timeout, enum usb_cmds cmd)
{
	int err, amount;
	char buf[1];

	if (timeout == USE_DEFAULT_TIMEOUT)
		err = usb_read(modminer, buf, 1, &amount, cmd);
	else
		err = usb_read_timeout(modminer, buf, 1, &amount, timeout, cmd);

	if (err < 0 || amount != 1) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error (%d:%d) getting %s reply",
			modminer->drv->name, modminer->device_id, amount, err, msg);

		return false;
	}

	if (buf[0] != 1) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error, invalid %s reply (was %d should be 1)",
			modminer->drv->name, modminer->device_id, msg, buf[0]);

		return false;
	}

	return true;
}

// mutex must always be locked before calling
static bool get_status(struct cgpu_info *modminer, char *msg, enum usb_cmds cmd)
{
	return get_status_timeout(modminer, msg, USE_DEFAULT_TIMEOUT, cmd);
}

static bool modminer_fpga_upload_bitstream(struct cgpu_info *modminer)
{
	const char *bsfile = BITSTREAM_FILENAME;
	char buf[0x100], *p;
	char devmsg[64];
	unsigned char *ubuf = (unsigned char *)buf;
	unsigned long totlen, len;
	size_t buflen, remaining;
	float nextmsg, upto;
	char fpgaid = FPGAID_ALL;
	int err, amount, tries;
	char *ptr;

	FILE *f = open_bitstream("modminer", bsfile);
	if (!f) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error (%d) opening bitstream file %s",
			modminer->drv->name, modminer->device_id, errno, bsfile);

		return false;
	}

	if (fread(buf, 2, 1, f) != 1) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error (%d) reading bitstream magic",
			modminer->drv->name, modminer->device_id, errno);

		goto dame;
	}

	if (buf[0] != BITSTREAM_MAGIC_0 || buf[1] != BITSTREAM_MAGIC_1) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: bitstream has incorrect magic (%u,%u) instead of (%u,%u)",
			modminer->drv->name, modminer->device_id,
			buf[0], buf[1],
			BITSTREAM_MAGIC_0, BITSTREAM_MAGIC_1);

		goto dame;
	}

	if (fseek(f, 11L, SEEK_CUR)) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error (%d) bitstream seek failed",
			modminer->drv->name, modminer->device_id, errno);

		goto dame;
	}

	if (!get_expect(modminer, f, 'a'))
		goto undame;

	if (!get_info(modminer, f, buf, sizeof(buf), "Design name"))
		goto undame;

	applog(LOG_DEBUG, "%s%u: bitstream file '%s' info:",
		modminer->drv->name, modminer->device_id, bsfile);

	applog(LOG_DEBUG, " Design name: '%s'", buf);

	p = strrchr(buf, ';') ? : buf;
	p = strrchr(buf, '=') ? : p;
	if (p[0] == '=')
		p++;

	unsigned long fwusercode = (unsigned long)strtoll(p, &p, 16);

	if (p[0] != '\0') {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Bad usercode in bitstream file",
			modminer->drv->name, modminer->device_id);

		goto dame;
	}

	if (fwusercode == 0xffffffff) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: bitstream doesn't support user code",
			modminer->drv->name, modminer->device_id);

		goto dame;
	}

	applog(LOG_DEBUG, " Version: %u, build %u", (fwusercode >> 8) & 0xff, fwusercode & 0xff);

	if (!get_expect(modminer, f, 'b'))
		goto undame;

	if (!get_info(modminer, f, buf, sizeof(buf), "Part number"))
		goto undame;

	applog(LOG_DEBUG, " Part number: '%s'", buf);

	if (!get_expect(modminer, f, 'c'))
		goto undame;

	if (!get_info(modminer, f, buf, sizeof(buf), "Build date"))
		goto undame;

	applog(LOG_DEBUG, " Build date: '%s'", buf);

	if (!get_expect(modminer, f, 'd'))
		goto undame;

	if (!get_info(modminer, f, buf, sizeof(buf), "Build time"))
		goto undame;

	applog(LOG_DEBUG, " Build time: '%s'", buf);

	if (!get_expect(modminer, f, 'e'))
		goto undame;

	if (fread(buf, 4, 1, f) != 1) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error (%d) reading bitstream data len",
			modminer->drv->name, modminer->device_id, errno);

		goto dame;
	}

	len = ((unsigned long)ubuf[0] << 24) | ((unsigned long)ubuf[1] << 16) | (ubuf[2] << 8) | ubuf[3];
	applog(LOG_DEBUG, " Bitstream size: %lu", len);

	strcpy(devmsg, modminer->device_path);
	ptr = strrchr(devmsg, ':');
	if (ptr)
		*ptr = '\0';

	applog(LOG_WARNING, "%s%u: Programming all FPGA on %s ... Mining will not start until complete",
		modminer->drv->name, modminer->device_id, devmsg);

	buf[0] = MODMINER_PROGRAM;
	buf[1] = fpgaid;
	buf[2] = (len >>  0) & 0xff;
	buf[3] = (len >>  8) & 0xff;
	buf[4] = (len >> 16) & 0xff;
	buf[5] = (len >> 24) & 0xff;

	if ((err = usb_write(modminer, buf, 6, &amount, C_STARTPROGRAM)) < 0 || amount != 6) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Program init failed (%d:%d)",
			modminer->drv->name, modminer->device_id, amount, err);

		goto dame;
	}

	if (!get_status(modminer, "initialise", C_STARTPROGRAMSTATUS))
		goto undame;

// It must be 32 bytes according to MCU legacy.c
#define WRITE_SIZE 32

	totlen = len;
	nextmsg = 0.1;
	while (len > 0) {
		buflen = len < WRITE_SIZE ? len : WRITE_SIZE;
		if (fread(buf, buflen, 1, f) != 1) {
			mutex_unlock(modminer->modminer_mutex);

			applog(LOG_ERR, "%s%u: bitstream file read error %d (%d bytes left)",
				modminer->drv->name, modminer->device_id, errno, len);

			goto dame;
		}

		tries = 0;
		ptr = buf;
		remaining = buflen;
		while ((err = usb_write(modminer, ptr, remaining, &amount, C_PROGRAM)) < 0 || amount != (int)remaining) {
			if (err == LIBUSB_ERROR_TIMEOUT && amount > 0 && ++tries < 4) {
				remaining -= amount;
				ptr += amount;

				if (opt_debug)
					applog(LOG_DEBUG, "%s%u: Program timeout (%d:%d) sent %d tries %d",
						modminer->drv->name, modminer->device_id,
						amount, err, remaining, tries);

				if (!get_status(modminer, "write status", C_PROGRAMSTATUS2))
					goto dame;

			} else {
				mutex_unlock(modminer->modminer_mutex);

				applog(LOG_ERR, "%s%u: Program failed (%d:%d) sent %d",
					modminer->drv->name, modminer->device_id, amount, err, remaining);

				goto dame;
			}
		}

		if (!get_status(modminer, "write status", C_PROGRAMSTATUS))
			goto dame;

		len -= buflen;

		upto = (float)(totlen - len) / (float)(totlen);
		if (upto >= nextmsg) {
			applog(LOG_WARNING,
				"%s%u: Programming %.1f%% (%d out of %d)",
				modminer->drv->name, modminer->device_id, upto*100, (totlen - len), totlen);

			nextmsg += 0.1;
		}
	}

	if (!get_status(modminer, "final status", C_FINALPROGRAMSTATUS))
		goto undame;

	applog(LOG_WARNING, "%s%u: Programming completed for all FPGA on %s",
		modminer->drv->name, modminer->device_id, devmsg);

	// Give it a 2/3s delay after programming
	nmsleep(666);

	return true;
undame:
	;
	mutex_unlock(modminer->modminer_mutex);
	;
dame:
	fclose(f);
	return false;
}

static bool modminer_fpga_prepare(struct thr_info *thr)
{
	struct cgpu_info *modminer = thr->cgpu;
	struct timeval now;

	gettimeofday(&now, NULL);
	get_datestamp(modminer->init, &now);

	struct modminer_fpga_state *state;
	state = thr->cgpu_data = calloc(1, sizeof(struct modminer_fpga_state));
	state->next_work_cmd[0] = MODMINER_SEND_WORK;
	state->next_work_cmd[1] = modminer->fpgaid;
	state->shares_to_good = MODMINER_EARLY_UP;
	state->overheated = false;

	return true;
}

/*
 * Clocking rules:
 *	If device exceeds cutoff or overheat temp - stop sending work until it cools
 *		decrease the clock by MODMINER_CLOCK_CUTOFF/MODMINER_CLOCK_OVERHEAT
 *		for when it restarts
 *		with MODMINER_CLOCK_OVERHEAT=0 basically says that temp shouldn't
 *		affect the clock unless we reach CUTOFF
 *
 *	If device overheats
 *		set shares_to_good back to MODMINER_MIN_BACK
 *		to speed up clock recovery if temp drop doesnt help
 *
 * When to clock down:
 *	If device gets MODMINER_HW_ERROR_PERCENT errors since last clock up or down
 *		if clock is <= default it requires 2 HW to do this test
 *		if clock is > default it only requires 1 HW to do this test
 *			also double shares_to_good
 *
 * When to clock up:
 *	If device gets shares_to_good good shares in a row
 *		and temp < MODMINER_TEMP_UP_LIMIT
 *
 * N.B. clock must always be a multiple of 2
 */
static const char *clocknodev = "clock failed - no device";
static const char *clockoldwork = "clock already changed for this work";
static const char *clocktoolow = "clock too low";
static const char *clocktoohi = "clock too high";
static const char *clocksetfail = "clock set command failed";
static const char *clockreplyfail = "clock reply failed";

static const char *modminer_delta_clock(struct thr_info *thr, int delta, bool temp, bool force)
{
	struct cgpu_info *modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	unsigned char cmd[6], buf[1];
	int err, amount;

	// Device is gone
	if (modminer->nodev)
		return clocknodev;

	// Only do once if multiple shares per work or multiple reasons
	if (!state->new_work && !force)
		return clockoldwork;

	state->new_work = false;

	state->shares = 0;
	state->shares_last_hw = 0;
	state->hw_errors = 0;

	// FYI clock drop has little effect on temp
	if (delta < 0 && (modminer->clock + delta) < MODMINER_MIN_CLOCK)
		return clocktoolow;

	if (delta > 0 && (modminer->clock + delta) > MODMINER_MAX_CLOCK)
		return clocktoohi;

	if (delta < 0) {
		if (temp)
			state->shares_to_good = MODMINER_MIN_BACK;
		else {
			if ((state->shares_to_good * 2) < MODMINER_TRY_UP)
				state->shares_to_good *= 2;
			else
				state->shares_to_good = MODMINER_TRY_UP;
		}
	}

	modminer->clock += delta;

	cmd[0] = MODMINER_SET_CLOCK;
	cmd[1] = modminer->fpgaid;
	cmd[2] = modminer->clock;
	cmd[3] = cmd[4] = cmd[5] = '\0';

	mutex_lock(modminer->modminer_mutex);

	if ((err = usb_write(modminer, (char *)cmd, 6, &amount, C_SETCLOCK)) < 0 || amount != 6) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error writing set clock speed (%d:%d)",
			modminer->drv->name, modminer->device_id, amount, err);

		return clocksetfail;
	}

	if ((err = usb_read(modminer, (char *)(&buf), 1, &amount, C_REPLYSETCLOCK)) < 0 || amount != 1) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error reading set clock speed (%d:%d)",
			modminer->drv->name, modminer->device_id, amount, err);

		return clockreplyfail;
	}

	mutex_unlock(modminer->modminer_mutex);

	applog(LOG_WARNING, "%s%u: Set clock speed %sto %u",
			modminer->drv->name, modminer->device_id,
			(delta < 0) ? "down " : (delta > 0 ? "up " : ""),
			modminer->clock);

	return NULL;
}

static bool modminer_fpga_init(struct thr_info *thr)
{
	struct cgpu_info *modminer = thr->cgpu;
	unsigned char cmd[2], buf[4];
	int err, amount;

	mutex_lock(modminer->modminer_mutex);

	cmd[0] = MODMINER_GET_USERCODE;
	cmd[1] = modminer->fpgaid;
	if ((err = usb_write(modminer, (char *)cmd, 2, &amount, C_REQUESTUSERCODE)) < 0 || amount != 2) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error requesting USER code (%d:%d)",
			modminer->drv->name, modminer->device_id, amount, err);

		return false;
	}

	if ((err = usb_read(modminer, (char *)buf, 4, &amount, C_GETUSERCODE)) < 0 || amount != 4) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Error reading USER code (%d:%d)",
			modminer->drv->name, modminer->device_id, amount, err);

		return false;
	}

	if (memcmp(buf, BISTREAM_USER_ID, 4)) {
		applog(LOG_ERR, "%s%u: FPGA not programmed",
			modminer->drv->name, modminer->device_id);

		if (!modminer_fpga_upload_bitstream(modminer))
			return false;

		mutex_unlock(modminer->modminer_mutex);
	} else {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_DEBUG, "%s%u: FPGA is already programmed :)",
			modminer->drv->name, modminer->device_id);
	}

	modminer->clock = MODMINER_DEF_CLOCK;
	modminer_delta_clock(thr, MODMINER_CLOCK_SET, false, false);

	thr->primary_thread = true;

	return true;
}

static void get_modminer_statline_before(char *buf, struct cgpu_info *modminer)
{
	char info[64];

	sprintf(info, " %s%.1fC %3uMHz  | ",
			(modminer->temp < 10) ? " " : "",
			modminer->temp,
			(unsigned int)(modminer->clock));

	strcat(buf, info);
}

static bool modminer_prepare_next_work(struct modminer_fpga_state *state, struct work *work)
{
	char *midstate = state->next_work_cmd + 2;
	char *taildata = midstate + 32;
	if (!(memcmp(midstate, work->midstate, 32) || memcmp(taildata, work->data + 64, 12)))
		return false;
	memcpy(midstate, work->midstate, 32);
	memcpy(taildata, work->data + 64, 12);
	return true;
}

static bool modminer_start_work(struct thr_info *thr)
{
	struct cgpu_info *modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	int err, amount;
	bool sta;

	if (state->first_work.tv_sec == 0)
		gettimeofday(&state->first_work, NULL);

	if (state->last_nonce.tv_sec == 0)
		gettimeofday(&state->last_nonce, NULL);

	mutex_lock(modminer->modminer_mutex);

	if ((err = usb_write(modminer, (char *)(state->next_work_cmd), 46, &amount, C_SENDWORK)) < 0 || amount != 46) {
		mutex_unlock(modminer->modminer_mutex);

		applog(LOG_ERR, "%s%u: Start work failed (%d:%d)",
			modminer->drv->name, modminer->device_id, amount, err);

		return false;
	}

	gettimeofday(&state->tv_workstart, NULL);
	state->hashes = 0;

	sta = get_status(modminer, "start work", C_SENDWORKSTATUS);

	if (sta) {
		mutex_unlock(modminer->modminer_mutex);
		state->new_work = true;
	}

	return sta;
}

static void check_temperature(struct thr_info *thr)
{
	struct cgpu_info *modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	char cmd[2], temperature[2];
	int tbytes, tamount;
	int amount;

	// Device is gone
	if (modminer->nodev)
		return;

	if (state->one_byte_temp) {
		cmd[0] = MODMINER_TEMP1;
		tbytes = 1;
	} else {
		cmd[0] = MODMINER_TEMP2;
		tbytes = 2;
	}

	cmd[1] = modminer->fpgaid;

	mutex_lock(modminer->modminer_mutex);
	if (usb_write(modminer, (char *)cmd, 2, &amount, C_REQUESTTEMPERATURE) == 0 && amount == 2 &&
	    usb_read(modminer, (char *)(&temperature), tbytes, &tamount, C_GETTEMPERATURE) == 0 && tamount == tbytes) {
		mutex_unlock(modminer->modminer_mutex);
		if (state->one_byte_temp)
			modminer->temp = temperature[0];
		else {
			// Only accurate to 2 and a bit places
			modminer->temp = roundf((temperature[1] * 256.0 + temperature[0]) / 0.128) / 1000.0;

			state->tried_two_byte_temp = true;
		}

		if (state->overheated) {
			// Limit recovery to lower than OVERHEAT so it doesn't just go straight over again
			if (modminer->temp < MODMINER_RECOVER_TEMP) {
				state->overheated = false;
				applog(LOG_WARNING, "%s%u: Recovered, temp less than (%.1f) now %.3f",
					modminer->drv->name, modminer->device_id,
					MODMINER_RECOVER_TEMP, modminer->temp);
			}
		}
		else if (modminer->temp >= MODMINER_OVERHEAT_TEMP) {
			if (modminer->temp >= MODMINER_CUTOFF_TEMP) {
				applog(LOG_WARNING, "%s%u: Hit thermal cutoff limit! (%.1f) at %.3f",
					modminer->drv->name, modminer->device_id,
					MODMINER_CUTOFF_TEMP, modminer->temp);

				modminer_delta_clock(thr, MODMINER_CLOCK_CUTOFF, true, false);
				state->overheated = true;
				dev_error(modminer, REASON_DEV_THERMAL_CUTOFF);
			} else {
				applog(LOG_WARNING, "%s%u: Overheat limit (%.1f) reached %.3f",
					modminer->drv->name, modminer->device_id,
					MODMINER_OVERHEAT_TEMP, modminer->temp);

				// If it's defined to be 0 then don't call modminer_delta_clock()
				if (MODMINER_CLOCK_OVERHEAT != 0)
					modminer_delta_clock(thr, MODMINER_CLOCK_OVERHEAT, true, false);
				state->overheated = true;
				dev_error(modminer, REASON_DEV_OVER_HEAT);
			}
		}
	} else {
		mutex_unlock(modminer->modminer_mutex);

		if (!state->tried_two_byte_temp) {
			state->tried_two_byte_temp = true;
			state->one_byte_temp = true;
		}
	}
}

#define work_restart(thr)  thr->work_restart

// 250Mhz is 17.17s - ensure we don't go idle
static const double processtime = 17.0;
// 160Mhz is 26.84 - when overheated ensure we don't throw away shares
static const double overheattime = 26.9;

static uint64_t modminer_process_results(struct thr_info *thr)
{
	struct cgpu_info *modminer = thr->cgpu;
	struct modminer_fpga_state *state = thr->cgpu_data;
	struct work *work = &state->running_work;
	struct timeval now;
	char cmd[2];
	uint32_t nonce;
	uint32_t curr_hw_errors;
	int err, amount, amount2;
	int timeoutloop;
	double timeout;
	int temploop;

	// Device is gone
	if (modminer->nodev)
		return -1;

	// If we are overheated it will just keep checking for results
	// since we can't stop the work
	// The next work will not start until the temp drops
	check_temperature(thr);

	cmd[0] = MODMINER_CHECK_WORK;
	cmd[1] = modminer->fpgaid;

	timeoutloop = 0;
	temploop = 0;
	while (1) {
		mutex_lock(modminer->modminer_mutex);
		if ((err = usb_write(modminer, cmd, 2, &amount, C_REQUESTWORKSTATUS)) < 0 || amount != 2) {
			mutex_unlock(modminer->modminer_mutex);

			// timeoutloop never resets so the timeouts can't
			// accumulate much during a single item of work
			if (err == LIBUSB_ERROR_TIMEOUT && ++timeoutloop < 10) {
				state->timeout_fail++;
				goto tryagain;
			}

			applog(LOG_ERR, "%s%u: Error sending (get nonce) (%d:%d)",
				modminer->drv->name, modminer->device_id, amount, err);

			return -1;
		}

		err = usb_read(modminer, (char *)(&nonce), 4, &amount, C_GETWORKSTATUS);
		while (err == LIBUSB_SUCCESS && amount < 4) {
			size_t remain = 4 - amount;
			char *pos = ((char *)(&nonce)) + amount;

			state->success_more++;

			err = usb_read(modminer, pos, remain, &amount2, C_GETWORKSTATUS);

			amount += amount2;
		}
		mutex_unlock(modminer->modminer_mutex);

		if (err < 0 || amount < 4) {
			// timeoutloop never resets so the timeouts can't
			// accumulate much during a single item of work
			if (err == LIBUSB_ERROR_TIMEOUT && ++timeoutloop < 10) {
				state->timeout_fail++;
				goto tryagain;
			}

			applog(LOG_ERR, "%s%u: Error reading (get nonce) (%d:%d)",
				modminer->drv->name, modminer->device_id, amount+amount2, err);
		}

		if (memcmp(&nonce, "\xff\xff\xff\xff", 4)) {
			// found 'something' ...
			state->shares++;
			curr_hw_errors = state->hw_errors;
			submit_nonce(thr, work, nonce);
			if (state->hw_errors > curr_hw_errors) {
				gettimeofday(&now, NULL);
				// Ignore initial errors that often happen
				if (tdiff(&now, &state->first_work) < 2.0) {
					state->shares = 0;
					state->shares_last_hw = 0;
					state->hw_errors = 0;
				} else {
					state->shares_last_hw = state->shares;
					if (modminer->clock > MODMINER_DEF_CLOCK || state->hw_errors > 1) {
						float pct = (state->hw_errors * 100.0 / (state->shares ? : 1.0));
						if (pct >= MODMINER_HW_ERROR_PERCENT)
							modminer_delta_clock(thr, MODMINER_CLOCK_DOWN, false, false);
					}
				}
			} else {
				gettimeofday(&state->last_nonce, NULL);
				state->death_stage_one = false;
				// If we've reached the required good shares in a row then clock up
				if (((state->shares - state->shares_last_hw) >= state->shares_to_good) &&
						modminer->temp < MODMINER_TEMP_UP_LIMIT)
					modminer_delta_clock(thr, MODMINER_CLOCK_UP, false, false);
			}
		} else {
			// on rare occasions - the MMQ can just stop returning valid nonces
			double death = ITS_DEAD_JIM * (state->death_stage_one ? 2.0 : 1.0);
			gettimeofday(&now, NULL);
			if (tdiff(&now, &state->last_nonce) >= death) {
				if (state->death_stage_one) {
					modminer_delta_clock(thr, MODMINER_CLOCK_DEAD, false, true);
					applog(LOG_ERR, "%s%u: DEATH clock down",
						modminer->drv->name, modminer->device_id);

					// reset the death info and DISABLE it
					state->last_nonce.tv_sec = 0;
					state->last_nonce.tv_usec = 0;
					state->death_stage_one = false;
					return -1;
				} else {
					modminer_delta_clock(thr, MODMINER_CLOCK_DEAD, false, true);
					applog(LOG_ERR, "%s%u: death clock down",
						modminer->drv->name, modminer->device_id);

					state->death_stage_one = true;
				}
			}
		}

tryagain:

		if (work_restart(thr))
			break;

		if (state->overheated == true) {
			// don't check every time
			if (++temploop > 30) {
				check_temperature(thr);
				temploop = 0;
			}

		}

		if (state->overheated == true)
			timeout = overheattime;
		else
			timeout = processtime;

		gettimeofday(&now, NULL);
		if (tdiff(&now, &state->tv_workstart) > timeout)
			break;

		nmsleep(10);
		if (work_restart(thr))
			break;
	}

	struct timeval tv_workend, elapsed;
	gettimeofday(&tv_workend, NULL);
	timersub(&tv_workend, &state->tv_workstart, &elapsed);

	// Not exact since the clock may have changed ... but close enough I guess
	uint64_t hashes = (uint64_t)modminer->clock * (((uint64_t)elapsed.tv_sec * 1000000) + elapsed.tv_usec);
	// Overheat will complete the nonce range
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

static int64_t modminer_scanhash(struct thr_info *thr, struct work *work, int64_t __maybe_unused max_nonce)
{
	struct modminer_fpga_state *state = thr->cgpu_data;
	int64_t hashes = 0;
	bool startwork;
	struct timeval tv1, tv2;

	// Device is gone
	if (thr->cgpu->nodev)
		return -1;

	// Don't start new work if overheated
	if (state->overheated == true) {
		gettimeofday(&tv1, NULL);
		if (state->work_running)
			state->work_running = false;

		while (state->overheated == true) {
			check_temperature(thr);

			// Device is gone
			if (thr->cgpu->nodev)
				return -1;

			if (state->overheated == true) {
				gettimeofday(&tv2, NULL);

				// give up on this work item
				if (work_restart(thr) || tdiff(&tv2, &tv1) > 30)
					return 0;

				// Give it 1s rest then check again
				nmsleep(1000);
			}
		}
	}

	startwork = modminer_prepare_next_work(state, work);
	if (state->work_running) {
		hashes = modminer_process_results(thr);
		if (hashes == -1)
			return hashes;

		if (work_restart(thr)) {
			state->work_running = false;
			return 0;
		}
	} else
		state->work_running = true;

	if (startwork) {
		if (!modminer_start_work(thr))
			return -1;
		__copy_work(&state->running_work, work);
	}

	// This is intentionally early
	work->blk.nonce += hashes;
	return hashes;
}

static void modminer_hw_error(struct thr_info *thr)
{
	struct modminer_fpga_state *state = thr->cgpu_data;

	state->hw_errors++;
}

static void modminer_fpga_shutdown(struct thr_info *thr)
{
	free(thr->cgpu_data);
}

static char *modminer_set_device(struct cgpu_info *modminer, char *option, char *setting, char *replybuf)
{
	const char *ret;
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

		val -= (int)(modminer->clock);

		ret = modminer_delta_clock(modminer->thr[0], val, false, true);
		if (ret) {
			sprintf(replybuf, "Set clock failed: %s", ret);
			return replybuf;
		} else
			return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

struct device_drv modminer_drv = {
	.drv = DRIVER_MODMINER,
	.dname = "modminer",
	.name = "MMQ",
	.drv_detect = modminer_detect,
	.get_statline_before = get_modminer_statline_before,
	.set_device = modminer_set_device,
	.thread_prepare = modminer_fpga_prepare,
	.thread_init = modminer_fpga_init,
	.scanhash = modminer_scanhash,
	.hw_error = modminer_hw_error,
	.thread_shutdown = modminer_fpga_shutdown,
};
