/*
 * cgminer driver for KnCminer devices
 *
 * Copyright 2014 KnCminer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#include <zlib.h>

#include "deviceapi.h"
#include "logging.h"
#include "miner.h"
#include "knc-asic/knc-transport.h"
#include "knc-asic/knc-asic.h"

#define WORKS_PER_CORE          3

#define CORE_ERROR_LIMIT	30
#define CORE_ERROR_INTERVAL	30
#define CORE_ERROR_DISABLE_TIME	5*60
#define CORE_SUBMIT_MIN_TIME	2
#define CORE_TIMEOUT		20
#define SCAN_ADJUST_RANGE	32

BFG_REGISTER_DRIVER(kncasic_drv)

static struct timeval now;
static const struct timeval core_check_interval = {
	CORE_ERROR_INTERVAL, 0
};
static const struct timeval core_disable_interval = {
	CORE_ERROR_DISABLE_TIME, 0
};
static const struct timeval core_submit_interval = {
	CORE_SUBMIT_MIN_TIME, 0
};
static const struct timeval core_timeout_interval = {
	CORE_TIMEOUT, 0
};

struct knc_die;

struct knc_core_state {
	int generation;
	int core;
	struct knc_die *die;
	struct {
		int slot;
		struct work *work;
	} workslot[WORKS_PER_CORE]; 	/* active, next */
	int transfer_stamp;
	struct knc_report report;
	struct {
		int slot;
		uint32_t nonce;
	} last_nonce;
	uint32_t works;
	uint32_t shares;
	uint32_t errors;
	uint32_t completed;
	int last_slot;
	uint32_t errors_now;
	struct timeval disabled_until;
	struct timeval hold_work_until;
	struct timeval timeout;
	bool inuse;
	
	struct cgpu_info *proc;
};

struct knc_state;

struct knc_die {
	int channel;
	int die;
	int version;
	int cores;
	struct knc_state *knc;
	struct knc_core_state *core;
};

#define MAX_SPI_SIZE		(4096)
#define MAX_SPI_RESPONSES	(MAX_SPI_SIZE / (2 + 4 + 1 + 1 + 1 + 4))
#define MAX_SPI_MESSAGE		(128)
#define KNC_SPI_BUFFERS		(3)

struct knc_state {
	void *ctx;
	int generation;    /* work/block generation, incremented on each flush invalidating older works */
	int dies;
	struct knc_die die[KNC_MAX_ASICS * KNC_MAX_DIES_PER_ASIC];
	int cores;
	int scan_adjust;
	int startup;
	/* Statistics */
	uint64_t shares;		/* diff1 shares reported by hardware */
	uint64_t works;			/* Work units submitted */
	uint64_t completed;		/* Work units completed */
	uint64_t errors;		/* Hardware & communication errors */
	struct timeval next_error_interval;
	/* End of statistics */
	/* SPI communications thread */
	pthread_mutex_t spi_qlock;	/* SPI queue status lock */
	struct thr_info spi_thr;	/* SPI I/O thread */
	pthread_cond_t spi_qcond;	/* SPI queue change wakeup */
	struct knc_spi_buffer {
		enum {
			KNC_SPI_IDLE=0,
			KNC_SPI_PENDING,
			KNC_SPI_DONE
		} state;
		int size;
		uint8_t txbuf[MAX_SPI_SIZE];
		uint8_t rxbuf[MAX_SPI_SIZE];
		int responses;
		struct knc_spi_response {
			int request_length;
			int response_length;
			enum {
				KNC_UNKNOWN = 0,
				KNC_NO_RESPONSE,
				KNC_SETWORK,
				KNC_REPORT,
				KNC_INFO
			} type;
			struct knc_core_state *core;
			uint32_t data;
			int offset;
		} response_info[MAX_SPI_RESPONSES];
	} spi_buffer[KNC_SPI_BUFFERS];
	int send_buffer;
	int read_buffer;
	int send_buffer_count;
	int read_buffer_count;
	/* end SPI thread */

