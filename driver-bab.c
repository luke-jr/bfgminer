/*
 * Copyright 2013 Andrew Smith
 * Copyright 2013 bitfury
 *
 * BitFury GPIO code based on chainminer code:
 *	https://github.com/bfsb/chainminer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"
#include "compat.h"
#include "miner.h"
#include "sha2.h"

/*
 * Tested on RPi running Raspbian with BlackArrow BitFury V1 16 chip GPIO board
 */

#ifndef LINUX
static void bab_detect(__maybe_unused bool hotplug)
{
}
#else

#include <unistd.h>
#include <linux/spi/spidev.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#define BAB_SPI_BUS 0
#define BAB_SPI_CHIP 0

#define BAB_SPI_SPEED 96000
#define BAB_SPI_BUFSIZ 1024

#define BAB_ADDR(_n) (*((babinfo->gpio) + (_n)))

#define BAB_INP_GPIO(_n) BAB_ADDR((_n) / 10) &= (~(7 << (((_n) % 10) * 3)))
#define BAB_OUT_GPIO(_n) BAB_ADDR((_n) / 10) |= (1 << (((_n) % 10) * 3))
#define BAB_OUT_GPIO_V(_n, _v) BAB_ADDR((_n) / 10) |= (((_v) <= 3 ? (_v) + 4 : \
					((_v) == 4 ? 3 : 2)) << (((_n) % 10) * 3))

#define BAB_GPIO_SET BAB_ADDR(7)
#define BAB_GPIO_CLR BAB_ADDR(10)
#define BAB_GPIO_LEVEL BAB_ADDR(13)

#define BAB_MAXCHIPS 256
#define BAB_MAXBUF (BAB_MAXCHIPS * 512)
#define BAB_MAXBANKS 4
#define BAB_CORES 16
#define BAB_X_COORD 21
#define BAB_Y_COORD 36

#define BAB_BREAK ((uint8_t *)"\04")
#define BAB_ASYNC ((uint8_t *)"\05")
#define BAB_SYNC ((uint8_t *)"\06")

#define BAB_FFL " - from %s %s() line %d"
#define BAB_FFL_HERE __FILE__, __func__, __LINE__
#define BAB_FFL_PASS file, func, line

#define bab_reset(_bank, _times) _bab_reset(babcgpu, babinfo, _bank, _times)
#define bab_txrx(_buf, _siz, _det) _bab_txrx(babcgpu, babinfo, _buf, _siz, _det, BAB_FFL_HERE)
#define bab_add_buf(_data) _bab_add_buf(babcgpu, babinfo, _data, sizeof(_data)-1, BAB_FFL_HERE)
#define BAB_ADD_BREAK() _bab_add_buf(babcgpu, babinfo, BAB_BREAK, 1, BAB_FFL_HERE)
#define BAB_ADD_ASYNC() _bab_add_buf(babcgpu, babinfo, BAB_ASYNC, 1, BAB_FFL_HERE)
#define bab_config_reg(_reg, _ena) _bab_config_reg(babcgpu, babinfo, _reg, _ena, BAB_FFL_HERE)
#define bab_add_data(_addr, _data, _siz) _bab_add_data(babcgpu, babinfo, _addr, (const uint8_t *)(_data), _siz, BAB_FFL_HERE)

#define BAB_ADD_MIN 4
#define BAB_ADD_MAX 128

#define BAB_STATE_DONE 0
#define BAB_STATE_READY 1
#define BAB_STATE_SENDING 2
#define BAB_STATE_SENT 3
#define BAB_STATE_READING 4

#define BAB_SPI_BUFFERS 2

#define BAB_BASEA 4
#define BAB_BASEB 61
#define BAB_COUNTERS 16
static const uint8_t bab_counters[BAB_COUNTERS] = {
	64,			64,
	BAB_BASEA,		BAB_BASEA+4,
	BAB_BASEA+2,		BAB_BASEA+2+16,
	BAB_BASEA,		BAB_BASEA+1,
	(BAB_BASEB)%65,		(BAB_BASEB+1)%65,
	(BAB_BASEB+3)%65,	(BAB_BASEB+3+16)%65,
	(BAB_BASEB+4)%65,	(BAB_BASEB+4+4)%65,
	(BAB_BASEB+3+3)%65,	(BAB_BASEB+3+1+3)%65
};

#define BAB_W1 16
static const uint32_t bab_w1[BAB_W1] = {
	0,		0,	0,	0xffffffff,
	0x80000000,	0,	0,	0,
	0,		0,	0,	0,
	0,		0,	0,	0x00000280
};

#define BAB_W2 8
static const uint32_t bab_w2[BAB_W2] = {
	0x80000000,	0,	0,	0,
	0,		0,	0,	0x00000100
};

#define BAB_TEST_DATA 19
static const uint32_t bab_test_data[BAB_TEST_DATA] = {
	0xb0e72d8e,	0x1dc5b862,	0xe9e7c4a6,	0x3050f1f5,
	0x8a1a6b7e,	0x7ec384e8,	0x42c1c3fc,	0x8ed158a1,
	0x8a1a6b7e,	0x6f484872,	0x4ff0bb9b,	0x12c97f07,
	0xb0e72d8e,	0x55d979bc,	0x39403296,	0x40f09e84,
	0x8a0bb7b7,	0x33af304f,	0x0b290c1a //,	0xf0c4e61f
};

//maximum number of chips on alternative bank
// #define BANKCHIPS 64

/*
 * maximum chip speed available for auto tuner
 * speed/nrate/hrate/watt
 *    53/   97/  100/  84
 *    54/   98/  107/  88
 *    55/   99/  115/  93
 *    56/  101/  125/  99
 */
#define BAB_MAXSPEED 57
#define BAB_DEFSPEED 54
#define BAB_MINSPEED 52

#define MIDSTATE_BYTES 32
#define MERKLE_OFFSET 64
#define MERKLE_BYTES 12
#define BLOCK_HEADER_BYTES 80

#define MIDSTATE_UINTS (MIDSTATE_BYTES / sizeof(uint32_t))
#define DATA_UINTS ((BLOCK_HEADER_BYTES / sizeof(uint32_t)) - 1)

// Auto adjust
#define BAB_AUTO_REG 0
#define BAB_AUTO_VAL 0x01
// iclk
#define BAB_ICLK_REG 1
#define BAB_ICLK_VAL 0x02
// No fast clock
#define BAB_FAST_REG 2
#define BAB_FAST_VAL 0x04
// Divide by 2
#define BAB_DIV2_REG 3
#define BAB_DIV2_VAL 0x08
// Slow Clock
#define BAB_SLOW_REG 4
#define BAB_SLOW_VAL 0x10
// No oclk
#define BAB_OCLK_REG 6
#define BAB_OCLK_VAL 0x20
// Has configured
#define BAB_CFGD_VAL 0x40

#define BAB_DEFCONF (BAB_AUTO_VAL | \
		     BAB_ICLK_VAL | \
		     BAB_DIV2_VAL | \
		     BAB_SLOW_VAL)

#define BAB_REG_CLR_FROM 7
#define BAB_REG_CLR_TO 11

#define BAB_AUTO_SET(_c) ((_c) & BAB_AUTO_VAL)
#define BAB_ICLK_SET(_c) ((_c) & BAB_ICLK_VAL)
#define BAB_FAST_SET(_c) ((_c) & BAB_FAST_VAL)
#define BAB_DIV2_SET(_c) ((_c) & BAB_DIV2_VAL)
#define BAB_SLOW_SET(_c) ((_c) & BAB_SLOW_VAL)
#define BAB_OCLK_SET(_c) ((_c) & BAB_OCLK_VAL)
#define BAB_CFGD_SET(_c) ((_c) & BAB_CFGD_VAL)

#define BAB_AUTO_BIT(_c) (BAB_AUTO_SET(_c) ? true : false)
#define BAB_ICLK_BIT(_c) (BAB_ICLK_SET(_c) ? false : true)
#define BAB_FAST_BIT(_c) (BAB_FAST_SET(_c) ? true : false)
#define BAB_DIV2_BIT(_c) (BAB_DIV2_SET(_c) ? false : true)
#define BAB_SLOW_BIT(_c) (BAB_SLOW_SET(_c) ? true : false)
#define BAB_OCLK_BIT(_c) (BAB_OCLK_SET(_c) ? true : false)

