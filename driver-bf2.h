#ifndef BFG_DRIVER_BF2_H
#define BFG_DRIVER_BF2_H

#define BPM_BAUD	115200

#define NUM_BITFURY_CHIPS	2

struct bf2_identity
{
	uint8_t version;
	char    product[8];
	uint8_t serial[11];
} __attribute__((packed));

struct bf2_state
{
    uint8_t chip;
    uint8_t state;
    uint8_t switched;
    uint32_t nonce;
} __attribute__((packed));

struct bf2_info
{
	uint32_t baud;

	struct work *prev_work;
	struct work *work;
	bool work_sent;
	struct bf2_identity id;

	char tx_buffer[46];
	char rx_buffer[1024];
	uint32_t rx_len;
};

#endif
