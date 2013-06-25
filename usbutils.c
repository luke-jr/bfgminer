/*
 * Copyright 2012-2013 Andrew Smith
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>

#include "logging.h"
#include "miner.h"
#include "usbutils.h"

#define NODEV(err) ((err) == LIBUSB_ERROR_NO_DEVICE || \
			(err) == LIBUSB_ERROR_PIPE || \
			(err) == LIBUSB_ERROR_OTHER)

#define NOCONTROLDEV(err) ((err) == LIBUSB_ERROR_NO_DEVICE || \
			(err) == LIBUSB_ERROR_OTHER)

			
#ifdef USE_BFLSC
#define DRV_BFLSC 1
#endif

#ifdef USE_BITFORCE
#define DRV_BITFORCE 2
#endif

#ifdef USE_MODMINER
#define DRV_MODMINER 3
#endif

#ifdef USE_ZTEX
#define DRV_ZTEX 4
#endif

#ifdef USE_ICARUS
#define DRV_ICARUS 5
#endif

#ifdef USE_AVALON
#define DRV_AVALON 6
#endif

#define DRV_LAST -1

#define USB_CONFIG 1

#ifdef WIN32
#define BFLSC_TIMEOUT_MS 999
#define BITFORCE_TIMEOUT_MS 999
#define MODMINER_TIMEOUT_MS 999
#define AVALON_TIMEOUT_MS 999
#define ICARUS_TIMEOUT_MS 999
#else
#define BFLSC_TIMEOUT_MS 300
#define BITFORCE_TIMEOUT_MS 200
#define MODMINER_TIMEOUT_MS 100
#define AVALON_TIMEOUT_MS 200
#define ICARUS_TIMEOUT_MS 200
#endif

#define USB_READ_MINPOLL 40

#ifdef USE_BFLSC
// N.B. transfer size is 512 with USB2.0, but only 64 with USB1.1
static struct usb_endpoints bas_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0 }
};
#endif

#ifdef USE_BITFORCE
// N.B. transfer size is 512 with USB2.0, but only 64 with USB1.1
static struct usb_endpoints bfl_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0 }
};
#endif

#ifdef USE_MODMINER
static struct usb_endpoints mmq_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(3), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(3), 0 }
};
#endif

#ifdef USE_AVALON
static struct usb_endpoints ava_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0 }
};
#endif

#ifdef USE_ICARUS
static struct usb_endpoints ica_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(3), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0 }
};
static struct usb_endpoints amu_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(1), 0 }
};
static struct usb_endpoints llt_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0 }
};
static struct usb_endpoints cmr1_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0 }
/*
 Interface 1
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(3), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(4), 0 },

 Interface 2
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(5), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(6), 0 },

 Interface 3
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(7), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(8), 0 }
*/
};
static struct usb_endpoints cmr2_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0 }
};
#endif

#define IDVENDOR_FTDI 0x0403

// TODO: Add support for (at least) Isochronous endpoints
static struct usb_find_devices find_dev[] = {
#ifdef USE_BFLSC
	{
		.drv = DRV_BFLSC,
		.name = "BAS",
		.ident = IDENT_BAS,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		//.iManufacturer = "Butterfly Labs",
		.iProduct = "BitFORCE SHA256 SC",
		.kernel = 0,
		.config = 1,
		.interface = 0,
		.timeout = BFLSC_TIMEOUT_MS,
		.latency = LATENCY_STD,
		.epcount = ARRAY_SIZE(bas_eps),
		.eps = bas_eps },
#endif
#ifdef USE_BITFORCE
	{
		.drv = DRV_BITFORCE,
		.name = "BFL",
		.ident = IDENT_BFL,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		.iManufacturer = "Butterfly Labs Inc.",
		.iProduct = "BitFORCE SHA256",
		.kernel = 0,
		.config = 1,
		.interface = 0,
		.timeout = BITFORCE_TIMEOUT_MS,
		.latency = LATENCY_STD,
		.epcount = ARRAY_SIZE(bfl_eps),
		.eps = bfl_eps },
#endif
#ifdef USE_MODMINER
	{
		.drv = DRV_MODMINER,
		.name = "MMQ",
		.ident = IDENT_MMQ,
		.idVendor = 0x1fc9,
		.idProduct = 0x0003,
		.kernel = 0,
		.config = 1,
		.interface = 1,
		.timeout = MODMINER_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		.epcount = ARRAY_SIZE(mmq_eps),
		.eps = mmq_eps },
#endif
#ifdef USE_AVALON
	{
		.drv = DRV_AVALON,
		.name = "AVA",
		.ident = IDENT_AVA,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6001,
		.kernel = 0,
		.config = 1,
		.interface = 0,
		.timeout = AVALON_TIMEOUT_MS,
		.latency = 10,
		.epcount = ARRAY_SIZE(ava_eps),
		.eps = ava_eps },
#endif
#ifdef USE_ICARUS
	{
		.drv = DRV_ICARUS,
		.name = "ICA",
		.ident = IDENT_ICA,
		.idVendor = 0x067b,
		.idProduct = 0x2303,
		.kernel = 0,
		.config = 1,
		.interface = 0,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		.epcount = ARRAY_SIZE(ica_eps),
		.eps = ica_eps },
	{
		.drv = DRV_ICARUS,
		.name = "AMU",
		.ident = IDENT_AMU,
		.idVendor = 0x10c4,
		.idProduct = 0xea60,
		.kernel = 0,
		.config = 1,
		.interface = 0,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_UNUSED,
		.epcount = ARRAY_SIZE(amu_eps),
		.eps = amu_eps },
	{
		.drv = DRV_ICARUS,
		.name = "BLT",
		.ident = IDENT_BLT,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6001,
		.iProduct = "FT232R USB UART",
		.kernel = 0,
		.config = 1,
		.interface = 0,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_STD,
		.epcount = ARRAY_SIZE(llt_eps),
		.eps = llt_eps },
	// For any that don't match the above "BLT"
	{
		.drv = DRV_ICARUS,
		.name = "LLT",
		.ident = IDENT_LLT,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6001,
		.kernel = 0,
		.config = 1,
		.interface = 0,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_STD,
		.epcount = ARRAY_SIZE(llt_eps),
		.eps = llt_eps },
	{
		.drv = DRV_ICARUS,
		.name = "CMR",
		.ident = IDENT_CMR1,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x8350,
		.iProduct = "Cairnsmore1",
		.kernel = 0,
		.config = 1,
		.interface = 0,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_STD,
		.epcount = ARRAY_SIZE(cmr1_eps),
		.eps = cmr1_eps },
	{
		.drv = DRV_ICARUS,
		.name = "CMR",
		.ident = IDENT_CMR2,
		.idVendor = IDVENDOR_FTDI,
		.idProduct = 0x6014,
		.iProduct = "Cairnsmore1",
		.kernel = 0,
		.config = 1,
		.interface = 0,
		.timeout = ICARUS_TIMEOUT_MS,
		.latency = LATENCY_STD,
		.epcount = ARRAY_SIZE(cmr2_eps),
		.eps = cmr2_eps },
#endif
#ifdef USE_ZTEX
// This is here so cgminer -n shows them
// the ztex driver (as at 201303) doesn't use usbutils
	{
		.drv = DRV_ZTEX,
		.name = "ZTX",
		.ident = IDENT_ZTX,
		.idVendor = 0x221a,
		.idProduct = 0x0100,
		.kernel = 0,
		.config = 1,
		.interface = 1,
		.timeout = 100,
		.latency = LATENCY_UNUSED,
		.epcount = 0,
		.eps = NULL },
#endif
	{ DRV_LAST, NULL, 0, 0, 0, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, NULL }
};

#ifdef USE_BFLSC
extern struct device_drv bflsc_drv;
#endif

#ifdef USE_BITFORCE
extern struct device_drv bitforce_drv;
#endif

#ifdef USE_MODMINER
extern struct device_drv modminer_drv;
#endif

#ifdef USE_ICARUS
extern struct device_drv icarus_drv;
#endif

#ifdef USE_AVALON
extern struct device_drv avalon_drv;
#endif

#define STRBUFLEN 256
static const char *BLANK = "";
static const char *space = " ";
static const char *nodatareturned = "no data returned ";

#define IOERR_CHECK(cgpu, err) \
		if (err == LIBUSB_ERROR_IO) { \
			cgpu->usbinfo.ioerr_count++; \
			cgpu->usbinfo.continuous_ioerr_count++; \
		} else { \
			cgpu->usbinfo.continuous_ioerr_count = 0; \
		}

#if 0 // enable USBDEBUG - only during development testing
 static const char *debug_true_str = "true";
 static const char *debug_false_str = "false";
 static const char *nodevstr = "=NODEV";
 #define bool_str(boo) ((boo) ? debug_true_str : debug_false_str)
 #define isnodev(err) (NODEV(err) ? nodevstr : BLANK)
 #define USBDEBUG(fmt, ...) applog(LOG_WARNING, fmt, ##__VA_ARGS__)
#else
 #define USBDEBUG(fmt, ...)
#endif

// For device limits by driver
static struct driver_count {
	uint32_t count;
	uint32_t limit;
} drv_count[DRIVER_MAX];

// For device limits by list of bus/dev
static struct usb_busdev {
	int bus_number;
	int device_address;
	void *resource1;
	void *resource2;
} *busdev;

static int busdev_count = 0;

// Total device limit
static int total_count = 0;
static int total_limit = 999999;

struct usb_in_use_list {
	struct usb_busdev in_use;
	struct usb_in_use_list *prev;
	struct usb_in_use_list *next;
};

// List of in use devices
static struct usb_in_use_list *in_use_head = NULL;

struct resource_work {
	bool lock;
	const char *dname;
	uint8_t bus_number;
	uint8_t device_address;
	struct resource_work *next;
};

// Pending work for the reslock thread
struct resource_work *res_work_head = NULL;

struct resource_reply {
	uint8_t bus_number;
	uint8_t device_address;
	bool got;
	struct resource_reply *next;
};

// Replies to lock requests
struct resource_reply *res_reply_head = NULL;

// Set this to 0 to remove stats processing
#define DO_USB_STATS 1

static bool stats_initialised = false;

#if DO_USB_STATS

// NONE must be 0 - calloced
#define MODE_NONE 0
#define MODE_CTRL_READ (1 << 0)
#define MODE_CTRL_WRITE (1 << 1)
#define MODE_BULK_READ (1 << 2)
#define MODE_BULK_WRITE (1 << 3)

#define MODE_SEP_STR "+"
#define MODE_NONE_STR "X"
#define MODE_CTRL_READ_STR "cr"
#define MODE_CTRL_WRITE_STR "cw"
#define MODE_BULK_READ_STR "br"
#define MODE_BULK_WRITE_STR "bw"

// One for each CMD, TIMEOUT, ERROR
struct cg_usb_stats_item {
	uint64_t count;
	double total_delay;
	double min_delay;
	double max_delay;
	struct timeval first;
	struct timeval last;
};

#define CMD_CMD 0
#define CMD_TIMEOUT 1
#define CMD_ERROR 2

// One for each C_CMD
struct cg_usb_stats_details {
	int seq;
	uint32_t modes;
	struct cg_usb_stats_item item[CMD_ERROR+1];
};

// One for each device
struct cg_usb_stats {
	char *name;
	int device_id;
	struct cg_usb_stats_details *details;
};

#define SEQ0 0
#define SEQ1 1

static struct cg_usb_stats *usb_stats = NULL;
static int next_stat = USB_NOSTAT;

#define USB_STATS(sgpu, sta, fin, err, mode, cmd, seq) \
		stats(cgpu, sta, fin, err, mode, cmd, seq)
#define STATS_TIMEVAL(tv) cgtime(tv)
#define USB_REJECT(sgpu, mode) rejected_inc(sgpu, mode)
#else
#define USB_STATS(sgpu, sta, fin, err, mode, cmd, seq)
#define STATS_TIMEVAL(tv)
#define USB_REJECT(sgpu, mode)

#endif // DO_USB_STATS

static const char **usb_commands;

static const char *C_REJECTED_S = "RejectedNoDevice";
static const char *C_PING_S = "Ping";
static const char *C_CLEAR_S = "Clear";
static const char *C_REQUESTVERSION_S = "RequestVersion";
static const char *C_GETVERSION_S = "GetVersion";
static const char *C_REQUESTFPGACOUNT_S = "RequestFPGACount";
static const char *C_GETFPGACOUNT_S = "GetFPGACount";
static const char *C_STARTPROGRAM_S = "StartProgram";
static const char *C_STARTPROGRAMSTATUS_S = "StartProgramStatus";
static const char *C_PROGRAM_S = "Program";
static const char *C_PROGRAMSTATUS_S = "ProgramStatus";
static const char *C_PROGRAMSTATUS2_S = "ProgramStatus2";
static const char *C_FINALPROGRAMSTATUS_S = "FinalProgramStatus";
static const char *C_SETCLOCK_S = "SetClock";
static const char *C_REPLYSETCLOCK_S = "ReplySetClock";
static const char *C_REQUESTUSERCODE_S = "RequestUserCode";
static const char *C_GETUSERCODE_S = "GetUserCode";
static const char *C_REQUESTTEMPERATURE_S = "RequestTemperature";
static const char *C_GETTEMPERATURE_S = "GetTemperature";
static const char *C_SENDWORK_S = "SendWork";
static const char *C_SENDWORKSTATUS_S = "SendWorkStatus";
static const char *C_REQUESTWORKSTATUS_S = "RequestWorkStatus";
static const char *C_GETWORKSTATUS_S = "GetWorkStatus";
static const char *C_REQUESTIDENTIFY_S = "RequestIdentify";
static const char *C_GETIDENTIFY_S = "GetIdentify";
static const char *C_REQUESTFLASH_S = "RequestFlash";
static const char *C_REQUESTSENDWORK_S = "RequestSendWork";
static const char *C_REQUESTSENDWORKSTATUS_S = "RequestSendWorkStatus";
static const char *C_RESET_S = "Reset";
static const char *C_SETBAUD_S = "SetBaud";
static const char *C_SETDATA_S = "SetDataCtrl";
static const char *C_SETFLOW_S = "SetFlowCtrl";
static const char *C_SETMODEM_S = "SetModemCtrl";
static const char *C_PURGERX_S = "PurgeRx";
static const char *C_PURGETX_S = "PurgeTx";
static const char *C_FLASHREPLY_S = "FlashReply";
static const char *C_REQUESTDETAILS_S = "RequestDetails";
static const char *C_GETDETAILS_S = "GetDetails";
static const char *C_REQUESTRESULTS_S = "RequestResults";
static const char *C_GETRESULTS_S = "GetResults";
static const char *C_REQUESTQUEJOB_S = "RequestQueJob";
static const char *C_REQUESTQUEJOBSTATUS_S = "RequestQueJobStatus";
static const char *C_QUEJOB_S = "QueJob";
static const char *C_QUEJOBSTATUS_S = "QueJobStatus";
static const char *C_QUEFLUSH_S = "QueFlush";
static const char *C_QUEFLUSHREPLY_S = "QueFlushReply";
static const char *C_REQUESTVOLTS_S = "RequestVolts";
static const char *C_SENDTESTWORK_S = "SendTestWork";
static const char *C_LATENCY_S = "SetLatency";
static const char *C_SETLINE_S = "SetLine";
static const char *C_VENDOR_S = "Vendor";
static const char *C_SETFAN_S = "SetFan";
static const char *C_FANREPLY_S = "GetFan";
static const char *C_AVALON_TASK_S = "AvalonTask";
static const char *C_AVALON_READ_S = "AvalonRead";
static const char *C_GET_AVALON_READY_S = "AvalonReady";
static const char *C_AVALON_RESET_S = "AvalonReset";
static const char *C_GET_AVALON_RESET_S = "GetAvalonReset";
static const char *C_FTDI_STATUS_S = "FTDIStatus";
static const char *C_ENABLE_UART_S = "EnableUART";

