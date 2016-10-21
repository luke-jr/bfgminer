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

#include <curl/curl.h>

#include "util.h"

//#define BITMAIN_TYPE_S1
//#define BITMAIN_TYPE_S2
//#define BITMAIN_TYPE_S3
#define BITMAIN_TYPE_S4

#define BITMAIN_MINER_THREADS 1

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

#define BITMAIN_MAX_CHAIN_NUM      16

#define BITMAIN_TASK_HEADER_SIZE   8
#define BITMAIN_TASK_FOOTER_SIZE   2
#define BITMAIN_WORK_SIZE          0x30

#define BITMAIN_MAX_PACKET_MAX_NONCE  0x80
#define BITMAIN_MAX_TEMP_NUM       32
#define BITMAIN_MAX_FAN_NUM        32

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
	struct bitmain_rxnonce_nonce nonces[BITMAIN_MAX_PACKET_MAX_NONCE];
	uint16_t crc;
} __attribute__((packed, aligned(4)));

enum bitmain_chip {
	BMC_UNKNOWN,
	BMC_BM1380,
	BMC_BM1382,
	BMC_BM1384,
};

struct bitmain_info {
	void *device_curl;
	SOCKETTYPE curl_sock;
	
	enum bitmain_chip chip_type;
	int chain_num;
	int asic_num;
	int chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
	uint32_t chain_asic_exist[BITMAIN_MAX_CHAIN_NUM*8];
	uint32_t chain_asic_status[BITMAIN_MAX_CHAIN_NUM*8];
	char chain_asic_status_t[BITMAIN_MAX_CHAIN_NUM][320];
	int timeout;
	int errorcount;
	int errorcount2;
	uint32_t nonce_error;
	uint32_t last_nonce_error;
	uint8_t reg_data[4];
	
	unsigned packet_max_work;  // BITMAIN_MAX_WORK_NUM
	unsigned poll_prio_threshold;  // BITMAIN_MAX_WORK_QUEUE_NUM
	unsigned packet_max_nonce;     // BITMAIN_MAX_NONCE_NUM

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

	int frequency;
	char frequency_t[256];
	uint8_t voltage[2];
	char voltage_t[8];

	int diff;
	float lowest_goal_diff;
	uint32_t next_work_id;

	int no_matching_work;
	//int matching_work[BITMAIN_DEFAULT_CHAIN_NUM];

	pthread_mutex_t qlock;
	int fifo_space;
	int max_fifo_space;
	int hw_version[4];

	int ready_to_queue;
	uint8_t *queuebuf;
	
	bool reset;
	bool work_restart;
	
	char g_miner_version[256];
	
	uint8_t readbuf[BITMAIN_READBUF_SIZE];
	int readbuf_offset;
};

#define BITMAIN_READ_SIZE 12

#define BTM_GETS_ERROR -1
#define BTM_GETS_OK 0

#define BTM_SEND_ERROR -1
#define BTM_SEND_OK 0

#define BITMAIN_READ_TIME(baud) ((double)BITMAIN_READ_SIZE * (double)8.0 / (double)(baud))
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]

#endif /* USE_BITMAIN */
#endif	/* BITMAIN_H */
