#ifndef BFG_WORK2D_H
#define BFG_WORK2D_H

#include <stdbool.h>
#include <stdint.h>

#define WORK2D_MAX_DIVISIONS  255

extern int work2d_xnonce1sz;
extern int work2d_xnonce2sz;

extern void work2d_init();
extern bool reserve_work2d_(uint32_t *xnonce1_p);
extern void release_work2d_(uint32_t xnonce1);

extern int work2d_pad_xnonce_size(const struct stratum_work *);
extern void *work2d_pad_xnonce(void *buf, const struct stratum_work *, bool hex);
extern void work2d_gen_dummy_work(struct work *, struct stratum_work *, const struct timeval *tvp_prepared, const void *xnonce2, uint32_t xnonce1);
extern void work2d_gen_dummy_work_for_stale_check(struct work *, struct stratum_work *, const struct timeval *tvp_prepared, cglock_t *data_lock_p);
extern bool work2d_submit_nonce(struct thr_info *, struct stratum_work *, const struct timeval *tvp_prepared, const void *xnonce2, uint32_t xnonce1, uint32_t nonce, uint32_t ntime, bool *out_is_stale, float nonce_diff);

#endif