#define BAB_COUNT_ADDR 0x0100
#define BAB_W1A_ADDR 0x1000
#define BAB_W1B_ADDR 0x1400
#define BAB_W2_ADDR 0x1900
#define BAB_INP_ADDR 0x3000
#define BAB_OSC_ADDR 0x6000
#define BAB_REG_ADDR 0x7000

/*
 * valid: 0x01 0x03 0x07 0x0F 0x1F 0x3F 0x7F 0xFF
 * max { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00 }
 * max { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x00 }
 * avg { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00 }
 * slo { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x00 }
 * min { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
 * good: 0x1F (97) 0x3F (104) 0x7F (109) 0xFF (104)
 */

#define BAB_OSC 8
static const uint8_t bab_osc_bits[BAB_OSC] =
	{ 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF };

static const uint8_t bab_reg_ena[4] = { 0xc1, 0x6a, 0x59, 0xe3 };
static const uint8_t bab_reg_dis[4] = { 0x00, 0x00, 0x00, 0x00 };

#define BAB_NONCE_OFFSETS 3
static const uint32_t bab_nonce_offsets[] = {-0x800000, 0, -0x400000};

struct bab_work_send {
	uint32_t midstate[MIDSTATE_UINTS];
	uint32_t ms3steps[MIDSTATE_UINTS];
	uint32_t merkle7;
	uint32_t ntime;
	uint32_t bits;
};

#define BAB_REPLY_NONCES 16
struct bab_work_reply {
	uint32_t nonce[BAB_REPLY_NONCES];
	uint32_t jobsel;
};

#define MAX_BLISTS 4096

typedef struct blist {
	struct blist *prev;
	struct blist *next;
	struct work *work;
	int nonces;
} BLIST;

#define MAX_RLISTS 256

typedef struct rlist {
	struct rlist *prev;
	struct rlist *next;
	int chip;
	uint32_t nonce;
	bool first_second;
} RLIST;

struct bab_info {
	struct thr_info spi_thr;
	struct thr_info res_thr;

	pthread_mutex_t spi_lock;
	pthread_mutex_t res_lock;
	pthread_mutex_t did_lock;
	cglock_t blist_lock;

	// All GPIO goes through this
	volatile unsigned *gpio;

	int spifd;
	int chips;
	uint32_t chip_spis[BAB_MAXCHIPS+1];

	int buffer;
	int buf_status[BAB_SPI_BUFFERS];
	uint8_t buf_write[BAB_SPI_BUFFERS][BAB_MAXBUF];
	uint8_t buf_read[BAB_SPI_BUFFERS][BAB_MAXBUF];
	uint32_t buf_used[BAB_SPI_BUFFERS];
	uint32_t chip_off[BAB_SPI_BUFFERS][BAB_MAXCHIPS+1];
	uint32_t bank_off[BAB_SPI_BUFFERS][BAB_MAXBANKS+2];

	struct bab_work_send chip_input[BAB_MAXCHIPS];
	struct bab_work_reply chip_results[BAB_MAXCHIPS];
	struct bab_work_reply chip_prev[BAB_MAXCHIPS];

	uint8_t chip_fast[BAB_MAXCHIPS];
	uint8_t chip_conf[BAB_MAXCHIPS];
	uint8_t old_fast[BAB_MAXCHIPS];
	uint8_t old_conf[BAB_MAXCHIPS];
	uint8_t chip_bank[BAB_MAXCHIPS+1];

	uint8_t osc[BAB_OSC];

	int fixchip;

	/*
	 * Ignore errors in the first work reply since
	 * they may be from a previous run or random junk
	 * There can be >100 with just a 16 chip board
	 */
	uint32_t initial_ignored;
	bool nonce_before[BAB_MAXCHIPS];
	bool not_first_reply[BAB_MAXCHIPS];

	// Stats
	struct timeval chip_start[BAB_MAXCHIPS];
	int chip_busy[BAB_MAXCHIPS];
	uint64_t core_good[BAB_MAXCHIPS][BAB_CORES];
	uint64_t core_bad[BAB_MAXCHIPS][BAB_CORES];
	uint64_t chip_spie[BAB_MAXCHIPS]; // spi errors
	uint64_t chip_miso[BAB_MAXCHIPS]; // msio errors
	uint64_t chip_nonces[BAB_MAXCHIPS];
	uint64_t chip_good[BAB_MAXCHIPS];
	uint64_t chip_bad[BAB_MAXCHIPS];
	uint64_t chip_ncore[BAB_MAXCHIPS][BAB_X_COORD][BAB_Y_COORD];

	uint64_t untested_nonces;
	uint64_t tested_nonces;

	uint64_t new_nonces;
	uint64_t ok_nonces;

	uint64_t nonce_offset_count[BAB_NONCE_OFFSETS];
	uint64_t total_tests;
	uint64_t max_tests_per_nonce;
	uint64_t total_links;
	uint64_t max_links;

	int blist_count;
	int bfree_count;
	int work_count;
	int chip_count;
	BLIST *bfree_list;
	BLIST *work_list;
	BLIST *chip_list[BAB_MAXCHIPS];

	int rlist_count;
	int rfree_count;
	int res_count;
	RLIST *rfree_list;
	RLIST *res_list_head;
	RLIST *res_list_tail;

	struct timeval last_did;

	bool initialised;
};

static BLIST *new_blist_set(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	BLIST *blist = NULL;
	int i;

	blist = calloc(MAX_BLISTS, sizeof(*blist));
	if (!blist)
		quithere(1, "Failed to calloc blist - when old count=%d", babinfo->blist_count);

	babinfo->blist_count += MAX_BLISTS;
	babinfo->bfree_count = MAX_BLISTS;

	blist[0].prev = NULL;
	blist[0].next = &(blist[1]);
	for (i = 1; i < MAX_BLISTS-1; i++) {
		blist[i].prev = &blist[i-1];
		blist[i].next = &blist[i+1];
	}
	blist[MAX_BLISTS-1].prev = &(blist[MAX_BLISTS-2]);
	blist[MAX_BLISTS-1].next = NULL;

	return blist;
}

static BLIST *next_work(struct cgpu_info *babcgpu, int chip)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	BLIST *bitem;

	cg_wlock(&babinfo->blist_lock);
	bitem = babinfo->work_list;
	if (bitem) {
		// Unlink it from work
		if (bitem->next)
			bitem->next->prev = NULL;
		babinfo->work_list = bitem->next;
		babinfo->work_count--;

		// Add it to the chip
		bitem->next = babinfo->chip_list[chip];
		bitem->prev = NULL;
		if (bitem->next)
			bitem->next->prev = bitem;
		babinfo->chip_list[chip] = bitem;
		babinfo->chip_count++;
	}
	cg_wunlock(&babinfo->blist_lock);

	return bitem;
}

static void discard_last(struct cgpu_info *babcgpu, int chip)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	BLIST *bitem;

	cg_wlock(&babinfo->blist_lock);
	bitem = babinfo->chip_list[chip];
	if (bitem) {
		// Unlink it from the chip
		if (bitem->next)
			bitem->next->prev = NULL;
		babinfo->chip_list[chip] = bitem->next;
		babinfo->chip_count--;

		// Put it in the free list
		bitem->next = babinfo->bfree_list;
		bitem->prev = NULL;
		if (bitem->next)
			bitem->next->prev = bitem;
		babinfo->bfree_list = bitem;
		babinfo->bfree_count++;
	}
	cg_wunlock(&babinfo->blist_lock);
}

static BLIST *store_work(struct cgpu_info *babcgpu, struct work *work)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	BLIST *bitem = NULL;
	int ran_out = 0;

	cg_wlock(&babinfo->blist_lock);

	if (babinfo->bfree_list == NULL) {
		ran_out = babinfo->blist_count;
		babinfo->bfree_list = new_blist_set(babcgpu);
	}

	// unlink from free
	bitem = babinfo->bfree_list;
	babinfo->bfree_list = babinfo->bfree_list->next;
	if (babinfo->bfree_list)
		babinfo->bfree_list->prev = NULL;
	babinfo->bfree_count--;

	// add to work
	bitem->next = babinfo->work_list;
	bitem->prev = NULL;
	if (bitem->next)
		bitem->next->prev = bitem;
	babinfo->work_list = bitem;
	babinfo->work_count++;

	bitem->work = work;
	bitem->nonces = 0;

	cg_wunlock(&babinfo->blist_lock);

	if (ran_out > 0) {
		applog(LOG_ERR, "%s%i: BLIST used count exceeded %d, now %d (work=%d chip=%d)",
				babcgpu->drv->name, babcgpu->device_id,
				ran_out, babinfo->blist_count,
				babinfo->work_count,
				babinfo->chip_count);
	}

	return bitem;
}

