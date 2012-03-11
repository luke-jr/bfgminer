//#include <limits.h>
//#include <pthread.h>
//#include <stdio.h>
//#include <sys/time.h>
//#include <sys/types.h>
//#include <dirent.h>
//#include <unistd.h>
//#ifndef WIN32
//  #include <termios.h>
//  #include <sys/stat.h>
//  #include <fcntl.h>
//  #ifndef O_CLOEXEC
//    #define O_CLOEXEC 0
//  #endif
//#else
//  #include <windows.h>
//  #include <io.h>
//#endif

//#include "elist.h"
#include <unistd.h>

#include "miner.h"
#include "libztex.h"
//#include <libusb.h>

#define GOLDEN_BACKLOG 5

struct device_api ztex_api;


static void ztex_detect()
{
  int cnt;
  int i;
  struct libztex_dev_list **ztex_devices;

  cnt = libztex_scanDevices(&ztex_devices);
  applog(LOG_WARNING, "Found %d ztex board(s)", cnt);

  for (i=0; i<cnt; i++) {
    if (total_devices == MAX_DEVICES)
      break;
    struct cgpu_info *ztex;
    ztex = calloc(1, sizeof(struct cgpu_info));
    ztex->api = &ztex_api;
    ztex->device_id = total_devices;
    ztex->device = ztex_devices[i]->dev;
    //ztex->device_path = strdup(devpath);
    ztex->threads = 1;
    devices[total_devices++] = ztex;

    applog(LOG_WARNING,"%s: Found Ztex, mark as %d", ztex->device->repr, ztex->device_id);
  }

  if (cnt > 0) {
    libztex_freeDevList(ztex_devices);
  }

}

static bool ztex_prepare(struct thr_info *thr)
{
  struct timeval now;
  struct cgpu_info *ztex = thr->cgpu;

  gettimeofday(&now, NULL);
  get_datestamp(ztex->init, &now);

  if (libztex_configureFpga(ztex->device) != 0) {
    return false;
  }
  return true;
}

static uint64_t ztex_scanhash(struct thr_info *thr, struct work *work,
				__maybe_unused uint64_t max_nonce)
{
  struct libztex_device *ztex;
  unsigned char sendbuf[44];
  int i, j;
  uint32_t backlog[GOLDEN_BACKLOG];
  int backlog_p = 0;
  uint32_t lastnonce[GOLDEN_BACKLOG], nonce, noncecnt = 0;
  bool overflow, found, rv;
  struct libztex_hash_data hdata[GOLDEN_BACKLOG];

  ztex = thr->cgpu->device;

  memcpy(sendbuf, work->data + 64, 12);
  memcpy(sendbuf+12, work->midstate, 32);
  memset(backlog, 0, sizeof(backlog));
  libztex_sendHashData(ztex, sendbuf);
  //applog(LOG_WARNING, "sent hashdata");

  //hdata = malloc(sizeof(struct libztex_hash_data*)*ztex->numNonces);
  for (i=0; i<ztex->numNonces; i++) {
    //hdata[i] = malloc(sizeof(struct libztex_hash_data));
    lastnonce[i] = 0;
  }
  //sleep(1.00);
  overflow = false;
  while (!(overflow || work_restart[thr->id].restart)) {
    //sleep(0.5);
    libztex_readHashData(ztex, &hdata[0]);

    for (i=0; i<ztex->numNonces; i++) {
      nonce = hdata[i].nonce;
      if (nonce > noncecnt)
        noncecnt = nonce;
      if ((nonce >> 4) < (lastnonce[i] >> 4))
        overflow = true;
      else
        lastnonce[i] = nonce;
      nonce = hdata[i].goldenNonce;
      if (nonce > 0) {
        found = false;
        for (j=0; j<GOLDEN_BACKLOG; j++) {
          if (backlog[j] == nonce) {
            found = true;
            break;
          }
        }
        if (!found) {
          // new nonce found!
          backlog[backlog_p++] = nonce;
          if (backlog_p >= GOLDEN_BACKLOG) {
            backlog_p = 0;
          }
#ifdef __BIG_ENDIAN__
          nonce = swab32(nonce);
#endif
	  work->blk.nonce = 0xffffffff;
	  rv = submit_nonce(thr, work, nonce);
          applog(LOG_WARNING, "submitted %0.8X %d", nonce, rv);
        }
      }

//
//      //applog(LOG_WARNING, "%0.8X %0.8X %d", hdata[i]->nonce, hdata[i]->goldenNonce, work_restart[thr->id].restart);
    }

  }

  //for (i=0; i<ztex->numNonces; i++) {
  //  free(hdata[i]);
 // }
 // free(hdata);

  applog(LOG_WARNING, "exit %0.8X", noncecnt);
  return noncecnt;
}

static void ztex_shutdown(struct thr_info *thr)
{
  if (thr->cgpu) {
    libztex_destroy_device(thr->cgpu->device);
    thr->cgpu = NULL;
  } 
}

struct device_api ztex_api = {
	.name = "ZTX",
	.api_detect = ztex_detect,
	.thread_prepare = ztex_prepare,
	.scanhash = ztex_scanhash,
	.thread_shutdown = ztex_shutdown,
};
