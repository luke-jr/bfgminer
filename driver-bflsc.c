/*
 * Copyright 2013 Andrew Smith
 * Copyright 2013 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"

#ifdef WIN32
#include <windows.h>
#endif

#include "compat.h"
#include "miner.h"
#include "usbutils.h"

#define BLANK ""
#define LFSTR "<LF>"

/*
 * With Firmware 1.0.0 and a result queue of 20 the Max is:
 * header = 9
 * 64+1+32+1+1+(1+8)*8+1 per line = 172 * 20
 * OK = 3
 * Total: 3452
 */
#define BFLSC_BUFSIZ (0x1000)

#define BFLSC_DI_FIRMWARE "FIRMWARE"
#define BFLSC_DI_ENGINES "ENGINES"
#define BFLSC_DI_JOBSINQUE "JOBS IN QUEUE"
#define BFLSC_DI_XLINKMODE "XLINK MODE"
#define BFLSC_DI_XLINKPRESENT "XLINK PRESENT"
#define BFLSC_DI_DEVICESINCHAIN "DEVICES IN CHAIN"
#define BFLSC_DI_CHAINPRESENCE "CHAIN PRESENCE MASK"

#define FULLNONCE 0x100000000ULL

struct bflsc_dev {
	// Work
	unsigned int ms_work;
	int work_queued;
	int work_complete;
	int nonces_hw; // TODO: this - need to add a paramter to submit_nonce()
			// so can pass 'dev' to hw_error
	uint64_t hashes_unsent;
	uint64_t hashes_sent;
	uint64_t nonces_found;

	struct timeval last_check_result;
	struct timeval last_dev_result; // array > 0
	struct timeval last_nonce_result; // > 0 nonce

	// Info
	char getinfo[(BFLSC_BUFSIZ+4)*4];
	char *firmware;
	int engines; // each engine represents a 'thread' in a chip
	char *xlink_mode;
	char *xlink_present;

	// Status
	bool dead; // TODO: handle seperate x-link devices failing?
	bool overheat;

	// Stats
	float temp1;
	float temp2;
	float vcc1;
	float vcc2;
	float vmain;
	float temp1_max;
	float temp2_max;
	time_t temp1_max_time;
	time_t temp2_max_time;
	float temp1_5min_av; // TODO:
	float temp2_5min_av; // TODO:

	// To handle the fact that flushing the queue may not remove all work
	// (normally one item is still being processed)
	// and also that once the queue is flushed, results may still be in
	// the output queue - but we don't want to process them at the time of doing an LP
	// when result_id > flush_id+1, flushed work can be discarded since it
	// is no longer in the device
	uint64_t flush_id; // counter when results were last flushed
	uint64_t result_id; // counter when results were last checked
	bool flushed; // are any flushed?
};

// TODO: I stole cgpu_info.device_file
//  ... need to update miner.h to instead have a generic void *device_info = NULL;
//  ... and these structure definitions need to be in miner.h if API needs to see them
//  ... but then again maybe not - maybe another devinfo that the driver provides
//  However, clean up all that for all devices in miner.h ... miner.h is a mess at the moment
struct bflsc_info {
	pthread_rwlock_t stat_lock;
	struct thr_info results_thr;
	uint64_t hashes_sent;
	uint32_t update_count;
	struct timeval last_update;
	int sc_count;
	struct bflsc_dev *sc_devs;
	unsigned int scan_sleep_time;
	unsigned int results_sleep_time;
	unsigned int default_ms_work;
	bool shutdown;
	bool flash_led;
	bool not_first_work; // allow ignoring the first nonce error
};

#define BFLSC_XLINKHDR '@'
#define BFLSC_MAXPAYLOAD 255

struct DataForwardToChain {
	uint8_t header;
	uint8_t deviceAddress;
	uint8_t payloadSize;
	uint8_t payloadData[BFLSC_MAXPAYLOAD];
};

#define DATAFORWARDSIZE(data) (1 + 1 + 1 + data.payloadSize)

#define MIDSTATE_BYTES 32
#define MERKLE_OFFSET 64
#define MERKLE_BYTES 12
#define BFLSC_QJOBSIZ (MIDSTATE_BYTES+MERKLE_BYTES+1)
#define BFLSC_EOB 0xaa

struct QueueJobStructure {
	uint8_t payloadSize;
	uint8_t midState[MIDSTATE_BYTES];
	uint8_t blockData[MERKLE_BYTES];
	uint8_t endOfBlock;
};

#define QUE_RES_LINES_MIN 3
#define QUE_MIDSTATE 0
#define QUE_BLOCKDATA 1
#define QUE_NONCECOUNT 2
#define QUE_FLD_MIN 3
#define QUE_FLD_MAX 11

#define BFLSC_SIGNATURE 0xc1
#define BFLSC_EOW 0xfe

// N.B. this will only work with 5 jobs
// requires a different jobs[N] for each job count
// but really only need to handle 5 anyway
struct QueueJobPackStructure {
	uint8_t payloadSize;
	uint8_t signature;
	uint8_t jobsInArray;
	struct QueueJobStructure jobs[5];
	uint8_t endOfWrapper;
};

// TODO: Implement in API and also in usb device selection
struct SaveString {
	uint8_t payloadSize;
	uint8_t payloadData[BFLSC_MAXPAYLOAD];
};

// Commands
#define BFLSC_IDENTIFY "ZGX"
#define BFLSC_IDENTIFY_LEN (sizeof(BFLSC_IDENTIFY)-1)
#define BFLSC_DETAILS "ZCX"
#define BFLSC_DETAILS_LEN (sizeof(BFLSC_DETAILS)-1)
#define BFLSC_FIRMWARE "ZJX"
#define BFLSC_FIRMWARE_LEN (sizeof(BFLSC_FIRMWARE)-1)
#define BFLSC_FLASH "ZMX"
#define BFLSC_FLASH_LEN (sizeof(BFLSC_FLASH)-1)
#define BFLSC_VOLTAGE "ZTX"
#define BFLSC_VOLTAGE_LEN (sizeof(BFLSC_VOLTAGE)-1)
#define BFLSC_TEMPERATURE "ZLX"
#define BFLSC_TEMPERATURE_LEN (sizeof(BFLSC_TEMPERATURE)-1)
#define BFLSC_QJOB "ZNX"
#define BFLSC_QJOB_LEN (sizeof(BFLSC_QJOB)-1)
#define BFLSC_QJOBS "ZWX"
#define BFLSC_QJOBS_LEN (sizeof(BFLSC_QJOBS)-1)
#define BFLSC_QRES "ZOX"
#define BFLSC_QRES_LEN (sizeof(BFLSC_QRES)-1)
#define BFLSC_QFLUSH "ZQX"
#define BFLSC_QFLUSH_LEN (sizeof(BFLSC_QFLUSH)-1)
#define BFLSC_FANAUTO "Z5X"
#define BFLSC_FANOUT_LEN (sizeof(BFLSC_FANAUTO)-1)
#define BFLSC_FAN0 "Z0X"
#define BFLSC_FAN0_LEN (sizeof(BFLSC_FAN0)-1)
#define BFLSC_FAN1 "Z1X"
#define BFLSC_FAN1_LEN (sizeof(BFLSC_FAN1)-1)
#define BFLSC_FAN2 "Z2X"
#define BFLSC_FAN2_LEN (sizeof(BFLSC_FAN2)-1)
#define BFLSC_FAN3 "Z3X"
#define BFLSC_FAN3_LEN (sizeof(BFLSC_FAN3)-1)
#define BFLSC_FAN4 "Z4X"
#define BFLSC_FAN4_LEN (sizeof(BFLSC_FAN4)-1)
#define BFLSC_SAVESTR "ZSX"
#define BFLSC_SAVESTR_LEN (sizeof(BFLSC_SAVESTR)-1)
#define BFLSC_LOADSTR "ZUX"
#define BFLSC_LOADSTR_LEN (sizeof(BFLSC_LOADSTR)-1)

// Replies
#define BFLSC_IDENTITY "BitFORCE SC"
#define BFLSC_BFLSC "SHA256 SC"

#define BFLSC_OK "OK\n"
#define BFLSC_OK_LEN (sizeof(BFLSC_OK)-1)
#define BFLSC_SUCCESS "SUCCESS\n"
#define BFLSC_SUCCESS_LEN (sizeof(BFLSC_SUCCESS)-1)

