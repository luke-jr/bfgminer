/*
 * Copyright 2013 bitfury
 * Copyright 2013 Anatoly Legkodymov
 * Copyright 2013 Luke Dashjr
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

#include <limits.h>
#include "miner.h"
#include <unistd.h>
#include <sha2.h>

#include "deviceapi.h"
#include "driver-bitfury.h"
#include "libbitfury.h"
#include "util.h"
#include "spidevc.h"

#define GOLDEN_BACKLOG 5
#define LINE_LEN 2048

struct device_drv bitfury_drv;

int calc_stat(time_t * stat_ts, time_t stat, struct timeval now);
double shares_to_ghashes(int shares, int seconds);

static
int bitfury_autodetect()
{
	RUNONCE(0);
	
	int chip_n;
	struct cgpu_info *bitfury_info;

	bitfury_info = calloc(1, sizeof(struct cgpu_info));
	bitfury_info->drv = &bitfury_drv;
	bitfury_info->threads = 1;

	applog(LOG_INFO, "INFO: bitfury_detect");
	spi_init();
	if (!sys_spi)
		return 0;
	chip_n = libbitfury_detectChips1(sys_spi);
	if (!chip_n) {
		applog(LOG_WARNING, "No Bitfury chips detected!");
		return 0;
	} else {
		applog(LOG_WARNING, "BITFURY: %d chips detected!", chip_n);
	}

	bitfury_info->procs = chip_n;
	add_cgpu(bitfury_info);
	
	return 1;
}

static void bitfury_detect(void)
{
	noserial_detect_manual(&bitfury_drv, bitfury_autodetect);
}


static
void *bitfury_just_io(struct bitfury_device * const bitfury)
{
	struct spi_port * const spi = bitfury->spi;
	const int chip = bitfury->fasync;
	void *rv;
	
	spi_clear_buf(spi);
	spi_emit_break(spi);
	spi_emit_fasync(spi, chip);
	rv = spi_emit_data(spi, 0x3000, &bitfury->atrvec[0], 19 * 4);
	spi_txrx(spi);
	return rv;
}

static
void bitfury_debug_nonce_array(const struct cgpu_info * const proc, const char *msg, const uint32_t * const inp)
{
	const struct bitfury_device * const bitfury = proc->device_data;
	const int active = bitfury->active;
	char s[((1 + 8) * 0x10) + 1];
	char *sp = s;
	for (int i = 0; i < 0x10; ++i)
		sp += sprintf(sp, "%c%08lx",
		              (active == i) ? '>' : ' ',
		              (unsigned long)bitfury_decnonce(inp[i]));
	applog(LOG_DEBUG, "%"PRIpreprv": %s%s (job=%08lx)",
	       proc->proc_repr, msg, s, (unsigned long)inp[0x10]);
}

static
bool bitfury_init_oldbuf(struct cgpu_info * const proc, const uint32_t *inp)
{
	struct bitfury_device * const bitfury = proc->device_data;
	uint32_t * const oldbuf = &bitfury->oldbuf[0];
	uint32_t * const buf = &bitfury->newbuf[0];
	int i, differ, tried = 0;
	
	if (!inp)
		inp = bitfury_just_io(bitfury);
tryagain:
	if (tried > 3)
	{
		applog(LOG_ERR, "%"PRIpreprv": %s: Giving up after %d tries",
		       proc->proc_repr, __func__, tried);
		bitfury->desync_counter = 99;
		return false;
	}
	++tried;
	memcpy(buf, inp, 0x10 * 4);
	inp = bitfury_just_io(bitfury);
	differ = -1;
	for (i = 0; i < 0x10; ++i)
	{
		if (inp[i] != buf[i])
		{
			if (differ != -1)
			{
				applog(LOG_DEBUG, "%"PRIpreprv": %s: Second differ at %d; trying again",
				       proc->proc_repr, __func__, i);
				goto tryagain;
			}
			differ = i;
			applog(LOG_DEBUG, "%"PRIpreprv": %s: Differ at %d",
			       proc->proc_repr, __func__, i);
			if (tried > 3)
				break;
		}
	}
	if (-1 == differ)
	{
		applog(LOG_DEBUG, "%"PRIpreprv": %s: No differ found; trying again",
		       proc->proc_repr, __func__);
		goto tryagain;
	}
	
	bitfury->active = differ;
	memcpy(&oldbuf[0], &inp[bitfury->active], 4 * (0x10 - bitfury->active));
	memcpy(&oldbuf[0x10 - bitfury->active], &inp[0], 4 * bitfury->active);
	bitfury->oldjob = inp[0x10];
	bitfury->desync_counter = 0;
	
	if (opt_debug)
		bitfury_debug_nonce_array(proc, "Init", inp);
	
	return true;
}

bool bitfury_init_chip(struct cgpu_info * const proc)
{
	struct bitfury_device * const bitfury = proc->device_data;
	struct bitfury_payload payload = {
		.midstate = "\xf9\x9a\xf0\xd5\x72\x34\x41\xdc\x9e\x10\xd1\x1f\xeb\xcd\xe3\xf5"
		            "\x52\xf1\x14\x63\x06\x14\xd1\x12\x15\x25\x39\xd1\x7d\x77\x5a\xfd",
		.m7    = 0xafbd0b42,
		.ntime = 0xb6c24563,
		.nbits = 0x6dfa4352,
	};
	payload_to_atrvec(bitfury->atrvec, &payload);
	return bitfury_init_oldbuf(proc, NULL);
}

static
bool bitfury_init(struct thr_info *thr)
{
	struct cgpu_info *proc;
	struct bitfury_device *bitfury;
	
	for (proc = thr->cgpu; proc; proc = proc->next_proc)
	{
		bitfury = proc->device_data = malloc(sizeof(struct bitfury_device));
		*bitfury = (struct bitfury_device){
			.spi = sys_spi,
			.fasync = proc->proc_id,
		};
		bitfury_init_chip(proc);
	}
	
	return true;
}

static
bool bitfury_queue_full(struct cgpu_info *cgpu)
{
	struct cgpu_info *proc;
	struct bitfury_device *bitfury;
	
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		bitfury = proc->device_data;
		
		if (bitfury->work)
			continue;
		
		bitfury->work = get_queued(cgpu);
		if (!bitfury->work)
			return false;
		
		work_to_payload(&bitfury->payload, bitfury->work);
	}
	
	return true;
}

int64_t bitfury_scanHash(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct bitfury_device * const sds = cgpu->device_data;
	struct cgpu_info *proc;
	struct thr_info *pthr;
	struct bitfury_device *bitfury;
	struct timeval now;
	char line[LINE_LEN];
	int short_stat = 10;
	int long_stat = 1800;
	int i;

	if (!bitfury_queue_full(cgpu))
		return 0;
	
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		const int chip = proc->proc_id;
		pthr = proc->thr[0];
		bitfury = proc->device_data;
		
		bitfury->job_switched = 0;
		payload_to_atrvec(bitfury->atrvec, &bitfury->payload);
		libbitfury_sendHashData1(chip, bitfury, pthr);
	}

	cgsleep_ms(5);

	cgtime(&now);
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		pthr = proc->thr[0];
		bitfury = proc->device_data;
		
		if (bitfury->job_switched) {
			int i,j;
			unsigned int * const res = bitfury->results;
			struct work * const work = bitfury->work;
			struct work * const owork = bitfury->owork;
			struct work * const o2work = bitfury->o2work;
			i = bitfury->results_n;
			for (j = i - 1; j >= 0; j--) {
				if (owork) {
					submit_nonce(pthr, owork, bswap_32(res[j]));
					bitfury->stat_ts[bitfury->stat_counter++] =
						now.tv_sec;
					if (bitfury->stat_counter == BITFURY_STAT_N) {
						bitfury->stat_counter = 0;
					}
				}
				if (o2work) {
					// TEST
					//submit_nonce(pthr, owork, bswap_32(res[j]));
				}
			}
			bitfury->results_n = 0;
			bitfury->job_switched = 0;
			if (bitfury->old_nonce && o2work) {
					submit_nonce(pthr, o2work, bswap_32(bitfury->old_nonce));
					i++;
			}
			if (bitfury->future_nonce) {
					submit_nonce(pthr, work, bswap_32(bitfury->future_nonce));
					i++;
			}

			if (o2work)
				work_completed(cgpu, o2work);

			bitfury->o2work = bitfury->owork;
			bitfury->owork = bitfury->work;
			bitfury->work = NULL;
			hashes_done2(pthr, 0xbd000000, NULL);
		}
	}

	if (now.tv_sec - sds->short_out_t > short_stat) {
		int shares_first = 0, shares_last = 0, shares_total = 0;
		char stat_lines[32][LINE_LEN] = {{0}};
		int len, k;
		double gh[32][8] = {{0}};
		double ghsum = 0, gh1h = 0, gh2h = 0;
		unsigned strange_counter = 0;

		for (proc = cgpu; proc; proc = proc->next_proc)
		{
			const int chip = proc->proc_id;
			bitfury = proc->device_data;
			
			int shares_found = calc_stat(bitfury->stat_ts, short_stat, now);
			double ghash;
			len = strlen(stat_lines[bitfury->slot]);
			ghash = shares_to_ghashes(shares_found, short_stat);
			gh[bitfury->slot][chip & 0x07] = ghash;
			snprintf(stat_lines[bitfury->slot] + len, LINE_LEN - len, "%.1f-%3.0f ", ghash, bitfury->mhz);

			if(sds->short_out_t && ghash < 0.5) {
				applog(LOG_WARNING, "Chip_id %d FREQ CHANGE", chip);
				send_freq(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits - 1);
				cgsleep_ms(1);
				send_freq(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
			}
			shares_total += shares_found;
			shares_first += chip < 4 ? shares_found : 0;
			shares_last += chip > 3 ? shares_found : 0;
			strange_counter += bitfury->strange_counter;
			bitfury->strange_counter = 0;
		}
		sprintf(line, "vvvvwww SHORT stat %ds: wwwvvvv", short_stat);
		applog(LOG_WARNING, "%s", line);
		sprintf(line, "stranges: %u", strange_counter);
		applog(LOG_WARNING, "%s", line);
		for(i = 0; i < 32; i++)
			if(strlen(stat_lines[i])) {
				len = strlen(stat_lines[i]);
				ghsum = 0;
				gh1h = 0;
				gh2h = 0;
				for(k = 0; k < 4; k++) {
					gh1h += gh[i][k];
					gh2h += gh[i][k+4];
					ghsum += gh[i][k] + gh[i][k+4];
				}
				snprintf(stat_lines[i] + len, LINE_LEN - len, "- %2.1f + %2.1f = %2.1f slot %i ", gh1h, gh2h, ghsum, i);
				applog(LOG_WARNING, "%s", stat_lines[i]);
			}
		sds->short_out_t = now.tv_sec;
	}

	if (now.tv_sec - sds->long_out_t > long_stat) {
		int shares_first = 0, shares_last = 0, shares_total = 0;
		char stat_lines[32][LINE_LEN] = {{0}};
		int len, k;
		double gh[32][8] = {{0}};
		double ghsum = 0, gh1h = 0, gh2h = 0;

		for (proc = cgpu; proc; proc = proc->next_proc)
		{
			const int chip = proc->proc_id;
			bitfury = proc->device_data;
			
			int shares_found = calc_stat(bitfury->stat_ts, long_stat, now);
			double ghash;
			len = strlen(stat_lines[bitfury->slot]);
			ghash = shares_to_ghashes(shares_found, long_stat);
			gh[bitfury->slot][chip & 0x07] = ghash;
			snprintf(stat_lines[bitfury->slot] + len, LINE_LEN - len, "%.1f-%3.0f ", ghash, bitfury->mhz);

			shares_total += shares_found;
			shares_first += chip < 4 ? shares_found : 0;
			shares_last += chip > 3 ? shares_found : 0;
		}
		sprintf(line, "!!!_________ LONG stat %ds: ___________!!!", long_stat);
		applog(LOG_WARNING, "%s", line);
		for(i = 0; i < 32; i++)
			if(strlen(stat_lines[i])) {
				len = strlen(stat_lines[i]);
				ghsum = 0;
				gh1h = 0;
				gh2h = 0;
				for(k = 0; k < 4; k++) {
					gh1h += gh[i][k];
					gh2h += gh[i][k+4];
					ghsum += gh[i][k] + gh[i][k+4];
				}
				snprintf(stat_lines[i] + len, LINE_LEN - len, "- %2.1f + %2.1f = %2.1f slot %i ", gh1h, gh2h, ghsum, i);
				applog(LOG_WARNING, "%s", stat_lines[i]);
			}
		sds->long_out_t = now.tv_sec;
	}

	return 0;
}

double shares_to_ghashes(int shares, int seconds) {
	return (double)shares / (double)seconds * 4.84387;  //orig: 4.77628
}

int calc_stat(time_t * stat_ts, time_t stat, struct timeval now) {
	int j;
	int shares_found = 0;
	for(j = 0; j < BITFURY_STAT_N; j++) {
		if (now.tv_sec - stat_ts[j] < stat) {
			shares_found++;
		}
	}
	return shares_found;
}

bool bitfury_prepare(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;

	get_now_datestamp(cgpu->init, sizeof(cgpu->init));

	applog(LOG_INFO, "INFO bitfury_prepare");
	return true;
}

void bitfury_shutdown(struct thr_info *thr) {
	struct cgpu_info *cgpu = thr->cgpu, *proc;
	struct bitfury_device *bitfury;
	
	applog(LOG_INFO, "INFO bitfury_shutdown");
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		bitfury = proc->device_data;
		send_shutdown(bitfury->spi, bitfury->slot, bitfury->fasync);
	}
}

bool bitfury_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	
	if (opt_debug)
	{
		char hex[153];
		bin2hex(hex, &work->data[0], 76);
		applog(LOG_DEBUG, "%"PRIpreprv": Preparing work %s",
		       proc->proc_repr, hex);
	}
	work_to_payload(&bitfury->payload, work);
	payload_to_atrvec(bitfury->atrvec, &bitfury->payload);
	
	work->blk.nonce = 0xffffffff;
	return true;
}

static
bool fudge_nonce(struct work * const work, uint32_t *nonce_p) {
	static const uint32_t offsets[] = {0, 0xffc00000, 0xff800000, 0x02800000, 0x02C00000, 0x00400000};
	uint32_t nonce;
	int i;
	
	if (unlikely(!work))
		return false;
	
	for (i = 0; i < 6; ++i)
	{
		nonce = *nonce_p + offsets[i];
		if (test_nonce(work, nonce, false))
		{
			*nonce_p = nonce;
			return true;
		}
	}
	return false;
}

void bitfury_noop_job_start(struct thr_info __maybe_unused * const thr)
{
}

#define HOP_DONE 600

unsigned int stat_done;

typedef uint32_t bitfury_inp_t[0x11];

int select_freq(struct bitfury_device *bitfury, struct cgpu_info *proc) {
	int freq;
	int random;
	int chip_id;
	struct freq_stat *c;

	chip_id = proc->device_id * 8 + proc->proc_id;
	c = &(bitfury->chip_stat[chip_id]);

	if (c->best_done) {
		freq = c->best_osc;
	} else {
		random = (int)(bitfury->mhz * 1000.0) & 1;
		freq = (bitfury->osc6_bits == 56) ? 52 : bitfury->osc6_bits + random;
		if (c->s_52 > HOP_DONE && c->s_53 > HOP_DONE && c->s_54 > HOP_DONE &&
			c->s_55 > HOP_DONE && c->s_56 > HOP_DONE) {
			double mh_max = 0.0;

			if (mh_max < c->mh_52 / c->s_52) { mh_max = c->mh_52 / c->s_52; freq = 52; }
			if (mh_max < c->mh_53 / c->s_53) { mh_max = c->mh_53 / c->s_53; freq = 53; }
			if (mh_max < c->mh_54 / c->s_54) { mh_max = c->mh_54 / c->s_54; freq = 54; }
			if (mh_max < c->mh_55 / c->s_55) { mh_max = c->mh_55 / c->s_55; freq = 55; }
			if (mh_max < c->mh_56 / c->s_56) { mh_max = c->mh_56 / c->s_56; freq = 56; }
			c->best_done = 1;
			c->best_osc = freq;
			stat_done++;
			applog(LOG_DEBUG, "AAA chip_id: %d. best_done = %d !!!!!!!!! best_osc = %d", chip_id, stat_done, freq);
		}
	}
	bitfury->osc6_bits = freq;
	send_freq(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
	return 0;
}

void bitfury_do_io(struct thr_info * const master_thr)
{
	struct cgpu_info *proc;
	struct thr_info *thr;
	struct bitfury_device *bitfury;
	const uint32_t *inp;
	int n, i, j;
	bool newjob;
	uint32_t nonce;
	int n_chips = 0, lastchip = 0;
	struct spi_port *spi = NULL;
	bool should_be_running;
	struct timeval tv_now;
	unsigned int counter;
	struct timeval tv_stat;
	struct timeval tv_diff;
	struct timeval tv_period;

	unsigned int skip_stat;

	tv_period.tv_sec = 60;
	tv_period.tv_usec = 0;
	for (proc = master_thr->cgpu; proc; proc = proc->next_proc)
		++n_chips;
	
	struct cgpu_info *procs[n_chips];
	void *rxbuf[n_chips];
	bitfury_inp_t rxbuf_copy[n_chips];
	
	// NOTE: This code assumes:
	// 1) that chips on the same SPI bus are grouped together
	// 2) that chips are in sequential fasync order
	n_chips = 0;
	for (proc = master_thr->cgpu; proc; proc = proc->next_proc)
	{
		thr = proc->thr[0];
		bitfury = proc->device_data;
		tv_stat = bitfury->tv_stat;
		skip_stat = bitfury->skip_stat;
		
		should_be_running = (proc->deven == DEV_ENABLED && !thr->pause);
		
		if (should_be_running)
		{
			if (spi != bitfury->spi)
			{
				if (spi)
					spi_txrx(spi);
				spi = bitfury->spi;
				spi_clear_buf(spi);
				spi_emit_break(spi);
				lastchip = 0;
			}
			procs[n_chips] = proc;
			spi_emit_fasync(spi, bitfury->fasync - lastchip);
			lastchip = bitfury->fasync;
			rxbuf[n_chips] = spi_emit_data(spi, 0x3000, &bitfury->atrvec[0], 19 * 4);
			++n_chips;
		}
		else
		if (thr->work /* is currently running */ && thr->busy_state != TBS_STARTING_JOB)
			;//FIXME: shutdown chip
	}
	timer_set_now(&tv_now);
	spi_txrx(spi);
	
	for (j = 0; j < n_chips; ++j)
	{
		memcpy(rxbuf_copy[j], rxbuf[j], 0x11 * 4);
		rxbuf[j] = rxbuf_copy[j];
	}
	
	for (j = 0; j < n_chips; ++j)
	{
		proc = procs[j];
		thr = proc->thr[0];
		bitfury = proc->device_data;
		uint32_t * const newbuf = &bitfury->newbuf[0];
		uint32_t * const oldbuf = &bitfury->oldbuf[0];
		
		inp = rxbuf[j];
		
		if (unlikely(bitfury->desync_counter == 99))
		{
			bitfury_init_oldbuf(proc, inp);
			goto out;
		}
		
		if (opt_debug)
			bitfury_debug_nonce_array(proc, "Read", inp);
		
		// To avoid dealing with wrap-around entirely, we rotate array so previous active uint32_t is at index 0
		memcpy(&newbuf[0], &inp[bitfury->active], 4 * (0x10 - bitfury->active));
		memcpy(&newbuf[0x10 - bitfury->active], &inp[0], 4 * bitfury->active);
		newjob = inp[0x10];
		
		if (newbuf[0xf] != oldbuf[0xf])
		{
			inc_hw_errors2(thr, NULL, NULL);
			if (unlikely(++bitfury->desync_counter >= 4))
			{
				applog(LOG_WARNING, "%"PRIpreprv": Previous nonce mismatch (4th try), recalibrating",
				       proc->proc_repr);
				bitfury_init_oldbuf(proc, inp);
				continue;
			}
			applog(LOG_DEBUG, "%"PRIpreprv": Previous nonce mismatch, ignoring response",
			       proc->proc_repr);
			goto out;
		}
		else
			bitfury->desync_counter = 0;
  
		if (bitfury->oldjob != newjob && thr->next_work)
		{
			mt_job_transition(thr);
			// TODO: Delay morework until right before it's needed
			timer_set_now(&thr->tv_morework);
			job_start_complete(thr);
		}
    
		for (n = 0; newbuf[n] == oldbuf[n]; ++n)
		{
			if (unlikely(n >= 0xf))
			{
				inc_hw_errors2(thr, NULL, NULL);
				applog(LOG_DEBUG, "%"PRIpreprv": Full result match, reinitialising",
				       proc->proc_repr);
				send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
				bitfury->desync_counter = 99;
				goto out;
			}
		}

		counter = bitfury_decnonce(newbuf[n]);
		if ((counter & 0xFFC00000) == 0xdf800000)
		{
			static int skip;

			counter -= 0xdf800000;
			if (!skip)
			{
				double expected_ghash;
				double ns;
				struct timeval d_time;
				unsigned int cycles;
				long long unsigned int period;

				cycles = counter < bitfury->counter1 ? 0x00400000 - bitfury->counter1 + counter : counter - bitfury->counter1;
				timersub(&(tv_now), &(bitfury->timer1), &d_time);
				period = timeval_to_us(&d_time) * 1000ULL;
				ns = (double)period / (double)(cycles);
				bitfury->mhz = 1.0 / ns * 65.0 * 1000.0;
				expected_ghash = bitfury->mhz * 0.765 / 65;
			}
			skip = (skip + 1) & 0x7;

			bitfury->counter1 = counter;
			copy_time(&(bitfury->timer1), &tv_now);
		}

		if (tv_stat.tv_sec == 0 && tv_stat.tv_usec == 0) {
			copy_time(&tv_stat, &tv_now);
		}

		if(!skip_stat)
		{
			timersub(&tv_now, &tv_stat, &tv_diff);
			if (time_less(&tv_period, &tv_diff)) {
				int chip_id;
				double mh_diff, s_diff;
				struct freq_stat *c;

				chip_id = proc->device_id * 8 + proc->proc_id;
				c = &(bitfury->chip_stat[chip_id]);
				applog(LOG_DEBUG, "AAA stat chip_id: %d total_secs: %f, omh: %f, os: %f",  chip_id, total_secs, c->omh, c->os);
				// Copy current statistics
				mh_diff = bitfury->counter2 - c->omh;
				s_diff = total_secs - c->os;
				if (bitfury->osc6_bits == 52) { c->mh_52 += mh_diff; c->s_52 += s_diff;}
				if (bitfury->osc6_bits == 53) { c->mh_53 += mh_diff; c->s_53 += s_diff;}
				if (bitfury->osc6_bits == 54) { c->mh_54 += mh_diff; c->s_54 += s_diff;}
				if (bitfury->osc6_bits == 55) { c->mh_55 += mh_diff; c->s_55 += s_diff;}
				if (bitfury->osc6_bits == 56) { c->mh_56 += mh_diff; c->s_56 += s_diff;}
				c->omh = bitfury->counter2;
				c->os = total_secs;
				if (stat_done != n_chips)
					applog(LOG_DEBUG, "AAA Chip_id: %3d: %.3f/%3.0fs %.3f/%3.0fs %.3f/%3.0fs %.3f/%3.0fs %.3f/%3.0fs",
						chip_id,
						c->mh_52 / c->s_52, c->s_52,
						c->mh_53 / c->s_53, c->s_53,
						c->mh_54 / c->s_54, c->s_54,
						c->mh_55 / c->s_55, c->s_55,
						c->mh_56 / c->s_56, c->s_56
					);


				// Change freq;
				if (stat_done != n_chips) {
					select_freq(bitfury, proc);
					if (stat_done == n_chips) {
						zero_stats();
						applog(LOG_DEBUG, "AAA zero_stats() !");
					}
				} else {
					applog(LOG_DEBUG, "AAA Stable freq chip: %d, osc6_bits: %d", chip_id, bitfury->osc6_bits);
				}
			}
		}


		if (n)
		{
			if(proc->device_id == 0 && proc->proc_id == 6) {
				applog(LOG_DEBUG, "AAA proc->total_mhashes: %f, total_secs: %f", proc->total_mhashes, total_secs);
			}
			for (i = 0; i < n; ++i)
			{
				nonce = bitfury_decnonce(newbuf[i]);
				if (fudge_nonce(thr->work, &nonce))
				{
					applog(LOG_DEBUG, "%"PRIpreprv": nonce %x = %08lx (work=%p)",
					       proc->proc_repr, i, (unsigned long)nonce, thr->work);
					submit_nonce(thr, thr->work, nonce);
					bitfury->counter2 += 1;
				}
				else
				if (fudge_nonce(thr->prev_work, &nonce))
				{
					applog(LOG_DEBUG, "%"PRIpreprv": nonce %x = %08lx (prev work=%p)",
					       proc->proc_repr, i, (unsigned long)nonce, thr->prev_work);
					submit_nonce(thr, thr->prev_work, nonce);
					bitfury->counter2 += 1;
				}
				else
				{
					inc_hw_errors(thr, thr->work, nonce);
					++bitfury->sample_hwe;
					bitfury->strange_counter += 1;
				}
				if (++bitfury->sample_tot >= 0x40 || bitfury->sample_hwe >= 8)
				{
					if (bitfury->sample_hwe >= 8)
					{
						applog(LOG_WARNING, "%"PRIpreprv": %d of the last %d results were bad, reinitialising",
						       proc->proc_repr, bitfury->sample_hwe, bitfury->sample_tot);
						send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
						bitfury->desync_counter = 99;
					}
					bitfury->sample_tot = bitfury->sample_hwe = 0;
				}
			}
			bitfury->active = (bitfury->active + n) % 0x10;
		}
    
		memcpy(&oldbuf[0], &newbuf[n], 4 * (0x10 - n));
		memcpy(&oldbuf[0x10 - n], &newbuf[0], 4 * n);
		bitfury->oldjob = newjob;
    
out:
		if (unlikely(bitfury->force_reinit))
		{
			applog(LOG_DEBUG, "%"PRIpreprv": Forcing reinitialisation",
			       proc->proc_repr);
			send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
			bitfury->desync_counter = 99;
			bitfury->force_reinit = false;
		}
	}
	skip_stat = (skip_stat + 1) & 0x0;
	if (time_less(&tv_period, &tv_diff)) {
		copy_time(&tv_stat, &tv_now);
	}
	
	timer_set_delay_from_now(&master_thr->tv_poll, 10000);
}

