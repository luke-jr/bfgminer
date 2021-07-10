/*
 * Copyright 2013 Andreas Auer
 * Copyright 2013-2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 * Twin Bitfury USB miner with two Bitfury ASIC
 */

#include "config.h"
#include "miner.h"
#include "logging.h"
#include "util.h"

#include "libbitfury.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "deviceapi.h"
#include "sha2.h"

#include "driver-twinfury.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

BFG_REGISTER_DRIVER(twinfury_drv)

static const uint8_t PREAMBLE[] = { 0xDE, 0xAD, 0xBE, 0xEF };

//------------------------------------------------------------------------------
static
bool twinfury_send_command(const int fd, const void * const tx, const uint16_t tx_size)
{
	if (opt_dev_protocol)
	{
		char hex[((4 + tx_size) * 2) + 1];
		bin2hex(hex, PREAMBLE, 4);
		bin2hex(&hex[8], tx, tx_size);
		applog(LOG_DEBUG, "%s fd=%d: DEVPROTO: SEND: %s", twinfury_drv.dname, fd, hex);
	}
	
	if(4 != write(fd, PREAMBLE, 4))
	{
		return false;
	}

	if(tx_size != write(fd, tx, tx_size))
	{
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------
static
int16_t twinfury_wait_response(const int fd, const void * const rx, const uint16_t rx_size)
{
	int16_t rx_len;
	int timeout = 20;

	while(timeout > 0)
	{
		rx_len = serial_read(fd, rx, rx_size);
		if(rx_len > 0)
			break;

		timeout--;
	}

	if (opt_dev_protocol)
	{
		char hex[(rx_len * 2) + 1];
		bin2hex(hex, rx, rx_len);
		applog(LOG_DEBUG, "%s fd=%d: DEVPROTO: RECV(%u=>%d): %s", twinfury_drv.dname, fd, rx_size, rx_len, hex);
	}
	
	if(unlikely(timeout == 0))
	{
		return -1;
	}

	return rx_len;
}

//------------------------------------------------------------------------------
static bool twinfury_detect_custom(const char *devpath, struct device_drv *api, struct twinfury_info *info)
{
	int fd = serial_open(devpath, info->baud, 1, true);

	if(fd < 0)
	{
		return false;
	}

	char buf[1024];
	int16_t len;

	applog(LOG_DEBUG, "%s: Probing for Twinfury device %s", twinfury_drv.dname, devpath);
	serial_read(fd, buf, sizeof(buf));
	if (!twinfury_send_command(fd, "I", 1))
	{
		applog(LOG_DEBUG, "%s: Failed writing id request to %s",
		       twinfury_drv.dname, devpath);
		serial_close(fd);
		return false;
	}
	len = twinfury_wait_response(fd, buf, sizeof(buf));
	if(len != 29)
	{
		applog(LOG_DEBUG, "%s: Not a valid response from device (%d)", twinfury_drv.dname, len);
		serial_close(fd);
		return false;
	}

	info->id.version = buf[1];
	memcpy(info->id.product, buf+2, 16);
	bin2hex(info->id.serial, buf+18, 11);
	applog(LOG_DEBUG, "%s: %s: %d, %s %s",
	       twinfury_drv.dname,
	       devpath,
	       info->id.version, info->id.product,
	       info->id.serial);

	char buf_state[sizeof(struct twinfury_state)+1];
	if (!twinfury_send_command(fd, "R", 1))
	{
		applog(LOG_DEBUG, "%s: Failed writing reset request to %s",
		       twinfury_drv.dname, devpath);
		serial_close(fd);
		return false;
	}
	len = 0;
	while(len == 0)
	{
		len = serial_read(fd, buf, sizeof(buf_state));
		cgsleep_ms(100);
	}
	serial_close(fd);

	if(len != 8)
	{
		applog(LOG_DEBUG, "%s: %s not responding to reset: %d",
		       twinfury_drv.dname,
		       devpath, len);
		return false;
	}

	if (serial_claim_v(devpath, api))
		return false;

	struct cgpu_info *bigpic;
	bigpic = calloc(1, sizeof(struct cgpu_info));
	bigpic->drv = api;
	bigpic->device_path = strdup(devpath);
	bigpic->device_fd = -1;
	bigpic->threads = 1;
	bigpic->procs = 2;
	add_cgpu(bigpic);

	applog(LOG_INFO, "Found %"PRIpreprv" at %s", bigpic->proc_repr, devpath);

	applog(LOG_DEBUG, "%"PRIpreprv": Init: baud=%d",
		bigpic->proc_repr, info->baud);

	bigpic->device_data = info;

	return true;
}

//------------------------------------------------------------------------------
static bool twinfury_detect_one(const char *devpath)
{
	struct twinfury_info *info = calloc(1, sizeof(struct twinfury_info));
	if (unlikely(!info))
		quit(1, "Failed to malloc bigpicInfo");

	info->baud = BPM_BAUD;

	if (!twinfury_detect_custom(devpath, &twinfury_drv, info))
	{
		free(info);
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
static bool twinfury_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_product(info, "Twinfury");
}

//------------------------------------------------------------------------------
static bool twinfury_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, twinfury_detect_one);
}

//------------------------------------------------------------------------------
static bool twinfury_init(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct twinfury_info *info = (struct twinfury_info *)cgpu->device_data;
	struct cgpu_info *proc;
	int i=0;

	applog(LOG_DEBUG, "%"PRIpreprv": init", cgpu->proc_repr);

	int fd = serial_open(cgpu->device_path, info->baud, 1, true);
	if (unlikely(-1 == fd))
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed to open %s",
				cgpu->proc_repr, cgpu->device_path);
		return false;
	}

	cgpu->device_fd = fd;

	applog(LOG_INFO, "%"PRIpreprv": Opened %s", cgpu->proc_repr, cgpu->device_path);

	info->tx_buffer[0] = 'W';

	if(info->id.version == 2)
	{
		char buf[8] = "V\x00\x00";
		if(twinfury_send_command(fd, buf, 3))
		{
			if(8 == twinfury_wait_response(fd, buf, 8))
			{
				info->voltage  =  (buf[4] & 0xFF);
				info->voltage |=  (buf[5] << 8);

				applog(LOG_DEBUG, "%s: Voltage: %dmV", cgpu->dev_repr, info->voltage);
				if(info->voltage < 800 || info->voltage > 950)
				{
					info->voltage = 0;
				}
			}
			else
			{
				applog(LOG_ERR, "%"PRIpreprv": Failed to get voltage.", cgpu->dev_repr);
				info->voltage = 0;
			}
		}
		else
		{
			applog(LOG_ERR, "%"PRIpreprv": Failed to send voltage request", cgpu->dev_repr);
		}
	}

	for(i=1, proc = cgpu->next_proc; proc; proc = proc->next_proc, i++)
	{
		struct twinfury_info *data = malloc(sizeof(struct twinfury_info));
		*data = *info;
		proc->device_data = data;
		data->tx_buffer[1] = i;
	}
	
	timer_set_now(&thr->tv_poll);

	return true;
}

