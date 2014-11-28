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

bool knc_titan_get_info(int log_level, void * const ctx, int channel, int die, struct knc_die_info *die_info)
{
	int rc;
	rc = knc_detect_die_(log_level, ctx, channel, die, die_info);
	return (0 == rc);
}

bool knc_titan_set_work(const char *repr, void * const ctx, int channel, int die, int core, int slot, struct work *work, bool urgent, bool *work_accepted, struct knc_report *report)
{
	int request_length = 4 + 1 + BLOCK_HEADER_BYTES_WITHOUT_NONCE;
	uint8_t request[request_length];
	int response_length = 1 + 1 + (1 + 4) * 5;
	uint8_t response[response_length];
	int status;

	request_length = knc_prepare_titan_setwork(request, die, core, slot, work, urgent);
	status = knc_syncronous_transfer(ctx, channel, request_length, request, response_length, response);
	if (status == KNC_ACCEPTED) {
		*work_accepted = true;
	} else {
		*work_accepted = false;
		if (response[0] == 0x7f) {
			applog(LOG_DEBUG, "%s[%d:%d:%d]: Core disabled", repr, channel, die, core);
			return false;
		}
		if (status & KNC_ERR_MASK) {
			applog(LOG_INFO, "%s[%d:%d:%d]: Failed to set work state (%x)", repr, channel, die, core, status);
			return false;
		}
		if (!(status & KNC_ERR_MASK)) {
			/* !KNC_ERRMASK */
			applog(LOG_DEBUG, "%s[%d:%d:%d]: Core busy (%x)", repr, channel, die, core, status);
		}
	}

	knc_decode_report(response, report, KNC_VERSION_TITAN);
	return true;
}

bool knc_titan_set_work_multi(const char *repr, void * const ctx, int channel, int die, int core_start, int slot, struct work *work, bool urgent, bool *work_accepted, struct knc_report *reports, int num)
{
	int REQUEST_BUFSIZE = 4 + 1 + BLOCK_HEADER_BYTES_WITHOUT_NONCE;
	int RESPONSE_BUFSIZE = 1 + 1 + (1 + 4) * 5;
	int i, core;
	uint8_t *requests = NULL;
	uint8_t *responses = NULL;
	int *request_lengths = NULL;
	int *response_lengths = NULL;
	int *statuses = NULL;

	requests = malloc(REQUEST_BUFSIZE * num);
	if (NULL == requests)
		goto exit_err;
	responses = malloc(RESPONSE_BUFSIZE * num);
	if (NULL == responses)
		goto exit_err;
	request_lengths = malloc(num * sizeof(int));
	if (NULL == request_lengths)
		goto exit_err;
	response_lengths = malloc(num * sizeof(int));
	if (NULL == response_lengths)
		goto exit_err;
	statuses = malloc(num * sizeof(int));
	if (NULL == statuses)
		goto exit_err;

	for (i = 0, core = core_start; i < num; ++i, ++core) {
		request_lengths[i] = knc_prepare_titan_setwork(&requests[REQUEST_BUFSIZE * i], die, core, slot, work, urgent);
		response_lengths[i] = RESPONSE_BUFSIZE;
		statuses[i] = KNC_ERR_UNAVAIL;
	}

	knc_syncronous_transfer_multi(ctx, channel, request_lengths, REQUEST_BUFSIZE, requests, response_lengths, RESPONSE_BUFSIZE, responses, statuses, num);

	for (i = 0, core = core_start; i < num; ++i, ++core) {
		uint8_t *response = &responses[RESPONSE_BUFSIZE * i];
		if (statuses[i] == KNC_ACCEPTED) {
			work_accepted[i] = true;
		} else {
			work_accepted[i] = false;
			if (response[0] == 0x7f) {
				applog(LOG_DEBUG, "%s[%d:%d:%d]: Core disabled", repr, channel, die, core);
				continue;
			}
			if (statuses[i] & KNC_ERR_MASK) {
				applog(LOG_INFO, "%s[%d:%d:%d]: Failed to set work state (%x)", repr, channel, die, core, statuses[i]);
				continue;
			}
			if (!(statuses[i] & KNC_ERR_MASK)) {
				applog(LOG_DEBUG, "%s[%d:%d:%d]: Core busy (%x)", repr, channel, die, core, statuses[i]);
			}
		}
		knc_decode_report(response, &reports[i], KNC_VERSION_TITAN);
	}

	free(response_lengths);
	free(request_lengths);
	free(statuses);
	free(responses);
	free(requests);
	return true;
exit_err:
	if (NULL != response_lengths)
		free(response_lengths);
	if (NULL != request_lengths)
		free(request_lengths);
	if (NULL != statuses)
		free(statuses);
	if (NULL != responses)
		free(responses);
	if (NULL != requests)
		free(requests);
	return false;
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
		applog(LOG_INFO, "%s[%d:%d:%d]: get_report failed (%x)", repr, channel, die, core, status);
		return false;
	}

	knc_decode_report(response, report, KNC_VERSION_TITAN);
	return true;
}

