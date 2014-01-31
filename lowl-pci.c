/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <utlist.h>

#include <sys/mman.h>

#include "logging.h"
#include "lowlevel.h"
#include "lowl-pci.h"
#include "miner.h"
#include "util.h"

static
struct lowlevel_device_info *pci_devinfo_scan()
{
	struct lowlevel_device_info *devinfo_list = NULL, *info;
	struct dirent *de;
	char filename[0x100] = "/sys/bus/pci/devices", buf[0x10];
	DIR * const D = opendir(filename);
	if (!D)
		return 0;
	char * const p = &filename[strlen(filename)], *devid;
	const size_t psz = sizeof(filename) - (p - filename);
	uint32_t vid, pid;
	size_t d_name_len;
	while ( (de = readdir(D)) )
	{
		d_name_len = strlen(de->d_name);
		snprintf(p, psz, "/%s/vendor", de->d_name);
		if (!bfg_slurp_file(buf, sizeof(buf), filename))
			continue;
		vid = strtoll(buf, NULL, 0);
		snprintf(p, psz, "/%s/device", de->d_name);
		if (!bfg_slurp_file(buf, sizeof(buf), filename))
			continue;
		pid = strtoll(buf, NULL, 0);
		devid = malloc(4 + d_name_len + 1);
		sprintf(devid, "pci:%s", de->d_name);
		
		info = malloc(sizeof(struct lowlevel_device_info));
		*info = (struct lowlevel_device_info){
			.lowl = &lowl_pci,
			.devid = devid,
			.path = strdup(de->d_name),
			.vid = vid,
			.pid = pid,
		};
		
		LL_PREPEND(devinfo_list, info);
	}
	closedir(D);
	return devinfo_list;
}

struct lowl_pci_interface {
	struct lowl_pci_handle *(*open)(const char *path, const struct _lowl_pci_config *);
	void (*close)(struct lowl_pci_handle *);
	const uint32_t *(*get_words)(struct lowl_pci_handle *, void *buf, size_t words, int bar, off_t);
	const void *(*get_data)(struct lowl_pci_handle *, void *buf, size_t, int bar, off_t);
	bool (*set_words)(struct lowl_pci_handle *, const uint32_t *, size_t, int bar, off_t);
	bool (*set_data)(struct lowl_pci_handle *, const void *, size_t, int bar, off_t);
};

struct lowl_pci_handle {
	const char *path;
	const struct lowl_pci_interface *lpi;
	uint32_t *bar[6];
	size_t barsz[6];
};

static
void lowl_pci_close_mmap(struct lowl_pci_handle * const lph)
{
	for (int i = 0; i < 6; ++i)
		if (lph->bar[i])
			munmap(lph->bar[i], lph->barsz[i]);
	free(lph);
}

static
const uint32_t *lowl_pci_get_words_mmap(struct lowl_pci_handle * const lph, void * const buf, const size_t words, const int bar, const off_t offset)
{
	return &lph->bar[bar][offset];
}

static
bool lowl_pci_set_words_mmap(struct lowl_pci_handle * const lph, const uint32_t *buf, const size_t words, const int bar, const off_t offset)
{
	uint32_t *dest = &lph->bar[bar][offset];
	for (int i = 0; i < words; ++i)
		*(dest++) = *(buf++);
	return true;
}

static
const void *lowl_pci_get_data_from_words(struct lowl_pci_handle * const lph, void * const bufp, const size_t sz, const int bar, const off_t offset)
{
	uint8_t * const buf = bufp;
	const off_t offset32 = offset / 4;
	const off_t offset8  = offset % 4;
	const size_t words = (sz + offset8 + 3) / 4;
	const uint32_t * const wdata = lowl_pci_get_words(lph, buf, words, bar, offset32);
	swap32tobe(buf, wdata, words);
	return &buf[offset8];
}

