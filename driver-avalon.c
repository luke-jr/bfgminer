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
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
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
int opt_avalon_fan_min = AVALON_DEFAULT_FAN_MIN_PWM;
int opt_avalon_fan_max = AVALON_DEFAULT_FAN_MAX_PWM;
int opt_avalon_freq_min = AVALON_MIN_FREQUENCY;
int opt_avalon_freq_max = AVALON_MAX_FREQUENCY;
int opt_bitburner_core_voltage = BITBURNER_DEFAULT_CORE_VOLTAGE;
bool opt_avalon_auto;

static int option_offset = -1;

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
	cgtimer_t ts_start;
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
	delay += 4000;

	if (at->reset) {
		ep = C_AVALON_RESET;
		nr_len = 1;
	}
	if (opt_debug) {
		applog(LOG_DEBUG, "Avalon: Sent(%u):", (unsigned int)nr_len);
		hexdump(buf, nr_len);
	}
	cgsleep_prepare_r(&ts_start);
	ret = avalon_write(avalon, (char *)buf, nr_len, ep);
	cgsleep_us_r(&ts_start, delay);

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
		cgsleep_ms(40);
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
	size_t total = 0, readsize = bufsize + 2;
	char readbuf[AVALON_READBUF_SIZE];
	int err, amount, ofs = 2, cp;

	err = usb_read_once_timeout(avalon, readbuf, readsize, &amount, timeout, ep);
	applog(LOG_DEBUG, "%s%i: Get avalon read got err %d",
	       avalon->drv->name, avalon->device_id, err);

	if (amount < 2)
		goto out;

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
	struct avalon_info *info = avalon->device_data;

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
		applog(LOG_ERR, "%s%d reset sequence sent", avalon->drv->name, avalon->device_id);
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
		applog(LOG_DEBUG, "%s%d reset: get:", avalon->drv->name, avalon->device_id);
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
		applog(LOG_ERR, "%s%d: Reset failed! not an Avalon?"
		       " (%d: %02x %02x %02x %02x)", avalon->drv->name, avalon->device_id,
		       i, buf[0], buf[1], buf[2], buf[3]);
		/* FIXME: return 1; */
	} else {
		/* buf[44]: minor
		 * buf[45]: day
		 * buf[46]: year,month, d6: 201306
		 */
		info->ctlr_ver = ((buf[46] >> 4) + 2000) * 1000000 +
			(buf[46] & 0x0f) * 10000 +
			buf[45] * 100 +	buf[44];
		applog(LOG_WARNING, "%s%d: Reset succeeded (Controller version: %d)",
		       avalon->drv->name, avalon->device_id, info->ctlr_ver);
	}

	return 0;
}

static int avalon_calc_timeout(int frequency)
{
	return AVALON_TIMEOUT_FACTOR / frequency;
}

static bool get_options(int this_option_offset, int *baud, int *miner_count,
			int *asic_count, int *timeout, int *frequency)
{
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2, *colon3, *colon4;
	bool timeout_default;
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

			timeout_default = false;
			if (colon3 && *colon3) {
				colon4 = strchr(colon3, ':');
				if (colon4)
					*(colon4++) = '\0';

				if (tolower(*colon3) == 'd')
					timeout_default = true;
				else {
					tmp = atoi(colon3);
					if (tmp > 0 && tmp <= 0xff)
						*timeout = tmp;
					else {
						quit(1, "Invalid avalon-options for "
							"timeout (%s) must be 1 ~ %d",
							colon3, 0xff);
					}
				}
				if (colon4 && *colon4) {
					tmp = atoi(colon4);
					if (tmp < AVALON_MIN_FREQUENCY || tmp > AVALON_MAX_FREQUENCY) {
						quit(1, "Invalid avalon-options for frequency, must be %d <= frequency <= %d",
						     AVALON_MIN_FREQUENCY, AVALON_MAX_FREQUENCY);
					}
					*frequency = tmp;
					if (timeout_default)
						*timeout = avalon_calc_timeout(*frequency);
				}
			}
		}
	}
	return true;
}

