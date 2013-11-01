#ifndef BFG_DRIVER_BIGPIC_H
#define BFG_DRIVER_BIGPIC_H

#include <stdint.h>

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

#endif