	/* lock to protect resources between different threads */
	pthread_mutex_t state_lock;

	/* Do not add anything below here!! core[] must be last */
	struct knc_core_state core[];
};

int opt_knc_device_bus = -1;
char *knc_log_file = NULL;

static void *knc_spi(void *thr_data)
{
	struct cgpu_info *cgpu = thr_data;
	struct knc_state *knc = cgpu->device_data;
	int buffer = 0;
	
	pthread_mutex_lock(&knc->spi_qlock);
	while (!cgpu->shutdown) {
		int this_buffer = buffer;
		while (knc->spi_buffer[buffer].state != KNC_SPI_PENDING && !cgpu->shutdown)
			pthread_cond_wait(&knc->spi_qcond, &knc->spi_qlock);
		pthread_mutex_unlock(&knc->spi_qlock);
		if (cgpu->shutdown)
			return NULL;

		knc_trnsp_transfer(knc->ctx, knc->spi_buffer[buffer].txbuf, knc->spi_buffer[buffer].rxbuf, knc->spi_buffer[buffer].size);

		buffer += 1;
		if (buffer >= KNC_SPI_BUFFERS)
			buffer = 0;

		pthread_mutex_lock(&knc->spi_qlock);
		knc->spi_buffer[this_buffer].state = KNC_SPI_DONE;
		pthread_cond_signal(&knc->spi_qcond);
	}
	pthread_mutex_unlock(&knc->spi_qlock);
	return NULL;
}

static void knc_process_responses(struct thr_info *thr);

static void knc_flush(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct knc_state *knc = cgpu->device_data;
	struct knc_spi_buffer *buffer = &knc->spi_buffer[knc->send_buffer];
	if (buffer->state == KNC_SPI_IDLE && buffer->size > 0) {
		pthread_mutex_lock(&knc->spi_qlock);
		buffer->state = KNC_SPI_PENDING;
		pthread_cond_signal(&knc->spi_qcond);
		knc->send_buffer += 1;
		knc->send_buffer_count += 1;
		if (knc->send_buffer >= KNC_SPI_BUFFERS)
			knc->send_buffer = 0;
		buffer = &knc->spi_buffer[knc->send_buffer];
		/* Block for SPI to finish a transfer if all buffers are busy */
		while (buffer->state == KNC_SPI_PENDING) {
			applog(LOG_DEBUG, "KnC: SPI buffer full (%d), waiting for SPI thread", buffer->responses);
			pthread_cond_wait(&knc->spi_qcond, &knc->spi_qlock);
		}
		pthread_mutex_unlock(&knc->spi_qlock);
	}
        knc_process_responses(thr);
}

static void knc_transfer(struct thr_info *thr, struct knc_core_state *core, int request_length, uint8_t *request, int response_length, int response_type, uint32_t data)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct knc_state *knc = cgpu->device_data;
	struct knc_spi_buffer *buffer = &knc->spi_buffer[knc->send_buffer];
	/* FPGA control, request header, request body/response, CRC(4), ACK(1), EXTRA(3) */
	int msglen = 2 + max(request_length, 4 + response_length) + 4 + 1 + 3;
	if (buffer->size + msglen > MAX_SPI_SIZE || buffer->responses >= MAX_SPI_RESPONSES) {
		applog(LOG_INFO, "KnC: SPI buffer sent, %d messages %d bytes", buffer->responses, buffer->size);
		knc_flush(thr);
		buffer = &knc->spi_buffer[knc->send_buffer];
	}
	struct knc_spi_response *response_info = &buffer->response_info[buffer->responses];
	buffer->responses++;
	response_info->offset = buffer->size;
	response_info->type = response_type;
	response_info->request_length = request_length;
	response_info->response_length = response_length;
	response_info->core = core;
	response_info->data = data;
	buffer->size = knc_prepare_transfer(buffer->txbuf, buffer->size, MAX_SPI_SIZE, core->die->channel, request_length, request, response_length);
}

