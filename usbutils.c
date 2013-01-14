/*
 * Copyright 2012-2013 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdint.h>
#include <stdbool.h>

#include "logging.h"
#include "miner.h"
#include "usbutils.h"

#define NODEV(err) ((err) == LIBUSB_ERROR_NO_DEVICE || \
			(err) == LIBUSB_ERROR_PIPE || \
			(err) == LIBUSB_ERROR_OTHER)

#ifdef USE_ICARUS
#define DRV_ICARUS 1
#endif

#ifdef USE_BITFORCE
#define DRV_BITFORCE 2
#endif

#ifdef USE_MODMINER
#define DRV_MODMINER 3
#endif

#define DRV_LAST -1

#define USB_CONFIG 1

#define EPI(x) (LIBUSB_ENDPOINT_IN | (unsigned char)(x))
#define EPO(x) (LIBUSB_ENDPOINT_OUT | (unsigned char)(x))

#ifdef WIN32
#define BITFORCE_TIMEOUT_MS 500
#define MODMINER_TIMEOUT_MS 200
#else
#define BITFORCE_TIMEOUT_MS 200
#define MODMINER_TIMEOUT_MS 100
#endif

#ifdef USE_BITFORCE
static struct usb_endpoints bfl_eps[] = {
#ifdef WIN32
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(1), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(2), 0 }
#else
	{ LIBUSB_TRANSFER_TYPE_BULK,	512,	EPI(1), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	512,	EPO(2), 0 }
#endif
};
#endif

#ifdef USE_MODMINER
static struct usb_endpoints mmq_eps[] = {
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPI(3), 0 },
	{ LIBUSB_TRANSFER_TYPE_BULK,	64,	EPO(3), 0 }
};
#endif

// TODO: Add support for (at least) Isochronous endpoints
static struct usb_find_devices find_dev[] = {
/*
#ifdef USE_ICARUS
	{ DRV_ICARUS, 	"ICA",	0x067b,	0x0230,	true,	EPI(3),	EPO(2), 1 },
	{ DRV_ICARUS, 	"LOT",	0x0403,	0x6001,	false,	EPI(0),	EPO(0), 1 },
	{ DRV_ICARUS, 	"CM1",	0x067b,	0x0230,	false,	EPI(0),	EPO(0), 1 },
#endif
*/
#ifdef USE_BITFORCE
	{
		.drv = DRV_BITFORCE,
		.name = "BFL",
		.idVendor = 0x0403,
		.idProduct = 0x6014,
		.config = 1,
		.interface = 0,
		.timeout = BITFORCE_TIMEOUT_MS,
		.epcount = ARRAY_SIZE(bfl_eps),
		.eps = bfl_eps },
#endif
#ifdef USE_MODMINER
	{
		.drv = DRV_MODMINER,
		.name = "MMQ",
		.idVendor = 0x1fc9,
		.idProduct = 0x0003,
		.config = 1,
		.interface = 1,
		.timeout = MODMINER_TIMEOUT_MS,
		.epcount = ARRAY_SIZE(mmq_eps),
		.eps = mmq_eps },
#endif
	{ DRV_LAST, NULL, 0, 0, 0, 0, 0, 0, NULL }
};

#ifdef USE_BITFORCE
extern struct device_drv bitforce_drv;
#endif

#ifdef USE_ICARUS
extern struct device_drv icarus_drv;
#endif

#ifdef USE_MODMINER
extern struct device_drv modminer_drv;
#endif

/*
 * Our own internal list of used USB devices
 * So two drivers or a single driver searching
 * can't touch the same device during detection
 */
struct usb_list {
	uint8_t bus_number;
	uint8_t device_address;
	uint8_t filler[2];
	struct usb_list *prev;
	struct usb_list *next;
};

#define STRBUFLEN 256
static const char *BLANK = "";

static pthread_mutex_t *list_lock = NULL;
static struct usb_list *usb_head = NULL;

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

