#ifndef BFG_GPIO_H
#define BFG_GPIO_H

enum bfg_gpio_direction {
	BGD_INPUT  = 0,
	BGD_OUTPUT = 1,
};
#define BGD_ALT(n) (2 + n)

typedef unsigned gpio_bitmask_t;
static const gpio_bitmask_t gpio_bitmask_all = -1;
static const int gpio_highest = ((sizeof(gpio_bitmask_t) * 8) - 1);

#define gpio_bitmask(pin)  (1<<pin)

struct bfg_gpio_controller;

struct bfg_gpio_controller_drv {
	bool (*set_mode)(struct bfg_gpio_controller *, gpio_bitmask_t, int);
	void (*set_values)(struct bfg_gpio_controller *, gpio_bitmask_t, gpio_bitmask_t);
	gpio_bitmask_t (*get_values)(struct bfg_gpio_controller *, gpio_bitmask_t);
};

struct bfg_gpio_controller {
	gpio_bitmask_t all_gpios;
	
	const struct bfg_gpio_controller_drv *drv;
	void *p;
};

static inline
bool gpio_set_mode(struct bfg_gpio_controller * const gc, const gpio_bitmask_t pins, const int mode)
{
	return gc->drv->set_mode(gc, pins, mode);
}

static inline
void gpio_set_values(struct bfg_gpio_controller * const gc, const gpio_bitmask_t pins, const gpio_bitmask_t vals)
{
	gc->drv->set_values(gc, pins, vals);
}

static inline
void gpio_set_value(struct bfg_gpio_controller * const gc, const gpio_bitmask_t pins, const bool val)
{
	gc->drv->set_values(gc, pins, val ? pins : 0);
}

static inline
gpio_bitmask_t gpio_get_values(struct bfg_gpio_controller * const gc, const gpio_bitmask_t pins)
{
	return gc->drv->get_values(gc, pins);
}

extern bool bfg_gpio_init(void);
extern struct bfg_gpio_controller _linux_gpio;
#define linux_gpio (&_linux_gpio)

#endif
