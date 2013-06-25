/*
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 * Copyright 2012-2013 Xiangfu <xiangfu@openmobilefree.com>
 * Copyright 2012 Luke Dashjr
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#ifndef WIN32
  #include <sys/select.h>
  #include <termios.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #ifndef O_CLOEXEC
    #define O_CLOEXEC 0
  #endif
#else
  #include "compat.h"
  #include <windows.h>
  #include <io.h>
#endif

#include "elist.h"
#include "miner.h"
#include "usbutils.h"
#include "driver-avalon.h"
#include "hexdump.c"
#include "util.h"

int opt_avalon_temp = AVALON_TEMP_TARGET;
int opt_avalon_overheat = AVALON_TEMP_OVERHEAT;
bool opt_avalon_auto;

static int option_offset = -1;
struct device_drv avalon_drv;

static int avalon_init_task(struct avalon_task *at,
			    uint8_t reset, uint8_t ff, uint8_t fan,
			    uint8_t timeout, uint8_t asic_num,
			    uint8_t miner_num, uint8_t nonce_elf,
			    uint8_t gate_miner, int frequency)
{
	uint16_t *lefreq16;
	uint8_t *buf;
	static bool first = true;

	if (unlikely(!at))
		return -1;

	if (unlikely(timeout <= 0 || asic_num <= 0 || miner_num <= 0))
		return -1;

	memset(at, 0, sizeof(struct avalon_task));

	if (unlikely(reset)) {
		at->reset = 1;
		at->fan_eft = 1;
		at->timer_eft = 1;
		first = true;
	}

	at->flush_fifo = (ff ? 1 : 0);
	at->fan_eft = (fan ? 1 : 0);

	if (unlikely(first && !at->reset)) {
		at->fan_eft = 1;
		at->timer_eft = 1;
		first = false;
	}

	at->fan_pwm_data = (fan ? fan : AVALON_DEFAULT_FAN_MAX_PWM);
	at->timeout_data = timeout;
	at->asic_num = asic_num;
	at->miner_num = miner_num;
	at->nonce_elf = nonce_elf;

	at->gate_miner_elf = 1;
	at->asic_pll = 1;

	if (unlikely(gate_miner)) {
		at-> gate_miner = 1;
		at->asic_pll = 0;
	}

	buf = (uint8_t *)at;
	buf[5] = 0x00;
	buf[8] = 0x74;
	buf[9] = 0x01;
	buf[10] = 0x00;
	buf[11] = 0x00;
	lefreq16 = (uint16_t *)&buf[6];
	*lefreq16 = htole16(frequency * 8);

	return 0;
}

static inline void avalon_create_task(struct avalon_task *at,
				      struct work *work)
{
	memcpy(at->midstate, work->midstate, 32);
	memcpy(at->data, work->data + 64, 12);
}

static int avalon_write(struct cgpu_info *avalon, char *buf, ssize_t len, int ep)
{
	int err, amount;

	err = usb_write(avalon, buf, len, &amount, ep);
	applog(LOG_DEBUG, "%s%i: usb_write got err %d", avalon->drv->name,
	       avalon->device_id, err);

	if (unlikely(err != 0)) {
		applog(LOG_WARNING, "usb_write error on avalon_write");
		return AVA_SEND_ERROR;
	}
	if (amount != len) {
		applog(LOG_WARNING, "usb_write length mismatch on avalon_write");
		return AVA_SEND_ERROR;
	}

	return AVA_SEND_OK;
}

static int avalon_send_task(const struct avalon_task *at, struct cgpu_info *avalon)

{
	uint8_t buf[AVALON_WRITE_SIZE + 4 * AVALON_DEFAULT_ASIC_NUM];
	int delay, ret, i, ep = C_AVALON_TASK;
	struct avalon_info *info;
	uint32_t nonce_range;
	size_t nr_len;

	if (at->nonce_elf)
		nr_len = AVALON_WRITE_SIZE + 4 * at->asic_num;
	else
		nr_len = AVALON_WRITE_SIZE;

	memcpy(buf, at, AVALON_WRITE_SIZE);

	if (at->nonce_elf) {
		nonce_range = (uint32_t)0xffffffff / at->asic_num;
		for (i = 0; i < at->asic_num; i++) {
			buf[AVALON_WRITE_SIZE + (i * 4) + 3] =
				(i * nonce_range & 0xff000000) >> 24;
			buf[AVALON_WRITE_SIZE + (i * 4) + 2] =
				(i * nonce_range & 0x00ff0000) >> 16;
			buf[AVALON_WRITE_SIZE + (i * 4) + 1] =
				(i * nonce_range & 0x0000ff00) >> 8;
			buf[AVALON_WRITE_SIZE + (i * 4) + 0] =
				(i * nonce_range & 0x000000ff) >> 0;
		}
	}
#if defined(__BIG_ENDIAN__) || defined(MIPSEB)
	uint8_t tt = 0;

	tt = (buf[0] & 0x0f) << 4;
	tt |= ((buf[0] & 0x10) ? (1 << 3) : 0);
	tt |= ((buf[0] & 0x20) ? (1 << 2) : 0);
	tt |= ((buf[0] & 0x40) ? (1 << 1) : 0);
	tt |= ((buf[0] & 0x80) ? (1 << 0) : 0);
	buf[0] = tt;

	tt = (buf[4] & 0x0f) << 4;
	tt |= ((buf[4] & 0x10) ? (1 << 3) : 0);
	tt |= ((buf[4] & 0x20) ? (1 << 2) : 0);
	tt |= ((buf[4] & 0x40) ? (1 << 1) : 0);
	tt |= ((buf[4] & 0x80) ? (1 << 0) : 0);
	buf[4] = tt;
#endif
	info = avalon->device_data;
	delay = nr_len * 10 * 1000000;
	delay = delay / info->baud;

	if (at->reset) {
		ep = C_AVALON_RESET;
		nr_len = 1;
	}
	if (opt_debug) {
		applog(LOG_DEBUG, "Avalon: Sent(%u):", (unsigned int)nr_len);
		hexdump(buf, nr_len);
	}
	ret = avalon_write(avalon, (char *)buf, nr_len, ep);

	delay += 4000;
	nusleep(delay);
	applog(LOG_DEBUG, "Avalon: Sent: Buffer delay: %dus", delay);

	return ret;
}

static bool avalon_decode_nonce(struct thr_info *thr, struct cgpu_info *avalon,
				struct avalon_info *info, struct avalon_result *ar,
				struct work *work)
{
	uint32_t nonce;

	info = avalon->device_data;
	info->matching_work[work->subid]++;
	nonce = htole32(ar->nonce);
	applog(LOG_DEBUG, "Avalon: nonce = %0x08x", nonce);
	return submit_nonce(thr, work, nonce);
}

/* Wait until the ftdi chip returns a CTS saying we can send more data. */
static void wait_avalon_ready(struct cgpu_info *avalon)
{
	while (avalon_buffer_full(avalon)) {
		nmsleep(40);
	}
}