static void free_blist(struct cgpu_info *babcgpu, BLIST *bhead, int chip)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	struct work *work;
	BLIST *bitem;

	if (!bhead)
		return;

	// Unlink it from the chip
	cg_wlock(&babinfo->blist_lock);
	if (unlikely(bhead == babinfo->chip_list[chip])) {
		// Removing the chip head is an error
		bhead = bhead->next;
		babinfo->chip_list[chip]->next = NULL;
	} else
		bhead->prev->next = NULL;
	bitem = bhead;
	while (bitem) {
		babinfo->chip_count--;
		bitem = bitem->next;
	}
	cg_wunlock(&babinfo->blist_lock);

	while (bhead) {
		bitem = bhead;
		bhead = bitem->next;

		// add to free
		cg_wlock(&babinfo->blist_lock);
		bitem->next = babinfo->bfree_list;
		if (babinfo->bfree_list)
			babinfo->bfree_list->prev = bitem;
		bitem->prev = NULL;
		babinfo->bfree_list = bitem;
		babinfo->bfree_count++;
		work = bitem->work;
		cg_wunlock(&babinfo->blist_lock);

		work_completed(babcgpu, work);
	}

}

static RLIST *new_rlist_set(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	RLIST *rlist = NULL;
	int i;

	rlist = calloc(MAX_RLISTS, sizeof(*rlist));
	if (!rlist)
		quithere(1, "Failed to calloc rlist - when old count=%d", babinfo->rlist_count);

	babinfo->rlist_count += MAX_RLISTS;
	babinfo->rfree_count = MAX_RLISTS;

	rlist[0].prev = NULL;
	rlist[0].next = &(rlist[1]);
	for (i = 1; i < MAX_RLISTS-1; i++) {
		rlist[i].prev = &rlist[i-1];
		rlist[i].next = &rlist[i+1];
	}
	rlist[MAX_RLISTS-1].prev = &(rlist[MAX_RLISTS-2]);
	rlist[MAX_RLISTS-1].next = NULL;

	return rlist;
}

static RLIST *store_nonce(struct cgpu_info *babcgpu, int chip, uint32_t nonce, bool first_second)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	RLIST *ritem = NULL;
	int ran_out = 0;

	mutex_lock(&(babinfo->res_lock));

	if (babinfo->rfree_list == NULL) {
		ran_out = babinfo->rlist_count;
		babinfo->rfree_list = new_rlist_set(babcgpu);
	}

	// unlink from rfree
	ritem = babinfo->rfree_list;
	babinfo->rfree_list = babinfo->rfree_list->next;
	if (babinfo->rfree_list)
		babinfo->rfree_list->prev = NULL;
	babinfo->rfree_count--;

	// add to head of results
	ritem->next = babinfo->res_list_head;
	ritem->prev = NULL;
	babinfo->res_list_head = ritem;
	if (ritem->next)
		ritem->next->prev = ritem;
	else
		babinfo->res_list_tail = ritem;

	babinfo->res_count++;

	ritem->chip = chip;
	ritem->nonce = nonce;
	ritem->first_second = first_second;

	mutex_unlock(&(babinfo->res_lock));

	if (ran_out > 0) {
		applog(LOG_ERR, "%s%i: RLIST used count exceeded %d, now %d (work=%d chip=%d)",
				babcgpu->drv->name, babcgpu->device_id,
				ran_out, babinfo->rlist_count,
				babinfo->work_count,
				babinfo->chip_count);
	}

	return ritem;
}

static bool oldest_nonce(struct cgpu_info *babcgpu, int *chip, uint32_t *nonce, bool *first_second)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	RLIST *ritem = NULL;
	bool found = false;

	mutex_lock(&(babinfo->res_lock));

	if (babinfo->res_list_tail) {
		// unlink from res
		ritem = babinfo->res_list_tail;
		if (ritem->prev) {
			ritem->prev->next = NULL;
			babinfo->res_list_tail = ritem->prev;
		} else
			babinfo->res_list_head = babinfo->res_list_tail = NULL;

		babinfo->res_count--;

		found = true;
		*chip = ritem->chip;
		*nonce = ritem->nonce;
		*first_second = ritem->first_second;

		// add to rfree
		ritem->next = babinfo->rfree_list;
		ritem->prev = NULL;
		if (ritem->next)
			ritem->next->prev = ritem;
		babinfo->rfree_list = ritem;

		babinfo->rfree_count++;
	}

	mutex_unlock(&(babinfo->res_lock));

	return found;
}

static void _bab_reset(__maybe_unused struct cgpu_info *babcgpu, struct bab_info *babinfo, int bank, int times)
{
	const int banks[4] = { 18, 23, 24, 25 };
	int i;

	BAB_INP_GPIO(10);
	BAB_OUT_GPIO(10);
	BAB_INP_GPIO(11);
	BAB_OUT_GPIO(11);

	if (bank) {
		for (i = 0; i < 4; i++) {
			BAB_INP_GPIO(banks[i]);
			BAB_OUT_GPIO(banks[i]);
			if (bank == i+1)
				BAB_GPIO_SET = 1 << banks[i];
			else
				BAB_GPIO_CLR = 1 << banks[i];
		}
		cgsleep_us(4096);
	} else {
		for (i = 0; i < 4; i++)
			BAB_INP_GPIO(banks[i]);
	}

	BAB_GPIO_SET = 1 << 11;
	for (i = 0; i < times; i++) { // 1us = 1MHz
		BAB_GPIO_SET = 1 << 10;
		cgsleep_us(1);
		BAB_GPIO_CLR = 1 << 10;
		cgsleep_us(1);
	}
	BAB_GPIO_CLR = 1 << 11;
	BAB_INP_GPIO(11);
	BAB_INP_GPIO(10);
	BAB_INP_GPIO(9);
	BAB_OUT_GPIO_V(11, 0);
	BAB_OUT_GPIO_V(10, 0);
	BAB_OUT_GPIO_V(9, 0);
}

// TODO: handle a false return where this is called?
static bool _bab_txrx(struct cgpu_info *babcgpu, struct bab_info *babinfo, int buf, uint32_t siz, bool detect_ignore, const char *file, const char *func, const int line)
{
	int bank, i;
	uint32_t pos;
	struct spi_ioc_transfer tran;
	uintptr_t rbuf, wbuf;

	wbuf = (uintptr_t)(babinfo->buf_write[buf]);
	rbuf = (uintptr_t)(babinfo->buf_read[buf]);

	memset(&tran, 0, sizeof(tran));
	tran.delay_usecs = 0;
	tran.speed_hz = BAB_SPI_SPEED;

	i = 0;
	pos = 0;
	for (bank = 0; bank <= BAB_MAXBANKS; bank++) {
		if (babinfo->bank_off[buf][bank]) {
			bab_reset(bank, 64);
			break;
		}
	}

	if (unlikely(bank > BAB_MAXBANKS)) {
		applog(LOG_ERR, "%s%d: %s() failed to find a bank" BAB_FFL,
				babcgpu->drv->name, babcgpu->device_id,
				__func__, BAB_FFL_PASS);
		return false;
	}

	while (siz > 0) {
		tran.tx_buf = wbuf;
		tran.rx_buf = rbuf;
		tran.speed_hz = BAB_SPI_SPEED;
		if (pos == babinfo->bank_off[buf][bank]) {
			for (; ++bank <= BAB_MAXBANKS; ) {
				if (babinfo->bank_off[buf][bank] > pos) {
					bab_reset(bank, 64);
					break;
				}
			}
		}
		if (siz < BAB_SPI_BUFSIZ)
			tran.len = siz;
		else
			tran.len = BAB_SPI_BUFSIZ;

		if (pos < babinfo->bank_off[buf][bank] &&
		    babinfo->bank_off[buf][bank] < (pos + tran.len))
			tran.len = babinfo->bank_off[buf][bank] - pos;

		for (; i < babinfo->chips; i++) {
			if (!babinfo->chip_off[buf][i])
				continue;
			if (babinfo->chip_off[buf][i] >= pos + tran.len) {
				tran.speed_hz = babinfo->chip_spis[i];
				break;
			}
		}

		if (unlikely(i > babinfo->chips)) {
			applog(LOG_ERR, "%s%d: %s() failed to find chip" BAB_FFL,
					babcgpu->drv->name, babcgpu->device_id,
					__func__, BAB_FFL_PASS);
			return false;
		}

		if (unlikely(babinfo->chip_spis[i] == BAB_SPI_SPEED)) {
			applog(LOG_DEBUG, "%s%d: %s() chip[%d] speed %d shouldn't be %d" BAB_FFL,
						babcgpu->drv->name, babcgpu->device_id,
						__func__, i, (int)babinfo->chip_spis[i],
						BAB_SPI_SPEED, BAB_FFL_PASS);
		}

		if (unlikely(tran.speed_hz == BAB_SPI_SPEED)) {
			applog(LOG_DEBUG, "%s%d: %s() transfer speed %d shouldn't be %d" BAB_FFL,
						babcgpu->drv->name, babcgpu->device_id,
						__func__, (int)tran.speed_hz,
						BAB_SPI_SPEED, BAB_FFL_PASS);
		}

		if (ioctl(babinfo->spifd, SPI_IOC_MESSAGE(1), (void *)&tran) < 0) {
			if (!detect_ignore || errno != 110) {
				applog(LOG_ERR, "%s%d: ioctl failed err=%d" BAB_FFL,
						babcgpu->drv->name, babcgpu->device_id,
						errno, BAB_FFL_PASS);
			}
			return false;
		}

		siz -= tran.len;
		wbuf += tran.len;
		rbuf += tran.len;
		pos += tran.len;
	}
	mutex_lock(&(babinfo->did_lock));
	cgtime(&(babinfo->last_did));
	mutex_unlock(&(babinfo->did_lock));
	return true;
}

