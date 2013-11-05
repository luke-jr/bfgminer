/*
 * Copyright 2013 Avalon project
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef AVALON_H
#define AVALON_H

#ifdef USE_AVALON

#include "util.h"

#define AVALON_RESET_FAULT_DECISECONDS 1
#define AVALON_MINER_THREADS 1

#define AVALON_IO_SPEED		115200
#define AVALON_HASH_TIME_FACTOR	((float)1.67/0x32)
#define AVALON_RESET_PITCH	(300*1000*1000)

#define AVALON_FAN_FACTOR 120
#define AVALON_PWM_MAX 0xA0
#define AVALON_DEFAULT_FAN_MIN 20
#define AVALON_DEFAULT_FAN_MAX 100
#define AVALON_DEFAULT_FAN_MAX_PWM 0xA0 /* 100% */
#define AVALON_DEFAULT_FAN_MIN_PWM 0x20 /*  20% */

#define AVALON_TEMP_TARGET 50
#define AVALON_TEMP_HYSTERESIS 3
#define AVALON_TEMP_OVERHEAT 60

/* Avalon-based BitBurner. */
#define BITBURNER_DEFAULT_CORE_VOLTAGE 1200 /* in millivolts */
#define BITBURNER_MIN_COREMV 1000
/* change here if you want to risk killing it :)  */
#define BITBURNER_MAX_COREMV 1400

/* BitFury-based BitBurner. */
#define BITBURNER_FURY_DEFAULT_CORE_VOLTAGE 900 /* in millivolts */
#define BITBURNER_FURY_MIN_COREMV 700
/* change here if you want to risk killing it :)  */
#define BITBURNER_FURY_MAX_COREMV 1100


#define AVALON_DEFAULT_TIMEOUT 0x2D
#define AVALON_MIN_FREQUENCY 256
#define AVALON_MAX_FREQUENCY 1024
#define AVALON_TIMEOUT_FACTOR 12690
#define AVALON_DEFAULT_FREQUENCY 282
#define AVALON_DEFAULT_MINER_NUM 0x20
#define AVALON_MAX_MINER_NUM 0x100
#define AVALON_DEFAULT_ASIC_NUM 0xA

/* Default number of miners for Bitburner Fury is for a stack of 8 boards,
   but it will work acceptably for smaller stacks, too */
#define BITBURNER_FURY_DEFAULT_MINER_NUM 128
#define BITBURNER_FURY_DEFAULT_FREQUENCY 256
#define BITBURNER_FURY_DEFAULT_TIMEOUT 50

#define AVALON_AUTO_CYCLE 1024

#define AVALON_FTDI_READSIZE 510
#define AVALON_READBUF_SIZE 8192
#define AVALON_RESET_TIMEOUT 100
#define AVALON_READ_TIMEOUT 18 /* Enough to only half fill the buffer */
#define AVALON_LATENCY 1

struct avalon_task {
	uint8_t reset		:1;
	uint8_t flush_fifo	:1;
	uint8_t fan_eft		:1;
	uint8_t timer_eft	:1;
	uint8_t asic_num	:4;
	uint8_t fan_pwm_data;
	uint8_t timeout_data;
	uint8_t miner_num;

	uint8_t nonce_elf		:1;
	uint8_t gate_miner_elf		:1;
	uint8_t asic_pll		:1;
	uint8_t gate_miner		:1;
	uint8_t _pad0			:4;
	uint8_t _pad1[3];
	uint32_t _pad2;

	uint8_t midstate[32];
	uint8_t data[12];
} __attribute__((packed, aligned(4)));

struct avalon_result {
	uint32_t nonce;
	uint8_t data[12];
	uint8_t midstate[32];

	uint8_t fan0;
	uint8_t fan1;
	uint8_t fan2;
	uint8_t temp0;
	uint8_t temp1;
	uint8_t temp2;
	uint8_t _pad0[2];

	uint16_t fifo_wp;
	uint16_t fifo_rp;
	uint8_t chip_num;
	uint8_t pwm_data;
	uint8_t timeout;
	uint8_t miner_num;
} __attribute__((packed, aligned(4)));

struct avalon_info {
	int baud;
	int miner_count;
	int asic_count;
	int timeout;

	int fan0;
	int fan1;
	int fan2;

	int temp0;
	int temp1;
	int temp2;
	int temp_max;
	int temp_history_count;
	int temp_history_index;
	int temp_sum;
	int temp_old;
	int fan_pwm;

	int core_voltage;

	int no_matching_work;
	int matching_work[AVALON_MAX_MINER_NUM];

	int frequency;
	uint32_t ctlr_ver;

	struct thr_info *thr;
	pthread_t read_thr;
	pthread_t write_thr;
	pthread_mutex_t lock;
	pthread_mutex_t qlock;
	cgsem_t qsem;
	int nonces;

	int auto_queued;
	int auto_nonces;
	int auto_hw;

	int idle;
	bool reset;
	bool overheat;
	bool optimal;

	uint8_t version1;
	uint8_t version2;
	uint8_t version3;
};

#define BITBURNER_VERSION1 1
#define BITBURNER_VERSION2 0
#define BITBURNER_VERSION3 0

#define AVALON_WRITE_SIZE (sizeof(struct avalon_task))
#define AVALON_READ_SIZE (sizeof(struct avalon_result))
#define AVALON_ARRAY_SIZE 3
#define BITBURNER_ARRAY_SIZE 4

#define AVA_GETS_ERROR -1
#define AVA_GETS_OK 0

#define AVA_SEND_ERROR -1
#define AVA_SEND_OK 0

#define avalon_buffer_full(avalon) !usb_ftdi_cts(avalon)

#define AVALON_READ_TIME(baud) ((double)AVALON_READ_SIZE * (double)8.0 / (double)(baud))
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

extern struct avalon_info **avalon_info;
extern int opt_avalon_temp;
extern int opt_avalon_overheat;
extern int opt_avalon_fan_min;
extern int opt_avalon_fan_max;
extern int opt_avalon_freq_min;
extern int opt_avalon_freq_max;
extern bool opt_avalon_auto;
extern int opt_bitburner_core_voltage;
extern int opt_bitburner_fury_core_voltage;
extern char *set_avalon_fan(char *arg);
extern char *set_avalon_freq(char *arg);

#endif /* USE_AVALON */
#endif	/* AVALON_H */
