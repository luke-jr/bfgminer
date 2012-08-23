/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#define _BSD_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libgen.h>
#include <arpa/inet.h>

#define BFL_FILE_MAGIC   "BFLDATA"
#define BFL_UPLOAD_MAGIC "NGH-STREAM"

#define myassert(expr, n, ...) \
do {  \
	if (!(expr)) {  \
		fprintf(stderr, __VA_ARGS__);  \
		return n;  \
	}  \
} while(0)

#define ERRRESP(buf)  buf, (buf[strlen(buf)-1] == '\n' ? "" : "\n")

#define WAITFOROK(n, msg) \
do {  \
	myassert(fgets(buf, sizeof(buf), BFL), n, "Error reading response from " msg "\n");  \
	myassert(!strcmp(buf, "OK\n"), n, "Invalid response from " msg ": %s%s", ERRRESP(buf));  \
} while(0)

int main(int argc, char**argv)
{
	myassert(argc == 3, 1, "Usage: %s <serialdev> <firmware.bfl>\n", argv[0]);
	setbuf(stdout, NULL);
	
	// Check filename
	char *FWname = basename(strdup(argv[2]));
	size_t FWnameLen = strlen(FWname);
	myassert(FWnameLen <= 255, 0x0f, "Firmware filename '%s' is too long\n", FWname);
	uint8_t n8 = FWnameLen;
	
	// Open and check firmware file
	FILE *FW = fopen(argv[2], "r");
	myassert(FW, 0x10, "Failed to open '%s' for reading\n", argv[2]);
	char buf[0x20];
	myassert(1 == fread(buf, 7, 1, FW), 0x10, "Failed to read from '%s'\n", argv[2]);
	myassert(!memcmp(buf, BFL_FILE_MAGIC, sizeof(BFL_FILE_MAGIC)-1), 0x11, "'%s' doesn't look like a BFL firmware\n", argv[2]);
	myassert(!fseek(FW, 0, SEEK_END), 0x12, "Failed to find end of '%s'\n", argv[2]);
	long FWlen = ftell(FW);
	myassert(FWlen > 0, 0x12, "Couldn't get size of '%s'\n", argv[2]);
	myassert(!fseek(FW, 7, SEEK_SET), 0x12, "Failed to rewind firmware file after getting size\n");
	FWlen -= 7;
	printf("Firmware file looks OK :)\n");
	
	// Open device
	FILE *BFL = fopen(argv[1], "r+");
	myassert(BFL, 0x20, "Failed to open '%s' for read/write\n", argv[1]);
	myassert(!setvbuf(BFL, NULL, _IOFBF, 1032), 0x21, "Failed to setup buffer for device");
	
	// ZAX: Start firmware upload
	printf("Starting firmware upload... ");
	myassert(1 == fwrite("ZAX", 3, 1, BFL), 0x22, "Failed to issue ZAX command\n");
	WAITFOROK(0x22, "ZAX");
	
	// Firmware upload header
	myassert(1 == fwrite(BFL_UPLOAD_MAGIC, sizeof(BFL_UPLOAD_MAGIC)-1, 1, BFL), 0x23, "Failed to send firmware upload header (magic)\n");
	uint32_t n32 = htonl(FWlen - FWlen / 6);
	myassert(1 == fwrite(&n32, sizeof(n32), 1, BFL), 0x23, "Failed to send firmware upload header (size)\n");
	myassert(1 == fwrite("\0\0", 2        , 1, BFL), 0x23, "Failed to send firmware upload header (padding 1)\n");
	myassert(1 == fwrite(&n8, sizeof(n8)  , 1, BFL), 0x23, "Failed to send firmware upload header (filename length)\n");
	myassert(1 == fwrite(FWname, n8       , 1, BFL), 0x23, "Failed to send firmware upload header (filename)\n");
	myassert(1 == fwrite("\0>>>>>>>>", 9  , 1, BFL), 0x23, "Failed to send firmware upload header (padding 2)\n");
	WAITFOROK(0x23, "firmware upload header");
	printf("OK, sending...\n");
	
	// Actual firmware upload
	long i, j;
	for (i = 0, j = 0; i < FWlen; ++i) {
		myassert(1 == fread(&n8, sizeof(n8), 1, FW), 0x30, "Error reading data from firmware file\n");
		if (5 == i % 6)
			continue;
		n8 ^= 0x2f;
		myassert(1 == fwrite(&n8, sizeof(n8), 1, BFL), 0x31, "Error sending data to device\n");
		if (!(++j % 0x400)) {
			myassert(1 == fwrite(">>>>>>>>", 8, 1, BFL), 0x32, "Error sending block-finish to device\n");
			printf("\r%5.2f%% complete", (double)i * 100. / (double)FWlen);
			WAITFOROK(0x32, "block-finish");
		}
	}
	printf("\r100%% complete :)\n");
	myassert(1 == fwrite(">>>>>>>>", 8, 1, BFL), 0x3f, "Error sending upload-finished to device\n");
	myassert(fgets(buf, sizeof(buf), BFL), 0x3f, "Error reading response from upload-finished\n");
	myassert(!strcmp(buf, "DONE\n"), 0x3f, "Invalid response from upload-finished: %s%s", ERRRESP(buf));

	// ZBX: Finish programming
	printf("Waiting for device... ");
	myassert(1 == fwrite("ZBX", 3, 1, BFL), 0x40, "Failed to issue ZBX command\n");
	WAITFOROK(0x40, "ZBX");
	printf("All done! Try mining to test the flash succeeded.\n");

	return 0;
}
