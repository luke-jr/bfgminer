/*
 * driver-s6lx75.h
 *
 *  Created on: 09.06.2013
 *      Author: andreas
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
    uint8_t nonce_valid;
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
};

#endif /* DRIVER_S6LX75_H_ */