//------------------------------------------------------------------------------
static bool twinfury_process_results(struct cgpu_info * const proc)
{
	struct twinfury_info *device = proc->device_data;
	uint8_t *rx_buffer = device->rx_buffer;
	int16_t rx_len = device->rx_len;

	struct work *work = proc->thr[0]->work;

	if(rx_len == 0 || rx_len == -1)
	{
		return false;
	}

	if(rx_buffer[3] == 0)
	{
		return false;
	}

	if(!work)
	{
		return true;
	}

	uint32_t m7    = *((uint32_t *)&work->data[64]);
	uint32_t ntime = *((uint32_t *)&work->data[68]);
	uint32_t nbits = *((uint32_t *)&work->data[72]);

	int j=0;
	for(j=0; j<rx_len; j+= 8)
	{
		struct twinfury_state state;
		state.chip = rx_buffer[j + 1];
		state.state = rx_buffer[j + 2];
		state.switched = rx_buffer[j + 3];
		memcpy(&state.nonce, rx_buffer + j + 4, 4);

		uint32_t nonce = bitfury_decnonce(state.nonce);
		if((nonce & 0xFFC00000) != 0xdf800000)
		{
			applog(LOG_DEBUG, "%"PRIpreprv": Len: %lu Cmd: %c Chip: %d State: %c Switched: %d Nonce: %08lx",
					proc->proc_repr,
				   (unsigned long)rx_len, rx_buffer[j], state.chip, state.state, state.switched, (unsigned long)nonce);
			if (bitfury_fudge_nonce(work->midstate, m7, ntime, nbits, &nonce))
				submit_nonce(proc->thr[0], work, nonce);
			else
				inc_hw_errors(proc->thr[0], work, nonce);
		}
	}
	return true;
}

