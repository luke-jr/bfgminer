/*
 * Copyright 2013 bitfury
 * Copyright 2013-2014 Luke Dashjr
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "config.h"

#ifdef HAVE_LINUX_SPI_SPIDEV_H
#define HAVE_LINUX_SPI
#endif

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#ifdef HAVE_LINUX_SPI
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <signal.h>
#include <sys/types.h>
#include <linux/spi/spidev.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#include "logging.h"
#include "lowl-spi.h"
#include "miner.h"
#include "util.h"

#ifdef HAVE_LINUX_SPI
bool sys_spi_txrx(struct spi_port *port);
static volatile unsigned *gpio;
#endif

struct spi_port *sys_spi;

void spi_init(void)
{
#ifdef HAVE_LINUX_SPI
	int fd;
	fd = open("/dev/mem",O_RDWR|O_SYNC);
	if (fd < 0)
	{
		perror("/dev/mem trouble");
		return;
	}
	gpio = mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x20200000);
	if (gpio == MAP_FAILED)
	{
		perror("gpio mmap trouble");
		return;
	}
	close(fd);
	
	sys_spi = malloc(sizeof(*sys_spi));
	*sys_spi = (struct spi_port){
		.txrx = sys_spi_txrx,
	};
#endif
}

#ifdef HAVE_LINUX_SPI

int spi_open(struct spi_port * const spi, const char * const devpath)
{
	const int fd = open(devpath, O_RDWR);
	if (fd < 0)
		return fd;
	
	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi->speed) < 0
	 || ioctl(fd, SPI_IOC_WR_MODE, &spi->mode) < 0
	 || ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &spi->bits) < 0)
	{
		close(fd);
		return -1;
	}
	
	spi->fd = fd;
	return fd;
}

#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0
#define GPIO_LEV *(gpio+13)

void bfg_gpio_setpin_output(const unsigned pin)
{
	INP_GPIO(pin);
	OUT_GPIO(pin);
}

void bfg_gpio_set_high(const unsigned mask)
{
	GPIO_SET = mask;
}

void bfg_gpio_set_low(const unsigned mask)
{
	GPIO_CLR = mask;
}

unsigned bfg_gpio_get()
{
	return GPIO_LEV;
}

// Bit-banging reset, to reset more chips in chain - toggle for longer period... Each 3 reset cycles reset first chip in chain
static
int spi_reset(int a)
{
	int i,j;
	int len = 8;
	INP_GPIO(10); OUT_GPIO(10);
	INP_GPIO(11); OUT_GPIO(11);
	GPIO_SET = 1 << 11; // Set SCK
	for (i = 0; i < 32; i++) { // On standard settings this unoptimized code produces 1 Mhz freq.
		GPIO_SET = 1 << 10;
		for (j = 0; j < len; j++) {
			a *= a;
		}
		GPIO_CLR = 1 << 10;
		for (j = 0; j < len; j++) {
			a *= a;
		}
	}
	GPIO_CLR = 1 << 10;
	GPIO_CLR = 1 << 11;
	INP_GPIO(10);
	SET_GPIO_ALT(10,0);
	INP_GPIO(11);
	SET_GPIO_ALT(11,0);
	INP_GPIO(9);
	SET_GPIO_ALT(9,0);

	return a;
}

#define BAILOUT(s)  do{  \
	perror(s);  \
	close(fd);  \
	return false;  \
}while(0)

bool sys_spi_txrx(struct spi_port *port)
{
	const void *wrbuf = spi_gettxbuf(port);
	void *rdbuf = spi_getrxbuf(port);
	size_t bufsz = spi_getbufsz(port);
	int fd;
	int mode, bits, speed, rv, i, j;
	struct spi_ioc_transfer tr[16];

	memset(&tr,0,sizeof(tr));
	mode = 0; bits = 8; speed = 4000000;
	if (port->speed)
		speed = port->speed;

	spi_reset(1234);
	fd = open("/dev/spidev0.0", O_RDWR);
	if (fd < 0) {
		perror("Unable to open SPI device");
		return false;
	}
	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0)
		BAILOUT("Unable to set WR MODE");
	if (ioctl(fd, SPI_IOC_RD_MODE, &mode) < 0)
		BAILOUT("Unable to set RD MODE");
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
		BAILOUT("Unable to set WR_BITS_PER_WORD");
	if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0)
		BAILOUT("Unable to set RD_BITS_PER_WORD");
	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
		BAILOUT("Unable to set WR_MAX_SPEED_HZ");
	if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0)
		BAILOUT("Unable to set RD_MAX_SPEED_HZ");

	rv = 0;
	while (bufsz >= 4096) {
                tr[rv].tx_buf = (uintptr_t) wrbuf;
                tr[rv].rx_buf = (uintptr_t) rdbuf;
                tr[rv].len = 4096;
                tr[rv].delay_usecs = 1;
                tr[rv].speed_hz = speed;
                tr[rv].bits_per_word = bits;
                bufsz -= 4096;
                wrbuf += 4096; rdbuf += 4096; rv ++;
        }
        if (bufsz > 0) {
                tr[rv].tx_buf = (uintptr_t) wrbuf;
                tr[rv].rx_buf = (uintptr_t) rdbuf;
                tr[rv].len = (unsigned)bufsz;
                tr[rv].delay_usecs = 1;
                tr[rv].speed_hz = speed;
                tr[rv].bits_per_word = bits;
                rv ++;
        }

        i = rv;
        for (j = 0; j < i; j++) {
                rv = (int)ioctl(fd, SPI_IOC_MESSAGE(1), (intptr_t)&tr[j]);
		if (rv < 0)
			BAILOUT("WTF!");
        }

	close(fd);
	spi_reset(4321);

	return true;
}

bool linux_spi_txrx(struct spi_port * const spi)
{
	const void * const wrbuf = spi_gettxbuf(spi);
	void * const rdbuf = spi_getrxbuf(spi);
	const size_t bufsz = spi_getbufsz(spi);
	const int fd = spi->fd;
	struct spi_ioc_transfer xf = {
		.tx_buf = (uintptr_t) wrbuf,
		.rx_buf = (uintptr_t) rdbuf,
		.len = bufsz,
		.delay_usecs = spi->delay,
		.speed_hz = spi->speed,
		.bits_per_word = spi->bits,
	};
	return (ioctl(fd, SPI_IOC_MESSAGE(1), &xf) > 0);
}

bool linux_spi_txrx2(struct spi_port * const spi)
{
	const size_t bufsz = spi_getbufsz(spi);
	
	if (opt_dev_protocol)
	{
		const void * const txbuf = spi_gettxbuf(spi);
		char hex[(bufsz * 2) + 1];
		bin2hex(hex, txbuf, bufsz);
		applog(LOG_DEBUG, "%s: %cX %s", spi->repr, 'T', hex);
	}
	bool rv = linux_spi_txrx(spi);
	if (opt_dev_protocol)
	{
		if (likely(rv))
		{
			void * const rxbuf = spi_getrxbuf(spi);
			char hex[(bufsz * 2) + 1];
			bin2hex(hex, rxbuf, bufsz);
			applog(LOG_DEBUG, "%s: %cX %s", spi->repr, 'R', hex);
		}
		else
			applog(LOG_DEBUG, "%s: SPI ERROR", spi->repr);
	}
	return rv;
}

#endif

static
void *spi_emit_buf_reverse(struct spi_port *port, const void *p, size_t sz)
{
	const unsigned char *str = p;
	void * const rv = &port->spibuf_rx[port->spibufsz];
	if (port->spibufsz + sz >= SPIMAXSZ)
		return NULL;
	for (size_t i = 0; i < sz; ++i)
	{
		// Reverse bit order in each byte!
		port->spibuf[port->spibufsz++] = bitflip8(str[i]);
	}
	return rv;
}

void spi_emit_buf(struct spi_port * const port, const void * const str, const size_t sz)
{
	if (port->spibufsz + sz >= SPIMAXSZ)
		return;
	memcpy(&port->spibuf[port->spibufsz], str, sz);
	port->spibufsz += sz;
}

/* TODO: in production, emit just bit-sequences! Eliminate padding to byte! */
void spi_emit_break(struct spi_port *port)
{
	spi_emit_buf(port, "\x4", 1);
}

