#ifndef __LIBBITFURY_H__
#define __LIBBITFURY_H__

#include "miner.h"
#include "spidevc.h"

extern int libbitfury_detectChips(struct spi_port *);

struct bitfury_payload {
	unsigned char midstate[32];
	unsigned int junk[8];
	unsigned m7;
	unsigned ntime;
	unsigned nbits;
	unsigned nnonce;
};

struct bitfury_device {
	struct spi_port *spi;
	int chip;
	unsigned char osc[8];
	unsigned newbuf[17];
	unsigned oldbuf[17];
	struct work * work;
	struct work * owork;
	int job_switched;
	uint32_t atrvec[20];
	struct bitfury_payload payload;
	struct bitfury_payload opayload;
	unsigned int results[16];
	int results_n;
	bool second_run;
};

int libbitfury_readHashData(unsigned int *res);
extern void libbitfury_sendHashData1(struct bitfury_device *d, bool want_results);
void libbitfury_sendHashData(struct bitfury_device *bf, int chip_n);
void work_to_payload(struct bitfury_payload *p, struct work *w);
extern void payload_to_atrvec(uint32_t *atrvec, struct bitfury_payload *);

#endif /* __LIBBITFURY_H__ */
