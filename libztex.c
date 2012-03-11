#include <stdio.h>
#include <unistd.h>
#include "miner.h"
#include "libztex.h"

#define BUFSIZE 256

//* Capability index for EEPROM support.
#define CAPABILITY_EEPROM 0,0
//* Capability index for FPGA configuration support. 
#define CAPABILITY_FPGA 0,1
//* Capability index for FLASH memory support.
#define CAPABILITY_FLASH 0,2
//* Capability index for DEBUG helper support.
#define CAPABILITY_DEBUG 0,3
//* Capability index for AVR XMEGA support.
#define CAPABILITY_XMEGA 0,4
//* Capability index for AVR XMEGA support.
#define CAPABILITY_HS_FPGA 0,5
//* Capability index for AVR XMEGA support.
#define CAPABILITY_MAC_EEPROM 0,6



static bool libztex_checkDevice (struct libusb_device *dev) {
  int err;

  struct libusb_device_descriptor desc;
  err = libusb_get_device_descriptor(dev, &desc);
  if (unlikely(err != 0)) {
    applog(LOG_ERR, "Ztex check device: Failed to open read descriptor with error %d", err);
    return false;
  }
  if (!(desc.idVendor == 0x221A && desc.idProduct == 0x0100)) {
    return false;
  }
  return true;
}

static bool libztex_checkCapability (struct libztex_device *ztex, int i, int j) {
  if (!((i>=0) && (i<=5) && (j>=0) && (j<8) &&
        (((ztex->interfaceCapabilities[i] & 255) & (1 << j)) != 0))) {
    applog(LOG_ERR, "%s: capability missing: %d %d", ztex->repr, i, i);
  }
  return true;
}

static int libztex_detectBitstreamBitOrder (const unsigned char *buf, int size) {
  int i;
  size -= 4;
  for (i=0; i<size; i++) {
    if ( ((buf[i] & 255)==0xaa) && ((buf[i+1] & 255)==0x99) && ((buf[i+2] & 255)==0x55) && ((buf[i+3] & 255)==0x66) )
      return 1;
    if ( ((buf[i] & 255)==0x55) && ((buf[i+1] & 255)==0x99) && ((buf[i+2] & 255)==0xaa) && ((buf[i+3] & 255)==0x66) )
      return 0;
  } 
  applog(LOG_WARNING, "Unable to determine bitstream bit order: no signature found");
  return 0;
}

static void libztex_swapBits (unsigned char *buf, int size) {
  int i;
  unsigned char c;
  for (i=0; i<size; i++) {
    c = buf[i];
    buf[i] = ((c & 128) >> 7) |
      ((c & 64) >> 5) |
      ((c & 32) >> 3) |
      ((c & 16) >> 1) |
      ((c & 8) << 1) |
      ((c & 4) << 3) |
      ((c & 2) << 5) |
      ((c & 1) << 7);
  }
}

static int libztex_getFpgaState (struct libztex_device *ztex, struct libztex_fpgastate *state) {
  int cnt;
  unsigned char buf[9];
  if (!libztex_checkCapability(ztex, CAPABILITY_FPGA)) {
    return -1;
  }
  cnt = libusb_control_transfer(ztex->hndl, 0xc0, 0x30, 0, 0, buf, 9, 1000);
  if (unlikely(cnt < 0)) {
    applog(LOG_ERR, "%s: Failed getFpgaState with err %d", ztex->repr, cnt);
    return cnt;
  }
  state->fpgaConfigured = buf[0] == 0;
  state->fpgaChecksum = buf[1] & 0xff;
  state->fpgaBytes = ((buf[5] & 0xff)<<24) | ((buf[4] & 0xff)<<16) | ((buf[3] & 0xff)<<8) | (buf[2] & 0xff);
  state->fpgaInitB = buf[6] & 0xff;
  state->fpgaFlashResult = buf[7];
  state->fpgaFlashBitSwap = buf[8] != 0;
  return 0;
}

