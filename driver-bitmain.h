/*
 * Copyright 2013 BitMain project
 * Copyright 2013 BitMain <xlc1985@126.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BITMAIN_H
#define BITMAIN_H

#ifdef USE_BITMAIN

#include "util.h"

//#define BITMAIN_TYPE_S1
//#define BITMAIN_TYPE_S2
//#define BITMAIN_TYPE_S3
#define BITMAIN_TYPE_S4

#define BITMAIN_RESET_FAULT_DECISECONDS 1
#define BITMAIN_MINER_THREADS 1

#define BITMAIN_IO_SPEED		115200
#define BITMAIN_HASH_TIME_FACTOR	((float)1.67/0x32)
#define BITMAIN_RESET_PITCH	(300*1000*1000)

#define BITMAIN_TOKEN_TYPE_TXCONFIG 0x51
#define BITMAIN_TOKEN_TYPE_TXTASK   0x52
#define BITMAIN_TOKEN_TYPE_RXSTATUS 0x53

#define BITMAIN_DATA_TYPE_RXSTATUS  0xa1
#define BITMAIN_DATA_TYPE_RXNONCE   0xa2

#define BITMAIN_FAN_FACTOR 60
#define BITMAIN_PWM_MAX 0xA0
#define BITMAIN_DEFAULT_FAN_MIN 20
#define BITMAIN_DEFAULT_FAN_MAX 100
#define BITMAIN_DEFAULT_FAN_MAX_PWM 0xA0 /* 100% */
#define BITMAIN_DEFAULT_FAN_MIN_PWM 0x20 /*  20% */

#define BITMAIN_TEMP_TARGET 50
#define BITMAIN_TEMP_HYSTERESIS 3
#define BITMAIN_TEMP_OVERHEAT 60

#define BITMAIN_DEFAULT_TIMEOUT 0x2D
#define BITMAIN_MIN_FREQUENCY 10
#define BITMAIN_MAX_FREQUENCY 1000000
#define BITMAIN_TIMEOUT_FACTOR 12690
#define BITMAIN_DEFAULT_FREQUENCY 282
#define BITMAIN_DEFAULT_VOLTAGE_T "0725"
#define BITMAIN_DEFAULT_VOLTAGE0 0x07
#define BITMAIN_DEFAULT_VOLTAGE1 0x25
#define BITMAIN_DEFAULT_CHAIN_NUM 8
#define BITMAIN_DEFAULT_ASIC_NUM 32
#define BITMAIN_DEFAULT_REG_DATA 0

#define BITMAIN_AUTO_CYCLE 1024

#define BITMAIN_FTDI_READSIZE 2048
#define BITMAIN_USB_PACKETSIZE 512
#define BITMAIN_SENDBUF_SIZE 8192
#define BITMAIN_READBUF_SIZE 8192
#define BITMAIN_RESET_TIMEOUT 100
#define BITMAIN_READ_TIMEOUT 18 /* Enough to only half fill the buffer */
#define BITMAIN_LATENCY 1

#ifdef BITMAIN_TYPE_S1
#define BITMAIN_MAX_WORK_NUM       8
#define BITMAIN_MAX_WORK_QUEUE_NUM 64
#define BITMAIN_MAX_DEAL_QUEUE_NUM 1
#define BITMAIN_MAX_NONCE_NUM      8
#define BITMAIN_MAX_CHAIN_NUM      8
#define BITMAIN_MAX_TEMP_NUM       32
#define BITMAIN_MAX_FAN_NUM        32
#define BITMAIN_ARRAY_SIZE         16384
#define BITMAIN_SEND_STATUS_TIME   10 //s
#define BITMAIN_SEND_FULL_SPACE    128
#endif