/* This fails if core is hashing!
 * Stop it before setting up.
 */
bool knc_titan_setup_core_local(const char *repr, void * const ctx, int channel, int die, int core, struct titan_setup_core_params *params)
{
	return knc_titan_setup_core_(LOG_INFO, ctx, channel, die, core, params);
}

bool knc_titan_setup_spi(const char *repr, void * const ctx, int asic, int divider, int preclk, int declk, int sslowmin)
{
	uint8_t request[7];
	int request_length;
	int status;

	request_length = knc_prepare_titan_setup(request, asic, divider, preclk, declk, sslowmin);

	status = knc_syncronous_transfer_fpga(ctx, request_length, request, 0, NULL);
	if (status) {
		applog(LOG_INFO, "%s[%d]: setup_spi failed (%x)", repr, asic, status);
		return false;
	}

	return true;
}

bool knc_titan_set_work_parallel(const char *repr, void * const ctx, int asic, int die, int core_start, int slot, struct work *work, bool urgent, int num, int resend)
{
	uint8_t request[9 + BLOCK_HEADER_BYTES_WITHOUT_NONCE];
	int request_length;
	int status;

	request_length = knc_prepare_titan_work_request(request, asic, die, slot, core_start, core_start + num - 1, resend, work);

	status = knc_syncronous_transfer_fpga(ctx, request_length, request, 0, NULL);
	if (status) {
		applog(LOG_INFO, "%s[%d]: set_work_parallel failed (%x)", repr, asic, status);
		return false;
	}

	return true;
}

bool knc_titan_get_work_status(const char *repr, void * const ctx, int asic, int *num_request_busy, int *num_status_byte_error)
{
	uint8_t request[2];
	int request_length;
	int response_length = 2 + 2 * KNC_STATUS_BYTE_ERROR_COUNTERS;
	uint8_t response[response_length];
	int status;
	uint8_t num_request_busy_byte;
	uint16_t num_status_byte_error_counters[KNC_STATUS_BYTE_ERROR_COUNTERS];

	request_length = knc_prepare_titan_work_status(request, asic);

	status = knc_syncronous_transfer_fpga(ctx, request_length, request, response_length, response);
	if (status) {
		applog(LOG_INFO, "%s[%d]: get_work_status failed (%x)", repr, asic, status);
		return false;
	}

	status = knc_decode_work_status(response, &num_request_busy_byte, num_status_byte_error_counters);
	if (status) {
		applog(LOG_INFO, "%s[%d]: get_work_status got undefined response", repr, asic);
		return false;
	}

	*num_request_busy = num_request_busy_byte;
	for (int i = 0 ; i < KNC_STATUS_BYTE_ERROR_COUNTERS ; i++)
		num_status_byte_error[i] = num_status_byte_error_counters[i];
	return true;
}