#define AVALON_CTS    (1 << 4)

static inline bool avalon_cts(char c)
{
	return (c & AVALON_CTS);
}

static int avalon_read(struct cgpu_info *avalon, unsigned char *buf,
		       size_t bufsize, int timeout, int ep)
{
	struct avalon_info *info = avalon->device_data;
	size_t total = 0, readsize = bufsize + 2;
	char readbuf[AVALON_READBUF_SIZE];
	int err, amount, ofs = 2, cp;

	err = usb_read_once_timeout(avalon, readbuf, readsize, &amount, timeout, ep);
	applog(LOG_DEBUG, "%s%i: Get avalon read got err %d",
	       avalon->drv->name, avalon->device_id, err);

	if (amount < 2)
		goto out;

	/* Use the fact that we're reading the status with the buffer to tell
	 * the write thread it should send more work without needing to call
	 * avalon_buffer_full directly. */
	if (avalon_cts(buf[0]))
		cgsem_post(&info->write_sem);

	/* The first 2 of every 64 bytes are status on FTDIRL */
	while (amount > 2) {
		cp = amount - 2;
		if (cp > 62)
			cp = 62;
		memcpy(&buf[total], &readbuf[ofs], cp);
		total += cp;
		amount -= cp + 2;
		ofs += 64;
	}
out:
	return total;
}

static int avalon_reset(struct cgpu_info *avalon, bool initial)
{
	struct avalon_result ar;
	int ret, i, spare;
	struct avalon_task at;
	uint8_t *buf, *tmp;
	struct timespec p;

	/* Send reset, then check for result */
	avalon_init_task(&at, 1, 0,
			 AVALON_DEFAULT_FAN_MAX_PWM,
			 AVALON_DEFAULT_TIMEOUT,
			 AVALON_DEFAULT_ASIC_NUM,
			 AVALON_DEFAULT_MINER_NUM,
			 0, 0,
			 AVALON_DEFAULT_FREQUENCY);

	wait_avalon_ready(avalon);
	ret = avalon_send_task(&at, avalon);
	if (unlikely(ret == AVA_SEND_ERROR))
		return -1;

	if (!initial) {
		applog(LOG_ERR, "AVA%d reset sequence sent", avalon->device_id);
		return 0;
	}

	ret = avalon_read(avalon, (unsigned char *)&ar, AVALON_READ_SIZE,
			  AVALON_RESET_TIMEOUT, C_GET_AVALON_RESET);

	/* What do these sleeps do?? */
	p.tv_sec = 0;
	p.tv_nsec = AVALON_RESET_PITCH;
	nanosleep(&p, NULL);

	/* Look for the first occurrence of 0xAA, the reset response should be:
	 * AA 55 AA 55 00 00 00 00 00 00 */
	spare = ret - 10;
	buf = tmp = (uint8_t *)&ar;
	if (opt_debug) {
		applog(LOG_DEBUG, "AVA%d reset: get:", avalon->device_id);
		hexdump(tmp, AVALON_READ_SIZE);
	}

	for (i = 0; i <= spare; i++) {
		buf = &tmp[i];
		if (buf[0] == 0xAA)
			break;
	}
	i = 0;

	if (buf[0] == 0xAA && buf[1] == 0x55 &&
	    buf[2] == 0xAA && buf[3] == 0x55) {
		for (i = 4; i < 11; i++)
			if (buf[i] != 0)
				break;
	}

	if (i != 11) {
		applog(LOG_ERR, "AVA%d: Reset failed! not an Avalon?"
		       " (%d: %02x %02x %02x %02x)", avalon->device_id,
		       i, buf[0], buf[1], buf[2], buf[3]);
		/* FIXME: return 1; */
	} else
		applog(LOG_WARNING, "AVA%d: Reset succeeded",
		       avalon->device_id);

	return 0;
}