#ifdef BITMAIN_TYPE_S2
#define BITMAIN_MAX_WORK_NUM       64
#define BITMAIN_MAX_WORK_QUEUE_NUM 4096
#define BITMAIN_MAX_DEAL_QUEUE_NUM 32
#define BITMAIN_MAX_NONCE_NUM      128
#define BITMAIN_MAX_CHAIN_NUM      16
#define BITMAIN_MAX_TEMP_NUM       32
#define BITMAIN_MAX_FAN_NUM        32
#define BITMAIN_ARRAY_SIZE         16384
#define BITMAIN_SEND_STATUS_TIME   15 //s
#define BITMAIN_SEND_FULL_SPACE    512
#endif

#ifdef BITMAIN_TYPE_S3
#define BITMAIN_MAX_WORK_NUM       8
#define BITMAIN_MAX_WORK_QUEUE_NUM 1024
#define BITMAIN_MAX_DEAL_QUEUE_NUM 2
#define BITMAIN_MAX_NONCE_NUM      128
#define BITMAIN_MAX_CHAIN_NUM      8
#define BITMAIN_MAX_TEMP_NUM       32
#define BITMAIN_MAX_FAN_NUM        32
#define BITMAIN_ARRAY_SIZE         16384
#define BITMAIN_SEND_STATUS_TIME   15 //s
#define BITMAIN_SEND_FULL_SPACE    256
#endif

#ifdef BITMAIN_TYPE_S4
#define BITMAIN_MAX_WORK_NUM       64
#define BITMAIN_MAX_WORK_QUEUE_NUM 4096
#define BITMAIN_MAX_DEAL_QUEUE_NUM 32
#define BITMAIN_MAX_NONCE_NUM      128
#define BITMAIN_MAX_CHAIN_NUM      16
#define BITMAIN_MAX_TEMP_NUM       32
#define BITMAIN_MAX_FAN_NUM        32
#define BITMAIN_ARRAY_SIZE         16384*2
#define BITMAIN_SEND_STATUS_TIME   15 //s
#define BITMAIN_SEND_FULL_SPACE    512
#endif

struct bitmain_packet_head {
	uint8_t token_type;
	uint8_t version;
	uint16_t length;
} __attribute__((packed, aligned(4)));

struct bitmain_txconfig_token {
	uint8_t token_type;
	uint8_t version;
	uint16_t length;
	uint8_t reset                :1;
	uint8_t fan_eft              :1;
	uint8_t timeout_eft          :1;
	uint8_t frequency_eft        :1;
	uint8_t voltage_eft          :1;
	uint8_t chain_check_time_eft :1;
	uint8_t chip_config_eft      :1;
	uint8_t hw_error_eft         :1;
	uint8_t beeper_ctrl          :1;
	uint8_t temp_over_ctrl       :1;
	uint8_t fan_home_mode        :1;
	uint8_t reserved1            :5;
	uint8_t chain_check_time;
	uint8_t reserved2;

	uint8_t chain_num;
	uint8_t asic_num;
	uint8_t fan_pwm_data;
	uint8_t timeout_data;

	uint16_t frequency;
	uint8_t voltage[2];