static int knc_transfer_stamp(struct knc_state *knc)
{
	return knc->send_buffer_count;
}

static int knc_transfer_completed(struct knc_state *knc, int stamp)
{
	/* signed delta math, counter wrap OK */
	return (int)(knc->read_buffer_count - stamp) >= 1;
}

static bool knc_detect_one(void *ctx)
{
	/* Scan device for ASICs */
	int channel, die, cores = 0, core;
	struct knc_state *knc;
	struct knc_die_info die_info[KNC_MAX_ASICS][KNC_MAX_DIES_PER_ASIC];

	memset(die_info, 0, sizeof(die_info));

	/* Send GETINFO to each die to detect if it is usable */
	for (channel = 0; channel < KNC_MAX_ASICS; channel++) {
		if (!knc_trnsp_asic_detect(ctx, channel))
			continue;
		for (die = 0; die < KNC_MAX_DIES_PER_ASIC; die++) {
		    if (knc_detect_die(ctx, channel, die, &die_info[channel][die]) == 0)
			cores += die_info[channel][die].cores;
		}
	}

	if (!cores) {
		applog(LOG_NOTICE, "no KnCminer cores found");
		return false;
	}

	applog(LOG_ERR, "Found a KnC miner with %d cores", cores);

	knc = calloc(1, sizeof(*knc) + cores * sizeof(struct knc_core_state));
	if (!knc)
	{
		applog(LOG_ERR, "KnC miner detected, but failed to allocate memory");
		return false;
	}

	knc->ctx = ctx;
	knc->generation = 1;

	/* Index all cores */
	struct cgpu_info *prev_cgpu = NULL, *first_cgpu;
	int dies = 0;
	cores = 0;
	struct knc_core_state *pcore = knc->core;
	int channel_cores_base = 0;
	for (channel = 0; channel < KNC_MAX_ASICS; channel++) {
		int channel_cores = 0;
		
		for (die = 0; die < KNC_MAX_DIES_PER_ASIC; die++) {
			if (die_info[channel][die].cores) {
				knc->die[dies].channel = channel;
				knc->die[dies].die = die;
				knc->die[dies].version = die_info[channel][die].version;
				knc->die[dies].cores = die_info[channel][die].cores;
				knc->die[dies].core = pcore;
				knc->die[dies].knc = knc;
				for (core = 0; core < knc->die[dies].cores; core++) {
					knc->die[dies].core[core].die = &knc->die[dies];
					knc->die[dies].core[core].core = core;
				}
				cores += knc->die[dies].cores;
				channel_cores += knc->die[dies].cores;
				pcore += knc->die[dies].cores;
				dies++;
			}
		}
		
		if (channel_cores)
		{
			struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
			*cgpu = (struct cgpu_info){
				.drv = &kncasic_drv,
				.name = "KnCminer",
				.procs = channel_cores,
				.threads = prev_cgpu ? 0 : 1,
				.device_data = knc,
			};
			add_cgpu_slave(cgpu, prev_cgpu);
			if (!prev_cgpu)
				first_cgpu = cgpu;
			prev_cgpu = cgpu;
			
			for_each_managed_proc(proc, cgpu)
			{
				knc->core[channel_cores_base++].proc = proc;
			}
		}
	}
	
	knc->dies = dies;
	knc->cores = cores;
	knc->startup = 2;

	pthread_mutex_init(&knc->spi_qlock, NULL);
	pthread_cond_init(&knc->spi_qcond, NULL);
	pthread_mutex_init(&knc->state_lock, NULL);

	if (thr_info_create(&knc->spi_thr, NULL, knc_spi, first_cgpu))
	{
		applog(LOG_ERR, "%s: SPI thread create failed", first_cgpu->dev_repr);
		free(knc);
		return false;
	}
	
	return true;
}