#ifdef EOL
#undef EOL
#endif
#define EOL "\n"

static const char *DESDEV = "Device";
static const char *DESCON = "Config";
static const char *DESSTR = "String";
static const char *DESINT = "Interface";
static const char *DESEP = "Endpoint";
static const char *DESHID = "HID";
static const char *DESRPT = "Report";
static const char *DESPHY = "Physical";
static const char *DESHUB = "Hub";

static const char *EPIN = "In: ";
static const char *EPOUT = "Out: ";
static const char *EPX = "?: ";

static const char *CONTROL = "Control";
static const char *ISOCHRONOUS_X = "Isochronous+?";
static const char *ISOCHRONOUS_N_X = "Isochronous+None+?";
static const char *ISOCHRONOUS_N_D = "Isochronous+None+Data";
static const char *ISOCHRONOUS_N_F = "Isochronous+None+Feedback";
static const char *ISOCHRONOUS_N_I = "Isochronous+None+Implicit";
static const char *ISOCHRONOUS_A_X = "Isochronous+Async+?";
static const char *ISOCHRONOUS_A_D = "Isochronous+Async+Data";
static const char *ISOCHRONOUS_A_F = "Isochronous+Async+Feedback";
static const char *ISOCHRONOUS_A_I = "Isochronous+Async+Implicit";
static const char *ISOCHRONOUS_D_X = "Isochronous+Adaptive+?";
static const char *ISOCHRONOUS_D_D = "Isochronous+Adaptive+Data";
static const char *ISOCHRONOUS_D_F = "Isochronous+Adaptive+Feedback";
static const char *ISOCHRONOUS_D_I = "Isochronous+Adaptive+Implicit";
static const char *ISOCHRONOUS_S_X = "Isochronous+Sync+?";
static const char *ISOCHRONOUS_S_D = "Isochronous+Sync+Data";
static const char *ISOCHRONOUS_S_F = "Isochronous+Sync+Feedback";
static const char *ISOCHRONOUS_S_I = "Isochronous+Sync+Implicit";
static const char *BULK = "Bulk";
static const char *INTERRUPT = "Interrupt";
static const char *UNKNOWN = "Unknown";

static const char *err_io_str = " IO Error";
static const char *err_access_str = " Access Denied-a";
static const char *err_timeout_str = " Reply Timeout";
static const char *err_pipe_str = " Access denied-p";
static const char *err_other_str = " Access denied-o";

static const char *usberrstr(int err)
{
	switch (err) {
		case LIBUSB_ERROR_IO:
			return err_io_str;
		case LIBUSB_ERROR_ACCESS:
			return err_access_str;
		case LIBUSB_ERROR_TIMEOUT:
			return err_timeout_str;
		case LIBUSB_ERROR_PIPE:
			return err_pipe_str;
		case LIBUSB_ERROR_OTHER:
			return err_other_str;
	}
	return BLANK;
}

static const char *destype(uint8_t bDescriptorType)
{
	switch (bDescriptorType) {
		case LIBUSB_DT_DEVICE:
			return DESDEV;
		case LIBUSB_DT_CONFIG:
			return DESCON;
		case LIBUSB_DT_STRING:
			return DESSTR;
		case LIBUSB_DT_INTERFACE:
			return DESINT;
		case LIBUSB_DT_ENDPOINT:
			return DESEP;
		case LIBUSB_DT_HID:
			return DESHID;
		case LIBUSB_DT_REPORT:
			return DESRPT;
		case LIBUSB_DT_PHYSICAL:
			return DESPHY;
		case LIBUSB_DT_HUB:
			return DESHUB;
	}
	return UNKNOWN;
}

static const char *epdir(uint8_t bEndpointAddress)
{
	switch (bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) {
		case LIBUSB_ENDPOINT_IN:
			return EPIN;
		case LIBUSB_ENDPOINT_OUT:
			return EPOUT;
	}
	return EPX;
}

static const char *epatt(uint8_t bmAttributes)
{
	switch(bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) {
		case LIBUSB_TRANSFER_TYPE_CONTROL:
			return CONTROL;
		case LIBUSB_TRANSFER_TYPE_BULK:
			return BULK;
		case LIBUSB_TRANSFER_TYPE_INTERRUPT:
			return INTERRUPT;
		case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
			switch(bmAttributes & LIBUSB_ISO_SYNC_TYPE_MASK) {
				case LIBUSB_ISO_SYNC_TYPE_NONE:
					switch(bmAttributes & LIBUSB_ISO_USAGE_TYPE_MASK) {
						case LIBUSB_ISO_USAGE_TYPE_DATA:
							return ISOCHRONOUS_N_D;
						case LIBUSB_ISO_USAGE_TYPE_FEEDBACK:
							return ISOCHRONOUS_N_F;
						case LIBUSB_ISO_USAGE_TYPE_IMPLICIT:
							return ISOCHRONOUS_N_I;
					}
					return ISOCHRONOUS_N_X;
				case LIBUSB_ISO_SYNC_TYPE_ASYNC:
					switch(bmAttributes & LIBUSB_ISO_USAGE_TYPE_MASK) {
						case LIBUSB_ISO_USAGE_TYPE_DATA:
							return ISOCHRONOUS_A_D;
						case LIBUSB_ISO_USAGE_TYPE_FEEDBACK:
							return ISOCHRONOUS_A_F;
						case LIBUSB_ISO_USAGE_TYPE_IMPLICIT:
							return ISOCHRONOUS_A_I;
					}
					return ISOCHRONOUS_A_X;
				case LIBUSB_ISO_SYNC_TYPE_ADAPTIVE:
					switch(bmAttributes & LIBUSB_ISO_USAGE_TYPE_MASK) {
						case LIBUSB_ISO_USAGE_TYPE_DATA:
							return ISOCHRONOUS_D_D;
						case LIBUSB_ISO_USAGE_TYPE_FEEDBACK:
							return ISOCHRONOUS_D_F;
						case LIBUSB_ISO_USAGE_TYPE_IMPLICIT:
							return ISOCHRONOUS_D_I;
					}
					return ISOCHRONOUS_D_X;
				case LIBUSB_ISO_SYNC_TYPE_SYNC:
					switch(bmAttributes & LIBUSB_ISO_USAGE_TYPE_MASK) {
						case LIBUSB_ISO_USAGE_TYPE_DATA:
							return ISOCHRONOUS_S_D;
						case LIBUSB_ISO_USAGE_TYPE_FEEDBACK:
							return ISOCHRONOUS_S_F;
						case LIBUSB_ISO_USAGE_TYPE_IMPLICIT:
							return ISOCHRONOUS_S_I;
					}
					return ISOCHRONOUS_S_X;
			}
			return ISOCHRONOUS_X;
	}

	return UNKNOWN;
}

static void append(char **buf, char *append, size_t *off, size_t *len)
{
	int new = strlen(append);
	if ((new + *off) >= *len)
	{
		*len *= 2;
		*buf = realloc(*buf, *len);
		if (unlikely(!*buf))
			quit(1, "USB failed to realloc append");
	}

	strcpy(*buf + *off, append);
	*off += new;
}

static bool setgetdes(ssize_t count, libusb_device *dev, struct libusb_device_handle *handle, struct libusb_config_descriptor **config, int cd, char **buf, size_t *off, size_t *len)
{
	char tmp[512];
	int err;

	err = libusb_set_configuration(handle, cd);
	if (err) {
		sprintf(tmp, EOL "  ** dev %d: Failed to set config descriptor to %d, err %d",
				(int)count, cd, err);
		append(buf, tmp, off, len);
		return false;
	}

	err = libusb_get_active_config_descriptor(dev, config);
	if (err) {
		sprintf(tmp, EOL "  ** dev %d: Failed to get active config descriptor set to %d, err %d",
				(int)count, cd, err);
		append(buf, tmp, off, len);
		return false;
	}

	sprintf(tmp, EOL "  ** dev %d: Set & Got active config descriptor to %d, err %d",
			(int)count, cd, err);
	append(buf, tmp, off, len);
	return true;
}

static void usb_full(ssize_t *count, libusb_device *dev, char **buf, size_t *off, size_t *len, int level)
{
	struct libusb_device_descriptor desc;
	uint8_t bus_number;
	uint8_t device_address;
	struct libusb_device_handle *handle;
	struct libusb_config_descriptor *config;
	const struct libusb_interface_descriptor *idesc;
	const struct libusb_endpoint_descriptor *epdesc;
	unsigned char man[STRBUFLEN+1];
	unsigned char prod[STRBUFLEN+1];
	unsigned char ser[STRBUFLEN+1];
	char tmp[512];
	int err, i, j, k;

	err = libusb_get_device_descriptor(dev, &desc);
	if (opt_usb_list_all && err) {
		sprintf(tmp, EOL ".USB dev %d: Failed to get descriptor, err %d",
					(int)(++(*count)), err);
		append(buf, tmp, off, len);
		return;
	}

	bus_number = libusb_get_bus_number(dev);
	device_address = libusb_get_device_address(dev);

	if (!opt_usb_list_all) {
		bool known = false;

		for (i = 0; find_dev[i].drv != DRV_LAST; i++)
			if ((find_dev[i].idVendor == desc.idVendor) &&
			    (find_dev[i].idProduct == desc.idProduct)) {
				known = true;
				break;
			}

		if (!known)
			return;
	}

	(*count)++;

	if (level == 0) {
		sprintf(tmp, EOL ".USB dev %d: Bus %d Device %d ID: %04x:%04x",
				(int)(*count), (int)bus_number, (int)device_address,
				desc.idVendor, desc.idProduct);
	} else {
		sprintf(tmp, EOL ".USB dev %d: Bus %d Device %d Device Descriptor:" EOL "\tLength: %d" EOL
			"\tDescriptor Type: %s" EOL "\tUSB: %04x" EOL "\tDeviceClass: %d" EOL
			"\tDeviceSubClass: %d" EOL "\tDeviceProtocol: %d" EOL "\tMaxPacketSize0: %d" EOL
			"\tidVendor: %04x" EOL "\tidProduct: %04x" EOL "\tDeviceRelease: %x" EOL
			"\tNumConfigurations: %d",
				(int)(*count), (int)bus_number, (int)device_address,
				(int)(desc.bLength), destype(desc.bDescriptorType),
				desc.bcdUSB, (int)(desc.bDeviceClass), (int)(desc.bDeviceSubClass),
				(int)(desc.bDeviceProtocol), (int)(desc.bMaxPacketSize0),
				desc.idVendor, desc.idProduct, desc.bcdDevice,
				(int)(desc.bNumConfigurations));
	}
	append(buf, tmp, off, len);

	err = libusb_open(dev, &handle);
	if (err) {
		sprintf(tmp, EOL "  ** dev %d: Failed to open, err %d", (int)(*count), err);
		append(buf, tmp, off, len);
		return;
	}

	err = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, man, STRBUFLEN);
	if (err < 0)
		sprintf((char *)man, "** err(%d)%s", err, usberrstr(err));

	err = libusb_get_string_descriptor_ascii(handle, desc.iProduct, prod, STRBUFLEN);
	if (err < 0)
		sprintf((char *)prod, "** err(%d)%s", err, usberrstr(err));

	if (level == 0) {
		libusb_close(handle);
		sprintf(tmp, EOL "  Manufacturer: '%s'" EOL "  Product: '%s'", man, prod);
		append(buf, tmp, off, len);
		return;
	}

	if (libusb_kernel_driver_active(handle, 0) == 1) {
		sprintf(tmp, EOL "   * dev %d: kernel attached", (int)(*count));
		append(buf, tmp, off, len);
	}

	err = libusb_get_active_config_descriptor(dev, &config);
	if (err) {
		if (!setgetdes(*count, dev, handle, &config, 1, buf, off, len)
		&&  !setgetdes(*count, dev, handle, &config, 0, buf, off, len)) {
			libusb_close(handle);
			sprintf(tmp, EOL "  ** dev %d: Failed to set config descriptor to %d or %d",
					(int)(*count), 1, 0);
			append(buf, tmp, off, len);
			return;
		}
	}

	sprintf(tmp, EOL "     dev %d: Active Config:" EOL "\tDescriptorType: %s" EOL
			"\tNumInterfaces: %d" EOL "\tConfigurationValue: %d" EOL
			"\tAttributes: %d" EOL "\tMaxPower: %d",
				(int)(*count), destype(config->bDescriptorType),
				(int)(config->bNumInterfaces), (int)(config->iConfiguration),
				(int)(config->bmAttributes), (int)(config->MaxPower));
	append(buf, tmp, off, len);

	for (i = 0; i < (int)(config->bNumInterfaces); i++) {
		for (j = 0; j < config->interface[i].num_altsetting; j++) {
			idesc = &(config->interface[i].altsetting[j]);

			sprintf(tmp, EOL "     _dev %d: Interface Descriptor %d:" EOL
					"\tDescriptorType: %s" EOL "\tInterfaceNumber: %d" EOL
					"\tNumEndpoints: %d" EOL "\tInterfaceClass: %d" EOL
					"\tInterfaceSubClass: %d" EOL "\tInterfaceProtocol: %d",
						(int)(*count), j, destype(idesc->bDescriptorType),
						(int)(idesc->bInterfaceNumber),
						(int)(idesc->bNumEndpoints),
						(int)(idesc->bInterfaceClass),
						(int)(idesc->bInterfaceSubClass),
						(int)(idesc->bInterfaceProtocol));
			append(buf, tmp, off, len);

			for (k = 0; k < (int)(idesc->bNumEndpoints); k++) {
				epdesc = &(idesc->endpoint[k]);

				sprintf(tmp, EOL "     __dev %d: Interface %d Endpoint %d:" EOL
						"\tDescriptorType: %s" EOL
						"\tEndpointAddress: %s0x%x" EOL
						"\tAttributes: %s" EOL "\tMaxPacketSize: %d" EOL
						"\tInterval: %d" EOL "\tRefresh: %d",
							(int)(*count), (int)(idesc->bInterfaceNumber), k,
							destype(epdesc->bDescriptorType),
							epdir(epdesc->bEndpointAddress),
							(int)(epdesc->bEndpointAddress),
							epatt(epdesc->bmAttributes),
							epdesc->wMaxPacketSize,
							(int)(epdesc->bInterval),
							(int)(epdesc->bRefresh));
				append(buf, tmp, off, len);
			}
		}
	}

	libusb_free_config_descriptor(config);
	config = NULL;

	err = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, ser, STRBUFLEN);
	if (err < 0)
		sprintf((char *)ser, "** err(%d)%s", err, usberrstr(err));

	sprintf(tmp, EOL "     dev %d: More Info:" EOL "\tManufacturer: '%s'" EOL
			"\tProduct: '%s'" EOL "\tSerial '%s'",
				(int)(*count), man, prod, ser);
	append(buf, tmp, off, len);

	libusb_close(handle);
}

