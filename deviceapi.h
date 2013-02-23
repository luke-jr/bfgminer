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

extern void *miner_thread(void *);

#endif