struct cg_usb_stats_details {
	int seq;
	struct cg_usb_stats_item item[CMD_ERROR+1];
};

struct cg_usb_stats {
	char *name;
	int device_id;
	struct cg_usb_stats_details *details;
};

#define SEQ0 0
#define SEQ1 1

static struct cg_usb_stats *usb_stats = NULL;
static int next_stat = 0;

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

static void usb_full(ssize_t count, libusb_device *dev, char **buf, size_t *off, size_t *len)
{
	struct libusb_device_descriptor desc;
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
	if (err) {
		sprintf(tmp, EOL ".USB dev %d: Failed to get descriptor, err %d",
					(int)count, err);
		append(buf, tmp, off, len);
		return;
	}

	sprintf(tmp, EOL ".USB dev %d: Device Descriptor:" EOL "\tLength: %d" EOL
			"\tDescriptor Type: %s" EOL "\tUSB: %04x" EOL "\tDeviceClass: %d" EOL
			"\tDeviceSubClass: %d" EOL "\tDeviceProtocol: %d" EOL "\tMaxPacketSize0: %d" EOL
			"\tidVendor: %04x" EOL "\tidProduct: %04x" EOL "\tDeviceRelease: %x" EOL
			"\tNumConfigurations: %d",
				(int)count, (int)(desc.bLength), destype(desc.bDescriptorType),
				desc.bcdUSB, (int)(desc.bDeviceClass), (int)(desc.bDeviceSubClass),
				(int)(desc.bDeviceProtocol), (int)(desc.bMaxPacketSize0),
				desc.idVendor, desc.idProduct, desc.bcdDevice,
				(int)(desc.bNumConfigurations));
	append(buf, tmp, off, len);

	err = libusb_open(dev, &handle);
	if (err) {
		sprintf(tmp, EOL "  ** dev %d: Failed to open, err %d", (int)count, err);
		append(buf, tmp, off, len);
		return;
	}

	if (libusb_kernel_driver_active(handle, 0) == 1) {
		sprintf(tmp, EOL "   * dev %d: kernel attached", (int)count);
		append(buf, tmp, off, len);
	}

	err = libusb_get_active_config_descriptor(dev, &config);
	if (err) {
		if (!setgetdes(count, dev, handle, &config, 1, buf, off, len)
		&&  !setgetdes(count, dev, handle, &config, 0, buf, off, len)) {
			libusb_close(handle);
			sprintf(tmp, EOL "  ** dev %d: Failed to set config descriptor to %d or %d",
					(int)count, 1, 0);
			append(buf, tmp, off, len);
			return;
		}
	}

	sprintf(tmp, EOL "     dev %d: Active Config:" EOL "\tDescriptorType: %s" EOL
			"\tNumInterfaces: %d" EOL "\tConfigurationValue: %d" EOL
			"\tAttributes: %d" EOL "\tMaxPower: %d",
				(int)count, destype(config->bDescriptorType),
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
						(int)count, j, destype(idesc->bDescriptorType),
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
							(int)count, (int)(idesc->bInterfaceNumber), k,
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

	err = libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, man, STRBUFLEN);
	if (err < 0)
		sprintf((char *)man, "** err(%d)", err);

	err = libusb_get_string_descriptor_ascii(handle, desc.iProduct, prod, STRBUFLEN);
	if (err < 0)
		sprintf((char *)prod, "** err(%d)", err);

	err = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, ser, STRBUFLEN);
	if (err < 0)
		sprintf((char *)ser, "** err(%d)", err);

	sprintf(tmp, EOL "     dev %d: More Info:" EOL "\tManufacturer: '%s'" EOL
			"\tProduct: '%s'" EOL "\tSerial '%s'",
				(int)count, man, prod, ser);
	append(buf, tmp, off, len);

	libusb_close(handle);
}