#define BFLSC_RESULT "COUNT:"
#define BFLSC_RESULT_LEN (sizeof(BFLSC_RESULT)-1)

#define BFLSC_ANERR "ERR:"
#define BFLSC_ANERR_LEN (sizeof(BFLSC_ANERR)-1)
#define BFLSC_TIMEOUT BFLSC_ANERR "TIMEOUT"
#define BFLSC_TIMEOUT_LEN (sizeof(BFLSC_TIMEOUT)-1)
#define BFLSC_INVALID BFLSC_ANERR "INVALID DATA"
#define BFLSC_INVALID_LEN (sizeof(BFLSC_INVALID)-1)
#define BFLSC_ERRSIG BFLSC_ANERR "SIGNATURE"
#define BFLSC_ERRSIG_LEN (sizeof(BFLSC_ERRSIG)-1)
#define BFLSC_OKQ "OK:QUEUED"
#define BFLSC_OKQ_LEN (sizeof(BFLSC_OKQ)-1)
// Followed by N=1..5
#define BFLSC_OKQN "OK:QUEUED "
#define BFLSC_OKQN_LEN (sizeof(BFLSC_OKQN)-1)
#define BFLSC_QFULL "QUEUE FULL"
#define BFLSC_QFULL_LEN (sizeof(BFLSC_QFULL)-1)
#define BFLSC_HITEMP "HIGH TEMPERATURE RECOVERY"
#define BFLSC_HITEMP_LEN (sizeof(BFLSC_HITEMP)-1)
#define BFLSC_EMPTYSTR "MEMORY EMPTY"
#define BFLSC_EMPTYSTR_LEN (sizeof(BFLSC_EMPTYSTR)-1)

// Queued and non-queued are the same
#define FullNonceRangeJob QueueJobStructure
#define BFLSC_JOBSIZ BFLSC_QJOBSIZ

// Non queued commands
#define BFLSC_SENDWORK "ZDX"
#define BFLSC_SENDWORK_LEN (sizeof(BFLSC_SENDWORK)-1)

// Non queued commands (not used)
#define BFLSC_WORKSTATUS "ZFX"
#define BFLSC_WORKSTATUS_LEN (sizeof(BFLSC_WORKSTATUS)-1)
#define BFLSC_SENDRANGE "ZPX"
#define BFLSC_SENDRANGE_LEN (sizeof(BFLSC_SENDRANGE)-1)

// Non queued work replies (not used)
#define BFLSC_NONCE "NONCE-FOUND:"
#define BFLSC_NONCE_LEN (sizeof(BFLSC_NONCE)-1)
#define BFLSC_NO_NONCE "NO-NONCE"
#define BFLSC_NO_NONCE_LEN (sizeof(BFLSC_NO_NONCE)-1)
#define BFLSC_IDLE "IDLE"
#define BFLSC_IDLE_LEN (sizeof(BFLSC_IDLE)-1)
#define BFLSC_BUSY "BUSY"
#define BFLSC_BUSY_LEN (sizeof(BFLSC_BUSY)-1)

#define BFLSC_MINIRIG "BAM"
#define BFLSC_SINGLE "BAS"
#define BFLSC_LITTLESINGLE "BAL"
#define BFLSC_JALAPENO "BAJ"

// Default expected time for a nonce range
// - thus no need to check until this + last time work was found
// 60GH/s MiniRig (1 board) or Single
#define BAM_WORK_TIME 71.58
#define BAS_WORK_TIME 71.58
// 30GH/s Little Single
#define BAL_WORK_TIME 143.17
// 4.5GH/s Jalapeno
#define BAJ_WORK_TIME 954.44

// Defaults (slightly over half the work time) but ensure none are above 100
// SCAN_TIME - delay after sending work
// RES_TIME - delay between checking for results
#define BAM_SCAN_TIME 20
#define BAM_RES_TIME 2
#define BAS_SCAN_TIME 360
#define BAS_RES_TIME 36
#define BAL_SCAN_TIME 720
#define BAL_RES_TIME 72
#define BAJ_SCAN_TIME 1000
#define BAJ_RES_TIME 100
#define BFLSC_MAX_SLEEP 2000

#define BFLSC_TEMP_SLEEPMS 5
#define BFLSC_QUE_SIZE 20
#define BFLSC_QUE_FULL_ENOUGH 13
#define BFLSC_QUE_WATERMARK 6

// Must drop this far below cutoff before resuming work
#define BFLSC_TEMP_RECOVER 5

// If initialisation fails the first time,
// sleep this amount (ms) and try again
#define REINIT_TIME_FIRST_MS 100
// Max ms per sleep
#define REINIT_TIME_MAX_MS 800
// Keep trying up to this many us
#define REINIT_TIME_MAX 3000000

static const char *blank = "";

struct device_drv bflsc_drv;

static void xlinkstr(char *xlink, int dev, struct bflsc_info *sc_info)
{
	if (dev > 0)
		sprintf(xlink, " x-%d", dev);
	else {
		if (sc_info->sc_count > 1)
			strcpy(xlink, " master");
		else
			*xlink = '\0';
	}
}

static void bflsc_applog(struct cgpu_info *bflsc, int dev, enum usb_cmds cmd, int amount, int err)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	char xlink[17];

	xlinkstr(xlink, dev, sc_info);

	usb_applog(bflsc, cmd, xlink, amount, err);
}

// Break an input up into lines with LFs removed
// false means an error, but if *lines > 0 then data was also found
// error would be no data or missing LF at the end
static bool tolines(struct cgpu_info *bflsc, int dev, char *buf, int *lines, char ***items, enum usb_cmds cmd)
{
	bool ok = true;
	char *ptr;

#define p_lines (*lines)
#define p_items (*items)

	p_lines = 0;
	p_items = NULL;

	if (!buf || !(*buf)) {
		applog(LOG_DEBUG, "USB: %s%i: (%d) empty %s",
			bflsc->drv->name, bflsc->device_id, dev, usb_cmdname(cmd));
		return false;
	}

	ptr = strdup(buf);
	while (ptr && *ptr) {
		p_items = realloc(p_items, ++p_lines * sizeof(*p_items));
		if (unlikely(!p_items))
			quit(1, "Failed to realloc p_items in tolines");
		p_items[p_lines-1] = ptr;
		ptr = strchr(ptr, '\n');
		if (ptr)
			*(ptr++) = '\0';
		else {
			if (ok) {
				applog(LOG_DEBUG, "USB: %s%i: (%d) missing lf(s) in %s",
					bflsc->drv->name, bflsc->device_id, dev, usb_cmdname(cmd));
			}
			ok = false;
		}
	}

	return ok;
}

static void freetolines(int *lines, char ***items)
{
	if (*lines > 0) {
		free(**items);
		free(*items);
	}
	*lines = 0;
	*items = NULL;
}

enum breakmode {
	NOCOLON,
	ONECOLON,
	ALLCOLON // Temperature uses this
};

// Break down a single line into 'fields'
// 'lf' will be a pointer to the final LF if it is there (or NULL)
// firstname will be the allocated buf copy pointer which is also
//  the string before ':' for ONECOLON and ALLCOLON
// If any string is missing the ':' when it was expected, false is returned
static bool breakdown(enum breakmode mode, char *buf, int *count, char **firstname, char ***fields, char **lf)
{
	char *ptr, *colon, *comma;
	bool ok;

#define p_count (*count)
#define p_firstname (*firstname)
#define p_fields (*fields)
#define p_lf (*lf)

	p_count = 0;
	p_firstname = NULL;
	p_fields = NULL;
	p_lf = NULL;

	if (!buf || !(*buf))
		return false;

	ptr = p_firstname = strdup(buf);
	p_lf = strchr(p_firstname, '\n');
	if (mode == ONECOLON) {
		colon = strchr(ptr, ':');
		if (colon) {
			ptr = colon;
			*(ptr++) = '\0';
		} else
			ok = false;
	}

	while (*ptr == ' ')
		ptr++;

	ok = true;
	while (ptr && *ptr) {
		if (mode == ALLCOLON) {
			colon = strchr(ptr, ':');
			if (colon)
				ptr = colon + 1;
			else
				ok = false;
		}
		while (*ptr == ' ')
			ptr++;
		comma = strchr(ptr, ',');
		if (comma)
			*(comma++) = '\0';
		p_fields = realloc(p_fields, ++p_count * sizeof(*p_fields));
		if (unlikely(!p_fields))
			quit(1, "Failed to realloc p_fields in breakdown");
		p_fields[p_count-1] = ptr;
		ptr = comma;
	}

	return ok;
}

