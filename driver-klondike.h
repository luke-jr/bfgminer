#ifndef BFG_DRIVER_KLONDIKE_H
#define BFG_DRIVER_KLONDIKE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include <pthread.h>

#include "lowlevel.h"
#include "miner.h"

struct klondike_info {
	pthread_rwlock_t stat_lock;
	struct thr_info replies_thr;
	cglock_t klist_lock;
	struct klist *used;
	struct klist *free;
	int kline_count;
	int used_count;
	int block_seq;
	struct klist *status;
	struct device_info *devinfo;
	struct klist *cfg;
	struct jobque *jobque;
	int noncecount;
	uint64_t hashcount;
	uint64_t errorcount;
	uint64_t noisecount;
	int incorrect_slave_sequential;
	int16_t nonce_offset;

	// us Delay from USB reply to being processed
	double delay_count;
	double delay_total;
	double delay_min;
	double delay_max;

	struct timeval tv_last_nonce_received;

	// Time from recieving one nonce to the next
	double nonce_count;
	double nonce_total;
	double nonce_min;
	double nonce_max;

	int wque_size;
	int wque_cleared;

	int clock;
	bool initialised;
	
	struct libusb_device_handle *usbdev_handle;
	
	// TODO:
	bool usbinfo_nodev;
	
	int max_work_count;
	int old_work_ms;
	int reply_wait_time;
};

extern bool klondike_lowl_probe_custom(const struct lowlevel_device_info * const info, struct device_drv * const drv, struct klondike_info * const klninfo);

extern struct device_drv klondike_drv;

#endif
