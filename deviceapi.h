#ifndef __DEVICEAPI_H__
#define __DEVICEAPI_H__

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include "miner.h"

extern void request_work(struct thr_info *);
extern struct work *get_work(struct thr_info *);
extern bool hashes_done(struct thr_info *, int64_t hashes, struct timeval *tvp_hashes, uint32_t *max_nonce);
extern void mt_disable_start(struct thr_info *);
extern void mt_disable_finish(struct thr_info *);
extern void mt_disable(struct thr_info *);  // blocks until reenabled

extern int restart_wait(struct thr_info *, unsigned int ms);
extern void minerloop_scanhash(struct thr_info *);

extern bool do_job_prepare(struct thr_info *, struct timeval *tvp_now);
extern void job_prepare_complete(struct thr_info *);
extern void do_get_results(struct thr_info *, bool proceed_with_new_job);
extern void job_results_fetched(struct thr_info *);
extern void do_job_start(struct thr_info *);
extern void mt_job_transition(struct thr_info *);
extern void job_start_complete(struct thr_info *);
extern void job_start_abort(struct thr_info *, bool failure);
extern bool do_process_results(struct thr_info *, struct timeval *tvp_now, struct work *, bool stopping);
extern void minerloop_async(struct thr_info *);

extern void minerloop_queue(struct thr_info *);

extern void *miner_thread(void *);

extern void add_cgpu_live(void*);

typedef bool(*detectone_func_t)(const char*);
typedef int(*autoscan_func_t)();

extern int _serial_detect(struct device_drv *api, detectone_func_t, autoscan_func_t, int flags);
#define serial_detect_fauto(api, detectone, autoscan)  \
	_serial_detect(api, detectone, autoscan, 1)
#define serial_detect_auto(api, detectone, autoscan)  \
	_serial_detect(api, detectone, autoscan, 0)
#define serial_detect_auto_byname(api, detectone, autoscan)  \
	_serial_detect(api, detectone, autoscan, 2)
#define serial_detect(api, detectone)  \
	_serial_detect(api, detectone,     NULL, 0)
#define serial_detect_byname(api, detectone)  \
	_serial_detect(api, detectone,     NULL, 2)
#define noserial_detect(api, autoscan)  \
	_serial_detect(api, NULL     , autoscan, 0)
#define noserial_detect_manual(api, autoscan)  \
	_serial_detect(api, NULL     , autoscan, 4)

extern FILE *open_bitstream(const char *dname, const char *filename);

#endif
