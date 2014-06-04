/*
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2012 Xiangfu
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 * Those code should be works fine with V2 and V3 bitstream of Zeus.
 * Operation:
 *   No detection implement.
 *   Input: 64B = 32B midstate + 20B fill bytes + last 12 bytes of block head.
 *   Return: send back 32bits immediately when Zeus found a valid nonce.
 *           no query protocol implemented here, if no data send back in ~11.3
 *           seconds (full cover time on 32bit nonce range by 380MH/s speed)
 *           just send another work.
 * Notice:
 *   1. Zeus will start calculate when you push a work to them, even they
 *      are busy.
 *   2. The 2 FPGAs on Zeus will distribute the job, one will calculate the
 *      0 ~ 7FFFFFFF, another one will cover the 80000000 ~ FFFFFFFF.
 *   3. It's possible for 2 FPGAs both find valid nonce in the meantime, the 2
 *      valid nonce will all be send back.
 *   4. Zeus will stop work when: a valid nonce has been found or 32 bits
 *      nonce range is completely calculated.
 */

#include "config.h"
#include "miner.h"

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#ifndef WIN32
  #include <termios.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #ifndef O_CLOEXEC
    #define O_CLOEXEC 0
  #endif
#else
  #include <windows.h>
  #include <io.h>
#endif
#ifdef HAVE_SYS_EPOLL_H
  #include <sys/epoll.h>
  #define HAVE_EPOLL
#endif
#include <math.h>

#include "compat.h"
#include "dynclock.h"
#include "driver-zeus.h"
#include "lowl-vcom.h"

#define CHIP_GEN1_CORES 8
#define CHIP_GEN 1
#define CHIP_CORES CHIP_GEN1_CORES

// Zeus definition
extern bool opt_ltc_debug;
extern int opt_chips_count;
extern int opt_chip_clk;
extern bool opt_ltc_nocheck_golden;
int zeus_opt_chips_count_max=1; 


// The serial I/O speed - Linux uses a define 'B115200' in bits/termios.h
#define ZEUS_IO_SPEED 115200

// The size of a successful nonce read
#define ZEUS_READ_SIZE 4

// The number of bytes in a nonce (always 4)
// This is NOT the read-size for the Zeus driver
// That is defined in ZEUS_INFO->read_size
#define ZEUS_NONCE_SIZE 4

#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

//#define ZEUS_READ_TIME(baud, read_size) ((double)read_size * (double)8.0 / (double)(baud))

// Defined in deciseconds
// There's no need to have this bigger, since the overhead/latency of extra work
// is pretty small once you get beyond a 10s nonce range time and 10s also
// means that nothing slower than 429MH/s can go idle so most zeus devices
// will always mine without idling
#define ZEUS_READ_COUNT_LIMIT_MAX 100

// In timing mode: Default starting value until an estimate can be obtained
// 5 seconds allows for up to a ~840MH/s device
#define ZEUS_READ_COUNT_TIMING	(5 * TIME_FACTOR)

// For a standard Zeus REV3
#define ZEUS_REV3_HASH_TIME 0.00000000264083

// Zeus Rev3 doesn't send a completion message when it finishes
// the full nonce range, so to avoid being idle we must abort the
// work (by starting a new work) shortly before it finishes
//
// Thus we need to estimate 2 things:
//	1) How many hashes were done if the work was aborted
//	2) How high can the timeout be before the Zeus is idle,
//		to minimise the number of work started
//	We set 2) to 'the calculated estimate' - 1
//	to ensure the estimate ends before idle
//
// The simple calculation used is:
//	Tn = Total time in seconds to calculate n hashes
//	Hs = seconds per hash
//	Xn = number of hashes
//	W  = code overhead per work
//
// Rough but reasonable estimate:
//	Tn = Hs * Xn + W	(of the form y = mx + b)
//
// Thus:
//	Line of best fit (using least squares)
//
//	Hs = (n*Sum(XiTi)-Sum(Xi)*Sum(Ti))/(n*Sum(Xi^2)-Sum(Xi)^2)
//	W = Sum(Ti)/n - (Hs*Sum(Xi))/n
//
// N.B. W is less when aborting work since we aren't waiting for the reply
//	to be transferred back (ZEUS_READ_TIME)
//	Calculating the hashes aborted at n seconds is thus just n/Hs
//	(though this is still a slight overestimate due to code delays)
//

// Both below must be exceeded to complete a set of data
// Minimum how long after the first, the last data point must be
#define HISTORY_SEC 60
// Minimum how many points a single ZEUS_HISTORY should have
#define MIN_DATA_COUNT 5
// The value above used is doubled each history until it exceeds:
#define MAX_MIN_DATA_COUNT 100

#if (TIME_FACTOR != 10)
#error TIME_FACTOR must be 10
#endif

static struct timeval history_sec = { HISTORY_SEC, 0 };

static const char *MODE_DEFAULT_STR = "default";
static const char *MODE_SHORT_STR = "short";
static const char *MODE_SHORT_STREQ = "short=";
static const char *MODE_LONG_STR = "long";
static const char *MODE_LONG_STREQ = "long=";
static const char *MODE_VALUE_STR = "value";
static const char *MODE_UNKNOWN_STR = "unknown";

#define END_CONDITION 0x0000ffff
#define DEFAULT_DETECT_THRESHOLD 1

BFG_REGISTER_DRIVER(zeus_drv)
extern const struct bfg_set_device_definition zeus_set_device_funcs[];

extern void convert_zeus_to_cairnsmore(struct cgpu_info *);

static inline
uint32_t zeus_nonce32toh(const struct ZEUS_INFO * const info, const uint32_t nonce)
{
	return info->nonce_littleendian ? le32toh(nonce) : be32toh(nonce);
}

uint8_t flush_buf[400];

void zeus_flush_uart(int fd)
{
#ifdef WIN32

//read(fd, flush_buf, 100);

	const HANDLE fh = (HANDLE)_get_osfhandle(fd);
	
	PurgeComm(fh, PURGE_RXCLEAR);
#else
	tcflush(fd, TCIFLUSH);
	//read(fd, flush_buf, 100);

#endif

}
	