static int libztex_configureFpgaLS (struct libztex_device *ztex, const char* firmware, bool force, char bs) {
  struct libztex_fpgastate state;
  unsigned char buf[8*1024*1024], cs;
  ssize_t pos=0;
  int transactionBytes = 2048;
  int tries, cnt, i, j;
  FILE *fp;

  if (!libztex_checkCapability(ztex, CAPABILITY_FPGA)) {
    return -1;
  }

  libztex_getFpgaState(ztex, &state);
  if (!force) {
    if (state.fpgaConfigured) {
      return 1;
    }
  }

  fp = fopen(firmware, "rb");
  if (!fp) {
    applog(LOG_ERR, "%s: failed to read firmware '%s'", ztex->repr, firmware);
    return -2;
  }

  while (!feof(fp)) {
    buf[pos++] = getc(fp);
  };
  pos--;
  applog(LOG_ERR, "%s: read firmware, %d bytes", ztex->repr, pos);

  fclose(fp);
  
  if ( bs<0 || bs>1 )
    bs = libztex_detectBitstreamBitOrder(buf, transactionBytes<pos ? transactionBytes : pos);
  if ( bs == 1 )
    libztex_swapBits(buf, pos);
            
  for (tries=10; tries>0; tries--) {
    //* Reset fpga
    cnt = libusb_control_transfer(ztex->hndl, 0x40, 0x31, 0, 0, NULL, 0, 1000);
    if (unlikely(cnt < 0)) {
      applog(LOG_ERR, "%s: Failed reset fpga with err %d", ztex->repr, cnt);
      continue;
    }
    cs = 0;
    i = 0;
    while (i < pos) {
      j = (i+transactionBytes) > pos ? pos-i : transactionBytes;
      cnt = libusb_control_transfer(ztex->hndl, 0x40, 0x32, 0, 0, &buf[i], j, 5000);
      if (unlikely(cnt < 0)) {
        applog(LOG_ERR, "%s: Failed send fpga data with err %d", ztex->repr, cnt);
        break;
      }
      for (j=0; j<cnt; j++) {
        cs = (cs + (buf[i+j] & 0xFF)) & 0xFF;
      }
      i += cnt;
    }
    if (i < pos) {
      continue;
    }
    tries = 0;
    libztex_getFpgaState(ztex, &state);
    if (!state.fpgaConfigured) {
      applog(LOG_ERR, "%s: FPGA configuration failed: DONE pin does not go high", ztex->repr);
      return 3;
    }
  }
  sleep(0.2);
  applog(LOG_ERR, "%s: FPGA configuration done", ztex->repr);
  return 0;
}

int libztex_configureFpga (struct libztex_device *ztex) {
  int rv;
  rv = libztex_configureFpgaLS(ztex, "bitstreams/ztex_ufm1_15d3.bit", true, 2);
  if (rv == 0) {
    libztex_setFreq(ztex, ztex->freqMDefault);
  }
  return rv;
}

int libztex_setFreq (struct libztex_device *ztex, uint16_t freq) {
  int cnt;
  if (freq > ztex->freqMaxM) {
    freq = ztex->freqMaxM;
  }

  cnt = libusb_control_transfer(ztex->hndl, 0x40, 0x83, freq, 0, NULL, 0, 500);
  if (unlikely(cnt < 0)) {
    applog(LOG_ERR, "Ztex check device: Failed to set frequency with err %d", cnt);
    return cnt;
  }
  ztex->freqM = freq;
  applog(LOG_WARNING, "%s: Frequency change to %d Mhz", ztex->repr, ztex->freqM1 * (ztex->freqM + 1));

  return 0;
}