char *set_avalon_fan(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to avalon-fan";
	if (ret == 1)
		val2 = val1;

	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value passed to avalon-fan";

	opt_avalon_fan_min = val1 * AVALON_PWM_MAX / 100;
	opt_avalon_fan_max = val2 * AVALON_PWM_MAX / 100;

	return NULL;
}

char *set_avalon_freq(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to avalon-freq";
	if (ret == 1)
		val2 = val1;

	if (val1 < AVALON_MIN_FREQUENCY || val1 > AVALON_MAX_FREQUENCY ||
	    val2 < AVALON_MIN_FREQUENCY || val2 > AVALON_MAX_FREQUENCY ||
	    val2 < val1)
		return "Invalid value passed to avalon-freq";

	opt_avalon_freq_min = val1;
	opt_avalon_freq_max = val2;

	return NULL;
}

static void avalon_idle(struct cgpu_info *avalon, struct avalon_info *info)
{
	int i;

	wait_avalon_ready(avalon);
	/* Send idle to all miners */
	for (i = 0; i < info->miner_count; i++) {
		struct avalon_task at;

		if (unlikely(avalon_buffer_full(avalon)))
			break;
		info->idle++;
		avalon_init_task(&at, 0, 0, info->fan_pwm, info->timeout,
				 info->asic_count, info->miner_count, 1, 1,
				 info->frequency);
		avalon_send_task(&at, avalon);
	}
	applog(LOG_WARNING, "%s%i: Idling %d miners", avalon->drv->name, avalon->device_id, i);
	wait_avalon_ready(avalon);
}

