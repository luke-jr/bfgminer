#ifndef BFG_WORK2D_H
#define BFG_WORK2D_H

#include <stdbool.h>
#include <stdint.h>

extern int work2d_xnonce1sz;
extern int work2d_xnonce2sz;

extern void work2d_init();
extern bool reserve_work2d_(uint32_t *xnonce1_p);
extern void release_work2d_(uint32_t xnonce1);

#endif