// Function to dump all USB devices
static void usb_all()
{
	libusb_device **list;
	ssize_t count, i;
	char *buf;
	size_t len, off;

	count = libusb_get_device_list(NULL, &list);
	if (count < 0) {
		applog(LOG_ERR, "USB all: failed, err %d", (int)count);
		return;
	}

	if (count == 0)
		applog(LOG_WARNING, "USB all: found no devices");
	else
	{
		len = 10000;
		buf = malloc(len+1);

		sprintf(buf, "USB all: found %d devices", (int)count);

		off = strlen(buf);

		for (i = 0; i < count; i++)
			usb_full(i, list[i], &buf, &off, &len);

		applog(LOG_WARNING, "%s", buf);

		free(buf);
	}

	libusb_free_device_list(list, 1);
}

static void cgusb_check_init()
{
	mutex_lock(&cgusb_lock);

	if (list_lock == NULL) {
		list_lock = calloc(1, sizeof(*list_lock));
		mutex_init(list_lock);

		// N.B. environment LIBUSB_DEBUG also sets libusb_set_debug()
		if (opt_usbdump >= 0) {
			libusb_set_debug(NULL, opt_usbdump);
			usb_all();
		}

		usb_commands = malloc(sizeof(*usb_commands) * C_MAX);

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
	}

	mutex_unlock(&cgusb_lock);
}

static bool in_use(libusb_device *dev, bool lock)
{
	struct usb_list *usb_tmp;
	bool used = false;
	uint8_t bus_number;
	uint8_t device_address;

	bus_number = libusb_get_bus_number(dev);
	device_address = libusb_get_device_address(dev);

	if (lock)
		mutex_lock(list_lock);

	if ((usb_tmp = usb_head))
		do {
			if (bus_number == usb_tmp->bus_number
			&&  device_address == usb_tmp->device_address) {
				used = true;
				break;
			}

			usb_tmp = usb_tmp->next;

		} while (usb_tmp != usb_head);

	if (lock)
		mutex_unlock(list_lock);

	return used;
}

static void add_used(libusb_device *dev, bool lock)
{
	struct usb_list *usb_tmp;
	char buf[128];
	uint8_t bus_number;
	uint8_t device_address;

	bus_number = libusb_get_bus_number(dev);
	device_address = libusb_get_device_address(dev);

	if (lock)
		mutex_lock(list_lock);

	if (in_use(dev, false)) {
		if (lock)
			mutex_unlock(list_lock);

		sprintf(buf, "add_used() duplicate bus_number %d device_address %d",
				bus_number, device_address);
		quit(1, buf);
	}

	usb_tmp = malloc(sizeof(*usb_tmp));

	usb_tmp->bus_number = bus_number;
	usb_tmp->device_address = device_address;

	if (usb_head) {
		// add to end
		usb_tmp->prev = usb_head->prev;
		usb_tmp->next = usb_head;
		usb_head->prev = usb_tmp;
		usb_tmp->prev->next = usb_tmp;
	} else {
		usb_tmp->prev = usb_tmp;
		usb_tmp->next = usb_tmp;
		usb_head = usb_tmp;
	}

	if (lock)
		mutex_unlock(list_lock);
}

static void release(uint8_t bus_number, uint8_t device_address, bool lock)
{
	struct usb_list *usb_tmp;
	bool found = false;
	char buf[128];

	if (lock)
		mutex_lock(list_lock);

	if ((usb_tmp = usb_head))
		do {
			if (bus_number == usb_tmp->bus_number
			&&  device_address == usb_tmp->device_address) {
				found = true;
				break;
			}

			usb_tmp = usb_tmp->next;

		} while (usb_tmp != usb_head);

	if (!found) {
		if (lock)
			mutex_unlock(list_lock);

		sprintf(buf, "release() unknown: bus_number %d device_address %d",
				bus_number, device_address);
		quit(1, buf);
	}

	if (usb_tmp->next == usb_tmp) {
		usb_head = NULL;
	} else {
		if (usb_head == usb_tmp)
			usb_head = usb_tmp->next;
		usb_tmp->next->prev = usb_tmp->prev;
		usb_tmp->prev->next = usb_tmp->next;
	}

	if (lock)
		mutex_unlock(list_lock);

	free(usb_tmp);
}

