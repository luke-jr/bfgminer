/**
 * libbitfury.h - library for Bitfury chip/board
 *
 * Copyright (c) 2013 bitfury
 * Copyright (c) 2013 legkodymov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
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
