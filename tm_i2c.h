/* - Version 1.0 - */

#define TM_ADDR         0xC0

#define TM_GET_TEMP     0x10
#define TM_GET_CORE0    0x11
#define TM_GET_CORE1    0x12

#define TM_SET_OE       0x20
#define TM_SET_MODE     0x21
#define TM_SET_RED      0x22
#define TM_SET_GREEN    0x23

#define TM_GET_PORTB    0x30
#define TM_SET_PORTB    0x31
#define TM_GET_PINB     0x32
#define TM_GET_PORTD    0x33
#define TM_SET_PORTD    0x34
#define TM_GET_PIND     0x35
#define TM_GET_ADC      0x36

#define TM_MODE_AUTO    0
#define TM_MODE_MANUAL  1

typedef struct {
	unsigned char cmd;
	unsigned char data_lsb;
	unsigned char data_msb;
} tm_struct;

int tm_i2c_init();
void tm_i2c_close();
unsigned int tm_i2c_req(int fd, unsigned char addr, unsigned char cmd, unsigned int data);
float tm_i2c_Data2Temp(unsigned int ans);
float tm_i2c_Data2Core(unsigned int ans);
float tm_i2c_gettemp(unsigned char slot);
float tm_i2c_getcore0(unsigned char slot);
float tm_i2c_getcore1(unsigned char slot);
void tm_i2c_set_oe(unsigned char slot);
void tm_i2c_clear_oe(unsigned char slot);
int tm_i2c_detect(unsigned char slot);
unsigned char tm_i2c_slot2addr(unsigned char slot);