static void release_dev(libusb_device *dev, bool lock)
{
	uint8_t bus_number;
	uint8_t device_address;

	bus_number = libusb_get_bus_number(dev);
	device_address = libusb_get_device_address(dev);

	release(bus_number, device_address, lock);
}

static struct cg_usb_device *free_cgusb(struct cg_usb_device *cgusb)
{
	if (cgusb->serial_string && cgusb->serial_string != BLANK)
		free(cgusb->serial_string);

	if (cgusb->manuf_string && cgusb->manuf_string != BLANK)
		free(cgusb->manuf_string);

	if (cgusb->prod_string && cgusb->prod_string != BLANK)
		free(cgusb->prod_string);

	free(cgusb->descriptor);

	free(cgusb->found);

	free(cgusb);

	return NULL;
}

void usb_uninit(struct cgpu_info *cgpu)
{
	libusb_release_interface(cgpu->usbdev->handle, cgpu->usbdev->found->interface);
	libusb_close(cgpu->usbdev->handle);
	cgpu->usbdev = free_cgusb(cgpu->usbdev);
}

void release_cgpu(struct cgpu_info *cgpu)
{
	struct cg_usb_device *cgusb = cgpu->usbdev;
	uint8_t bus_number;
	uint8_t device_address;
	int i;

	cgpu->nodev = true;

	// Any devices sharing the same USB device should be marked also
	// Currently only MMQ shares a USB device
	for (i = 0; i < total_devices; i++)
		if (devices[i] != cgpu && devices[i]->usbdev == cgusb) {
			devices[i]->nodev = true;
			devices[i]->usbdev = NULL;
		}

	bus_number = cgusb->bus_number;
	device_address = cgusb->device_address;

	usb_uninit(cgpu);

	release(bus_number, device_address, true);
}