void spi_emit_fsync(struct spi_port *port)
{
	spi_emit_buf(port, "\x6", 1);
}

void spi_emit_fasync(struct spi_port *port, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		spi_emit_buf(port, "\x5", 1);
	}
}

void spi_emit_nop(struct spi_port *port, int n) {
	int i;
	for (i = 0; i < n; ++i) {
		spi_emit_buf(port, "\x0", 1);
	}
}

void *spi_emit_data(struct spi_port *port, uint16_t addr, const void *buf, size_t len)
{
	unsigned char otmp[3];
	if (len < 4 || len > 128)
		return NULL;  /* This cannot be programmed in single frame! */
	len /= 4; /* Strip */
	otmp[0] = (len - 1) | 0xE0;
	otmp[1] = (addr >> 8)&0xFF; otmp[2] = addr & 0xFF;
	spi_emit_buf(port, otmp, 3);
	return spi_emit_buf_reverse(port, buf, len*4);
}

#ifdef USE_BFSB
void spi_bfsb_select_bank(int bank)
{
	static int last_bank = -2;
	if (bank == last_bank)
		return;
	const int banks[4]={18,23,24,25}; // GPIO connected to OE of level shifters
	int i;
	for(i=0;i<4;i++)
	{
		if (i == bank)
			continue;
		
		INP_GPIO(banks[i]);
		OUT_GPIO(banks[i]);
		GPIO_CLR = 1 << banks[i];
	}
	
	if (bank != -1)
	{
		OUT_GPIO(banks[bank]);
		GPIO_SET = 1 << banks[bank];
	}
	
	last_bank = bank;
}
#endif
