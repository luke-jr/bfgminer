/*
 * Copyright 2013-2014 Luke Dashjr
 * Copyright 2013 Vladimir Strinski
 * Copyright 2013 HashBuster team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "deviceapi.h"
#include "driver-bitfury.h"
#include "libbitfury.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-usb.h"
#include "miner.h"

#define HASHBUSTER_USB_PRODUCT "HashBuster"

#define HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER 61

BFG_REGISTER_DRIVER(hashbusterusb_drv)
static const struct bfg_set_device_definition hashbusterusb_set_device_funcs[];

struct hashbusterusb_state {
	uint16_t voltage;
	struct timeval identify_started;
	bool identify_requested;
};

static
bool hashbusterusb_io(struct lowl_usb_endpoint * const h, unsigned char *buf, unsigned char *cmd)
{
	char x[0x81];
	
	bool rv = true;
	if (unlikely(opt_dev_protocol))
	{
		bin2hex(x, cmd, 0x40);
		applog(LOG_DEBUG, "%s(%p): SEND: %s", __func__, h, x);
	}
	
	do // Workaround for PIC USB buffer corruption. We should repeat last packet if receive FF
	{
		do
		{
			usb_write(h, cmd, 64);
		} while (usb_read(h, buf, 64) != 64);
	} while(buf[0]==0xFF);
	
	if (unlikely(opt_dev_protocol))
	{
		bin2hex(x, buf, 0x40);
		applog(LOG_DEBUG, "%s(%p): RECV: %s", __func__, h, x);
	}
	return rv;
}

static
bool hashbusterusb_spi_config(struct lowl_usb_endpoint * const h, const uint8_t mode, const uint8_t miso, const uint32_t freq)
{
	uint8_t buf[0x40] = {'\x01', '\x01'};
	if (!hashbusterusb_io(h, buf, buf))
		return false;
	return (buf[1] == '\x00');
}

static
bool hashbusterusb_spi_disable(struct lowl_usb_endpoint * const h)
{
	uint8_t buf[0x40] = {'\x01', '\x00'};
	if (!hashbusterusb_io(h, buf, buf))
		return false;
	return (buf[1] == '\x00');
}

static
bool hashbusterusb_spi_reset(struct lowl_usb_endpoint * const h, uint8_t chips)
{
	uint8_t buf[0x40] = {'\x02', '\x00', chips};
	if (!hashbusterusb_io(h, buf, buf))
		return false;
	return (buf[1] == '\x00');
}

static
bool hashbusterusb_spi_transfer(struct lowl_usb_endpoint * const h, void * const buf, const void * const data, size_t datasz)
{
	if (datasz > HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER)
		return false;
	uint8_t cbuf[0x40] = {'\x03', '\x00', datasz};
	memcpy(&cbuf[3], data, datasz);
	if (!hashbusterusb_io(h, cbuf, cbuf))
		return false;
	if (cbuf[2] != datasz)
		return false;
	memcpy(buf, &cbuf[3], datasz);
	return true;
}

static
bool hashbusterusb_spi_txrx(struct spi_port * const port)
{
	struct lowl_usb_endpoint * const h = port->userp;
	const uint8_t *wrbuf = spi_gettxbuf(port);
	uint8_t *rdbuf = spi_getrxbuf(port);
	size_t bufsz = spi_getbufsz(port);
	
	hashbusterusb_spi_disable(h);
	hashbusterusb_spi_reset(h, 0x10);
	
	hashbusterusb_spi_config(h, port->mode, 0, port->speed);
	
	while (bufsz >= HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER)
	{
		if (!hashbusterusb_spi_transfer(h, rdbuf, wrbuf, HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER))
			return false;
		rdbuf += HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
		wrbuf += HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
		bufsz -= HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
	}
	
	if (bufsz > 0)
	{
		if (!hashbusterusb_spi_transfer(h, rdbuf, wrbuf, bufsz))
			return false;
	}
	
	return true;
}

static
bool hashbusterusb_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_id(info, &lowl_usb, 0xFA04, 0x000D);
}

static
bool hashbusterusb_lowl_probe(const struct lowlevel_device_info * const info)
{
	struct cgpu_info *cgpu = NULL;
	struct bitfury_device **devicelist, *bitfury;
	struct spi_port *port;
	int j;
	struct cgpu_info dummy_cgpu;
	const char * const product = info->product;
	char *serial = info->serial;
	libusb_device_handle *h;
	
	if (info->lowl != &lowl_usb)
	{
		bfg_probe_result_flags = BPR_WRONG_DEVTYPE;
		applogr(false, LOG_DEBUG, "%s: Matched \"%s\" %s, but lowlevel driver is not usb_generic!",
		       __func__, product, info->devid);
	}
	
	if (info->vid != 0xFA04 || info->pid != 0x000D)
	{
		bfg_probe_result_flags = BPR_WRONG_DEVTYPE;
		applogr(false, LOG_DEBUG, "%s: Wrong VID/PID", __func__);
	}
	
	libusb_device *dev = info->lowl_data;
	if ( (j = libusb_open(dev, &h)) )
		applogr(false, LOG_ERR, "%s: Failed to open %s: %s",
		        __func__, info->devid, bfg_strerror(j, BST_LIBUSB));
	if ( (j = libusb_set_configuration(h, 1)) )
	{
		libusb_close(h);
		applogr(false, LOG_ERR, "%s: Failed to set configuration 1 on %s: %s",
		        __func__, info->devid, bfg_strerror(j, BST_LIBUSB));
	}
	if ( (j = libusb_claim_interface(h, 0)) )
	{
		libusb_close(h);
		applogr(false, LOG_ERR, "%s: Failed to claim interface 0 on %s: %s",
		        __func__, info->devid, bfg_strerror(j, BST_LIBUSB));
	}
	struct lowl_usb_endpoint * const ep = usb_open_ep_pair(h, 0x81, 64, 0x01, 64);
	usb_ep_set_timeouts_ms(ep, 100, 0);
	
	unsigned char OUTPacket[64] = { 0xfe };
	unsigned char INPacket[64];
	hashbusterusb_io(ep, INPacket, OUTPacket);
	if (INPacket[1] == 0x18)
	{
		// Turn on miner PSU
		OUTPacket[0] = 0x10;
		OUTPacket[1] = 0x00;
		OUTPacket[2] = 0x01;
		hashbusterusb_io(ep, INPacket, OUTPacket);
	}
	
	OUTPacket[0] = '\x20';
	hashbusterusb_io(ep, INPacket, OUTPacket);
	if (!memcmp(INPacket, "\x20\0", 2))
	{
		// 64-bit BE serial number
		uint64_t sernum = 0;
		for (j = 0; j < 8; ++j)
			sernum |= (uint64_t)INPacket[j + 2] << (j * 8);
		serial = malloc((8 * 2) + 1);
		sprintf(serial, "%08"PRIX64, sernum);
	}
	else
		serial = maybe_strdup(info->serial);
	
	int chip_n;
	
	port = malloc(sizeof(*port));
	port->cgpu = &dummy_cgpu;
	port->txrx = hashbusterusb_spi_txrx;
	port->userp = ep;
	port->repr = hashbusterusb_drv.dname;
	port->logprio = LOG_DEBUG;
	port->speed = 100000;
	port->mode = 0;
	
	chip_n = libbitfury_detectChips1(port);

	if (unlikely(!chip_n))
		chip_n = libbitfury_detectChips1(port);

	if (unlikely(!chip_n))
	{
		applog(LOG_WARNING, "%s: No chips found on %s (serial \"%s\")",
		       __func__, info->devid, serial);
fail:
		usb_close_ep(ep);
		free(port);
		free(serial);
		libusb_release_interface(h, 0);
		libusb_close(h);
		return false;
	}
	
	if (bfg_claim_libusb(&hashbusterusb_drv, true, dev))
		goto fail;
	
	{
		devicelist = malloc(sizeof(*devicelist) * chip_n);
		for (j = 0; j < chip_n; ++j)
		{
			devicelist[j] = bitfury = malloc(sizeof(*bitfury));
			*bitfury = (struct bitfury_device){
				.spi = port,
				.slot = 0,
				.fasync = j,
			};
		}
		
		cgpu = malloc(sizeof(*cgpu));
		*cgpu = (struct cgpu_info){
			.drv = &hashbusterusb_drv,
			.set_device_funcs = hashbusterusb_set_device_funcs,
			.procs = chip_n,
			.device_data = devicelist,
			.cutofftemp = 200,
			.threads = 1,
			.device_path = strdup(info->devid),
			.dev_manufacturer = maybe_strdup(info->manufacturer),
			.dev_product = maybe_strdup(product),
			.dev_serial = serial,
			.deven = DEV_ENABLED,
		};
	}
	
	return add_cgpu(cgpu);
}

static
bool hashbusterusb_init(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	
	struct bitfury_device **devicelist;
	struct bitfury_device *bitfury;
	struct hashbusterusb_state * const state = malloc(sizeof(*state));
	
	*state = (struct hashbusterusb_state){
		.voltage = 0,
	};
	cgpu_setup_control_requests(cgpu);
	
	for (proc = thr->cgpu; proc; proc = proc->next_proc)
	{
		devicelist = proc->device_data;
		bitfury = devicelist[proc->proc_id];
		proc->device_data = bitfury;
		proc->thr[0]->cgpu_data = state;
		bitfury->spi->cgpu = proc;
		bitfury_init_chip(proc);
		bitfury->osc6_bits = 53;
		bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		bitfury_init_freq_stat(&bitfury->chip_stat, 52, 56);
		
		if (proc->proc_id == proc->procs - 1)
			free(devicelist);
	}
	
	timer_set_now(&thr->tv_poll);
	cgpu->status = LIFE_INIT2;
	return true;
}

static void hashbusterusb_set_colour(struct cgpu_info *, uint8_t, uint8_t, uint8_t);

static
void hashbusterusb_poll(struct thr_info * const master_thr)
{
	struct hashbusterusb_state * const state = master_thr->cgpu_data;
	struct cgpu_info * const cgpu = master_thr->cgpu;
	
	if (state->identify_requested)
	{
		if (!timer_isset(&state->identify_started))
			hashbusterusb_set_colour(cgpu, 0xff, 0, 0xff);
		timer_set_delay_from_now(&state->identify_started, 5000000);
		state->identify_requested = false;
	}
	
	bitfury_do_io(master_thr);
	
	if (timer_passed(&state->identify_started, NULL))
	{
		hashbusterusb_set_colour(cgpu, 0, 0x7e, 0);
		timer_unset(&state->identify_started);
	}
}

static
bool hashbusterusb_get_stats(struct cgpu_info * const cgpu)
{
	bool rv = false;
	struct cgpu_info *proc;
	if (cgpu != cgpu->device)
		return true;
	
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct spi_port * const spi = bitfury->spi;
	struct lowl_usb_endpoint * const h = spi->userp;
	uint8_t buf[0x40] = {'\x04'};
	
	if (hashbusterusb_io(h, buf, buf))
	{
		if (buf[1])
		{
			rv = true;
			for (proc = cgpu; proc; proc = proc->next_proc)
				proc->temp = buf[1];
		}
	}
	
	buf[0] = '\x15';
	if (hashbusterusb_io(h, buf, buf))
	{
		if (!memcmp(buf, "\x15\0", 2))
		{
			rv = true;
			const uint16_t voltage = (buf[3] << 8) | buf[2];
			for (proc = cgpu; proc; proc = proc->next_proc)
			{
				struct hashbusterusb_state * const state = proc->thr[0]->cgpu_data;
				state->voltage = voltage;
			}
		}
	}
	
	return rv;
}

static
void hashbusterusb_shutdown(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct spi_port * const spi = bitfury->spi;
	struct lowl_usb_endpoint * const h = spi->userp;
	
	// Shutdown PSU
	unsigned char OUTPacket[64] = { 0x10 };
	unsigned char INPacket[64];
	hashbusterusb_io(h, INPacket, OUTPacket);
}

static
void hashbusterusb_set_colour(struct cgpu_info * const cgpu, const uint8_t red, const uint8_t green, const uint8_t blue)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct spi_port * const spi = bitfury->spi;
	struct lowl_usb_endpoint * const h = spi->userp;
	
	uint8_t buf[0x40] = {'\x30', 0, red, green, blue};
	hashbusterusb_io(h, buf, buf);
	applog(LOG_DEBUG, "%s: Set LED colour to r=0x%x g=0x%x b=0x%x",
	       cgpu->dev_repr, (unsigned)red, (unsigned)green, (unsigned)blue);
}

static
bool hashbusterusb_identify(struct cgpu_info * const proc)
{
	struct hashbusterusb_state * const state = proc->thr[0]->cgpu_data;
	
	state->identify_requested = true;
	
	return true;
}

static
bool hashbusterusb_set_voltage(struct cgpu_info * const proc, const uint16_t nv)
{
	struct bitfury_device * const bitfury = proc->device_data;
	struct spi_port * const spi = bitfury->spi;
	struct lowl_usb_endpoint * const h = spi->userp;
	unsigned char buf[0x40] = {0x11, 0, (nv & 0xff), (nv >> 8)};
	
	hashbusterusb_io(h, buf, buf);
	return !memcmp(buf, "\x11\0", 2);
}

static
bool hashbusterusb_vrm_unlock(struct cgpu_info * const proc, const char * const code)
{
	struct bitfury_device * const bitfury = proc->device_data;
	struct spi_port * const spi = bitfury->spi;
	struct lowl_usb_endpoint * const h = spi->userp;
	unsigned char buf[0x40] = {0x12};
	size_t size;
	
	size = strlen(code) >> 1;
	if (size > 63)
		size = 63;
	
	hex2bin(&buf[1], code, size);
	
	hashbusterusb_io(h, buf, buf);
	return !memcmp(buf, "\x12\0", 2);
}

static
void hashbusterusb_vrm_lock(struct cgpu_info * const proc)
{
	struct bitfury_device * const bitfury = proc->device_data;
	struct spi_port * const spi = bitfury->spi;
	struct lowl_usb_endpoint * const h = spi->userp;
	unsigned char buf[0x40] = {0x14};
	hashbusterusb_io(h, buf, buf);
}

static
struct api_data *hashbusterusb_api_extra_device_stats(struct cgpu_info * const cgpu)
{
	struct hashbusterusb_state * const state = cgpu->thr[0]->cgpu_data;
	struct api_data *root = bitfury_api_device_status(cgpu);
	
	float volts = state->voltage;
	volts /= 1000.;
	root = api_add_volts(root, "Voltage", &volts, true);
	
	return root;
}

static
const char *hashbusterusb_rpcset_vrmlock(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	cgpu_request_control(proc->device);
	hashbusterusb_vrm_lock(proc);
	cgpu_release_control(proc->device);
	return NULL;
}

static
const char *hashbusterusb_rpcset_vrmunlock(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	cgpu_request_control(proc->device);
	const bool rv = hashbusterusb_vrm_unlock(proc, setting);
	cgpu_release_control(proc->device);
	if (!rv)
		return "Unlock error";
	return NULL;
}

static
const char *hashbusterusb_rpcset_voltage(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	const int val = atof(setting) * 1000;
	if (val < 600 || val > 1100)
		return "Invalid PSU voltage value";
	
	cgpu_request_control(proc->device);
	const bool rv = hashbusterusb_set_voltage(proc, val);
	cgpu_release_control(proc->device);
	
	if (!rv)
		return "Voltage change error";
	return NULL;
}

static const struct bfg_set_device_definition hashbusterusb_set_device_funcs[] = {
	{"baud"     , bitfury_set_baud              , "SPI baud rate"},
	{"osc6_bits", bitfury_set_osc6_bits         , "range 1-"BITFURY_MAX_OSC6_BITS_S" (slow to fast)"},
	{"vrmlock"  , hashbusterusb_rpcset_vrmlock  , "Lock the VRM voltage to safe range"},
	{"vrmunlock", hashbusterusb_rpcset_vrmunlock, "Allow setting potentially unsafe voltages (requires unlock code)"},
	{"voltage"  , hashbusterusb_rpcset_voltage  , "Set voltage"},
	{NULL},
};

#ifdef HAVE_CURSES
void hashbusterusb_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	wlogprint("[V]oltage ");
	wlogprint("[O]scillator bits ");
	//wlogprint("[F]an speed ");  // To be implemented
	wlogprint("[U]nlock VRM ");
	wlogprint("[L]ock VRM ");
}

const char *hashbusterusb_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	switch (input)
	{
		case 'v': case 'V':
		{
			const int val = curses_int("Set PSU voltage (range 600mV-1100mV. VRM unlock is required for over 870mV)");
			if (val < 600 || val > 1100)
				return "Invalid PSU voltage value\n";
			
			cgpu_request_control(proc->device);
			const bool rv = hashbusterusb_set_voltage(proc, val);
			cgpu_release_control(proc->device);
			
			if (!rv)
				return "Voltage change error\n";
			
			return "Voltage change successful\n";
		}
		
		case 'u': case 'U':
		{
			char *input = curses_input("VRM unlock code");
			
			if (!input)
				input = calloc(1, 1);
			
			cgpu_request_control(proc->device);
			const bool rv = hashbusterusb_vrm_unlock(proc, input);
			cgpu_release_control(proc->device);
			free(input);
			
			if (!rv)
				return "Unlock error\n";
			
			return "Unlocking PSU\n";
		}
		
		case 'o': case 'O':
			return bitfury_tui_handle_choice(proc, input);
		
		case 'l': case 'L':
		{
			cgpu_request_control(proc->device);
			hashbusterusb_vrm_lock(proc);
			cgpu_release_control(proc->device);
			return "VRM lock\n";
		}
	}
	return NULL;
}

void hashbusterusb_wlogprint_status(struct cgpu_info * const proc)
{
	struct hashbusterusb_state * const state = proc->thr[0]->cgpu_data;
	
	bitfury_wlogprint_status(proc);
	
	wlogprint("PSU voltage: %umV\n", (unsigned)state->voltage);
}
#endif

struct device_drv hashbusterusb_drv = {
	.dname = "hashbusterusb",
	.name = "HBR",
	.lowl_match = hashbusterusb_lowl_match,
	.lowl_probe = hashbusterusb_lowl_probe,
	
	.thread_init = hashbusterusb_init,
	.thread_disable = bitfury_disable,
	.thread_enable = bitfury_enable,
	.thread_shutdown = hashbusterusb_shutdown,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = hashbusterusb_poll,
	.job_process_results = bitfury_job_process_results,
	
	.get_stats = hashbusterusb_get_stats,
	
	.get_api_extra_device_detail = bitfury_api_device_detail,
	.get_api_extra_device_status = hashbusterusb_api_extra_device_stats,
	.identify_device = hashbusterusb_identify,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = hashbusterusb_wlogprint_status,
	.proc_tui_wlogprint_choices = hashbusterusb_tui_wlogprint_choices,
	.proc_tui_handle_choice = hashbusterusb_tui_handle_choice,
#endif
};
