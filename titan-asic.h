#ifndef __TITAN_ASIC_H
#define __TITAN_ASIC_H

#include "knc-asic/knc-asic.h"
#include "knc-asic/knc-transport.h"

#define	KNC_TITAN_MAX_ASICS		6
#define	KNC_TITAN_DIES_PER_ASIC		4
#define	KNC_TITAN_CORES_PER_DIE		571
#define	KNC_TITAN_CORES_PER_ASIC	(KNC_TITAN_CORES_PER_DIE * KNC_TITAN_DIES_PER_ASIC)
#define	KNC_TITAN_WORKSLOTS_PER_CORE	2
#define	KNC_TITAN_THREADS_PER_CORE	8
#define	KNC_TITAN_NONCES_PER_REPORT	5

/* Valid slot numbers: 1..15 */
#define	KNC_TITAN_MIN_WORK_SLOT_NUM	1
#define	KNC_TITAN_MAX_WORK_SLOT_NUM	15

#define KNC_TITAN_FPGA_SYSCLK_FREQ      24576000
#define KNC_TITAN_FPGA_SPI_FREQ         6144000
#define KNC_TITAN_FPGA_SPI_DIVIDER      (KNC_TITAN_FPGA_SYSCLK_FREQ / (2*KNC_TITAN_FPGA_SPI_FREQ) - 1)
#if KNC_TITAN_FPGA_SYSCLK_FREQ % (2*KNC_TITAN_FPGA_SPI_FREQ) != 0
#warning Requested SPI frequency could not be accomplished exactly, adjusting as needed
#endif
#define KNC_TITAN_FPGA_SPI_PRECLK       7
#define KNC_TITAN_FPGA_SPI_DECLK        7
#define KNC_TITAN_FPGA_SPI_SSLOWMIN     15
#define KNC_TITAN_FPGA_RETRIES          1

struct nonce_report {
	uint32_t nonce;
	uint8_t slot;
};

bool knc_titan_get_info(int log_level, void * const ctx, int channel, int die, struct knc_die_info *die_info);
bool knc_titan_set_work(const char *repr, void * const ctx, int channel, int die, int core, int slot, struct work *work, bool urgent, bool *work_accepted, struct knc_report *report);
bool knc_titan_set_work_multi(const char *repr, void * const ctx, int channel, int die, int core_start, int slot, struct work *work, bool urgent, bool *work_accepted, struct knc_report *reports, int num);
bool knc_titan_get_report(const char *repr, void * const ctx, int channel, int die, int core, struct knc_report *report);
bool knc_titan_setup_core_local(const char *repr, void * const ctx, int channel, int die, int core, struct titan_setup_core_params *params);
bool knc_titan_setup_spi(const char *repr, void * const ctx, int asic, int divider, int preclk, int declk, int sslowmin);
bool knc_titan_set_work_parallel(const char *repr, void * const ctx, int asic, int die, int core_start, int slot, struct work *work, bool urgent, int num, int resend);
bool knc_titan_get_work_status(const char *repr, void * const ctx, int asic, int *num_request_busy, int *num_status_byte_error);

#endif /* __TITAN_ASIC_H */