int libztex_prepare_device (struct libusb_device *dev, struct libztex_device** ztex) {
  struct libztex_device *newdev;
  int cnt, err;
  unsigned char buf[64];

  newdev = malloc(sizeof(struct libztex_device));
  newdev->valid = false;
  newdev->hndl = NULL;
  *ztex = newdev;

  err = libusb_get_device_descriptor(dev, &newdev->descriptor);
  if (unlikely(err != 0)) {
    applog(LOG_ERR, "Ztex check device: Failed to open read descriptor with error %d", err);
    return err;
  }

  // Check vendorId and productId
  if (!(newdev->descriptor.idVendor == LIBZTEX_IDVENDOR &&
        newdev->descriptor.idProduct == LIBZTEX_IDPRODUCT)) {
    applog(LOG_ERR, "Not a ztex device? %0.4X, %0.4X", newdev->descriptor.idVendor, newdev->descriptor.idProduct);
    return 1;
  }

  libusb_open(dev, &newdev->hndl);
  cnt = libusb_get_string_descriptor_ascii (newdev->hndl, newdev->descriptor.iSerialNumber, newdev->snString,
                                            LIBZTEX_SNSTRING_LEN+1);
  if (unlikely(cnt < 0)) {
    applog(LOG_ERR, "Ztex check device: Failed to read device snString with err %d", cnt);
    return cnt;
  }
  applog(LOG_WARNING, "-- %s", newdev->snString);

  cnt = libusb_control_transfer(newdev->hndl, 0xc0, 0x22, 0, 0, buf, 40, 500);
  if (unlikely(cnt < 0)) {
    applog(LOG_ERR, "Ztex check device: Failed to read ztex descriptor with err %d", cnt);
    return cnt;
  }
  
  if ( buf[0]!=40 || buf[1]!=1 || buf[2]!='Z' || buf[3]!='T' || buf[4]!='E' || buf[5]!='X' ) {
    applog(LOG_ERR, "Ztex check device: Error reading ztex descriptor");
    return 2;
  }

  newdev->productId[0] = buf[6];
  newdev->productId[1] = buf[7];
  newdev->productId[2] = buf[8];
  newdev->productId[3] = buf[9];
  newdev->fwVersion = buf[10];
  newdev->interfaceVersion = buf[11];
  newdev->interfaceCapabilities[0] = buf[12];
  newdev->interfaceCapabilities[1] = buf[13];
  newdev->interfaceCapabilities[2] = buf[14];
  newdev->interfaceCapabilities[3] = buf[15];
  newdev->interfaceCapabilities[4] = buf[16];
  newdev->interfaceCapabilities[5] = buf[17];
  newdev->moduleReserved[0] = buf[18];
  newdev->moduleReserved[1] = buf[19];
  newdev->moduleReserved[2] = buf[20];
  newdev->moduleReserved[3] = buf[21];
  newdev->moduleReserved[4] = buf[22];
  newdev->moduleReserved[5] = buf[23];
  newdev->moduleReserved[6] = buf[24];
  newdev->moduleReserved[7] = buf[25];
  newdev->moduleReserved[8] = buf[26];
  newdev->moduleReserved[9] = buf[27];
  newdev->moduleReserved[10] = buf[28];
  newdev->moduleReserved[11] = buf[29];


  cnt = libusb_control_transfer(newdev->hndl, 0xc0, 0x82, 0, 0, buf, 64, 500);
  if (unlikely(cnt < 0)) {
    applog(LOG_ERR, "Ztex check device: Failed to read ztex descriptor with err %d", cnt);
    return cnt;
  }

  if (unlikely(buf[0]) != 4) {
    if (unlikely(buf[0]) != 2) {
      applog(LOG_ERR, "Invalid BTCMiner descriptor version. Firmware must be updated.");
      return 3;
    }
    applog(LOG_WARNING, "Firmware out of date");
  }

  newdev->numNonces = buf[1] + 1;
  newdev->offsNonces =  ((buf[2] & 255) | ((buf[3] & 255) << 8)) - 10000;
  newdev->freqM1 = ( (buf[4] & 255) | ((buf[5] & 255) << 8) ) * 0.01;
  newdev->freqMaxM = (buf[7] & 255);
  newdev->freqM = (buf[6] & 255);
  newdev->freqMDefault = newdev->freqM;

  newdev->usbbus = libusb_get_bus_number(dev);
  newdev->usbaddress = libusb_get_device_address(dev);
  sprintf(newdev->repr, "ZTEX %.3d:%.3d-%s", newdev->usbbus, newdev->usbaddress, newdev->snString);
  newdev->valid = true;
  return 0;
}