int zeus_log_2(int value)   //·ÇµÝ¹éÅÐ¶ÏÒ»¸öÊýÊÇ2µÄ¶àÉÙ´Î·½  
{  
    int x=0;  
    while(value>1)  
    {  
        value>>=1;  
        x++;  
    }  
    return x;  
}  


uint32_t zeus_get_revindex(uint32_t value,int bit_num)
{

	uint32_t newvalue;
	int i;

	
#if CHIP_GEN==1
	value = (value&0x1ff80000)>>(29-bit_num);
#else
#error
#endif

	newvalue=0;

	for(i=0;i<bit_num;i++){
		newvalue = newvalue<<1;
		newvalue += value&0x01;
		value = value>>1;
	}
	return newvalue;
}

static void rev(unsigned char *s, size_t l)
{
	size_t i, j;
	unsigned char t;

	for (i = 0, j = l - 1; i < j; i++, j--) {
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}
}



#define zeus_open2(devpath, baud, purge)  serial_open(devpath, baud, ZEUS_READ_FAULT_DECISECONDS, purge)
#define zeus_open(devpath, baud)  zeus_open2(devpath, baud, false)

static
void zeus_log_protocol(int fd, const void *buf, size_t bufLen, const char *prefix)
{
	char hex[(bufLen * 2) + 1];
	bin2hex(hex, buf, bufLen);
	applog(LOG_DEBUG, "%s fd=%d: DEVPROTO: %s %s", zeus_drv.dname, fd, prefix, hex);
}

int zeus_gets(unsigned char *buf, int fd, struct timeval *tv_finish, struct thr_info *thr, int read_count, int read_size, uint32_t *elapsed_count)
{
	ssize_t ret = 0;
	int rc = 0;
	int epollfd = -1;
	int epoll_timeout = ZEUS_READ_FAULT_DECISECONDS * 100;
	int read_amount = read_size;
	bool first = true;
	
	*elapsed_count = 0;

#ifdef HAVE_EPOLL
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data.fd = fd,
	};
	struct epoll_event evr[2];
	if (thr && thr->work_restart_notifier[1] != -1) {
	epollfd = epoll_create(2);
	if (epollfd != -1) {
		if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev)) {
			close(epollfd);
			epollfd = -1;
		}
		{
			ev.data.fd = thr->work_restart_notifier[0];
			if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, thr->work_restart_notifier[0], &ev))
				applog(LOG_ERR, "%s: Error adding work restart fd to epoll", __func__);
			else
			{
				epoll_timeout *= read_count;
				read_count = 1;
			}
		}
	}
	else
		applog(LOG_ERR, "%s: Error creating epoll", __func__);
	}
#endif

	// Read reply 1 byte at a time to get earliest tv_finish
	while (true) {
#ifdef HAVE_EPOLL
		if (epollfd != -1 && (ret = epoll_wait(epollfd, evr, 2, epoll_timeout)) != -1)
		{
			if (ret == 1 && evr[0].data.fd == fd)
				ret = read(fd, buf, 1);
			else
			{
				if (ret)
					notifier_read(thr->work_restart_notifier);
				ret = 0;
			}
		}
		else
#endif
		ret = read(fd, buf, 1);
		if (ret < 0)
			return ICA_GETS_ERROR;

		if (first)
			cgtime(tv_finish);

		if (ret >= read_amount)
		{
			if (epollfd != -1)
				close(epollfd);

			if (opt_dev_protocol && opt_debug)
				zeus_log_protocol(fd, buf, read_size, "RECV");

			return ICA_GETS_OK;
		}

		if (ret > 0) {
			buf += ret;
			read_amount -= ret;
			first = false;
			continue;
		}
			
		if (thr && thr->work_restart) {
			if (epollfd != -1)
				close(epollfd);
			applog(LOG_DEBUG, "%s: Interrupted by work restart", __func__);
			return ICA_GETS_RESTART;
		}

		rc++;
		*elapsed_count=rc;
		if (rc >= read_count) {
			if (epollfd != -1)
				close(epollfd);
			applog(LOG_DEBUG, "%s: No data in %.2f seconds",
			       __func__,
			       (float)rc * epoll_timeout / 1000.);
			return ICA_GETS_TIMEOUT;
		}
	}
}

int zeus_write(int fd, const void *buf, size_t bufLen)
{
	size_t ret;
/*
	unsigned char ob_bin[84], nonce_bin[ZEUS_READ_SIZE];
	char nonce_hex[(sizeof(nonce_bin) * 2) + 1];
	#if 0
		char *hexstr;
		hexstr = bin2hex(nonce_hex, buf, bufLen);
		applog(LOG_WARNING, "zeus_write %s", hexstr);
		free(hexstr);
	#endif
*/
	if (opt_dev_protocol && opt_debug)
		zeus_log_protocol(fd, buf, bufLen, "SEND");

	if (unlikely(fd == -1))
		return 1;
	
	ret = write(fd, buf, bufLen);
	if (unlikely(ret != bufLen))
		return 1;

	return 0;
}

#define zeus_close(fd) serial_close(fd)

void do_zeus_close(struct thr_info *thr)
{
	struct cgpu_info *zeus = thr->cgpu;
	const int fd = zeus->device_fd;
	if (fd == -1)
		return;
	zeus_close(fd);
	zeus->device_fd = -1;
}

static const char *timing_mode_str(enum timing_mode timing_mode)
{
	switch(timing_mode) {
	case MODE_DEFAULT:
		return MODE_DEFAULT_STR;
	case MODE_SHORT:
		return MODE_SHORT_STR;
	case MODE_LONG:
		return MODE_LONG_STR;
	case MODE_VALUE:
		return MODE_VALUE_STR;
	default:
		return MODE_UNKNOWN_STR;
	}
}

