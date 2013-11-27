/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deviceapi.h"
#include "logging.h"
#include "miner.h"
#include "util.h"

#define bailout(...)  do {  \
	applog(__VA_ARGS__);  \
	return NULL;  \
} while(0)

#define check_magic(L)  do {  \
	if (1 != fread(buf, 1, 1, f))  \
		bailout(LOG_ERR, "%s: Error reading bitstream ('%c')",  \
		        repr, L);  \
	if (buf[0] != L)  \
		bailout(LOG_ERR, "%s: Firmware has wrong magic ('%c')",  \
		        repr, L);  \
} while(0)

#define read_str(eng)  do {  \
	if (1 != fread(buf, 2, 1, f))  \
		bailout(LOG_ERR, "%s: Error reading bitstream (" eng " len)",  \
		        repr);  \
	len = (ubuf[0] << 8) | ubuf[1];  \
	if (len >= sizeof(buf))  \
		bailout(LOG_ERR, "%s: Firmware " eng " too long",  \
		        repr);  \
	if (1 != fread(buf, len, 1, f))  \
		bailout(LOG_ERR, "%s: Error reading bitstream (" eng ")",  \
		        repr);  \
	buf[len] = '\0';  \
} while(0)

void _bitstream_not_found(const char *repr, const char *fn)
{
	applog(LOG_ERR, "ERROR: Unable to load '%s', required for %s to work!", fn, repr);
	applog(LOG_ERR, "ERROR: Please read README.FPGA for instructions");
}

FILE *open_xilinx_bitstream(const char *dname, const char *repr, const char *fwfile, unsigned long *out_len)
{
	char buf[0x100];
	unsigned char *ubuf = (unsigned char*)buf;
	unsigned long len;
	char *p;

	FILE *f = open_bitstream(dname, fwfile);
	if (!f)
	{
		_bitstream_not_found(repr, fwfile);
		return NULL;
	}
	if (1 != fread(buf, 2, 1, f))
		bailout(LOG_ERR, "%s: Error reading bitstream (magic)",
		        repr);
	if (buf[0] || buf[1] != 9)
		bailout(LOG_ERR, "%s: Firmware has wrong magic (9)",
		        repr);
	if (-1 == fseek(f, 11, SEEK_CUR))
		bailout(LOG_ERR, "%s: Firmware seek failed",
		        repr);
	check_magic('a');
	read_str("design name");
	applog(LOG_DEBUG, "%s: Firmware file %s info:",
	       repr, fwfile);
	applog(LOG_DEBUG, "  Design name: %s", buf);
	p = strrchr(buf, ';') ?: buf;
	p = strrchr(buf, '=') ?: p;
	if (p[0] == '=')
		++p;
	unsigned long fwusercode = (unsigned long)strtoll(p, &p, 16);
	if (p[0] != '\0')
		bailout(LOG_ERR, "%s: Bad usercode in bitstream file",
		        repr);
	if (fwusercode == 0xffffffff)
		bailout(LOG_ERR, "%s: Firmware doesn't support user code",
		        repr);
	applog(LOG_DEBUG, "  Version: %u, build %u", (unsigned)((fwusercode >> 8) & 0xff), (unsigned)(fwusercode & 0xff));
	check_magic('b');
	read_str("part number");
	applog(LOG_DEBUG, "  Part number: %s", buf);
	check_magic('c');
	read_str("build date");
	applog(LOG_DEBUG, "  Build date: %s", buf);
	check_magic('d');
	read_str("build time");
	applog(LOG_DEBUG, "  Build time: %s", buf);
	check_magic('e');
	if (1 != fread(buf, 4, 1, f))
		bailout(LOG_ERR, "%s: Error reading bitstream (data len)",
		        repr);
	len = ((unsigned long)ubuf[0] << 24) | ((unsigned long)ubuf[1] << 16) | (ubuf[2] << 8) | ubuf[3];
	applog(LOG_DEBUG, "  Bitstream size: %lu", len);

	*out_len = len;
	return f;
}

bool load_bitstream_intelhex(bytes_t *rv, const char *dname, const char *repr, const char *fn)
{
	char buf[0x100];
	size_t sz;
	uint8_t xsz, xrt;
	uint16_t xaddr;
	FILE *F = open_bitstream(dname, fn);
	if (!F)
		return false;
	while (!feof(F))
	{
		if (unlikely(ferror(F)))
		{
			applog(LOG_ERR, "Error reading '%s'", fn);
			goto ihxerr;
		}
		if (!fgets(buf, sizeof(buf), F))
			goto ihxerr;
		if (unlikely(buf[0] != ':'))
			goto ihxerr;
		if (unlikely(!(
			hex2bin(&xsz, &buf[1], 1)
		 && hex2bin((unsigned char*)&xaddr, &buf[3], 2)
		 && hex2bin(&xrt, &buf[7], 1)
		)))
		{
			applog(LOG_ERR, "Error parsing in '%s'", fn);
			goto ihxerr;
		}
		switch (xrt)
		{
			case 0:  // data
				break;
			case 1:  // EOF
				fclose(F);
				return true;
			default:
				applog(LOG_ERR, "Unsupported record type in '%s'", fn);
				goto ihxerr;
		}
		xaddr = be16toh(xaddr);
		sz = bytes_len(rv);
		bytes_resize(rv, xaddr + xsz);
		if (sz < xaddr)
			memset(&bytes_buf(rv)[sz], 0xff, xaddr - sz);
		if (unlikely(!(hex2bin(&bytes_buf(rv)[xaddr], &buf[9], xsz))))
		{
			applog(LOG_ERR, "Error parsing data in '%s'", fn);
			goto ihxerr;
		}
		// TODO: checksum
	}
	
ihxerr:
	fclose(F);
	bytes_reset(rv);
	return false;
}

bool load_bitstream_bytes(bytes_t *rv, const char *dname, const char *repr, const char *fileprefix)
{
	FILE *F;
	size_t fplen = strlen(fileprefix);
	char fnbuf[fplen + 4 + 1];
	int e;
	
	bytes_reset(rv);
	memcpy(fnbuf, fileprefix, fplen);
	
	strcpy(&fnbuf[fplen], ".bin");
	F = open_bitstream(dname, fnbuf);
	if (F)
	{
		char buf[0x100];
		size_t sz;
		while ( (sz = fread(buf, 1, sizeof(buf), F)) )
			bytes_append(rv, buf, sz);
		e = ferror(F);
		fclose(F);
		if (unlikely(e))
		{
			applog(LOG_ERR, "Error reading '%s'", fnbuf);
			bytes_reset(rv);
		}
		else
			return true;
	}
	
	strcpy(&fnbuf[fplen], ".ihx");
	if (load_bitstream_intelhex(rv, dname, repr, fnbuf))
		return true;
	
	// TODO: Xilinx
	
	_bitstream_not_found(repr, fnbuf);
	return false;
}