static void freebreakdown(int *count, char **firstname, char ***fields)
{
	if (*firstname)
		free(*firstname);
	if (*count > 0)
		free(*fields);
	*count = 0;
	*firstname = NULL;
	*fields = NULL;
}

static int write_to_dev(struct cgpu_info *bflsc, int dev, char *buf, int buflen, int *amount, enum usb_cmds cmd)
{
	struct DataForwardToChain data;
	int len;

	if (dev == 0)
		return usb_write(bflsc, buf, buflen, amount, cmd);

	data.header = BFLSC_XLINKHDR;
	data.deviceAddress = (uint8_t)dev;
	data.payloadSize = buflen;
	memcpy(data.payloadData, buf, buflen);
	len = DATAFORWARDSIZE(data);

	// TODO: handle xlink timeout message - here or at call?
	return usb_write(bflsc, (char *)&data, len, amount, cmd);
}

static bool getok(struct cgpu_info *bflsc, enum usb_cmds cmd, int *err, int *amount)
{
	char buf[BFLSC_BUFSIZ+1];

	*err = usb_ftdi_read_nl(bflsc, buf, sizeof(buf)-1, amount, cmd);
	if (*err < 0 || *amount < (int)BFLSC_OK_LEN)
		return false;
	else
		return true;
}

static bool getokerr(struct cgpu_info *bflsc, enum usb_cmds cmd, int *err, int *amount, char *buf, size_t bufsiz)
{
	*err = usb_ftdi_read_nl(bflsc, buf, bufsiz-1, amount, cmd);
	if (*err < 0 || *amount < (int)BFLSC_OK_LEN)
		return false;
	else {
		if (*amount > (int)BFLSC_ANERR_LEN && strncmp(buf, BFLSC_ANERR, BFLSC_ANERR_LEN) == 0)
			return false;
		else
			return true;
	}
}

static void bflsc_send_flush_work(struct cgpu_info *bflsc, int dev)
{
	int err, amount;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return;

	mutex_lock(&bflsc->device_mutex);

	err = write_to_dev(bflsc, dev, BFLSC_QFLUSH, BFLSC_QFLUSH_LEN, &amount, C_QUEFLUSH);
	if (err < 0 || amount != BFLSC_QFLUSH_LEN) {
		mutex_unlock(&bflsc->device_mutex);
		bflsc_applog(bflsc, dev, C_QUEFLUSH, amount, err);
	} else {
		// TODO: do we care if we don't get 'OK'? (always will in normal processing)
		err = getok(bflsc, C_QUEFLUSHREPLY, &err, &amount);
		mutex_unlock(&bflsc->device_mutex);
		// TODO: report an error if not 'OK' ?
	}
}

/* return True = attempted usb_ftdi_read_ok()
 * set ignore to true means no applog/ignore errors */
static bool bflsc_qres(struct cgpu_info *bflsc, char *buf, size_t bufsiz, int dev, int *err, int *amount, bool ignore)
{
	bool readok = false;

	mutex_lock(&(bflsc->device_mutex));

	*err = write_to_dev(bflsc, dev, BFLSC_QRES, BFLSC_QRES_LEN, amount, C_REQUESTRESULTS);
	if (*err < 0 || *amount != BFLSC_QRES_LEN) {
		mutex_unlock(&(bflsc->device_mutex));
		if (!ignore)
			bflsc_applog(bflsc, dev, C_REQUESTRESULTS, *amount, *err);

		// TODO: do what? flag as dead device?
		// count how many times it has happened and reset/fail it
		// or even make sure it is all x-link and that means device
		// has failed after some limit of this?
		// of course all other I/O must also be failing ...
	} else {
		readok = true;
		*err = usb_ftdi_read_ok(bflsc, buf, bufsiz-1, amount, C_GETRESULTS);
		mutex_unlock(&(bflsc->device_mutex));

		if (*err < 0 || *amount < 1) {
			if (!ignore)
				bflsc_applog(bflsc, dev, C_GETRESULTS, *amount, *err);

			// TODO: do what? ... see above
		}
	}

	return readok;
}

static void __bflsc_initialise(struct cgpu_info *bflsc)
{
	int err;

// TODO: this is a standard BFL FPGA Initialisation
// it probably will need changing ...
// TODO: does x-link bypass the other device FTDI? (I think it does)
//	So no initialisation required except for the master device?

	if (bflsc->usbinfo.nodev)
		return;

	// Reset
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
				FTDI_VALUE_RESET, bflsc->usbdev->found->interface, C_RESET);

	applog(LOG_DEBUG, "%s%i: reset got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Set data control
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_DATA,
				FTDI_VALUE_DATA, bflsc->usbdev->found->interface, C_SETDATA);

	applog(LOG_DEBUG, "%s%i: setdata got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Set the baud
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, FTDI_VALUE_BAUD,
				(FTDI_INDEX_BAUD & 0xff00) | bflsc->usbdev->found->interface,
				C_SETBAUD);

	applog(LOG_DEBUG, "%s%i: setbaud got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
				FTDI_VALUE_FLOW, bflsc->usbdev->found->interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Set Modem Control
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
				FTDI_VALUE_MODEM, bflsc->usbdev->found->interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Clear any sent data
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
				FTDI_VALUE_PURGE_TX, bflsc->usbdev->found->interface, C_PURGETX);

	applog(LOG_DEBUG, "%s%i: purgetx got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Clear any received data
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
				FTDI_VALUE_PURGE_RX, bflsc->usbdev->found->interface, C_PURGERX);

	applog(LOG_DEBUG, "%s%i: purgerx got err %d",
		bflsc->drv->name, bflsc->device_id, err);

}

static void bflsc_initialise(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	char buf[BFLSC_BUFSIZ+1];
	int err, amount;
	int dev;

	mutex_lock(&(bflsc->device_mutex));
	__bflsc_initialise(bflsc);
	mutex_unlock(&(bflsc->device_mutex));

	for (dev = 0; dev < sc_info->sc_count; dev++) {
		bflsc_send_flush_work(bflsc, dev);
		bflsc_qres(bflsc, buf, sizeof(buf), dev, &err, &amount, true);
	}
}