static
const char *zeus_set_timing(struct cgpu_info * const proc, const char * const optname, const char * const buf, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ZEUS_INFO * const info = proc->device_data;
	double Hs;
	char *eq;

	if (strcasecmp(buf, MODE_SHORT_STR) == 0) {
		// short
		info->read_count = ZEUS_READ_COUNT_TIMING;
		info->read_count_limit = 0;  // 0 = no limit

		info->timing_mode = MODE_SHORT;
		info->do_zeus_timing = true;
	} else if (strncasecmp(buf, MODE_SHORT_STREQ, strlen(MODE_SHORT_STREQ)) == 0) {
		// short=limit
		info->read_count = ZEUS_READ_COUNT_TIMING;

		info->timing_mode = MODE_SHORT;
		info->do_zeus_timing = true;

		info->read_count_limit = atoi(&buf[strlen(MODE_SHORT_STREQ)]);
		if (info->read_count_limit < 0)
			info->read_count_limit = 0;
		if (info->read_count_limit > ZEUS_READ_COUNT_LIMIT_MAX)
			info->read_count_limit = ZEUS_READ_COUNT_LIMIT_MAX;
	} else if (strcasecmp(buf, MODE_LONG_STR) == 0) {
		// long
		info->read_count = ZEUS_READ_COUNT_TIMING;
		info->read_count_limit = 0;  // 0 = no limit

		info->timing_mode = MODE_LONG;
		info->do_zeus_timing = true;
	} else if (strncasecmp(buf, MODE_LONG_STREQ, strlen(MODE_LONG_STREQ)) == 0) {
		// long=limit
		info->read_count = ZEUS_READ_COUNT_TIMING;

		info->timing_mode = MODE_LONG;
		info->do_zeus_timing = true;

		info->read_count_limit = atoi(&buf[strlen(MODE_LONG_STREQ)]);
		if (info->read_count_limit < 0)
			info->read_count_limit = 0;
		if (info->read_count_limit > ZEUS_READ_COUNT_LIMIT_MAX)
			info->read_count_limit = ZEUS_READ_COUNT_LIMIT_MAX;
	} else if ((Hs = atof(buf)) != 0) {
		// ns[=read_count]
		info->Hs = Hs / NANOSEC;
		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		info->read_count = 0;
		if ((eq = strchr(buf, '=')) != NULL)
			info->read_count = atoi(eq+1);

		if (info->read_count < 1)
			info->read_count = (int)(info->fullnonce * TIME_FACTOR) - 1;

		if (unlikely(info->read_count < 1))
			info->read_count = 1;

		info->read_count_limit = 0;  // 0 = no limit
		
		info->timing_mode = MODE_VALUE;
		info->do_zeus_timing = false;
	} else {
		// Anything else in buf just uses DEFAULT mode

		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		info->read_count = 0;
		if ((eq = strchr(buf, '=')) != NULL)
			info->read_count = atoi(eq+1);

		int def_read_count = ZEUS_READ_COUNT_TIMING;

		if (info->timing_mode == MODE_DEFAULT) {
			if (proc->drv == &zeus_drv) {
				info->do_default_detection = 0x10;
			} else {
				def_read_count = (int)(info->fullnonce * TIME_FACTOR) - 1;
			}

			info->do_zeus_timing = false;
		}
		if (info->read_count < 1)
			info->read_count = def_read_count;
		
		info->read_count_limit = 0;  // 0 = no limit
	}

	info->min_data_count = MIN_DATA_COUNT;

	applog(LOG_DEBUG, "%"PRIpreprv": Init: mode=%s read_count=%d limit=%dms Hs=%e",
		proc->proc_repr,
		timing_mode_str(info->timing_mode),
		info->read_count, info->read_count_limit, info->Hs);
	
	return NULL;
}

static uint32_t mask(int work_division)
{
	uint32_t nonce_mask = 0x7fffffff;

	// yes we can calculate these, but this way it's easy to see what they are
	switch (work_division) {
	case 1:
		nonce_mask = 0xffffffff;
		break;
	case 2:
		nonce_mask = 0x7fffffff;
		break;
	case 4:
		nonce_mask = 0x3fffffff;
		break;
	case 8:
		nonce_mask = 0x1fffffff;
		break;
	default:
		quit(1, "Invalid2 work_division (%d) must be 1, 2, 4 or 8", work_division);
	}

	return nonce_mask;
}

// Number of bytes remaining after reading a nonce from Zeus
int zeus_excess_nonce_size(int fd, struct ZEUS_INFO *info)
{
	// How big a buffer?
	int excess_size = info->read_size - ZEUS_NONCE_SIZE;

	// Try to read one more to ensure the device doesn't return
	// more than we want for this driver
	excess_size++;

	unsigned char excess_bin[excess_size];
	// Read excess_size from Zeus
	struct timeval tv_now;
	timer_set_now(&tv_now);
	//zeus_gets(excess_bin, fd, &tv_now, NULL, 1, excess_size);
	int bytes_read = read(fd, excess_bin, excess_size);
	// Number of bytes that were still available

	return bytes_read;
}

