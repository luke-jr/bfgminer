#include "config.h"
#include "compat.h"
#include "miner.h"

static void bab_detect(__maybe_unused bool hotplug)
{
}

struct device_drv bab_drv = {
	.drv_id = DRIVER_bab,
	.dname = "BlackArrowBitFuryGPIO",
	.name = "BaB",
	.drv_detect = bab_detect
};