// Function to dump all USB devices
void usb_all(int level)
{
	libusb_device **list;
	ssize_t count, i, j;
	char *buf;
	size_t len, off;

	count = libusb_get_device_list(NULL, &list);
	if (count < 0) {
		applog(LOG_ERR, "USB all: failed, err %d%s", (int)count, usberrstr((int)count));
		return;
	}

	if (count == 0)
		applog(LOG_WARNING, "USB all: found no devices");
	else
	{
		len = 10000;
		buf = malloc(len+1);
		if (unlikely(!buf))
			quit(1, "USB failed to malloc buf in usb_all");

		sprintf(buf, "USB all: found %d devices", (int)count);
		off = strlen(buf);

		if (!opt_usb_list_all)
			append(&buf, " - listing known devices", &off, &len);

		j = -1;
		for (i = 0; i < count; i++)
			usb_full(&j, list[i], &buf, &off, &len, level);

		_applog(LOG_WARNING, buf);

		free(buf);

		if (j == -1)
			applog(LOG_WARNING, "No known USB devices");
		else
			applog(LOG_WARNING, "%d %sUSB devices",
				(int)(++j), opt_usb_list_all ? BLANK : "known ");

	}

	libusb_free_device_list(list, 1);
}

static void cgusb_check_init()
{
	mutex_lock(&cgusb_lock);

	if (stats_initialised == false) {
		// N.B. environment LIBUSB_DEBUG also sets libusb_set_debug()
		if (opt_usbdump >= 0) {
			libusb_set_debug(NULL, opt_usbdump);
			usb_all(opt_usbdump);
		}

		usb_commands = malloc(sizeof(*usb_commands) * C_MAX);
		if (unlikely(!usb_commands))
			quit(1, "USB failed to malloc usb_commands");

		// use constants so the stat generation is very quick
		// and the association between number and name can't
		// be missalined easily
		usb_commands[C_REJECTED] = C_REJECTED_S;
		usb_commands[C_PING] = C_PING_S;
		usb_commands[C_CLEAR] = C_CLEAR_S;
		usb_commands[C_REQUESTVERSION] = C_REQUESTVERSION_S;
		usb_commands[C_GETVERSION] = C_GETVERSION_S;
		usb_commands[C_REQUESTFPGACOUNT] = C_REQUESTFPGACOUNT_S;
		usb_commands[C_GETFPGACOUNT] = C_GETFPGACOUNT_S;
		usb_commands[C_STARTPROGRAM] = C_STARTPROGRAM_S;
		usb_commands[C_STARTPROGRAMSTATUS] = C_STARTPROGRAMSTATUS_S;
		usb_commands[C_PROGRAM] = C_PROGRAM_S;
		usb_commands[C_PROGRAMSTATUS] = C_PROGRAMSTATUS_S;
		usb_commands[C_PROGRAMSTATUS2] = C_PROGRAMSTATUS2_S;
		usb_commands[C_FINALPROGRAMSTATUS] = C_FINALPROGRAMSTATUS_S;
		usb_commands[C_SETCLOCK] = C_SETCLOCK_S;
		usb_commands[C_REPLYSETCLOCK] = C_REPLYSETCLOCK_S;
		usb_commands[C_REQUESTUSERCODE] = C_REQUESTUSERCODE_S;
		usb_commands[C_GETUSERCODE] = C_GETUSERCODE_S;
		usb_commands[C_REQUESTTEMPERATURE] = C_REQUESTTEMPERATURE_S;
		usb_commands[C_GETTEMPERATURE] = C_GETTEMPERATURE_S;
		usb_commands[C_SENDWORK] = C_SENDWORK_S;
		usb_commands[C_SENDWORKSTATUS] = C_SENDWORKSTATUS_S;
		usb_commands[C_REQUESTWORKSTATUS] = C_REQUESTWORKSTATUS_S;
		usb_commands[C_GETWORKSTATUS] = C_GETWORKSTATUS_S;
		usb_commands[C_REQUESTIDENTIFY] = C_REQUESTIDENTIFY_S;
		usb_commands[C_GETIDENTIFY] = C_GETIDENTIFY_S;
		usb_commands[C_REQUESTFLASH] = C_REQUESTFLASH_S;
		usb_commands[C_REQUESTSENDWORK] = C_REQUESTSENDWORK_S;
		usb_commands[C_REQUESTSENDWORKSTATUS] = C_REQUESTSENDWORKSTATUS_S;
		usb_commands[C_RESET] = C_RESET_S;
		usb_commands[C_SETBAUD] = C_SETBAUD_S;
		usb_commands[C_SETDATA] = C_SETDATA_S;
		usb_commands[C_SETFLOW] = C_SETFLOW_S;
		usb_commands[C_SETMODEM] = C_SETMODEM_S;
		usb_commands[C_PURGERX] = C_PURGERX_S;
		usb_commands[C_PURGETX] = C_PURGETX_S;
		usb_commands[C_FLASHREPLY] = C_FLASHREPLY_S;
		usb_commands[C_REQUESTDETAILS] = C_REQUESTDETAILS_S;
		usb_commands[C_GETDETAILS] = C_GETDETAILS_S;
		usb_commands[C_REQUESTRESULTS] = C_REQUESTRESULTS_S;
		usb_commands[C_GETRESULTS] = C_GETRESULTS_S;
		usb_commands[C_REQUESTQUEJOB] = C_REQUESTQUEJOB_S;
		usb_commands[C_REQUESTQUEJOBSTATUS] = C_REQUESTQUEJOBSTATUS_S;
		usb_commands[C_QUEJOB] = C_QUEJOB_S;
		usb_commands[C_QUEJOBSTATUS] = C_QUEJOBSTATUS_S;
		usb_commands[C_QUEFLUSH] = C_QUEFLUSH_S;
		usb_commands[C_QUEFLUSHREPLY] = C_QUEFLUSHREPLY_S;
		usb_commands[C_REQUESTVOLTS] = C_REQUESTVOLTS_S;
		usb_commands[C_SENDTESTWORK] = C_SENDTESTWORK_S;
		usb_commands[C_LATENCY] = C_LATENCY_S;
		usb_commands[C_SETLINE] = C_SETLINE_S;
		usb_commands[C_VENDOR] = C_VENDOR_S;
		usb_commands[C_SETFAN] = C_SETFAN_S;
		usb_commands[C_FANREPLY] = C_FANREPLY_S;
		usb_commands[C_AVALON_TASK] = C_AVALON_TASK_S;
		usb_commands[C_AVALON_READ] = C_AVALON_READ_S;
		usb_commands[C_GET_AVALON_READY] = C_GET_AVALON_READY_S;
		usb_commands[C_AVALON_RESET] = C_AVALON_RESET_S;
		usb_commands[C_GET_AVALON_RESET] = C_GET_AVALON_RESET_S;
		usb_commands[C_FTDI_STATUS] = C_FTDI_STATUS_S;
		usb_commands[C_ENABLE_UART] = C_ENABLE_UART_S;

		stats_initialised = true;
	}

	mutex_unlock(&cgusb_lock);
}

const char *usb_cmdname(enum usb_cmds cmd)
{
	cgusb_check_init();

	return usb_commands[cmd];
}

void usb_applog(struct cgpu_info *cgpu, enum usb_cmds cmd, char *msg, int amount, int err)
{
	if (msg && !*msg)
		msg = NULL;

	if (!msg && amount == 0 && err == LIBUSB_SUCCESS)
		msg = (char *)nodatareturned;

        applog(LOG_ERR, "%s%i: %s failed%s%s (err=%d amt=%d)",
                        cgpu->drv->name, cgpu->device_id,
                        usb_cmdname(cmd),
                        msg ? space : BLANK, msg ? msg : BLANK,
                        err, amount);
}

static void in_use_store_ress(uint8_t bus_number, uint8_t device_address, void *resource1, void *resource2)
{
	struct usb_in_use_list *in_use_tmp;
	bool found = false, empty = true;

	mutex_lock(&cgusb_lock);
	in_use_tmp = in_use_head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
			in_use_tmp->in_use.device_address == (int)device_address) {
			found = true;

			if (in_use_tmp->in_use.resource1)
				empty = false;
			in_use_tmp->in_use.resource1 = resource1;

			if (in_use_tmp->in_use.resource2)
				empty = false;
			in_use_tmp->in_use.resource2 = resource2;

			break;
		}
		in_use_tmp = in_use_tmp->next;
	}
	mutex_unlock(&cgusb_lock);

	if (found == false)
		applog(LOG_ERR, "FAIL: USB store_ress not found (%d:%d)",
				(int)bus_number, (int)device_address);

	if (empty == false)
		applog(LOG_ERR, "FAIL: USB store_ress not empty (%d:%d)",
				(int)bus_number, (int)device_address);
}

static void in_use_get_ress(uint8_t bus_number, uint8_t device_address, void **resource1, void **resource2)
{
	struct usb_in_use_list *in_use_tmp;
	bool found = false, empty = false;

	mutex_lock(&cgusb_lock);
	in_use_tmp = in_use_head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
			in_use_tmp->in_use.device_address == (int)device_address) {
			found = true;

			if (!in_use_tmp->in_use.resource1)
				empty = true;
			*resource1 = in_use_tmp->in_use.resource1;
			in_use_tmp->in_use.resource1 = NULL;

			if (!in_use_tmp->in_use.resource2)
				empty = true;
			*resource2 = in_use_tmp->in_use.resource2;
			in_use_tmp->in_use.resource2 = NULL;

			break;
		}
		in_use_tmp = in_use_tmp->next;
	}
	mutex_unlock(&cgusb_lock);

	if (found == false)
		applog(LOG_ERR, "FAIL: USB get_lock not found (%d:%d)",
				(int)bus_number, (int)device_address);

	if (empty == true)
		applog(LOG_ERR, "FAIL: USB get_lock empty (%d:%d)",
				(int)bus_number, (int)device_address);
}

static bool __is_in_use(uint8_t bus_number, uint8_t device_address)
{
	struct usb_in_use_list *in_use_tmp;
	bool ret = false;

	in_use_tmp = in_use_head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
		    in_use_tmp->in_use.device_address == (int)device_address) {
			ret = true;
			break;
		}
		in_use_tmp = in_use_tmp->next;
	}

	return ret;
}

static bool is_in_use_bd(uint8_t bus_number, uint8_t device_address)
{
	bool ret;

	mutex_lock(&cgusb_lock);
	ret = __is_in_use(bus_number, device_address);
	mutex_unlock(&cgusb_lock);
	return ret;
}

static bool is_in_use(libusb_device *dev)
{
	return is_in_use_bd(libusb_get_bus_number(dev), libusb_get_device_address(dev));
}

static void add_in_use(uint8_t bus_number, uint8_t device_address)
{
	struct usb_in_use_list *in_use_tmp;
	bool found = false;

	mutex_lock(&cgusb_lock);
	if (unlikely(__is_in_use(bus_number, device_address))) {
		found = true;
		goto nofway;
	}

	in_use_tmp = calloc(1, sizeof(*in_use_tmp));
	if (unlikely(!in_use_tmp))
		quit(1, "USB failed to calloc in_use_tmp");
	in_use_tmp->in_use.bus_number = (int)bus_number;
	in_use_tmp->in_use.device_address = (int)device_address;
	in_use_tmp->next = in_use_head;
	if (in_use_head)
		in_use_head->prev = in_use_tmp;
	in_use_head = in_use_tmp;
nofway:
	mutex_unlock(&cgusb_lock);

	if (found)
		applog(LOG_ERR, "FAIL: USB add already in use (%d:%d)",
				(int)bus_number, (int)device_address);
}

static void remove_in_use(uint8_t bus_number, uint8_t device_address)
{
	struct usb_in_use_list *in_use_tmp;
	bool found = false;

	mutex_lock(&cgusb_lock);

	in_use_tmp = in_use_head;
	while (in_use_tmp) {
		if (in_use_tmp->in_use.bus_number == (int)bus_number &&
		    in_use_tmp->in_use.device_address == (int)device_address) {
			found = true;
			if (in_use_tmp == in_use_head) {
				in_use_head = in_use_head->next;
				if (in_use_head)
					in_use_head->prev = NULL;
			} else {
				in_use_tmp->prev->next = in_use_tmp->next;
				if (in_use_tmp->next)
					in_use_tmp->next->prev = in_use_tmp->prev;
			}
			free(in_use_tmp);
			break;
		}
		in_use_tmp = in_use_tmp->next;
	}

	mutex_unlock(&cgusb_lock);

	if (!found)
		applog(LOG_ERR, "FAIL: USB remove not already in use (%d:%d)",
				(int)bus_number, (int)device_address);
}