/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
void zeus_suffix_string(uint64_t val, char *buf, int sigdigits)
{
	const double  dkilo = 1000.0;
	const uint64_t kilo = 1000ull;
	const uint64_t mega = 1000000ull;
	const uint64_t giga = 1000000000ull;
	const uint64_t tera = 1000000000000ull;
	const uint64_t peta = 1000000000000000ull;
	const uint64_t exa  = 1000000000000000000ull;
	char suffix[2] = "";
	bool decimal = true;
	double dval;

	if (val >= exa) {
		val /= peta;
		dval = (double)val / dkilo;
		sprintf(suffix, "E");
	} else if (val >= peta) {
		val /= tera;
		dval = (double)val / dkilo;
		sprintf(suffix, "P");
	} else if (val >= tera) {
		val /= giga;
		dval = (double)val / dkilo;
		sprintf(suffix, "T");
	} else if (val >= giga) {
		val /= mega;
		dval = (double)val / dkilo;
		sprintf(suffix, "G");
	} else if (val >= mega) {
		val /= kilo;
		dval = (double)val / dkilo;
		sprintf(suffix, "M");
	} else if (val >= kilo) {
		dval = (double)val / dkilo;
		sprintf(suffix, "K");
	} else {
		dval = val;
		decimal = false;
	}

	if (!sigdigits) {
		if (decimal)
			sprintf(buf, "%.3g%s", dval, suffix);
		else
			sprintf(buf, "%d%s", (unsigned int)dval, suffix);
	} else {
		/* Always show sigdigits + 1, padded on right with zeroes
		 * followed by suffix */
		int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

		sprintf(buf, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
	}
}

int zeus_update_num(int chips_count)
{
	int i;
	for (i=1;i<1024;i=i*2){
		if (chips_count<=i){
			return i;
		}
	}
	return 1024;
}

/**
 * See if we can detect a single instance of the Zeus chip.
 **/
static bool zeus_detect_one(const char *devpath)
{
	int this_option_offset = ++option_offset;

	struct ZEUS_INFO *info;
	int fd;
	struct timeval tv_start, tv_finish;
	struct timeval golden_tv;
	
	int numbytes = 84;
	
	int baud, cores_perchip, chips_count_max,chips_count;
	uint32_t clk_reg;
	uint32_t clk_reg_init;
	uint64_t golden_speed_percore;
	
	applog(LOG_DEBUG, "Looking for Zeus devices...");
#if 1	
	if(opt_chip_clk>(0xff*3/2)){
		opt_chip_clk = 0xff*3/2;
	}
	else if(opt_chip_clk<2){
		opt_chip_clk = 2;
	}

	clk_reg= (uint32_t)(opt_chip_clk*2/3);
#endif
		
	char clk_header_str[10];
		
	

#if 1
	char golden_ob[] =
		"55aa0001"
		"00038000063b0b1b028f32535e900609c15dc49a42b1d8492a6dd4f8f15295c989a1decf584a6aa93be26066d3185f55ef635b5865a7a79b7fa74121a6bb819da416328a9bd2f8cef72794bf02000000";

	char golden_ob2[] =
		"55aa00ff"
		"c00278894532091be6f16a5381ad33619dacb9e6a4a6e79956aac97b51112bfb93dc450b8fc765181a344b6244d42d78625f5c39463bbfdc10405ff711dc1222dd065b015ac9c2c66e28da7202000000";

	const char golden_nonce[] = "00038d26";
	const uint32_t golden_nonce_val = 0x00038d26;// 0xd26= 3366
#endif




	unsigned char ob_bin[84], nonce_bin[ZEUS_READ_SIZE];
	//char *nonce_hex;
	char nonce_hex[(sizeof(nonce_bin) * 2) + 1];


	baud = ZEUS_IO_SPEED;
	cores_perchip = CHIP_CORES;
	chips_count = opt_chips_count;
	if(chips_count>zeus_opt_chips_count_max){
		zeus_opt_chips_count_max = zeus_update_num(chips_count);
	}
	chips_count_max = zeus_opt_chips_count_max;


		applog(LOG_DEBUG, "Zeus Detect: Attempting to open %s", devpath);
		
		fd = zeus_open2(devpath, baud, true);
		if (unlikely(fd == -1)) {
			applog(LOG_ERR, "Zeus Detect: Failed to open %s", devpath);
			return false;
		}
		
	uint32_t clk_header;



	//from 150M step to the high or low speed. we need to add delay and resend to init chip


	if(clk_reg>(150*2/3)){
		clk_reg_init = 165*2/3;
	}
	else {
		clk_reg_init = 139*2/3;
	}

	
	zeus_flush_uart(fd);
		
	clk_header = (clk_reg_init<<24)+((0xff-clk_reg_init)<<16);
	sprintf(clk_header_str,"%08x",clk_header+0x01);
	memcpy(golden_ob2,clk_header_str,8);
		
	hex2bin(ob_bin, golden_ob2, numbytes);
	zeus_write(fd, ob_bin, numbytes);
	sleep(1);
	zeus_flush_uart(fd);
	zeus_write(fd, ob_bin, numbytes);
	sleep(1);
	zeus_flush_uart(fd);
	zeus_write(fd, ob_bin, numbytes);
	read(fd, flush_buf, 400);


	clk_header = (clk_reg<<24)+((0xff-clk_reg)<<16);
	sprintf(clk_header_str,"%08x",clk_header+0x01);
	memcpy(golden_ob2,clk_header_str,8);
		
	hex2bin(ob_bin, golden_ob2, numbytes);
	zeus_write(fd, ob_bin, numbytes);
	sleep(1);
	zeus_flush_uart(fd);
	zeus_write(fd, ob_bin, numbytes);
	sleep(1);
	zeus_flush_uart(fd);



	clk_header = (clk_reg<<24)+((0xff-clk_reg)<<16);
	sprintf(clk_header_str,"%08x",clk_header+1);
	memcpy(golden_ob,clk_header_str,8);


	if (opt_ltc_nocheck_golden==false){
		
		read(fd, flush_buf, 400);
		hex2bin(ob_bin, golden_ob, numbytes);	
		zeus_write(fd, ob_bin, numbytes);
		cgtime(&tv_start);

		memset(nonce_bin, 0, sizeof(nonce_bin));
		
		uint32_t elapsed_count;
		zeus_gets(nonce_bin, fd, &tv_finish, NULL, 50, ZEUS_READ_SIZE, &elapsed_count);
		zeus_close(fd);

		//nonce_hex = bin2hex(nonce_bin, sizeof(nonce_bin));
		bin2hex(nonce_hex, nonce_bin, sizeof(nonce_bin));
		if (strncmp(nonce_hex, golden_nonce, 8)) {
			applog(LOG_ERR,
				"Zeus Detect: "
				"Test failed at %s: get %s, should: %s",
				devpath, nonce_hex, golden_nonce);
			//free(nonce_hex);
			return false;
		}

		timersub(&tv_finish, &tv_start, &golden_tv);

		golden_speed_percore = (uint64_t)(((double)0xd26)/((double)(golden_tv.tv_sec) + ((double)(golden_tv.tv_usec))/((double)1000000)));

		if(opt_ltc_debug){
			applog(LOG_ERR,
				"[Test succeeded] at %s: got %s.",
					devpath, nonce_hex);
		}
		
		//free(nonce_hex);
	}
	else{
		
		zeus_close(fd);
		golden_speed_percore = (((opt_chip_clk*2)/3)*1024)/8;
	}

	/* We have a real Zeus! */
	struct cgpu_info *zeus;
	zeus = calloc(1, sizeof(struct cgpu_info));
	zeus->drv = &zeus_drv;
	zeus->device_path = strdup(devpath);
	zeus->device_fd = -1;
	zeus->threads = 1;
	add_cgpu(zeus);
	zeus_info = realloc(zeus_info, sizeof(struct ZEUS_INFO *) * (total_devices + 1));

	applog(LOG_INFO, "Found Zeus at %s, mark as %d",
		devpath, zeus->device_id);

	applog(LOG_DEBUG, "Zeus: Init: %d baud=%d cores_perchip=%d chips_count=%d",
		zeus->device_id, baud, cores_perchip, chips_count);

	// Since we are adding a new device on the end it needs to always be allocated
	zeus_info[zeus->device_id] = (struct ZEUS_INFO *)malloc(sizeof(struct ZEUS_INFO));
	if (unlikely(!(zeus_info[zeus->device_id])))
		quit(1, "Failed to malloc ZEUS_INFO");

	info = zeus_info[zeus->device_id];

	// Initialise everything to zero for a new device
	memset(info, 0, sizeof(struct ZEUS_INFO));

	info->check_num = 0x1234;
	info->baud = baud;
	info->cores_perchip = cores_perchip;
	info->chips_count = chips_count;
	info->chips_count_max= chips_count_max;
	if ((chips_count_max &(chips_count_max-1))!=0){
		quit(1, "chips_count_max  must be 2^n");
	}	
	info->chips_bit_num = zeus_log_2(chips_count_max);
	info->golden_speed_percore = golden_speed_percore;


	info->read_count = (uint32_t)((4294967296*10)/(cores_perchip*chips_count_max*golden_speed_percore*2));

	info->read_count = info->read_count*3/4;


	info->chip_clk=opt_chip_clk;
	
	info->clk_header=clk_header;
	
	zeus_suffix_string(golden_speed_percore, info->core_hash, 0);
	zeus_suffix_string(golden_speed_percore*cores_perchip, info->chip_hash, 0);
	zeus_suffix_string(golden_speed_percore*cores_perchip*chips_count, info->board_hash, 0);

	if(opt_ltc_debug){
		applog(LOG_ERR,
			"[Speed] %dMhz core|chip|board: [%s/s], [%s/s], [%s/s], readcount:%d,bitnum:%d ",
				info->chip_clk,info->core_hash,info->chip_hash,info->board_hash,info->read_count,info->chips_bit_num);

	}

	return true;
}

static
bool zeus_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, zeus_detect_one);
}