int64_t bitfury_job_process_results(struct thr_info *thr, struct work *work, bool stopping)
{
	// Bitfury chips process only 768/1024 of the nonce range
	return 0xbd000000;
}

struct api_data *bitfury_api_device_detail(struct cgpu_info * const cgpu)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct api_data *root = NULL;
	
	root = api_add_uint(root, "fasync", &bitfury->fasync, false);
	
	return root;
}

struct api_data *bitfury_api_device_status(struct cgpu_info * const cgpu)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct api_data *root = NULL;
	int clock_bits = bitfury->osc6_bits;
	
	root = api_add_int(root, "Clock Bits", &clock_bits, true);
	root = api_add_freq(root, "Frequency", &bitfury->mhz, false);
	
	return root;
}

static
bool _bitfury_set_device_parse_setting(uint32_t * const rv, char * const setting, char * const replybuf, const int maxval)
{
	char *p;
	long int nv;
	
	if (!setting || !*setting)
	{
		sprintf(replybuf, "missing setting");
		return false;
	}
	nv = strtol(setting, &p, 0);
	if (nv > maxval || nv < 1)
	{
		sprintf(replybuf, "invalid setting");
		return false;
	}
	*rv = nv;
	return true;
}

char *bitfury_set_device(struct cgpu_info * const proc, char * const option, char * const setting, char * const replybuf)
{
	struct bitfury_device * const bitfury = proc->device_data;
	uint32_t newval;
	
	if (!strcasecmp(option, "help"))
	{
		sprintf(replybuf, "baud: SPI baud rate\nosc6_bits: range 1-%d (slow to fast)", BITFURY_MAX_OSC6_BITS);
		return replybuf;
	}
	
	if (!strcasecmp(option, "baud"))
	{
		if (!_bitfury_set_device_parse_setting(&bitfury->spi->speed, setting, replybuf, INT_MAX))
			return replybuf;
		
		return NULL;
	}
	
	if (!strcasecmp(option, "osc6_bits"))
	{
		newval = bitfury->osc6_bits;
		if (!_bitfury_set_device_parse_setting(&newval, setting, replybuf, BITFURY_MAX_OSC6_BITS))
			return replybuf;
		
		bitfury->osc6_bits = newval;
		bitfury->force_reinit = true;
		
		return NULL;
	}
	
	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

#ifdef HAVE_CURSES
void bitfury_tui_wlogprint_choices(struct cgpu_info *cgpu)
{
	wlogprint("[O]scillator bits ");
}

const char *bitfury_tui_handle_choice(struct cgpu_info *cgpu, int input)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	char buf[0x100];
	
	switch (input)
	{
		case 'o': case 'O':
		{
			int val;
			char *intvar;
			
			sprintf(buf, "Set oscillator bits (range 1-%d; slow to fast)", BITFURY_MAX_OSC6_BITS);
			intvar = curses_input(buf);
			if (!intvar)
				return "Invalid oscillator bits\n";
			val = atoi(intvar);
			free(intvar);
			if (val < 1 || val > BITFURY_MAX_OSC6_BITS)
				return "Invalid oscillator bits\n";
			
			bitfury->osc6_bits = val;
			bitfury->force_reinit = true;
			
			return "Oscillator bits changing\n";
		}
	}
	return NULL;
}

void bitfury_wlogprint_status(struct cgpu_info *cgpu)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	wlogprint("Oscillator bits: %d\n", bitfury->osc6_bits);
}
#endif

struct device_drv bitfury_drv = {
	.dname = "bitfury_gpio",
	.name = "BFY",
	.drv_detect = bitfury_detect,
	.thread_prepare = bitfury_prepare,
	.thread_init = bitfury_init,
	.queue_full = bitfury_queue_full,
	.scanwork = bitfury_scanHash,
	.thread_shutdown = bitfury_shutdown,
	.minerloop = hash_queued_work,
};

