#ifndef BFG_DRIVER_BITFURY_H
#define BFG_DRIVER_BITFURY_H

#include <stdbool.h>
#include <stdint.h>

#include "miner.h"

#define BITFURY_MAX_OSC6_BITS  60
#define BITFURY_MAX_OSC6_BITS_S  "60"

extern const struct bfg_set_device_definition bitfury_set_device_funcs[];
extern const struct bfg_set_device_definition *bitfury_set_device_funcs_probe;
extern const char *bitfury_set_baud(struct cgpu_info *, const char *, const char *, char *, enum bfg_set_device_replytype *);
extern const char *bitfury_set_osc6_bits(struct cgpu_info *, const char *, const char *, char *, enum bfg_set_device_replytype *);

extern bool bitfury_prepare(struct thr_info *);
extern bool bitfury_init_chip(struct cgpu_info *);
extern void bitfury_init_freq_stat(struct freq_stat *, int osc6_min, int osc6_max);
extern void bitfury_clean_freq_stat(struct freq_stat *);

extern bool bitfury_job_prepare(struct thr_info *, struct work *, uint64_t max_nonce);
extern void bitfury_noop_job_start(struct thr_info *);
extern void bitfury_do_io(struct thr_info *);
extern int64_t bitfury_job_process_results(struct thr_info *, struct work *, bool stopping);
extern struct api_data *bitfury_api_device_detail(struct cgpu_info *);
extern struct api_data *bitfury_api_device_status(struct cgpu_info *);
extern void bitfury_tui_wlogprint_choices(struct cgpu_info *);
extern const char *bitfury_tui_handle_choice(struct cgpu_info *, int input);
extern void bitfury_wlogprint_status(struct cgpu_info *);

extern void bitfury_disable(struct thr_info *);
extern void bitfury_enable(struct thr_info *);
extern void bitfury_shutdown(struct thr_info *);

#endif
