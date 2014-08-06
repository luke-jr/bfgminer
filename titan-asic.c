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

#include "miner.h"
#include "logging.h"
#include "lowl-spi.h"

#include "titan-asic.h"

/* ASIC Command codes */
#define	KNC_ASIC_CMD_GETINFO		0x80
#define	KNC_ASIC_CMD_REPORT		0x82
#define	KNC_ASIC_CMD_SETWORK		0x81
#define	KNC_ASIC_CMD_SETWORK_URGENT	0x83

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
static uint8_t * spi_transfer(struct spi_port * const spi, uint8_t *send_buf, int send_size, int transfer_size, int rcv_crc_data_len, uint32_t *errors)
{
	uint8_t *rxbuf, crcbuf[CRC32_SIZE];
	uint32_t crc;
	uint8_t rcv_status;
	int min_transfer_size = send_size + CRC32_SIZE + SPI_RESPONSE_TRAILER_SIZE;
	if (0 < rcv_crc_data_len) {
		if (min_transfer_size < (4 + rcv_crc_data_len + CRC32_SIZE + SPI_RESPONSE_TRAILER_SIZE))
			min_transfer_size = 4 + rcv_crc_data_len + CRC32_SIZE + SPI_RESPONSE_TRAILER_SIZE;
	}

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
	*((uint32_t *)crcbuf) = htobe32(crc);
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
	if (0 < rcv_crc_data_len) {
		crc = crc32(0, Z_NULL, 0);
		crc = crc32(crc, rxbuf + 4, rcv_crc_data_len);
		memcpy(crcbuf, &rxbuf[4 + rcv_crc_data_len], CRC32_SIZE);
		if (crc != be32toh(*((uint32_t *)crcbuf)))
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
	uint8_t get_info_cmd[] = {KNC_ASIC_CMD_GETINFO, die, 0x00, 0x00};
	uint8_t *rxbuf;
	uint32_t errors;
	uint16_t revision;
	int transfer_size = 24 + ((core_hint + 3) / 4);
	int i, core;

	for (i = 0; i < 3; ++i) {
		rxbuf = spi_transfer(spi, get_info_cmd, sizeof(get_info_cmd), transfer_size, transfer_size - 4 - CRC32_SIZE - SPI_RESPONSE_TRAILER_SIZE, &errors);
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
		if (KNC_TITAN_ASIC_REVISION != revision) {
exit_bad_revision:	applog(LOG_ERR, "%s[%d] knc_titan_spi_get_info: Bad revision 0x%04hX", repr, die, revision);
			return false;
		}
		resp->cores = (rxbuf[4] << 8) | rxbuf[5];
		if (resp->cores != core_hint) {
			applog(LOG_NOTICE, "%s[%d] core hint %d might be wrong, new guess is %d", repr, die, core_hint, resp->cores);
			transfer_size = 24 + ((resp->cores + 3) / 4);
			for (i = 0; i < 3; ++i) {
				rxbuf = spi_transfer(spi, get_info_cmd, sizeof(get_info_cmd), transfer_size, transfer_size - 4 - CRC32_SIZE - SPI_RESPONSE_TRAILER_SIZE, &errors);
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
	if (KNC_TITAN_ASIC_REVISION != revision)
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

static void knc_titan_parse_get_report(uint8_t *data, struct titan_report *report)
{
	int i;

	report->flags = data[0];
	report->core_counter = data[1];
	report->slot_core = (data[2] >> 4) & 0x0F;
	for (i = 0; i < KNC_TITAN_NONCES_PER_REPORT; ++i) {
		report->nonces[i].slot = data[2 + i * 5] & 0x0F;
		report->nonces[i].nonce = ((uint32_t)data[2 + i * 5 + 1] << 24) |
					  ((uint32_t)data[2 + i * 5 + 2] << 16) |
					  ((uint32_t)data[2 + i * 5 + 3] << 8) |
					  ((uint32_t)data[2 + i * 5 + 4]);
	}
}

bool knc_titan_set_work(const char *repr, struct spi_port * const spi, struct titan_report *report, int die, int core, int slot, struct work *work, bool urgent)
{
#define	SETWORK_CMD_SIZE	(5 + BLOCK_HEADER_BYTES_WITHOUT_NONCE)
	uint8_t set_work_cmd_aligned[3 + SETWORK_CMD_SIZE] = {
		0, 0, 0, /* three extra bytes for alignment */
		urgent ? KNC_ASIC_CMD_SETWORK_URGENT : KNC_ASIC_CMD_SETWORK,
		die,
		(core >> 8) & 0xFF,
		core & 0xFF,
		0xF0 | (slot & 0x0F),
		/* next follows data. Thanks to the first three extra bytes it is 64bit-aligned */
	};
	const int send_size = sizeof(set_work_cmd_aligned) - 3;
	const int transfer_size = send_size + CRC32_SIZE + SPI_RESPONSE_TRAILER_SIZE;
	uint8_t *rxbuf;
	int i;
	uint32_t *src, *dst;
	uint32_t errors;

	src = (uint32_t *)work->data;
	dst = (uint32_t *)(&set_work_cmd_aligned[3 + 5]);
	for (i = 0; i < (BLOCK_HEADER_BYTES_WITHOUT_NONCE / 4); ++i)
		dst[i] = htobe32(src[i]);

	rxbuf = spi_transfer(spi, &set_work_cmd_aligned[3], send_size, transfer_size, 2 + KNC_TITAN_NONCES_PER_REPORT * 5, &errors);
	if (NULL == rxbuf) {
		applog(LOG_ERR, "%s[%d:%d] knc_titan_set_work: Unrecognized error", repr, die, core);
		return false;
	}
	if (0 != errors) {
		applog(LOG_ERR, "%s[%d:%d] knc_titan_set_work: Communication failed, errors = 0x%X", repr, die, core, errors);
		return false;
	}
	knc_titan_parse_get_report(&rxbuf[4], report);
	return true;
}

bool knc_titan_get_report(const char *repr, struct spi_port * const spi, struct titan_report *report, int die, int core)
{
	uint8_t get_report_cmd[] = {KNC_ASIC_CMD_REPORT, die, (core >> 8) & 0xFF, core & 0xFF};
	const int send_size = sizeof(get_report_cmd);
	const int transfer_size = send_size + 2 + KNC_TITAN_NONCES_PER_REPORT * 5 + CRC32_SIZE + SPI_RESPONSE_TRAILER_SIZE;
	uint8_t *rxbuf;
	uint32_t errors;

	rxbuf = spi_transfer(spi, get_report_cmd, send_size, transfer_size, 2 + KNC_TITAN_NONCES_PER_REPORT * 5, &errors);
	if (NULL == rxbuf) {
		applog(LOG_ERR, "%s[%d:%d] knc_titan_get_report: Unrecognized error", repr, die, core);
		return false;
	}
	if (0 != errors) {
		applog(LOG_ERR, "%s[%d:%d] knc_titan_get_report: Communication failed, errors = 0x%X", repr, die, core, errors);
		return false;
	}
	knc_titan_parse_get_report(&rxbuf[4], report);
	return true;
}