static bool getinfo(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	struct bflsc_dev sc_dev;
	char buf[BFLSC_BUFSIZ+1];
	int err, amount;
	char **items, *firstname, **fields, *lf;
	int i, lines, count;
	bool res, ok;
	char *tmp;

	/*
	 * Kano's first dev Jalapeno output:
	 * DEVICE: BitFORCE SC<LF>
	 * FIRMWARE: 1.0.0<LF>
	 * ENGINES: 30<LF>
	 * FREQUENCY: [UNKNOWN]<LF>
	 * XLINK MODE: MASTER<LF>
	 * XLINK PRESENT: YES<LF>
	 * --DEVICES IN CHAIN: 0<LF>
	 * --CHAIN PRESENCE MASK: 00000000<LF>
	 * OK<LF>
	 */

	// TODO: if dev is ever > 0 must handle xlink timeout message
	err = write_to_dev(bflsc, dev, BFLSC_DETAILS, BFLSC_DETAILS_LEN, &amount, C_REQUESTDETAILS);
	if (err < 0 || amount != BFLSC_DETAILS_LEN) {
		applog(LOG_ERR, "%s detect (%s) send details request failed (%d:%d)",
			bflsc->drv->dname, bflsc->device_path, amount, err);
		return false;
	}

	err = usb_ftdi_read_ok(bflsc, buf, sizeof(buf)-1, &amount, C_GETDETAILS);
	if (err < 0 || amount < 1) {
		if (err < 0) {
			applog(LOG_ERR, "%s detect (%s) get details return invalid/timed out (%d:%d)",
					bflsc->drv->dname, bflsc->device_path, amount, err);
		} else {
			applog(LOG_ERR, "%s detect (%s) get details returned nothing (%d:%d)",
					bflsc->drv->dname, bflsc->device_path, amount, err);
		}
		return false;
	}

	memset(&sc_dev, 0, sizeof(struct bflsc_dev));
	sc_info->sc_count = 1;
	res = tolines(bflsc, dev, &(buf[0]), &lines, &items, C_GETDETAILS);
	if (!res)
		return false;

	tmp = str_text(buf);
	strcpy(sc_dev.getinfo, tmp);
	free(tmp);

	for (i = 0; i < lines-2; i++) {
		res = breakdown(ONECOLON, items[i], &count, &firstname, &fields, &lf);
		if (lf)
			*lf = '\0';
		if (!res || count != 1) {
			tmp = str_text(items[i]);
			applog(LOG_WARNING, "%s detect (%s) invalid details line: '%s' %d",
					bflsc->drv->dname, bflsc->device_path, tmp, count);
			free(tmp);
			dev_error(bflsc, REASON_DEV_COMMS_ERROR);
			goto mata;
		}
		if (strcmp(firstname, BFLSC_DI_FIRMWARE) == 0) {
			sc_dev.firmware = strdup(fields[0]);
			if (strcmp(sc_dev.firmware, "1.0.0")) {
				tmp = str_text(items[i]);
				applog(LOG_WARNING, "%s detect (%s) Warning unknown firmware '%s'",
					bflsc->drv->dname, bflsc->device_path, tmp);
				free(tmp);
			}
		}
		else if (strcmp(firstname, BFLSC_DI_ENGINES) == 0) {
			sc_dev.engines = atoi(fields[0]);
			if (sc_dev.engines < 1) {
				tmp = str_text(items[i]);
				applog(LOG_WARNING, "%s detect (%s) invalid engine count: '%s'",
					bflsc->drv->dname, bflsc->device_path, tmp);
				free(tmp);
				goto mata;
			}
		}
		else if (strcmp(firstname, BFLSC_DI_XLINKMODE) == 0)
			sc_dev.xlink_mode = strdup(fields[0]);
		else if (strcmp(firstname, BFLSC_DI_XLINKPRESENT) == 0)
			sc_dev.xlink_present = strdup(fields[0]);
		else if (strcmp(firstname, BFLSC_DI_DEVICESINCHAIN) == 0) {
			sc_info->sc_count = atoi(fields[0]) + 1;
			if (sc_info->sc_count < 1 || sc_info->sc_count > 30) {
				tmp = str_text(items[i]);
				applog(LOG_WARNING, "%s detect (%s) invalid s-link count: '%s'",
					bflsc->drv->dname, bflsc->device_path, tmp);
				free(tmp);
				goto mata;
			}
		}
		freebreakdown(&count, &firstname, &fields);
	}

	sc_info->sc_devs = calloc(sc_info->sc_count, sizeof(struct bflsc_dev));
	if (unlikely(!sc_info->sc_devs))
		quit(1, "Failed to calloc in getinfo");
	memcpy(&(sc_info->sc_devs[0]), &sc_dev, sizeof(sc_dev));
	// TODO: do we care about getting this info for the rest if > 0 x-link

	ok = true;
	goto ne;

mata:
	freebreakdown(&count, &firstname, &fields);
	ok = false;
ne:
	freetolines(&lines, &items);
	return ok;
}

static bool bflsc_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct bflsc_info *sc_info = NULL;
	char buf[BFLSC_BUFSIZ+1];
	char devpath[20];
	int i, err, amount;
	struct timeval init_start, init_now;
	int init_sleep, init_count;
	bool ident_first;
	char *newname;

	struct cgpu_info *bflsc = calloc(1, sizeof(*bflsc));

	if (unlikely(!bflsc))
		quit(1, "Failed to calloc bflsc in bflsc_detect_one");
	bflsc->drv = &bflsc_drv;
	bflsc->deven = DEV_ENABLED;
	bflsc->threads = 1;

	sc_info = calloc(1, sizeof(*sc_info));
	if (unlikely(!sc_info))
		quit(1, "Failed to calloc sc_info in bflsc_detect_one");
	// TODO: fix ... everywhere ...
	bflsc->device_file = (FILE *)sc_info;

	if (!usb_init(bflsc, dev, found))
		goto shin;

	sprintf(devpath, "%d:%d",
			(int)(bflsc->usbinfo.bus_number),
			(int)(bflsc->usbinfo.device_address));


	// Allow 2 complete attempts if the 1st time returns an unrecognised reply
	ident_first = true;
retry:
	init_count = 0;
	init_sleep = REINIT_TIME_FIRST_MS;
	cgtime(&init_start);
reinit:
	__bflsc_initialise(bflsc);
	err = write_to_dev(bflsc, 0, BFLSC_IDENTIFY, BFLSC_IDENTIFY_LEN, &amount, C_REQUESTIDENTIFY);
	if (err < 0 || amount != BFLSC_IDENTIFY_LEN) {
		applog(LOG_ERR, "%s detect (%s) send identify request failed (%d:%d)",
			bflsc->drv->dname, devpath, amount, err);
		goto unshin;
	}

	err = usb_ftdi_read_nl(bflsc, buf, sizeof(buf)-1, &amount, C_GETIDENTIFY);
	if (err < 0 || amount < 1) {
		init_count++;
		cgtime(&init_now);
		if (us_tdiff(&init_now, &init_start) <= REINIT_TIME_MAX) {
			if (init_count == 2) {
				applog(LOG_WARNING, "%s detect (%s) 2nd init failed (%d:%d) - retrying",
					bflsc->drv->dname, devpath, amount, err);
			}
			nmsleep(init_sleep);
			if ((init_sleep * 2) <= REINIT_TIME_MAX_MS)
				init_sleep *= 2;
			goto reinit;
		}

		if (init_count > 0)
			applog(LOG_WARNING, "%s detect (%s) init failed %d times %.2fs",
				bflsc->drv->dname, devpath, init_count, tdiff(&init_now, &init_start));

		if (err < 0) {
			applog(LOG_ERR, "%s detect (%s) error identify reply (%d:%d)",
				bflsc->drv->dname, devpath, amount, err);
		} else {
			applog(LOG_ERR, "%s detect (%s) empty identify reply (%d)",
				bflsc->drv->dname, devpath, amount);
		}

		goto unshin;
	}
	buf[amount] = '\0';

	if (unlikely(!strstr(buf, BFLSC_BFLSC))) {
		applog(LOG_DEBUG, "%s detect (%s) found an FPGA '%s' ignoring",
			bflsc->drv->dname, devpath, buf);
		goto unshin;
	}

	if (unlikely(strstr(buf, BFLSC_IDENTITY))) {
		if (ident_first) {
			applog(LOG_DEBUG, "%s detect (%s) didn't recognise '%s' trying again ...",
				bflsc->drv->dname, devpath, buf);
			ident_first = false;
			goto retry;
		}
		applog(LOG_DEBUG, "%s detect (%s) didn't recognise '%s' on 2nd attempt",
			bflsc->drv->dname, devpath, buf);
		goto unshin;
	}

	bflsc->device_path = strdup(devpath);

	if (!getinfo(bflsc, 0))
		goto unshin;

	sc_info->scan_sleep_time = BAS_SCAN_TIME;
	sc_info->results_sleep_time = BAS_RES_TIME;
	sc_info->default_ms_work = BAS_WORK_TIME;

	/* When getinfo() "FREQUENCY: [UNKNOWN]" is fixed -
	 * use 'freq * engines' to estimate.
	 * Otherwise for now: */
	newname = NULL;
	if (sc_info->sc_count > 1) {
		newname = BFLSC_MINIRIG;
		sc_info->scan_sleep_time = BAM_SCAN_TIME;
		sc_info->results_sleep_time = BAM_RES_TIME;
		sc_info->default_ms_work = BAM_WORK_TIME;
	} else {
		if (sc_info->sc_devs[0].engines < 34) { // 16 * 2 + 2
			newname = BFLSC_JALAPENO;
			sc_info->scan_sleep_time = BAJ_SCAN_TIME;
			sc_info->results_sleep_time = BAJ_RES_TIME;
			sc_info->default_ms_work = BAJ_WORK_TIME;
		} else if (sc_info->sc_devs[0].engines < 130)  { // 16 * 8 + 2
			newname = BFLSC_LITTLESINGLE;
			sc_info->scan_sleep_time = BAL_SCAN_TIME;
			sc_info->results_sleep_time = BAL_RES_TIME;
			sc_info->default_ms_work = BAL_WORK_TIME;
		}
	}

	for (i = 0; i < sc_info->sc_count; i++)
		sc_info->sc_devs[i].ms_work = sc_info->default_ms_work;

	if (newname) {
		if (!bflsc->drv->copy)
			bflsc->drv = copy_drv(bflsc->drv);
		bflsc->drv->name = newname;
	}

	// We have a real BFLSC!
	applog(LOG_DEBUG, "%s (%s) identified as: '%s'",
		bflsc->drv->dname, devpath, bflsc->drv->name);

	if (!add_cgpu(bflsc))
		goto unshin;

	update_usb_stats(bflsc);

	mutex_init(&bflsc->device_mutex);
	rwlock_init(&sc_info->stat_lock);

	return true;