static bool cgminer_usb_lock_bd(struct device_drv *drv, uint8_t bus_number, uint8_t device_address)
{
	struct resource_work *res_work;
	bool ret;

	applog(LOG_DEBUG, "USB lock %s %d-%d", drv->dname, (int)bus_number, (int)device_address);

	res_work = calloc(1, sizeof(*res_work));
	if (unlikely(!res_work))
		quit(1, "USB failed to calloc lock res_work");
	res_work->lock = true;
	res_work->dname = (const char *)(drv->dname);
	res_work->bus_number = bus_number;
	res_work->device_address = device_address;

	mutex_lock(&cgusbres_lock);
	res_work->next = res_work_head;
	res_work_head = res_work;
	mutex_unlock(&cgusbres_lock);

	cgsem_post(&usb_resource_sem);

	// TODO: add a timeout fail - restart the resource thread?
	while (true) {
		nmsleep(50);

		mutex_lock(&cgusbres_lock);
		if (res_reply_head) {
			struct resource_reply *res_reply_prev = NULL;
			struct resource_reply *res_reply = res_reply_head;
			while (res_reply) {
				if (res_reply->bus_number == bus_number &&
					res_reply->device_address == device_address) {

					if (res_reply_prev)
						res_reply_prev->next = res_reply->next;
					else
						res_reply_head = res_reply->next;

					mutex_unlock(&cgusbres_lock);

					ret = res_reply->got;

					free(res_reply);

					return ret;
				}
				res_reply_prev = res_reply;
				res_reply = res_reply->next;
			}
		}
		mutex_unlock(&cgusbres_lock);
	}
}

static bool cgminer_usb_lock(struct device_drv *drv, libusb_device *dev)
{
	return cgminer_usb_lock_bd(drv, libusb_get_bus_number(dev), libusb_get_device_address(dev));
}

static void cgminer_usb_unlock_bd(struct device_drv *drv, uint8_t bus_number, uint8_t device_address)
{
	struct resource_work *res_work;

	applog(LOG_DEBUG, "USB unlock %s %d-%d", drv->dname, (int)bus_number, (int)device_address);

	res_work = calloc(1, sizeof(*res_work));
	if (unlikely(!res_work))
		quit(1, "USB failed to calloc unlock res_work");
	res_work->lock = false;
	res_work->dname = (const char *)(drv->dname);
	res_work->bus_number = bus_number;
	res_work->device_address = device_address;

	mutex_lock(&cgusbres_lock);
	res_work->next = res_work_head;
	res_work_head = res_work;
	mutex_unlock(&cgusbres_lock);

	cgsem_post(&usb_resource_sem);

	return;
}

static void cgminer_usb_unlock(struct device_drv *drv, libusb_device *dev)
{
	cgminer_usb_unlock_bd(drv, libusb_get_bus_number(dev), libusb_get_device_address(dev));
}

static struct cg_usb_device *free_cgusb(struct cg_usb_device *cgusb)
{
	applog(LOG_DEBUG, "USB free %s", cgusb->found->name);

	if (cgusb->serial_string && cgusb->serial_string != BLANK)
		free(cgusb->serial_string);

	if (cgusb->manuf_string && cgusb->manuf_string != BLANK)
		free(cgusb->manuf_string);

	if (cgusb->prod_string && cgusb->prod_string != BLANK)
		free(cgusb->prod_string);

	free(cgusb->descriptor);

	free(cgusb->found);

	if (cgusb->buffer)
		free(cgusb->buffer);

	free(cgusb);

	return NULL;
}

void usb_uninit(struct cgpu_info *cgpu)
{
	applog(LOG_DEBUG, "USB uninit %s%i",
			cgpu->drv->name, cgpu->device_id);

	// May have happened already during a failed initialisation
	//  if release_cgpu() was called due to a USB NODEV(err)
	if (!cgpu->usbdev)
		return;
	if (!libusb_release_interface(cgpu->usbdev->handle, cgpu->usbdev->found->interface)) {
		cg_wlock(&cgusb_fd_lock);
		libusb_close(cgpu->usbdev->handle);
		cg_wunlock(&cgusb_fd_lock);
	}
	cgpu->usbdev = free_cgusb(cgpu->usbdev);
}

/*
 * N.B. this is always called inside
 *	DEVLOCK(cgpu, pstate);
 */
static void release_cgpu(struct cgpu_info *cgpu)
{
	struct cg_usb_device *cgusb = cgpu->usbdev;
	struct cgpu_info *lookcgpu;
	int i;

	applog(LOG_DEBUG, "USB release %s%i",
			cgpu->drv->name, cgpu->device_id);

	// It has already been done
	if (cgpu->usbinfo.nodev)
		return;

	zombie_devs++;
	total_count--;
	drv_count[cgpu->drv->drv_id].count--;

	cgpu->usbinfo.nodev = true;
	cgpu->usbinfo.nodev_count++;
	cgtime(&cgpu->usbinfo.last_nodev);

	// Any devices sharing the same USB device should be marked also
	// Currently only MMQ shares a USB device
	for (i = 0; i < total_devices; i++) {
		lookcgpu = get_devices(i);
		if (lookcgpu != cgpu && lookcgpu->usbdev == cgusb) {
			total_count--;
			drv_count[lookcgpu->drv->drv_id].count--;

			lookcgpu->usbinfo.nodev = true;
			lookcgpu->usbinfo.nodev_count++;
			memcpy(&(lookcgpu->usbinfo.last_nodev),
				&(cgpu->usbinfo.last_nodev), sizeof(struct timeval));
			lookcgpu->usbdev = NULL;
		}
	}

	usb_uninit(cgpu);

	cgminer_usb_unlock_bd(cgpu->drv, cgpu->usbinfo.bus_number, cgpu->usbinfo.device_address);
}

// Currently only used by MMQ
struct cgpu_info *usb_copy_cgpu(struct cgpu_info *orig)
{
	struct cgpu_info *copy = calloc(1, sizeof(*copy));

	if (unlikely(!copy))
		quit(1, "Failed to calloc cgpu for %s in usb_copy_cgpu", orig->drv->dname);

	copy->name = orig->name;
	copy->drv = copy_drv(orig->drv);
	copy->deven = orig->deven;
	copy->threads = orig->threads;

	copy->usbdev = orig->usbdev;

	memcpy(&(copy->usbinfo), &(orig->usbinfo), sizeof(copy->usbinfo));

	copy->usbinfo.nodev = (copy->usbdev == NULL);

	copy->usbinfo.devlock = orig->usbinfo.devlock;

	return copy;
}

struct cgpu_info *usb_alloc_cgpu(struct device_drv *drv, int threads)
{
	struct cgpu_info *cgpu = calloc(1, sizeof(*cgpu));

	if (unlikely(!cgpu))
		quit(1, "Failed to calloc cgpu for %s in usb_alloc_cgpu", drv->dname);

	cgpu->drv = drv;
	cgpu->deven = DEV_ENABLED;
	cgpu->threads = threads;

	cgpu->usbinfo.nodev = true;

	cgpu->usbinfo.devlock = calloc(1, sizeof(*(cgpu->usbinfo.devlock)));
	if (unlikely(!cgpu->usbinfo.devlock))
		quit(1, "Failed to calloc devlock for %s in usb_alloc_cgpu", drv->dname);

	rwlock_init(cgpu->usbinfo.devlock);

	return cgpu;
}

struct cgpu_info *usb_free_cgpu_devlock(struct cgpu_info *cgpu, bool free_devlock)
{
	if (cgpu->drv->copy)
		free(cgpu->drv);

	free(cgpu->device_path);

	if (free_devlock)
		free(cgpu->usbinfo.devlock);

	free(cgpu);

	return NULL;
}

#define USB_INIT_FAIL 0
#define USB_INIT_OK 1
#define USB_INIT_IGNORE 2

/*
 * WARNING - these assume DEVLOCK(cgpu, pstate) is called first and
 *  DEVUNLOCK(cgpu, pstate) in called in the same function with the same pstate
 *  given to DEVLOCK.
 *  You must call DEVUNLOCK(cgpu, pstate) before exiting the function or it will leave
 *  the thread Cancelability unrestored
 */
#define DEVLOCK(cgpu, _pth_state) do { \
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &_pth_state); \
			wr_lock(cgpu->usbinfo.devlock); \
			} while (0)

#define DEVUNLOCK(cgpu, _pth_state) do { \
			wr_unlock(cgpu->usbinfo.devlock); \
			pthread_setcancelstate(_pth_state, NULL); \
			} while (0)

static int _usb_init(struct cgpu_info *cgpu, struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cg_usb_device *cgusb = NULL;
	struct libusb_config_descriptor *config = NULL;
	const struct libusb_interface_descriptor *idesc;
	const struct libusb_endpoint_descriptor *epdesc;
	unsigned char strbuf[STRBUFLEN+1];
	char devpath[32];
	char devstr[STRBUFLEN+1];
	int err, i, j, k, pstate;
	int bad = USB_INIT_FAIL;

	DEVLOCK(cgpu, pstate);

	cgpu->usbinfo.bus_number = libusb_get_bus_number(dev);
	cgpu->usbinfo.device_address = libusb_get_device_address(dev);

	sprintf(devpath, "%d:%d",
		(int)(cgpu->usbinfo.bus_number),
		(int)(cgpu->usbinfo.device_address));

	cgpu->device_path = strdup(devpath);

	sprintf(devstr, "- %s device %s", found->name, devpath);

	cgusb = calloc(1, sizeof(*cgusb));
	if (unlikely(!cgusb))
		quit(1, "USB failed to calloc _usb_init cgusb");
	cgusb->found = found;

	if (found->idVendor == IDVENDOR_FTDI)
		cgusb->usb_type = USB_TYPE_FTDI;

	cgusb->ident = found->ident;

	cgusb->descriptor = calloc(1, sizeof(*(cgusb->descriptor)));
	if (unlikely(!cgusb->descriptor))
		quit(1, "USB failed to calloc _usb_init cgusb descriptor");

	err = libusb_get_device_descriptor(dev, cgusb->descriptor);
	if (err) {
		applog(LOG_DEBUG,
			"USB init failed to get descriptor, err %d %s",
			err, devstr);
		goto dame;
	}

	cg_wlock(&cgusb_fd_lock);
	err = libusb_open(dev, &(cgusb->handle));
	cg_wunlock(&cgusb_fd_lock);
	if (err) {
		switch (err) {
			case LIBUSB_ERROR_ACCESS:
				applog(LOG_ERR,
					"USB init open device failed, err %d, "
					"you dont have priviledge to access %s",
					err, devstr);
				break;
#ifdef WIN32
			// Windows specific message
			case LIBUSB_ERROR_NOT_SUPPORTED:
				applog(LOG_ERR,
					"USB init, open device failed, err %d, "
					"you need to install a WinUSB driver for %s",
					err, devstr);
				break;
#endif
			default:
				applog(LOG_DEBUG,
					"USB init, open failed, err %d %s",
					err, devstr);
		}
		goto dame;
	}

#ifndef WIN32
	if (libusb_kernel_driver_active(cgusb->handle, found->kernel) == 1) {
		applog(LOG_DEBUG, "USB init, kernel attached ... %s", devstr);
		err = libusb_detach_kernel_driver(cgusb->handle, found->kernel);
		if (err == 0) {
			applog(LOG_DEBUG,
				"USB init, kernel detached successfully %s",
				devstr);
		} else {
			applog(LOG_WARNING,
				"USB init, kernel detach failed, err %d in use? %s",
				err, devstr);
			goto cldame;
		}
	}
#endif

	if (found->iManufacturer) {
		unsigned char man[STRBUFLEN+1];

		err = libusb_get_string_descriptor_ascii(cgusb->handle,
							 cgusb->descriptor->iManufacturer,
							 man, STRBUFLEN);
		if (err < 0) {
			applog(LOG_DEBUG,
				"USB init, failed to get iManufacturer, err %d %s",
				err, devstr);
			goto cldame;
		}
		if (strcmp((char *)man, found->iManufacturer)) {
			applog(LOG_DEBUG, "USB init, iManufacturer mismatch %s",
			       devstr);
			bad = USB_INIT_IGNORE;
			goto cldame;
		}
	}

	if (found->iProduct) {
		unsigned char prod[STRBUFLEN+1];

		err = libusb_get_string_descriptor_ascii(cgusb->handle,
							 cgusb->descriptor->iProduct,
							 prod, STRBUFLEN);
		if (err < 0) {
			applog(LOG_DEBUG,
				"USB init, failed to get iProduct, err %d %s",
				err, devstr);
			goto cldame;
		}
		if (strcmp((char *)prod, found->iProduct)) {
			applog(LOG_DEBUG, "USB init, iProduct mismatch %s",
			       devstr);
			bad = USB_INIT_IGNORE;
			goto cldame;
		}
	}

	err = libusb_set_configuration(cgusb->handle, found->config);
	if (err) {
		switch(err) {
			case LIBUSB_ERROR_BUSY:
				applog(LOG_WARNING,
					"USB init, set config %d in use %s",
					found->config, devstr);
				break;
			default:
				applog(LOG_DEBUG,
					"USB init, failed to set config to %d, err %d %s",
					found->config, err, devstr);
		}
		goto cldame;
	}

	err = libusb_get_active_config_descriptor(dev, &config);
	if (err) {
		applog(LOG_DEBUG,
			"USB init, failed to get config descriptor, err %d %s",
			err, devstr);
		goto cldame;
	}

	if ((int)(config->bNumInterfaces) <= found->interface) {
		applog(LOG_DEBUG, "USB init bNumInterfaces <= interface %s",
		       devstr);
		goto cldame;
	}

	for (i = 0; i < found->epcount; i++)
		found->eps[i].found = false;

	for (i = 0; i < config->interface[found->interface].num_altsetting; i++) {
		idesc = &(config->interface[found->interface].altsetting[i]);
		for (j = 0; j < (int)(idesc->bNumEndpoints); j++) {
			epdesc = &(idesc->endpoint[j]);
			for (k = 0; k < found->epcount; k++) {
				if (!found->eps[k].found) {
					if (epdesc->bmAttributes == found->eps[k].att
					&&  epdesc->wMaxPacketSize >= found->eps[k].size
					&&  epdesc->bEndpointAddress == found->eps[k].ep) {
						found->eps[k].found = true;
						found->wMaxPacketSize = epdesc->wMaxPacketSize;
						break;
					}
				}
			}
		}
	}

	for (i = 0; i < found->epcount; i++) {
		if (found->eps[i].found == false) {
			applog(LOG_DEBUG, "USB init found == false %s",
			       devstr);
			goto cldame;
		}
	}

	err = libusb_claim_interface(cgusb->handle, found->interface);
	if (err) {
		switch(err) {
			case LIBUSB_ERROR_BUSY:
				applog(LOG_WARNING,
					"USB init, claim interface %d in use %s",
					found->interface, devstr);
				break;
			default:
				applog(LOG_DEBUG,
					"USB init, claim interface %d failed, err %d %s",
					found->interface, err, devstr);
		}
		goto cldame;
	}

	cgusb->usbver = cgusb->descriptor->bcdUSB;

// TODO: allow this with the right version of the libusb include and running library
//	cgusb->speed = libusb_get_device_speed(dev);

	err = libusb_get_string_descriptor_ascii(cgusb->handle,
				cgusb->descriptor->iProduct, strbuf, STRBUFLEN);
	if (err > 0)
		cgusb->prod_string = strdup((char *)strbuf);
	else
		cgusb->prod_string = (char *)BLANK;

	err = libusb_get_string_descriptor_ascii(cgusb->handle,
				cgusb->descriptor->iManufacturer, strbuf, STRBUFLEN);
	if (err > 0)
		cgusb->manuf_string = strdup((char *)strbuf);
	else
		cgusb->manuf_string = (char *)BLANK;

	err = libusb_get_string_descriptor_ascii(cgusb->handle,
				cgusb->descriptor->iSerialNumber, strbuf, STRBUFLEN);
	if (err > 0)
		cgusb->serial_string = strdup((char *)strbuf);
	else
		cgusb->serial_string = (char *)BLANK;

// TODO: ?
//	cgusb->fwVersion <- for temp1/temp2 decision? or serial? (driver-modminer.c)
//	cgusb->interfaceVersion

	applog(LOG_DEBUG,
		"USB init %s usbver=%04x prod='%s' manuf='%s' serial='%s'",
		devstr, cgusb->usbver, cgusb->prod_string,
		cgusb->manuf_string, cgusb->serial_string);

	cgpu->usbdev = cgusb;
	cgpu->usbinfo.nodev = false;

	libusb_free_config_descriptor(config);

	// Allow a name change based on the idVendor+idProduct
	// N.B. must be done before calling add_cgpu()
	if (strcmp(cgpu->drv->name, found->name)) {
		if (!cgpu->drv->copy)
			cgpu->drv = copy_drv(cgpu->drv);
		cgpu->drv->name = (char *)(found->name);
	}

	bad = USB_INIT_OK;
	goto out_unlock;

cldame:

	cg_wlock(&cgusb_fd_lock);
	libusb_close(cgusb->handle);
	cg_wunlock(&cgusb_fd_lock);

dame:

	if (config)
		libusb_free_config_descriptor(config);

	cgusb = free_cgusb(cgusb);

out_unlock:
	DEVUNLOCK(cgpu, pstate);

	return bad;
}

