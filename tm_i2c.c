/*
 * Copyright 2013 gluk
 * Copyright 2013 Anatoly Legkodymov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "config.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef NEED_LINUX_I2C_H
#include <linux/i2c.h>
#endif
#include <linux/i2c-dev.h>

#include "logging.h"
#include "tm_i2c.h"

static int tm_i2c_fd;

float tm_i2c_Data2Temp(unsigned int ans) {
	float t = ans;
	return (t / 1023.0 * 3.3 * 2-2.73) * 100.0;
}

float tm_i2c_Data2Core(unsigned int ans) {
	float t = ans;
	return t / 1023.0 * 3.3;
}

int tm_i2c_init() {
	if ((tm_i2c_fd = open("/dev/i2c-1", O_RDWR)) < 0)
		return 1;
	else
		return 0;
}

void tm_i2c_close() {
	close(tm_i2c_fd);
}

unsigned int tm_i2c_req(int fd, unsigned char addr, unsigned char cmd, unsigned int data) {
	int i;
	unsigned char buf[16];
	struct i2c_msg msg;
	tm_struct *tm = (tm_struct *) buf;
	struct i2c_rdwr_ioctl_data msg_rdwr;
	unsigned int ret;

	//applog(LOG_DEBUG, "REQ from %02X cmd: %02X", addr, cmd);

	tm->cmd = cmd;
	tm->data_lsb = data & 0xFF;
	tm->data_msb = (data & 0xFF00) >> 8;

	/* Write CMD */
	msg.addr = addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = (void*)tm;
	msg_rdwr.msgs = &msg;
	msg_rdwr.nmsgs = 1;
	if ((i = ioctl(fd, I2C_RDWR, &msg_rdwr)) < 0) {
//		perror("ioctl error");
		return -1;
	}

	/* Read result */
	msg.addr = addr;
	msg.flags = I2C_M_RD;
	msg.len = 3;
	msg.buf = (void*)tm;
	msg_rdwr.msgs = &msg;
	msg_rdwr.nmsgs = 1;
	if ((i = ioctl(fd, I2C_RDWR, &msg_rdwr)) < 0) {
//		perror("ioctl error");
		return -1;
	}

	ret = (tm->data_msb << 8) + tm->data_lsb;
	if (tm->cmd == cmd) return ret;
	return 0;
}

int tm_i2c_detect(unsigned char slot) {
	if (slot < 0 || slot > 31) return 0;
	return tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, TM_GET_CORE0, 0);
}

float tm_i2c_getcore0(unsigned char slot) {
	if (slot < 0 || slot > 31) return 0;
	return tm_i2c_Data2Core(tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, TM_GET_CORE0, 0));
}

float tm_i2c_getcore1(unsigned char slot) {
	if (slot < 0 || slot > 31) return 0;
	return tm_i2c_Data2Core(tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, TM_GET_CORE1, 0));
}

float tm_i2c_gettemp(unsigned char slot) {
	if (slot < 0 || slot > 31) return 0;
	return tm_i2c_Data2Temp(tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, TM_GET_TEMP, 0));
}

void tm_i2c_set_oe(unsigned char slot) {
	if (slot < 0 || slot > 31) return;
	tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, TM_SET_OE, 0);
}

void tm_i2c_clear_oe(unsigned char slot) {
	if (slot < 0 || slot > 31) return;
	tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, TM_SET_OE, 1);
}

unsigned char tm_i2c_slot2addr(unsigned char slot) {
	if (slot < 0 || slot > 31) return 0;
	return ((TM_ADDR >> 1) + slot);
}