unshin:

	usb_uninit(bflsc);

shin:

	free(bflsc->device_path);
	free(bflsc->device_file);

	if (bflsc->name != blank)
		free(bflsc->name);

	if (bflsc->drv->copy)
		free(bflsc->drv);

	free(bflsc);

	return false;
}

static void bflsc_detect(void)
{
	usb_detect(&bflsc_drv, bflsc_detect_one);
}

static void get_bflsc_statline_before(char *buf, struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	float temp = 0;
	float vcc1 = 0;
	int i;

	rd_lock(&(sc_info->stat_lock));
	for (i = 0; i < sc_info->sc_count; i++) {
		if (sc_info->sc_devs[i].temp1 > temp)
			temp = sc_info->sc_devs[i].temp1;
		if (sc_info->sc_devs[i].temp2 > temp)
			temp = sc_info->sc_devs[i].temp2;
		if (sc_info->sc_devs[i].vcc1 > vcc1)
			vcc1 = sc_info->sc_devs[i].vcc1;
	}
	rd_unlock(&(sc_info->stat_lock));

	tailsprintf(buf, " max%3.0fC %4.2fV | ", temp, vcc1);
}

static void flush_one_dev(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	struct work *work, *tmp;
	bool did = false;

	bflsc_send_flush_work(bflsc, dev);

	rd_lock(&bflsc->qlock);

	HASH_ITER(hh, bflsc->queued_work, work, tmp) {
		if (work->queued && work->subid == dev) {
			// devflag is used to flag stale work
			work->devflag = true;
			did = true;
		}
	}

	rd_unlock(&bflsc->qlock);

	if (did) {
		wr_lock(&(sc_info->stat_lock));
		sc_info->sc_devs[dev].flushed = true;
		sc_info->sc_devs[dev].flush_id = sc_info->sc_devs[dev].result_id;
		sc_info->sc_devs[dev].work_queued = 0;
		wr_unlock(&(sc_info->stat_lock));
	}
}

static void bflsc_flush_work(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	int dev;

	for (dev = 0; dev < sc_info->sc_count; dev++)
		flush_one_dev(bflsc, dev);
}

static void bflsc_flash_led(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	int err, amount;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return;

	// It is not critical flashing the led so don't get stuck if we
	// can't grab the mutex now
	if (mutex_trylock(&bflsc->device_mutex))
		return;

	err = write_to_dev(bflsc, dev, BFLSC_FLASH, BFLSC_FLASH_LEN, &amount, C_REQUESTFLASH);
	if (err < 0 || amount != BFLSC_FLASH_LEN) {
		mutex_unlock(&(bflsc->device_mutex));
		bflsc_applog(bflsc, dev, C_REQUESTFLASH, amount, err);
	} else {
		getok(bflsc, C_FLASHREPLY, &err, &amount);

		mutex_unlock(&(bflsc->device_mutex));
	}

	// Once we've tried - don't do it until told to again
	// - even if it failed
	sc_info->flash_led = false;

	return;
}

static bool bflsc_get_temp(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	struct bflsc_dev *sc_dev;
	char temp_buf[BFLSC_BUFSIZ+1];
	char volt_buf[BFLSC_BUFSIZ+1];
	char *tmp;
	int err, amount;
	char *firstname, **fields, *lf;
	char xlink[17];
	int count;
	bool res;
	float temp, temp1, temp2;
	float vcc1, vcc2, vmain;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return false;

	if (dev >= sc_info->sc_count) {
		applog(LOG_ERR, "%s%i: temp invalid xlink device %d - limit %d",
			bflsc->drv->name, bflsc->device_id, dev, sc_info->sc_count - 1);
		return false;
	}

	// Flash instead of Temp
	if (sc_info->flash_led) {
		bflsc_flash_led(bflsc, dev);
		return true;
	}

	/* It is not very critical getting temp so don't get stuck if we
	 * can't grab the mutex here */
	if (mutex_trylock(&bflsc->device_mutex))
		return false;

	xlinkstr(&(xlink[0]), dev, sc_info);

	err = write_to_dev(bflsc, dev, BFLSC_TEMPERATURE, BFLSC_TEMPERATURE_LEN, &amount, C_REQUESTTEMPERATURE);
	if (err < 0 || amount != BFLSC_TEMPERATURE_LEN) {
		mutex_unlock(&(bflsc->device_mutex));
		applog(LOG_ERR, "%s%i: Error: Request%s temp invalid/timed out (%d:%d)",
				bflsc->drv->name, bflsc->device_id, xlink, amount, err);
		return false;
	}

	err = usb_ftdi_read_nl(bflsc, temp_buf, sizeof(temp_buf)-1, &amount, C_GETTEMPERATURE);
	if (err < 0 || amount < 1) {
		mutex_unlock(&(bflsc->device_mutex));
		if (err < 0) {
			applog(LOG_ERR, "%s%i: Error: Get%s temp return invalid/timed out (%d:%d)",
					bflsc->drv->name, bflsc->device_id, xlink, amount, err);
		} else {
			applog(LOG_ERR, "%s%i: Error: Get%s temp returned nothing (%d:%d)",
					bflsc->drv->name, bflsc->device_id, xlink, amount, err);
		}
		return false;
	}

	// N.B. we only get the voltages if the temp succeeds - temp is the important one
	err = write_to_dev(bflsc, dev, BFLSC_VOLTAGE, BFLSC_VOLTAGE_LEN, &amount, C_REQUESTVOLTS);
	if (err < 0 || amount != BFLSC_VOLTAGE_LEN) {
		mutex_unlock(&(bflsc->device_mutex));
		applog(LOG_ERR, "%s%i: Error: Request%s volts invalid/timed out (%d:%d)",
				bflsc->drv->name, bflsc->device_id, xlink, amount, err);
		return false;
	}

	err = usb_ftdi_read_nl(bflsc, volt_buf, sizeof(volt_buf)-1, &amount, C_GETTEMPERATURE);
	if (err < 0 || amount < 1) {
		mutex_unlock(&(bflsc->device_mutex));
		if (err < 0) {
			applog(LOG_ERR, "%s%i: Error: Get%s temp return invalid/timed out (%d:%d)",
					bflsc->drv->name, bflsc->device_id, xlink, amount, err);
		} else {
			applog(LOG_ERR, "%s%i: Error: Get%s temp returned nothing (%d:%d)",
					bflsc->drv->name, bflsc->device_id, xlink, amount, err);
		}
		return false;
	}

	mutex_unlock(&(bflsc->device_mutex));
	
	res = breakdown(ALLCOLON, temp_buf, &count, &firstname, &fields, &lf);
	if (lf)
		*lf = '\0';
	if (!res || count != 2 || !lf) {
		tmp = str_text(temp_buf);
		applog(LOG_WARNING, "%s%i: Invalid%s temp reply: '%s'",
				bflsc->drv->name, bflsc->device_id, xlink, tmp);
		free(tmp);
		freebreakdown(&count, &firstname, &fields);
		dev_error(bflsc, REASON_DEV_COMMS_ERROR);
		return false;
	}

	temp = temp1 = (float)atoi(fields[0]);
	temp2 = (float)atoi(fields[1]);

	res = breakdown(NOCOLON, volt_buf, &count, &firstname, &fields, &lf);
	if (lf)
		*lf = '\0';
	if (!res || count != 3 || !lf) {
		tmp = str_text(volt_buf);
		applog(LOG_WARNING, "%s%i: Invalid%s volt reply: '%s'",
				bflsc->drv->name, bflsc->device_id, xlink, tmp);
		free(tmp);
		freebreakdown(&count, &firstname, &fields);
		dev_error(bflsc, REASON_DEV_COMMS_ERROR);
		return false;
	}

	sc_dev = &sc_info->sc_devs[dev];
	vcc1 = (float)atoi(fields[0]) / 1000.0;
	vcc2 = (float)atoi(fields[1]) / 1000.0;
	vmain = (float)atoi(fields[2]) / 1000.0;
	if (vcc1 > 0 || vcc2 > 0 || vmain > 0) {
		wr_lock(&(sc_info->stat_lock));
		if (vcc1 > 0) {
			if (unlikely(sc_dev->vcc1 == 0))
				sc_dev->vcc1 = vcc1;
			else {
				sc_dev->vcc1 += vcc1 * 0.63;
				sc_dev->vcc1 /= 1.63;
			}
		}
		if (vcc2 > 0) {
			if (unlikely(sc_dev->vcc2 == 0))
				sc_dev->vcc2 = vcc2;
			else {
				sc_dev->vcc2 += vcc2 * 0.63;
				sc_dev->vcc2 /= 1.63;
			}
		}
		if (vmain > 0) {
			if (unlikely(sc_dev->vmain == 0))
				sc_dev->vmain = vmain;
			else {
				sc_dev->vmain += vmain * 0.63;
				sc_dev->vmain /= 1.63;
			}
		}
		wr_unlock(&(sc_info->stat_lock));
	}

	if (temp1 > 0 || temp2 > 0) {
		wr_lock(&(sc_info->stat_lock));
		if (unlikely(!sc_dev->temp1))
			sc_dev->temp1 = temp1;
		else {
			sc_dev->temp1 += temp1 * 0.63;
			sc_dev->temp1 /= 1.63;
		}
		if (unlikely(!sc_dev->temp2))
			sc_dev->temp2 = temp2;
		else {
			sc_dev->temp2 += temp2 * 0.63;
			sc_dev->temp2 /= 1.63;
		}
		if (temp1 > sc_dev->temp1_max) {
			sc_dev->temp1_max = temp1;
			sc_dev->temp1_max_time = time(NULL);
		}
		if (temp2 > sc_dev->temp2_max) {
			sc_dev->temp2_max = temp2;
			sc_dev->temp2_max_time = time(NULL);
		}

		if (unlikely(sc_dev->temp1_5min_av == 0))
			sc_dev->temp1_5min_av = temp1;
		else {
			sc_dev->temp1_5min_av += temp1 * .0042;
			sc_dev->temp1_5min_av /= 1.0042;
		}
		if (unlikely(sc_dev->temp2_5min_av == 0))
			sc_dev->temp2_5min_av = temp2;
		else {
			sc_dev->temp2_5min_av += temp2 * .0042;
			sc_dev->temp2_5min_av /= 1.0042;
		}
		wr_unlock(&(sc_info->stat_lock));

		if (temp < temp2)
			temp = temp2;

		bflsc->temp = temp;

		if (bflsc->cutofftemp > 0 && temp > bflsc->cutofftemp) {
			applog(LOG_WARNING, "%s%i:%s temp (%.1f) hit thermal cutoff limit %d, stopping work!",
						bflsc->drv->name, bflsc->device_id, xlink,
						temp, bflsc->cutofftemp);
			dev_error(bflsc, REASON_DEV_THERMAL_CUTOFF);
			sc_dev->overheat = true;
			flush_one_dev(bflsc, dev);
			return false;
		}

		if (bflsc->cutofftemp > 0 && temp < (bflsc->cutofftemp - BFLSC_TEMP_RECOVER))
			sc_dev->overheat = false;
	}

	freebreakdown(&count, &firstname, &fields);
	return true;
}