bool usb_init(struct cgpu_info *cgpu, struct libusb_device *dev, struct usb_find_devices *found)
{
	int ret;

	ret = _usb_init(cgpu, dev, found);

	if (ret == USB_INIT_FAIL)
		applog(LOG_ERR, "%s detect (%d:%d) failed to initialise (incorrect device?)",
				cgpu->drv->dname,
				(int)(cgpu->usbinfo.bus_number),
				(int)(cgpu->usbinfo.device_address));

	return (ret == USB_INIT_OK);
}

static bool usb_check_device(struct device_drv *drv, struct libusb_device *dev, struct usb_find_devices *look)
{
	struct libusb_device_descriptor desc;
	int bus_number, device_address;
	int err, i;
	bool ok;

	err = libusb_get_device_descriptor(dev, &desc);
	if (err) {
		applog(LOG_DEBUG, "USB check device: Failed to get descriptor, err %d", err);
		return false;
	}

	if (desc.idVendor != look->idVendor || desc.idProduct != look->idProduct) {
		applog(LOG_DEBUG, "%s looking for %s %04x:%04x but found %04x:%04x instead",
			drv->name, look->name, look->idVendor, look->idProduct, desc.idVendor, desc.idProduct);

		return false;
	}

	if (busdev_count > 0) {
		bus_number = (int)libusb_get_bus_number(dev);
		device_address = (int)libusb_get_device_address(dev);
		ok = false;
		for (i = 0; i < busdev_count; i++) {
			if (bus_number == busdev[i].bus_number) {
				if (busdev[i].device_address == -1 ||
				    device_address == busdev[i].device_address) {
					ok = true;
					break;
				}
			}
		}
		if (!ok) {
			applog(LOG_DEBUG, "%s rejected %s %04x:%04x with bus:dev (%d:%d)",
				drv->name, look->name, look->idVendor, look->idProduct,
				bus_number, device_address);
			return false;
		}
	}

	applog(LOG_DEBUG, "%s looking for and found %s %04x:%04x",
		drv->name, look->name, look->idVendor, look->idProduct);

	return true;
}

static struct usb_find_devices *usb_check_each(int drvnum, struct device_drv *drv, struct libusb_device *dev)
{
	struct usb_find_devices *found;
	int i;

	for (i = 0; find_dev[i].drv != DRV_LAST; i++)
		if (find_dev[i].drv == drvnum) {
			if (usb_check_device(drv, dev, &(find_dev[i]))) {
				found = malloc(sizeof(*found));
				if (unlikely(!found))
					quit(1, "USB failed to malloc found");
				memcpy(found, &(find_dev[i]), sizeof(*found));
				return found;
			}
		}

	return NULL;
}

static struct usb_find_devices *usb_check(__maybe_unused struct device_drv *drv, __maybe_unused struct libusb_device *dev)
{
	if (drv_count[drv->drv_id].count >= drv_count[drv->drv_id].limit) {
		applog(LOG_DEBUG,
			"USB scan devices3: %s limit %d reached",
			drv->dname, drv_count[drv->drv_id].limit);
		return NULL;
	}

#ifdef USE_BFLSC
	if (drv->drv_id == DRIVER_BFLSC)
		return usb_check_each(DRV_BFLSC, drv, dev);
#endif

#ifdef USE_BITFORCE
	if (drv->drv_id == DRIVER_BITFORCE)
		return usb_check_each(DRV_BITFORCE, drv, dev);
#endif

#ifdef USE_MODMINER
	if (drv->drv_id == DRIVER_MODMINER)
		return usb_check_each(DRV_MODMINER, drv, dev);
#endif

#ifdef USE_ICARUS
	if (drv->drv_id == DRIVER_ICARUS)
		return usb_check_each(DRV_ICARUS, drv, dev);
#endif

#ifdef USE_AVALON
	if (drv->drv_id == DRIVER_AVALON)
		return usb_check_each(DRV_AVALON, drv, dev);
#endif

	return NULL;
}

void usb_detect(struct device_drv *drv, bool (*device_detect)(struct libusb_device *, struct usb_find_devices *))
{
	libusb_device **list;
	ssize_t count, i;
	struct usb_find_devices *found;

	applog(LOG_DEBUG, "USB scan devices: checking for %s devices", drv->name);

	if (total_count >= total_limit) {
		applog(LOG_DEBUG, "USB scan devices: total limit %d reached", total_limit);
		return;
	}

	if (drv_count[drv->drv_id].count >= drv_count[drv->drv_id].limit) {
		applog(LOG_DEBUG,
			"USB scan devices: %s limit %d reached",
			drv->dname, drv_count[drv->drv_id].limit);
		return;
	}

	count = libusb_get_device_list(NULL, &list);
	if (count < 0) {
		applog(LOG_DEBUG, "USB scan devices: failed, err %d", (int)count);
		return;
	}

	if (count == 0)
		applog(LOG_DEBUG, "USB scan devices: found no devices");

	for (i = 0; i < count; i++) {
		if (total_count >= total_limit) {
			applog(LOG_DEBUG, "USB scan devices2: total limit %d reached", total_limit);
			break;
		}

		if (drv_count[drv->drv_id].count >= drv_count[drv->drv_id].limit) {
			applog(LOG_DEBUG,
				"USB scan devices2: %s limit %d reached",
				drv->dname, drv_count[drv->drv_id].limit);
			break;
		}

		found = usb_check(drv, list[i]);
		if (found != NULL) {
			if (is_in_use(list[i]) || cgminer_usb_lock(drv, list[i]) == false)
				free(found);
			else {
				if (!device_detect(list[i], found))
					cgminer_usb_unlock(drv, list[i]);
				else {
					total_count++;
					drv_count[drv->drv_id].count++;
				}
			}
		}
	}

	libusb_free_device_list(list, 1);
}

#if DO_USB_STATS
static void modes_str(char *buf, uint32_t modes)
{
	bool first;

	*buf = '\0';

	if (modes == MODE_NONE)
		strcpy(buf, MODE_NONE_STR);
	else {
		first = true;

		if (modes & MODE_CTRL_READ) {
			strcpy(buf, MODE_CTRL_READ_STR);
			first = false;
		}

		if (modes & MODE_CTRL_WRITE) {
			if (!first)
				strcat(buf, MODE_SEP_STR);
			strcat(buf, MODE_CTRL_WRITE_STR);
			first = false;
		}

		if (modes & MODE_BULK_READ) {
			if (!first)
				strcat(buf, MODE_SEP_STR);
			strcat(buf, MODE_BULK_READ_STR);
			first = false;
		}

		if (modes & MODE_BULK_WRITE) {
			if (!first)
				strcat(buf, MODE_SEP_STR);
			strcat(buf, MODE_BULK_WRITE_STR);
			first = false;
		}
	}
}
#endif

// The stat data can be spurious due to not locking it before copying it -
// however that would require the stat() function to also lock and release
// a mutex every time a usb read or write is called which would slow
// things down more
struct api_data *api_usb_stats(__maybe_unused int *count)
{
#if DO_USB_STATS
	struct cg_usb_stats_details *details;
	struct cg_usb_stats *sta;
	struct api_data *root = NULL;
	int device;
	int cmdseq;
	char modes_s[32];

	if (next_stat == USB_NOSTAT)
		return NULL;

	while (*count < next_stat * C_MAX * 2) {
		device = *count / (C_MAX * 2);
		cmdseq = *count % (C_MAX * 2);

		(*count)++;

		sta = &(usb_stats[device]);
		details = &(sta->details[cmdseq]);

		// Only show stats that have results
		if (details->item[CMD_CMD].count == 0 &&
		    details->item[CMD_TIMEOUT].count == 0 &&
		    details->item[CMD_ERROR].count == 0)
			continue;

		root = api_add_string(root, "Name", sta->name, false);
		root = api_add_int(root, "ID", &(sta->device_id), false);
		root = api_add_const(root, "Stat", usb_commands[cmdseq/2], false);
		root = api_add_int(root, "Seq", &(details->seq), true);
		modes_str(modes_s, details->modes);
		root = api_add_string(root, "Modes", modes_s, true);
		root = api_add_uint64(root, "Count",
					&(details->item[CMD_CMD].count), true);
		root = api_add_double(root, "Total Delay",
					&(details->item[CMD_CMD].total_delay), true);
		root = api_add_double(root, "Min Delay",
					&(details->item[CMD_CMD].min_delay), true);
		root = api_add_double(root, "Max Delay",
					&(details->item[CMD_CMD].max_delay), true);
		root = api_add_uint64(root, "Timeout Count",
					&(details->item[CMD_TIMEOUT].count), true);
		root = api_add_double(root, "Timeout Total Delay",
					&(details->item[CMD_TIMEOUT].total_delay), true);
		root = api_add_double(root, "Timeout Min Delay",
					&(details->item[CMD_TIMEOUT].min_delay), true);
		root = api_add_double(root, "Timeout Max Delay",
					&(details->item[CMD_TIMEOUT].max_delay), true);
		root = api_add_uint64(root, "Error Count",
					&(details->item[CMD_ERROR].count), true);
		root = api_add_double(root, "Error Total Delay",
					&(details->item[CMD_ERROR].total_delay), true);
		root = api_add_double(root, "Error Min Delay",
					&(details->item[CMD_ERROR].min_delay), true);
		root = api_add_double(root, "Error Max Delay",
					&(details->item[CMD_ERROR].max_delay), true);
		root = api_add_timeval(root, "First Command",
					&(details->item[CMD_CMD].first), true);
		root = api_add_timeval(root, "Last Command",
					&(details->item[CMD_CMD].last), true);
		root = api_add_timeval(root, "First Timeout",
					&(details->item[CMD_TIMEOUT].first), true);
		root = api_add_timeval(root, "Last Timeout",
					&(details->item[CMD_TIMEOUT].last), true);
		root = api_add_timeval(root, "First Error",
					&(details->item[CMD_ERROR].first), true);
		root = api_add_timeval(root, "Last Error",
					&(details->item[CMD_ERROR].last), true);

		return root;
	}
#endif
	return NULL;
}

#if DO_USB_STATS
static void newstats(struct cgpu_info *cgpu)
{
	int i;

	mutex_lock(&cgusb_lock);

	cgpu->usbinfo.usbstat = next_stat + 1;

	usb_stats = realloc(usb_stats, sizeof(*usb_stats) * (next_stat+1));
	if (unlikely(!usb_stats))
		quit(1, "USB failed to realloc usb_stats %d", next_stat+1);

	usb_stats[next_stat].name = cgpu->drv->name;
	usb_stats[next_stat].device_id = -1;
	usb_stats[next_stat].details = calloc(1, sizeof(struct cg_usb_stats_details) * C_MAX * 2);
	if (unlikely(!usb_stats[next_stat].details))
		quit(1, "USB failed to calloc details for %d", next_stat+1);

	for (i = 1; i < C_MAX * 2; i += 2)
		usb_stats[next_stat].details[i].seq = 1;

	next_stat++;

	mutex_unlock(&cgusb_lock);
}
#endif

