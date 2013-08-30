/*
 *   spidevc.c - SPI library for raspberry pi/bitfury chip/board
 *
 *   Copyright (c) 2013 bitfury
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see http://www.gnu.org/licenses/.
*/

#include "spidevc.h"
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <signal.h>
#include <sys/types.h>
#include <linux/spi/spidev.h>
#include <time.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/stat.h>

static volatile unsigned *gpio;

void spi_init(void)
{
	int fd;
	fd = open("/dev/mem",O_RDWR|O_SYNC);
	if (fd < 0) { perror("/dev/mem trouble"); exit(1); }
	gpio = mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x20200000);
	if (gpio == MAP_FAILED) { perror("gpio mmap trouble"); exit(1); }
	close(fd);
}

#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0

// Bit-banging reset, to reset more chips in chain - toggle for longer period... Each 3 reset cycles reset first chip in chain
void spi_reset(void)
{
	int i,j;
	int a = 1234, len = 2;
	INP_GPIO(10); OUT_GPIO(10);
	INP_GPIO(11); OUT_GPIO(11);
	GPIO_SET = 1 << 11; // Set SCK
	for (i = 0; i < 16; i++) { // On standard settings this unoptimized code produces 1 Mhz freq.
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
}

int spi_txrx(const char *wrbuf, char *rdbuf, int bufsz)
{
	int fd;
	int mode, bits, speed, rv, i, j;
	struct timespec tv;
	struct spi_ioc_transfer tr[16];

	memset(&tr,0,sizeof(tr));
	mode = 0; bits = 8; speed = 2000000;

	spi_reset();
	fd = open("/dev/spidev0.0", O_RDWR);
	if (fd < 0) { perror("Unable to open SPI device"); exit(1); }
        if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) { perror("Unable to set WR MODE"); close(fd); return -1; }
        if (ioctl(fd, SPI_IOC_RD_MODE, &mode) < 0) { perror("Unable to set RD MODE"); close(fd); return -1; }
        if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) { perror("Unable to set WR_BITS_PER_WORD"); close(fd); return -1; }
        if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0) { perror("Unable to set RD_BITS_PER_WORD"); close(fd); return -1; }
        if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) { perror("Unable to set WR_MAX_SPEED_HZ"); close(fd); return -1; }
        if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0) { perror("Unable to set RD_MAX_SPEED_HZ"); close(fd); return -1; }

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
                if (rv < 0) { perror("WTF!"); close(fd); return -1; }
        }

	close(fd);
	spi_reset();

	return 0;
}

#define SPIMAXSZ 256*1024
static unsigned char spibuf[SPIMAXSZ], spibuf_rx[SPIMAXSZ];
static unsigned spibufsz;

void spi_clear_buf(void) { spibufsz = 0; }
unsigned char *spi_getrxbuf(void) { return spibuf_rx; }
unsigned char *spi_gettxbuf(void) { return spibuf; }
unsigned spi_getbufsz(void) { return spibufsz; }

void spi_emit_buf_reverse(const char *str, unsigned sz)
{
	unsigned i;
	if (spibufsz + sz >= SPIMAXSZ) return;
	for (i = 0; i < sz; i++) { // Reverse bit order in each byte!
		unsigned char p = str[i];
		p = ((p & 0xaa)>>1) | ((p & 0x55) << 1);
		p = ((p & 0xcc)>>2) | ((p & 0x33) << 2);
		p = ((p & 0xf0)>>4) | ((p & 0x0f) << 4);
		spibuf[spibufsz+i] = p;
	}
	spibufsz += sz;
}

void spi_emit_buf(const char *str, unsigned sz)
{
	unsigned i;
	if (spibufsz + sz >= SPIMAXSZ) return;
	memcpy(&spibuf[spibufsz], str, sz); spibufsz += sz;
}

/* TODO: in production, emit just bit-sequences! Eliminate padding to byte! */
void spi_emit_break(void) { spi_emit_buf("\x4", 1); }
void spi_emit_fsync(void) { spi_emit_buf("\x6", 1); }

void spi_emit_fasync(int n) {
	int i;
	for (i = 0; i < n; i++) {
		spi_emit_buf("\x5", 1);
	}
}

void spi_emit_nop(int n) {
	int i;
	for (i = 0; i < n; n++) {
		spi_emit_buf("\x0", 1);
	}
}

void spi_emit_data(unsigned addr, const char *buf, unsigned len)
{
	unsigned char otmp[3];
	if (len < 4 || len > 128) return; /* This cannot be programmed in single frame! */
	len /= 4; /* Strip */
	otmp[0] = (len - 1) | 0xE0;
	otmp[1] = (addr >> 8)&0xFF; otmp[2] = addr & 0xFF;
	spi_emit_buf(otmp, 3);
	spi_emit_buf_reverse(buf, len*4);
}