bool usb_init(struct cgpu_info *cgpu, struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cg_usb_device *cgusb = NULL;
	struct libusb_config_descriptor *config = NULL;
	const struct libusb_interface_descriptor *idesc;
	const struct libusb_endpoint_descriptor *epdesc;
	unsigned char strbuf[STRBUFLEN+1];
	int err, i, j, k;

	cgusb = calloc(1, sizeof(*cgusb));
	cgusb->found = found;

	cgusb->bus_number = libusb_get_bus_number(dev);
	cgusb->device_address = libusb_get_device_address(dev);

	cgusb->descriptor = calloc(1, sizeof(*(cgusb->descriptor)));

	err = libusb_get_device_descriptor(dev, cgusb->descriptor);
	if (err) {
		applog(LOG_ERR, "USB init failed to get descriptor, err %d", err);
		goto dame;
	}

	err = libusb_open(dev, &(cgusb->handle));
	if (err) {
		switch (err) {
			case LIBUSB_ERROR_ACCESS:
				applog(LOG_ERR, "USB init open device failed, err %d, you dont have priviledge to access the device", err);
				break;
#ifdef WIN32
			// Windows specific message
			case LIBUSB_ERROR_NOT_SUPPORTED:
				applog(LOG_ERR, "USB init, open device failed, err %d, you need to install a Windows USB driver for the device", err);
				break;
#endif
			default:
				applog(LOG_ERR, "USB init, open device failed, err %d", err);
		}
		goto dame;
	}

	if (libusb_kernel_driver_active(cgusb->handle, 0) == 1) {
		applog(LOG_DEBUG, "USB init, kernel attached ...");
		if (libusb_detach_kernel_driver(cgusb->handle, 0) == 0)
			applog(LOG_DEBUG, "USB init, kernel detached successfully");
		else
			applog(LOG_WARNING, "USB init, kernel detach failed :(");
	}

	err = libusb_set_configuration(cgusb->handle, found->config);
	if (err) {
		switch(err) {
			case LIBUSB_ERROR_BUSY:
				applog(LOG_WARNING, "USB init, %s device %d:%d in use",
						found->name, cgusb->bus_number, cgusb->device_address);
				break;
			default:
				applog(LOG_DEBUG, "USB init, failed to set config to %d, err %d",
						found->config, err);
		}
		goto cldame;
	}

	err = libusb_get_active_config_descriptor(dev, &config);
	if (err) {
		applog(LOG_DEBUG, "USB init, failed to get config descriptor %d, err %d",
			found->config, err);
		goto cldame;
	}

	if ((int)(config->bNumInterfaces) <= found->interface)
		goto cldame;

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
						break;
					}
				}
			}
		}
	}

	for (i = 0; i < found->epcount; i++)
		if (found->eps[i].found == false)
			goto cldame;

	err = libusb_claim_interface(cgusb->handle, found->interface);
	if (err) {
		applog(LOG_DEBUG, "USB init, claim interface %d failed, err %d",
			found->interface, err);
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

	applog(LOG_DEBUG, "USB init device bus_number=%d device_address=%d usbver=%04x prod='%s' manuf='%s' serial='%s'", (int)(cgusb->bus_number), (int)(cgusb->device_address), cgusb->usbver, cgusb->prod_string, cgusb->manuf_string, cgusb->serial_string);

	cgpu->usbdev = cgusb;

	libusb_free_config_descriptor(config);

	// Allow a name change based on the idVendor+idProduct
	// N.B. must be done before calling add_cgpu()
	if (strcmp(cgpu->drv->name, found->name)) {
		if (!cgpu->drv->copy)
			cgpu->drv = copy_drv(cgpu->drv);
		cgpu->drv->name = (char *)(found->name);
	}

	return true;

cldame:

	libusb_close(cgusb->handle);

dame:

	if (config)
		libusb_free_config_descriptor(config);

	cgusb = free_cgusb(cgusb);

	return false;
}

static bool usb_check_device(struct device_drv *drv, struct libusb_device *dev, struct usb_find_devices *look)
{
	struct libusb_device_descriptor desc;
	int err;

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
				memcpy(found, &(find_dev[i]), sizeof(*found));
				return found;
			}
		}

	return NULL;
}

static struct usb_find_devices *usb_check(__maybe_unused struct device_drv *drv, __maybe_unused struct libusb_device *dev)
{
#ifdef USE_BITFORCE
	if (drv->drv == DRIVER_BITFORCE)
		return usb_check_each(DRV_BITFORCE, drv, dev);
#endif

#ifdef USE_ICARUS
	if (drv->drv == DRIVER_ICARUS)
		return usb_check_each(DRV_ICARUS, drv, dev);
#endif

#ifdef USE_MODMINER
	if (drv->drv == DRIVER_MODMINER)
		return usb_check_each(DRV_MODMINER, drv, dev);
#endif

	return NULL;
}

void usb_detect(struct device_drv *drv, bool (*device_detect)(struct libusb_device *, struct usb_find_devices *))
{
	libusb_device **list;
	ssize_t count, i;
	struct usb_find_devices *found;

	cgusb_check_init();

	count = libusb_get_device_list(NULL, &list);
	if (count < 0) {
		applog(LOG_DEBUG, "USB scan devices: failed, err %d", count);
		return;
	}

	if (count == 0)
		applog(LOG_DEBUG, "USB scan devices: found no devices");

	for (i = 0; i < count; i++) {
		mutex_lock(list_lock);

		if (in_use(list[i], false))
			mutex_unlock(list_lock);
		else {
			add_used(list[i], false);

			mutex_unlock(list_lock);

			found = usb_check(drv, list[i]);
			if (!found)
				release_dev(list[i], true);
			else
				if (!device_detect(list[i], found))
					release_dev(list[i], true);
		}
	}

	libusb_free_device_list(list, 1);
}

