/*
 * Copyright 2013 gluk <glukolog@mail.ru>
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
#include <fcntl.h>
#include <sys/ioctl.h>
//#ifdef NEED_LINUX_I2C_H
#include <linux/i2c.h>
//#endif
#include <linux/i2c-dev.h>
#include "tm_i2cm.h"
#include <sys/mman.h>

#define TM_TRIES	3

//static unsigned leds;
//static 
int tm_i2c_fd;

//static 
unsigned leds = 0xFF;

static volatile unsigned *gpiom;

#define INP_GPIO(g) *(gpiom+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpiom+((g)/10)) |=  (1<<(((g)%10)*3))
#define GPIO_SET *(gpiom+7)
#define GPIO_CLR *(gpiom+10)

unsigned char slotI2C(unsigned char slot) {
	unsigned char x = (slot >> 1);

	if (x == 6) return 4;
	if (x == 4) return 6;
	return x;
}

double tm_i2c_Data2Temp(unsigned int ans) {
	double t = ans;
	return (t / 1023.0 * 3.3 * 2.0 - 2.73) * 100.0;
}

double tm_i2c_Data2Core(unsigned int ans) {
	double t = ans;
	return t / 1023.0 * 3.3;
}

int tm_i2c_init() {
	int i;
	int fd;

	fd = open("/dev/mem",O_RDWR|O_SYNC);
	if (fd < 0) { perror("/dev/mem trouble"); return (1); }
	gpiom = mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x20200000);
	if (gpiom == MAP_FAILED) { perror("gpio mmap trouble"); return(1); }
	close(fd);

	for(i = 0; i < 4 ; i++) {
		INP_GPIO(i + 22); OUT_GPIO(i + 22);
		GPIO_CLR = 1 << (i + 22);
	}
	INP_GPIO(27); OUT_GPIO(27);
	INP_GPIO(17); OUT_GPIO(17);
	INP_GPIO(18); OUT_GPIO(18);

	GPIO_CLR = 1 << 27;
	usleep(1000);
	GPIO_SET = 1 << 27;

	if ((tm_i2c_fd = open("/dev/i2c-1", O_RDWR)) < 0) return 1;
	else return 0;
}

void leds_push() {
	int i;

	for(i = 0 ; i < 16 ; i++) {
		if (leds & (1 << i)) GPIO_SET = 1 << 17;
		else GPIO_CLR = 1 << 17;
		GPIO_SET = 1 << 18;
		usleep(10);
		GPIO_CLR = 1 << 18;
	}
}

void leds_set(unsigned char b) {
	leds &= ~(1 << b);
	leds_push();
}

void leds_clr(unsigned char b) {
	leds |= (1 << b);
	leds_push();
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

	//printf("fd: %d, REQ from %02X cmd: %02X\n", fd, addr, cmd);

	tm->cmd = cmd;
	tm->data_lsb = data & 0xFF;
	tm->data_msb = (data & 0xFF00) >> 8;

	/* Write CMD */
	msg.addr = addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = buf;
	msg_rdwr.msgs = &msg;
	msg_rdwr.nmsgs = 1;
	if ((i = ioctl(fd, I2C_RDWR, &msg_rdwr)) < 0) {
		perror("ioctl error");
		return -1;
	}

	/* Read result */
	msg.addr = addr;
	msg.flags = I2C_M_RD;
	msg.len = 3;
	msg.buf = buf;
	msg_rdwr.msgs = &msg;
	msg_rdwr.nmsgs = 1;
	if ((i = ioctl(fd, I2C_RDWR, &msg_rdwr)) < 0) {
		perror("ioctl error");
		return -1;
	}

	//hexdump(buf, 10);
	ret = (tm->data_msb << 8) + tm->data_lsb;
	//printf("REQ from %02X, cmd: %02X, res: %04X\n", addr, cmd, ret);
	if (tm->cmd == cmd) return ret;
	return 0;
}

int tm_i2c_detect(unsigned char slot) {
	if (slot < 0 || slot > 31) return 0;
	leds_set(slot >> 1);
	//return tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + (slot >> 1), TM_GET_CORE0, 0);
	return 1;
}

