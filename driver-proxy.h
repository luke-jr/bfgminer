#ifndef BFG_DRIVER_PROXY_H
#define BFG_DRIVER_PROXY_H

#include <uthash.h>

#include "miner.h"

extern struct device_drv proxy_drv;

struct proxy_client {
	char *username;
	struct cgpu_info *cgpu;
	struct work *work;
	struct timeval tv_hashes_done;
	float desired_share_pdiff;
	
	UT_hash_handle hh;
};

extern struct proxy_client *proxy_find_or_create_client(const char *user);

#endif
