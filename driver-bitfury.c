/*
 * Copyright 2013 bitfury
 * Copyright 2013 Anatoly Legkodymov
 * Copyright 2013-2014 Luke Dashjr
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
#include <stdbool.h>
#include <stdint.h>
#include <sha2.h>

#include "deviceapi.h"
#include "driver-bitfury.h"
#include "libbitfury.h"
#include "lowl-spi.h"
#include "util.h"

static const int chipgen_timeout_secs = 30;

BFG_REGISTER_DRIVER(bitfury_drv)
const struct bfg_set_device_definition bitfury_set_device_funcs[];

static
int bitfury_autodetect()
{
	RUNONCE(0);
	
	int chip_n;
	struct cgpu_info *bitfury_info;

	applog(LOG_INFO, "INFO: bitfury_detect");
	spi_init();
	if (!sys_spi)
		return 0;
	
	bitfury_info = calloc(1, sizeof(struct cgpu_info));
	bitfury_info->drv = &bitfury_drv;
	bitfury_info->threads = 1;
	
	{
		struct bitfury_device dummy_bitfury = {
			.spi = sys_spi,
		};
		drv_set_defaults(&bitfury_drv, bitfury_set_device_funcs_probe, &dummy_bitfury, NULL, NULL, 1);
	}
	
	chip_n = libbitfury_detectChips1(sys_spi);
	if (!chip_n) {
		applog(LOG_WARNING, "No Bitfury chips detected!");
		free(bitfury_info);
		return 0;
	} else {
		applog(LOG_WARNING, "BITFURY: %d chips detected!", chip_n);
	}

	bitfury_info->procs = chip_n;
	bitfury_info->set_device_funcs = bitfury_set_device_funcs;
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
	uint32_t *inp_new;
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
	swap32tole(buf, inp, 0x10);
	inp_new = bitfury_just_io(bitfury);
	swap32tole(inp_new, inp_new, 0x10);
	inp = inp_new;
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
	
	if (opt_dev_protocol)
		bitfury_debug_nonce_array(proc, "Init", inp);
	
	return true;
}

bool bitfury_init_chip(struct cgpu_info * const proc)
{
	struct bitfury_device * const bitfury = proc->device_data;
	struct bitfury_payload payload = {
		.midstate = "\x33\xfb\x46\xdc\x61\x2a\x7a\x23\xf0\xa2\x2d\x63\x31\x54\x21\xdc"
		            "\xae\x86\xfe\xc3\x88\xc1\x9c\x8c\x20\x18\x10\x68\xfc\x95\x3f\xf7",
		.m7    = htole32(0xc3baafef),
		.ntime = htole32(0x326fa351),
		.nbits = htole32(0x6461011a),
	};
	bitfury_payload_to_atrvec(bitfury->atrvec, &payload);
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
		bitfury->osc6_bits = 50;
		bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
	}
	
	timer_set_now(&thr->tv_poll);
	
	return true;
}

void bitfury_disable(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	
	applog(LOG_DEBUG, "%"PRIpreprv": Shutting down chip (disable)", proc->proc_repr);
	bitfury_send_shutdown(bitfury->spi, bitfury->slot, bitfury->fasync);
}

void bitfury_enable(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	struct cgpu_info * const dev = proc->device;
	struct thr_info * const master_thr = dev->thr[0];
	
	applog(LOG_DEBUG, "%"PRIpreprv": Reinitialising chip (enable)", proc->proc_repr);
	bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
	bitfury_init_chip(proc);
	
	if (!timer_isset(&master_thr->tv_poll))
		timer_set_now(&master_thr->tv_poll);
}

void bitfury_shutdown(struct thr_info *thr) {
	struct cgpu_info *cgpu = thr->cgpu, *proc;
	struct bitfury_device *bitfury;
	
	applog(LOG_INFO, "INFO bitfury_shutdown");
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		bitfury = proc->device_data;
		bitfury_send_shutdown(bitfury->spi, bitfury->slot, bitfury->fasync);
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
	work_to_bitfury_payload(&bitfury->payload, work);
	if (bitfury->chipgen)
		bitfury_payload_to_atrvec(bitfury->atrvec, &bitfury->payload);
	
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

// freq_stat->{mh,s} are allocated such that [osc6_min] is the first valid index and [0] falls outside the allocation

void bitfury_init_freq_stat(struct freq_stat * const c, const int osc6_min, const int osc6_max)
{
	const int osc6_values = (osc6_max + 1 - osc6_min);
	void * const p = calloc(osc6_values, (sizeof(*c->mh) + sizeof(*c->s)));
	c->mh = p - (sizeof(*c->mh) * osc6_min);
	c->s = p + (sizeof(*c->mh) * osc6_values) - (sizeof(*c->s) * osc6_min);
	c->osc6_min = osc6_min;
	c->osc6_max = osc6_max;
}

void bitfury_clean_freq_stat(struct freq_stat * const c)
{
	free(&c->mh[c->osc6_min]);
}

#define HOP_DONE 600

typedef uint32_t bitfury_inp_t[0x11];

static
int bitfury_select_freq(struct bitfury_device *bitfury, struct cgpu_info *proc) {
	int freq;
	int random;
	int i;
	bool all_done;
	struct freq_stat *c;
	
	c = &bitfury->chip_stat;
	
	if (c->best_done) {
		freq = c->best_osc;
	} else {
		random = (int)(bitfury->mhz * 1000.0) & 1;
		freq = (bitfury->osc6_bits == c->osc6_max) ? c->osc6_min : bitfury->osc6_bits + random;
		all_done = true;
		for (i = c->osc6_min; i <= c->osc6_max; ++i)
			if (c->s[i] <= HOP_DONE)
			{
				all_done = false;
				break;
			}
		if (all_done)
		{
			double mh_max = 0.0;
			
			for (i = c->osc6_min; i <= c->osc6_max; ++i)
			{
				const double mh_actual = c->mh[i] / c->s[i];
				if (mh_max >= mh_actual)
					continue;
				mh_max = mh_actual;
				freq = i;
			}
			c->best_done = 1;
			c->best_osc = freq;
			applog(LOG_DEBUG, "%"PRIpreprv": best_osc = %d",
			       proc->proc_repr, freq);
		}
	}
	applog(LOG_DEBUG, "%"PRIpreprv": Changing osc6_bits to %d",
	       proc->proc_repr, freq);
	bitfury->osc6_bits = freq;
	bitfury_send_freq(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
	return 0;
}

void bitfury_do_io(struct thr_info * const master_thr)
{
	struct cgpu_info *proc;
	struct thr_info *thr;
	struct bitfury_device *bitfury;
	struct freq_stat *c;
	const uint32_t *inp;
	int n, i, j;
	bool newjob;
	uint32_t nonce;
	int n_chips = 0, lastchip = 0;
	struct spi_port *spi = NULL;
	bool should_be_running;
	struct timeval tv_now;
	uint32_t counter;
	struct timeval *tvp_stat;
	
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
		
		should_be_running = (proc->deven == DEV_ENABLED && !thr->pause);
		
		if (should_be_running || thr->_job_transition_in_progress)
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
	if (!spi)
	{
		timer_unset(&master_thr->tv_poll);
		return;
	}
	timer_set_now(&tv_now);
	spi_txrx(spi);
	
	for (j = 0; j < n_chips; ++j)
	{
		swap32tole(rxbuf_copy[j], rxbuf[j], 0x11);
		rxbuf[j] = rxbuf_copy[j];
	}
	
	for (j = 0; j < n_chips; ++j)
	{
		proc = procs[j];
		thr = proc->thr[0];
		bitfury = proc->device_data;
		tvp_stat = &bitfury->tv_stat;
		c = &bitfury->chip_stat;
		uint32_t * const newbuf = &bitfury->newbuf[0];
		uint32_t * const oldbuf = &bitfury->oldbuf[0];
		
		if (tvp_stat->tv_sec == 0 && tvp_stat->tv_usec == 0) {
			copy_time(tvp_stat, &tv_now);
		}
		
		int stat_elapsed_secs = timer_elapsed(tvp_stat, &tv_now);
		
		inp = rxbuf[j];
		
		if (unlikely(bitfury->desync_counter == 99))
		{
			bitfury_init_oldbuf(proc, inp);
			goto out;
		}
		
		if (opt_dev_protocol)
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
		
		if (bitfury->oldjob != newjob && thr->next_work && bitfury->chipgen)
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
				bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
				bitfury->desync_counter = 99;
				goto out;
			}
		}
		
		counter = bitfury_decnonce(newbuf[n]);
		if ((counter & 0xFFC00000) == 0xdf800000)
		{
			counter &= 0x003fffff;
			int32_t cycles = counter - bitfury->counter1;
			if (cycles < 0)
				cycles += 0x00400000;
			
			if (cycles & 0x00200000)
			{
				long long unsigned int period;
				double ns;
				struct timeval d_time;
				
				timersub(&(tv_now), &(bitfury->timer1), &d_time);
				period = timeval_to_us(&d_time) * 1000ULL;
				ns = (double)period / (double)(cycles);
				bitfury->mhz = 1.0 / ns * 65.0 * 1000.0;
				
				if (bitfury->mhz_best)
				{
					const double mhz_half_best = bitfury->mhz_best / 2;
					if (bitfury->mhz < mhz_half_best && bitfury->mhz_last < mhz_half_best)
					{
						applog(LOG_WARNING, "%"PRIpreprv": Frequency drop over 50%% detected, reinitialising",
						       proc->proc_repr);
						bitfury->force_reinit = true;
					}
				}
				if ((int)bitfury->mhz > bitfury->mhz_best && bitfury->mhz_last > bitfury->mhz_best)
				{
					// mhz_best is the lowest of two sequential readings over the previous best
					if ((int)bitfury->mhz > bitfury->mhz_last)
						bitfury->mhz_best = bitfury->mhz_last;
					else
						bitfury->mhz_best = bitfury->mhz;
				}
				bitfury->mhz_last = bitfury->mhz;
				
				bitfury->counter1 = counter;
				copy_time(&(bitfury->timer1), &tv_now);
			}
		}
		
		if (c->osc6_max)
		{
			if (stat_elapsed_secs >= 60)
			{
				double mh_diff, s_diff;
				const int osc = bitfury->osc6_bits;
				
				// Copy current statistics
				mh_diff = bitfury->counter2 - c->omh;
				s_diff = total_secs - c->os;
				applog(LOG_DEBUG, "%"PRIpreprv": %.0f completed in %f seconds",
				       proc->proc_repr, mh_diff, s_diff);
				if (osc >= c->osc6_min && osc <= c->osc6_max)
				{
					c->mh[osc] += mh_diff;
					c->s[osc] += s_diff;
				}
				c->omh = bitfury->counter2;
				c->os = total_secs;
				if (opt_debug && !c->best_done)
				{
					char logbuf[0x100];
					logbuf[0] = '\0';
					for (i = c->osc6_min; i <= c->osc6_max; ++i)
						tailsprintf(logbuf, sizeof(logbuf), " %d=%.3f/%3.0fs",
						            i, c->mh[i] / c->s[i], c->s[i]);
					applog(LOG_DEBUG, "%"PRIpreprv":%s",
					       proc->proc_repr, logbuf);
				}
				
				// Change freq;
				if (!c->best_done) {
					bitfury_select_freq(bitfury, proc);
				} else {
					applog(LOG_DEBUG, "%"PRIpreprv": Stable freq, osc6_bits: %d",
					       proc->proc_repr, bitfury->osc6_bits);
				}
			}
		}
		
		if (n)
		{
			for (i = 0; i < n; ++i)
			{
				nonce = bitfury_decnonce(newbuf[i]);
				if (unlikely(!bitfury->chipgen))
				{
					switch (nonce & 0xe03fffff)
					{
						case 0x40060f87:
						case 0x600054e0:
						case 0x80156423:
						case 0x991abced:
						case 0xa004b2a0:
							if (++bitfury->chipgen_probe > 0x10)
								bitfury->chipgen = 1;
							break;
						case 0xe03081a3:
						case 0xe003df88:
							bitfury->chipgen = 2;
					}
					if (bitfury->chipgen)
						goto chipgen_detected;
				}
				else
				if (fudge_nonce(thr->work, &nonce))
				{
					applog(LOG_DEBUG, "%"PRIpreprv": nonce %x = %08lx (work=%p)",
					       proc->proc_repr, i, (unsigned long)nonce, thr->work);
					submit_nonce(thr, thr->work, nonce);
					bitfury->counter2 += 1;
				}
				else
				if (!thr->prev_work)
					applog(LOG_DEBUG, "%"PRIpreprv": Ignoring unrecognised nonce %08lx (no prev work)",
					       proc->proc_repr, (unsigned long)be32toh(nonce));
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
						bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
						bitfury->desync_counter = 99;
					}
					bitfury->sample_tot = bitfury->sample_hwe = 0;
				}
			}
			if ((!bitfury->chipgen) && stat_elapsed_secs >= chipgen_timeout_secs)
			{
				bitfury->chipgen = 1;
				applog(LOG_WARNING, "%"PRIpreprv": Failed to detect chip generation in %d seconds, falling back to gen%d assumption",
				       proc->proc_repr, chipgen_timeout_secs, bitfury->chipgen);
chipgen_detected:
				applog(LOG_DEBUG, "%"PRIpreprv": Detected bitfury gen%d chip",
				       proc->proc_repr, bitfury->chipgen);
				bitfury_payload_to_atrvec(bitfury->atrvec, &bitfury->payload);
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
			bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
			bitfury->desync_counter = 99;
			bitfury->mhz_last = 0;
			bitfury->mhz_best = 0;
			bitfury->force_reinit = false;
		}
		if (stat_elapsed_secs >= 60)
			copy_time(tvp_stat, &tv_now);
	}
	
	timer_set_delay(&master_thr->tv_poll, &tv_now, 10000);
}

int64_t bitfury_job_process_results(struct thr_info *thr, struct work *work, bool stopping)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	switch (bitfury->chipgen)
	{
		default:
		case 1:
			// Bitfury gen1 chips process only 756/1024 of the nonce range
			return 0xbd000000;
		case 2:
			// Bitfury gen2 chips process only 864/1024 of the nonce range
			return 0xd8000000;
	}
}

struct api_data *bitfury_api_device_detail(struct cgpu_info * const cgpu)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct api_data *root = NULL;
	
	root = api_add_uint(root, "fasync", &bitfury->fasync, false);
	if (bitfury->chipgen)
		root = api_add_int(root, "Chip Generation", &bitfury->chipgen, false);
	
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
bool _bitfury_set_device_parse_setting(uint32_t * const rv, const char * const setting, char * const replybuf, const int maxval)
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

const char *bitfury_set_baud(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct bitfury_device * const bitfury = proc->device_data;
	if (!_bitfury_set_device_parse_setting(&bitfury->spi->speed, setting, replybuf, INT_MAX))
		return replybuf;
	
	return NULL;
}

const char *bitfury_set_osc6_bits(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct bitfury_device * const bitfury = proc->device_data;
	uint32_t newval;
	struct freq_stat * const c = &bitfury->chip_stat;
	
	newval = bitfury->osc6_bits;
	if (!_bitfury_set_device_parse_setting(&newval, setting, replybuf, BITFURY_MAX_OSC6_BITS))
		return replybuf;
	
	bitfury->osc6_bits = newval;
	bitfury->force_reinit = true;
	c->osc6_max = 0;
	
	return NULL;
}

const struct bfg_set_device_definition bitfury_set_device_funcs[] = {
	{"osc6_bits", bitfury_set_osc6_bits, "range 1-"BITFURY_MAX_OSC6_BITS_S" (slow to fast)"},
	// NOTE: bitfury_set_device_funcs_probe should begin here:
	{"baud", bitfury_set_baud, "SPI baud rate"},
	{NULL},
};
const struct bfg_set_device_definition *bitfury_set_device_funcs_probe = &bitfury_set_device_funcs[1];

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
			struct freq_stat * const c = &bitfury->chip_stat;
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
			c->osc6_max = 0;
			
			return "Oscillator bits changing\n";
		}
	}
	return NULL;
}

void bitfury_wlogprint_status(struct cgpu_info *cgpu)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	wlogprint("Oscillator bits: %d", bitfury->osc6_bits);
	if (bitfury->chipgen)
		wlogprint("  Chip generation: %d", bitfury->chipgen);
	wlogprint("\n");
}
#endif

struct device_drv bitfury_drv = {
	.dname = "bitfury_gpio",
	.name = "BFY",
	.drv_detect = bitfury_detect,
	
	.thread_init = bitfury_init,
	.thread_disable = bitfury_disable,
	.thread_enable = bitfury_enable,
	.thread_shutdown = bitfury_shutdown,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = bitfury_do_io,
	.job_process_results = bitfury_job_process_results,
	
	.get_api_extra_device_detail = bitfury_api_device_detail,
	.get_api_extra_device_status = bitfury_api_device_status,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitfury_wlogprint_status,
	.proc_tui_wlogprint_choices = bitfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitfury_tui_handle_choice,
#endif
};