static void process_nonces(struct cgpu_info *bflsc, int dev, char *xlink, char *data, int count, char **fields, int *nonces)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	char midstate[MIDSTATE_BYTES], blockdata[MERKLE_BYTES];
	struct work *work;
	uint32_t nonce;
	int i, num;
	bool res;
	char *tmp;

	if (count < QUE_FLD_MIN) {
		tmp = str_text(data);
		applog(LOG_ERR, "%s%i:%s work returned too small (%d,%s)",
				bflsc->drv->name, bflsc->device_id, xlink, count, tmp);
		free(tmp);
		inc_hw_errors(bflsc->thr[0]);
		return;
	}

	if (count > QUE_FLD_MAX) {
		applog(LOG_ERR, "%s%i:%s work returned too large (%d) processing %d anyway",
				bflsc->drv->name, bflsc->device_id, xlink, count, QUE_FLD_MAX);
		count = QUE_FLD_MAX;
		inc_hw_errors(bflsc->thr[0]);
	}

	num = atoi(fields[QUE_NONCECOUNT]);
	if (num != count - QUE_FLD_MIN) {
		tmp = str_text(data);
		applog(LOG_ERR, "%s%i:%s incorrect data count (%d) will use %d instead from (%s)",
				bflsc->drv->name, bflsc->device_id, xlink, num, count - QUE_FLD_MAX, tmp);
		free(tmp);
		inc_hw_errors(bflsc->thr[0]);
	}

	memset(midstate, 0, MIDSTATE_BYTES);
	memset(blockdata, 0, MERKLE_BYTES);
	hex2bin((unsigned char *)midstate, fields[QUE_MIDSTATE], MIDSTATE_BYTES);
	hex2bin((unsigned char *)blockdata, fields[QUE_BLOCKDATA], MERKLE_BYTES);

	work = find_queued_work_bymidstate(bflsc, midstate, MIDSTATE_BYTES,
						blockdata, MERKLE_OFFSET, MERKLE_BYTES);
	if (!work) {
		if (sc_info->not_first_work) {
			applog(LOG_ERR, "%s%i:%s failed to find nonce work - can't be processed - ignored",
					bflsc->drv->name, bflsc->device_id, xlink);
					inc_hw_errors(bflsc->thr[0]);
		}
		return;
	}

	res = false;
	for (i = QUE_FLD_MIN; i < count; i++) {
		if (strlen(fields[i]) != 8) {
			tmp = str_text(data);
			applog(LOG_ERR, "%s%i:%s invalid nonce (%s) will try to process anyway",
					bflsc->drv->name, bflsc->device_id, xlink, tmp);
			free(tmp);
		}

		hex2bin((void*)&nonce, fields[i], 4);
		nonce = htobe32(nonce);
		wr_lock(&(sc_info->stat_lock));
		sc_info->sc_devs[dev].nonces_found++;
		wr_unlock(&(sc_info->stat_lock));

		submit_nonce(bflsc->thr[0], work, nonce);
		(*nonces)++;
		res = true;
	}

	wr_lock(&(sc_info->stat_lock));
	if (res)
		sc_info->sc_devs[dev].result_id++;
	sc_info->sc_devs[dev].work_complete++;
	sc_info->sc_devs[dev].hashes_unsent += FULLNONCE;
	// If not flushed (stale)
	if (!(work->devflag))
		sc_info->sc_devs[dev].work_queued -= 1;
	wr_unlock(&(sc_info->stat_lock));

	work_completed(bflsc, work);
}

