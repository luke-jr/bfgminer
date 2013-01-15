/*
 * Copyright 2013 Xiangfu <xiangfu@openmobilefree.com>
 *
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
	uint8_t asic_num	:4;
	uint8_t fan_pwm_data;
	uint8_t timeout_data;
	uint8_t miner_num;

	uint8_t nonce_elf		:1;
	uint32_t pad0_miner_ctrl	:31;
	uint32_t pad1_miner_ctrl;

	uint8_t midstate[32];
	uint8_t data[12];
} __attribute__((packed, aligned(4)));

struct avalon_result {
	uint32_t nonce;
	uint8_t data[12];
	uint8_t midstate[32];
	uint8_t reserved[16];
} __attribute__((packed, aligned(4)));

struct avalon_info {
	double Hs;
	int read_count;
	double fullnonce;

	int baud;
	int miner_count;
	int asic_count;
	int timeout;
};

#define TIME_FACTOR 10
#define AVALON_RESET_FAULT_DECISECONDS 1
#define AVALON_READ_COUNT_TIMING	(5 * TIME_FACTOR)
#define AVALON_HASH_TIME (0.0000000026316 / 3)
#define NANOSEC 1000000000.0


#define AVALON_MINER_THREADS 1

#define AVALON_IO_SPEED 115200
#define AVALON_SEND_WORK_PITCH (32*1000*1000)
#define AVALON_RESET_PITCH     (80*1000*1000)

#define AVALON_DEFAULT_FAN_PWM 0x98
#define AVALON_DEFAULT_TIMEOUT 0x32
#define AVALON_DEFAULT_MINER_NUM 24
#define AVALON_DEFAULT_ASIC_NUM 0xA

#define AVALON_WRITE_SIZE (sizeof(struct avalon_task))
#define AVALON_READ_SIZE (sizeof(struct avalon_result))

#define AVA_GETS_ERROR -1
#define AVA_GETS_OK 0
#define AVA_GETS_RESTART 1
#define AVA_GETS_TIMEOUT 2

#define AVA_SEND_ERROR -1
#define AVA_SEND_OK 0
#define AVA_SEND_BUFFER_EMPTY 1
#define AVA_SEND_BUFFER_FULL 2

#define AVA_BUFFER_FULL 0
#define AVA_BUFFER_EMPTY 1

#define avalon_open2(devpath, baud, purge)  serial_open(devpath, baud, AVALON_RESET_FAULT_DECISECONDS, purge)
#define avalon_open(devpath, baud)  avalon_open2(devpath, baud, true)
#define avalon_close(fd) close(fd)

#define avalon_buffer_full(fd)	get_serial_cts(fd)

#define AVALON_READ_TIME(baud) ((double)AVALON_READ_SIZE * (double)8.0 / (double)(baud))
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

extern  struct avalon_info **avalon_info;

static inline uint8_t rev8(uint8_t d)
{
    int i;
    uint8_t out = 0;

    /* (from left to right) */
    for (i = 0; i < 8; i++)
        if (d & (1 << i))
            out |= (1 << (7 - i));

    return out;
}

#endif	/* AVALON_H */
