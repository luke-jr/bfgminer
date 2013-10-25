#ifndef __LIBBITFURY_H__
#define __LIBBITFURY_H__

#include "miner.h"
#include "spidevc.h"

#define BITFURY_STAT_N 1024

struct bitfury_payload {
	unsigned char midstate[32];
	unsigned int junk[8];
	unsigned m7;
	unsigned ntime;
	unsigned nbits;
	unsigned nnonce;
};

struct freq_stat {
	double mh_52;
	double s_52;
	double mh_53;
	double s_53;
	double mh_54;
	double s_54;
	double mh_55;
	double s_55;
	double mh_56;
	double s_56;
	double omh;
	double os;
	int best_osc;
	int best_done;
};

struct bitfury_device {
	struct spi_port *spi;
	unsigned char osc6_bits;
	unsigned newbuf[17];
	unsigned oldbuf[17];
	bool oldjob;
	int active;
	struct work * work;
	struct work * owork;
	struct work * o2work;
	int job_switched;
	uint32_t atrvec[20];
	struct bitfury_payload payload;
	struct bitfury_payload opayload;
	struct bitfury_payload o2payload;
	struct freq_stat chip_stat[120];
	unsigned int results[16];
	int results_n;
	time_t stat_ts[BITFURY_STAT_N];
	unsigned int stat_counter;
	unsigned int future_nonce;
	unsigned int old_nonce;
	struct timeval timer1;
	struct timeval timer2;
	struct timeval otimer1;
	struct timeval otimer2;
	struct timeval predict1;
	struct timeval predict2;
	struct timeval tv_stat;
	unsigned int counter1, counter2;
	unsigned int ocounter1, ocounter2;
	unsigned int skip_stat;
	int rate; //per msec
	int osc_slow;
	int osc_fast;
	int req1_done, req2_done;
	double mhz;
	double ns;
	unsigned slot;
	unsigned fasync;
	unsigned strange_counter;
	bool second_run;
	bool force_reinit;
	int desync_counter;
	int sample_hwe;
	int sample_tot;

	
	time_t short_out_t;
	time_t long_out_t;
};

extern void libbitfury_sendHashData1(int chip_id, struct bitfury_device *, struct thr_info *);
void work_to_payload(struct bitfury_payload *p, struct work *w);
extern void payload_to_atrvec(uint32_t *atrvec, struct bitfury_payload *);
extern void send_reinit(struct spi_port *, int slot, int chip_n, int n);
extern void send_shutdown(struct spi_port *, int slot, int chip_n);
extern void send_freq(struct spi_port *, int slot, int chip_n, int bits);
extern int libbitfury_detectChips1(struct spi_port *);
extern unsigned bitfury_decnonce(unsigned);
extern bool bitfury_fudge_nonce(const void *midstate, const uint32_t m7, const uint32_t ntime, const uint32_t nbits, uint32_t *nonce_p);

#endif /* __LIBBITFURY_H__ */
