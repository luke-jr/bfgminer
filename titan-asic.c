/*
 * Copyright 2014 Vitalii Demianets
 * Copyright 2014 KnCMiner
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "miner.h"
#include "logging.h"

#include "titan-asic.h"

bool knc_titan_get_info(const char *repr, void * const ctx, int channel, int die, struct knc_die_info *die_info)
{
	int rc;
	rc = knc_detect_die(ctx, channel, die, die_info);
	return (0 == rc);
}

bool knc_titan_set_work(const char *repr, void * const ctx, int channel, int die, int core, int slot, struct work *work, bool urgent, bool *work_accepted, struct knc_report *report)
{
	int request_length = 4 + 1 + 6*4 + 3*4 + 8*4;
	uint8_t request[request_length];
	int response_length = 1 + 1 + (1 + 4) * 5;
	uint8_t response[response_length];
	int status;

	request_length = knc_prepare_titan_setwork(request, die, core, slot, work, urgent);
	status = knc_syncronous_transfer(ctx, channel, request_length, request, response_length, response);
	if (status != KNC_ACCEPTED) {
		*work_accepted = false;
		if (response[0] == 0x7f) {
			applog(LOG_DEBUG, "%s[%d:%d]: Core disabled", repr, channel, die);
			return false;
		}
		if (status & KNC_ERR_MASK) {
			applog(LOG_ERR, "%s[%d:%d]: Failed to set work state (%x)", repr, channel, die, status);
			return false;
		}
		if (!(status & KNC_ERR_MASK)) {
			/* !KNC_ERRMASK */
			applog(LOG_DEBUG, "%s[%d:%d]: Core busy", repr, channel, die, status);
		}
	}

	knc_decode_report(response, report, KNC_VERSION_TITAN);
	return true;
}

bool knc_titan_get_report(const char *repr, void * const ctx, int channel, int die, int core, struct knc_report *report)
{
	uint8_t request[4];
	int request_length;
	int response_length = 1 + 1 + (1 + 4) * 5;
	uint8_t response[response_length];
	int status;

	request_length = knc_prepare_report(request, die, core);
	status = knc_syncronous_transfer(ctx, channel, request_length, request, response_length, response);
	if (status) {
		applog(LOG_ERR, "%s[%d:%d]: get_report failed (%x)", repr, channel, die, status);
		return false;
	}

	knc_decode_report(response, report, KNC_VERSION_TITAN);
	return true;
}