//------------------------------------------------------------------------------
int64_t twinfury_job_process_results(struct thr_info *thr, struct work *work, bool stopping)
{
	// Bitfury chips process only 768/1024 of the nonce range
	return 0xbd000000;
}

//------------------------------------------------------------------------------
static
bool twinfury_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info *board = thr->cgpu;
	struct twinfury_info *info = (struct twinfury_info *)board->device_data;

	memcpy(&info->tx_buffer[ 2], work->midstate, 32);
	memcpy(&info->tx_buffer[34], &work->data[64], 12);

	work->blk.nonce = 0xffffffff;
	return true;
}

//------------------------------------------------------------------------------
static
void twinfury_poll(struct thr_info *thr)
{
	struct cgpu_info * const dev = thr->cgpu;
	struct twinfury_info *info = dev->device_data;

	uint8_t n_chips = 0;
	uint8_t buffer[2] = { 'Q', 0 };

	uint8_t response[8];
	bool flashed = false;

	if(info->send_voltage)
	{
		char buf[8] = "V";
		buf[1] = info->voltage & 0xFF;
		buf[2] = (info->voltage >> 8) & 0xFF;
		if(!twinfury_send_command(dev->device_fd, buf, 3))
			applog(LOG_ERR, "%s: Failed supply voltage", dev->dev_repr);
		else
		if(8 != twinfury_wait_response(dev->device_fd, buf, 8))
		{
			applog(LOG_ERR, "%s: Waiting for response timed out (Supply voltage)", dev->dev_repr);
		}
		else
		{
			info->voltage  =  (buf[4] & 0xFF);
			info->voltage |=  (buf[5] << 8);
		}
		info->send_voltage = false;
	}

	for (struct cgpu_info *proc = dev; proc; proc = proc->next_proc, ++n_chips)
	{
		struct thr_info * const proc_thr = proc->thr[0];
		struct twinfury_info *info = (struct twinfury_info *)proc->device_data;
		
		if (proc->flash_led)
		{
			if (flashed)
				proc->flash_led = 0;
			else
			{
				char buf[] = "L";
				
				if(!twinfury_send_command(dev->device_fd, buf, 1))
					applog(LOG_ERR, "%s: Failed writing flash LED", dev->dev_repr);
				else
				if(1 != twinfury_wait_response(dev->device_fd, buf, 1))
					applog(LOG_ERR, "%s: Waiting for response timed out (Flash LED)", dev->dev_repr);
				else
				{
					flashed = true;
					proc->flash_led = 0;
				}
			}
		}
		
		buffer[1] = n_chips;

		if(!twinfury_send_command(dev->device_fd, buffer, 2))
		{
			applog(LOG_ERR, "%"PRIpreprv": Failed writing work task", proc->proc_repr);
			dev_error(dev, REASON_DEV_COMMS_ERROR);
			return;
		}

		info->rx_len = twinfury_wait_response(dev->device_fd, info->rx_buffer, sizeof(info->rx_buffer));
		if(unlikely(info->rx_len == -1))
		{
			applog(LOG_ERR, "%"PRIpreprv": Query timeout", proc->proc_repr);
		}

		if(twinfury_process_results(proc) && proc_thr->next_work)
		{
			mt_job_transition(proc_thr);
			// TODO: Delay morework until right before it's needed
			timer_set_now(&proc_thr->tv_morework);
			job_start_complete(proc_thr);
		}
	}

	buffer[0] = 'T';
	if(twinfury_send_command(dev->device_fd, buffer, 1))
	{
		if(8 == twinfury_wait_response(dev->device_fd, response, 8))
		{
			if(response[0] == buffer[0])
			{
				const float temp = ((uint16_t)response[4] | (uint16_t)(response[5] << 8)) / 10.0;
				for (struct cgpu_info *proc = dev; proc; proc = proc->next_proc)
					proc->temp = temp;
				applog(LOG_DEBUG, "%"PRIpreprv": Temperature: %f", dev->dev_repr, temp);
			}
		}
		else
		{
			applog(LOG_DEBUG, "%"PRIpreprv": No temperature response", dev->dev_repr);
		}
	}

	timer_set_delay_from_now(&thr->tv_poll, 250000);
}