/* Probe devices and register with add_cgpu */
static
bool kncasic_detect_one(const char * const devpath)
{
	void *ctx = knc_trnsp_new(devpath);

	if (ctx != NULL) {
		if (!knc_detect_one(ctx))
			knc_trnsp_free(ctx);
		else
			return true;
	}
	return false;
}

static
int kncasic_detect_auto(void)
{
	return kncasic_detect_one(NULL) ? 1 : 0;
}

static
void kncasic_detect(void)
{
	generic_detect(&kncasic_drv, kncasic_detect_one, kncasic_detect_auto, GDF_REQUIRE_DNAME | GDF_DEFAULT_NOAUTO);
}

/* Core helper functions */
static int knc_core_hold_work(struct knc_core_state *core)
{
	return timercmp(&core->hold_work_until, &now, >);
}

static int knc_core_has_work(struct knc_core_state *core)
{
	int i;
	for (i = 0; i < WORKS_PER_CORE; i++) {
		if (core->workslot[i].slot > 0)
			return true;
	}
	return false;
}

static int knc_core_need_work(struct knc_core_state *core)
{
	return !knc_core_hold_work(core) && !core->workslot[1].work && !core->workslot[2].work;
}

static int knc_core_disabled(struct knc_core_state *core)
{
	return timercmp(&core->disabled_until, &now, >);
}

static int _knc_core_next_slot(struct knc_core_state *core)
{
	/* Avoid slot #0 and #15. #0 is "no work assigned" and #15 is seen on bad cores */
	int slot = core->last_slot + 1;
	if (slot >= 15)
		slot = 1;
	core->last_slot = slot;
	return slot;
}

static bool knc_core_slot_busy(struct knc_core_state *core, int slot)
{
	if (slot == core->report.active_slot)
		return true;
	if (slot == core->report.next_slot)
		return true;
	int i;
	for (i = 0; i < WORKS_PER_CORE; i++) {
		if (slot == core->workslot[i].slot)
			return true;
	}
	return false;
}

static int knc_core_next_slot(struct knc_core_state *core)
{
	int slot;
	do slot = _knc_core_next_slot(core);
	while (knc_core_slot_busy(core, slot));
	return slot;
}

static void knc_core_failure(struct knc_core_state *core)
{
	core->errors++;
	core->errors_now++;
	core->die->knc->errors++;
	if (knc_core_disabled(core))
		return;
	if (core->errors_now > CORE_ERROR_LIMIT) {
		struct cgpu_info * const proc = core->proc;
		applog(LOG_ERR, "%"PRIpreprv" disabled for %ld seconds due to repeated hardware errors",
			proc->proc_repr, (long)core_disable_interval.tv_sec);
		timeradd(&now, &core_disable_interval, &core->disabled_until);
	}
}

static
void knc_core_handle_nonce(struct thr_info *thr, struct knc_core_state *core, int slot, uint32_t nonce)
{
	int i;
	if (!slot)
		return;
	core->last_nonce.slot = slot;
	core->last_nonce.nonce = nonce;
	if (core->die->knc->startup)
		return;
	for (i = 0; i < WORKS_PER_CORE; i++) {
		if (slot == core->workslot[i].slot && core->workslot[i].work) {
			struct cgpu_info * const proc = core->proc;
			struct thr_info * const corethr = proc->thr[0];
			
			applog(LOG_INFO, "%"PRIpreprv" found nonce %08x", proc->proc_repr, nonce);
			if (submit_nonce(corethr, core->workslot[i].work, nonce)) {
				/* Good share */
				core->shares++;
				core->die->knc->shares++;
				hashes_done2(corethr, 0x100000000, NULL);
				/* This core is useful. Ignore any errors */
				core->errors_now = 0;
			} else {
				applog(LOG_INFO, "%"PRIpreprv" hwerror nonce %08x", proc->proc_repr, nonce);
				/* Bad share */
				knc_core_failure(core);
			}
		}
	}
}

