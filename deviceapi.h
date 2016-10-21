#ifndef BFG_DEVICEAPI_H
#define BFG_DEVICEAPI_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include <utlist.h>

#include "miner.h"

struct driver_registration;
struct driver_registration {
	const struct device_drv *drv;
	
	struct driver_registration *next_dname;
	struct driver_registration *next_prio;
	struct driver_registration *next;  // DO NOT USE
};

extern struct driver_registration *_bfg_drvreg1;
extern struct driver_registration *_bfg_drvreg2;
extern void bfg_devapi_init();

#define BFG_FOREACH_DRIVER_BY_DNAME(reg)  \
	LL_FOREACH2(_bfg_drvreg1, reg, next_dname)
#define BFG_FOREACH_DRIVER_BY_PRIORITY(reg)  \
	LL_FOREACH2(_bfg_drvreg2, reg, next_prio)

extern void _bfg_register_driver(const struct device_drv *);
#define BFG_REGISTER_DRIVER(drv)                \
	struct device_drv drv;                      \
	__attribute__((constructor))                \
	static void __bfg_register_drv_ ## drv() {  \
		_bfg_register_driver(&drv);             \
	}                                           \
// END BFG_REGISTER_DRIVER

extern bool bfg_need_detect_rescan;

extern float common_sha256d_and_scrypt_min_nonce_diff(struct cgpu_info *, const struct mining_algorithm *);
extern float common_scrypt_min_nonce_diff(struct cgpu_info *, const struct mining_algorithm *);

extern void request_work(struct thr_info *);
extern struct work *get_work(struct thr_info *);
extern bool hashes_done(struct thr_info *, int64_t hashes, struct timeval *tvp_hashes, uint32_t *max_nonce);
extern bool hashes_done2(struct thr_info *, int64_t hashes, uint32_t *max_nonce);
extern void mt_disable_start(struct thr_info *);
extern void mt_disable_finish(struct thr_info *);
extern void mt_disable(struct thr_info *);  // blocks until reenabled

extern int restart_wait(struct thr_info *, unsigned int ms);
extern void minerloop_scanhash(struct thr_info *);

extern void mt_disable_start__async(struct thr_info *);
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

// Establishes a simple way for external threads to directly communicate with device
extern void cgpu_setup_control_requests(struct cgpu_info *);
extern void cgpu_request_control(struct cgpu_info *);
extern void cgpu_release_control(struct cgpu_info *);

extern void *miner_thread(void *);

extern void add_cgpu_live(void*);
extern bool add_cgpu_slave(struct cgpu_info *, struct cgpu_info *master);

enum bfg_set_device_replytype {
	SDR_AUTO,
	SDR_OK,
	SDR_ERR,
	SDR_HELP,
	SDR_UNKNOWN,
	SDR_NOSUPP,
};
typedef const char *(*bfg_set_device_func_t)(struct cgpu_info *proc, const char *optname, const char *newvalue, char *replybuf, enum bfg_set_device_replytype *out_success);
struct bfg_set_device_definition {
	const char *optname;
	bfg_set_device_func_t func;
	const char *description;
};
extern const char *proc_set_device(struct cgpu_info *proc, char *optname, char *newvalue, char *replybuf, enum bfg_set_device_replytype *out_success);
#ifdef HAVE_CURSES
extern const char *proc_set_device_tui_wrapper(struct cgpu_info *proc, char *optname, bfg_set_device_func_t, const char *prompt, const char *success_msg);
#endif

typedef bool(*detectone_func_t)(const char*);
typedef int(*autoscan_func_t)();

enum generic_detect_flags {
	GDF_REQUIRE_DNAME = 2,
	GDF_DEFAULT_NOAUTO = 4,
};

extern int _serial_detect(struct device_drv *api, detectone_func_t, autoscan_func_t, int flags);
#define noserial_detect_manual(api, autoscan)  \
	_serial_detect(api, NULL     , autoscan, 4)
#define generic_detect(drv, detectone, autoscan, flags)  _serial_detect(drv, detectone, autoscan, flags)

extern FILE *open_bitstream(const char *dname, const char *filename);

extern void close_device_fd(struct thr_info *);

#define for_each_managed_proc(procvar, dev)  \
	for (struct cgpu_info *procvar = dev; procvar; procvar = procvar->next_proc)
#define for_each_logical_proc(procvar, dev)  \
	for (struct cgpu_info *procvar = dev; procvar && procvar->device == (dev); procvar = procvar->next_proc)
extern struct cgpu_info *device_proc_by_id(const struct cgpu_info *dev, int procid);

#endif