static void _bab_add_buf_rev(__maybe_unused struct cgpu_info *babcgpu, struct bab_info *babinfo, const uint8_t *data, uint32_t siz, const char *file, const char *func, const int line)
{
	uint8_t tmp;
	uint32_t now_used, i;
	int buf;

	buf = babinfo->buffer;
	now_used = babinfo->buf_used[buf];
	if (now_used + siz >= BAB_MAXBUF) {
		quitfrom(1, file, func, line,
			"%s() buffer %d limit of %d exceeded=%d siz=%d",
			__func__, buf, BAB_MAXBUF, now_used + siz, siz);
	}

	for (i = 0; i < siz; i++) {
		tmp = data[i];
		tmp = ((tmp & 0xaa)>>1) | ((tmp & 0x55) << 1);
		tmp = ((tmp & 0xcc)>>2) | ((tmp & 0x33) << 2);
		tmp = ((tmp & 0xf0)>>4) | ((tmp & 0x0f) << 4);
		babinfo->buf_write[buf][now_used + i] = tmp;
	}

	babinfo->buf_used[buf] += siz;
}

static void _bab_add_buf(__maybe_unused struct cgpu_info *babcgpu, struct bab_info *babinfo, const uint8_t *data, size_t siz, const char *file, const char *func, const int line)
{
	uint32_t now_used;
	int buf;

	buf = babinfo->buffer;
	now_used = babinfo->buf_used[buf];
	if (now_used + siz >= BAB_MAXBUF) {
		quitfrom(1, file, func, line,
			"%s() buffer %d limit of %d exceeded=%d siz=%d",
			__func__, buf, BAB_MAXBUF, (int)(now_used + siz), (int)siz);
	}

	memcpy(&(babinfo->buf_write[buf][now_used]), data, siz);
	babinfo->buf_used[buf] += siz;
}

static void _bab_add_data(struct cgpu_info *babcgpu, struct bab_info *babinfo, uint32_t addr, const uint8_t *data, size_t siz, const char *file, const char *func, const int line)
{
	uint8_t tmp[3];
	int trf_siz;

	if (siz < BAB_ADD_MIN || siz > BAB_ADD_MAX) {
		quitfrom(1, file, func, line,
			"%s() called with invalid siz=%d (min=%d max=%d)",
			__func__, (int)siz, BAB_ADD_MIN, BAB_ADD_MAX);
	}
	trf_siz = siz / 4;
	tmp[0] = (trf_siz - 1) | 0xE0;
	tmp[1] = (addr >> 8) & 0xff;
	tmp[2] = addr & 0xff;
	_bab_add_buf(babcgpu, babinfo, tmp, sizeof(tmp), BAB_FFL_PASS);
	_bab_add_buf_rev(babcgpu, babinfo, data, siz, BAB_FFL_PASS);
}

static void _bab_config_reg(struct cgpu_info *babcgpu, struct bab_info *babinfo, uint32_t reg, bool enable, const char *file, const char *func, const int line)
{
	if (enable) {
		_bab_add_data(babcgpu, babinfo, BAB_REG_ADDR + reg*32,
				bab_reg_ena, sizeof(bab_reg_ena), BAB_FFL_PASS);
	} else {
		_bab_add_data(babcgpu, babinfo, BAB_REG_ADDR + reg*32,
				bab_reg_dis, sizeof(bab_reg_dis), BAB_FFL_PASS);
	}

}

static void bab_set_osc(struct bab_info *babinfo, int chip)
{
	int fast, i;

	fast = babinfo->chip_fast[chip];

	for (i = 0; i < BAB_OSC && fast > BAB_OSC; i++, fast -= BAB_OSC) {
		babinfo->osc[i] = 0xff;
	}
	if (i < BAB_OSC && fast > 0 && fast <= BAB_OSC)
		babinfo->osc[i++] = bab_osc_bits[fast - 1];
	for (; i < BAB_OSC; i++)
		babinfo->osc[i] = 0x00;

	applog(LOG_DEBUG, "@osc(chip=%d) fast=%d 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x", chip, fast, babinfo->osc[0], babinfo->osc[1], babinfo->osc[2], babinfo->osc[3], babinfo->osc[4], babinfo->osc[5], babinfo->osc[6], babinfo->osc[7]);
}