static int knc_core_process_report(struct thr_info *thr, struct knc_core_state *core, uint8_t *response)
{
	struct cgpu_info * const proc = core->proc;
	struct knc_report *report = &core->report;
	knc_decode_report(response, report, core->die->version);
	bool had_event = false;

	applog(LOG_DEBUG, "%"PRIpreprv": Process report %d %d(%d) / %d %d %d", proc->proc_repr, report->active_slot, report->next_slot, report->next_state, core->workslot[0].slot, core->workslot[1].slot, core->workslot[2].slot);
	int n;
	for (n = 0; n < KNC_NONCES_PER_REPORT; n++) {
		if (report->nonce[n].slot < 0)
			break;
		if (core->last_nonce.slot == report->nonce[n].slot && core->last_nonce.nonce == report->nonce[n].nonce)
			break;
	}
	while(n-- > 0) {
		knc_core_handle_nonce(thr, core, report->nonce[n].slot, report->nonce[n].nonce);
	}

	if (report->active_slot && core->workslot[0].slot != report->active_slot) {
		had_event = true;
		applog(LOG_INFO, "%"PRIpreprv": New work %d %d / %d %d %d", proc->proc_repr, report->active_slot, report->next_slot, core->workslot[0].slot, core->workslot[1].slot, core->workslot[2].slot);
		/* Core switched to next work */
		if (core->workslot[0].work) {
			core->die->knc->completed++;
			core->completed++;
			applog(LOG_INFO, "%"PRIpreprv": Work completed!", proc->proc_repr);
			free_work(core->workslot[0].work);
		}
		core->workslot[0] = core->workslot[1];
		core->workslot[1].work = NULL;
		core->workslot[1].slot = -1;

		/* or did it switch directly to pending work? */
		if (report->active_slot == core->workslot[2].slot) {
			applog(LOG_INFO, "%"PRIpreprv": New work %d %d %d %d (pending)", proc->proc_repr, report->active_slot, core->workslot[0].slot, core->workslot[1].slot, core->workslot[2].slot);
			if (core->workslot[0].work)
				free_work(core->workslot[0].work);
			core->workslot[0] = core->workslot[2];
			core->workslot[2].work = NULL;
			core->workslot[2].slot = -1;
		}
	}

	if (report->next_state && core->workslot[2].slot > 0 && (core->workslot[2].slot == report->next_slot  || report->next_slot == -1)) {
		had_event = true;
		applog(LOG_INFO, "%"PRIpreprv": Accepted work %d %d %d %d (pending)", proc->proc_repr, report->active_slot, core->workslot[0].slot, core->workslot[1].slot, core->workslot[2].slot);
		/* core accepted next work */
		if (core->workslot[1].work)
			free_work(core->workslot[1].work);
		core->workslot[1] = core->workslot[2];
		core->workslot[2].work = NULL;
		core->workslot[2].slot = -1;
	}

	if (core->workslot[2].work && knc_transfer_completed(core->die->knc, core->transfer_stamp)) {
		had_event = true;
		applog(LOG_INFO, "%"PRIpreprv": Setwork failed?", proc->proc_repr);
		free_work(core->workslot[2].work);
		core->workslot[2].slot = -1;
	}

	if (had_event)
		applog(LOG_INFO, "%"PRIpreprv": Exit report %d %d / %d %d %d", proc->proc_repr, report->active_slot, report->next_slot, core->workslot[0].slot, core->workslot[1].slot, core->workslot[2].slot);

	return 0;
}

