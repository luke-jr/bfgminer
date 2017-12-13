/*
 * Copyright 2015 John Stefanopoulos
 * Copyright 2014-2015 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */


#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <libusb.h>
#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "util.h"
#include <bwltc-commands.h>

static const uint8_t futurebit_max_chips = 0x01;
#define FUTUREBIT_DEFAULT_FREQUENCY  600
#define FUTUREBIT_MIN_CLOCK          384
#define FUTUREBIT_MAX_CLOCK          954
// Number of seconds chip of 64 cores @ 600mhz takes to scan full range
#define FUTUREBIT_HASH_SPEED         1300.0
#define FUTUREBIT_MAX_NONCE          0xffffffff
#define FUTUREBIT_READ_SIZE            8
//#define futurebit_max_clusters_per_chip  6
//#define futurebit_max_cores_per_cluster  9
unsigned char job2[] = {
0x3c, 0xff, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff,
0x07, 0x00, 0x00, 0x00, 0xd7, 0xa2, 0xea, 0xb0, 0xc2, 0xd7, 0x6f, 0x1e, 0x33, 0xa4, 0xb5, 0x3e,
0x0e, 0xb2, 0x84, 0x34, 0x89, 0x5a, 0x8b, 0x10, 0xfb, 0x19, 0x7d, 0x76, 0xe6, 0xe0, 0x38, 0x60,
0x15, 0x3f, 0x6a, 0x6e, 0x00, 0x00, 0x00, 0x04, 0xb5, 0x93, 0x93, 0x27, 0xf7, 0xc9, 0xfb, 0x26,
0xdf, 0x3b, 0xde, 0xc0, 0xa6, 0x6c, 0xae, 0x10, 0xb5, 0x53, 0xb7, 0x61, 0x5d, 0x67, 0xa4, 0x97,
0xe8, 0x7f, 0x06, 0xa6, 0x27, 0xfc, 0xd5, 0x57, 0x44, 0x38, 0xb8, 0x4d, 0xb1, 0xfe, 0x4f, 0x5f,
0x31, 0xaa, 0x47, 0x3d, 0x3d, 0xb4, 0xfc, 0x03, 0xa2, 0x78, 0x92, 0x44, 0xa1, 0x39, 0xb0, 0x35,
0xe1, 0x46, 0x04, 0x1e, 0x8c, 0x0a, 0xad, 0x28, 0x58, 0xec, 0x78, 0x3c, 0x1b, 0x00, 0xa4, 0x43
};



BFG_REGISTER_DRIVER(futurebit_drv)

static const struct bfg_set_device_definition futurebit_set_device_funcs_probe[];

struct futurebit_chip {
    uint8_t chipid;
    unsigned active_cores;
    unsigned freq;
    uint32_t last_nonce;
};

static
void futurebit_chip_init(struct futurebit_chip * const chip, const uint8_t chipid)
{
    *chip = (struct futurebit_chip){
        .chipid = chipid,
        .active_cores = 64,
        .freq = FUTUREBIT_DEFAULT_FREQUENCY,
        .last_nonce = 0x00000000,
    };
}

static
void futurebit_reset_board(const int fd)
{

    applog(LOG_DEBUG, "RESET START");
    if(set_serial_rts(fd, BGV_HIGH) == BGV_ERROR)
        applog(LOG_DEBUG, "IOCTL RTS RESET FAILED");

    cgsleep_ms(1000);

    if(set_serial_rts(fd, BGV_LOW) == BGV_ERROR)
        applog(LOG_DEBUG, "IOCTL RTS RESET FAILED");

    applog(LOG_DEBUG, "RESET END");
}

static
bool futurebit_write(const int fd,  const void *buf, size_t buflen)
{
	int repeat = 0;
	int size = 0;
	int nwrite = 0;

	//char output[(buflen * 2) + 1];
    //bin2hex(output, buf, buflen);
    //applog(LOG_DEBUG, "WRITE BUFFER %s", output);

	while(size < buflen)
	{
		nwrite = write(fd, buf, buflen);
        //applog(LOG_DEBUG, "FutureBit Write SIZE: %u", nwrite);
		if (nwrite < 0)
		{
			applog(LOG_ERR, "FutureBit Write error: %s", strerror(errno));
			return false;
		}

		size += nwrite;

		if (repeat++ > 1)
		{
			break;
		}

	}

	return true;
}