bool knc_titan_setup_core(const char *repr, void * const ctx, int channel, int die, int core, struct titan_setup_core_params *params)
{
#define	SETWORK_CMD_SIZE	(5 + BLOCK_HEADER_BYTES_WITHOUT_NONCE)
	/* The size of command is the same as for set_work */
	uint8_t setup_core_cmd[SETWORK_CMD_SIZE] = {
		KNC_ASIC_CMD_SETUP_CORE,
		die,
		(core >> 8) & 0xFF,
		core & 0xFF,
		/* next follows padding and data */
	};
	const int send_size = sizeof(setup_core_cmd);
	int response_length = send_size;
	uint8_t response[response_length];
	int status;
	uint32_t *src, *dst;
	int i;
	struct titan_packed_core_params {
		/* WORD [0] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t padding			:26;
		uint32_t bad_address_mask_0_6msb	:6;
#else
		uint32_t bad_address_mask_0_6msb	:6;
		uint32_t padding			:26;
#endif
		/* WORD [1] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t bad_address_mask_0_4lsb	:4;
		uint32_t bad_address_mask_1		:10;
		uint32_t bad_address_match_0		:10;
		uint32_t bad_address_match_1_8msb	:8;
#else
		uint32_t bad_address_match_1_8msb	:8;
		uint32_t bad_address_match_0		:10;
		uint32_t bad_address_mask_1		:10;
		uint32_t bad_address_mask_0_4lsb	:4;
#endif
		/* WORD [2] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t bad_address_match_1_2lsb	:2;
		uint32_t difficulty			:6;
		uint32_t thread_enable			:8;
		uint32_t thread_base_address_0		:10;
		uint32_t thread_base_address_1_6msb	:6;
#else
		uint32_t thread_base_address_1_6msb	:6;
		uint32_t thread_base_address_0		:10;
		uint32_t thread_enable			:8;
		uint32_t difficulty			:6;
		uint32_t bad_address_match_1_2lsb	:2;
#endif
		/* WORD [3] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t thread_base_address_1_4lsb	:4;
		uint32_t thread_base_address_2		:10;
		uint32_t thread_base_address_3		:10;
		uint32_t thread_base_address_4_8msb	:8;
#else
		uint32_t thread_base_address_4_8msb	:8;
		uint32_t thread_base_address_3		:10;
		uint32_t thread_base_address_2		:10;
		uint32_t thread_base_address_1_4lsb	:4;
#endif
		/* WORD [4] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t thread_base_address_4_2lsb	:2;
		uint32_t thread_base_address_5		:10;
		uint32_t thread_base_address_6		:10;
		uint32_t thread_base_address_7		:10;
#else
		uint32_t thread_base_address_7		:10;
		uint32_t thread_base_address_6		:10;
		uint32_t thread_base_address_5		:10;
		uint32_t thread_base_address_4_2lsb	:2;
#endif
		/* WORD [5] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t lookup_gap_mask_0		:10;
		uint32_t lookup_gap_mask_1		:10;
		uint32_t lookup_gap_mask_2		:10;
		uint32_t lookup_gap_mask_3_2msb		:2;
#else
		uint32_t lookup_gap_mask_3_2msb		:2;
		uint32_t lookup_gap_mask_2		:10;
		uint32_t lookup_gap_mask_1		:10;
		uint32_t lookup_gap_mask_0		:10;
#endif
		/* WORD [6] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t lookup_gap_mask_3_8lsb		:8;
		uint32_t lookup_gap_mask_4		:10;
		uint32_t lookup_gap_mask_5		:10;
		uint32_t lookup_gap_mask_6_4msb		:4;
#else
		uint32_t lookup_gap_mask_6_4msb		:4;
		uint32_t lookup_gap_mask_5		:10;
		uint32_t lookup_gap_mask_4		:10;
		uint32_t lookup_gap_mask_3_8lsb		:8;
#endif
		/* WORD [7] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t lookup_gap_mask_6_6lsb		:6;
		uint32_t lookup_gap_mask_7		:10;
		uint32_t N_mask_0			:10;
		uint32_t N_mask_1_6msb			:6;
#else
		uint32_t N_mask_1_6msb			:6;
		uint32_t N_mask_0			:10;
		uint32_t lookup_gap_mask_7		:10;
		uint32_t lookup_gap_mask_6_6lsb		:6;
#endif
		/* WORD [8] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t N_mask_1_4lsb			:4;
		uint32_t N_mask_2			:10;
		uint32_t N_mask_3			:10;
		uint32_t N_mask_4_8msb			:8;
#else
		uint32_t N_mask_4_8msb			:8;
		uint32_t N_mask_3			:10;
		uint32_t N_mask_2			:10;
		uint32_t N_mask_1_4lsb			:4;
#endif
		/* WORD [9] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t N_mask_4_2lsb			:2;
		uint32_t N_mask_5			:10;
		uint32_t N_mask_6			:10;
		uint32_t N_mask_7			:10;
#else
		uint32_t N_mask_7			:10;
		uint32_t N_mask_6			:10;
		uint32_t N_mask_5			:10;
		uint32_t N_mask_4_2lsb			:2;
#endif
		/* WORD [10] */
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
		uint32_t N_shift_0			:4;
		uint32_t N_shift_1			:4;
		uint32_t N_shift_2			:4;
		uint32_t N_shift_3			:4;
		uint32_t N_shift_4			:4;
		uint32_t N_shift_5			:4;
		uint32_t N_shift_6			:4;
		uint32_t N_shift_7			:4;
#else
		uint32_t N_shift_7			:4;
		uint32_t N_shift_6			:4;
		uint32_t N_shift_5			:4;
		uint32_t N_shift_4			:4;
		uint32_t N_shift_3			:4;
		uint32_t N_shift_2			:4;
		uint32_t N_shift_1			:4;
		uint32_t N_shift_0			:4;
#endif
		/* WORD [11] */
		uint32_t nonce_top;
		/* WORD [12] */
		uint32_t nonce_bottom;
	} __attribute__((packed)) packed_params;

	packed_params.padding = 0;
	packed_params.bad_address_mask_0_6msb = (params->bad_address_mask[0] >> 4) & 0x03F;
	packed_params.bad_address_mask_0_4lsb = params->bad_address_mask[0] & 0x00F;
	packed_params.bad_address_mask_1 = params->bad_address_mask[1];
	packed_params.bad_address_match_0 = params->bad_address_match[0];
	packed_params.bad_address_match_1_8msb = (params->bad_address_match[1] >> 2) & 0x0FF;
	packed_params.bad_address_match_1_2lsb = params->bad_address_match[1] & 0x003;
	packed_params.difficulty = params->difficulty;
	packed_params.thread_enable = params->thread_enable;
	packed_params.thread_base_address_0 = params->thread_base_address[0];
	packed_params.thread_base_address_1_6msb = (params->thread_base_address[1] >> 4) & 0x03F;
	packed_params.thread_base_address_1_4lsb = params->thread_base_address[1] & 0x00F;
	packed_params.thread_base_address_2 = params->thread_base_address[2];
	packed_params.thread_base_address_3 = params->thread_base_address[3];
	packed_params.thread_base_address_4_8msb = (params->thread_base_address[4] >> 2) & 0x0FF;
	packed_params.thread_base_address_4_2lsb = params->thread_base_address[4] & 0x003;
	packed_params.thread_base_address_5 = params->thread_base_address[5];
	packed_params.thread_base_address_6 = params->thread_base_address[6];
	packed_params.thread_base_address_7 = params->thread_base_address[7];
	packed_params.lookup_gap_mask_0 = params->lookup_gap_mask[0];
	packed_params.lookup_gap_mask_1 = params->lookup_gap_mask[1];
	packed_params.lookup_gap_mask_2 = params->lookup_gap_mask[2];
	packed_params.lookup_gap_mask_3_2msb = (params->lookup_gap_mask[3] >> 8) & 0x003;
	packed_params.lookup_gap_mask_3_8lsb = params->lookup_gap_mask[3] & 0x0FF;
	packed_params.lookup_gap_mask_4 = params->lookup_gap_mask[4];
	packed_params.lookup_gap_mask_5 = params->lookup_gap_mask[5];
	packed_params.lookup_gap_mask_6_4msb = (params->lookup_gap_mask[6] >> 6) & 0x00F;
	packed_params.lookup_gap_mask_6_6lsb = params->lookup_gap_mask[6] & 0x03F;
	packed_params.lookup_gap_mask_7 = params->lookup_gap_mask[7];
	packed_params.N_mask_0 = params->N_mask[0];
	packed_params.N_mask_1_6msb = (params->N_mask[1] >> 4) & 0x03F;
	packed_params.N_mask_1_4lsb = params->N_mask[1] & 0x00F;
	packed_params.N_mask_2 = params->N_mask[2];
	packed_params.N_mask_3 = params->N_mask[3];
	packed_params.N_mask_4_8msb = (params->N_mask[4] >> 2) & 0x0FF;
	packed_params.N_mask_4_2lsb = params->N_mask[4] & 0x003;
	packed_params.N_mask_5 = params->N_mask[5];
	packed_params.N_mask_6 = params->N_mask[6];
	packed_params.N_mask_7 = params->N_mask[7];
	packed_params.N_shift_0 = params->N_shift[0];
	packed_params.N_shift_1 = params->N_shift[1];
	packed_params.N_shift_2 = params->N_shift[2];
	packed_params.N_shift_3 = params->N_shift[3];
	packed_params.N_shift_4 = params->N_shift[4];
	packed_params.N_shift_5 = params->N_shift[5];
	packed_params.N_shift_6 = params->N_shift[6];
	packed_params.N_shift_7 = params->N_shift[7];
	packed_params.nonce_top = params->nonce_top;
	packed_params.nonce_bottom = params->nonce_bottom;

	src = (uint32_t *)&packed_params;
	dst = (uint32_t *)(&setup_core_cmd[send_size - sizeof(packed_params)]);
	for (i = 0; i < (sizeof(packed_params) / 4); ++i)
		dst[i] = htobe32(src[i]);

	status = knc_syncronous_transfer(ctx, channel, send_size, setup_core_cmd, response_length, response);
	if (status) {
		applog(LOG_ERR, "%s[%d:%d]: setup_core failed (%x)", repr, channel, die, status);
		return false;
	}

	return true;
}