static void knc_process_responses(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct knc_state *knc = cgpu->device_data;
	struct knc_spi_buffer *buffer = &knc->spi_buffer[knc->read_buffer];
	while (buffer->state == KNC_SPI_DONE) {
		int i;
		for (i = 0; i < buffer->responses; i++) {
			struct knc_spi_response *response_info = &buffer->response_info[i];
			uint8_t *rxbuf = &buffer->rxbuf[response_info->offset];
			struct knc_core_state *core = response_info->core;
			struct cgpu_info * const proc = core->proc;
			int status = knc_decode_response(rxbuf, response_info->request_length, &rxbuf, response_info->response_length);
			/* Invert KNC_ACCEPTED to simplify logics below */
			if (response_info->type == KNC_SETWORK && !KNC_IS_ERROR(status))
				status ^= KNC_ACCEPTED;
			if (core->die->version != KNC_VERSION_JUPITER && status != 0) {
				applog(LOG_ERR, "%s: Communication error (%x / %d)", proc->proc_repr, status, i);
				if (status == KNC_ACCEPTED) {
					/* Core refused our work vector. Likely out of sync. Reset it */
					core->inuse = false;
				}
				knc_core_failure(core);
			}
			switch(response_info->type) {
			case KNC_REPORT:
			case KNC_SETWORK:
				/* Should we care about failed SETWORK explicit? Or simply handle it by next state not loaded indication in reports?  */
				knc_core_process_report(thr, core, rxbuf);
				break;
			default:
				break;
			}
		}

		buffer->state = KNC_SPI_IDLE;
		buffer->responses = 0;
		buffer->size = 0;
		knc->read_buffer += 1;
		knc->read_buffer_count += 1;
		if (knc->read_buffer >= KNC_SPI_BUFFERS)
			knc->read_buffer = 0;
		buffer = &knc->spi_buffer[knc->read_buffer];
	}
}

static int knc_core_send_work(struct thr_info *thr, struct knc_core_state *core, struct work *work, bool clean)
{
	struct knc_state *knc = core->die->knc;
	struct cgpu_info * const proc = core->proc;
	int request_length = 4 + 1 + 6*4 + 3*4 + 8*4;
	uint8_t request[request_length];
	int response_length = 1 + 1 + (1 + 4) * 5;

	int slot = knc_core_next_slot(core);
	if (slot < 0)
		goto error;

	applog(LOG_INFO, "%"PRIpreprv" setwork%s  = %d, %d %d / %d %d %d", proc->proc_repr, clean ? " CLEAN" : "", slot, core->report.active_slot, core->report.next_slot, core->workslot[0].slot, core->workslot[1].slot, core->workslot[2].slot);
	if (!clean && !knc_core_need_work(core))
		goto error;

	switch(core->die->version) {
	case KNC_VERSION_JUPITER:
		if (clean) {
			/* Double halt to get rid of any previous queued work */
			request_length = knc_prepare_jupiter_halt(request, core->die->die, core->core);
			knc_transfer(thr, core, request_length, request, 0, KNC_NO_RESPONSE, 0);
			knc_transfer(thr, core, request_length, request, 0, KNC_NO_RESPONSE, 0);
		}
		request_length = knc_prepare_jupiter_setwork(request, core->die->die, core->core, slot, work);
		knc_transfer(thr, core, request_length, request, 0, KNC_NO_RESPONSE, 0);
		break;
	case KNC_VERSION_NEPTUNE:
		request_length = knc_prepare_neptune_setwork(request, core->die->die, core->core, slot, work, clean);
		knc_transfer(thr, core, request_length, request, response_length, KNC_SETWORK, slot);
		break;
	default:
		goto error;
	}

	core->workslot[2].work = work;
	core->workslot[2].slot = slot;
	core->works++;
	core->die->knc->works++;
	core->transfer_stamp = knc_transfer_stamp(knc);
	core->inuse = true;

	timeradd(&now, &core_submit_interval, &core->hold_work_until);
	timeradd(&now, &core_timeout_interval, &core->timeout);

	return 0;

error:
	applog(LOG_INFO, "%"PRIpreprv": Failed to setwork (%d)", proc->proc_repr, core->errors_now);
	knc_core_failure(core);
	free_work(work);
	return -1;
}