void libztex_destroy_device (struct libztex_device* ztex) {
  if (ztex->hndl != NULL) {
    libusb_close(ztex->hndl);
  }
  free(ztex);
}

int libztex_scanDevices (struct libztex_dev_list*** devs_p) {
  libusb_device **list;
  struct libztex_device *ztex;
  ssize_t cnt = libusb_get_device_list(NULL, &list);
  ssize_t i = 0;
  int found = 0, pos = 0, err;
  
  if (unlikely(cnt < 0)) {
    applog(LOG_ERR, "Ztex scan devices: Failed to list usb devices with err %d", cnt);
    return 0;
  }

  int usbdevices[LIBZTEX_MAX_DESCRIPTORS];

  for (i = 0; i < cnt; i++) {
    if (libztex_checkDevice(list[i])) {
      // Got one!
      usbdevices[found] = i;
      found++;
    }
  }

  struct libztex_dev_list **devs;
  devs = malloc(sizeof(struct libztex_dev_list *) * found);
  if (devs == NULL) {
    applog(LOG_ERR, "Ztex scan devices: Failed to allocate memory");
    return 0;
  }

  for (i = 0; i < found; i++) {
    err = libztex_prepare_device(list[usbdevices[i]], &ztex);
    if (unlikely(err != 0)) {
      applog(LOG_ERR, "prepare device: %d", err);
    }
    // check if valid
    if (!ztex->valid) {
      libztex_destroy_device(ztex);
      continue;
    }
    devs[pos] = malloc(sizeof(struct libztex_dev_list));
    devs[pos]->dev = ztex;
    devs[pos]->next = NULL;
    //libusb_open(list[usbdevices[i]], &devs[i]->hndl);
    //libusb_close(devs[cnt]->dev->hndl);
    if (pos > 0) {
      devs[pos]->next = devs[pos];
    }
    pos++;
  }

  libusb_free_device_list(list, 1);
  *devs_p = devs;
  return pos;
}

int libztex_sendHashData (struct libztex_device *ztex, unsigned char *sendbuf) {
  int cnt;

  cnt = libusb_control_transfer(ztex->hndl, 0x40, 0x80, 0, 0, sendbuf, 44, 1000);
  if (unlikely(cnt < 0)) {
    applog(LOG_ERR, "%s: Failed sendHashData with err %d", ztex->repr, cnt);
  }
  
  return cnt;
}

int libztex_readHashData (struct libztex_device *ztex, struct libztex_hash_data nonces[]) {
  // length of buf must be 8 * (numNonces + 1)
  unsigned char rbuf[12*8];
  int cnt, i;
  
  cnt = libusb_control_transfer(ztex->hndl, 0xc0, 0x81, 0, 0, rbuf, 12*ztex->numNonces, 1000);
  if (unlikely(cnt < 0)) {
    applog(LOG_ERR, "%s: Failed readHashData with err %d", ztex->repr, cnt);
    return cnt;
  }

  for (i=0; i<ztex->numNonces; i++) {
    memcpy((char*)&nonces[i].goldenNonce, &rbuf[i*12], 4);
    nonces[i].goldenNonce -= ztex->offsNonces;
    memcpy((char*)&nonces[i].nonce, &rbuf[(i*12)+4], 4);
    nonces[i].nonce -= ztex->offsNonces;
    memcpy((char*)&nonces[i].hash7, &rbuf[(i*12)+8], 4);
  }
  
  return cnt;
}

void libztex_freeDevList (struct libztex_dev_list **devs) {
  ssize_t cnt = 0;
  bool done = false;
  while (!done) {
    if (devs[cnt]->next == NULL) {
      done = true;
    }
    free(devs[cnt++]);
  }
  free(devs);
}

int libztex_configreFpga (struct libztex_dev_list* dev) {
  return 0;
}