// Set this to 0 to remove stats processing
#define DO_USB_STATS 1

#if DO_USB_STATS
#define USB_STATS(sgpu, sta, fin, err, cmd, seq) stats(cgpu, sta, fin, err, cmd, seq)
#define STATS_TIMEVAL(tv) gettimeofday(tv, NULL)
#else
#define USB_STATS(sgpu, sta, fin, err, cmd, seq)
#define STATS_TIMEVAL(tv)
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

	cgusb_check_init();

	if (next_stat == 0)
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

	cgpu->usbstat = ++next_stat;

	usb_stats = realloc(usb_stats, sizeof(*usb_stats) * next_stat);
	usb_stats[next_stat-1].name = cgpu->drv->name;
	usb_stats[next_stat-1].device_id = -1;
	usb_stats[next_stat-1].details = calloc(1, sizeof(struct cg_usb_stats_details) * C_MAX * 2);
	for (i = 1; i < C_MAX * 2; i += 2)
		usb_stats[next_stat-1].details[i].seq = 1;
}
#endif

void update_usb_stats(__maybe_unused struct cgpu_info *cgpu)
{
#if DO_USB_STATS
	if (cgpu->usbstat < 1)
		newstats(cgpu);

	// we don't know the device_id until after add_cgpu()
	usb_stats[cgpu->usbstat - 1].device_id = cgpu->device_id;
#endif
}

#if DO_USB_STATS
static void stats(struct cgpu_info *cgpu, struct timeval *tv_start, struct timeval *tv_finish, int err, enum usb_cmds cmd, int seq)
{
	struct cg_usb_stats_details *details;
	double diff;
	int item;

	if (cgpu->usbstat < 1)
		newstats(cgpu);

	details = &(usb_stats[cgpu->usbstat - 1].details[cmd * 2 + seq]);

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
	memcpy(&(details->item[item].last), tv_start, sizeof(tv_start));
	details->item[item].count++;
}

static void rejected_inc(struct cgpu_info *cgpu)
{
	struct cg_usb_stats_details *details;
	int item = CMD_ERROR;

	if (cgpu->usbstat < 1)
		newstats(cgpu);

	details = &(usb_stats[cgpu->usbstat - 1].details[C_REJECTED * 2 + 0]);

	details->item[item].count++;
}
#endif

