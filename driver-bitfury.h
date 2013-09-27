/*
 * Copyright 2013 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BITFURY_H
#define BITFURY_H

#include "miner.h"
#include "usbutils.h"

#define BF1ARRAY_SIZE 2

struct bitfury_info {
	struct cgpu_info *base_cgpu;
	uint8_t version;
	char product[8];
	uint32_t serial;
	struct work *prevwork[BF1ARRAY_SIZE + 1];
	char buf[512];
	int tot;
	int nonces;
	struct timeval tv_start;
};

#endif /* BITFURY_H */