static int process_results(struct cgpu_info *bflsc, int dev, char *buf, int *nonces)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	char **items, *firstname, **fields, *lf;
	int que, i, lines, count;
	char xlink[17];
	char *tmp, *tmp2;

	*nonces = 0;

	xlinkstr(&(xlink[0]), dev, sc_info);

	tolines(bflsc, dev, buf, &lines, &items, C_GETRESULTS);
	if (lines < 1) {
		tmp = str_text(buf);
		applog(LOG_ERR, "%s%i:%s empty result (%s) ignored",
					bflsc->drv->name, bflsc->device_id, xlink, tmp);
		free(tmp);
		que = 0;
		goto arigatou;
	}

	if (lines < QUE_RES_LINES_MIN) {
		tmp = str_text(buf);
		applog(LOG_ERR, "%s%i:%s result too small (%s) ignored",
					bflsc->drv->name, bflsc->device_id, xlink, tmp);
		free(tmp);
		que = 0;
		goto arigatou;
	}

	breakdown(ONECOLON, items[1], &count, &firstname, &fields, &lf);
	if (count < 1) {
		tmp = str_text(buf);
		tmp2 = str_text(items[1]);
		applog(LOG_ERR, "%s%i:%s empty result count (%s) in (%s) will try anyway",
					bflsc->drv->name, bflsc->device_id, xlink, tmp2, tmp);
		free(tmp2);
		free(tmp);
	} else if (count != 1) {
		tmp = str_text(buf);
		tmp2 = str_text(items[1]);
		applog(LOG_ERR, "%s%i:%s incorrect result count %d (%s) in (%s) will try anyway",
					bflsc->drv->name, bflsc->device_id, xlink, count, tmp2, tmp);
		free(tmp2);
		free(tmp);
	}

	que = atoi(fields[0]);
	if (que != (lines - QUE_RES_LINES_MIN)) {
		i = que;
		// 1+ In case the last line isn't 'OK' - try to process it
		que = 1 + lines - QUE_RES_LINES_MIN;

		tmp = str_text(buf);
		tmp2 = str_text(items[0]);
		applog(LOG_ERR, "%s%i:%s incorrect result count %d (%s) will try %d (%s)",
					bflsc->drv->name, bflsc->device_id, xlink, i, tmp2, que, tmp);
		free(tmp2);
		free(tmp);

	}

	freebreakdown(&count, &firstname, &fields);

	for (i = 0; i < que; i++) {
		breakdown(NOCOLON, items[i + QUE_RES_LINES_MIN - 1], &count, &firstname, &fields, &lf);
		process_nonces(bflsc, dev, &(xlink[0]), items[i], count, fields, nonces);
		freebreakdown(&count, &firstname, &fields);
		sc_info->not_first_work = true;
	}

arigatou:
	freetolines(&lines, &items);

	return que;
}

#define TVF(tv) ((float)((tv)->tv_sec) + ((float)((tv)->tv_usec) / 1000000.0))
#define TVFMS(tv) (TVF(tv) * 1000.0)

// Thread to simply keep looking for results
static void *bflsc_get_results(void *userdata)
{
	struct cgpu_info *bflsc = (struct cgpu_info *)userdata;
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	struct timeval elapsed, now;
	float oldest, f;
	char buf[BFLSC_BUFSIZ+1];
	int err, amount;
	int i, que, dev, nonces;
	bool readok;

	cgtime(&now);
	for (i = 0; i < sc_info->sc_count; i++) {
		copy_time(&(sc_info->sc_devs[i].last_check_result), &now);
		copy_time(&(sc_info->sc_devs[i].last_dev_result), &now);
		copy_time(&(sc_info->sc_devs[i].last_nonce_result), &now);
	}

	while (sc_info->shutdown == false) {
		if (bflsc->usbinfo.nodev)
			return NULL;

		dev = -1;
		oldest = FLT_MAX;
		cgtime(&now);

		// Find the first oldest ... that also needs checking
		for (i = 0; i < sc_info->sc_count; i++) {
			timersub(&now, &(sc_info->sc_devs[i].last_check_result), &elapsed);
			f = TVFMS(&elapsed);
			if (f < oldest && f >= sc_info->sc_devs[i].ms_work) {
				f = oldest;
				dev = i;
			}
		}

		if (bflsc->usbinfo.nodev)
			return NULL;

		if (dev == -1)
			goto utsura;

		cgtime(&(sc_info->sc_devs[dev].last_check_result));

		readok = bflsc_qres(bflsc, buf, sizeof(buf), dev, &err, &amount, false);
		if (err < 0 || (!readok && amount != BFLSC_QRES_LEN) || (readok && amount < 1)) {
			// TODO: do what else?
		} else {
			que = process_results(bflsc, dev, buf, &nonces);
			sc_info->not_first_work = true; // in case it failed processing it
			if (que > 0)
				cgtime(&(sc_info->sc_devs[dev].last_dev_result));
			if (nonces > 0)
				cgtime(&(sc_info->sc_devs[dev].last_nonce_result));

			// TODO: if not getting results ... reinit?
		}

utsura:
		nmsleep(sc_info->results_sleep_time);
	}

	return NULL;
}

static bool bflsc_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	struct timeval now;

	if (thr_info_create(&(sc_info->results_thr), NULL, bflsc_get_results, (void *)bflsc)) {
		applog(LOG_ERR, "%s%i: thread create failed", bflsc->drv->name, bflsc->device_id);
		return false;
	}
	pthread_detach(sc_info->results_thr.pth);

	cgtime(&now);
	get_datestamp(bflsc->init, &now);

	return true;
}

static void bflsc_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);

	bflsc_flush_work(bflsc);
	sc_info->shutdown = true;
}

static void bflsc_thread_enable(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;

	if (bflsc->usbinfo.nodev)
		return;

	bflsc_initialise(bflsc);
}

static bool bflsc_send_work(struct cgpu_info *bflsc, int dev, struct work *work)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	struct FullNonceRangeJob data;
	char buf[BFLSC_BUFSIZ+1];
	int err, amount;
	int len;
	int try;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return false;

	// TODO: handle this everywhere
	if (sc_info->sc_devs[dev].overheat == true)
		return false;

	// Initially code only deals with sending one work item
	data.payloadSize = BFLSC_JOBSIZ;
	memcpy(data.midState, work->midstate, MIDSTATE_BYTES);
	memcpy(data.blockData, work->data + MERKLE_OFFSET, MERKLE_BYTES);
	data.endOfBlock = BFLSC_EOB;

	try = 0;

	mutex_lock(&(bflsc->device_mutex));
re_send:
	err = write_to_dev(bflsc, dev, BFLSC_QJOB, BFLSC_QJOB_LEN, &amount, C_REQUESTQUEJOB);
	if (err < 0 || amount != BFLSC_QJOB_LEN) {
		mutex_unlock(&(bflsc->device_mutex));
		bflsc_applog(bflsc, dev, C_REQUESTQUEJOB, amount, err);
		return false;
	}

	if (!getok(bflsc, C_REQUESTQUEJOBSTATUS, &err, &amount)) {
		mutex_unlock(&(bflsc->device_mutex));
		bflsc_applog(bflsc, dev, C_REQUESTQUEJOBSTATUS, amount, err);
		return false;
	}

	len = sizeof(struct FullNonceRangeJob);

	err = write_to_dev(bflsc, dev, (char *)&data, len, &amount, C_QUEJOB);
	if (err < 0 || amount != len) {
		mutex_unlock(&(bflsc->device_mutex));
		bflsc_applog(bflsc, dev, C_QUEJOB, amount, err);
		return false;
	}

	if (!getokerr(bflsc, C_QUEJOBSTATUS, &err, &amount, buf, sizeof(buf))) {
		// TODO: check for QUEUE FULL and set work_queued to BFLSC_QUE_SIZE
		//  and report a code bug LOG_ERR - coz it should never happen

		// Try twice
		if (try++ < 1 && amount > 1 &&
			strncasecmp(buf, BFLSC_TIMEOUT, BFLSC_TIMEOUT_LEN) == 0)
				goto re_send;

		mutex_unlock(&(bflsc->device_mutex));
		bflsc_applog(bflsc, dev, C_QUEJOBSTATUS, amount, err);
		return false;
	}

	mutex_unlock(&(bflsc->device_mutex));

	wr_lock(&(sc_info->stat_lock));
	sc_info->sc_devs[dev].work_queued++;
	wr_unlock(&(sc_info->stat_lock));

	work->subid = dev;

	return true;
}

static bool bflsc_queue_full(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	struct work *work = NULL;
	int i, dev, tried, que;
	bool ret = false;
	int tries = 0;

	tried = -1;
	// if something is wrong with a device try the next one available
	// TODO: try them all? Add an unavailable flag to sc_devs[i] init to 0 here first
	while (++tries < 3) {
		// Device is gone - shouldn't normally get here
		if (bflsc->usbinfo.nodev) {
			ret = true;
			break;
		}

		dev = -1;
		rd_lock(&(sc_info->stat_lock));
		// Anything waiting - gets the work first
		for (i = 0; i < sc_info->sc_count; i++) {
			// TODO: and ignore x-link dead - once I work out how to decide it is dead
			if (i != tried && sc_info->sc_devs[i].work_queued == 0 &&
			    !sc_info->sc_devs[i].overheat) {
				dev = i;
				break;
			}
		}

		if (dev == -1) {
			que = BFLSC_QUE_SIZE * 10; // 10x is certainly above the MAX it could be
			// The first device with the smallest amount queued
			for (i = 0; i < sc_info->sc_count; i++) {
				if (i != tried && sc_info->sc_devs[i].work_queued < que &&
				    !sc_info->sc_devs[i].overheat) {
					dev = i;
					que = sc_info->sc_devs[i].work_queued;
				}
			}
			if (que > BFLSC_QUE_FULL_ENOUGH)
				dev = -1;
		}
		rd_unlock(&(sc_info->stat_lock));

		// nothing needs work yet
		if (dev == -1) {
			ret = true;
			break;
		}

		if (!work)
			work = get_queued(bflsc);
		if (unlikely(!work))
			break;
		if (bflsc_send_work(bflsc, dev, work)) {
			work = NULL;
			break;
		} else
			tried = dev;
	}

	if (unlikely(work))
		work_completed(bflsc, work);
	return ret;
}

