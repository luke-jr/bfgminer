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


static const uint8_t futurebit_max_chips = 0x01;
#define FUTUREBIT_DEFAULT_FREQUENCY  104
#define FUTUREBIT_MIN_CLOCK          104
#define FUTUREBIT_MAX_CLOCK          400
// Number of seconds chip of 54 cores @ 352mhz takes to scan full range
#define FUTUREBIT_HASH_SPEED         4090.0
#define FUTUREBIT_MAX_NONCE          0xffffffff
#define FUTUREBIT_READ_SIZE            8
#define futurebit_max_clusters_per_chip  6
#define futurebit_max_cores_per_cluster  9
static const uint8_t futurebit_g_head[] = {
    0xd4, 0x59, 0x2d, 0x01, 0x1d, 0x01, 0x8e, 0xa7, 0x4e, 0xbb, 0x17, 0xb8, 0x06, 0x6b, 0x2a, 0x75,
    0x83, 0x99, 0xd5, 0xf1, 0x9b, 0x5c, 0x60, 0x73, 0xd0, 0x9b, 0x50, 0x0d, 0x92, 0x59, 0x82, 0xad,
    0xc4, 0xb3, 0xed, 0xd3, 0x52, 0xef, 0xe1, 0x46, 0x67, 0xa8, 0xca, 0x9f, 0x27, 0x9f, 0x63, 0x30,
    0xcc, 0xbb, 0xb9, 0x10, 0x3b, 0x9e, 0x3a, 0x53, 0x50, 0x76, 0x50, 0x52, 0x08, 0x1d, 0xdb, 0xae,
    0x89, 0x8f, 0x1e, 0xf6, 0xb8, 0xc6, 0x4f, 0x3b, 0xce, 0xf7, 0x15, 0xf6,    0,    0,    0,    1,
	   0,    0,    0,    1, 0x8e, 0xa7,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
	   0,    0,    0,    0,    0,    0,    0
};


BFG_REGISTER_DRIVER(futurebit_drv)

static const struct bfg_set_device_definition futurebit_set_device_funcs_probe[];

struct futurebit_chip {
    uint8_t chipid;
    uint8_t global_reg[8];
    uint16_t chip_mask[futurebit_max_clusters_per_chip];
    uint32_t clst_offset[futurebit_max_clusters_per_chip];
    unsigned active_cores;
    unsigned freq;
};