	uint8_t reg_data[4];
	uint8_t chip_address;
	uint8_t reg_address;
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_txtask_work {
	uint32_t work_id;
	uint8_t midstate[32];
	uint8_t data2[12];
} __attribute__((packed, aligned(4)));

struct bitmain_txtask_token {
	uint8_t token_type;
	uint8_t version;
	uint16_t length;
	uint8_t new_block            :1;
	uint8_t reserved1            :7;
	uint8_t diff;
	uint16_t net_diff;
	struct bitmain_txtask_work works[BITMAIN_MAX_WORK_NUM];
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_rxstatus_token {
	uint8_t token_type;
	uint8_t version;
	uint16_t length;
	uint8_t chip_status_eft      :1;
	uint8_t detect_get           :1;
	uint8_t reserved1            :6;
	uint8_t reserved2[3];

	uint8_t chip_address;
	uint8_t reg_address;
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_rxstatus_data {
	uint8_t data_type;
	uint8_t version;
	uint16_t length;
	uint8_t chip_value_eft       :1;
	uint8_t reserved1            :3;
	uint8_t get_blk_num          :4;
	uint8_t chain_num;
	uint16_t fifo_space;
	uint8_t hw_version[4];
	uint8_t fan_num;
	uint8_t temp_num;
	uint16_t fan_exist;
	uint32_t temp_exist;
	uint32_t nonce_error;
	uint32_t reg_value;
	uint32_t chain_asic_exist[BITMAIN_MAX_CHAIN_NUM*8];
	uint32_t chain_asic_status[BITMAIN_MAX_CHAIN_NUM*8];
	uint8_t chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
	uint8_t temp[BITMAIN_MAX_TEMP_NUM];
	uint8_t fan[BITMAIN_MAX_FAN_NUM];
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_rxnonce_nonce {
	uint32_t work_id;
	uint32_t nonce;
} __attribute__((packed, aligned(4)));

struct bitmain_rxnonce_data {
	uint8_t data_type;
	uint8_t version;
	uint16_t length;
	uint16_t fifo_space;
	uint16_t diff;
	uint64_t total_nonce_num;
	struct bitmain_rxnonce_nonce nonces[BITMAIN_MAX_NONCE_NUM];
	uint16_t crc;
} __attribute__((packed, aligned(4)));

struct bitmain_info {
	int baud;
	int chain_num;
	int asic_num;
	int chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
	uint32_t chain_asic_exist[BITMAIN_MAX_CHAIN_NUM*8];
	uint32_t chain_asic_status[BITMAIN_MAX_CHAIN_NUM*8];
	char chain_asic_status_t[BITMAIN_MAX_CHAIN_NUM][320];
	int timeout;
	int errorcount;
	uint32_t nonce_error;
	uint32_t last_nonce_error;
	uint8_t reg_data[4];

	int fan_num;
	int fan[BITMAIN_MAX_FAN_NUM];
	int temp_num;
	int temp[BITMAIN_MAX_TEMP_NUM];

	int temp_max;
	int temp_avg;
	int temp_history_count;
	int temp_history_index;
	int temp_sum;
	int temp_old;
	int fan_pwm;
	uint64_t total_nonce_num;

	int frequency;
	char frequency_t[256];
	uint8_t voltage[2];
	char voltage_t[8];

	int diff;

	int no_matching_work;
	//int matching_work[BITMAIN_DEFAULT_CHAIN_NUM];

	struct thr_info *thr;
	pthread_t read_thr;
	pthread_t write_thr;
	pthread_mutex_t lock;
	pthread_mutex_t qlock;
	pthread_cond_t qcond;
	cgsem_t write_sem;
	int nonces;
	int fifo_space;
	int hw_version[4];
	unsigned int last_work_block;
	struct timeval last_status_time;
	int send_full_space;

	int auto_queued;
	int auto_nonces;
	int auto_hw;

	int idle;
	bool reset;
	bool overheat;
	bool optimal;
};

#define BITMAIN_READ_SIZE 12

#define BTM_GETS_ERROR -1
#define BTM_GETS_OK 0

#define BTM_SEND_ERROR -1
#define BTM_SEND_OK 0

#define BITMAIN_READ_TIME(baud) ((double)BITMAIN_READ_SIZE * (double)8.0 / (double)(baud))
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

extern struct bitmain_info **bitmain_info;
extern char opt_bitmain_dev[256];
extern int opt_bitmain_temp;
extern int opt_bitmain_overheat;
extern int opt_bitmain_fan_min;
extern int opt_bitmain_fan_max;
extern bool opt_bitmain_auto;
extern char *set_bitmain_dev(char *arg);
extern char *set_bitmain_fan(char *arg);

#endif /* USE_BITMAIN */
#endif	/* BITMAIN_H */