static int64_t bflsc_scanwork(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	int64_t ret, unsent;
	bool flushed, cleanup;
	struct work *work, *tmp;
	int dev, waited, i;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return -1;

	flushed = false;
	// Single lock check if any are flagged as flushed
	rd_lock(&(sc_info->stat_lock));
	for (dev = 0; dev < sc_info->sc_count; dev++)
		flushed |= sc_info->sc_devs[dev].flushed;
	rd_unlock(&(sc_info->stat_lock));

	// > 0 flagged as flushed
	if (flushed) {
// TODO: something like this ......
		for (dev = 0; dev < sc_info->sc_count; dev++) {
			cleanup = false;

			// Is there any flushed work that can be removed?
			rd_lock(&(sc_info->stat_lock));
			if (sc_info->sc_devs[dev].flushed) {
				if (sc_info->sc_devs[dev].result_id > (sc_info->sc_devs[dev].flush_id + 1))
					cleanup = true;
			}
			rd_unlock(&(sc_info->stat_lock));

			// yes remove the flushed work that can be removed
			if (cleanup) {
				wr_lock(&bflsc->qlock);
				HASH_ITER(hh, bflsc->queued_work, work, tmp) {
					if (work->devflag && work->subid == dev) {
						bflsc->queued_count--;
						HASH_DEL(bflsc->queued_work, work);
						discard_work(work);
					}
				}
				wr_unlock(&bflsc->qlock);

				wr_lock(&(sc_info->stat_lock));
				sc_info->sc_devs[dev].flushed = false;
				wr_unlock(&(sc_info->stat_lock));
			}
		}
	}

	waited = restart_wait(sc_info->scan_sleep_time);
	if (waited == ETIMEDOUT) {
		unsigned int old_sleep_time, new_sleep_time = 0;
		int min_queued = BFLSC_QUE_SIZE;
		/* Only adjust the scan_sleep_time if we did not receive a
		 * restart message while waiting. Try to adjust sleep time
		 * so we drop to BFLSC_QUE_WATERMARK before getting more work.
		 */

		rd_lock(&sc_info->stat_lock);
		old_sleep_time = sc_info->scan_sleep_time;
		for (i = 0; i < sc_info->sc_count; i++) {
			if (sc_info->sc_devs[i].work_queued < min_queued)
				min_queued = sc_info->sc_devs[i].work_queued;
		}
		rd_unlock(&sc_info->stat_lock);
		new_sleep_time = old_sleep_time;

		/* Increase slowly but decrease quickly */
		if (min_queued > BFLSC_QUE_WATERMARK && old_sleep_time < BFLSC_MAX_SLEEP)
			new_sleep_time = old_sleep_time * 21 / 20;
		else if (min_queued < BFLSC_QUE_WATERMARK)
			new_sleep_time = old_sleep_time * 2 / 3;

		/* Do not sleep more than BFLSC_MAX_SLEEP so we can always
		 * report in at least 2 results per 5s log interval. */
		if (new_sleep_time != old_sleep_time) {
			if (new_sleep_time > BFLSC_MAX_SLEEP)
				new_sleep_time = BFLSC_MAX_SLEEP;
			else if (new_sleep_time == 0)
				new_sleep_time = 1;
			applog(LOG_DEBUG, "%s%i: Changed scan sleep time to %d",
			       bflsc->drv->name, bflsc->device_id, new_sleep_time);

			wr_lock(&sc_info->stat_lock);
			sc_info->scan_sleep_time = new_sleep_time;
			wr_unlock(&sc_info->stat_lock);
		}
	}

	// Count up the work done since we last were here
	ret = 0;
	wr_lock(&(sc_info->stat_lock));
	for (dev = 0; dev < sc_info->sc_count; dev++) {
		unsent = sc_info->sc_devs[dev].hashes_unsent;
		sc_info->sc_devs[dev].hashes_unsent = 0;
		sc_info->sc_devs[dev].hashes_sent += unsent;
		sc_info->hashes_sent += unsent;
		ret += unsent;
	}
	wr_unlock(&(sc_info->stat_lock));

	return ret;
}

static bool bflsc_get_stats(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	bool allok = true;
	int i;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return false;

	for (i = 0; i < sc_info->sc_count; i++) {
		if (!bflsc_get_temp(bflsc, i))
			allok = false;

		// Device is gone
		if (bflsc->usbinfo.nodev)
			return false;

		if (i < (sc_info->sc_count - 1))
			nmsleep(BFLSC_TEMP_SLEEPMS);
	}

	return allok;
}

static void bflsc_identify(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);

	// TODO: handle x-link
	sc_info->flash_led = true;
}

static bool bflsc_thread_init(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;

	if (bflsc->usbinfo.nodev)
		return false;

	bflsc_initialise(bflsc);

	return true;
}

// there should be a new API function to return device info that isn't the standard stuff
// instead of bflsc_api_stats - since the stats should really just be internal code info
// and the new one should be UNusual device stats/extra details - like the stuff below

static struct api_data *bflsc_api_stats(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_file);
	struct api_data *root = NULL;

//if no x-link ... etc
	rd_lock(&(sc_info->stat_lock));
	root = api_add_temp(root, "Temp1", &(sc_info->sc_devs[0].temp1), true);
	root = api_add_temp(root, "Temp2", &(sc_info->sc_devs[0].temp2), true);
	root = api_add_volts(root, "Vcc1", &(sc_info->sc_devs[0].vcc1), true);
	root = api_add_volts(root, "Vcc2", &(sc_info->sc_devs[0].vcc2), true);
	root = api_add_volts(root, "Vmain", &(sc_info->sc_devs[0].vmain), true);
	root = api_add_temp(root, "Temp1 Max", &(sc_info->sc_devs[0].temp1_max), true);
	root = api_add_temp(root, "Temp2 Max", &(sc_info->sc_devs[0].temp2_max), true);
	root = api_add_time(root, "Temp1 Max Time", &(sc_info->sc_devs[0].temp1_max_time), true);
	root = api_add_time(root, "Temp2 Max Time", &(sc_info->sc_devs[0].temp2_max_time), true);
	rd_unlock(&(sc_info->stat_lock));
	root = api_add_escape(root, "GetInfo", sc_info->sc_devs[0].getinfo, false);

/*
else a whole lot of something like these ... etc
	root = api_add_temp(root, "X-%d-Temp1", &(sc_info->temp1), false);
	root = api_add_temp(root, "X-%d-Temp2", &(sc_info->temp2), false);
	root = api_add_volts(root, "X-%d-Vcc1", &(sc_info->vcc1), false);
	root = api_add_volts(root, "X-%d-Vcc2", &(sc_info->vcc2), false);
	root = api_add_volts(root, "X-%d-Vmain", &(sc_info->vmain), false);
*/

	return root;
}

struct device_drv bflsc_drv = {
	.drv_id = DRIVER_BFLSC,
	.dname = "BitForceSC",
	.name = BFLSC_SINGLE,
	.drv_detect = bflsc_detect,
	.get_api_stats = bflsc_api_stats,
	.get_statline_before = get_bflsc_statline_before,
	.get_stats = bflsc_get_stats,
	.identify_device = bflsc_identify,
	.thread_prepare = bflsc_thread_prepare,
	.thread_init = bflsc_thread_init,
	.hash_work = hash_queued_work,
	.scanwork = bflsc_scanwork,
	.queue_full = bflsc_queue_full,
	.flush_work = bflsc_flush_work,
	.thread_shutdown = bflsc_shutdown,
	.thread_enable = bflsc_thread_enable
};