static
bool futurebit_read (const int fd, unsigned char *buf, int read_amount)
{

	ssize_t nread = 0;
	int size = 0;
	int repeat = 0;



	while(size < read_amount)
	{
		nread = read(fd, buf, read_amount);
		if(nread < 0)
            return false;

		size += nread;

		//char output[(read_amount * 2) + 1];
       // bin2hex(output, buf, read_amount);
        //applog(LOG_DEBUG, "READ BUFFER %s", output);

		if (repeat++ > 0)
		{
			break;
		}
	}

#if 0
	int i;
	for (i=0; i<size; i++)
	{
		printf("0x%02x ", buf[i]);
	}
	printf("\n");
#endif

	return true;
}

static
char futurebit_read_register(const int fd, uint32_t chip, uint32_t moudle, uint32_t RegAddr, int pos)
{
	uint8_t read_reg_data[8]={0};
	uint8_t read_reg_cmd[16]={0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0xc3};


	read_reg_cmd[1] = chip;
	read_reg_cmd[2] = moudle;
	read_reg_cmd[3] = 0x80|RegAddr; //read


	static int nonce=0;
	futurebit_write(fd, read_reg_cmd, 9);


	cgsleep_us(100000);
	if(!futurebit_read(fd, read_reg_data, 8))
        applog(LOG_DEBUG, "FutureBit read register fail");


    //applog(LOG_DEBUG, "FutureBit Read Return:");
    //for (int i=0; i<8; i++)
	//	{
	//		applog(LOG_DEBUG,"0x%02x ", read_reg_data[i]);
	//	}
    //applog(LOG_DEBUG,"\n");

    return read_reg_data[pos];
}

unsigned
int futurebit_write_register(const int fd, uint32_t chipId, uint32_t moudle, uint32_t Regaddr, uint32_t value)
{

	uint8_t read_reg_cmd[16]={0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0xc3};

	read_reg_cmd[1] = chipId;
	read_reg_cmd[2] = moudle;
	read_reg_cmd[3] = 0x7f&Regaddr; //&0x7f->write\BF\BFbit[7]:1 read, 0 write
	read_reg_cmd[4] = value&0xff;
	read_reg_cmd[5] = (value>>8)&0xff;
	read_reg_cmd[6] = (value>>16)&0xff;
	read_reg_cmd[7] = (value>>24)&0xff;




	return futurebit_write(fd, read_reg_cmd, 9);
}

static
void futurebit_send_cmds(const int fd, const unsigned char *cmds[])
{
	int i;
	for(i = 0; cmds[i] != NULL; i++)
	{
		futurebit_write(fd, cmds[i] + 1, cmds[i][0]);
		cgsleep_us(10000);
	}
}


static
void futurebit_set_frequency(const int fd, uint32_t freq)
{
        struct frequecy *p;
        unsigned char **cmd = cmd_set_600M;
        int i;

        for (i=0; i<ARRAY_LEN; i++)
        {
            if (fre_array[i].freq ==  freq)
            {
                cmd = fre_array[i].cmd;
            }
        }

        futurebit_send_cmds(fd, cmd);

}

static
bool futurebit_soft_reset(int fd)
{
    bool ret = false;
    do
    {
		if(!futurebit_write_register(fd, 0xff, 0xf8, 0x1e, 0x00))
		    break;
		cgsleep_us(50000);
		if(!futurebit_write_register(fd, 0xff, 0xf8, 0x1e, 0x03))
		    break;
		ret = true;
    }while(0);
    return ret;
}

unsigned char calculate_good_core(unsigned int reg_val)
{

		unsigned char goodCores=0;
		unsigned char i;
		unsigned int RegisterVal = reg_val;

		for(i=0; i<32; i++)
		{
				if (RegisterVal & 0x01)
						goodCores++;

				RegisterVal = RegisterVal>>1;
		}

		return goodCores;
}

