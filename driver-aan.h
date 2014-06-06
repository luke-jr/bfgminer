#ifndef BFG_DRIVER_AAN
#define BFG_DRIVER_AAN

#include "lowl-spi.h"

#define AAN_ALL_CHIPS  0

struct aan_hooks {
	void (*precmd)(struct spi_port *);
};

extern int aan_detect_spi(struct spi_port *);

#endif