static bool zeus_prepare(struct thr_info *thr)
{
	struct cgpu_info *zeus = thr->cgpu;

	struct zeus_state *state;

	applog(LOG_DEBUG, "Called Zeus prepare..");
	thr->cgpu_data = state = calloc(1, sizeof(*state));
	state->firstrun = true;

#ifdef HAVE_EPOLL
	int epollfd = epoll_create(2);
	if (epollfd != -1)
	{
		close(epollfd);
		notifier_init(thr->work_restart_notifier);
	}
#endif

	zeus->min_nonce_diff = 1./0x10000;
	zeus->status = LIFE_INIT2;
	
	return true;
}

bool zeus_init(struct thr_info *thr)
{
	struct cgpu_info *zeus = thr->cgpu;
	struct ZEUS_INFO *info = zeus->device_data;
	struct zeus_state * const state = thr->cgpu_data;
	
	applog(LOG_DEBUG, "Opening ports..");
	int fd = zeus_open2(zeus->device_path, info->baud, true);
	applog(LOG_DEBUG, "Ports may be open...");
	zeus->device_fd = fd;
	if (unlikely(-1 == fd)) {
		applog(LOG_ERR, "%s: Failed to open %s",
		       zeus->dev_repr,
		       zeus->device_path);
		return false;
	}
	applog(LOG_INFO, "%s: Opened %s", zeus->dev_repr, zeus->device_path);
	
	BFGINIT(info->job_start_func, zeus_job_start);
	BFGINIT(state->ob_bin, calloc(1, info->ob_size));
	
	if (!info->work_division)
	{
		struct timeval tv_finish;
		
		// For reading the nonce from Zeus
		unsigned char res_bin[info->read_size];
		// For storing the the 32-bit nonce
		uint32_t res;
		
		applog(LOG_DEBUG, "%"PRIpreprv": Work division not specified - autodetecting", zeus->proc_repr);
		
		// Special packet to probe work_division
		unsigned char pkt[64] =
			"\x2e\x4c\x8f\x91\xfd\x59\x5d\x2d\x7e\xa2\x0a\xaa\xcb\x64\xa2\xa0"
			"\x43\x82\x86\x02\x77\xcf\x26\xb6\xa1\xee\x04\xc5\x6a\x5b\x50\x4a"
			"BFGMiner Probe\0\0"
			"BFG\0\x64\x61\x01\x1a\xc9\x06\xa9\x51\xfb\x9b\x3c\x73";
		
		zeus_write(fd, pkt, sizeof(pkt));
		memset(res_bin, 0, sizeof(res_bin));
		uint32_t elapsed_count;
		if (ICA_GETS_OK == zeus_gets(res_bin, fd, &tv_finish, NULL, info->read_count, info->read_size, &elapsed_count))
		{
			memcpy(&res, res_bin, sizeof(res));
			res = zeus_nonce32toh(info, res);
		}
		else
			res = 0;
		
		switch (res) {
			case 0x04C0FDB4:
				info->work_division = 1;
				break;
			case 0x82540E46:
				info->work_division = 2;
				break;
			case 0x417C0F36:
				info->work_division = 4;
				break;
			case 0x60C994D5:
				info->work_division = 8;
				break;
			default:
				applog(LOG_ERR, "%"PRIpreprv": Work division autodetection failed (assuming 2): got %08x", zeus->proc_repr, res);
				info->work_division = 2;
		}
		applog(LOG_DEBUG, "%"PRIpreprv": Work division autodetection got %08x (=%d)", zeus->proc_repr, res, info->work_division);
	}
	
	if (!info->fpga_count)
		info->fpga_count = info->work_division;
	
	info->nonce_mask = mask(info->work_division);
	
	return true;
}