static bool get_options(int this_option_offset, int *baud, int *miner_count,
			int *asic_count, int *timeout, int *frequency)
{
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2, *colon3, *colon4;
	size_t max;
	int i, tmp;

	if (opt_avalon_options == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_avalon_options;
		for (i = 0; i < this_option_offset; i++) {
			comma = strchr(ptr, ',');
			if (comma == NULL)
				break;
			ptr = comma + 1;
		}

		comma = strchr(ptr, ',');
		if (comma == NULL)
			max = strlen(ptr);
		else
			max = comma - ptr;

		if (max > BUFSIZ)
			max = BUFSIZ;
		strncpy(buf, ptr, max);
		buf[max] = '\0';
	}

	if (!(*buf))
		return false;

	colon = strchr(buf, ':');
	if (colon)
		*(colon++) = '\0';

	tmp = atoi(buf);
	switch (tmp) {
	case 115200:
		*baud = 115200;
		break;
	case 57600:
		*baud = 57600;
		break;
	case 38400:
		*baud = 38400;
		break;
	case 19200:
		*baud = 19200;
		break;
	default:
		quit(1, "Invalid avalon-options for baud (%s) "
			"must be 115200, 57600, 38400 or 19200", buf);
	}

	if (colon && *colon) {
		colon2 = strchr(colon, ':');
		if (colon2)
			*(colon2++) = '\0';

		if (*colon) {
			tmp = atoi(colon);
			if (tmp > 0 && tmp <= AVALON_DEFAULT_MINER_NUM) {
				*miner_count = tmp;
			} else {
				quit(1, "Invalid avalon-options for "
					"miner_count (%s) must be 1 ~ %d",
					colon, AVALON_DEFAULT_MINER_NUM);
			}
		}

		if (colon2 && *colon2) {
			colon3 = strchr(colon2, ':');
			if (colon3)
				*(colon3++) = '\0';

			tmp = atoi(colon2);
			if (tmp > 0 && tmp <= AVALON_DEFAULT_ASIC_NUM)
				*asic_count = tmp;
			else {
				quit(1, "Invalid avalon-options for "
					"asic_count (%s) must be 1 ~ %d",
					colon2, AVALON_DEFAULT_ASIC_NUM);
			}

			if (colon3 && *colon3) {
				colon4 = strchr(colon3, ':');
				if (colon4)
					*(colon4++) = '\0';

				tmp = atoi(colon3);
				if (tmp > 0 && tmp <= 0xff)
					*timeout = tmp;
				else {
					quit(1, "Invalid avalon-options for "
						"timeout (%s) must be 1 ~ %d",
						colon3, 0xff);
				}
				if (colon4 && *colon4) {
					tmp = atoi(colon4);
					switch (tmp) {
					case 256:
					case 270:
					case 282:
					case 300:
					case 325:
					case 350:
					case 375:
						*frequency = tmp;
						break;
					default:
						quit(1, "Invalid avalon-options for "
							"frequency must be 256/270/282/300/325/350/375");
					}
				}
			}
		}
	}
	return true;
}

static void avalon_idle(struct cgpu_info *avalon, struct avalon_info *info)
{
	int i;

	info->idle = true;
	wait_avalon_ready(avalon);
	/* Send idle to all miners */
	for (i = 0; i < info->miner_count; i++) {
		struct avalon_task at;

		if (unlikely(avalon_buffer_full(avalon)))
			break;
		avalon_init_task(&at, 0, 0, info->fan_pwm, info->timeout,
				 info->asic_count, info->miner_count, 1, 1,
				 info->frequency);
		avalon_send_task(&at, avalon);
	}
	applog(LOG_WARNING, "AVA%i: Idling %d miners", avalon->device_id, i);
	wait_avalon_ready(avalon);
}