static void avalon_initialise(struct cgpu_info *avalon)
{
	int err, interface;

	if (avalon->usbinfo.nodev)
		return;

	interface = usb_interface(avalon);
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

static bool bitburner_set_core_voltage(struct cgpu_info *avalon, int core_voltage)
{
	uint8_t buf[2];
	int err;

	if (usb_ident(avalon) == IDENT_BTB) {
		buf[0] = (uint8_t)core_voltage;
		buf[1] = (uint8_t)(core_voltage >> 8);
		err = usb_transfer_data(avalon, FTDI_TYPE_OUT, BITBURNER_REQUEST,
				BITBURNER_VALUE, BITBURNER_INDEX_SET_VOLTAGE,
				(uint32_t *)buf, sizeof(buf), C_BB_SET_VOLTAGE);
		if (unlikely(err < 0)) {
			applog(LOG_ERR, "%s%i: SetCoreVoltage failed: err = %d",
				avalon->drv->name, avalon->device_id, err);
			return false;
		} else {
			applog(LOG_WARNING, "%s%i: Core voltage set to %d millivolts",
				avalon->drv->name, avalon->device_id,
				core_voltage);
		}
		return true;
	}
	return false;
}

static int bitburner_get_core_voltage(struct cgpu_info *avalon)
{
	uint8_t buf[2];
	int err;
	int amount;

	if (usb_ident(avalon) == IDENT_BTB) {
		err = usb_transfer_read(avalon, FTDI_TYPE_IN, BITBURNER_REQUEST,
				BITBURNER_VALUE, BITBURNER_INDEX_GET_VOLTAGE,
				(char *)buf, sizeof(buf), &amount,
				C_BB_GET_VOLTAGE);
		if (unlikely(err != 0 || amount != 2)) {
			applog(LOG_ERR, "%s%i: GetCoreVoltage failed: err = %d, amount = %d",
				avalon->drv->name, avalon->device_id, err, amount);
			return 0;
		} else {
			return (int)(buf[0] + ((unsigned int)buf[1] << 8));
		}
	} else {
		return 0;
	}
}

static void bitburner_get_version(struct cgpu_info *avalon)
{
	struct avalon_info *info = avalon->device_data;
	uint8_t buf[3];
	int err;
	int amount;

	err = usb_transfer_read(avalon, FTDI_TYPE_IN, BITBURNER_REQUEST,
			BITBURNER_VALUE, BITBURNER_INDEX_GET_VERSION,
			(char *)buf, sizeof(buf), &amount,
			C_GETVERSION);
	if (unlikely(err != 0 || amount != sizeof(buf))) {
		applog(LOG_DEBUG, "%s%i: GetVersion failed: err=%d, amt=%d assuming %d.%d.%d",
			avalon->drv->name, avalon->device_id, err, amount,
			BITBURNER_VERSION1, BITBURNER_VERSION2, BITBURNER_VERSION3);
		info->version1 = BITBURNER_VERSION1;
		info->version2 = BITBURNER_VERSION2;
		info->version3 = BITBURNER_VERSION3;
	} else {
		info->version1 = buf[0];
		info->version2 = buf[1];
		info->version3 = buf[2];
	}
}

static bool avalon_detect_one(libusb_device *dev, struct usb_find_devices *found)
{
	int baud, miner_count, asic_count, timeout, frequency;
	int this_option_offset = ++option_offset;
	struct avalon_info *info;
	struct cgpu_info *avalon;
	bool configured;
	int ret;

	avalon = usb_alloc_cgpu(&avalon_drv, AVALON_MINER_THREADS);

	baud = AVALON_IO_SPEED;
	miner_count = AVALON_DEFAULT_MINER_NUM;
	asic_count = AVALON_DEFAULT_ASIC_NUM;
	timeout = AVALON_DEFAULT_TIMEOUT;
	frequency = AVALON_DEFAULT_FREQUENCY;

	configured = get_options(this_option_offset, &baud, &miner_count,
				 &asic_count, &timeout, &frequency);

	if (!usb_init(avalon, dev, found))
		goto shin;

	/* Even though this is an FTDI type chip, we want to do the parsing
	 * all ourselves so set it to std usb type */
	avalon->usbdev->usb_type = USB_TYPE_STD;
	usb_set_pps(avalon, AVALON_USB_PACKETSIZE);

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

	if (usb_ident(avalon) == IDENT_BTB) {
		if (opt_bitburner_core_voltage < BITBURNER_MIN_COREMV ||
		    opt_bitburner_core_voltage > BITBURNER_MAX_COREMV) {
			quit(1, "Invalid bitburner-voltage %d must be %dmv - %dmv",
				opt_bitburner_core_voltage,
				BITBURNER_MIN_COREMV,
				BITBURNER_MAX_COREMV);
		} else
			bitburner_set_core_voltage(avalon, opt_bitburner_core_voltage);

		bitburner_get_version(avalon);
	}

	return true;

unshin:

	usb_uninit(avalon);

shin:

	free(avalon->device_data);
	avalon->device_data = NULL;

	avalon = usb_free_cgpu(avalon);

	return false;
}

static void avalon_detect(bool __maybe_unused hotplug)
{
	usb_detect(&avalon_drv, avalon_detect_one);
}

static void avalon_init(struct cgpu_info *avalon)
{
	applog(LOG_INFO, "Avalon: Opened on %s", avalon->device_path);
}

static struct work *avalon_valid_result(struct cgpu_info *avalon, struct avalon_result *ar)
{
	return clone_queued_work_bymidstate(avalon, (char *)ar->midstate, 32,
					    (char *)ar->data, 64, 12);
}

static void avalon_update_temps(struct cgpu_info *avalon, struct avalon_info *info,
				struct avalon_result *ar);

static void avalon_inc_nvw(struct avalon_info *info, struct thr_info *thr)
{
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
			free_work(work);

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
	const int rsize = AVALON_FTDI_READSIZE;
	char readbuf[AVALON_READBUF_SIZE];
	struct thr_info *thr = info->thr;
	cgtimer_t ts_start;
	int offset = 0, ret = 0;
	char threadname[24];

	snprintf(threadname, 24, "ava_recv/%d", avalon->device_id);
	RenameThread(threadname);
	cgsleep_prepare_r(&ts_start);

	while (likely(!avalon->shutdown)) {
		unsigned char buf[rsize];

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
		 * sleep if we have been receiving data, and we do not yet have
		 * a full result as more may be coming. */
		if (ret < 1 || offset == 0)
			cgsleep_ms_r(&ts_start, AVALON_READ_TIMEOUT);

		cgsleep_prepare_r(&ts_start);
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

static void bitburner_rotate_array(struct cgpu_info *avalon)
{
	avalon->queued = 0;
	if (++avalon->work_array >= BITBURNER_ARRAY_SIZE)
		avalon->work_array = 0;
}

static void avalon_set_timeout(struct avalon_info *info)
{
	info->timeout = avalon_calc_timeout(info->frequency);
}

static void avalon_set_freq(struct cgpu_info *avalon, int frequency)
{
	struct avalon_info *info = avalon->device_data;

	info->frequency = frequency;
	if (info->frequency > opt_avalon_freq_max)
		info->frequency = opt_avalon_freq_max;
	if (info->frequency < opt_avalon_freq_min)
		info->frequency = opt_avalon_freq_min;
	avalon_set_timeout(info);
	applog(LOG_WARNING, "%s%i: Set frequency to %d, timeout %d",
		avalon->drv->name, avalon->device_id,
		info->frequency, info->timeout);
}

static void avalon_inc_freq(struct avalon_info *info)
{
	info->frequency += 2;
	if (info->frequency > opt_avalon_freq_max)
		info->frequency = opt_avalon_freq_max;
	avalon_set_timeout(info);
	applog(LOG_NOTICE, "Avalon increasing frequency to %d, timeout %d",
	       info->frequency, info->timeout);
}

static void avalon_dec_freq(struct avalon_info *info)
{
	info->frequency -= 1;
	if (info->frequency < opt_avalon_freq_min)
		info->frequency = opt_avalon_freq_min;
	avalon_set_timeout(info);
	applog(LOG_NOTICE, "Avalon decreasing frequency to %d, timeout %d",
	       info->frequency, info->timeout);
}

static void avalon_reset_auto(struct avalon_info *info)
{
	info->auto_queued =
	info->auto_nonces =
	info->auto_hw = 0;
}

static void avalon_adjust_freq(struct avalon_info *info, struct cgpu_info *avalon)
{
	if (opt_avalon_auto && info->auto_queued >= AVALON_AUTO_CYCLE) {
		mutex_lock(&info->lock);
		if (!info->optimal) {
			if (info->fan_pwm >= opt_avalon_fan_max) {
				applog(LOG_WARNING,
				       "%s%i: Above optimal temperature, throttling",
				       avalon->drv->name, avalon->device_id);
				avalon_dec_freq(info);
			}
		} else if (info->auto_nonces >= (AVALON_AUTO_CYCLE * 19 / 20) &&
			   info->auto_nonces <= (AVALON_AUTO_CYCLE * 21 / 20)) {
				int total = info->auto_nonces + info->auto_hw;

				/* Try to keep hw errors < 2% */
				if (info->auto_hw * 100 < total)
					avalon_inc_freq(info);
				else if (info->auto_hw * 66 > total)
					avalon_dec_freq(info);
		}
		avalon_reset_auto(info);
		mutex_unlock(&info->lock);
	}
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
		cgtimer_t ts_start;
		struct avalon_task at;
		bool idled = false;
		int64_t us_timeout;

		while (avalon_buffer_full(avalon))
			cgsleep_ms(40);

		avalon_adjust_freq(info, avalon);

		/* A full nonce range */
		us_timeout = 0x100000000ll / info->asic_count / info->frequency;
		cgsleep_prepare_r(&ts_start);

		mutex_lock(&info->qlock);
		start_count = avalon->work_array * avalon_get_work_count;
		end_count = start_count + avalon_get_work_count;
		for (i = start_count, j = 0; i < end_count; i++, j++) {
			if (avalon_buffer_full(avalon)) {
				applog(LOG_INFO,
				       "%s%i: Buffer full after only %d of %d work queued",
					avalon->drv->name, avalon->device_id, j, avalon_get_work_count);
				break;
			}

			if (likely(j < avalon->queued && !info->overheat && avalon->works[i])) {
				avalon_init_task(&at, 0, 0, info->fan_pwm,
						info->timeout, info->asic_count,
						info->miner_count, 1, 0, info->frequency);
				avalon_create_task(&at, avalon->works[i]);
				info->auto_queued++;
			} else {
				int idle_freq = info->frequency;

				if (!info->idle++)
					idled = true;
				if (unlikely(info->overheat && opt_avalon_auto))
					idle_freq = AVALON_MIN_FREQUENCY;
				avalon_init_task(&at, 0, 0, info->fan_pwm,
						info->timeout, info->asic_count,
						info->miner_count, 1, 1, idle_freq);
				/* Reset the auto_queued count if we end up
				 * idling any miners. */
				avalon_reset_auto(info);
			}

			ret = avalon_send_task(&at, avalon);

			if (unlikely(ret == AVA_SEND_ERROR)) {
				applog(LOG_ERR, "%s%i: Comms error(buffer)",
				       avalon->drv->name, avalon->device_id);
				dev_error(avalon, REASON_DEV_COMMS_ERROR);
				info->reset = true;
				break;
			}
		}

		avalon_rotate_array(avalon);
		pthread_cond_signal(&info->qcond);
		mutex_unlock(&info->qlock);

		if (unlikely(idled)) {
			applog(LOG_WARNING, "%s%i: Idled %d miners",
			       avalon->drv->name, avalon->device_id, idled);
		}

		/* Sleep how long it would take to complete a full nonce range
		 * at the current frequency using the clock_nanosleep function
		 * timed from before we started loading new work so it will
		 * fall short of the full duration. */
		cgsleep_us_r(&ts_start, us_timeout);
	}
	return NULL;
}

static void *bitburner_send_tasks(void *userdata)
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
		bool idled = false;

		while (avalon_buffer_full(avalon))
			cgsleep_ms(40);

		avalon_adjust_freq(info, avalon);

		/* Give other threads a chance to acquire qlock. */
		i = 0;
		do {
			cgsleep_ms(40);
		} while (!avalon->shutdown && i++ < 15
			&& avalon->queued < avalon_get_work_count);

		mutex_lock(&info->qlock);
		start_count = avalon->work_array * avalon_get_work_count;
		end_count = start_count + avalon_get_work_count;
		for (i = start_count, j = 0; i < end_count; i++, j++) {
			while (avalon_buffer_full(avalon))
				cgsleep_ms(40);

			if (likely(j < avalon->queued && !info->overheat && avalon->works[i])) {
				avalon_init_task(&at, 0, 0, info->fan_pwm,
						info->timeout, info->asic_count,
						info->miner_count, 1, 0, info->frequency);
				avalon_create_task(&at, avalon->works[i]);
				info->auto_queued++;
			} else {
				int idle_freq = info->frequency;

				if (!info->idle++)
					idled = true;
				if (unlikely(info->overheat && opt_avalon_auto))
					idle_freq = AVALON_MIN_FREQUENCY;
				avalon_init_task(&at, 0, 0, info->fan_pwm,
						info->timeout, info->asic_count,
						info->miner_count, 1, 1, idle_freq);
				/* Reset the auto_queued count if we end up
				 * idling any miners. */
				avalon_reset_auto(info);
			}

			ret = avalon_send_task(&at, avalon);

			if (unlikely(ret == AVA_SEND_ERROR)) {
				applog(LOG_ERR, "%s%i: Comms error(buffer)",
				       avalon->drv->name, avalon->device_id);
				dev_error(avalon, REASON_DEV_COMMS_ERROR);
				info->reset = true;
				break;
			}
		}

		bitburner_rotate_array(avalon);
		pthread_cond_signal(&info->qcond);
		mutex_unlock(&info->qlock);

		if (unlikely(idled)) {
			applog(LOG_WARNING, "%s%i: Idled %d miners",
			       avalon->drv->name, avalon->device_id, idled);
		}
	}
	return NULL;
}

