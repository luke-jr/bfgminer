/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef AVALON_H
#define AVALON_H

struct avalon_task {
	uint8_t reset		:1;
	uint8_t flush_fifo	:1;
	uint8_t fan_eft		:1;
	uint8_t timer_eft	:1;
	uint8_t chip_num	:4;
	uint8_t fan_pwm_data;
	uint8_t timeout_data;
	uint8_t miner_num;	// Word[0]

	uint8_t nonce_elf	:1;
	uint32_t miner_ctrl	:31;
	uint32_t pad0;		//Word[2:1]

	uint32_t midstate[8];
	uint32_t data[3];

	// nonce_range: Word[??:14]
} __attribute__((packed));

struct avalon_result {
	uint32_t data[3];
	uint32_t midstate[8];
	uint32_t nonce;
	uint32_t reserved;
} __attribute__((packed));

#define AVALON_MINER_THREADS 1
#define AVALON_IO_SPEED 115200
#define AVALON_READ_SIZE 8
#define AVALON_READ_FAULT_DECISECONDS 10
#define TIME_FACTOR 10

#define AVA_GETS_ERROR -1
#define AVA_GETS_OK 0
#define AVA_GETS_RESTART 1
#define AVA_GETS_TIMEOUT 2

#define avalon_open2(devpath, baud, purge)  serial_open(devpath, baud, AVALON_READ_FAULT_DECISECONDS, purge)
#define avalon_open(devpath, baud)  avalon_open2(devpath, baud, false)
#define avalon_close(fd) close(fd)

#endif	/* AVALON_H */
