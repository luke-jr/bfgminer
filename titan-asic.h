#ifndef __TITAN_ASIC_H
#define __TITAN_ASIC_H

#include "knc-asic.h"
#include "knc-transport.h"

#define	KNC_TITAN_MAX_ASICS		6
#define	KNC_TITAN_DIES_PER_ASIC		4
#define	KNC_TITAN_CORES_PER_DIE		571
#define	KNC_TITAN_CORES_PER_ASIC	(KNC_TITAN_CORES_PER_DIE * KNC_TITAN_DIES_PER_ASIC)
#define	KNC_TITAN_WORKSLOTS_PER_CORE	2
#define	KNC_TITAN_THREADS_PER_CORE	8
#define	KNC_TITAN_NONCES_PER_REPORT	5

struct nonce_report {
	uint32_t nonce;
	uint8_t slot;
};

struct titan_setup_core_params {
	uint16_t bad_address_mask[2];
	uint16_t bad_address_match[2];
	uint8_t difficulty;
	uint8_t thread_enable;
	uint16_t thread_base_address[KNC_TITAN_THREADS_PER_CORE];
	uint16_t lookup_gap_mask[KNC_TITAN_THREADS_PER_CORE];
	uint16_t N_mask[KNC_TITAN_THREADS_PER_CORE];
	uint8_t N_shift[KNC_TITAN_THREADS_PER_CORE];
	uint32_t nonce_top;
	uint32_t nonce_bottom;
};

bool knc_titan_get_info(const char *repr, void * const ctx, int channel, int die, struct knc_die_info *die_info);
bool knc_titan_set_work(const char *repr, void * const ctx, int channel, int die, int core, int slot, struct work *work, bool urgent, bool *work_accepted, struct knc_report *report);
bool knc_titan_get_report(const char *repr, void * const ctx, int channel, int die, int core, struct knc_report *report);
bool knc_titan_setup_core(const char *repr, void * const ctx, int channel, int die, int core, struct titan_setup_core_params *params);

#endif /* __TITAN_ASIC_H */
