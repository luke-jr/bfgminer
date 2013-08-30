#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include "tm_i2c.h"

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

	//printf("REQ from %02X cmd: %02X\n", addr, cmd);

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
//		perror("ioctl error");
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
//		perror("ioctl error");
		return -1;
	}

	//hexdump(buf, 10);
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

