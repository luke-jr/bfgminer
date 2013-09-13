#ifndef SPIDEVC_H
#define SPIDEVC_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#define SPIMAXSZ (256*1024)

/* Initialize SPI using this function */
void spi_init(void);

struct spi_port {
	/* TX-RX single frame */
	bool (*txrx)(struct spi_port *port);
	
	char spibuf[SPIMAXSZ], spibuf_rx[SPIMAXSZ];
	size_t spibufsz;
	
	struct cgpu_info *cgpu;
	const char *repr;
	int logprio;
	int speed;
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


extern void spi_emit_break(struct spi_port *port); /* BREAK CONNECTIONS AFTER RESET */
extern void spi_emit_fsync(struct spi_port *port); /* FEED-THROUGH TO NEXT CHIP SYNCHRONOUSLY (WITH FLIP-FLOP) */
extern void spi_emit_fasync(struct spi_port *port, int n); /* FEED-THROUGH TO NEXT CHIP ASYNCHRONOUSLY (WITHOUT FLIP-FLOP INTERMEDIATE) */
extern void spi_emit_nop(struct spi_port *port, int n);

/* TRANSMIT PROGRAMMING SEQUENCE (AND ALSO READ-BACK) */
/* addr is the destination address in bits (16-bit - 0 to 0xFFFF valid ones)
   buf is buffer to be transmitted, it will go at position spi_getbufsz()+3
   len is length in _bytes_, should be 4 to 128 and be multiple of 4, as smallest
   transmission quantum is 32 bits */
extern void spi_emit_data(struct spi_port *port, uint16_t addr, const void *buf, size_t len);

static inline
bool spi_txrx(struct spi_port *port)
{
	return port->txrx(port);
}

extern bool sys_spi_txrx(struct spi_port *);

void spi_bfsb_select_bank(int bank);

#endif