static int knc_core_request_report(struct thr_info *thr, struct knc_core_state *core)
{
	struct cgpu_info * const proc = core->proc;
	int request_length = 4;
	uint8_t request[request_length];
	int response_length = 1 + 1 + (1 + 4) * 5;

	applog(LOG_DEBUG, "%"PRIpreprv": Request report", proc->proc_repr);

	request_length = knc_prepare_report(request, core->die->die, core->core);

	switch(core->die->version) {
	case KNC_VERSION_JUPITER:
		response_length = 1 + 1 + (1 + 4);
		knc_transfer(thr, core, request_length, request, response_length, KNC_REPORT, 0);
		return 0;
	case KNC_VERSION_NEPTUNE:
		knc_transfer(thr, core, request_length, request, response_length, KNC_REPORT, 0);
		return 0;
	}

	applog(LOG_INFO, "%"PRIpreprv": Failed to scan work report", proc->proc_repr);
	knc_core_failure(core);
	return -1;
}

/* return value is number of nonces that have been checked since
 * previous call
 */
static int64_t knc_scanwork(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct knc_state *knc = cgpu->device_data;

	applog(LOG_DEBUG, "KnC running scanwork");
	mutex_lock(&knc->state_lock);

	gettimeofday(&now, NULL);

	knc_trnsp_periodic_check(knc->ctx);

	int i;

	knc_process_responses(thr);

	if (timercmp(&knc->next_error_interval, &now, >)) {
		/* Reset hw error limiter every check interval */
		timeradd(&now, &core_check_interval, &knc->next_error_interval);
		for (i = 0; i < knc->cores; i++) {
			struct knc_core_state *core = &knc->core[i];
			core->errors_now = 0;
		}
	}

	for (i = 0; i < knc->cores; i++) {
		struct knc_core_state *core = &knc->core[i];
		struct cgpu_info * const proc = core->proc;
		bool clean = !core->inuse;
		if (knc_core_disabled(core))
			continue;
		if (core->generation != knc->generation) {
			applog(LOG_INFO, "%"PRIpreprv" flush gen=%d/%d", proc->proc_repr, core->generation, knc->generation);
			/* clean set state, forget everything */
			int slot;
			for (slot = 0; slot < WORKS_PER_CORE; slot ++) {
				if (core->workslot[slot].work)
					free_work(core->workslot[slot].work);
				core->workslot[slot].slot = -1;
			}
			core->hold_work_until = now;
			core->generation = knc->generation;
		} else if (timercmp(&core->timeout, &now, <=) && (core->workslot[0].slot > 0 || core->workslot[1].slot > 0 || core->workslot[2].slot > 0)) {
			applog(LOG_ERR, "%"PRIpreprv" timeout gen=%d/%d", proc->proc_repr, core->generation, knc->generation);
			clean = true;
		}
		if (!knc_core_has_work(core))
			clean = true;
		if (core->workslot[0].slot < 0 && core->workslot[1].slot < 0 && core->workslot[2].slot < 0)
			clean = true;
		if (i % SCAN_ADJUST_RANGE == knc->scan_adjust)
			clean = true;
		if ((knc_core_need_work(core) || clean) && !knc->startup) {
			struct work *work = get_work(thr);
			knc_core_send_work(thr, core, work, clean);
		} else {
			knc_core_request_report(thr, core);
		}
	}
	/* knc->startup delays initial work submission until we have had chance to query all cores on their current status, to avoid slot number collisions with earlier run */
	if (knc->startup)
		knc->startup--;
	else if (knc->scan_adjust < SCAN_ADJUST_RANGE)
		knc->scan_adjust++;

	knc_flush(thr);

	mutex_unlock(&knc->state_lock);
	return 0;
}

static void knc_flush_work(struct cgpu_info *cgpu)
{
	struct knc_state *knc = cgpu->device_data;

	applog(LOG_INFO, "KnC running flushwork");

	mutex_lock(&knc->state_lock);

	knc->generation++;
	knc->scan_adjust=0;
	if (!knc->generation)
		knc->generation++;

	mutex_unlock(&knc->state_lock);
}