static bool bab_put(struct cgpu_info *babcgpu, struct bab_info *babinfo)
{
	int buf, i, reg, bank = 0;

	babinfo->buffer = -1;

	mutex_lock(&(babinfo->spi_lock));
	if (babinfo->buf_status[0] == BAB_STATE_DONE) {
		babinfo->buffer = 0;
	} else if (babinfo->buf_status[1] == BAB_STATE_DONE) {
		babinfo->buffer = 1;
	} else if (babinfo->buf_status[0] == BAB_STATE_READY) {
		babinfo->buf_status[0] = BAB_STATE_DONE;
		babinfo->buffer = 0;
	} else if (babinfo->buf_status[1] == BAB_STATE_READY) {
		babinfo->buf_status[1] = BAB_STATE_DONE;
		babinfo->buffer = 1;
	}
	mutex_unlock(&(babinfo->spi_lock));

	if (babinfo->buffer == -1)
		return false;

	buf = babinfo->buffer;
	babinfo->buf_used[buf] = 0;
	memset(babinfo->bank_off[buf], 0, sizeof(babinfo->bank_off) / BAB_SPI_BUFFERS);

	BAB_ADD_BREAK();
	for (i = 0; i < babinfo->chips; i++) {
		if (babinfo->chip_bank[i] != bank) {
			babinfo->bank_off[buf][bank] = babinfo->buf_used[buf];
			bank = babinfo->chip_bank[i];
			BAB_ADD_BREAK();
		}
		if (i == babinfo->fixchip &&
		    (BAB_CFGD_SET(babinfo->chip_conf[i]) ||
		     !babinfo->chip_conf[i])) {
			bab_set_osc(babinfo, i);
			bab_add_data(BAB_OSC_ADDR, babinfo->osc, sizeof(babinfo->osc));
			bab_config_reg(BAB_ICLK_REG, BAB_ICLK_BIT(babinfo->chip_conf[i]));
			bab_config_reg(BAB_FAST_REG, BAB_FAST_BIT(babinfo->chip_conf[i]));
			bab_config_reg(BAB_DIV2_REG, BAB_DIV2_BIT(babinfo->chip_conf[i]));
			bab_config_reg(BAB_SLOW_REG, BAB_SLOW_BIT(babinfo->chip_conf[i]));
			bab_config_reg(BAB_OCLK_REG, BAB_OCLK_BIT(babinfo->chip_conf[i]));
			for (reg = BAB_REG_CLR_FROM; reg <= BAB_REG_CLR_TO; reg++)
				bab_config_reg(reg, false);
			if (babinfo->chip_conf[i]) {
				bab_add_data(BAB_COUNT_ADDR, bab_counters, sizeof(bab_counters));
				bab_add_data(BAB_W1A_ADDR, bab_w1, sizeof(bab_w1));
				bab_add_data(BAB_W1B_ADDR, bab_w1, sizeof(bab_w1)/2);
				bab_add_data(BAB_W2_ADDR, bab_w2, sizeof(bab_w2));
				babinfo->chip_conf[i] ^= BAB_CFGD_VAL;
			}
			babinfo->old_fast[i] = babinfo->chip_fast[i];
			babinfo->old_conf[i] = babinfo->chip_conf[i];
		} else {
			if (babinfo->old_fast[i] != babinfo->chip_fast[i]) {
				bab_set_osc(babinfo, i);
				bab_add_data(BAB_OSC_ADDR, babinfo->osc, sizeof(babinfo->osc));
				babinfo->old_fast[i] = babinfo->chip_fast[i];
			}
			if (babinfo->old_conf[i] != babinfo->chip_conf[i]) {
				if (BAB_ICLK_SET(babinfo->old_conf[i]) !=
						 BAB_ICLK_SET(babinfo->chip_conf[i]))
					bab_config_reg(BAB_ICLK_REG,
							BAB_ICLK_BIT(babinfo->chip_conf[i]));
				if (BAB_FAST_SET(babinfo->old_conf[i]) !=
						 BAB_FAST_SET(babinfo->chip_conf[i]))
					bab_config_reg(BAB_FAST_REG,
							BAB_FAST_BIT(babinfo->chip_conf[i]));
				if (BAB_DIV2_SET(babinfo->old_conf[i]) !=
						 BAB_DIV2_SET(babinfo->chip_conf[i]))
					bab_config_reg(BAB_DIV2_REG,
							BAB_DIV2_BIT(babinfo->chip_conf[i]));
				if (BAB_SLOW_SET(babinfo->old_conf[i]) !=
						 BAB_SLOW_SET(babinfo->chip_conf[i]))
					bab_config_reg(BAB_SLOW_REG,
							BAB_SLOW_BIT(babinfo->chip_conf[i]));
				if (BAB_OCLK_SET(babinfo->old_conf[i]) !=
						 BAB_OCLK_SET(babinfo->chip_conf[i]))
					bab_config_reg(BAB_OCLK_REG,
							BAB_OCLK_BIT(babinfo->chip_conf[i]));
				babinfo->old_conf[i] = babinfo->chip_conf[i];
			}
		}
		babinfo->chip_off[buf][i] = babinfo->buf_used[buf] + 3;
		if (babinfo->chip_conf[i])
			bab_add_data(BAB_INP_ADDR, (uint8_t *)(&(babinfo->chip_input[i])),
					sizeof(babinfo->chip_input[i]));

		BAB_ADD_ASYNC();
	}
	babinfo->chip_off[buf][i] = babinfo->buf_used[buf];
	babinfo->bank_off[buf][bank] = babinfo->buf_used[buf];

	mutex_lock(&(babinfo->spi_lock));
	babinfo->buf_status[buf] = BAB_STATE_READY;
	mutex_unlock(&(babinfo->spi_lock));

	babinfo->fixchip = (babinfo->fixchip + 1) % babinfo->chips;
	return true;
}

static bool bab_get(__maybe_unused struct cgpu_info *babcgpu, struct bab_info *babinfo)
{
	int buf, i;

	babinfo->buffer = -1;

	mutex_lock(&(babinfo->spi_lock));
	if (babinfo->buf_status[0] == BAB_STATE_SENT) {
		babinfo->buf_status[0] = BAB_STATE_READING;
		babinfo->buffer = 0;
	} else if (babinfo->buf_status[1] == BAB_STATE_SENT) {
			babinfo->buf_status[1] = BAB_STATE_READING;
			babinfo->buffer = 1;
	}
	mutex_unlock(&(babinfo->spi_lock));

	if (babinfo->buffer == -1)
		return false;

	buf = babinfo->buffer;
	for (i = 0; i < babinfo->chips; i++) {
		if (babinfo->chip_conf[i] & 0x7f) {
			memcpy((void *)&(babinfo->chip_results[i]),
				(void *)(babinfo->buf_read[buf] + babinfo->chip_off[buf][i]),
				sizeof(babinfo->chip_results[0]));
		}
	}

	mutex_lock(&(babinfo->spi_lock));
	babinfo->buf_status[buf] = BAB_STATE_DONE;
	mutex_unlock(&(babinfo->spi_lock));

	return true;
}

void bab_detect_chips(struct cgpu_info *babcgpu, struct bab_info *babinfo, int bank, int first, int last)
{
	int buf, i, reg, j;

	if (sizeof(struct bab_work_send) != sizeof(bab_test_data)) {
		quithere(1, "struct bab_work_send (%d) and bab_test_data (%d)"
			    " must be the same size",
			    (int)sizeof(struct bab_work_send),
			    (int)sizeof(bab_test_data));
	}

	memset(babinfo->bank_off, 0, sizeof(babinfo->bank_off));

	buf = babinfo->buffer = 0;
	babinfo->buf_used[buf] = 0;
	BAB_ADD_BREAK();
	for (i = first; i < last && i < BAB_MAXCHIPS; i++) {
		bab_set_osc(babinfo, i);
		bab_add_data(BAB_OSC_ADDR, babinfo->osc, sizeof(babinfo->osc));
		bab_config_reg(BAB_ICLK_REG, BAB_ICLK_BIT(babinfo->chip_conf[i]));
		bab_config_reg(BAB_FAST_REG, BAB_FAST_BIT(babinfo->chip_conf[i]));
		bab_config_reg(BAB_DIV2_REG, BAB_DIV2_BIT(babinfo->chip_conf[i]));
		bab_config_reg(BAB_SLOW_REG, BAB_SLOW_BIT(babinfo->chip_conf[i]));
		bab_config_reg(BAB_OCLK_REG, BAB_OCLK_BIT(babinfo->chip_conf[i]));
		for (reg = BAB_REG_CLR_FROM; reg <= BAB_REG_CLR_TO; reg++)
			bab_config_reg(reg, false);
		bab_add_data(BAB_COUNT_ADDR, bab_counters, sizeof(bab_counters));
		bab_add_data(BAB_W1A_ADDR, bab_w1, sizeof(bab_w1));
		bab_add_data(BAB_W1B_ADDR, bab_w1, sizeof(bab_w1)/2);
		bab_add_data(BAB_W2_ADDR, bab_w2, sizeof(bab_w2));
		babinfo->chip_off[buf][i] = babinfo->buf_used[buf] + 3;
		bab_add_data(BAB_INP_ADDR, bab_test_data, sizeof(bab_test_data));
		babinfo->chip_off[buf][i+1] = babinfo->buf_used[buf];
		babinfo->bank_off[buf][bank] = babinfo->buf_used[buf];
		babinfo->chips = i + 1;
		bab_txrx(buf, babinfo->buf_used[buf], false);
		babinfo->buf_used[buf] = 0;
		BAB_ADD_BREAK();
		for (j = first; j <= i; j++) {
			babinfo->chip_off[buf][j] = babinfo->buf_used[buf] + 3;
			BAB_ADD_ASYNC();
		}
	}

	buf = babinfo->buffer = 1;
	babinfo->buf_used[buf] = 0;
	BAB_ADD_BREAK();
	for (i = first; i < last && i < BAB_MAXCHIPS; i++) {
		babinfo->chip_off[buf][i] = babinfo->buf_used[buf] + 3;
		bab_add_data(BAB_INP_ADDR, bab_test_data, sizeof(bab_test_data));
		BAB_ADD_ASYNC();
	}
	babinfo->chip_off[buf][i] = babinfo->buf_used[buf];
	babinfo->bank_off[buf][bank] = babinfo->buf_used[buf];
	babinfo->chips = i;
	bab_txrx(buf, babinfo->buf_used[buf], true);
	babinfo->buf_used[buf] = 0;
	babinfo->chips = first;
	for (i = first; i < last && i < BAB_MAXCHIPS; i++) {
		uint32_t tmp[DATA_UINTS-1];
		memcpy(tmp, babinfo->buf_read[buf]+babinfo->chip_off[buf][i], sizeof(tmp));
		for (j = 0; j < BAB_SPI_BUFFERS; j++)
			babinfo->chip_off[j][i] = 0;
		for (j = 0; j < BAB_REPLY_NONCES; j++) {
			if (tmp[j] != 0xffffffff && tmp[j] != 0x00000000) {
				babinfo->chip_bank[i] = bank;
				babinfo->chips = i + 1;
				break;
			}
		}
	}
	for (i = first ; i < babinfo->chips; i++)
		babinfo->chip_bank[i] = bank;
}

