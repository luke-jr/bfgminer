#ifndef BFG_DRIVER_TWINFURY_H
#define BFG_DRIVER_TWINFURY_H

#define BPM_BAUD	115200

#define NUM_BITFURY_CHIPS	2

struct twinfury_identity
{
	uint8_t version;
	char    product[16];
	char    serial[23];
} __attribute__((packed));

struct twinfury_state
{
    uint8_t chip;
    uint8_t state;
    uint8_t switched;
    uint32_t nonce;
} __attribute__((packed));

struct twinfury_info
{
	uint32_t baud;

	struct work *prev_work;
	struct work *work;
	bool work_sent;
	struct twinfury_identity id;

	uint8_t tx_buffer[46];
	uint8_t rx_buffer[1024];
	int16_t rx_len;

	uint32_t voltage;
	bool send_voltage;
};

#endif
