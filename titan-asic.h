#ifndef __TITAN_ASIC_H
#define __TITAN_ASIC_H

#define	BLOCK_HEADER_BYTES			80
#define	BLOCK_HEADER_BYTES_WITHOUT_NONCE	(BLOCK_HEADER_BYTES - 4)

#define	KNC_TITAN_MAX_ASICS		6
#define	KNC_TITAN_DIES_PER_ASIC		4
#define	KNC_TITAN_CORES_PER_DIE		48
#define	KNC_TITAN_CORES_PER_ASIC	(KNC_TITAN_CORES_PER_DIE * KNC_TITAN_DIES_PER_ASIC)
#define	KNC_TITAN_NONCES_PER_REPORT	5

#define	KNC_TITAN_ASIC_REVISION		0xA102

struct titan_info_response {
	uint64_t pll_state;
	uint16_t cores;
	bool want_work[KNC_TITAN_CORES_PER_DIE];
	bool have_report[KNC_TITAN_CORES_PER_DIE];
};

struct titan_report {
	uint8_t flags;
	uint8_t core_counter;
	uint8_t slot_core;
	struct nonce_report {
		uint32_t nonce;
		uint8_t slot;
	} nonces[KNC_TITAN_NONCES_PER_REPORT];
};

bool knc_titan_spi_get_info(const char *repr, struct spi_port * const spi, struct titan_info_response *resp, int die, int core_hint);
bool knc_titan_get_report(const char *repr, struct spi_port * const spi, struct titan_report *report, int die, int core);
bool knc_titan_set_work(const char *repr, struct spi_port * const spi, struct titan_report *report, int die, int core, int slot, struct work *work, bool urgent);

#endif /* __TITAN_ASIC_H */