static const char *bab_modules[] = {
	"i2c-dev",
	"i2c-bcm2708",
	"spidev",
	"spi-bcm2708",
	NULL
};

static const char *bab_memory = "/dev/mem";

static int bab_memory_addr = 0x20200000;

static struct {
	int request;
	int value;
} bab_ioc[] = {
	{ SPI_IOC_RD_MODE, 0 },
	{ SPI_IOC_WR_MODE, 0 },
	{ SPI_IOC_RD_BITS_PER_WORD, 8 },
	{ SPI_IOC_WR_BITS_PER_WORD, 8 },
	{ SPI_IOC_RD_MAX_SPEED_HZ, 1000000 },
	{ SPI_IOC_WR_MAX_SPEED_HZ, 1000000 },
	{ -1, -1 }
};

static bool bab_init_gpio(struct cgpu_info *babcgpu, struct bab_info *babinfo, int bus, int chip)
{
	int i, err, memfd, data;
	char buf[64];

	for (i = 0; bab_modules[i]; i++) {
		snprintf(buf, sizeof(buf), "modprobe %s", bab_modules[i]);
		err = system(buf);
		if (err) {
			applog(LOG_ERR, "%s failed to modprobe %s (%d) - you need to be root?",
					babcgpu->drv->dname,
					bab_modules[i], err);
			goto bad_out;
		}
	}

	memfd = open(bab_memory, O_RDWR | O_SYNC);
	if (memfd < 0) {
		applog(LOG_ERR, "%s failed open %s (%d)",
				babcgpu->drv->dname,
				bab_memory, errno);
		goto bad_out;
	}

	babinfo->gpio = (volatile unsigned *)mmap(NULL, BAB_SPI_BUFSIZ,
						  PROT_READ | PROT_WRITE,
						  MAP_SHARED, memfd,
						  bab_memory_addr);
	if (babinfo->gpio == MAP_FAILED) {
		close(memfd);
		applog(LOG_ERR, "%s failed mmap gpio (%d)",
				babcgpu->drv->dname,
				errno);
		goto bad_out;
	}

	close(memfd);

	snprintf(buf, sizeof(buf), "/dev/spidev%d.%d", bus, chip);
	babinfo->spifd = open(buf, O_RDWR);
	if (babinfo->spifd < 0) {
		applog(LOG_ERR, "%s failed to open spidev (%d)",
				babcgpu->drv->dname,
				errno);
		goto map_out;
	}

	babcgpu->device_path = strdup(buf);

	for (i = 0; bab_ioc[i].value != -1; i++) {
		data = bab_ioc[i].value;
		err = ioctl(babinfo->spifd, bab_ioc[i].request, (void *)&data);
		if (err < 0) {
			applog(LOG_ERR, "%s failed ioctl (%d) (%d)",
					babcgpu->drv->dname,
					i, errno);
			goto close_out;
		}
	}

	for (i = 0; i < BAB_MAXCHIPS; i++)
		babinfo->chip_spis[i] = (int)((1000000.0 / (100.0 + 31.0 * (i + 1))) * 1000);

	return true;

close_out:
	close(babinfo->spifd);
	babinfo->spifd = 0;
	free(babcgpu->device_path);
	babcgpu->device_path = NULL;
map_out:
	munmap((void *)(babinfo->gpio), BAB_SPI_BUFSIZ);
	babinfo->gpio = NULL;
bad_out:
	return false;
}

static void bab_init_chips(struct cgpu_info *babcgpu, struct bab_info *babinfo)
{
	bab_detect_chips(babcgpu, babinfo, 0, 0, BAB_MAXCHIPS);
	memcpy(babinfo->old_conf, babinfo->chip_conf, sizeof(babinfo->old_conf));
	memcpy(babinfo->old_fast, babinfo->chip_fast, sizeof(babinfo->old_fast));
}

static void bab_detect(bool hotplug)
{
	struct cgpu_info *babcgpu = NULL;
	struct bab_info *babinfo = NULL;
	int i;

	if (hotplug)
		return;

	babcgpu = calloc(1, sizeof(*babcgpu));
	if (unlikely(!babcgpu))
		quithere(1, "Failed to calloc babcgpu");

	babcgpu->drv = &bab_drv;
	babcgpu->deven = DEV_ENABLED;
	babcgpu->threads = 1;

	babinfo = calloc(1, sizeof(*babinfo));
	if (unlikely(!babinfo))
		quithere(1, "Failed to calloc babinfo");
	babcgpu->device_data = (void *)babinfo;

	for (i = 0; i < BAB_MAXCHIPS; i++) {
		babinfo->chip_conf[i] = BAB_DEFCONF;
		babinfo->chip_fast[i] = BAB_DEFSPEED;
	}

	mutex_init(&babinfo->spi_lock);

	if (!bab_init_gpio(babcgpu, babinfo, BAB_SPI_BUS, BAB_SPI_CHIP))
		goto unalloc;

	applog(LOG_WARNING, "%s V1 testing for %d chips ...", babcgpu->drv->dname, BAB_MAXCHIPS);

	bab_init_chips(babcgpu, babinfo);

	applog(LOG_WARNING, "%s found %d chips", babcgpu->drv->dname, babinfo->chips);

	if (babinfo->chips == 0)
		goto cleanup;

	if (!add_cgpu(babcgpu))
		goto cleanup;

	mutex_init(&babinfo->res_lock);
	mutex_init(&babinfo->did_lock);
	cglock_init(&babinfo->blist_lock);

	babinfo->initialised = true;

	return;

cleanup:
	close(babinfo->spifd);
	munmap((void *)(babinfo->gpio), BAB_SPI_BUFSIZ);
unalloc:
	mutex_destroy(&babinfo->spi_lock);
	free(babinfo);
	free(babcgpu);
}

static void bab_identify(__maybe_unused struct cgpu_info *babcgpu)
{
}

#define BAB_LONG_WAIT_uS 1200000
#define BAB_WAIT_MSG_EVERY 10
#define BAB_LONG_WAIT_SLEEP_uS 100000
#define BAB_STD_WAIT_uS 3000