static bool zeus_reopen(struct cgpu_info *zeus, struct zeus_state *state, int *fdp)
{
	struct ZEUS_INFO *info = zeus->device_data;

	// Reopen the serial port to workaround a USB-host-chipset-specific issue with the Zeus's buggy USB-UART
	do_zeus_close(zeus->thr[0]);
	*fdp = zeus->device_fd = zeus_open(zeus->device_path, info->baud);
	if (unlikely(-1 == *fdp)) {
		applog(LOG_ERR, "%"PRIpreprv": Failed to reopen on %s", zeus->proc_repr, zeus->device_path);
		dev_error(zeus, REASON_DEV_COMMS_ERROR);
		state->firstrun = true;
		return false;
	}
	return true;
}

static
bool zeus_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info * const zeus = thr->cgpu;
	struct zeus_state * const state = thr->cgpu_data;
	uint8_t * const ob_bin = state->ob_bin;
	
	applog(LOG_DEBUG, "Zeus : Preparing job...");
	swab256(ob_bin, work->midstate);
	bswap_96p(&ob_bin[0x34], &work->data[0x40]);
	if (!(memcmp(&ob_bin[56], "\xff\xff\xff\xff", 4)
	   || memcmp(&ob_bin, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32))) {
		// This sequence is used on cairnsmore bitstreams for commands, NEVER send it otherwise
		applog(LOG_WARNING, "%"PRIpreprv": Received job attempting to send a command, corrupting it!",
		       zeus->proc_repr);
		ob_bin[56] = 0;
	}
	
	return true;
}

bool zeus_job_start(struct thr_info *thr)
{
	struct cgpu_info *zeus = thr->cgpu;
	struct ZEUS_INFO *info = zeus->device_data;
	struct zeus_state *state = thr->cgpu_data;
	const uint8_t * const ob_bin = state->ob_bin;
	int fd = zeus->device_fd;
	int ret;

	// Handle dynamic clocking for "subclass" devices
	// This needs to run before sending next job, since it hashes the command too
	if (info->dclk.freqM && likely(!state->firstrun)) {
		dclk_preUpdate(&info->dclk);
		dclk_updateFreq(&info->dclk, info->dclk_change_clock_func, thr);
	}
	
	cgtime(&state->tv_workstart);

	ret = zeus_write(fd, ob_bin, info->ob_size);
	if (ret) {
		do_zeus_close(thr);
		applog(LOG_ERR, "%"PRIpreprv": Comms error (werr=%d)", zeus->proc_repr, ret);
		dev_error(zeus, REASON_DEV_COMMS_ERROR);
		return false;	/* This should never happen */
	}

	if (opt_debug) {
		char ob_hex[(info->ob_size * 2) + 1];
		bin2hex(ob_hex, ob_bin, info->ob_size);
		applog(LOG_DEBUG, "%"PRIpreprv" sent: %s",
			zeus->proc_repr,
			ob_hex);
	}

	return true;
}

static
struct work *zeus_process_worknonce(const struct ZEUS_INFO * const info, struct zeus_state *state, uint32_t *nonce)
{
	*nonce = zeus_nonce32toh(info, *nonce);
	if (test_nonce(state->last_work, *nonce, false))
		return state->last_work;
	if (likely(state->last2_work && test_nonce(state->last2_work, *nonce, false)))
		return state->last2_work;
	return NULL;
}

static
void handle_identify(struct thr_info * const thr, int ret, const bool was_first_run)
{
	const struct cgpu_info * const zeus = thr->cgpu;
	const struct ZEUS_INFO * const info = zeus->device_data;
	struct zeus_state * const state = thr->cgpu_data;
	int fd = zeus->device_fd;
	struct timeval tv_now;
	double delapsed;
	
	// For reading the nonce from Zeus
	unsigned char nonce_bin[info->read_size];
	// For storing the the 32-bit nonce
	uint32_t nonce;
	
	if (fd == -1)
		return;
	
	// If identify is requested (block erupters):
	// 1. Don't start the next job right away (above)
	// 2. Wait for the current job to complete 100%
	
	if (!was_first_run)
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Identify: Waiting for current job to finish", zeus->proc_repr);
		while (true)
		{
			cgtime(&tv_now);
			delapsed = tdiff(&tv_now, &state->tv_workstart);
			if (delapsed + 0.1 > info->fullnonce)
				break;
			
			// Try to get more nonces (ignoring work restart)
			memset(nonce_bin, 0, sizeof(nonce_bin));
			uint32_t elapsed_count;
			ret = zeus_gets(nonce_bin, fd, &tv_now, NULL, (info->fullnonce - delapsed) * 10, info->read_size, &elapsed_count);
			if (ret == ICA_GETS_OK)
			{
				memcpy(&nonce, nonce_bin, sizeof(nonce));
				nonce = zeus_nonce32toh(info, nonce);
				submit_nonce(thr, state->last_work, nonce);
			}
		}
	}
	else
		applog(LOG_DEBUG, "%"PRIpreprv": Identify: Current job should already be finished", zeus->proc_repr);
	
	// 3. Delay 3 more seconds
	applog(LOG_DEBUG, "%"PRIpreprv": Identify: Leaving idle for 3 seconds", zeus->proc_repr);
	cgsleep_ms(3000);
	
	// Check for work restart in the meantime
	if (thr->work_restart)
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Identify: Work restart requested during delay", zeus->proc_repr);
		goto no_job_start;
	}
	
	// 4. Start next job
	if (!state->firstrun)
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Identify: Starting next job", zeus->proc_repr);
		if (!info->job_start_func(thr))