static bool avalon_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	struct avalon_info *info = avalon->device_data;
	int array_size = AVALON_ARRAY_SIZE;
	void *(*write_thread_fn)(void *) = avalon_send_tasks;

	if (usb_ident(avalon) == IDENT_BTB) {
		array_size = BITBURNER_ARRAY_SIZE;
		write_thread_fn = bitburner_send_tasks;
	}

	free(avalon->works);
	avalon->works = calloc(info->miner_count * sizeof(struct work *),
			       array_size);
	if (!avalon->works)
		quit(1, "Failed to calloc avalon works in avalon_prepare");

	info->thr = thr;
	mutex_init(&info->lock);
	mutex_init(&info->qlock);
	if (unlikely(pthread_cond_init(&info->qcond, NULL)))
		quit(1, "Failed to pthread_cond_init avalon qcond");

	if (pthread_create(&info->read_thr, NULL, avalon_get_results, (void *)avalon))
		quit(1, "Failed to create avalon read_thr");

	if (pthread_create(&info->write_thr, NULL, write_thread_fn, (void *)avalon))
		quit(1, "Failed to create avalon write_thr");

	avalon_init(avalon);

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
		info->fan_pwm = AVALON_PWM_MAX;
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

	if (info->fan_pwm > opt_avalon_fan_max)
		info->fan_pwm = opt_avalon_fan_max;
}