double tm_i2c_getcore0(unsigned char slot) {
	int t = 0;
	double v;
	if (slot < 0 || slot > 31) return 0;
	do {
		usleep(10000);
		v = tm_i2c_Data2Core(tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slotI2C(slot), TM_GET_CORE0, 0));
		t++;
	} while(t <= TM_TRIES && (v == 0 || v > 3.3));
	if (v == 0 || v > 3.3) v = -1;
	return v;
}

double tm_i2c_getcore1(unsigned char slot) {
	int t = 0;
	double v;
	if (slot < 0 || slot > 31) return 0;
	do {
		usleep(10000);
		v = tm_i2c_Data2Core(tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slotI2C(slot), TM_GET_CORE1, 0));
		t++;
	} while(t <= TM_TRIES && (v == 0 || v > 3.3));
	if (v == 0 || v > 3.3) v = -1;
	return v;
}

double tm_i2c_gettemp(unsigned char slot) {
	if (slot < 0 || slot > 31) return 0;
	return tm_i2c_Data2Temp(tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slotI2C(slot), TM_GET_TEMP, 0));
}
/*
void tm_i2c_set_oe(unsigned char slot) {
	if (slot < 0 || slot > 31) return;
	tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, TM_SET_OE, 0);
}

void tm_i2c_clear_oe(unsigned char slot) {
	if (slot < 0 || slot > 31) return;
	tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, TM_SET_OE, 1);
}
*/
void tm_i2c_set_oe(unsigned char slot) {
	int i;

	if (slot < 0 || slot > 15) return;
	for(i = 0 ; i < 4 ; i++) {
		if (slot & (1 << i)) GPIO_SET = 1 << (i + 22);
		else GPIO_CLR = 1 << (i + 22);
	}
	//usleep(1000);
	GPIO_CLR = 1 << 27;
	//usleep(1000);
	leds ^= (1 << slot);
	leds_push();
}

void tm_i2c_clear_oe(unsigned char slot) {
	if (slot < 0 || slot > 31) return;
	//usleep(10000);
	GPIO_SET = 1 << 27;
}

unsigned char tm_i2c_slot2addr(unsigned char slot) {
	if (slot < 0 || slot > 31) return 0;
	return ((TM_ADDR >> 1) + slotI2C(slot));
}

double tm_i2c_set_voltage_abs(unsigned char slot2, double voltage) {
	int vid = 0, core_id;
	double prev = 0.0, next = 0.0, current = 0.0;
	double (*get_core)(unsigned char);
	unsigned char slot = slotI2C(slot2);
	double eps = 0.01, eps_max=0.02;

	if ((slot2 & 1)) {
		core_id = TM_SET_CORE1;
		get_core = &tm_i2c_getcore1;
	} else {
		core_id = TM_SET_CORE0;
		get_core = &tm_i2c_getcore0;
	}
	if (get_core(slot2) <= 0) return -1;

	do {
		prev = current;
		tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, core_id, vid);
		usleep(100000);
		current = get_core(slot2);
		vid++;
		usleep(1000);
	} while (current <= voltage && vid < 16);
	if (current - voltage <= eps) return current;
	if (current - voltage > eps_max || current - voltage > voltage - prev) {
		int tr = 0;
		double volt_chk;
		//set low
		vid -= 2;
		do {
			tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slot, core_id, vid);
			usleep(100000);
			volt_chk = get_core(slot2);
			tr++;
		} while(volt_chk > current && tr < TM_TRIES);
	}
	usleep(100000);
	return get_core(slot2);
}

unsigned int tm_i2c_set_vid(unsigned char slot, unsigned char vid) {
	int tr = 0;
	unsigned int res;

	if (slot < 0 || slot > 31) return -1;
	do {
		res = tm_i2c_req(tm_i2c_fd, (TM_ADDR >> 1) + slotI2C(slot), (slot & 1) ? TM_SET_CORE1 : TM_SET_CORE0, vid);
		tr++;
	} while(tr < TM_TRIES && res < 0);
	return res;
}