// thread to do spi txrx
static void *bab_spi(void *userdata)
{
	struct cgpu_info *babcgpu = (struct cgpu_info *)userdata;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	struct timeval start, stop;
	double wait;
	int i, buf, msgs;

	applog(LOG_DEBUG, "%s%i: SPIing...",
			  babcgpu->drv->name, babcgpu->device_id);

	// Wait until we're ready
	while (babcgpu->shutdown == false) {
		if (babinfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	msgs = 0;
	cgtime(&start);
	while (babcgpu->shutdown == false) {
		buf = -1;
		mutex_lock(&(babinfo->spi_lock));
		for (i = 0; i < BAB_SPI_BUFFERS; i++) {
			if (babinfo->buf_status[i] == BAB_STATE_READY) {
				babinfo->buf_status[i] = BAB_STATE_SENDING;
				buf = i;
				cgtime(&start);
				break;
			}
		}
		mutex_unlock(&(babinfo->spi_lock));

		if (buf == -1) {
			cgtime(&stop);
			wait = us_tdiff(&stop, &start);
			if (wait > BAB_LONG_WAIT_uS) {
				if ((msgs++ % BAB_WAIT_MSG_EVERY) == 0) {
					applog(LOG_WARNING, "%s%i: SPI waiting %.0fus ...",
								babcgpu->drv->name,
								babcgpu->device_id,
								(float)wait);
				}
			}
			cgsleep_us(BAB_LONG_WAIT_SLEEP_uS);
			continue;
		}

		bab_txrx(buf, babinfo->buf_used[buf], false);
		cgtime(&stop);
		wait = us_tdiff(&stop, &start);
		if (wait < BAB_STD_WAIT_uS)
			cgsleep_us((uint64_t)(BAB_STD_WAIT_uS - wait));
		else if (wait > BAB_LONG_WAIT_uS) {
			applog(LOG_DEBUG, "%s%i: SPI waited %.0fus",
					  babcgpu->drv->name, babcgpu->device_id,
					  (float)wait);
		}

		mutex_lock(&(babinfo->spi_lock));
		babinfo->buf_status[i] = BAB_STATE_SENT;
		mutex_unlock(&(babinfo->spi_lock));
		msgs = 0;
	}

	return NULL;
}

static void bab_flush_work(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);

	applog(LOG_DEBUG, "%s%i: flushing work",
			  babcgpu->drv->name, babcgpu->device_id);

	mutex_lock(&(babinfo->did_lock));
	memset(&(babinfo->last_did), 0, sizeof(babinfo->last_did));
	mutex_unlock(&(babinfo->did_lock));
}

static void ms3steps(uint32_t *p)
{
	uint32_t a, b, c, d, e, f, g, h, new_e, new_a;
	int i;

	a = p[0];
	b = p[1];
	c = p[2];
	d = p[3];
	e = p[4];
	f = p[5];
	g = p[6];
	h = p[7];
	for (i = 0; i < 3; i++) {
		new_e = p[i+16] + sha256_k[i] + h + CH(e,f,g) + SHA256_F2(e) + d;
		new_a = p[i+16] + sha256_k[i] + h + CH(e,f,g) + SHA256_F2(e) +
			SHA256_F1(a) + MAJ(a,b,c);
		d = c;
		c = b;
		b = a;
		a = new_a;
		h = g;
		g = f;
		f = e;
		e = new_e;
	}
	p[15] = a;
	p[14] = b;
	p[13] = c;
	p[12] = d;
	p[11] = e;
	p[10] = f;
	p[9] = g;
	p[8] = h;
}

#define DATA_MERKLE7 16
#define DATA_NTIME 17
#define DATA_BITS 18
#define DATA_NONCE 19

#define WORK_MERKLE7 (16*4)
#define WORK_NTIME (17*4)
#define WORK_BITS (18*4)
#define WORK_NONCE (19*4)

static uint32_t decnonce(uint32_t in)
{
	uint32_t out;

	/* First part load */
	out = (in & 0xFF) << 24;
	in >>= 8;

	/* Byte reversal */
	in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
	in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
	in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));

	out |= (in >> 2) & 0x3FFFFF;

	/* Extraction */
	if (in & 1)
		out |= (1 << 23);
	if (in & 2)
		out |= (1 << 22);

	out -= 0x800004;
	return out;
}

/*
 * Find the matching work item by checking the nonce against each work
 * item for the chip
 * Discard any work items older than a match
 */
static bool oknonce(struct thr_info *thr, struct cgpu_info *babcgpu, int chip, uint32_t nonce)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	BLIST *bitem;
	unsigned int links, tests;
	int i;

	babinfo->chip_nonces[chip]++;

	nonce = decnonce(nonce);

	/*
	 * We can grab the head of the chip work queue and then
	 * release the lock and follow it to the end
	 * since the other thread will only add items above the
	 * head - it wont touch the list->next pointers from the
	 * head to the end - only the head->prev pointer may get
	 * changed
	 */
	cg_rlock(&babinfo->blist_lock);
	bitem = babinfo->chip_list[chip];
	cg_runlock(&babinfo->blist_lock);

	if (!bitem) {
		applog(LOG_ERR, "%s%i: chip %d has no work!",
				babcgpu->drv->name, babcgpu->device_id, chip);
		babinfo->untested_nonces++;
		return false;
	}

	babinfo->tested_nonces++;

	tests = 0;
	links = 0;
	while (bitem) {
		if (!bitem->work) {
			applog(LOG_ERR, "%s%i: chip %d bitem links %d has no work!",
					babcgpu->drv->name,
					babcgpu->device_id,
					chip, links);
		} else {
			for (i = 0; i < BAB_NONCE_OFFSETS; i++) {
				tests++;
				if (test_nonce(bitem->work, nonce + bab_nonce_offsets[i])) {
					submit_tested_work(thr, bitem->work);
					babinfo->nonce_offset_count[i]++;
					babinfo->chip_good[chip]++;
					bitem->nonces++;
					babinfo->new_nonces++;
					babinfo->ok_nonces++;
					free_blist(babcgpu, bitem->next, chip);
					babinfo->total_tests += tests;
					if (babinfo->max_tests_per_nonce < tests)
						babinfo->max_tests_per_nonce = tests;
					babinfo->total_links += links;
					if (babinfo->max_links < links)
						babinfo->max_links = links;
					return true;
				}
			}
		}
		bitem = bitem->next;
		links++;
	}

	if (babinfo->not_first_reply[chip]) {
		babinfo->chip_bad[chip]++;
		inc_hw_errors(thr);
	} else
		babinfo->initial_ignored++;

	return false;
}

// Results checking thread
static void *bab_res(void *userdata)
{
	struct cgpu_info *babcgpu = (struct cgpu_info *)userdata;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	struct thr_info *thr = babcgpu->thr[0];
	bool first_second;
	uint32_t nonce;
	int chip;

	applog(LOG_DEBUG, "%s%i: Results...",
			  babcgpu->drv->name, babcgpu->device_id);

	// Wait until we're ready
	while (babcgpu->shutdown == false) {
		if (babinfo->initialised) {
			break;
		}
		cgsleep_ms(3);
	}

	while (babcgpu->shutdown == false) {
		if (!oldest_nonce(babcgpu, &chip, &nonce, &first_second)) {
			cgsleep_ms(3);
			continue;
		}

		if (first_second)
			babinfo->not_first_reply[chip] = true;

		oknonce(thr, babcgpu, chip, nonce);
	}

	return NULL;
}

static bool bab_do_work(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	int busy, newbusy, match, work_items = 0;
	int spi, mis, miso;
	int i, j;
	BLIST *bitem;
	bool res, got_a_nonce;

	for (i = 0; i < babinfo->chips; i++) {
		bitem = next_work(babcgpu, i);
		if (!bitem) {
			applog(LOG_ERR, "%s%i: short work list (%i) expected %d - discarded",
					babcgpu->drv->name, babcgpu->device_id,
					i, babinfo->chips);
			for (j = 0; j < i; i++)
				discard_last(babcgpu, j);

			return false;
		}
		memcpy((void *)&(babinfo->chip_input[i].midstate[0]),
			bitem->work->midstate, sizeof(bitem->work->midstate));
		memcpy((void *)&(babinfo->chip_input[i].merkle7),
			(void *)&(bitem->work->data[WORK_MERKLE7]), 12);

		ms3steps((void *)&(babinfo->chip_input[i]));
		work_items++;
	}

	// Send
	res = bab_put(babcgpu, babinfo);
	if (!res) {
		applog(LOG_DEBUG, "%s%i: couldn't put work ...",
				  babcgpu->drv->name, babcgpu->device_id);
	}

	// Receive
	res = bab_get(babcgpu, babinfo);
	if (!res) {
		applog(LOG_DEBUG, "%s%i: didn't get work reply ...",
				  babcgpu->drv->name, babcgpu->device_id);
		return false;
	}

	applog(LOG_DEBUG, "%s%i: Did get work reply ...",
			  babcgpu->drv->name, babcgpu->device_id);

	spi = mis = miso = 0;

	for (i = 0; i < babinfo->chips; i++) {
		match = 0;
		newbusy = busy = babinfo->chip_busy[i];

		if (!babinfo->chip_conf[i])
			continue;

		for (j = 1; j < BAB_REPLY_NONCES; j++) {
			if (babinfo->chip_results[i].nonce[(busy+j) % BAB_REPLY_NONCES] !=
			    babinfo->chip_prev[i].nonce[(busy+j) % BAB_REPLY_NONCES])
				newbusy = (busy+j) % BAB_REPLY_NONCES;
			else
				match++;
		}

		if (!match) {
			if (!miso) {
				mis++;
// ignore for now ...				babinfo->chip_miso[i]++;
			}
			miso = 1;
			continue;
		}

		miso = 0;
		if (babinfo->chip_results[i].jobsel != 0xffffffff &&
		    babinfo->chip_results[i].jobsel != 0x00000000) {
			spi++;
			babinfo->chip_spie[i]++;
			applog(LOG_DEBUG, "%s%i: SPI ERROR on chip %d (0x%08x)",
					  babcgpu->drv->name, babcgpu->device_id,
					  i, babinfo->chip_results[i].jobsel);
		}

// Not used yet
//		if (babinfo->chip_results[i].jobsel != babinfo->chip_prev[i].jobsel) {

		got_a_nonce = false;
		for (; newbusy != busy; busy = (busy + 1) % BAB_REPLY_NONCES) {
			if (babinfo->chip_results[i].nonce[busy] == 0xffffffff ||
			    babinfo->chip_results[i].nonce[busy] == 0x00000000) {
				babinfo->chip_results[i].nonce[busy] = babinfo->chip_prev[i].nonce[busy];
				spi = 1;
				continue;
			}

			store_nonce(babcgpu, i,
				    babinfo->chip_results[i].nonce[busy],
				    babinfo->nonce_before[i]);

			got_a_nonce = true;
		}

		/*
		 * We only care about this after the first reply we find a nonce
		 * After that, the value has no more effect
		 */
		if (got_a_nonce)
			babinfo->nonce_before[i] = true;

		mis += miso;
		babinfo->chip_miso[i] += miso;
		babinfo->chip_busy[i] = busy;
	}

	memcpy((void *)(&(babinfo->chip_prev[0])),
		(void *)(&(babinfo->chip_results[0])),
		sizeof(babinfo->chip_prev));

	applog(LOG_DEBUG, "Work: items:%d spi:%d miso:%d",
			  work_items, spi, mis);

	return true;
}

