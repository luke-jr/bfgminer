/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#if defined(USE_VFIO) || defined(USE_UIO)
#	define USE_LOWL_PCI_MMAP
#	define USE_LOWL_PCI_DATA_WRAPPERS
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <utlist.h>

#ifdef USE_VFIO
#include <linux/vfio.h>
#include <sys/ioctl.h>
#endif

#ifdef USE_LOWL_PCI_MMAP
#include <sys/mman.h>
#endif

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
#ifdef USE_VFIO
	int fd[3];
	off_t baroff[6];
#endif
#ifdef USE_LOWL_PCI_MMAP
	volatile uint32_t *bar[6];
	size_t barsz[6];
#endif
};

#ifdef USE_LOWL_PCI_MMAP
static
void lowl_pci_close_mmap(struct lowl_pci_handle * const lph)
{
	for (int i = 0; i < 6; ++i)
		if (lph->bar[i])
			munmap((void*)lph->bar[i], lph->barsz[i]);
	free(lph);
}

static
const uint32_t *lowl_pci_get_words_mmap(struct lowl_pci_handle * const lph, void * const buf, const size_t words, const int bar, const off_t offset)
{
	volatile uint32_t *src = &lph->bar[bar][offset];
	uint32_t *dest = buf;
	for (int i = 0; i < words; ++i)
		*(dest++) = *(src++);
	return buf;
}

static
bool lowl_pci_set_words_mmap(struct lowl_pci_handle * const lph, const uint32_t *buf, const size_t words, const int bar, const off_t offset)
{
	volatile uint32_t *dest = &lph->bar[bar][offset];
	for (int i = 0; i < words; ++i)
		*(dest++) = *(buf++);
	return true;
}

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
#endif

#ifdef USE_LOWL_PCI_DATA_WRAPPERS
static
const void *lowl_pci_get_data_from_words(struct lowl_pci_handle * const lph, void * const bufp, const size_t sz, const int bar, const off_t offset)
{
	uint8_t * const buf = bufp;
	const off_t offset32 = offset / 4;
	const off_t offset8  = offset % 4;
	const size_t words = (sz + offset8 + 3) / 4;
	const uint32_t * const wdata = lowl_pci_get_words(lph, buf, words, bar, offset32);
	swap32tobe((uint32_t *)buf, wdata, words);
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
#endif

#ifdef USE_UIO
static const struct lowl_pci_interface lpi_uio;

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
			munmap((void*)lph->bar[i], lph->barsz[i]);
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
#endif

#ifdef USE_VFIO
static const struct lowl_pci_interface lpi_vfio;

#define _VFIO_ACCESS_BAR_PROBLEM ((void*)&lpi_vfio)
static
void *_vfio_access_bar(const int device, const int bar, const size_t sz, const int prot, off_t *out_offset)
{
	struct vfio_region_info region_info = { .argsz = sizeof(region_info) };
	switch (bar)
	{
#define _BARCASE(n)  \
		case n:  \
			region_info.index = VFIO_PCI_BAR ## n ## _REGION_INDEX;  \
			break;
		_BARCASE(0) _BARCASE(1) _BARCASE(2) _BARCASE(3)
		_BARCASE(4) _BARCASE(5)
#undef _BARCASE
		default:
			return _VFIO_ACCESS_BAR_PROBLEM;
	}
	if (ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &region_info))
		applogr(_VFIO_ACCESS_BAR_PROBLEM, LOG_ERR, "%s: VFIO_DEVICE_GET_REGION_INFO failed", __func__);
	if ((prot & PROT_READ ) && !(region_info.flags & VFIO_REGION_INFO_FLAG_READ ))
		applogr(_VFIO_ACCESS_BAR_PROBLEM, LOG_ERR, "%s: region does not support %s", __func__, "read");
	if ((prot & PROT_WRITE) && !(region_info.flags & VFIO_REGION_INFO_FLAG_WRITE))
		applogr(_VFIO_ACCESS_BAR_PROBLEM, LOG_ERR, "%s: region does not support %s", __func__, "write");
	if (region_info.size < sz)
		applogr(_VFIO_ACCESS_BAR_PROBLEM, LOG_ERR, "%s: region is only %lu bytes (needed %lu)",
		        __func__, (unsigned long)region_info.size, (unsigned long)sz);
	
	*out_offset = region_info.offset;
	if (!(region_info.flags & VFIO_REGION_INFO_FLAG_MMAP))
		return MAP_FAILED;
	
	return mmap(NULL, sz, prot, MAP_SHARED, device, region_info.offset);
}

struct lowl_pci_handle *lowl_pci_open_vfio(const char * const path, const struct _lowl_pci_config * const barcfgs)
{
	char buf[0x100], buf2[0x100];
	ssize_t ss;
	char *p;
	int group = -1, device = -1;
	off_t offset;
	
