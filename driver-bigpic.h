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

#ifndef BFG_DRIVER_BIGPIC_H
#define BFG_DRIVER_BIGPIC_H

#define BPM_BAUD	115200

struct bigpic_identity
{
	uint8_t version;
	char    product[8];
	uint32_t serial;
} __attribute__((packed));

struct bigpic_state
{
    uint8_t state;
    uint8_t switched;
    uint32_t nonce;
} __attribute__((packed));

struct bigpic_info
{
	uint32_t baud;

	struct bigpic_identity id;

	char tx_buffer[45];
	char rx_buffer[1024];
	uint32_t rx_len;
};

#endif /* DRIVER_S6LX75_H_ */
