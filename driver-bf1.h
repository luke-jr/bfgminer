/*
 * Copyright 2013 DI Andreas Auer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 *
 *  Created on: 09.10.2013
 *      Author: DI Andreas Auer
 *        Mail: aauer1@gmail.com
 */

#ifndef DRIVER_BF1_H_
#define DRIVER_BF1_H_

#define BF1_BAUD	115200

struct BF1Identity
{
	uint8_t version;
	char    product[8];
	uint32_t serial;
} __attribute__((packed));

struct BF1State
{
    uint8_t state;
    uint8_t switched;
    uint32_t nonce;
} __attribute__((packed));

struct BF1HashData
{
	uint32_t golden_nonce;
	uint32_t nonce;
};

struct BF1Info
{
	uint32_t baud;

	struct BF1Identity id;
	struct work *work;
	struct work *prev_work[2];

	char rx_buffer[1024];
	uint32_t rx_len;
};

#endif /* DRIVER_S6LX75_H_ */
