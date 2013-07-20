/**
 *   libbitfury.h - headers for Bitfury chip/board library
 *
 *   Copyright (c) 2013 bitfury
 *   Copyright (c) 2013 legkodymov
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see http://www.gnu.org/licenses/.
**/
#ifndef __LIBBITFURY_H__
#define __LIBBITFURY_H__

#include "miner.h"

extern int libbitfury_detectChips(void);

struct bitfury_payload {
	unsigned char midstate[32];
	unsigned int junk[8];
	unsigned m7;
	unsigned ntime;
	unsigned nbits;
	unsigned nnonce;
};

struct bitfury_device {
	unsigned char osc[8];
	unsigned newbuf[17];
	unsigned oldbuf[17];
	struct work * work;
	struct work * owork;
	int job_switched;
	struct bitfury_payload payload;
	struct bitfury_payload opayload;
	unsigned int results[16];
	int results_n;
};

int libbitfury_readHashData(unsigned int *res);
int libbitfury_sendHashData(struct bitfury_device *bf, int chip_n);
void work_to_payload(struct bitfury_payload *p, struct work *w);

#endif /* __LIBBITFURY_H__ */