	struct lowl_pci_handle * const lph = malloc(sizeof(*lph));
	*lph = (struct lowl_pci_handle){
		.path = path,
		.lpi = &lpi_vfio,
	};
	const char * const vfio_path = "/dev/vfio/vfio";
	const int container = open(vfio_path, O_RDWR);
	if (container == -1)
	{
		applog(LOG_ERR, "%s: Failed to open %s", __func__, vfio_path);
		goto err;
	}
	{
		const int vfio_ver = ioctl(container, VFIO_GET_API_VERSION);
		if (vfio_ver != VFIO_API_VERSION)
		{
			applog(LOG_ERR, "%s: vfio API version mismatch (have=%d expect=%d)",
			       __func__, vfio_ver, VFIO_API_VERSION);
			goto err;
		}
	}
	snprintf(buf, sizeof(buf), "/sys/bus/pci/devices/%s/iommu_group", path);
	ss = readlink(buf, buf2, sizeof(buf2) - 1);
	if (ss == -1)
	{
		applog(LOG_ERR, "%s: Failed to read %s", __func__, buf);
		goto err;
	}
	buf2[ss] = '\0';
	p = memrchr(buf2, '/', ss - 1);
	if (p)
		++p;
	else
		p = buf2;
	snprintf(buf, sizeof(buf), "/dev/vfio/%s", p);
	group = open(buf, O_RDWR);
	if (group == -1)
	{
		applog(LOG_ERR, "%s: Failed to open %s", __func__, buf);
		goto err;
	}
	struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
	if (ioctl(group, VFIO_GROUP_GET_STATUS, &group_status))
	{
		applog(LOG_ERR, "%s: VFIO_GROUP_GET_STATUS failed on iommu group %s", __func__, p);
		goto err;
	}
	if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE))
	{
		applog(LOG_ERR, "%s: iommu group %s is not viable", __func__, p);
		goto err;
	}
	if (ioctl(group, VFIO_GROUP_SET_CONTAINER, &container))
	{
		applog(LOG_ERR, "%s: VFIO_GROUP_SET_CONTAINER failed on iommu group %s", __func__, p);
		goto err;
	}
	if (ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU))
	{
		applog(LOG_ERR, "%s: Failed to set type1 iommu on group %s", __func__, p);
		goto err;
	}
	device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, path);
	if (device == -1)
	{
		applog(LOG_ERR, "%s: Failed to get device fd for %s in group %s", __func__, path, p);
		goto err;
	}
	for (const struct _lowl_pci_config *barcfg = barcfgs; barcfg->bar != -1; ++barcfg)
	{
		const int barno = barcfg->bar;
		const int prot = _file_mode_to_mmap_prot(barcfg->mode);
		if (unlikely(prot == -1))
			goto err;
		lph->bar[barno] = _vfio_access_bar(device, barno, barcfg->sz, prot, &offset);
		lph->barsz[barno] = barcfg->sz;
		if (lph->bar[barno] == _VFIO_ACCESS_BAR_PROBLEM)
		{
			applog(LOG_ERR, "%s: Accessing %s bar %d failed", __func__, path, barno);
			goto err;
		}
		else
		if (lph->bar[barno] == MAP_FAILED)
			lph->bar[barno] = NULL;
		lph->baroff[barno] = offset;
	}
	lph->fd[0] = device;
	lph->fd[1] = group;
	lph->fd[2] = container;
	return lph;

err:
	for (int i = 0; i < 6; ++i)
		if (lph->bar[i])
			munmap((void*)lph->bar[i], lph->barsz[i]);
	if (device != -1)
		close(device);
	if (group != -1)
		close(group);
	if (container != -1)
		close(container);
	free(lph);
	return NULL;
}

static
void lowl_pci_close_vfio(struct lowl_pci_handle * const lph)
{
	close(lph->fd[0]);
	close(lph->fd[1]);
	close(lph->fd[2]);
	lowl_pci_close_mmap(lph);
}

static
const uint32_t *lowl_pci_get_words_vfio(struct lowl_pci_handle * const lph, void * const buf, const size_t words, const int bar, const off_t offset)
{
	if (lph->bar[bar])
		return lowl_pci_get_words_mmap(lph, buf, words, bar, offset);
	
	const size_t sz = 4 * words;
	if (unlikely(sz != pread(lph->fd[0], buf, sz, (4 * offset) + lph->baroff[bar])))
		return NULL;
	return buf;
}

static
bool lowl_pci_set_words_vfio(struct lowl_pci_handle * const lph, const uint32_t *buf, const size_t words, const int bar, const off_t offset)
{
	if (lph->bar[bar])
		return lowl_pci_set_words_mmap(lph, buf, words, bar, offset);
	
	const size_t sz = 4 * words;
	if (unlikely(sz != pwrite(lph->fd[0], buf, sz, (4 * offset) + lph->baroff[bar])))
		return false;
	return true;
}

static const struct lowl_pci_interface lpi_vfio = {
	.open = lowl_pci_open_vfio,
	.close = lowl_pci_close_vfio,
	.get_words = lowl_pci_get_words_vfio,
	.get_data  = lowl_pci_get_data_from_words,
	.set_words = lowl_pci_set_words_vfio,
	.set_data  = lowl_pci_set_data_in_words,
};
#endif

struct lowl_pci_handle *lowl_pci_open(const char * const path, const struct _lowl_pci_config * const barcfgs)
{
	return
#ifdef USE_VFIO
		lpi_vfio.open(path, barcfgs) ?:
#endif
#ifdef USE_UIO
		lpi_uio.open(path, barcfgs) ?:
#endif
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
	.exclude_from_all = true,
};