void update_usb_stats(__maybe_unused struct cgpu_info *cgpu)
{
#if DO_USB_STATS
	if (cgpu->usbinfo.usbstat < 1)
		newstats(cgpu);

	// we don't know the device_id until after add_cgpu()
	usb_stats[cgpu->usbinfo.usbstat - 1].device_id = cgpu->device_id;
#endif
}

#if DO_USB_STATS
static void stats(struct cgpu_info *cgpu, struct timeval *tv_start, struct timeval *tv_finish, int err, int mode, enum usb_cmds cmd, int seq)
{
	struct cg_usb_stats_details *details;
	double diff;
	int item;

	if (cgpu->usbinfo.usbstat < 1)
		newstats(cgpu);

	details = &(usb_stats[cgpu->usbinfo.usbstat - 1].details[cmd * 2 + seq]);
	details->modes |= mode;

	diff = tdiff(tv_finish, tv_start);

	switch (err) {
		case LIBUSB_SUCCESS:
			item = CMD_CMD;
			break;
		case LIBUSB_ERROR_TIMEOUT:
			item = CMD_TIMEOUT;
			break;
		default:
			item = CMD_ERROR;
			break;
	}

	if (details->item[item].count == 0) {
		details->item[item].min_delay = diff;
		memcpy(&(details->item[item].first), tv_start, sizeof(*tv_start));
	} else if (diff < details->item[item].min_delay)
		details->item[item].min_delay = diff;

	if (diff > details->item[item].max_delay)
		details->item[item].max_delay = diff;

	details->item[item].total_delay += diff;
	memcpy(&(details->item[item].last), tv_start, sizeof(*tv_start));
	details->item[item].count++;
}

static void rejected_inc(struct cgpu_info *cgpu, uint32_t mode)
{
	struct cg_usb_stats_details *details;
	int item = CMD_ERROR;

	if (cgpu->usbinfo.usbstat < 1)
		newstats(cgpu);

	details = &(usb_stats[cgpu->usbinfo.usbstat - 1].details[C_REJECTED * 2 + 0]);
	details->modes |= mode;
	details->item[item].count++;
}
#endif

static char *find_end(unsigned char *buf, unsigned char *ptr, int ptrlen, int tot, char *end, int endlen, bool first)
{
	unsigned char *search;

	if (endlen > tot)
		return NULL;

	// If end is only 1 char - do a faster search
	if (endlen == 1) {
		if (first)
			search = buf;
		else
			search = ptr;

		return strchr((char *)search, *end);
	} else {
		if (first)
			search = buf;
		else {
			// must allow end to have been chopped in 2
			if ((tot - ptrlen) >= (endlen - 1))
				search = ptr - (endlen - 1);
			else
				search = ptr - (tot - ptrlen);
		}

		return strstr((char *)search, end);
	}
}

#define USB_MAX_READ 8192
#define USB_RETRY_MAX 5

static int
usb_bulk_transfer(struct libusb_device_handle *dev_handle,
		  unsigned char endpoint, unsigned char *data, int length,
		  int *transferred, unsigned int timeout,
		  struct cgpu_info *cgpu, enum usb_cmds cmd)
{
	uint16_t MaxPacketSize;
	int err, errn, tries = 0;

	/* Limit length of transfer to the largest this descriptor supports
	 * and leave the higher level functions to transfer more if needed. */
	if (cgpu->usbdev->PrefPacketSize)
		MaxPacketSize = cgpu->usbdev->PrefPacketSize;
	else
		MaxPacketSize = cgpu->usbdev->found->wMaxPacketSize;
	if (length > MaxPacketSize)
		length = MaxPacketSize;

	cg_rlock(&cgusb_fd_lock);
	err = libusb_bulk_transfer(dev_handle, endpoint, data, length,
				   transferred, timeout);
	errn = errno;
	cg_runlock(&cgusb_fd_lock);

	if (err < 0)
		applog(LOG_DEBUG, "%s%i: %s (amt=%d err=%d ern=%d)",
				cgpu->drv->name, cgpu->device_id,
				usb_cmdname(cmd), *transferred, err, errn);

	if (err == LIBUSB_ERROR_PIPE) {
		cgpu->usbinfo.last_pipe = time(NULL);
		cgpu->usbinfo.pipe_count++;
		applog(LOG_INFO, "%s%i: libusb pipe error, trying to clear",
			cgpu->drv->name, cgpu->device_id);
		do {
			err = libusb_clear_halt(dev_handle, endpoint);
			if (unlikely(err == LIBUSB_ERROR_NOT_FOUND ||
				     err == LIBUSB_ERROR_NO_DEVICE)) {
					cgpu->usbinfo.clear_err_count++;
					break;
			}

			cg_rlock(&cgusb_fd_lock);
			err = libusb_bulk_transfer(dev_handle, endpoint, data,
						   length, transferred, timeout);
			errn = errno;
			cg_runlock(&cgusb_fd_lock);

			if (err < 0)
				applog(LOG_DEBUG, "%s%i: %s (amt=%d err=%d ern=%d)",
						cgpu->drv->name, cgpu->device_id,
						usb_cmdname(cmd), *transferred, err, errn);

			if (err)
				cgpu->usbinfo.retry_err_count++;
		} while (err == LIBUSB_ERROR_PIPE && tries++ < USB_RETRY_MAX);
		applog(LOG_DEBUG, "%s%i: libusb pipe error%scleared",
			cgpu->drv->name, cgpu->device_id, err ? " not " : " ");

		if (err)
			cgpu->usbinfo.clear_fail_count++;
	}

	return err;
}

int _usb_read(struct cgpu_info *cgpu, int ep, char *buf, size_t bufsiz, int *processed, unsigned int timeout, const char *end, enum usb_cmds cmd, bool readonce)
{
	struct cg_usb_device *usbdev;
	bool ftdi;
#if DO_USB_STATS
	struct timeval tv_start;
#endif
	struct timeval read_start, tv_finish;
	unsigned int initial_timeout;
	double max, done;
	int bufleft, err, got, tot, pstate;
	bool first = true;
	char *search;
	int endlen;

	// We add 4: 1 for null, 2 for FTDI status and 1 to round to 4 bytes
	unsigned char usbbuf[USB_MAX_READ+4], *ptr;
	size_t usbbufread;

	DEVLOCK(cgpu, pstate);

	if (cgpu->usbinfo.nodev) {
		*buf = '\0';
		*processed = 0;
		USB_REJECT(cgpu, MODE_BULK_READ);

		err = LIBUSB_ERROR_NO_DEVICE;
		goto out_unlock;
	}

	usbdev = cgpu->usbdev;
	ftdi = (usbdev->usb_type == USB_TYPE_FTDI);

	USBDEBUG("USB debug: _usb_read(%s (nodev=%s),ep=%d,buf=%p,bufsiz=%zu,proc=%p,timeout=%u,end=%s,cmd=%s,ftdi=%s,readonce=%s)", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), ep, buf, bufsiz, processed, timeout, end ? (char *)str_text((char *)end) : "NULL", usb_cmdname(cmd), bool_str(ftdi), bool_str(readonce));

	if (bufsiz > USB_MAX_READ)
		quit(1, "%s USB read request %d too large (max=%d)", cgpu->drv->name, (int)bufsiz, USB_MAX_READ);

	if (timeout == DEVTIMEOUT)
		timeout = usbdev->found->timeout;

	if (end == NULL) {
		if (usbdev->buffer && usbdev->bufamt) {
			tot = usbdev->bufamt;
			bufleft = bufsiz - tot;
			memcpy(usbbuf, usbdev->buffer, tot);
			ptr = usbbuf + tot;
			usbdev->bufamt = 0;
		} else {
			tot = 0;
			bufleft = bufsiz;
			ptr = usbbuf;
		}

		err = LIBUSB_SUCCESS;
		initial_timeout = timeout;
		max = ((double)timeout) / 1000.0;
		cgtime(&read_start);
		while (bufleft > 0) {
			// TODO: use (USB_MAX_READ - tot) always?
			if (usbdev->buffer)
				usbbufread = USB_MAX_READ - tot;
			else {
				if (ftdi)
					usbbufread = bufleft + 2;
				else
					usbbufread = bufleft;
			}
			got = 0;

			STATS_TIMEVAL(&tv_start);
			err = usb_bulk_transfer(usbdev->handle,
						usbdev->found->eps[ep].ep,
						ptr, usbbufread, &got, timeout,
						cgpu, cmd);
			cgtime(&tv_finish);
			USB_STATS(cgpu, &tv_start, &tv_finish, err,
					MODE_BULK_READ, cmd, first ? SEQ0 : SEQ1);
			ptr[got] = '\0';

			USBDEBUG("USB debug: @_usb_read(%s (nodev=%s)) first=%s err=%d%s got=%d ptr='%s' usbbufread=%zu", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), bool_str(first), err, isnodev(err), got, (char *)str_text((char *)ptr), usbbufread);

			IOERR_CHECK(cgpu, err);

			if (ftdi) {
				// first 2 bytes returned are an FTDI status
				if (got > 2) {
					got -= 2;
					memmove(ptr, ptr+2, got+1);
				} else {
					got = 0;
					*ptr = '\0';
				}
			}

			tot += got;

			if (err || readonce)
				break;

			ptr += got;
			bufleft -= got;

			first = false;

			done = tdiff(&tv_finish, &read_start);
			// N.B. this is: return LIBUSB_SUCCESS with whatever size has already been read
			if (unlikely(done >= max))
				break;
			timeout = initial_timeout - (done * 1000);
			if (!timeout)
				break;
		}

		// N.B. usbdev->buffer was emptied before the while() loop
		if (usbdev->buffer && tot > (int)bufsiz) {
			usbdev->bufamt = tot - bufsiz;
			memcpy(usbdev->buffer, ptr + bufsiz, usbdev->bufamt);
			tot -= usbdev->bufamt;
			usbbuf[tot] = '\0';
			applog(LOG_ERR, "USB: %s%i read1 buffering %d extra bytes",
					cgpu->drv->name, cgpu->device_id, usbdev->bufamt);
		}

		*processed = tot;
		memcpy((char *)buf, (const char *)usbbuf, (tot < (int)bufsiz) ? tot + 1 : (int)bufsiz);

		if (NODEV(err))
			release_cgpu(cgpu);

		goto out_unlock;
	}

	if (usbdev->buffer && usbdev->bufamt) {
		tot = usbdev->bufamt;
		bufleft = bufsiz - tot;
		memcpy(usbbuf, usbdev->buffer, tot);
		ptr = usbbuf + tot;
		usbdev->bufamt = 0;
	} else {
		tot = 0;
		bufleft = bufsiz;
		ptr = usbbuf;
	}

	endlen = strlen(end);
	err = LIBUSB_SUCCESS;
	initial_timeout = timeout;
	max = ((double)timeout) / 1000.0;
	cgtime(&read_start);
	while (bufleft > 0) {
		// TODO: use (USB_MAX_READ - tot) always?
		if (usbdev->buffer)
			usbbufread = USB_MAX_READ - tot;
		else {
			if (ftdi)
				usbbufread = bufleft + 2;
			else
				usbbufread = bufleft;
		}
		got = 0;
		STATS_TIMEVAL(&tv_start);
		err = usb_bulk_transfer(usbdev->handle,
					usbdev->found->eps[ep].ep, ptr,
					usbbufread, &got, timeout,
					cgpu, cmd);
		cgtime(&tv_finish);
		USB_STATS(cgpu, &tv_start, &tv_finish, err,
				MODE_BULK_READ, cmd, first ? SEQ0 : SEQ1);
		ptr[got] = '\0';

		USBDEBUG("USB debug: @_usb_read(%s (nodev=%s)) first=%s err=%d%s got=%d ptr='%s' usbbufread=%zu", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), bool_str(first), err, isnodev(err), got, (char *)str_text((char *)ptr), usbbufread);

		IOERR_CHECK(cgpu, err);

		if (ftdi) {
			// first 2 bytes returned are an FTDI status
			if (got > 2) {
				got -= 2;
				memmove(ptr, ptr+2, got+1);
			} else {
				got = 0;
				*ptr = '\0';
			}
		}

		tot += got;

		if (err || readonce)
			break;

		if (find_end(usbbuf, ptr, got, tot, (char *)end, endlen, first))
			break;

		ptr += got;
		bufleft -= got;

		first = false;

		done = tdiff(&tv_finish, &read_start);
		// N.B. this is: return LIBUSB_SUCCESS with whatever size has already been read
		if (unlikely(done >= max))
			break;
		timeout = initial_timeout - (done * 1000);
		if (!timeout)
			break;
	}

	if (usbdev->buffer) {
		bool dobuffer = false;

		if ((search = find_end(usbbuf, usbbuf, tot, tot, (char *)end, endlen, true))) {
			// end finishes after bufsiz
			if ((search + endlen - (char *)usbbuf) > (int)bufsiz) {
				usbdev->bufamt = tot - bufsiz;
				dobuffer = true;
			} else {
				// extra data after end
				if (*(search + endlen)) {
					usbdev->bufamt = tot - (search + endlen - (char *)usbbuf);
					dobuffer = true;
				}
			}
		} else {
			// no end, but still bigger than bufsiz
			if (tot > (int)bufsiz) {
				usbdev->bufamt = tot - bufsiz;
				dobuffer = true;
			}
		}

		if (dobuffer) {
			tot -= usbdev->bufamt;
			memcpy(usbdev->buffer, usbbuf + tot, usbdev->bufamt);
			usbbuf[tot] = '\0';
			applog(LOG_ERR, "USB: %s%i read2 buffering %d extra bytes",
					cgpu->drv->name, cgpu->device_id, usbdev->bufamt);
		}
	}

	*processed = tot;
	memcpy((char *)buf, (const char *)usbbuf, (tot < (int)bufsiz) ? tot + 1 : (int)bufsiz);

	if (NODEV(err))
		release_cgpu(cgpu);

out_unlock:
	DEVUNLOCK(cgpu, pstate);

	return err;
}

