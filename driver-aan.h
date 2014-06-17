#ifndef BFG_DRIVER_AAN
#define BFG_DRIVER_AAN

#include "lowl-spi.h"

#define AAN_ALL_CHIPS  0

struct aan_hooks {
	void (*precmd)(struct spi_port *);
};

extern int aan_detect_spi(int *out_chipcount, struct spi_port * const *spi_a, int spi_n);

#endif
