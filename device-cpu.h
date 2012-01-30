#ifndef __DEVICE_CPU_H__
#define __DEVICE_CPU_H__

#include "miner.h"

#ifndef OPT_SHOW_LEN
#define OPT_SHOW_LEN 80
#endif

extern const char *algo_names[];
extern bool opt_usecpu;
extern struct device_api cpu_api;

extern char *set_algo(const char *arg, enum sha256_algos *algo);
extern void show_algo(char buf[OPT_SHOW_LEN], const enum sha256_algos *algo);
extern char *force_nthreads_int(const char *arg, int *i);
extern void init_max_name_len();
extern double bench_algo_stage3(enum sha256_algos algo);

#endif /* __DEVICE_CPU_H__ */
