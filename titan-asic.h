#ifndef __TITAN_ASIC_H
#define __TITAN_ASIC_H

#define	KNC_TITAN_MAX_ASICS		6
#define	KNC_TITAN_DIES_PER_ASIC		4
#define	KNC_TITAN_CORES_PER_DIE		48
#define	KNC_TITAN_CORES_PER_ASIC	(KNC_TITAN_CORES_PER_DIE * KNC_TITAN_DIES_PER_ASIC)

struct titan_info_response {
	uint64_t pll_state;
	uint16_t cores;
	bool want_work[KNC_TITAN_CORES_PER_DIE];
	bool have_report[KNC_TITAN_CORES_PER_DIE];
};

bool knc_titan_spi_get_info(const char *repr, struct spi_port * const spi, struct titan_info_response *resp, int die, int core_hint);

#endif /* __TITAN_ASIC_H */