static void avalon_initialise(struct cgpu_info *avalon)
{
	int err, interface;

	if (avalon->usbinfo.nodev)
		return;

	interface = avalon->usbdev->found->interface;
	// Reset
	err = usb_transfer(avalon, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
				FTDI_VALUE_RESET, interface, C_RESET);

	applog(LOG_DEBUG, "%s%i: reset got err %d",
		avalon->drv->name, avalon->device_id, err);

	if (avalon->usbinfo.nodev)
		return;

	// Set latency
	err = usb_transfer(avalon, FTDI_TYPE_OUT, FTDI_REQUEST_LATENCY,
			   AVALON_LATENCY, interface, C_LATENCY);

	applog(LOG_DEBUG, "%s%i: latency got err %d",
		avalon->drv->name, avalon->device_id, err);

	if (avalon->usbinfo.nodev)
		return;

	// Set data
	err = usb_transfer(avalon, FTDI_TYPE_OUT, FTDI_REQUEST_DATA,
				FTDI_VALUE_DATA_AVA, interface, C_SETDATA);

	applog(LOG_DEBUG, "%s%i: data got err %d",
		avalon->drv->name, avalon->device_id, err);

	if (avalon->usbinfo.nodev)
		return;

	// Set the baud
	err = usb_transfer(avalon, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, FTDI_VALUE_BAUD_AVA,
				(FTDI_INDEX_BAUD_AVA & 0xff00) | interface,
				C_SETBAUD);

	applog(LOG_DEBUG, "%s%i: setbaud got err %d",
		avalon->drv->name, avalon->device_id, err);

	if (avalon->usbinfo.nodev)
		return;

	// Set Modem Control
	err = usb_transfer(avalon, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
				FTDI_VALUE_MODEM, interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl got err %d",
		avalon->drv->name, avalon->device_id, err);

	if (avalon->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(avalon, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
				FTDI_VALUE_FLOW, interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl got err %d",
		avalon->drv->name, avalon->device_id, err);

	if (avalon->usbinfo.nodev)
		return;

	/* Avalon repeats the following */
	// Set Modem Control
	err = usb_transfer(avalon, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
				FTDI_VALUE_MODEM, interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl 2 got err %d",
		avalon->drv->name, avalon->device_id, err);

	if (avalon->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(avalon, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
				FTDI_VALUE_FLOW, interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl 2 got err %d",
		avalon->drv->name, avalon->device_id, err);
}

static bool avalon_detect_one(libusb_device *dev, struct usb_find_devices *found)
{
	int baud, miner_count, asic_count, timeout, frequency = 0;
	int this_option_offset = ++option_offset;
	struct avalon_info *info;
	struct cgpu_info *avalon;
	bool configured;
	int ret;

	avalon = usb_alloc_cgpu(&avalon_drv, AVALON_MINER_THREADS);

	configured = get_options(this_option_offset, &baud, &miner_count,
				 &asic_count, &timeout, &frequency);

	if (!usb_init(avalon, dev, found))
		goto shin;

	/* Even though this is an FTDI type chip, we want to do the parsing
	 * all ourselves so set it to std usb type */
	avalon->usbdev->usb_type = USB_TYPE_STD;
	avalon->usbdev->PrefPacketSize = AVALON_USB_PACKETSIZE;

	/* We have a real Avalon! */
	avalon_initialise(avalon);

	avalon->device_data = calloc(sizeof(struct avalon_info), 1);
	if (unlikely(!(avalon->device_data)))
		quit(1, "Failed to calloc avalon_info data");
	info = avalon->device_data;

	if (configured) {
		info->baud = baud;
		info->miner_count = miner_count;
		info->asic_count = asic_count;
		info->timeout = timeout;
		info->frequency = frequency;
	} else {
		info->baud = AVALON_IO_SPEED;
		info->miner_count = AVALON_DEFAULT_MINER_NUM;
		info->asic_count = AVALON_DEFAULT_ASIC_NUM;
		info->timeout = AVALON_DEFAULT_TIMEOUT;
		info->frequency = AVALON_DEFAULT_FREQUENCY;
	}

	info->fan_pwm = AVALON_DEFAULT_FAN_MIN_PWM;
	info->temp_max = 0;
	/* This is for check the temp/fan every 3~4s */
	info->temp_history_count = (4 / (float)((float)info->timeout * ((float)1.67/0x32))) + 1;
	if (info->temp_history_count <= 0)
		info->temp_history_count = 1;

	info->temp_history_index = 0;
	info->temp_sum = 0;
	info->temp_old = 0;

	if (!add_cgpu(avalon))
		goto unshin;

	ret = avalon_reset(avalon, true);
	if (ret && !configured)
		goto unshin;

	update_usb_stats(avalon);

	avalon_idle(avalon, info);

	applog(LOG_DEBUG, "Avalon Detected: %s "
	       "(miner_count=%d asic_count=%d timeout=%d frequency=%d)",
	       avalon->device_path, info->miner_count, info->asic_count, info->timeout,
	       info->frequency);

	return true;

unshin:

	usb_uninit(avalon);

shin:

	free(avalon->device_data);
	avalon->device_data = NULL;

	avalon = usb_free_cgpu(avalon);

	return false;
}

static void avalon_detect(void)
{
	usb_detect(&avalon_drv, avalon_detect_one);
}

static void avalon_init(struct cgpu_info *avalon)
{
	applog(LOG_INFO, "Avalon: Opened on %s", avalon->device_path);
}

static struct work *avalon_valid_result(struct cgpu_info *avalon, struct avalon_result *ar)
{
	return find_queued_work_bymidstate(avalon, (char *)ar->midstate, 32,
					   (char *)ar->data, 64, 12);
}

static void avalon_update_temps(struct cgpu_info *avalon, struct avalon_info *info,
				struct avalon_result *ar);

static void avalon_inc_nvw(struct avalon_info *info, struct thr_info *thr)
{
	if (unlikely(info->idle))
		return;

	applog(LOG_INFO, "%s%d: No matching work - HW error",
	       thr->cgpu->drv->name, thr->cgpu->device_id);

	inc_hw_errors(thr);
	info->no_matching_work++;
}

static void avalon_parse_results(struct cgpu_info *avalon, struct avalon_info *info,
				 struct thr_info *thr, char *buf, int *offset)
{
	int i, spare = *offset - AVALON_READ_SIZE;
	bool found = false;

	for (i = 0; i <= spare; i++) {
		struct avalon_result *ar;
		struct work *work;

		ar = (struct avalon_result *)&buf[i];
		work = avalon_valid_result(avalon, ar);
		if (work) {
			bool gettemp = false;

			found = true;

			if (avalon_decode_nonce(thr, avalon, info, ar, work)) {
				mutex_lock(&info->lock);
				if (!info->nonces++)
					gettemp = true;
				info->auto_nonces++;
				mutex_unlock(&info->lock);
			} else if (opt_avalon_auto) {
				mutex_lock(&info->lock);
				info->auto_hw++;
				mutex_unlock(&info->lock);
			}

			if (gettemp)
				avalon_update_temps(avalon, info, ar);
			break;
		}
	}

	if (!found) {
		spare = *offset - AVALON_READ_SIZE;
		/* We are buffering and haven't accumulated one more corrupt
		 * work result. */
		if (spare < (int)AVALON_READ_SIZE)
			return;
		avalon_inc_nvw(info, thr);
	} else {
		spare = AVALON_READ_SIZE + i;
		if (i) {
			if (i >= (int)AVALON_READ_SIZE)
				avalon_inc_nvw(info, thr);
			else
				applog(LOG_WARNING, "Avalon: Discarding %d bytes from buffer", i);
		}
	}

	*offset -= spare;
	memmove(buf, buf + spare, *offset);
}

static void avalon_running_reset(struct cgpu_info *avalon,
				   struct avalon_info *info)
{
	avalon_reset(avalon, false);
	avalon_idle(avalon, info);
	avalon->results = 0;
	info->reset = false;
}

static void *avalon_get_results(void *userdata)
{
	struct cgpu_info *avalon = (struct cgpu_info *)userdata;
	struct avalon_info *info = avalon->device_data;
	int offset = 0, read_delay = 0, ret = 0;
	const int rsize = AVALON_FTDI_READSIZE;
	char readbuf[AVALON_READBUF_SIZE];
	struct thr_info *thr = info->thr;
	struct timeval tv_start, tv_end;
	char threadname[24];

	snprintf(threadname, 24, "ava_recv/%d", avalon->device_id);
	RenameThread(threadname);

	while (likely(!avalon->shutdown)) {
		unsigned char buf[rsize];
		struct timeval tv_diff;
		int us_diff;

		if (offset >= (int)AVALON_READ_SIZE)
			avalon_parse_results(avalon, info, thr, readbuf, &offset);

		if (unlikely(offset + rsize >= AVALON_READBUF_SIZE)) {
			/* This should never happen */
			applog(LOG_ERR, "Avalon readbuf overflow, resetting buffer");
			offset = 0;
		}

		if (unlikely(info->reset)) {
			avalon_running_reset(avalon, info);
			/* Discard anything in the buffer */
			offset = 0;
		}

		/* As the usb read returns after just 1ms, sleep long enough
		 * to leave the interface idle for writes to occur, but do not
		 * sleep if we have been receiving data as more may be coming. */
		if (ret < 1) {
			cgtime(&tv_end);
			timersub(&tv_end, &tv_start, &tv_diff);
			/* Assume it has not been > 1 second so ignore tv_sec */
			us_diff = tv_diff.tv_usec;
			read_delay = AVALON_READ_TIMEOUT * 1000 - us_diff;
			if (likely(read_delay >= 1000))
				nusleep(read_delay);
		}

		cgtime(&tv_start);
		ret = avalon_read(avalon, buf, rsize, AVALON_READ_TIMEOUT,
				  C_AVALON_READ);

		if (ret < 1)
			continue;

		if (opt_debug) {
			applog(LOG_DEBUG, "Avalon: get:");
			hexdump((uint8_t *)buf, ret);
		}

		memcpy(&readbuf[offset], &buf, ret);
		offset += ret;
	}
	return NULL;
}

static void avalon_rotate_array(struct cgpu_info *avalon)
{
	avalon->queued = 0;
	if (++avalon->work_array >= AVALON_ARRAY_SIZE)
		avalon->work_array = 0;
}

static void avalon_set_timeout(struct avalon_info *info)
{
	info->timeout = AVALON_TIMEOUT_FACTOR / info->frequency;
}

static void avalon_inc_freq(struct avalon_info *info)
{
	info->frequency += 2;
	if (info->frequency > AVALON_MAX_FREQUENCY)
		info->frequency = AVALON_MAX_FREQUENCY;
	avalon_set_timeout(info);
	applog(LOG_NOTICE, "Avalon increasing frequency to %d, timeout %d",
	       info->frequency, info->timeout);
}

static void avalon_dec_freq(struct avalon_info *info)
{
	info->frequency -= 1;
	if (info->frequency < AVALON_MIN_FREQUENCY)
		info->frequency = AVALON_MIN_FREQUENCY;
	avalon_set_timeout(info);
	applog(LOG_NOTICE, "Avalon decreasing frequency to %d, timeout %d",
	       info->frequency, info->timeout);
}

static void *avalon_send_tasks(void *userdata)
{
	struct cgpu_info *avalon = (struct cgpu_info *)userdata;
	struct avalon_info *info = avalon->device_data;
	const int avalon_get_work_count = info->miner_count;
	char threadname[24];

	snprintf(threadname, 24, "ava_send/%d", avalon->device_id);
	RenameThread(threadname);

	while (likely(!avalon->shutdown)) {
		int start_count, end_count, i, j, ret;
		struct avalon_task at;
		int idled = 0;

		while (avalon_buffer_full(avalon))
			cgsem_wait(&info->write_sem);

		if (opt_avalon_auto && info->auto_queued >= AVALON_AUTO_CYCLE) {
			mutex_lock(&info->lock);
			if (info->auto_nonces >= (AVALON_AUTO_CYCLE * 19 / 20) &&
			    info->auto_nonces <= (AVALON_AUTO_CYCLE * 21 / 20)) {
				int total = info->auto_nonces + info->auto_hw;

				/* Try to keep hw errors ~1-1.5% */
				if (info->auto_hw * 100 < total)
					avalon_inc_freq(info);
				else if (info->auto_hw * 66 > total)
					avalon_dec_freq(info);
			}
			info->auto_queued =
			info->auto_nonces =
			info->auto_hw = 0;
			mutex_unlock(&info->lock);
		}

		mutex_lock(&info->qlock);
		start_count = avalon->work_array * avalon_get_work_count;
		end_count = start_count + avalon_get_work_count;
		for (i = start_count, j = 0; i < end_count; i++, j++) {
			if (avalon_buffer_full(avalon)) {
				applog(LOG_INFO,
				       "AVA%i: Buffer full after only %d of %d work queued",
					avalon->device_id, j, avalon_get_work_count);
				break;
			}

			if (likely(j < avalon->queued && !info->overheat)) {
				info->idle = false;
				avalon_init_task(&at, 0, 0, info->fan_pwm,
						info->timeout, info->asic_count,
						info->miner_count, 1, 0, info->frequency);
				avalon_create_task(&at, avalon->works[i]);
				info->auto_queued++;
			} else {
				idled++;
				avalon_init_task(&at, 0, 0, info->fan_pwm,
						info->timeout, info->asic_count,
						info->miner_count, 1, 1, info->frequency);
				/* Reset the auto_queued count if we end up
				 * idling any miners. */
				info->auto_queued = 0;
			}

			ret = avalon_send_task(&at, avalon);

			if (unlikely(ret == AVA_SEND_ERROR)) {
				applog(LOG_ERR, "AVA%i: Comms error(buffer)",
				       avalon->device_id);
				dev_error(avalon, REASON_DEV_COMMS_ERROR);
				info->reset = true;
				break;
			}
		}

		avalon_rotate_array(avalon);
		pthread_cond_signal(&info->qcond);
		mutex_unlock(&info->qlock);

		if (unlikely(idled && !info->idle)) {
			info->idle = true;
			applog(LOG_WARNING, "AVA%i: Idled %d miners",
			       avalon->device_id, idled);
		}
	}
	return NULL;
}

static bool avalon_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	struct avalon_info *info = avalon->device_data;
	struct timeval now;

	free(avalon->works);
	avalon->works = calloc(info->miner_count * sizeof(struct work *),
			       AVALON_ARRAY_SIZE);
	if (!avalon->works)
		quit(1, "Failed to calloc avalon works in avalon_prepare");

	info->thr = thr;
	mutex_init(&info->lock);
	mutex_init(&info->qlock);
	if (unlikely(pthread_cond_init(&info->qcond, NULL)))
		quit(1, "Failed to pthread_cond_init avalon qcond");
	cgsem_init(&info->write_sem);

	if (pthread_create(&info->read_thr, NULL, avalon_get_results, (void *)avalon))
		quit(1, "Failed to create avalon read_thr");

	if (pthread_create(&info->write_thr, NULL, avalon_send_tasks, (void *)avalon))
		quit(1, "Failed to create avalon write_thr");

	avalon_init(avalon);

	cgtime(&now);
	get_datestamp(avalon->init, &now);
	return true;
}

static void do_avalon_close(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	struct avalon_info *info = avalon->device_data;

	pthread_join(info->read_thr, NULL);
	pthread_join(info->write_thr, NULL);
	avalon_running_reset(avalon, info);

	info->no_matching_work = 0;

	cgsem_destroy(&info->write_sem);
}

static inline void record_temp_fan(struct avalon_info *info, struct avalon_result *ar, float *temp_avg)
{
	info->fan0 = ar->fan0 * AVALON_FAN_FACTOR;
	info->fan1 = ar->fan1 * AVALON_FAN_FACTOR;
	info->fan2 = ar->fan2 * AVALON_FAN_FACTOR;

	info->temp0 = ar->temp0;
	info->temp1 = ar->temp1;
	info->temp2 = ar->temp2;
	if (ar->temp0 & 0x80) {
		ar->temp0 &= 0x7f;
		info->temp0 = 0 - ((~ar->temp0 & 0x7f) + 1);
	}
	if (ar->temp1 & 0x80) {
		ar->temp1 &= 0x7f;
		info->temp1 = 0 - ((~ar->temp1 & 0x7f) + 1);
	}
	if (ar->temp2 & 0x80) {
		ar->temp2 &= 0x7f;
		info->temp2 = 0 - ((~ar->temp2 & 0x7f) + 1);
	}

	*temp_avg = info->temp2 > info->temp1 ? info->temp2 : info->temp1;

	if (info->temp0 > info->temp_max)
		info->temp_max = info->temp0;
	if (info->temp1 > info->temp_max)
		info->temp_max = info->temp1;
	if (info->temp2 > info->temp_max)
		info->temp_max = info->temp2;
}

static void temp_rise(struct avalon_info *info, int temp)
{
	if (temp >= opt_avalon_temp + AVALON_TEMP_HYSTERESIS * 3) {
		info->fan_pwm = AVALON_DEFAULT_FAN_MAX_PWM;
		return;
	}
	if (temp >= opt_avalon_temp + AVALON_TEMP_HYSTERESIS * 2)
		info->fan_pwm += 10;
	else if (temp > opt_avalon_temp)
		info->fan_pwm += 5;
	else if (temp >= opt_avalon_temp - AVALON_TEMP_HYSTERESIS)
		info->fan_pwm += 1;
	else
		return;

	if (info->fan_pwm > AVALON_DEFAULT_FAN_MAX_PWM)
		info->fan_pwm = AVALON_DEFAULT_FAN_MAX_PWM;
}

static void temp_drop(struct avalon_info *info, int temp)
{
	if (temp <= opt_avalon_temp - AVALON_TEMP_HYSTERESIS * 3) {
		info->fan_pwm = AVALON_DEFAULT_FAN_MIN_PWM;
		return;
	}
	if (temp <= opt_avalon_temp - AVALON_TEMP_HYSTERESIS * 2)
		info->fan_pwm -= 10;
	else if (temp <= opt_avalon_temp - AVALON_TEMP_HYSTERESIS)
		info->fan_pwm -= 5;
	else if (temp < opt_avalon_temp)
		info->fan_pwm -= 1;

	if (info->fan_pwm < AVALON_DEFAULT_FAN_MIN_PWM)
		info->fan_pwm = AVALON_DEFAULT_FAN_MIN_PWM;
}

static inline void adjust_fan(struct avalon_info *info)
{
	int temp_new;

	temp_new = info->temp_sum / info->temp_history_count;

	if (temp_new > info->temp_old)
		temp_rise(info, temp_new);
	else if (temp_new < info->temp_old)
		temp_drop(info, temp_new);
	else {
		/* temp_new == info->temp_old */
		if (temp_new > opt_avalon_temp)
			temp_rise(info, temp_new);
		else if (temp_new < opt_avalon_temp - AVALON_TEMP_HYSTERESIS)
			temp_drop(info, temp_new);
	}
	info->temp_old = temp_new;
}

static void avalon_update_temps(struct cgpu_info *avalon, struct avalon_info *info,
				struct avalon_result *ar)
{
	record_temp_fan(info, ar, &(avalon->temp));
	applog(LOG_INFO,
		"Avalon: Fan1: %d/m, Fan2: %d/m, Fan3: %d/m\t"
		"Temp1: %dC, Temp2: %dC, Temp3: %dC, TempMAX: %dC",
		info->fan0, info->fan1, info->fan2,
		info->temp0, info->temp1, info->temp2, info->temp_max);
	info->temp_history_index++;
	info->temp_sum += avalon->temp;
	applog(LOG_DEBUG, "Avalon: temp_index: %d, temp_count: %d, temp_old: %d",
		info->temp_history_index, info->temp_history_count, info->temp_old);
	if (info->temp_history_index == info->temp_history_count) {
		adjust_fan(info);
		info->temp_history_index = 0;
		info->temp_sum = 0;
	}
	if (unlikely(info->temp_old >= opt_avalon_overheat)) {
		applog(LOG_WARNING, "AVA%d overheat! Idling", avalon->device_id);
		info->overheat = true;
	} else if (info->overheat && info->temp_old <= opt_avalon_temp) {
		applog(LOG_WARNING, "AVA%d cooled, restarting", avalon->device_id);
		info->overheat = false;
	}
}

static void get_avalon_statline_before(char *buf, struct cgpu_info *avalon)
{
	struct avalon_info *info = avalon->device_data;
	int lowfan = 10000;

	/* Find the lowest fan speed of the ASIC cooling fans. */
	if (info->fan1 >= 0 && info->fan1 < lowfan)
		lowfan = info->fan1;
	if (info->fan2 >= 0 && info->fan2 < lowfan)
		lowfan = info->fan2;

	tailsprintf(buf, "%2d/%3dC %04dR | ", info->temp0, info->temp2, lowfan);
}

/* We use a replacement algorithm to only remove references to work done from
 * the buffer when we need the extra space for new work. */
static bool avalon_fill(struct cgpu_info *avalon)
{
	struct avalon_info *info = avalon->device_data;
	int subid, slot, mc;
	struct work *work;
	bool ret = true;

	mc = info->miner_count;
	mutex_lock(&info->qlock);
	if (avalon->queued >= mc)
		goto out_unlock;
	work = get_queued(avalon);
	if (unlikely(!work)) {
		ret = false;
		goto out_unlock;
	}
	subid = avalon->queued++;
	work->subid = subid;
	slot = avalon->work_array * mc + subid;
	if (likely(avalon->works[slot]))
		work_completed(avalon, avalon->works[slot]);
	avalon->works[slot] = work;
	if (avalon->queued < mc)
		ret = false;
out_unlock:
	mutex_unlock(&info->qlock);

	return ret;
}

static int64_t avalon_scanhash(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	struct avalon_info *info = avalon->device_data;
	const int miner_count = info->miner_count;
	struct timeval now, then, tdiff;
	int64_t hash_count, us_timeout;
	struct timespec abstime;

	/* Half nonce range */
	us_timeout = 0x80000000ll / info->asic_count / info->frequency;
	tdiff.tv_sec = us_timeout / 1000000;
	tdiff.tv_usec = us_timeout - (tdiff.tv_sec * 1000000);
	cgtime(&now);
	timeradd(&now, &tdiff, &then);
	abstime.tv_sec = then.tv_sec;
	abstime.tv_nsec = then.tv_usec * 1000;

	/* Wait until avalon_send_tasks signals us that it has completed
	 * sending its work or a full nonce range timeout has occurred */
	mutex_lock(&info->qlock);
	pthread_cond_timedwait(&info->qcond, &info->qlock, &abstime);
	mutex_unlock(&info->qlock);

	mutex_lock(&info->lock);
	hash_count = 0xffffffffull * (uint64_t)info->nonces;
	avalon->results += info->nonces;
	if (avalon->results > miner_count)
		avalon->results = miner_count;
	if (!info->idle && !info->reset)
		avalon->results -= miner_count / 3;
	else
		avalon->results = miner_count;
	info->nonces = 0;
	mutex_unlock(&info->lock);

	/* Check for nothing but consecutive bad results or consistently less
	 * results than we should be getting and reset the FPGA if necessary */
	if (avalon->results < -miner_count && !info->reset) {
		applog(LOG_ERR, "AVA%d: Result return rate low, resetting!",
			avalon->device_id);
		info->reset = true;
	}

	if (unlikely(avalon->usbinfo.nodev)) {
		applog(LOG_ERR, "AVA%d: Device disappeared, shutting down thread",
		       avalon->device_id);
		avalon->shutdown = true;
	}

	/* This hashmeter is just a utility counter based on returned shares */
	return hash_count;
}

static void avalon_flush_work(struct cgpu_info *avalon)
{
	struct avalon_info *info = avalon->device_data;

	mutex_lock(&info->qlock);
	/* Will overwrite any work queued */
	avalon->queued = 0;
	pthread_cond_signal(&info->qcond);
	mutex_unlock(&info->qlock);
}

static struct api_data *avalon_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct avalon_info *info = cgpu->device_data;
	int i;

	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "miner_count", &(info->miner_count),false);
	root = api_add_int(root, "asic_count", &(info->asic_count), false);
	root = api_add_int(root, "timeout", &(info->timeout), false);
	root = api_add_int(root, "frequency", &(info->frequency), false);

	root = api_add_int(root, "fan1", &(info->fan0), false);
	root = api_add_int(root, "fan2", &(info->fan1), false);
	root = api_add_int(root, "fan3", &(info->fan2), false);

	root = api_add_int(root, "temp1", &(info->temp0), false);
	root = api_add_int(root, "temp2", &(info->temp1), false);
	root = api_add_int(root, "temp3", &(info->temp2), false);
	root = api_add_int(root, "temp_max", &(info->temp_max), false);

	root = api_add_int(root, "no_matching_work", &(info->no_matching_work), false);
	for (i = 0; i < info->miner_count; i++) {
		char mcw[24];

		sprintf(mcw, "match_work_count%d", i + 1);
		root = api_add_int(root, mcw, &(info->matching_work[i]), false);
	}

	return root;
}

static void avalon_shutdown(struct thr_info *thr)
{
	do_avalon_close(thr);
}

struct device_drv avalon_drv = {
	.drv_id = DRIVER_AVALON,
	.dname = "avalon",
	.name = "AVA",
	.drv_detect = avalon_detect,
	.thread_prepare = avalon_prepare,
	.hash_work = hash_queued_work,
	.queue_full = avalon_fill,
	.scanwork = avalon_scanhash,
	.flush_work = avalon_flush_work,
	.get_api_stats = avalon_api_stats,
	.get_statline_before = get_avalon_statline_before,
	.reinit_device = avalon_init,
	.thread_shutdown = avalon_shutdown,
};