unsigned
int futurebit_core_test(int fd, uint32_t freq_t)
{
	unsigned int i;
	unsigned int bist_value = 0;
	unsigned int goodcores0=0;
	unsigned int goodcores1=0;
    unsigned int  total = 0;
    unsigned int regval=0xff;
    int ret = -1;

    futurebit_send_cmds(fd, cmd_auto_address);
    cgsleep_us(100000);
    futurebit_set_frequency(fd, freq_t);
	cgsleep_us(100000);
    futurebit_write_register(fd, 0xff, 0xf8,0x22,0x11090005);//feed through
	cgsleep_us(100000);


    for (i=0; i<8; i++)
    {
        regval = 0x0f<<(4*i);
        if (!futurebit_write_register(fd, 0xff, 0xf8, 0x04, regval)) return ret;
        cgsleep_us(50000);
        if(!futurebit_write_register(fd, 0xff, 0xf8, 0x05, regval)) return ret;
        cgsleep_us(50000);

        do
        {
            if(!futurebit_write_register(fd, 0xff, 0x00, 0x3f, 0x00000020))//bist enable
                return ret;

            cgsleep_us(50000);
            if(!futurebit_write_register(fd, 0xff, 0x00, 0x23, 0xD799431B))//bist start, data2
                return ret;

            cgsleep_us(50000);

            goodcores0 = 0;
            goodcores1 = 0;

            bist_value = futurebit_read_register(fd, 0xff, 0x00, 0xbe, 4);

            if (bist_value > 0)
                goodcores0  = calculate_good_core(bist_value);

            bist_value = futurebit_read_register(fd, 0xff, 0x00, 0xbe, 4);

            if (bist_value > 0)
                goodcores1  = calculate_good_core(bist_value);

            ret += ( goodcores0+goodcores1);

        }while(0);

        if(!futurebit_soft_reset(fd)) return ret;//retset
        cgsleep_us(50000);
    }



	return ret;
}

void futurebit_config_all_chip(const int fd, uint32_t freq)
{
	uint32_t reg_val;
	int i;

	futurebit_reset_board(fd);

	futurebit_send_cmds(fd, cmd_auto_address);
	cgsleep_us(100000);
	//futurebit_set_baudrate(fd);
	//cgsleep_us(100000);
	futurebit_set_frequency(fd, freq);
	cgsleep_us(100000);
	futurebit_write_register(fd, 0xff, 0xf8,0x22,0x11090005);//feed through
	cgsleep_us(100000);

    //Start Nonce at zero for single chip
	//reg_val = 0xffffffff/futurebit_max_chips;
	//for (i=1; i<(futurebit_max_chips+1); i++)
	//{
    futurebit_write_register(fd, 0xff, 0x40, 0x00, 0x00000000);
    cgsleep_us(100000);
	//}

	futurebit_send_cmds(fd, gcp_cmd_reset);
	cgsleep_us(100000);


}

void futurebit_pull_up_payload(const int fd)
{
		char i;
		unsigned int regval = 0;

		//pull up payload by steps.
        for (i=0; i<8; i++)
        {
            regval  |= (0x0f<<(4*i));
            futurebit_write_register(fd, 0xff, 0xf8, 0x04, regval);
            cgsleep_us(35000);
            futurebit_write_register(fd, 0xff, 0xf8, 0x05, regval);
            cgsleep_us(35000);
            futurebit_write(fd, job2,144) ;
            cgsleep_us(35000);
        }
}


static
bool futurebit_send_golden(const int fd, const struct futurebit_chip * const chip, const void * const data, const void * const target_p)
{
    uint8_t buf[112];
    const uint8_t * const target = target_p;

    memcpy(buf, data, 80);
    if (target && !target[0x1f])
        memcpy(&buf[80], target, 0x20);
    else
    {
        memset(&buf[80], 0xff, 0x1f);
        buf[111] = 0;
    }

    //char output[(sizeof(buf) * 2) + 1];
    //bin2hex(output, buf, sizeof(buf));
    //applog(LOG_DEBUG, "GOLDEN OUTPUT %s", output);

    if (write(fd, buf, sizeof(buf)) != sizeof(buf))
        return false;
    return true;
}