no_job_start:
			state->firstrun = true;
	}
	
	state->identify = false;
}

static
void zeus_transition_work(struct zeus_state *state, struct work *work)
{
	if (state->last2_work)
		free_work(state->last2_work);
	state->last2_work = state->last_work;
	state->last_work = copy_work(work);
}

void update_chip_stat(struct ZEUS_INFO *info,uint32_t nonce);

static int64_t zeus_scanhash(struct thr_info *thr, struct work *work,
				__maybe_unused int64_t max_nonce)
{
	struct cgpu_info *zeus;
	int fd;
	int ret;

	struct ZEUS_INFO *info;
	int numbytes = 84;				// KRAMBLE 76 byte protocol
	unsigned char ob_bin[84], nonce_bin[ZEUS_READ_SIZE];
	char *ob_hex;

	struct work *nonce_work;
	uint32_t nonce;
	int64_t hash_count;
	uint32_t mask;
	struct timeval tv_start = {.tv_sec=0}, tv_finish, elapsed;
	struct timeval tv_history_start, tv_history_finish;
	double Ti, Xi;
	int curr_hw_errors, i;
	bool was_hw_error = false;
	bool was_first_run;

	struct ZEUS_HISTORY *history0, *history;
	int count;
	double Hs, W, fullnonce;
	//int read_count;
	bool limited;
	int64_t estimate_hashes;
	uint32_t values;
	int64_t hash_count_range;

	elapsed.tv_sec = elapsed.tv_usec = 0;

	zeus = thr->cgpu;
	struct zeus_state *state = thr->cgpu_data;
	was_first_run = state->firstrun;

	zeus->drv->job_prepare(thr, work, max_nonce);

	// Wait for the previous run's result
	fd = zeus->device_fd;
	info = zeus->device_data;
	
		uint32_t clock = info->clk_header;


	int diff = floor(work->share_diff);

	if(diff<info->chips_count){
		diff=info->chips_count;
	}
	
	
	uint32_t target_me = 0xffff/diff;

	uint32_t header = clock+target_me;
	
#if !defined (__BIG_ENDIAN__) && !defined(MIPSEB)
		header = header;

#else
		header = swab32(header);
#endif

	memcpy(ob_bin,(uint8_t *)&header,4);
	memcpy(&ob_bin[4], work->data, 80);	
	rev(ob_bin, 4);
	rev(ob_bin+4, 80);


	char nonce_hex[(sizeof(nonce_bin) * 2) + 1];
	if (opt_ltc_debug&1) {
		//ob_hex = bin2hex(ob_bin, sizeof(ob_bin));
		//ob_hex = bin2hex(nonce_hex, ob_bin, 8);
		bin2hex(nonce_hex, nonce_bin, sizeof(nonce_bin));
		applog(LOG_ERR, "Zeus %d nounce2 = %s readcount = %d try sent: %s",
			zeus->device_id,work->nonce2,info->read_count, nonce_hex);
		//free(nonce_hex);
	}


	read(fd, flush_buf, 400);
	ret = zeus_write(fd, ob_bin, 84); 
	if (ret) {
		do_zeus_close(thr);
		applog(LOG_ERR, "%s%i: Comms error", zeus->drv->name, zeus->device_id);
		dev_error(zeus, REASON_DEV_COMMS_ERROR);
		return 0;	/* This should never happen */
	}


	cgtime(&tv_start);


	/* Zeus will return 4 bytes (ZEUS_READ_SIZE) nonces or nothing */
	memset(nonce_bin, 0, sizeof(nonce_bin));


	if (opt_ltc_debug&0) {
		applog(LOG_ERR, "diff is %d",diff);
	}

	uint32_t elapsed_count;
	uint32_t read_count = info->read_count;
	while(1){
		ret = zeus_gets(nonce_bin, fd, &tv_finish, thr, info->read_count, info->read_size, &elapsed_count);
		if (ret == ICA_GETS_ERROR) {
			do_zeus_close(thr);
			applog(LOG_ERR, "%s%i: Comms error", zeus->drv->name, zeus->device_id);
			dev_error(zeus, REASON_DEV_COMMS_ERROR);
			return 0;
		}
#ifndef WIN32
//openwrt
		zeus_flush_uart(fd);
#endif

		work->blk.nonce = 0xffffffff;

		// aborted before becoming idle, get new work
		if (ret == ICA_GETS_TIMEOUT || ret == ICA_GETS_RESTART) {

			if (opt_ltc_debug&1) {
				applog(LOG_ERR, "1restart or 2timeout:%d ",ret);
			}


			timersub(&tv_finish, &tv_start, &elapsed);

			estimate_hashes = ((double)(elapsed.tv_sec) + ((double)(elapsed.tv_usec))/((double)1000000))
								* info->golden_speed_percore*info->chips_count*info->cores_perchip;

			if (unlikely(estimate_hashes > 0xffffffff))
				estimate_hashes = 0xffffffff;


			return estimate_hashes;
		}

		if(read_count>elapsed_count){
			read_count -= elapsed_count;
		}
		else {
			read_count=0;
		}

		memcpy((char *)&nonce, nonce_bin, sizeof(nonce_bin));
		//rev(nonce_bin,4);
#if !defined (__BIG_ENDIAN__) && !defined(MIPSEB)
		nonce = swab32(nonce);
#endif


		curr_hw_errors = zeus->hw_errors;

		submit_nonce(thr, work, nonce);
		
		was_hw_error = (curr_hw_errors < zeus->hw_errors);

		if (was_hw_error){
			
			zeus_flush_uart(fd);
			if (opt_ltc_debug&&1) {
				applog(LOG_ERR, "ERR nonce:%08x ",nonce);
			}
		}
		else {
			
			if (opt_ltc_debug&&0) {
#if CHIP_GEN==1
				uint32_t chip_index=zeus_get_revindex(nonce,info->chips_bit_num);
				uint32_t core_index=(nonce&0xe0000000)>>29;
#else
#error
#endif
				applog(LOG_ERR, "nonce:%08x,chip_index:%d ",nonce,chip_index);
			}

			

		}

	}

}