static void knc_zero_stats(struct cgpu_info *cgpu)
{
	int core;
	struct knc_state *knc = cgpu->device_data;

	mutex_lock(&knc->state_lock);
	for (core = 0; core < knc->cores; core++) {
		knc->shares = 0;
		knc->completed = 0;
		knc->works = 0;
		knc->errors = 0;
		knc->core[core].works = 0;
		knc->core[core].errors = 0;
		knc->core[core].shares = 0;
		knc->core[core].completed = 0;
	}
	mutex_unlock(&knc->state_lock);
}

static struct api_data *knc_api_stats(struct cgpu_info *cgpu)
{
	struct knc_state *knc = cgpu->device_data;
	struct knc_core_state * const proccore = &knc->core[cgpu->proc_id];
	struct knc_die * const die = proccore->die;
	struct api_data *root = NULL;
	int core;
	char label[256];

	mutex_lock(&knc->state_lock);

	root = api_add_int(root, "dies", &knc->dies, 1);
	root = api_add_int(root, "cores", &knc->cores, 1);
	root = api_add_uint64(root, "shares", &knc->shares, 1);
	root = api_add_uint64(root, "works", &knc->works, 1);
	root = api_add_uint64(root, "completed", &knc->completed, 1);
	root = api_add_uint64(root, "errors", &knc->errors, 1);

	/* Active cores */
	int active = knc->cores;
	for (core = 0; core < knc->cores; core++) {
		if (knc_core_disabled(&knc->core[core]))
			active -= 1;
	}
	root = api_add_int(root, "active", &active, 1);

	/* Per ASIC/die data */
	{
#define knc_api_die_string(name, value) do { \
	snprintf(label, sizeof(label), "%d.%d.%s", die->channel, die->die, name); \
	root = api_add_string(root, label, value, 1); \
	} while(0)
#define knc_api_die_int(name, value) do { \
	snprintf(label, sizeof(label), "%d.%d.%s", die->channel, die->die, name); \
	uint64_t v = value; \
	root = api_add_uint64(root, label, &v, 1); \
	} while(0)

		/* Model */
		{
			char *model = "?";
			switch(die->version) {
			case KNC_VERSION_JUPITER:
				model = "Jupiter";
				break;
			case KNC_VERSION_NEPTUNE:
				model = "Neptune";
				break;
			}
			knc_api_die_string("model", model);
			knc_api_die_int("cores", die->cores);
		}

		/* Core based stats */
		{
			uint64_t errors = 0;
			uint64_t shares = 0;
			uint64_t works = 0;
			uint64_t completed = 0;
			char coremap[die->cores+1];

			for (core = 0; core < die->cores; core++) {
				coremap[core] = knc_core_disabled(&die->core[core]) ? '0' : '1';
				works += die->core[core].works;
				shares += die->core[core].shares;
				errors += die->core[core].errors;
				completed += die->core[core].completed;
			}
			coremap[die->cores] = '\0';
			knc_api_die_int("errors", errors);
			knc_api_die_int("shares", shares);
			knc_api_die_int("works", works);
			knc_api_die_int("completed", completed);
			knc_api_die_string("coremap", coremap);
		}
	}

	mutex_unlock(&knc->state_lock);
	return root;
}

static
void hash_driver_work(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct device_drv * const drv = cgpu->drv;
	
	while (likely(!cgpu->shutdown))
	{
		drv->scanwork(thr);
		
		if (unlikely(thr->pause || cgpu->deven != DEV_ENABLED))
			mt_disable(thr);
	}
}

struct device_drv kncasic_drv = {
	.dname = "kncasic",
	.name = "KNC",
	.drv_detect = kncasic_detect,
	.minerloop = hash_driver_work,
	.flush_work = knc_flush_work,
	.scanwork = knc_scanwork,
	.zero_stats = knc_zero_stats,
	.get_api_stats = knc_api_stats,
};