static
void futurebit_chip_init(struct futurebit_chip * const chip, const uint8_t chipid)
{
    *chip = (struct futurebit_chip){
        .chipid = chipid,
        .global_reg = {0, 4, 0x40, 0, 0, 0, 0, 1},
        .chip_mask = {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
        .clst_offset = {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000},
        .active_cores = 54,
        .freq = FUTUREBIT_DEFAULT_FREQUENCY,
    };
}

static
void futurebit_reset_board(const int fd)
{
 
    if(set_serial_rts(fd, BGV_HIGH) == BGV_ERROR)
        applog(LOG_DEBUG, "IOCTL RTS RESET FAILED");
    
    cgsleep_ms(100);
    
    if(set_serial_rts(fd, BGV_LOW) == BGV_ERROR)
        applog(LOG_DEBUG, "IOCTL RTS RESET FAILED");
}

static
void futurebit_set_diag_mode(struct futurebit_chip * const chip, bool diag_enable)
{
    if (diag_enable)
        chip->global_reg[1] |= 1;
    else
        chip->global_reg[1] &= ~1;
}

static
bool futurebit_write_global_reg(const int fd, const struct futurebit_chip * const chip)
{
    uint8_t buf[112];
    memset(&buf, 0, 102);
    memcpy(&buf[102], &chip->global_reg[0], 8);
    buf[110] = 0;
    buf[111] = 0xff;
    
    if (write(fd, buf, sizeof(buf)) != sizeof(buf))
        return false;
    return true;
}

static
bool futurebit_write_cluster_reg(const int fd, const struct futurebit_chip * const chip, const uint16_t cores_active, const uint32_t offset, const uint8_t clstid)
{
    uint8_t buf[112];
    memset(&buf, 0, 104);
    pk_u16be(buf, 104, cores_active);
    pk_u32be(buf, 106, offset);
    buf[110] = clstid;
    buf[111] = 0xfe;
    //applog(LOG_DEBUG, " %u: %u: %u : %u", buf[106], buf[107], buf[108], buf[109]);
    if (write(fd, buf, sizeof(buf)) != sizeof(buf))
        return false;
    return true;
}


static
bool futurebit_init_pll(const int fd, struct futurebit_chip * const chip)
{
    unsigned freq = chip->freq;
    uint8_t bytes1;
    uint8_t bytes2;
    uint8_t divider;
    
    if (freq <= 200){
        divider = (freq - 8)/8;
        divider <<= 1;
        bytes1 = 0x70 | ((divider & 0xf0) >> 4);
        bytes2 = 0x30 | ((divider & 0xf0) >> 4);
        
    }else {
        divider = (freq - 16)/16;
        divider <<= 1;
        bytes1 = 0x60 | ((divider & 0xf0) >> 4);
        bytes2 = 0x20 | ((divider & 0xf0) >> 4);
    }
    
    uint8_t bytes3 = 0x00 | ((divider & 0x0f) << 4);
    
    pk_u16be(chip->global_reg, 2, 0x4000);
    chip->global_reg[1] |= 0xc;
    if (!futurebit_write_global_reg(fd, chip))
        return false;
    
    chip->global_reg[2] = bytes1;
    chip->global_reg[3] = bytes3;
    cgsleep_ms(50);
    if (!futurebit_write_global_reg(fd, chip))
        return false;
    
    chip->global_reg[2] = bytes2;
    chip->global_reg[1] &= ~8;
    cgsleep_ms(50);
    if (!futurebit_write_global_reg(fd, chip))
        return false;
    
    chip->global_reg[1] &= ~4;
    cgsleep_ms(50);
    if (!futurebit_write_global_reg(fd, chip))
        return false;
    
    return true;
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
    uint8_t buf[112];
    uint8_t cmd[112];
    const uint8_t * const target = work->target;
    
    unsigned char swpdata[80];
    
    //buf[0] = 0;
    //memset(&buf[1], 0xff, 0x1f);
    memset(&buf[0], 0, 0x18);
    memcpy(&buf[24], &target[24], 0x8);
    
    swap32tobe(swpdata, work->data, 80/4);
    memcpy(&buf[32], swpdata, 80);
    
    for (int i = 0; i<112; i++) {
        cmd[i] = buf[111 - i];
    }
    
    if (write(device->device_fd, cmd, sizeof(cmd)) != sizeof(cmd))
        return false;
    
    work->blk.nonce = FUTUREBIT_MAX_NONCE;
    
    return true;
}

static
bool futurebit_detect_one(const char * const devpath)
{
    struct futurebit_chip *chips = NULL;
    unsigned total_cores = 0;
    
    const int fd = serial_open(devpath, 115200, 1, true);
    if (fd < 0)
        return_via_applog(err, , LOG_DEBUG, "%s: %s %s", futurebit_drv.dname, "Failed to open", devpath);
    
    applog(LOG_DEBUG, "%s: %s %s", futurebit_drv.dname, "Successfully opened", devpath);
    
    futurebit_reset_board(fd);
    
    // Init chips, setup PLL, and scan for good cores
    chips = malloc(futurebit_max_chips * sizeof(*chips));
    
    struct futurebit_chip * const dummy_chip = &chips[0];
    futurebit_chip_init(dummy_chip, 0);
    
    // pick up any user-defined settings passed in via --set
    drv_set_defaults(&futurebit_drv, futurebit_set_device_funcs_probe, dummy_chip, devpath, detectone_meta_info.serial, 1);
    
    unsigned freq = dummy_chip->freq;
    
    applog(LOG_DEBUG, "%s: %s %u mhz", futurebit_drv.dname, "Core clock set to", freq);
    
    {
        uint8_t buf[8];
        for (unsigned i = 0; i < futurebit_max_chips; ++i)
        {
            struct futurebit_chip * const chip = &chips[i];
            futurebit_chip_init(chip, i);
            chip->freq = freq;
            
            //chip->global_reg[1] = 0x05;
            //if (!futurebit_write_global_reg(fd, chip))
            //    return_via_applog(err, , LOG_DEBUG, "%s: Failed to (%s) %s", futurebit_drv.dname, "global", devpath);
            //cgsleep_ms(50);
            futurebit_set_diag_mode(chip, true);
            if (!futurebit_init_pll(fd, chip))
                return_via_applog(err, , LOG_DEBUG, "%s: Failed to (%s) %s", futurebit_drv.dname, "init PLL", devpath);
            cgsleep_ms(50);
            if (!futurebit_send_golden(fd, chip, futurebit_g_head, NULL))
                return_via_applog(err, , LOG_DEBUG, "%s: Failed to (%s) %s", futurebit_drv.dname, "send scan job", devpath);
            
            while (serial_read(fd, buf, 8) == 8)
            {
                
                const uint8_t clsid = buf[7];
                if (clsid >= futurebit_max_clusters_per_chip)
                    applog(LOG_DEBUG, "%s: Bad %s id (%u) during scan of %s chip %u", futurebit_drv.dname, "cluster", clsid, devpath, i);
                const uint8_t coreid = buf[6];
                if (coreid >= futurebit_max_cores_per_cluster)
                    applog(LOG_DEBUG, "%s: Bad %s id (%u) during scan of %s chip %u", futurebit_drv.dname, "core", coreid, devpath, i);
                
                if (buf[0] != 0xd9 || buf[1] != 0xeb || buf[2] != 0x86 || buf[3] != 0x63) {
                    //chips[i].chip_good[clsid][coreid] = false;
                    applog(LOG_DEBUG, "%s: Bad %s at core (%u) during scan of %s chip %u cluster %u", futurebit_drv.dname, "nonce", coreid, devpath, i, clsid);
                } else {
                    ++total_cores;
                    chips[i].chip_mask[clsid] |= (1 << coreid);
                }
            }
        }
    }
    
    applog(LOG_DEBUG, "%s: Identified %d cores on %s", futurebit_drv.dname, total_cores, devpath);
    
    if (total_cores == 0)
        goto err;
    
    futurebit_reset_board(fd);
    
    // config nonce ranges per cluster based on core responses
    unsigned mutiple = FUTUREBIT_MAX_NONCE / total_cores;
    uint32_t n_offset = 0x00000000;
    
    for (unsigned i = 0; i < futurebit_max_chips; ++i)
    {
        struct futurebit_chip * const chip = &chips[i];
        
        chips[i].active_cores = total_cores;
        
        //chip->global_reg[1] = 0x04;
        //if (!futurebit_write_global_reg(fd, chip))
        //return_via_applog(err, , LOG_DEBUG, "%s: Failed to (%s) %s", futurebit_drv.dname, "global", devpath);
        //cgsleep_ms(50);
        futurebit_set_diag_mode(chip, false);
        if (!futurebit_init_pll(fd, chip))
            return_via_applog(err, , LOG_DEBUG, "%s: Failed to (%s) %s", futurebit_drv.dname, "init PLL", devpath);
        cgsleep_ms(50);
        
        for (unsigned x = 0; x < futurebit_max_clusters_per_chip; ++x) {
            unsigned gc = 0;
            
            uint16_t core_mask = chips[i].chip_mask[x];
            chips[i].clst_offset[x] = n_offset;
            
            applog(LOG_DEBUG, "OFFSET %u MASK %u CHIP %u CLUSTER %u", n_offset, core_mask, i, x);
            
            if (!futurebit_write_cluster_reg(fd, chip, core_mask, n_offset, x))
                return_via_applog(err, , LOG_DEBUG, "%s: Failed to (%s) %s", futurebit_drv.dname, "send config register", devpath);
            
            for (unsigned z = 0; z < 15; ++z) {
                if (core_mask & 0x0001)
                    gc += 1;
                core_mask >>= 1;
            }
            
            n_offset += mutiple * gc;
            
            cgsleep_ms(50);
        }
    }
    
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
    
    return add_cgpu(cgpu);
    
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
    
    uint32_t nonce = *(uint32_t *)buf;
    nonce = bswap_32(nonce);
    
    submit_nonce(thr, work, nonce);
    
    // hashrate calc
    
    const uint8_t clstid = buf[7];
    uint32_t range = chips[0].clst_offset[clstid];

    struct timeval now_tv;
    timer_set_now(&now_tv);
    int elapsed_ms = ms_tdiff(&now_tv, &start_tv);
    
    double total_hashes = ((nonce - range)/9.0) * chips[0].active_cores;
    double hashes_per_ms = total_hashes/elapsed_ms;
    uint64_t hashes = hashes_per_ms * ms_tdiff(&now_tv, &thr->_tv_last_hashes_done_call);
    
    if(hashes_per_ms < 1500 && hashes < 100000000)
        hashes_done2(thr, hashes, NULL);
    else
        hashes_done2(thr, 100000, NULL);
}

// send work to the device
static
int64_t futurebit_scanhash(struct thr_info *thr, struct work *work, int64_t __maybe_unused max_nonce)
{
    struct cgpu_info *device = thr->cgpu;
    int fd = device->device_fd;
    struct futurebit_chip *chips = device->device_data;
    struct timeval start_tv, nonce_range_tv;
    
    // amount of time it takes this device to scan a nonce range:
    uint32_t nonce_full_range_sec = FUTUREBIT_HASH_SPEED * 352.0 / FUTUREBIT_DEFAULT_FREQUENCY * 54.0 / chips[0].active_cores;
    // timer to break out of scanning should we close in on an entire nonce range
    // should break out before the range is scanned, so we are doing 95% of the range
    uint64_t nonce_near_range_usec = (nonce_full_range_sec * 1000000. * 0.95);
    timer_set_delay_from_now(&nonce_range_tv, nonce_near_range_usec);
    
    // start the job
    timer_set_now(&start_tv);
    
    if (!futurebit_send_work(thr, work)) {
        applog(LOG_DEBUG, "Failed to start job");
        dev_error(device, REASON_DEV_COMMS_ERROR);
    }
    
    uint8_t buf[8];
    int read = 0;
    bool range_nearly_scanned = false;
    
    while (!thr->work_restart                                              // true when new work is available (miner.c)
           && ((read = serial_read(fd, buf, 8)) >= 0)                         // only check for failure - allow 0 bytes
           && !(range_nearly_scanned = timer_passed(&nonce_range_tv, NULL)))  // true when we've nearly scanned a nonce range
    {
        if (read == 0)
            continue;
        
        if (read == 8) {
            futurebit_submit_nonce(thr, buf, work, start_tv);
        }
        else
            applog(LOG_ERR, "%"PRIpreprv": Unrecognized response", device->proc_repr);
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
    
    if (val < FUTUREBIT_MIN_CLOCK || val > FUTUREBIT_MAX_CLOCK || (val%8)) {
        sprintf(replybuf, "invalid clock: '%s' valid range %d-%d. Clock must be a mutiple of 8 between 104-200mhz, and a mutiple of 16 between 208-400mhz",
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