static struct api_data *zeus_drv_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct ZEUS_INFO *info = cgpu->device_data;

	// Warning, access to these is not locked - but we don't really
	// care since hashing performance is way more important than
	// locking access to displaying API debug 'stats'
	// If locking becomes an issue for any of them, use copy_data=true also
	root = api_add_int(root, "read_count", &(info->read_count), false);
	root = api_add_int(root, "read_count_limit", &(info->read_count_limit), false);
	root = api_add_double(root, "fullnonce", &(info->fullnonce), false);
	root = api_add_int(root, "count", &(info->count), false);
	root = api_add_hs(root, "Hs", &(info->Hs), false);
	root = api_add_double(root, "W", &(info->W), false);
	root = api_add_uint(root, "total_values", &(info->values), false);
	root = api_add_uint64(root, "range", &(info->hash_count_range), false);
	root = api_add_uint64(root, "history_count", &(info->history_count), false);
	root = api_add_timeval(root, "history_time", &(info->history_time), false);
	root = api_add_uint(root, "min_data_count", &(info->min_data_count), false);
	root = api_add_uint(root, "timing_values", &(info->history[0].values), false);
	root = api_add_const(root, "timing_mode", timing_mode_str(info->timing_mode), false);
	root = api_add_bool(root, "is_timing", &(info->do_zeus_timing), false);
	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "work_division", &(info->work_division), false);
	root = api_add_int(root, "fpga_count", &(info->fpga_count), false);
	root = api_add_string(root, "golden_speed_chip", info->chip_hash, false);
	root = api_add_int(root, "chipclk", &(info->chip_clk), false);
	root = api_add_int(root, "chips_count", &(info->chips_count), false);
	root = api_add_int(root, "chips_count_max", &(info->chips_count_max), false);
	root = api_add_uint32(root, "readcount", &(info->read_count), false);

	return root;
}

static
const char *zeus_set_baud(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ZEUS_INFO * const info = proc->device_data;
	const int baud = atoi(newvalue);
	if (!valid_baud(baud))
		return "Invalid baud setting";
	if (info->baud != baud)
	{
		info->baud = baud;
		info->reopen_now = true;
	}
	return NULL;
}

static
const char *zeus_set_probe_timeout(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ZEUS_INFO * const info = proc->device_data;
	info->probe_read_count = atof(newvalue) * 10.0 / ZEUS_READ_FAULT_DECISECONDS;
	return NULL;
}

static
const char *zeus_set_work_division(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ZEUS_INFO * const info = proc->device_data;
	const int work_division = atoi(newvalue);
	if (!(work_division == 1 || work_division == 2 || work_division == 4 || work_division == 8))
		return "Invalid work_division: must be 1, 2, 4 or 8";
	if (info->user_set & IUS_FPGA_COUNT)
	{
		if (info->fpga_count > work_division)
			return "work_division must be >= fpga_count";
	}
	else
		info->fpga_count = work_division;
	info->user_set |= IUS_WORK_DIVISION;
	info->work_division = work_division;
	info->nonce_mask = mask(work_division);
	return NULL;
}

static
const char *zeus_set_fpga_count(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ZEUS_INFO * const info = proc->device_data;
	const int fpga_count = atoi(newvalue);
	if (fpga_count < 1 || fpga_count > info->work_division)
		return "Invalid fpga_count: must be >0 and <=work_division";
	info->fpga_count = fpga_count;
	return NULL;
}

static
const char *zeus_set_reopen(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ZEUS_INFO * const info = proc->device_data;
	if ((!strcasecmp(newvalue, "never")) || !strcasecmp(newvalue, "-r"))
		info->reopen_mode = IRM_NEVER;
	else
	if (!strcasecmp(newvalue, "timeout"))
		info->reopen_mode = IRM_TIMEOUT;
	else
	if ((!strcasecmp(newvalue, "cycle")) || !strcasecmp(newvalue, "r"))
		info->reopen_mode = IRM_CYCLE;
	else
	if (!strcasecmp(newvalue, "now"))
		info->reopen_now = true;
	else
		return "Invalid reopen mode";
	return NULL;
}

static void zeus_shutdown(struct thr_info *thr)
{
  applog(LOG_DEBUG, "Shutting down...");
	do_zeus_close(thr);
	free(thr->cgpu_data);
}

const struct bfg_set_device_definition zeus_set_device_funcs[] = {
	// NOTE: Order of parameters below is important for --zeus-options
	{"baud"         , zeus_set_baud         , "serial baud rate"},
	{"work_division", zeus_set_work_division, "number of pieces work is split into"},
	{"fpga_count"   , zeus_set_fpga_count   , "number of chips working on pieces"},
	{"reopen"       , zeus_set_reopen       , "how often to reopen device: never, timeout, cycle, (or now for a one-shot reopen)"},
	// NOTE: Below here, order is irrelevant
	{"probe_timeout", zeus_set_probe_timeout},
	{"timing"       , zeus_set_timing       , "timing of device; see README.FPGA"},
	{NULL},
};

struct device_drv zeus_drv = {
	.dname = "zeus",
	.name = "ZUS",
	.supported_algos = POW_SCRYPT,
	//.max_diff = 32768,
	//.probe_priority = -115,

	// Detect device.
	.lowl_probe = zeus_lowl_probe,

	// ???
	//.get_api_stats = zeus_drv_stats,
	
	// Initalize device.
	.thread_prepare = zeus_prepare,

	// ???
	//.thread_init = zeus_init,

	.minerloop = minerloop_scanhash,

	// scanhash mining hooks
	.scanhash = zeus_scanhash,
	// XXX Missing prepare work?

	//.prepare_work = zeus_job_prepare,
	// ???
	.job_prepare = zeus_job_prepare,

	// ???
	//.thread_disable = close_device_fd,

	// teardown device
	.thread_shutdown = zeus_shutdown,

	// need specify settings/options?
};
