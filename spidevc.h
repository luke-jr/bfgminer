/*
 *   spidevc.h - SPI library for raspberry pi/bitfury chip/board
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

#ifndef SPIDEVC_H
#define SPIDEVC_H

/* Initialize SPI using this function */
void spi_init(void);

/* TX-RX single frame */
int spi_txrx(const char *wrbuf, char *rdbuf, int bufsz);

/* SPI BUFFER OPS */
void spi_clear_buf(void);
unsigned char *spi_getrxbuf(void);
unsigned char *spi_gettxbuf(void);
unsigned spi_getbufsz(void);

void spi_emit_buf_reverse(const char *str, unsigned sz); /* INTERNAL USE: EMIT REVERSED BYTE SEQUENCE DIRECTLY TO STREAM */
void spi_emit_buf(const char *str, unsigned sz); /* INTERNAL USE: EMIT BYTE SEQUENCE DIRECTLY TO STREAM */

void spi_emit_break(void); /* BREAK CONNECTIONS AFTER RESET */
void spi_emit_fsync(void); /* FEED-THROUGH TO NEXT CHIP SYNCHRONOUSLY (WITH FLIP-FLOP) */
void spi_emit_fasync(int n); /* FEED-THROUGH TO NEXT CHIP ASYNCHRONOUSLY (WITHOUT FLIP-FLOP INTERMEDIATE) */
void spi_emit_nop(int n);

/* TRANSMIT PROGRAMMING SEQUENCE (AND ALSO READ-BACK) */
/* addr is the destination address in bits (16-bit - 0 to 0xFFFF valid ones)
   buf is buffer to be transmitted, it will go at position spi_getbufsz()+3
   len is length in _bytes_, should be 4 to 128 and be multiple of 4, as smallest
   transmission quantum is 32 bits */
void spi_emit_data(unsigned addr, const char *buf, unsigned len);

#endif