int _usb_write(struct cgpu_info *cgpu, int ep, char *buf, size_t bufsiz, int *processed, unsigned int timeout, enum usb_cmds cmd)
{
	struct cg_usb_device *usbdev;
#if DO_USB_STATS
	struct timeval tv_start;
#endif
	struct timeval read_start, tv_finish;
	unsigned int initial_timeout;
	double max, done;
	__maybe_unused bool first = true;
	int err, sent, tot, pstate;

	DEVLOCK(cgpu, pstate);

	USBDEBUG("USB debug: _usb_write(%s (nodev=%s),ep=%d,buf='%s',bufsiz=%zu,proc=%p,timeout=%u,cmd=%s)", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), ep, (char *)str_text(buf), bufsiz, processed, timeout, usb_cmdname(cmd));

	*processed = 0;

	if (cgpu->usbinfo.nodev) {
		USB_REJECT(cgpu, MODE_BULK_WRITE);

		err = LIBUSB_ERROR_NO_DEVICE;
		goto out_unlock;
	}

	usbdev = cgpu->usbdev;

	if (timeout == DEVTIMEOUT)
		timeout = usbdev->found->timeout;

	tot = 0;
	err = LIBUSB_SUCCESS;
	initial_timeout = timeout;
	max = ((double)timeout) / 1000.0;
	cgtime(&read_start);
	while (bufsiz > 0) {
		sent = 0;
		STATS_TIMEVAL(&tv_start);
		err = usb_bulk_transfer(usbdev->handle,
					usbdev->found->eps[ep].ep,
					(unsigned char *)buf, bufsiz, &sent,
					timeout, cgpu, cmd);
		cgtime(&tv_finish);
		USB_STATS(cgpu, &tv_start, &tv_finish, err,
				MODE_BULK_WRITE, cmd, first ? SEQ0 : SEQ1);

		USBDEBUG("USB debug: @_usb_write(%s (nodev=%s)) err=%d%s sent=%d", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), err, isnodev(err), sent);

		IOERR_CHECK(cgpu, err);

		tot += sent;

		if (err)
			break;

		buf += sent;
		bufsiz -= sent;

		first = false;

		done = tdiff(&tv_finish, &read_start);
		// N.B. this is: return LIBUSB_SUCCESS with whatever size was written
		if (unlikely(done >= max))
			break;
		timeout = initial_timeout - (done * 1000);
		if (!timeout)
			break;
	}

	*processed = tot;

	if (NODEV(err))
		release_cgpu(cgpu);

out_unlock:
	DEVUNLOCK(cgpu, pstate);

	return err;
}

int __usb_transfer(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint32_t *data, int siz, unsigned int timeout, __maybe_unused enum usb_cmds cmd)
{
	struct cg_usb_device *usbdev;
#if DO_USB_STATS
	struct timeval tv_start, tv_finish;
#endif
	uint32_t *buf = NULL;
	int err, i, bufsiz;

	USBDEBUG("USB debug: _usb_transfer(%s (nodev=%s),type=%"PRIu8",req=%"PRIu8",value=%"PRIu16",index=%"PRIu16",siz=%d,timeout=%u,cmd=%s)", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), request_type, bRequest, wValue, wIndex, siz, timeout, usb_cmdname(cmd));

	if (cgpu->usbinfo.nodev) {
		USB_REJECT(cgpu, MODE_CTRL_WRITE);

		err = LIBUSB_ERROR_NO_DEVICE;
		goto out_;
	}
	usbdev = cgpu->usbdev;

	USBDEBUG("USB debug: @_usb_transfer() data=%s", bin2hex((unsigned char *)data, (size_t)siz));

	if (siz > 0) {
		bufsiz = siz - 1;
		bufsiz >>= 2;
		bufsiz++;
		buf = malloc(bufsiz << 2);
		if (unlikely(!buf))
			quit(1, "Failed to malloc in _usb_transfer");
		for (i = 0; i < bufsiz; i++)
			buf[i] = htole32(data[i]);
	}

	USBDEBUG("USB debug: @_usb_transfer() buf=%s", bin2hex((unsigned char *)buf, (size_t)siz));

	STATS_TIMEVAL(&tv_start);
	cg_rlock(&cgusb_fd_lock);
	err = libusb_control_transfer(usbdev->handle, request_type,
		bRequest, wValue, wIndex, (unsigned char *)buf, (uint16_t)siz,
		timeout == DEVTIMEOUT ? usbdev->found->timeout : timeout);
	cg_runlock(&cgusb_fd_lock);
	STATS_TIMEVAL(&tv_finish);
	USB_STATS(cgpu, &tv_start, &tv_finish, err, MODE_CTRL_WRITE, cmd, SEQ0);

	USBDEBUG("USB debug: @_usb_transfer(%s (nodev=%s)) err=%d%s", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), err, isnodev(err));

	IOERR_CHECK(cgpu, err);

	free(buf);

	if (NOCONTROLDEV(err))
		release_cgpu(cgpu);

out_:
	return err;
}

int _usb_transfer(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint32_t *data, int siz, unsigned int timeout, enum usb_cmds cmd)
{
	int pstate, err;

	DEVLOCK(cgpu, pstate);

	err = __usb_transfer(cgpu, request_type, bRequest, wValue, wIndex, data, siz, timeout, cmd);

	DEVUNLOCK(cgpu, pstate);

	return err;
}

int _usb_transfer_read(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, char *buf, int bufsiz, int *amount, unsigned int timeout, __maybe_unused enum usb_cmds cmd)
{
	struct cg_usb_device *usbdev;
#if DO_USB_STATS
	struct timeval tv_start, tv_finish;
#endif
	int err, pstate;

	DEVLOCK(cgpu, pstate);

	USBDEBUG("USB debug: _usb_transfer_read(%s (nodev=%s),type=%"PRIu8",req=%"PRIu8",value=%"PRIu16",index=%"PRIu16",bufsiz=%d,timeout=%u,cmd=%s)", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), request_type, bRequest, wValue, wIndex, bufsiz, timeout, usb_cmdname(cmd));

	if (cgpu->usbinfo.nodev) {
		USB_REJECT(cgpu, MODE_CTRL_READ);

		err = LIBUSB_ERROR_NO_DEVICE;
		goto out_unlock;
	}
	usbdev = cgpu->usbdev;

	*amount = 0;

	STATS_TIMEVAL(&tv_start);
	cg_rlock(&cgusb_fd_lock);
	err = libusb_control_transfer(usbdev->handle, request_type,
		bRequest, wValue, wIndex,
		(unsigned char *)buf, (uint16_t)bufsiz,
		timeout == DEVTIMEOUT ? usbdev->found->timeout : timeout);
	cg_runlock(&cgusb_fd_lock);
	STATS_TIMEVAL(&tv_finish);
	USB_STATS(cgpu, &tv_start, &tv_finish, err, MODE_CTRL_READ, cmd, SEQ0);

	USBDEBUG("USB debug: @_usb_transfer_read(%s (nodev=%s)) amt/err=%d%s%s%s", cgpu->drv->name, bool_str(cgpu->usbinfo.nodev), err, isnodev(err), err > 0 ? " = " : BLANK, err > 0 ? bin2hex((unsigned char *)buf, (size_t)err) : BLANK);

	IOERR_CHECK(cgpu, err);

	if (err > 0) {
		*amount = err;
		err = 0;
	} else if (NOCONTROLDEV(err))
		release_cgpu(cgpu);

out_unlock:
	DEVUNLOCK(cgpu, pstate);

	return err;
}

#define FTDI_STATUS_B0_MASK     (FTDI_RS0_CTS | FTDI_RS0_DSR | FTDI_RS0_RI | FTDI_RS0_RLSD)
#define FTDI_RS0_CTS    (1 << 4)
#define FTDI_RS0_DSR    (1 << 5)
#define FTDI_RS0_RI     (1 << 6)
#define FTDI_RS0_RLSD   (1 << 7)

/* Clear to send for FTDI */
int usb_ftdi_cts(struct cgpu_info *cgpu)
{
	char buf[2], ret;
	int err, amount;

	err = _usb_transfer_read(cgpu, (uint8_t)FTDI_TYPE_IN, (uint8_t)5,
				 (uint16_t)0, (uint16_t)0, buf, 2,
				 &amount, DEVTIMEOUT, C_FTDI_STATUS);
	/* We return true in case drivers are waiting indefinitely to try and
	 * write to something that's not there. */
	if (err)
		return true;

	ret = buf[0] & FTDI_STATUS_B0_MASK;
	return (ret & FTDI_RS0_CTS);
}

int usb_ftdi_set_latency(struct cgpu_info *cgpu)
{
	int err = 0;
	int pstate;

	DEVLOCK(cgpu, pstate);

	if (cgpu->usbdev) {
		if (cgpu->usbdev->usb_type != USB_TYPE_FTDI) {
			applog(LOG_ERR, "%s: cgid %d latency request on non-FTDI device",
				cgpu->drv->name, cgpu->cgminer_id);
			err = LIBUSB_ERROR_NOT_SUPPORTED;
		} else if (cgpu->usbdev->found->latency == LATENCY_UNUSED) {
			applog(LOG_ERR, "%s: cgid %d invalid latency (UNUSED)",
				cgpu->drv->name, cgpu->cgminer_id);
			err = LIBUSB_ERROR_NOT_SUPPORTED;
		}

		if (!err)
			err = __usb_transfer(cgpu, FTDI_TYPE_OUT, FTDI_REQUEST_LATENCY,
						cgpu->usbdev->found->latency,
						cgpu->usbdev->found->interface,
						NULL, 0, DEVTIMEOUT, C_LATENCY);
	}

	DEVUNLOCK(cgpu, pstate);

	applog(LOG_DEBUG, "%s: cgid %d %s got err %d",
				cgpu->drv->name, cgpu->cgminer_id,
				usb_cmdname(C_LATENCY), err);

	return err;
}

void usb_buffer_enable(struct cgpu_info *cgpu)
{
	struct cg_usb_device *cgusb;
	int pstate;

	DEVLOCK(cgpu, pstate);

	cgusb = cgpu->usbdev;
	if (cgusb && !cgusb->buffer) {
		cgusb->bufamt = 0;
		cgusb->buffer = malloc(USB_MAX_READ+1);
		if (!cgusb->buffer)
			quit(1, "Failed to malloc buffer for USB %s%i",
				cgpu->drv->name, cgpu->device_id);
		cgusb->bufsiz = USB_MAX_READ;
	}

	DEVUNLOCK(cgpu, pstate);
}

void usb_buffer_disable(struct cgpu_info *cgpu)
{
	struct cg_usb_device *cgusb;
	int pstate;

	DEVLOCK(cgpu, pstate);

	cgusb = cgpu->usbdev;
	if (cgusb && cgusb->buffer) {
		cgusb->bufamt = 0;
		cgusb->bufsiz = 0;
		free(cgusb->buffer);
		cgusb->buffer = NULL;
	}

	DEVUNLOCK(cgpu, pstate);
}

void usb_buffer_clear(struct cgpu_info *cgpu)
{
	int pstate;

	DEVLOCK(cgpu, pstate);

	if (cgpu->usbdev)
		cgpu->usbdev->bufamt = 0;

	DEVUNLOCK(cgpu, pstate);
}

uint32_t usb_buffer_size(struct cgpu_info *cgpu)
{
	uint32_t ret = 0;
	int pstate;

	DEVLOCK(cgpu, pstate);

	if (cgpu->usbdev)
		ret = cgpu->usbdev->bufamt;

	DEVUNLOCK(cgpu, pstate);

	return ret;
}

// Need to set all devices with matching usbdev
void usb_set_dev_start(struct cgpu_info *cgpu)
{
	struct cg_usb_device *cgusb;
	struct cgpu_info *cgpu2;
	struct timeval now;
	int pstate;

	DEVLOCK(cgpu, pstate);

	cgusb = cgpu->usbdev;

	// If the device wasn't dropped
	if (cgusb != NULL) {
		int i;

		cgtime(&now);

		for (i = 0; i < total_devices; i++) {
			cgpu2 = get_devices(i);
			if (cgpu2->usbdev == cgusb)
				copy_time(&(cgpu2->dev_start_tv), &now);
		}
	}

	DEVUNLOCK(cgpu, pstate);
}

void usb_cleanup()
{
	struct cgpu_info *cgpu;
	int count;
	int i;

	hotplug_time = 0;

	nmsleep(10);

	count = 0;
	for (i = 0; i < total_devices; i++) {
		cgpu = devices[i];
		switch (cgpu->drv->drv_id) {
			case DRIVER_BFLSC:
			case DRIVER_BITFORCE:
			case DRIVER_MODMINER:
			case DRIVER_ICARUS:
			case DRIVER_AVALON:
				wr_lock(cgpu->usbinfo.devlock);
				release_cgpu(cgpu);
				wr_unlock(cgpu->usbinfo.devlock);
				count++;
				break;
			default:
				break;
		}
	}

	/*
	 * Must attempt to wait for the resource thread to release coz
	 * during a restart it won't automatically release them in linux
	 */
	if (count) {
		struct timeval start, now;

		cgtime(&start);
		while (42) {
			nmsleep(50);

			mutex_lock(&cgusbres_lock);

			if (!res_work_head)
				break;

			cgtime(&now);
			if (tdiff(&now, &start) > 0.366) {
				applog(LOG_WARNING,
					"usb_cleanup gave up waiting for resource thread");
				break;
			}

			mutex_unlock(&cgusbres_lock);
		}
		mutex_unlock(&cgusbres_lock);
	}

	cgsem_destroy(&usb_resource_sem);
}

