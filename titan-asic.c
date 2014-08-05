/*
 * Copyright 2014 Vitalii Demianets
 * Copyright 2014 KnCMiner
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <zlib.h>

#include "logging.h"
#include "lowl-spi.h"

#include "titan-asic.h"

/* Error bits */
#define	ERR_SEND_CRC_FAIL	(1 << 0)
#define	ERR_RCV_CRC_FAIL	(1 << 1)
#define	ERR_BAD_RESPONSE	(1 << 2)
#define	ERR_OTHER_ERR		(1 << 31)

#define	CRC32_SIZE			4
/* In SPI responses after crc goes trailer: status(1 byte) + address(3 bytes) */
#define	SPI_RESPONSE_TRAILER_SIZE	4

#define	RCV_STATUS_NOFLAGS	0x81
#define	RCV_STATUS_ACCEPTED_WORK (1 << 2)
#define	RCV_STATUS_SEND_CRC_BAD	(1 << 5)

/* send_size - size of send_buf, without crc
 * transfer_size - total size of transfer
 */
static uint8_t * spi_transfer(struct spi_port * const spi, uint8_t *send_buf, int send_size, int transfer_size, bool check_rcv_crc, uint32_t *errors)
{
	uint8_t *rxbuf, crcbuf[CRC32_SIZE];
	uint32_t crc;
	uint8_t rcv_status;
	int min_transfer_size = check_rcv_crc ? (send_size + SPI_RESPONSE_TRAILER_SIZE + CRC32_SIZE) : (send_size + CRC32_SIZE);

	*errors = 0;
	if (transfer_size < min_transfer_size) {
exit_other_error:
		*errors |= ERR_OTHER_ERR;
		return NULL;
	}
	spi_clear_buf(spi);
	spi_emit_buf(spi, send_buf, send_size);
	crc = crc32(0, Z_NULL, 0);
	crc = crc32(crc, send_buf, send_size);
	*((uint32_t *)crcbuf) = htonl(crc);
	spi_emit_buf(spi, crcbuf, CRC32_SIZE);
	spi_emit_nop(spi, transfer_size - spi_getbufsz(spi));
	if (!spi_txrx(spi))
		goto exit_other_error;
	rxbuf = spi_getrxbuf(spi);

	rcv_status = rxbuf[transfer_size - SPI_RESPONSE_TRAILER_SIZE];
	if (RCV_STATUS_NOFLAGS != (rcv_status & (~(RCV_STATUS_SEND_CRC_BAD | RCV_STATUS_ACCEPTED_WORK))))
		*errors |= ERR_BAD_RESPONSE;
	if (rcv_status & RCV_STATUS_SEND_CRC_BAD)
		*errors |= ERR_SEND_CRC_FAIL;
	if (check_rcv_crc) {
		crc = crc32(0, Z_NULL, 0);
		crc = crc32(crc, rxbuf + send_size, transfer_size - send_size - SPI_RESPONSE_TRAILER_SIZE - CRC32_SIZE);
		memcpy(crcbuf, &rxbuf[transfer_size - SPI_RESPONSE_TRAILER_SIZE - CRC32_SIZE], CRC32_SIZE);
		if (crc != ntohl(*((uint32_t *)crcbuf)))
			*errors |= ERR_RCV_CRC_FAIL;
	}

	return rxbuf;
}

/*
 * core_hint - which number of cores is expected. The function needs to know it to
 *   calculate the SPI transfer size. It uses this hint for the first transfer.
 *   If the first transfer fails, it assumes that it is because of wrong hint and
 *   then tries to detect right number of cores from the first response.
 */
bool knc_titan_spi_get_info(const char *repr, struct spi_port * const spi, struct titan_info_response *resp, int die, int core_hint)
{
	uint8_t get_info_cmd[] = {0x80, die, 0x00, 0x00};
	uint8_t *rxbuf;
	uint32_t errors;
	uint16_t revision;
	int transfer_size = 24 + ((core_hint + 3) / 4);
	int i, core;

	for (i = 0; i < 3; ++i) {
		rxbuf = spi_transfer(spi, get_info_cmd, sizeof(get_info_cmd), transfer_size, true, &errors);
		if (NULL == rxbuf) {
exit_unrec_error:	applog(LOG_ERR, "%s[%d] knc_titan_spi_get_info: Unrecognized error", repr, die);
			return false;
		}
		if (errors != ERR_SEND_CRC_FAIL)
			break;
		/* If the only error is SEND_CRC, assume there was a communication error
		 * and retry three times
		 */
	}
	if (ERR_SEND_CRC_FAIL == errors) {
		applog(LOG_ERR, "%s[%d] knc_titan_spi_get_info: CRC error in Tx", repr, die);
		return false;
	}

	if (0 != errors) {
		/* It might be that we have different number of cores. Try to guess it
		 * from partial response.
		 */
		revision = (rxbuf[6] << 8) | rxbuf[7];
		if (0xA102 != revision) {
exit_bad_revision:	applog(LOG_ERR, "%s[%d] knc_titan_spi_get_info: Bad revision 0x%04hX", repr, die, revision);
			return false;
		}
		resp->cores = (rxbuf[4] << 8) | rxbuf[5];
		if (resp->cores != core_hint) {
			applog(LOG_NOTICE, "%s[%d] core hint %d might be wrong, new guess is %d", repr, die, core_hint, resp->cores);
			transfer_size = 24 + ((resp->cores + 3) / 4);
			for (i = 0; i < 3; ++i) {
				rxbuf = spi_transfer(spi, get_info_cmd, sizeof(get_info_cmd), transfer_size, true, &errors);
				if (NULL == rxbuf)
					goto exit_unrec_error;
				if (errors != ERR_SEND_CRC_FAIL)
					break;
				/* If the only error is SEND_CRC, assume there was a communication error
				 * and retry three times
				 */
			}
		}
	}

	if (0 != errors) {
		applog(LOG_ERR, "%s[%d] knc_titan_spi_get_info: Communication failed, errors = 0x%X", repr, die, errors);
		return false;
	}

	revision = (rxbuf[6] << 8) | rxbuf[7];
	if (0xA102 != revision)
		goto exit_bad_revision;
	resp->cores = (rxbuf[4] << 8) | rxbuf[5];
	resp->pll_state = *((uint64_t *)(&rxbuf[8]));
	for (core = 0; core < resp->cores; ) {
		uint8_t data = rxbuf[16 + (core / 4)];
		resp->want_work[core] = !!(data & (1 << 7));
		resp->have_report[core] = !!(data & (1 << 6));
		if (++core >= resp->cores)
			break;
		resp->want_work[core] = !!(data & (1 << 5));
		resp->have_report[core] = !!(data & (1 << 4));
		if (++core >= resp->cores)
			break;
		resp->want_work[core] = !!(data & (1 << 3));
		resp->have_report[core] = !!(data & (1 << 2));
		if (++core >= resp->cores)
			break;
		resp->want_work[core] = !!(data & (1 << 1));
		resp->have_report[core] = !!(data & (1 << 0));
		if (++core >= resp->cores)
			break;
	}

	return true;
}