static
bool futurebit_send_work(const struct thr_info * const thr, struct work * const work)
{
    struct cgpu_info *device = thr->cgpu;
    struct futurebit_chip *chips = device->device_data;

    uint32_t *pdata = work->data;
    uint32_t *midstate = work->midstate;
    const uint32_t ptarget[8];
	memset(ptarget, 0, 0x8);

    work->nonce_diff = 32./0x10000;            //set device diff low to keep accurate hashrate and device status

	if(work->work_difficulty < work->nonce_diff){
        work->nonce_diff = work->work_difficulty;
        set_target_to_pdiff(&ptarget, work->work_difficulty);
	}else
        set_target_to_pdiff(&ptarget, work->nonce_diff);

    //applog(LOG_DEBUG, "TARGET_DIFF %u", work->work_difficulty);

    int i, bpos;
    unsigned char bin[156];
    // swab for big endian
    uint32_t midstate2[8];
    uint32_t data2[20];
    uint32_t target2[8];
    for(i = 0; i < 19; i++)
    {
        data2[i] = htole32(pdata[i]);
        if(i >= 8) continue;
        target2[i] = htole32(ptarget[i]);
        midstate2[i] = htole32(midstate[i]);
    }


    data2[19] = 0;

    memset(bin, 0, sizeof(bin));
    bpos = 0;  memcpy(bin, "\x3c\xff\x40\x01", 4);
   // bpos += 4;  memcpy(bin + bpos, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\x00\x00", 32);     //target
    bpos += 4;  memcpy(bin + bpos, (unsigned char *)target2, 32);  memset(bin + bpos, 0, 24);
    bpos += 32; memcpy(bin + bpos, (unsigned char *)midstate2, 32);   //midstateno
    bpos += 32; memcpy(bin + bpos, (unsigned char *)data2, 76);		 //blockheader 76 bytes (ignore last 4bytes nounce)
    bpos += 76;

   /* char szVal[] = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\xff\x7f\x00\x00\x00fb357fbeda2ee2a93b841afac3e58173d4a97a400a84a4ec27c47ef5e9322ca620000000b99512c06534b34f62d0a88a5f90ac1857f0c02a1b6e6bb3185aec323b0eb79d2983a6d34c0e59272444dc28b1041e6114939ca8cdbd99f4058ef4965e293ba7598b98cc1a25e34f"; // source string

    char szOutput[144];

    size_t nLen = strlen(szVal);
    // Make sure it is even.
    if ((nLen % 2) == 1)
    {
        printf("Error string must be even number of digits %s", szVal);
    }

    // Process each set of characters as a single character.
    nLen >>= 1;
    for (size_t idx = 0; idx < nLen; idx++)
    {
        char acTmp[3];
        sscanf(szVal + (idx << 1), "%2s", acTmp);
        szOutput[idx] = (char)strtol(acTmp, NULL, 16);
    }
    */

    chips[0].last_nonce = 0x00000000;

    work->blk.nonce = FUTUREBIT_MAX_NONCE;

    return futurebit_write(device->device_fd, bin, 144);//144bytes
}

