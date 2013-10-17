/*
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 * Copyright 2013 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BFLSC_H
#define BFLSC_H
#define BLANK ""
#define LFSTR "<LF>"

/*
 * Firmware
 * DRV_V2 expects (beyond V1) the GetInfo to return the chip count
 * The queues are 40 instead of 20 and are *usually* consumed and filled
 * in bursts due to e.g. a 16 chip device doing 16 items at a time and
 * returning 16 results at a time
 * If the device has varying chip speeds, it will gradually break up the
 * burst of results as we progress
 */
enum driver_version {
	BFLSC_DRVUNDEF = 0,
	BFLSC_DRV1,
	BFLSC_DRV2
};

/*
 * With Firmware 1.0.0 and a result queue of 20 the Max is:
 * inprocess = 12
 * max count = 9
 * 64+1+24+1+1+(1+8)*8+1 per line = 164 * 20
 * OK = 3
 * Total: 3304
 *
 * With Firmware 1.2.* and a result queue of 40 but a limit of 15 replies:
 * inprocess = 12
 * max count = 9
 * 64+1+24+1+1+1+1+(1+8)*8+1 per line = 166 * 15
 * OK = 3
 * Total: 2514
 *
 */
#define BFLSC_BUFSIZ (0x1000)

// Should be big enough
#define BFLSC_APPLOGSIZ 8192

#define BFLSC_INFO_TIMEOUT 999

#define BFLSC_DI_FIRMWARE "FIRMWARE"
#define BFLSC_DI_ENGINES "ENGINES"
#define BFLSC_DI_JOBSINQUE "JOBS IN QUEUE"
#define BFLSC_DI_XLINKMODE "XLINK MODE"
#define BFLSC_DI_XLINKPRESENT "XLINK PRESENT"
#define BFLSC_DI_DEVICESINCHAIN "DEVICES IN CHAIN"
#define BFLSC_DI_CHAINPRESENCE "CHAIN PRESENCE MASK"
#define BFLSC_DI_CHIPS "CHIP PARALLELIZATION"
#define BFLSC_DI_CHIPS_PARALLEL "YES"

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
	char *chips;

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

#define QUE_MAX_RESULTS 8

struct bflsc_info {
	enum driver_version driver_version;
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
	bool fanauto;
	int que_size;
	int que_full_enough;
	int que_watermark;
	int que_low;
	int que_noncecount;
	int que_fld_min;
	int que_fld_max;
	int flush_size;
	// count of given size, [+2] is for any > QUE_MAX_RESULTS
	uint64_t result_size[QUE_MAX_RESULTS+2];
};

#define BFLSC_XLINKHDR '@'
#define BFLSC_MAXPAYLOAD 255

struct DataForwardToChain {
	uint8_t header;
	uint8_t payloadSize;
	uint8_t deviceAddress;
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

#define QUE_NONCECOUNT_V1 2
#define QUE_FLD_MIN_V1 3
#define QUE_FLD_MAX_V1 (QUE_MAX_RESULTS+QUE_FLD_MIN_V1)

#define QUE_CHIP_V2 2
#define QUE_NONCECOUNT_V2 3
#define QUE_FLD_MIN_V2 4
#define QUE_FLD_MAX_V2 (QUE_MAX_RESULTS+QUE_FLD_MIN_V2)

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

// Commands (Single Stage)
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
#define BFLSC_QRES "ZOX"
#define BFLSC_QRES_LEN (sizeof(BFLSC_QRES)-1)
#define BFLSC_QFLUSH "ZQX"
#define BFLSC_QFLUSH_LEN (sizeof(BFLSC_QFLUSH)-1)
#define BFLSC_FANAUTO "Z9X"
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
#define BFLSC_LOADSTR "ZUX"
#define BFLSC_LOADSTR_LEN (sizeof(BFLSC_LOADSTR)-1)

// Commands (Dual Stage)
#define BFLSC_QJOB "ZNX"
#define BFLSC_QJOB_LEN (sizeof(BFLSC_QJOB)-1)
#define BFLSC_QJOBS "ZWX"
#define BFLSC_QJOBS_LEN (sizeof(BFLSC_QJOBS)-1)
#define BFLSC_SAVESTR "ZSX"
#define BFLSC_SAVESTR_LEN (sizeof(BFLSC_SAVESTR)-1)

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
// x-link timeout has a space (a number follows)
#define BFLSC_XTIMEOUT BFLSC_ANERR "TIMEOUT "
#define BFLSC_XTIMEOUT_LEN (sizeof(BFLSC_XTIMEOUT)-1)
#define BFLSC_INVALID BFLSC_ANERR "INVALID DATA"
#define BFLSC_INVALID_LEN (sizeof(BFLSC_INVALID)-1)
#define BFLSC_ERRSIG BFLSC_ANERR "SIGNATURE"
#define BFLSC_ERRSIG_LEN (sizeof(BFLSC_ERRSIG)-1)
#define BFLSC_OKQ "OK:QUEUED"
#define BFLSC_OKQ_LEN (sizeof(BFLSC_OKQ)-1)
#define BFLSC_INPROCESS "INPROCESS"
#define BFLSC_INPROCESS_LEN (sizeof(BFLSC_INPROCESS)-1)
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

// Non queued commands (not used)
#define BFLSC_SENDWORK "ZDX"
#define BFLSC_SENDWORK_LEN (sizeof(BFLSC_SENDWORK)-1)
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
#define BAS_SCAN_TIME 360
#define BAL_SCAN_TIME 720
#define BAJ_SCAN_TIME 1000
#define BFLSC_RES_TIME 100
#define BFLSC_MAX_SLEEP 2000

#define BAJ_LATENCY LATENCY_STD
#define BAL_LATENCY 12
#define BAS_LATENCY 12
// For now a BAM doesn't really exist - it's currently 8 independent BASs
#define BAM_LATENCY 2

#define BFLSC_TEMP_SLEEPMS 5

#define BFLSC_QUE_SIZE_V1 20
#define BFLSC_QUE_FULL_ENOUGH_V1 13
#define BFLSC_QUE_WATERMARK_V1 6
#define BFLSC_QUE_LOW_V1 3

// TODO: use 5 batch jobs
// TODO: base these numbers on the chip count?
#define BFLSC_QUE_SIZE_V2 40
#define BFLSC_QUE_FULL_ENOUGH_V2 36
#define BFLSC_QUE_WATERMARK_V2 32
#define BFLSC_QUE_LOW_V2 16

#define BFLSC_TEMP_OVERHEAT 90
// Must drop this far below cutoff before resuming work
#define BFLSC_TEMP_RECOVER 5

// If initialisation fails the first time,
// sleep this amount (ms) and try again
#define REINIT_TIME_FIRST_MS 100
// Max ms per sleep
#define REINIT_TIME_MAX_MS 800
// Keep trying up to this many us
#define REINIT_TIME_MAX 3000000

int opt_bflsc_overheat;

#endif /* BFLSC_H */