static
bool lowl_pci_set_data_in_words(struct lowl_pci_handle * const lph, const void * const bufp, size_t sz, const int bar, const off_t offset)
{
	const uint8_t *buf = bufp;
	const off_t offset32 = offset / 4;
	off_t offset8  = offset % 4;
	const size_t words = (sz + offset8 + 3) / 4;
	uint32_t wdata[words], *wdp = wdata;
	if (offset8)
	{
		const uint32_t * const p = lowl_pci_get_words(lph, wdata, 1, bar, offset32);
		if (unlikely(!p))
			return false;
		wdata[0] = *p >> (32 - (8 * offset8));
	}
	for ( ; sz; --sz)
	{
		*wdp = (*wdp << 8) | *(buf++);
		if (++offset8 == 4)
		{
			offset8 = 0;
			++wdp;
		}
	}
	if (offset8)
	{
		uint32_t u;
		const uint32_t * const p = lowl_pci_get_words(lph, &u, 1, bar, offset32 + words - 1);
		if (unlikely(!p))
			return false;
		const int n = 32 - (8 * offset8);
		wdp[0] <<= n;
		wdp[0] |= *p & ((1 << n) - 1);
	}
	return lowl_pci_set_words(lph, wdata, words, bar, offset32);
}

static const struct lowl_pci_interface lpi_uio;

static
int _file_mode_to_mmap_prot(const int mode)
{
	switch (mode)
	{
		case O_RDONLY:
			return PROT_READ;
		case O_WRONLY:
			return PROT_WRITE;
		case O_RDWR:
			return PROT_READ | PROT_WRITE;
		default:
			return -1;
	}
}

static
void *_uio_mmap_bar(const char * const path, const int bar, const size_t sz, const int prot)
{
	char buf[0x100];
	snprintf(buf, sizeof(buf), "/sys/bus/pci/devices/%s/resource%d", path, bar);
	const int fd = open(buf, O_RDWR);
	if (fd == -1)
		return MAP_FAILED;
	void * const rv = mmap(NULL, sz, prot, MAP_SHARED, fd, 0);
	close(fd);
	return rv;
}

struct lowl_pci_handle *lowl_pci_open_uio(const char * const path, const struct _lowl_pci_config * const barcfgs)
{
	struct lowl_pci_handle * const lph = malloc(sizeof(*lph));
	*lph = (struct lowl_pci_handle){
		.path = path,
		.lpi = &lpi_uio,
	};
	for (const struct _lowl_pci_config *barcfg = barcfgs; barcfg->bar != -1; ++barcfg)
	{
		const int barno = barcfg->bar;
		const int prot = _file_mode_to_mmap_prot(barcfg->mode);
		if (unlikely(prot == -1))
			goto err;
		lph->bar[barno] = _uio_mmap_bar(path, barno, barcfg->sz, prot);
		lph->barsz[barno] = barcfg->sz;
		if (lph->bar[barno] == MAP_FAILED)
		{
			applog(LOG_ERR, "mmap %s bar %d failed", path, barno);
			goto err;
		}
	}
	return lph;

err:
	for (int i = 0; i < 6; ++i)
		if (lph->bar[i])
			munmap(lph->bar[i], lph->barsz[i]);
	free(lph);
	return NULL;
}

static const struct lowl_pci_interface lpi_uio = {
	.open = lowl_pci_open_uio,
	.close = lowl_pci_close_mmap,
	.get_words = lowl_pci_get_words_mmap,
	.get_data  = lowl_pci_get_data_from_words,
	.set_words = lowl_pci_set_words_mmap,
	.set_data  = lowl_pci_set_data_in_words,
};

struct lowl_pci_handle *lowl_pci_open(const char * const path, const struct _lowl_pci_config * const barcfgs)
{
	return
		lpi_uio.open(path, barcfgs) ?:
		false;
}

const uint32_t *lowl_pci_get_words(struct lowl_pci_handle * const lph, void * const buf, const size_t words, const int bar, const off_t offset)
{
	return lph->lpi->get_words(lph, buf, words, bar, offset);
}

const void *lowl_pci_get_data(struct lowl_pci_handle * const lph, void * const buf, const size_t sz, const int bar, const off_t offset)
{
	return lph->lpi->get_data(lph, buf, sz, bar, offset);
}

bool lowl_pci_set_words(struct lowl_pci_handle * const lph, const uint32_t * const buf, const size_t words, const int bar, const off_t offset)
{
	return lph->lpi->set_words(lph, buf, words, bar, offset);
}

bool lowl_pci_set_data(struct lowl_pci_handle * const lph, const void * const buf, const size_t sz, const int bar, const off_t offset)
{
	return lph->lpi->set_data(lph, buf, sz, bar, offset);
}

void lowl_pci_close(struct lowl_pci_handle * const lph)
{
	return lph->lpi->close(lph);
}

struct lowlevel_driver lowl_pci = {
	.dname = "pci",
	.devinfo_scan = pci_devinfo_scan,
};
