#ifndef BFG_LOWL_SPI_H
#define BFG_LOWL_SPI_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#define SPIMAXSZ (256*1024)

/* Initialize SPI using this function */
void spi_init(void);

#ifdef HAVE_LINUX_SPI_SPIDEV_H
extern void bfg_gpio_setpin_output(unsigned pin);
extern void bfg_gpio_set_high(unsigned mask);
extern void bfg_gpio_set_low(unsigned mask);
extern unsigned bfg_gpio_get();
#endif

/* Do not allocate spi_port on the stack! OS X, at least, has a 512 KB default stack size for secondary threads
   This includes struct assignments which get allocated on the stack before being assigned to */
struct spi_port {
	/* TX-RX single frame */
	bool (*txrx)(struct spi_port *port);
	
	char spibuf[SPIMAXSZ], spibuf_rx[SPIMAXSZ];
	size_t spibufsz;
	
	void *userp;
	struct cgpu_info *cgpu;
	const char *repr;
	int logprio;
	
	int fd;
	uint32_t speed;
	uint16_t delay;
	uint8_t mode;
	uint8_t bits;
	int chipselect;
	int *chipselect_current;
};

extern struct spi_port *sys_spi;


/* SPI BUFFER OPS */
static inline
void spi_clear_buf(struct spi_port *port)
{
	port->spibufsz = 0;
}

static inline
void *spi_getrxbuf(struct spi_port *port)
{
	return port->spibuf_rx;
}

static inline
void *spi_gettxbuf(struct spi_port *port)
{
	return port->spibuf;
}

static inline
size_t spi_getbufsz(struct spi_port *port)
{
	return port->spibufsz;
}

extern void spi_emit_buf(struct spi_port *, const void *, size_t);

extern void spi_emit_break(struct spi_port *port); /* BREAK CONNECTIONS AFTER RESET */
extern void spi_emit_fsync(struct spi_port *port); /* FEED-THROUGH TO NEXT CHIP SYNCHRONOUSLY (WITH FLIP-FLOP) */
extern void spi_emit_fasync(struct spi_port *port, int n); /* FEED-THROUGH TO NEXT CHIP ASYNCHRONOUSLY (WITHOUT FLIP-FLOP INTERMEDIATE) */
extern void spi_emit_nop(struct spi_port *port, int n);

/* TRANSMIT PROGRAMMING SEQUENCE (AND ALSO READ-BACK) */
/* addr is the destination address in bits (16-bit - 0 to 0xFFFF valid ones)
   buf is buffer to be transmitted, it will go at position spi_getbufsz()+3
   len is length in _bytes_, should be 4 to 128 and be multiple of 4, as smallest
   transmission quantum is 32 bits */
extern void *spi_emit_data(struct spi_port *port, uint16_t addr, const void *buf, size_t len);

static inline
bool spi_txrx(struct spi_port *port)
{
	return port->txrx(port);
}

extern int spi_open(struct spi_port *, const char *);
extern bool sys_spi_txrx(struct spi_port *);
extern bool linux_spi_txrx(struct spi_port *);
extern bool linux_spi_txrx2(struct spi_port *);

void spi_bfsb_select_bank(int bank);

#endif
