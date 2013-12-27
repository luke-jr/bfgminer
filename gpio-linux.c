#include <stdbool.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "logging.h"

#include "gpio.h"

extern struct bfg_gpio_controller _linux_gpio;

bool bfg_gpio_init(void)
{
	const int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0)
		applogr(false, LOG_ERR, "Failed to open /dev/mem");
	volatile unsigned *gpio;
	gpio = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x20200000);
	if (gpio == MAP_FAILED)
		applogr(false, LOG_ERR, "Failed to mmap GPIO from /dev/mem");
	close(fd);
	
	linux_gpio->all_gpios = gpio_bitmask_all;
	linux_gpio->p = (void*)gpio;
	
	return true;
}

static
bool linux_gpio_set_mode(struct bfg_gpio_controller * const gc, const gpio_bitmask_t pins, int mode)
{
	volatile unsigned * const gpio = gc->p;
	if (mode > 1)
	{
		if (mode > 3)
		{
			if (mode > 5)
				return false;
			mode = 7 - mode;
		}
		else
			mode |= 4;
	}
	for (int i = gpio_highest; i >= 0; --i)
	{
		volatile unsigned * const g = &gpio[i / 10];
		const int sh = (i % 10) * 3;
		*g = (*g & ~(7 << sh)) | (mode << sh);
	}
	return true;
}

static
void linux_gpio_set_values(struct bfg_gpio_controller * const gc, const gpio_bitmask_t pins, const gpio_bitmask_t vals)
{
	volatile unsigned * const gpio = gc->p;
	gpio[7] = pins & vals;
	gpio[10] = pins & ~vals;
}

static
gpio_bitmask_t linux_gpio_get_values(struct bfg_gpio_controller * const gc, const gpio_bitmask_t pins)
{
	volatile unsigned * const gpio = gc->p;
	return gpio[13] & pins;
}

static
const struct bfg_gpio_controller_drv linux_gpio_drv = {
	.set_mode = linux_gpio_set_mode,
	.set_values = linux_gpio_set_values,
	.get_values = linux_gpio_get_values,
};

struct bfg_gpio_controller _linux_gpio = {
	.drv = &linux_gpio_drv,
};