static bool bab_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *babcgpu = thr->cgpu;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);

	if (thr_info_create(&(babinfo->spi_thr), NULL, bab_spi, (void *)babcgpu)) {
		applog(LOG_ERR, "%s%i: SPI thread create failed",
				babcgpu->drv->name, babcgpu->device_id);
		return false;
	}
	pthread_detach(babinfo->spi_thr.pth);

	/*
	 * We require a seperate results checking thread since there is a lot
	 * of work done checking the results multiple times - thus we don't
	 * want that delay affecting sending/receiving work to/from the device
	 */
	if (thr_info_create(&(babinfo->res_thr), NULL, bab_res, (void *)babcgpu)) {
		applog(LOG_ERR, "%s%i: Results thread create failed",
				babcgpu->drv->name, babcgpu->device_id);
		return false;
	}
	pthread_detach(babinfo->res_thr.pth);

	return true;
}

static void bab_shutdown(struct thr_info *thr)
{
	struct cgpu_info *babcgpu = thr->cgpu;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	int i;

	applog(LOG_DEBUG, "%s%i: shutting down",
			  babcgpu->drv->name, babcgpu->device_id);

	for (i = 0; i < babinfo->chips; i++)
// TODO:	bab_shutdown(babcgpu, babinfo, i);
		;

	babcgpu->shutdown = true;
}

static bool bab_queue_full(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	struct work *work;
	bool ret;

	if (babinfo->work_count >= babinfo->chips)
		ret = true;
	else {
		work = get_queued(babcgpu);
		if (work)
			store_work(babcgpu, work);
		else
			// Avoid a hard loop when we can't get work fast enough
			cgsleep_ms(10);

		ret = false;
	}

	return ret;
}

/*
 * 1.0s per nonce = 4.2GH/s
 * So anything around 4GH/s or less per chip should be fine
 */
#define BAB_STD_WORK_uS 1000000

#define BAB_STD_DELAY_uS 30000

/*
 * TODO: allow this to run through more than once - the second+
 * time not sending any new work unless a flush occurs since:
 * at the moment we have BAB_STD_WORK_uS latency added to earliest replies
 */
static int64_t bab_scanwork(__maybe_unused struct thr_info *thr)
{
	struct cgpu_info *babcgpu = thr->cgpu;
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	int64_t hashcount = 0;
	struct timeval now;
	double delay;

	bab_do_work(babcgpu);

	// Sleep now so we get the work "bab_queue_full()" just before we use it
	while (80085) {
		cgtime(&now);
		mutex_lock(&(babinfo->did_lock));
		delay = us_tdiff(&now, &(babinfo->last_did));
		mutex_unlock(&(babinfo->did_lock));
		if (delay < (BAB_STD_WORK_uS - BAB_STD_DELAY_uS))
			cgsleep_us(BAB_STD_DELAY_uS);
		else
			break;
	}

	if (babinfo->new_nonces) {
		hashcount += 0xffffffffull * babinfo->new_nonces;
		babinfo->new_nonces = 0;
	}

	return hashcount;
}

#define CHIPS_PER_STAT 16

static struct api_data *bab_api_stats(struct cgpu_info *babcgpu)
{
	struct bab_info *babinfo = (struct bab_info *)(babcgpu->device_data);
	struct api_data *root = NULL;
	char data[2048];
	char buf[32];
	int i, to, j;

	if (babinfo->initialised == false)
		return NULL;

	root = api_add_int(root, "Chips", &(babinfo->chips), true);

	for (i = 0; i < babinfo->chips; i += CHIPS_PER_STAT) {
		to = i + CHIPS_PER_STAT - 1;
		if (to >= babinfo->chips)
			to = babinfo->chips - 1;

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					babinfo->chip_nonces[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Nonces %d - %d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					babinfo->chip_good[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Good %d - %d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s%"PRIu64,
					j == i ? "" : " ",
					babinfo->chip_bad[j]);
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Bad %d - %d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s0x%02x",
					j == i ? "" : " ",
					(int)(babinfo->chip_conf[j]));
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Conf %d - %d", i, to);
		root = api_add_string(root, buf, data, true);

		data[0] = '\0';
		for (j = i; j <= to; j++) {
			snprintf(buf, sizeof(buf),
					"%s0x%02x",
					j == i ? "" : " ",
					(int)(babinfo->chip_fast[j]));
			strcat(data, buf);
		}
		snprintf(buf, sizeof(buf), "Fast %d - %d", i, to);
		root = api_add_string(root, buf, data, true);
	}

	for (i = 0; i < BAB_NONCE_OFFSETS; i++) {
		snprintf(buf, sizeof(buf), "Nonce Offset 0x%08x", bab_nonce_offsets[i]);
		root = api_add_uint64(root, buf, &(babinfo->nonce_offset_count[i]), true);
	}

	root = api_add_uint64(root, "Tested", &(babinfo->tested_nonces), true);
	root = api_add_uint64(root, "Total Tests", &(babinfo->total_tests), true);
	root = api_add_uint64(root, "Max Tests", &(babinfo->max_tests_per_nonce), true);
	float avg = babinfo->tested_nonces ? (float)(babinfo->total_tests) /
					     (float)(babinfo->tested_nonces) : 0;
// TODO: add a API_AVG which is 3 places - double/float?
	root = api_add_volts(root, "Avg Tests", &avg, true);
	root = api_add_uint64(root, "Untested", &(babinfo->untested_nonces), true);

	root = api_add_uint64(root, "Work Links", &(babinfo->total_links), true);
	root = api_add_uint64(root, "Max Links", &(babinfo->max_links), true);
	avg = babinfo->tested_nonces ? (float)(babinfo->total_links) /
					(float)(babinfo->tested_nonces) : 0;
	root = api_add_volts(root, "Avg Links", &avg, true);

	root = api_add_uint32(root, "Initial Ignored", &(babinfo->initial_ignored), true);

	root = api_add_int(root, "BList Count", &(babinfo->blist_count), true);
	root = api_add_int(root, "BFree Count", &(babinfo->bfree_count), true);
	root = api_add_int(root, "Work Count", &(babinfo->work_count), true);
	root = api_add_int(root, "Chip Count", &(babinfo->chip_count), true);

	root = api_add_int(root, "RList Count", &(babinfo->rlist_count), true);
	root = api_add_int(root, "RFree Count", &(babinfo->rfree_count), true);
	root = api_add_int(root, "Result Count", &(babinfo->res_count), true);

	return root;
}
#endif

struct device_drv bab_drv = {
	.drv_id = DRIVER_bab,
	.dname = "BlackArrowBitFuryGPIO",
	.name = "BaB",
	.drv_detect = bab_detect,
#ifdef LINUX
	.get_api_stats = bab_api_stats,
//TODO:	.get_statline_before = get_bab_statline_before,
	.identify_device = bab_identify,
	.thread_prepare = bab_thread_prepare,
	.hash_work = hash_queued_work,
	.scanwork = bab_scanwork,
	.queue_full = bab_queue_full,
	.flush_work = bab_flush_work,
	.thread_shutdown = bab_shutdown
#endif
};