static void temp_drop(struct avalon_info *info, int temp)
{
	if (temp <= opt_avalon_temp - AVALON_TEMP_HYSTERESIS * 3) {
		info->fan_pwm = opt_avalon_fan_min;
		return;
	}
	if (temp <= opt_avalon_temp - AVALON_TEMP_HYSTERESIS * 2)
		info->fan_pwm -= 10;
	else if (temp <= opt_avalon_temp - AVALON_TEMP_HYSTERESIS)
		info->fan_pwm -= 5;
	else if (temp < opt_avalon_temp)
		info->fan_pwm -= 1;

	if (info->fan_pwm < opt_avalon_fan_min)
		info->fan_pwm = opt_avalon_fan_min;
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
	if (info->temp_old <= opt_avalon_temp)
		info->optimal = true;
	else
		info->optimal = false;
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
	if (usb_ident(avalon) == IDENT_BTB) {
		info->core_voltage = bitburner_get_core_voltage(avalon);
	}
	if (info->temp_history_index == info->temp_history_count) {
		adjust_fan(info);
		info->temp_history_index = 0;
		info->temp_sum = 0;
	}
	if (unlikely(info->temp_old >= opt_avalon_overheat)) {
		applog(LOG_WARNING, "%s%d overheat! Idling", avalon->drv->name, avalon->device_id);
		info->overheat = true;
	} else if (info->overheat && info->temp_old <= opt_avalon_temp) {
		applog(LOG_WARNING, "%s%d cooled, restarting", avalon->drv->name, avalon->device_id);
		info->overheat = false;
	}
}