void usb_initialise()
{
	char *fre, *ptr, *comma, *colon;
	int bus, dev, lim, i;
	bool found;

	for (i = 0; i < DRIVER_MAX; i++) {
		drv_count[i].count = 0;
		drv_count[i].limit = 999999;
	}

	cgusb_check_init();

	if (opt_usb_select && *opt_usb_select) {
		// Absolute device limit
		if (*opt_usb_select == ':') {
			total_limit = atoi(opt_usb_select+1);
			if (total_limit < 0)
				quit(1, "Invalid --usb total limit");
		// Comma list of bus:dev devices to match
		} else if (isdigit(*opt_usb_select)) {
			fre = ptr = strdup(opt_usb_select);
			do {
				comma = strchr(ptr, ',');
				if (comma)
					*(comma++) = '\0';

				colon = strchr(ptr, ':');
				if (!colon)
					quit(1, "Invalid --usb bus:dev missing ':'");

				*(colon++) = '\0';

				if (!isdigit(*ptr))
					quit(1, "Invalid --usb bus:dev - bus must be a number");

				if (!isdigit(*colon) && *colon != '*')
					quit(1, "Invalid --usb bus:dev - dev must be a number or '*'");

				bus = atoi(ptr);
				if (bus <= 0)
					quit(1, "Invalid --usb bus:dev - bus must be > 0");

				if (*colon == '*')
					dev = -1;
				else {
					dev = atoi(colon);
					if (dev <= 0)
						quit(1, "Invalid --usb bus:dev - dev must be > 0 or '*'");
				}

				busdev = realloc(busdev, sizeof(*busdev) * (++busdev_count));
				if (unlikely(!busdev))
					quit(1, "USB failed to realloc busdev");

				busdev[busdev_count-1].bus_number = bus;
				busdev[busdev_count-1].device_address = dev;

				ptr = comma;
			} while (ptr);
			free(fre);
		// Comma list of DRV:limit
		} else {
			fre = ptr = strdup(opt_usb_select);
			do {
				comma = strchr(ptr, ',');
				if (comma)
					*(comma++) = '\0';

				colon = strchr(ptr, ':');
				if (!colon)
					quit(1, "Invalid --usb DRV:limit missing ':'");

				*(colon++) = '\0';

				if (!isdigit(*colon))
					quit(1, "Invalid --usb DRV:limit - limit must be a number");

				lim = atoi(colon);
				if (lim < 0)
					quit(1, "Invalid --usb DRV:limit - limit must be >= 0");

				found = false;
#ifdef USE_BFLSC
				if (strcasecmp(ptr, bflsc_drv.name) == 0) {
					drv_count[bflsc_drv.drv_id].limit = lim;
					found = true;
				}
#endif
#ifdef USE_BITFORCE
				if (!found && strcasecmp(ptr, bitforce_drv.name) == 0) {
					drv_count[bitforce_drv.drv_id].limit = lim;
					found = true;
				}
#endif
#ifdef USE_MODMINER
				if (!found && strcasecmp(ptr, modminer_drv.name) == 0) {
					drv_count[modminer_drv.drv_id].limit = lim;
					found = true;
				}
#endif
#ifdef USE_ICARUS
				if (!found && strcasecmp(ptr, icarus_drv.name) == 0) {
					drv_count[icarus_drv.drv_id].limit = lim;
					found = true;
				}
#endif
#ifdef USE_AVALON
				if (!found && strcasecmp(ptr, avalon_drv.name) == 0) {
					drv_count[avalon_drv.drv_id].limit = lim;
					found = true;
				}
#endif
				if (!found)
					quit(1, "Invalid --usb DRV:limit - unknown DRV='%s'", ptr);

				ptr = comma;
			} while (ptr);
			free(fre);
		}
	}
}

#ifndef WIN32
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef __APPLE__
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
	struct seminfo *__buf;
};
#endif

#else
static LPSECURITY_ATTRIBUTES unsec(LPSECURITY_ATTRIBUTES sec)
{
	FreeSid(((PSECURITY_DESCRIPTOR)(sec->lpSecurityDescriptor))->Group);
	free(sec->lpSecurityDescriptor);
	free(sec);
	return NULL;
}

static LPSECURITY_ATTRIBUTES mksec(const char *dname, uint8_t bus_number, uint8_t device_address)
{
	SID_IDENTIFIER_AUTHORITY SIDAuthWorld = {SECURITY_WORLD_SID_AUTHORITY};
	PSID gsid = NULL;
	LPSECURITY_ATTRIBUTES sec_att = NULL;
	PSECURITY_DESCRIPTOR sec_des = NULL;

	sec_des = malloc(sizeof(*sec_des));
	if (unlikely(!sec_des))
		quit(1, "MTX: Failed to malloc LPSECURITY_DESCRIPTOR");

	if (!InitializeSecurityDescriptor(sec_des, SECURITY_DESCRIPTOR_REVISION)) {
		applog(LOG_ERR,
			"MTX: %s (%d:%d) USB failed to init secdes err (%d)",
			dname, (int)bus_number, (int)device_address,
			(int)GetLastError());
		free(sec_des);
		return NULL;
	}

	if (!SetSecurityDescriptorDacl(sec_des, TRUE, NULL, FALSE)) {
		applog(LOG_ERR,
			"MTX: %s (%d:%d) USB failed to secdes dacl err (%d)",
			dname, (int)bus_number, (int)device_address,
			(int)GetLastError());
		free(sec_des);
		return NULL;
	}

	if(!AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &gsid)) {
		applog(LOG_ERR,
			"MTX: %s (%d:%d) USB failed to create gsid err (%d)",
			dname, (int)bus_number, (int)device_address,
			(int)GetLastError());
		free(sec_des);
		return NULL;
	}

	if (!SetSecurityDescriptorGroup(sec_des, gsid, FALSE)) {
		applog(LOG_ERR,
			"MTX: %s (%d:%d) USB failed to secdes grp err (%d)",
			dname, (int)bus_number, (int)device_address,
			(int)GetLastError());
		FreeSid(gsid);
		free(sec_des);
		return NULL;
	}

	sec_att = malloc(sizeof(*sec_att));
	if (unlikely(!sec_att))
		quit(1, "MTX: Failed to malloc LPSECURITY_ATTRIBUTES");

	sec_att->nLength = sizeof(*sec_att);
	sec_att->lpSecurityDescriptor = sec_des;
	sec_att->bInheritHandle = FALSE;

	return sec_att;
}
#endif

// Any errors should always be printed since they will rarely if ever occur
// and thus it is best to always display them
static bool resource_lock(const char *dname, uint8_t bus_number, uint8_t device_address)
{
	applog(LOG_DEBUG, "USB res lock %s %d-%d", dname, (int)bus_number, (int)device_address);

#ifdef WIN32
	struct cgpu_info *cgpu;
	LPSECURITY_ATTRIBUTES sec;
	HANDLE usbMutex;
	char name[64];
	DWORD res;
	int i;

	if (is_in_use_bd(bus_number, device_address))
		return false;

	sprintf(name, "cg-usb-%d-%d", (int)bus_number, (int)device_address);

	sec = mksec(dname, bus_number, device_address);
	if (!sec)
		return false;

	usbMutex = CreateMutex(sec, FALSE, name);
	if (usbMutex == NULL) {
		applog(LOG_ERR,
			"MTX: %s USB failed to get '%s' err (%d)",
			dname, name, (int)GetLastError());
		sec = unsec(sec);
		return false;
	}

	res = WaitForSingleObject(usbMutex, 0);

	switch(res) {
		case WAIT_OBJECT_0:
		case WAIT_ABANDONED:
			// Am I using it already?
			for (i = 0; i < total_devices; i++) {
				cgpu = get_devices(i);
				if (cgpu->usbinfo.bus_number == bus_number &&
				    cgpu->usbinfo.device_address == device_address &&
				    cgpu->usbinfo.nodev == false) {
					if (ReleaseMutex(usbMutex)) {
						applog(LOG_WARNING,
							"MTX: %s USB can't get '%s' - device in use",
							dname, name);
						goto fail;
					}
					applog(LOG_ERR,
						"MTX: %s USB can't get '%s' - device in use - failure (%d)",
						dname, name, (int)GetLastError());
					goto fail;
				}
			}
			break;
		case WAIT_TIMEOUT:
			if (!hotplug_mode)
				applog(LOG_WARNING,
					"MTX: %s USB failed to get '%s' - device in use",
					dname, name);
			goto fail;
		case WAIT_FAILED:
			applog(LOG_ERR,
				"MTX: %s USB failed to get '%s' err (%d)",
				dname, name, (int)GetLastError());
			goto fail;
		default:
			applog(LOG_ERR,
				"MTX: %s USB failed to get '%s' unknown reply (%d)",
				dname, name, (int)res);
			goto fail;
	}

	add_in_use(bus_number, device_address);
	in_use_store_ress(bus_number, device_address, (void *)usbMutex, (void *)sec);

	return true;
fail:
	CloseHandle(usbMutex);
	sec = unsec(sec);
	return false;
#else
	struct semid_ds seminfo;
	union semun opt;
	char name[64];
	key_t *key;
	int *sem;
	int fd, count;

	if (is_in_use_bd(bus_number, device_address))
		return false;

	sprintf(name, "/tmp/cgminer-usb-%d-%d", (int)bus_number, (int)device_address);
	fd = open(name, O_CREAT|O_RDONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		applog(LOG_ERR,
			"SEM: %s USB open failed '%s' err (%d) %s",
			dname, name, errno, strerror(errno));
		goto _out;
	}
	close(fd);

	key = malloc(sizeof(*key));
	if (unlikely(!key))
		quit(1, "SEM: Failed to malloc key");

	sem = malloc(sizeof(*sem));
	if (unlikely(!sem))
		quit(1, "SEM: Failed to malloc sem");

	*key = ftok(name, 'K');
	*sem = semget(*key, 1, IPC_CREAT | IPC_EXCL | 438);
	if (*sem < 0) {
		if (errno != EEXIST) {
			applog(LOG_ERR,
				"SEM: %s USB failed to get '%s' err (%d) %s",
				dname, name, errno, strerror(errno));
			goto free_out;
		}

		*sem = semget(*key, 1, 0);
		if (*sem < 0) {
			applog(LOG_ERR,
				"SEM: %s USB failed to access '%s' err (%d) %s",
				dname, name, errno, strerror(errno));
			goto free_out;
		}

		opt.buf = &seminfo;
		count = 0;
		while (++count) {
			// Should NEVER take 100ms
			if (count > 99) {
				applog(LOG_ERR,
					"SEM: %s USB timeout waiting for (%d) '%s'",
					dname, *sem, name);
				goto free_out;
			}
			if (semctl(*sem, 0, IPC_STAT, opt) == -1) {
				applog(LOG_ERR,
					"SEM: %s USB failed to wait for (%d) '%s' count %d err (%d) %s",
					dname, *sem, name, count, errno, strerror(errno));
				goto free_out;
			}
			if (opt.buf->sem_otime != 0)
				break;
			nmsleep(1);
		}
	}

	struct sembuf sops[] = {
		{ 0, 0, IPC_NOWAIT | SEM_UNDO },
		{ 0, 1, IPC_NOWAIT | SEM_UNDO }
	};

	if (semop(*sem, sops, 2)) {
		if (errno == EAGAIN) {
			if (!hotplug_mode)
				applog(LOG_WARNING,
					"SEM: %s USB failed to get (%d) '%s' - device in use",
					dname, *sem, name);
		} else {
			applog(LOG_DEBUG,
				"SEM: %s USB failed to get (%d) '%s' err (%d) %s",
				dname, *sem, name, errno, strerror(errno));
		}
		goto free_out;
	}

	add_in_use(bus_number, device_address);
	in_use_store_ress(bus_number, device_address, (void *)key, (void *)sem);
	return true;

free_out:
	free(sem);
	free(key);
_out:
	return false;
#endif
}

// Any errors should always be printed since they will rarely if ever occur
// and thus it is best to always display them
static void resource_unlock(const char *dname, uint8_t bus_number, uint8_t device_address)
{
	applog(LOG_DEBUG, "USB res unlock %s %d-%d", dname, (int)bus_number, (int)device_address);

#ifdef WIN32
	LPSECURITY_ATTRIBUTES sec = NULL;
	HANDLE usbMutex = NULL;
	char name[64];

	sprintf(name, "cg-usb-%d-%d", (int)bus_number, (int)device_address);

	in_use_get_ress(bus_number, device_address, (void **)(&usbMutex), (void **)(&sec));

	if (!usbMutex || !sec)
		goto fila;

	if (!ReleaseMutex(usbMutex))
		applog(LOG_ERR,
			"MTX: %s USB failed to release '%s' err (%d)",
			dname, name, (int)GetLastError());

fila:

	if (usbMutex)
		CloseHandle(usbMutex);
	if (sec)
		unsec(sec);
	remove_in_use(bus_number, device_address);
	return;
#else
	char name[64];
	key_t *key = NULL;
	int *sem = NULL;

	sprintf(name, "/tmp/cgminer-usb-%d-%d", (int)bus_number, (int)device_address);

	in_use_get_ress(bus_number, device_address, (void **)(&key), (void **)(&sem));

	if (!key || !sem)
		goto fila;

	struct sembuf sops[] = {
		{ 0, -1, SEM_UNDO }
	};

	// Allow a 10ms timeout
	// exceeding this timeout means it would probably never succeed anyway
	struct timespec timeout = { 0, 10000000 };

	if (semtimedop(*sem, sops, 1, &timeout)) {
		applog(LOG_ERR,
			"SEM: %s USB failed to release '%s' err (%d) %s",
			dname, name, errno, strerror(errno));
	}

	if (semctl(*sem, 0, IPC_RMID)) {
		applog(LOG_WARNING,
			"SEM: %s USB failed to remove SEM '%s' err (%d) %s",
			dname, name, errno, strerror(errno));
	}

fila:

	free(sem);
	free(key);
	remove_in_use(bus_number, device_address);
	return;
#endif
}

static void resource_process()
{
	struct resource_work *res_work = NULL;
	struct resource_reply *res_reply = NULL;
	bool ok;

	applog(LOG_DEBUG, "RES: %s (%d:%d) lock=%d",
			res_work_head->dname,
			(int)res_work_head->bus_number,
			(int)res_work_head->device_address,
			res_work_head->lock);

	if (res_work_head->lock) {
		ok = resource_lock(res_work_head->dname,
					res_work_head->bus_number,
					res_work_head->device_address);

		applog(LOG_DEBUG, "RES: %s (%d:%d) lock ok=%d",
				res_work_head->dname,
				(int)res_work_head->bus_number,
				(int)res_work_head->device_address,
				ok);

		res_reply = calloc(1, sizeof(*res_reply));
		if (unlikely(!res_reply))
			quit(1, "USB failed to calloc res_reply");

		res_reply->bus_number = res_work_head->bus_number;
		res_reply->device_address = res_work_head->device_address;
		res_reply->got = ok;
		res_reply->next = res_reply_head;

		res_reply_head = res_reply;
	}
	else
		resource_unlock(res_work_head->dname,
				res_work_head->bus_number,
				res_work_head->device_address);

	res_work = res_work_head;
	res_work_head = res_work_head->next;
	free(res_work);
}

void *usb_resource_thread(void __maybe_unused *userdata)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("usbresource");

	applog(LOG_DEBUG, "RES: thread starting");

	while (42) {
		/* Wait to be told we have work to do */
		cgsem_wait(&usb_resource_sem);

		mutex_lock(&cgusbres_lock);
		while (res_work_head)
			resource_process();
		mutex_unlock(&cgusbres_lock);
	}

	return NULL;
}
