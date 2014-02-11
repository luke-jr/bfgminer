/*
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 * Copyright 2012-2014 Xiangfu <xiangfu@openmobilefree.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef _AVALON2_H_
#define _AVALON2_H_

#include "util.h"
#include "fpgautils.h"

#ifdef USE_AVALON2

#define AVA2_MINER_THREADS	1

#define AVA2_RESET_FAULT_DECISECONDS	10
#define AVA2_IO_SPEED		115200

#define AVA2_DEFAULT_MINERS	10
#define AVA2_DEFAULT_MODULARS	3

#define AVA2_PWM_MAX	0x3FF
#define AVA2_DEFAULT_FAN_PWM	80 /* % */
#define AVA2_DEFAULT_FAN_MIN	0
#define AVA2_DEFAULT_FAN_MAX	100

#define AVA2_DEFAULT_VOLTAGE	10000 /* V * 10000 */
#define AVA2_DEFAULT_VOLTAGE_MIN	6000
#define AVA2_DEFAULT_VOLTAGE_MAX	11000

#define AVA2_DEFAULT_FREQUENCY	1500 /* In MH/s */
#define AVA2_DEFAULT_FREQUENCY_MIN	300
#define AVA2_DEFAULT_FREQUENCY_MAX	2000

/* Avalon2 protocol package type */
#define AVA2_H1	'A'
#define AVA2_H2	'V'

#define AVA2_P_COINBASE_SIZE	(6 * 1024)
#define AVA2_P_MERKLES_COUNT	20

#define AVA2_P_COUNT	39
#define AVA2_P_DATA_LEN		(AVA2_P_COUNT - 7)

#define AVA2_P_DETECT	10
#define AVA2_P_STATIC	11
#define AVA2_P_JOB_ID	12
#define AVA2_P_COINBASE	13
#define AVA2_P_MERKLES	14
#define AVA2_P_HEADER	15
#define AVA2_P_POLLING  16
#define AVA2_P_TARGET	17
#define AVA2_P_REQUIRE	18
#define AVA2_P_SET	19
#define AVA2_P_TEST	20

#define AVA2_P_ACK		21
#define AVA2_P_NAK		22
#define AVA2_P_NONCE		23
#define AVA2_P_STATUS		24
#define AVA2_P_ACKDETECT	25
#define AVA2_P_TEST_RET		26
/* Avalon2 protocol package type */

struct avalon2_pkg {
	uint8_t head[2];
	uint8_t type;
	uint8_t idx;
	uint8_t cnt;
	uint8_t data[32];
	uint8_t crc[2];
};
#define avalon2_ret avalon2_pkg

struct avalon2_info {
	int fd;
	int baud;

	int set_frequency;
	int set_voltage;

	int get_voltage[AVA2_DEFAULT_MODULARS];
	int get_frequency[AVA2_DEFAULT_MODULARS];

	int fan_pwm;

	int fan[2 * AVA2_DEFAULT_MODULARS];
	int temp[2 * AVA2_DEFAULT_MODULARS];

	int temp_max;
	int temp_history_count;
	int temp_history_index;
	int temp_sum;
	int temp_old;

	bool first;
	bool new_stratum;

	int pool_no;
	int diff;

	int local_works[AVA2_DEFAULT_MODULARS];
	int hw_works[AVA2_DEFAULT_MODULARS];
	int matching_work[AVA2_DEFAULT_MINERS * AVA2_DEFAULT_MODULARS];
	int local_work[AVA2_DEFAULT_MODULARS];
	int hw_work[AVA2_DEFAULT_MODULARS];

	int modulars[AVA2_DEFAULT_MODULARS];
	char mm_version[AVA2_DEFAULT_MODULARS][16];
};

#define AVA2_WRITE_SIZE (sizeof(struct avalon2_pkg))
#define AVA2_READ_SIZE AVA2_WRITE_SIZE

#define AVA2_GETS_OK 0
#define AVA2_GETS_TIMEOUT -1
#define AVA2_GETS_RESTART -2
#define AVA2_GETS_ERROR -3

#define AVA2_SEND_OK 0
#define AVA2_SEND_ERROR -1

#define avalon2_open(devpath, baud, purge)  serial_open(devpath, baud, AVA2_RESET_FAULT_DECISECONDS, purge)
#define avalon2_close(fd) close(fd)

extern char *set_avalon2_fan(char *arg);
extern char *set_avalon2_freq(char *arg);
extern char *set_avalon2_voltage(char *arg);

#endif /* USE_AVALON2 */
#endif	/* _AVALON2_H_ */