//------------------------------------------------------------------------------
static
void twinfury_job_start(struct thr_info *thr)
{
	struct cgpu_info *board = thr->cgpu;
	struct twinfury_info *info = (struct twinfury_info *)board->device_data;
	int device_fd = thr->cgpu->device->device_fd;

	uint8_t buffer[8];
	int16_t len;

	if(!twinfury_send_command(device_fd, info->tx_buffer, 46))
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed writing work task", board->proc_repr);
		dev_error(board, REASON_DEV_COMMS_ERROR);
		job_start_abort(thr, true);
		return;
	}

	len = twinfury_wait_response(device_fd, buffer, 8);
	if(unlikely(len == -1))
	{
		applog(LOG_ERR, "%"PRIpreprv": Work send timeout.", board->proc_repr);
	}
}

//------------------------------------------------------------------------------
static void twinfury_shutdown(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;

	serial_close(cgpu->device_fd);
}

//------------------------------------------------------------------------------
static bool twinfury_identify(struct cgpu_info *cgpu)
{
	cgpu->flash_led = 1;

	return true;
}

#ifdef HAVE_CURSES
void twinfury_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	struct twinfury_info * const state = proc->device->device_data;
	if(state->id.version > 1)
	{
		wlogprint("[V]oltage ");
	}
}

const char *twinfury_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	struct twinfury_info * const state = proc->device->device_data;

	if(state->id.version > 1)
	{
		switch (input)
		{
			case 'v': case 'V':
			{
				const int val = curses_int("Set supply voltage (range 800mV-950mV; slow to fast)");
				if (val < 800 || val > 950)
					return "Invalid supply voltage value\n";

				state->voltage = val;
				state->send_voltage = true;

				return "Supply voltage changing\n";
			}
		}
	}
	return NULL;
}

void twinfury_wlogprint_status(struct cgpu_info * const proc)
{
	const struct twinfury_info * const state = proc->device->device_data;
	if(state->id.version > 1)
		wlogprint("Supply voltage: %dmV\n", state->voltage);
}
#endif

//------------------------------------------------------------------------------
struct device_drv twinfury_drv = {
        //lowercase driver name so --scan pattern matching works
	.dname = "twinfury",
	.name = "TBF",
	.probe_priority = -111,

	.lowl_match = twinfury_lowl_match,
	.lowl_probe = twinfury_lowl_probe,

	.identify_device = twinfury_identify,

	.thread_init = twinfury_init,

	.minerloop = minerloop_async,

	.job_prepare = twinfury_job_prepare,
	.job_start = twinfury_job_start,
	.poll = twinfury_poll,
	.job_process_results = twinfury_job_process_results,

	.thread_shutdown = twinfury_shutdown,

#ifdef HAVE_CURSES
	.proc_wlogprint_status = twinfury_wlogprint_status,
	.proc_tui_wlogprint_choices = twinfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = twinfury_tui_handle_choice,
#endif

};
