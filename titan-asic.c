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
	if (status == KNC_ACCEPTED) {
		*work_accepted = true;
	} else {
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

/* This fails if core is hashing!
 * Stop it before setting up.
 */
bool knc_titan_setup_core_local(const char *repr, void * const ctx, int channel, int die, int core, struct titan_setup_core_params *params)
{
	return knc_titan_setup_core(ctx, channel, die, core, params);
}
