struct modminer_fpga_state {
	bool work_running;
	struct work running_work;
	struct work last_work;
	struct timeval tv_workstart;
	uint32_t hashes;

	char next_work_cmd[46];

	struct dclk_data dclk;
	uint8_t freqMaxMaxM;
	// Number of nonces didn't meet pdiff 1, ever
	int bad_share_counter;
	// Number of nonces did meet pdiff 1, ever
	int good_share_counter;
	// Time the clock was last reduced due to temperature
	struct timeval tv_last_cutoff_reduced;

	unsigned char temp;

	unsigned char pdone;
	uint8_t min_clock;
};

static bool
modminer_fpga_prepare(struct thr_info *thr)
{
	struct cgpu_info *proc = thr->cgpu;
	struct cgpu_info *modminer = proc->device;

	// Don't need to lock the mutex here, since prepare runs from the main thread before the miner threads start
	if (modminer->device->device_fd == -1 && !modminer_device_prepare(modminer))
		return false;

	struct modminer_fpga_state *state;
	state = thr->cgpu_data = calloc(1, sizeof(struct modminer_fpga_state));
	dclk_prepare(&state->dclk);
	state->min_clock = MODMINER_MIN_CLOCK;
	state->dclk.freqMinM = state->min_clock / 2;
	state->next_work_cmd[0] = MODMINER_SEND_WORK;
	state->next_work_cmd[1] = proc->proc_id;  // FPGA id
	
	proc->status = LIFE_INIT2;

	return true;
}

static
bool
modminer_reduce_clock(struct thr_info*thr, bool needlock)
{
	struct modminer_fpga_state *state = thr->cgpu_data;

	if (state->dclk.freqM <= state->min_clock / 2)
		return false;

	return modminer_change_clock(thr, needlock, -2);
}

static
void
modminer_fpga_init(struct thr_info *thr)
{
	struct cgpu_info *proc = thr->cgpu;
	struct cgpu_info *modminer = proc->device;
	struct modminer_fpga_state *state = thr->cgpu_data;

	state->dclk.freqM = MODMINER_MAX_CLOCK / 2 + 1;  // Will be reduced immediately
	while (1) {
		if (state->dclk.freqM <= state->min_clock / 2)
			bailout2(LOG_ERR, "%s: Hit minimum trying to find acceptable frequencies", modminer->proc_repr);
		--state->dclk.freqM;
		if (!modminer_change_clock(thr, false, 0))
			// MCU rejected assignment
			continue;
		if (!_modminer_get_nonce(modminer, proc->proc_id, &nonce))
			bailout2(LOG_ERR, "%s: Error detecting acceptable frequencies", modminer->proc_repr);
		if (!memcmp(&nonce, "\x00\xff\xff\xff", 4))
			// MCU took assignment, but disabled FPGA
			continue;
		break;
	}

	applog(LOG_WARNING, "%s: Frequency set to %u MHz (range: %u-%u)",
		modminer->proc_repr, state->dclk.freqM * 2, state->min_clock, state->dclk.freqMaxM * 2);
}

static char *
modminer_set_device(struct cgpu_info *modminer, char *option, char *setting, char *replybuf)
{
	int val;

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "clock: range %d-%d and a multiple of 2, min_clock: range %d-%d and a multiple of 2",
				MODMINER_MIN_CLOCK, MODMINER_MAX_CLOCK, MODMINER_MIN_CLOCK, MODMINER_MAX_CLOCK);
		return replybuf;
	}

	if (strcasecmp(option, "clock") == 0) {
		// ... existing clock logic ...
	}

	if (strcasecmp(option, "min_clock") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing min_clock setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < MODMINER_MIN_CLOCK || val > MODMINER_MAX_CLOCK || (val & 1) != 0) {
			sprintf(replybuf, "invalid min_clock: '%s' valid range %d-%d and a multiple of 2",
					setting, MODMINER_MIN_CLOCK, MODMINER_MAX_CLOCK);
			return replybuf;
		}

		for (int i = modminer->threads; i--; ) {
			struct modminer_fpga_state *s = modminer->thr[i]->cgpu_data;
			s->min_clock = val;
			s->dclk.freqMinM = val / 2;
		}

		return NULL;
	}

	return NULL;
}

static const char *
modminer_tui_handle_choice(struct cgpu_info *cgpu, int input)
{
	char buf[128];
	// ... existing TUI logic ...

	switch (input) {
		// ... existing cases ...
		case 'm': case 'M':
		{
			int val;
			char *intvar;

			sprintf(buf, "Set min clock speed (range %d-%d, multiple of 2)", MODMINER_MIN_CLOCK, MODMINER_MAX_CLOCK);
			intvar = curses_input(buf);
			if (!intvar)
				return "Invalid min clock speed\n";
			val = atoi(intvar);
			free(intvar);
			if (val < MODMINER_MIN_CLOCK || val > MODMINER_MAX_CLOCK || (val & 1) != 0)
				return "Invalid min clock speed\n";

			for (int i = cgpu->threads; i--; ) {
				struct modminer_fpga_state *s = cgpu->thr[i]->cgpu_data;
				s->min_clock = val;
				s->dclk.freqMinM = val / 2;
			}
			return "Min clock speed changed\n";
		}
	}

	return NULL;
}