static void get_avalon_statline_before(char *buf, size_t bufsiz, struct cgpu_info *avalon)
{
	struct avalon_info *info = avalon->device_data;
	int lowfan = 10000;

	if (usb_ident(avalon) == IDENT_BTB) {
		int temp = info->temp0;
		if (info->temp2 > temp)
			temp = info->temp2;
		if (temp > 99)
			temp = 99;
		if (temp < 0)
			temp = 0;
		tailsprintf(buf, bufsiz, "%2dC %3d %4dmV | ", temp, info->frequency, info->core_voltage);
	} else {
		/* Find the lowest fan speed of the ASIC cooling fans. */
		if (info->fan1 >= 0 && info->fan1 < lowfan)
			lowfan = info->fan1;
		if (info->fan2 >= 0 && info->fan2 < lowfan)
			lowfan = info->fan2;

		tailsprintf(buf, bufsiz, "%2dC/%3dC %04dR | ", info->temp0, info->temp2, lowfan);
	}
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
	us_to_timeval(&tdiff, us_timeout);
	cgtime(&now);
	timeradd(&now, &tdiff, &then);
	timeval_to_spec(&abstime, &then);

	/* Wait until avalon_send_tasks signals us that it has completed
	 * sending its work or a full nonce range timeout has occurred */
	mutex_lock(&info->qlock);
	pthread_cond_timedwait(&info->qcond, &info->qlock, &abstime);
	mutex_unlock(&info->qlock);

	mutex_lock(&info->lock);
	hash_count = 0xffffffffull * (uint64_t)info->nonces;
	avalon->results += info->nonces + info->idle;
	if (avalon->results > miner_count)
		avalon->results = miner_count;
	if (!info->reset)
		avalon->results--;
	info->nonces = info->idle = 0;
	mutex_unlock(&info->lock);

	/* Check for nothing but consecutive bad results or consistently less
	 * results than we should be getting and reset the FPGA if necessary */
	if (usb_ident(avalon) != IDENT_BTB) {
		if (avalon->results < -miner_count && !info->reset) {
			applog(LOG_ERR, "%s%d: Result return rate low, resetting!",
				avalon->drv->name, avalon->device_id);
			info->reset = true;
		}
	}

	if (unlikely(avalon->usbinfo.nodev)) {
		applog(LOG_ERR, "%s%d: Device disappeared, shutting down thread",
		       avalon->drv->name, avalon->device_id);
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
	char buf[64];
	int i;
	double hwp = (cgpu->hw_errors + cgpu->diff1) ?
		     (double)(cgpu->hw_errors) / (double)(cgpu->hw_errors + cgpu->diff1) : 0;

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

	root = api_add_percent(root, "Device Hardware%", &hwp, true);
	root = api_add_int(root, "no_matching_work", &(info->no_matching_work), false);
	for (i = 0; i < info->miner_count; i++) {
		char mcw[24];

		sprintf(mcw, "match_work_count%d", i + 1);
		root = api_add_int(root, mcw, &(info->matching_work[i]), false);
	}
	if (usb_ident(cgpu) == IDENT_BTB) {
		root = api_add_int(root, "core_voltage", &(info->core_voltage), false);
		snprintf(buf, sizeof(buf), "%"PRIu8".%"PRIu8".%"PRIu8,
				info->version1, info->version2, info->version3);
		root = api_add_string(root, "version", buf, true);
	}
	root = api_add_uint32(root, "Controller Version", &(info->ctlr_ver), false);

	return root;
}

static void avalon_shutdown(struct thr_info *thr)
{
	do_avalon_close(thr);
}

static char *avalon_set_device(struct cgpu_info *avalon, char *option, char *setting, char *replybuf)
{
	int val;

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "freq: range %d-%d millivolts: range %d-%d",
					AVALON_MIN_FREQUENCY, AVALON_MAX_FREQUENCY,
					BITBURNER_MIN_COREMV, BITBURNER_MAX_COREMV);
		return replybuf;
	}

	if (strcasecmp(option, "millivolts") == 0 || strcasecmp(option, "mv") == 0) {
		if (usb_ident(avalon) != IDENT_BTB) {
			sprintf(replybuf, "%s cannot set millivolts", avalon->drv->name);
			return replybuf;
		}

		if (!setting || !*setting) {
			sprintf(replybuf, "missing millivolts setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < BITBURNER_MIN_COREMV || val > BITBURNER_MAX_COREMV) {
			sprintf(replybuf, "invalid millivolts: '%s' valid range %d-%d",
						setting, BITBURNER_MIN_COREMV, BITBURNER_MAX_COREMV);
			return replybuf;
		}

		if (bitburner_set_core_voltage(avalon, val))
			return NULL;
		else {
			sprintf(replybuf, "Set millivolts failed");
			return replybuf;
		}
	}

	if (strcasecmp(option, "freq") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing freq setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < AVALON_MIN_FREQUENCY || val > AVALON_MAX_FREQUENCY) {
			sprintf(replybuf, "invalid freq: '%s' valid range %d-%d",
						setting, AVALON_MIN_FREQUENCY, AVALON_MAX_FREQUENCY);
			return replybuf;
		}

		avalon_set_freq(avalon, val);
		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

struct device_drv avalon_drv = {
	.drv_id = DRIVER_avalon,
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
	.set_device = avalon_set_device,
	.reinit_device = avalon_init,
	.thread_shutdown = avalon_shutdown,
};