int _usb_read(struct cgpu_info *cgpu, int ep, char *buf, size_t bufsiz, int *processed, unsigned int timeout, int eol, enum usb_cmds cmd, bool ftdi)
{
	struct cg_usb_device *usbdev = cgpu->usbdev;
#if DO_USB_STATS
	struct timeval tv_start;
#endif
	struct timeval read_start, tv_finish;
	unsigned int initial_timeout;
	double max, done;
	int err, got, tot, i;
	bool first = true;

	if (cgpu->nodev) {
		*buf = '\0';
		*processed = 0;
#if DO_USB_STATS
		rejected_inc(cgpu);
#endif
		return LIBUSB_ERROR_NO_DEVICE;
	}

	if (timeout == DEVTIMEOUT)
		timeout = usbdev->found->timeout;

	if (eol == -1) {
		got = 0;
		STATS_TIMEVAL(&tv_start);
		err = libusb_bulk_transfer(usbdev->handle,
				usbdev->found->eps[ep].ep,
				(unsigned char *)buf,
				bufsiz, &got, timeout);
		STATS_TIMEVAL(&tv_finish);
		USB_STATS(cgpu, &tv_start, &tv_finish, err, cmd, SEQ0);

		if (ftdi) {
			// first 2 bytes returned are an FTDI status
			if (got > 2) {
				got -= 2;
				memmove(buf, buf+2, got+1);
			} else {
				got = 0;
				*buf = '\0';
			}
		}

		*processed = got;

		if (NODEV(err)) {
			cgpu->nodev = true;
			release_cgpu(cgpu);
		}

		return err;
	}

	tot = 0;
	err = LIBUSB_SUCCESS;
	initial_timeout = timeout;
	max = ((double)timeout) / 1000.0;
	gettimeofday(&read_start, NULL);
	while (bufsiz) {
		got = 0;
		STATS_TIMEVAL(&tv_start);
		err = libusb_bulk_transfer(usbdev->handle,
				usbdev->found->eps[ep].ep,
				(unsigned char *)buf,
				bufsiz, &got, timeout);
		gettimeofday(&tv_finish, NULL);
		USB_STATS(cgpu, &tv_start, &tv_finish, err, cmd, first ? SEQ0 : SEQ1);

		if (ftdi) {
			// first 2 bytes returned are an FTDI status
			if (got > 2) {
				got -= 2;
				memmove(buf, buf+2, got+1);
			} else {
				got = 0;
				*buf = '\0';
			}
		}

		tot += got;

		if (err)
			break;

		// WARNING - this will return data past EOL ('if' there is extra data)
		for (i = 0; i < got; i++)
			if (buf[i] == eol)
				goto goteol;

		buf += got;
		bufsiz -= got;

		first = false;

		done = tdiff(&tv_finish, &read_start);
		// N.B. this is return LIBUSB_SUCCESS with whatever size has already been read
		if (unlikely(done >= max))
			break;

		timeout = initial_timeout - (done * 1000);
	}

goteol:

	*processed = tot;

	if (NODEV(err)) {
		cgpu->nodev = true;
		release_cgpu(cgpu);
	}

	return err;
}

int _usb_write(struct cgpu_info *cgpu, int ep, char *buf, size_t bufsiz, int *processed, unsigned int timeout, enum usb_cmds cmd)
{
	struct cg_usb_device *usbdev = cgpu->usbdev;
#if DO_USB_STATS
	struct timeval tv_start, tv_finish;
#endif
	int err, sent;

	if (cgpu->nodev) {
		*processed = 0;
#if DO_USB_STATS
		rejected_inc(cgpu);
#endif
		return LIBUSB_ERROR_NO_DEVICE;
	}

	sent = 0;
	STATS_TIMEVAL(&tv_start);
	err = libusb_bulk_transfer(usbdev->handle,
			usbdev->found->eps[ep].ep,
			(unsigned char *)buf,
			bufsiz, &sent,
			timeout == DEVTIMEOUT ? usbdev->found->timeout : timeout);
	STATS_TIMEVAL(&tv_finish);
	USB_STATS(cgpu, &tv_start, &tv_finish, err, cmd, SEQ0);

	*processed = sent;

	if (NODEV(err)) {
		cgpu->nodev = true;
		release_cgpu(cgpu);
	}

	return err;
}

int _usb_transfer(struct cgpu_info *cgpu, uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, unsigned int timeout, enum usb_cmds cmd)
{
	struct cg_usb_device *usbdev = cgpu->usbdev;
#if DO_USB_STATS
	struct timeval tv_start, tv_finish;
#endif
	int err;

	if (cgpu->nodev) {
#if DO_USB_STATS
		rejected_inc(cgpu);
#endif
		return LIBUSB_ERROR_NO_DEVICE;
	}

	STATS_TIMEVAL(&tv_start);
	err = libusb_control_transfer(usbdev->handle, request_type,
		bRequest, wValue, wIndex, NULL, 0,
		timeout == DEVTIMEOUT ? usbdev->found->timeout : timeout);
	STATS_TIMEVAL(&tv_finish);
	USB_STATS(cgpu, &tv_start, &tv_finish, err, cmd, SEQ0);

	if (NODEV(err)) {
		cgpu->nodev = true;
		release_cgpu(cgpu);
	}

	return err;
}

void usb_cleanup()
{
	// TODO:
}