static
bool futurebit_detect_one(const char * const devpath)
{
    struct futurebit_chip *chips = NULL;
    unsigned total_cores = 0;
    uint32_t  regval = 0;

    const int fd = serial_open(devpath, 115200, 1, true);
    if (fd < 0)
        return_via_applog(err, , LOG_DEBUG, "%s: %s %s", futurebit_drv.dname, "Failed to open", devpath);

    applog(LOG_DEBUG, "%s: %s %s", futurebit_drv.dname, "Successfully opened", devpath);

    futurebit_reset_board(fd);

    if(futurebit_read_register(fd, 0xff, 0xf8, 0xa6, 0) != 0x3c)
        return_via_applog(err, , LOG_DEBUG, "%s: Failed to (%s) %s", futurebit_drv.dname, "find chip", devpath);



    // Init chips, setup PLL, and scan for good cores
    chips = malloc(futurebit_max_chips * sizeof(*chips));

    struct futurebit_chip * const dummy_chip = &chips[0];
    futurebit_chip_init(dummy_chip, 0);

    // pick up any user-defined settings passed in via --set
    drv_set_defaults(&futurebit_drv, futurebit_set_device_funcs_probe, dummy_chip, devpath, detectone_meta_info.serial, 1);

    unsigned freq = dummy_chip->freq;

    applog(LOG_DEBUG, "%s: %s %u mhz", futurebit_drv.dname, "Core clock set to", freq);


    struct futurebit_chip * const chip = &chips[0];
    futurebit_chip_init(chip, 0);
    chip->freq = freq;

    total_cores = futurebit_core_test(fd, freq);

    if(total_cores < 0)
        return_via_applog(err, , LOG_DEBUG, "%s: %s %s", futurebit_drv.dname, "Failed core detection", devpath);
    else if(total_cores < 60)
        applog(LOG_DEBUG, "%s: %s %u%s", futurebit_drv.dname, "Warning:", total_cores, "/64 detected");
    else
        applog(LOG_DEBUG, "%s: Identified %u cores on %s", futurebit_drv.dname, total_cores, devpath);


    if (serial_claim_v(devpath, &futurebit_drv))
    goto err;

    //serial_close(fd);
    struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
    *cgpu = (struct cgpu_info){
        .drv = &futurebit_drv,
        .device_path = strdup(devpath),
        .deven = DEV_ENABLED,
        .procs = 1,
        .threads = 1,
        .device_data = chips,
    };
    // NOTE: Xcode's clang has a bug where it cannot find fields inside anonymous unions (more details in fpgautils)
    cgpu->device_fd = fd;

    const bool ret = add_cgpu(cgpu);

    cgsleep_ms(cgpu->device_id*200);  //add small delay for devices > 0 so all devices dont start up at once

    //applog(LOG_DEBUG, "DEVICE ID %d", cgpu->device_id);

    futurebit_config_all_chip(fd, freq);
    futurebit_pull_up_payload(fd);


    return ret;

err:
    if (fd >= 0)
        serial_close(fd);
    free(chips);
    return false;
}

/*
 * scanhash mining loop
 */

static
void futurebit_submit_nonce(struct thr_info * const thr, const uint8_t buf[8], struct work * const work, struct timeval const start_tv)
{
    struct cgpu_info *device = thr->cgpu;
    struct futurebit_chip *chips = device->device_data;

    uint32_t nonce;
    uint32_t last_hashes;

    // swab for big endian
    memcpy((unsigned char *)&nonce, buf+4, 4);
    nonce = htole32(nonce);

    char output[(8 * 2) + 1];
    bin2hex(output, buf, 8);

    //applog(LOG_DEBUG, "NONCE %s", output);
    //applog(LOG_DEBUG, "NONCE int %u", nonce);
    //applog(LOG_DEBUG, "LAST NONCE int %u", chips[0].last_nonce);
    submit_nonce(thr, work, nonce);

    // hashrate calc

    last_hashes = (nonce+512)-chips[0].last_nonce;

    if(last_hashes > 0 && last_hashes < 0x4000000){
        hashes_done2(thr, last_hashes, NULL);
        chips[0].last_nonce = nonce;
    }else
       chips[0].last_nonce = nonce;

    if(chips[0].last_nonce == 0){
         hashes_done2(thr, 3200000*(chips[0].freq/600), NULL);
    }

}

// send work to the device
static
int64_t futurebit_scanhash(struct thr_info *thr, struct work *work, int64_t __maybe_unused max_nonce)
{
    struct cgpu_info *device = thr->cgpu;
    int fd = device->device_fd;
    struct futurebit_chip *chips = device->device_data;
    struct timeval start_tv, nonce_range_tv, last_submit_tv, now_tv;


    // amount of time it takes this device to scan a nonce range:
    uint32_t nonce_full_range_sec = FUTUREBIT_HASH_SPEED * FUTUREBIT_DEFAULT_FREQUENCY / chips[0].freq * 64.0 / chips[0].active_cores;
    // timer to break out of scanning should we close in on an entire nonce range
    // should break out before the range is scanned, so we are doing 95% of the range
    uint64_t nonce_near_range_usec = (nonce_full_range_sec * 1000000. * 0.95);
    timer_set_delay_from_now(&nonce_range_tv, nonce_near_range_usec);

    // start the job
    timer_set_now(&start_tv);
    timer_set_delay_from_now(&last_submit_tv, 10*1000000);

    cgsleep_ms(device->device_id*10 + 50);  //add small delay for devices > 0 so all devices dont start up at once

    if (!futurebit_send_work(thr, work)) {
        applog(LOG_DEBUG, "Failed to start job");
        dev_error(device, REASON_DEV_COMMS_ERROR);
    }

    unsigned char buf[12];
    int read = 0;
    bool range_nearly_scanned = false;
    bool no_asic_response = false;


    while (!thr->work_restart                                               // true when new work is available (miner.c)
           && !(no_asic_response = timer_passed(&last_submit_tv, NULL))     // check for core stall
           && ((read = serial_read(fd, buf, 8)) >= 0)                       // only check for failure - allow 0 bytes
           && !(range_nearly_scanned = timer_passed(&nonce_range_tv, NULL)))// true when we've nearly scanned a nonce range
    {
        if (read == 0)
            continue;

        if (read == 8) {
            futurebit_submit_nonce(thr, buf, work, start_tv);
            timer_set_delay_from_now(&last_submit_tv, 10*1000000);
        }
        else
            applog(LOG_ERR, "%"PRIpreprv": Unrecognized response", device->proc_repr);

    }

    if(no_asic_response){                             //asic is dead, lets attempt to restart it
        futurebit_reset_board(device->device_fd);
        futurebit_config_all_chip(fd, chips[0].freq);
        futurebit_pull_up_payload(fd);
        applog(LOG_ERR, "%s: ASIC has stopped hashing, attempting to restart", device->dev_repr);
    }

    if (read == -1)
    {
        applog(LOG_ERR, "%s: Failed to read result", device->dev_repr);
        dev_error(device, REASON_DEV_COMMS_ERROR);
    }

    return 0;
}

/*
 * setup & shutdown
 */

static
bool futurebit_lowl_probe(const struct lowlevel_device_info * const info)
{
    return vcom_lowl_probe_wrapper(info, futurebit_detect_one);
}

static
void futurebit_thread_shutdown(struct thr_info *thr)
{
    struct cgpu_info *device = thr->cgpu;

    futurebit_reset_board(device->device_fd);

    serial_close(device->device_fd);
}

/*
 * specify settings / options via RPC or command line
 */

// support for --set
// must be set before probing the device

// for setting clock and chips during probe / detect

static
const char *futurebit_set_clock(struct cgpu_info * const device, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
    struct futurebit_chip * const chip = device->device_data;
    int val = atoi(setting);

    if (val < FUTUREBIT_MIN_CLOCK || val > FUTUREBIT_MAX_CLOCK ) {
        sprintf(replybuf, "invalid clock: '%s' valid range %d-%d. Check the Moonlander 2 Support thread for list of valid clock speeds.",
                setting, FUTUREBIT_MIN_CLOCK, FUTUREBIT_MAX_CLOCK);
        return replybuf;
    } else
        chip->freq = val;

    return NULL;
}

static
const struct bfg_set_device_definition futurebit_set_device_funcs_probe[] = {
    { "clock", futurebit_set_clock, NULL },
    { NULL },
};

struct device_drv futurebit_drv = {
    .dname = "futurebit",
    .name = "MLD",
    .drv_min_nonce_diff = common_scrypt_min_nonce_diff,
    // detect device
    .lowl_probe = futurebit_lowl_probe,
    // specify mining type - scanhash
    .minerloop = minerloop_scanhash,

    // scanhash mining hooks
    .scanhash = futurebit_scanhash,

    // teardown device
    .thread_shutdown = futurebit_thread_shutdown,
};
