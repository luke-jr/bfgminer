/*
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2011-2014 Luke Dashjr
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifndef WIN32
#include <sys/wait.h>
#include <sys/resource.h>
#endif
#include <libgen.h>

#include "compat.h"
#include "deviceapi.h"
#include "miner.h"
#include "logging.h"
#include "util.h"
#include "driver-cpu.h"

#if defined(unix)
	#include <errno.h>
	#include <fcntl.h>
#endif

BFG_REGISTER_DRIVER(cpu_drv)

#if defined(__linux) && defined(CPU_ZERO)  /* Linux specific policy and affinity management */
#include <sched.h>
static inline void drop_policy(void)
{
	struct sched_param param;
	param.sched_priority = 0;

#ifdef SCHED_BATCH
#ifdef SCHED_IDLE
	if (unlikely(sched_setscheduler(0, SCHED_IDLE, &param) == -1))
#endif
		sched_setscheduler(0, SCHED_BATCH, &param);
#endif
}

static inline void affine_to_cpu(int id, int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	sched_setaffinity(0, sizeof(set), &set);
	applog(LOG_INFO, "Binding cpu mining thread %d to cpu %d", id, cpu);
}
#else
static inline void drop_policy(void)
{
}

static inline void affine_to_cpu(int __maybe_unused id, int __maybe_unused cpu)
{
}
#endif



/* TODO: resolve externals */
extern char *set_int_range(const char *arg, int *i, int min, int max);
extern int dev_from_id(int thr_id);


/* chipset-optimized hash functions */
typedef bool (*sha256_func)(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);

extern bool ScanHash_4WaySSE2(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);
extern bool ScanHash_altivec_4way(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);
extern bool scanhash_via(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);
extern bool scanhash_c(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);
extern bool scanhash_cryptopp(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);
extern bool scanhash_asm32(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);
extern bool scanhash_sse2_64(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);
extern bool scanhash_sse4_64(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);
extern bool scanhash_sse2_32(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);
extern bool scanhash_scrypt(struct thr_info *, struct work *, uint32_t max_nonce, uint32_t *last_nonce, uint32_t nonce);


#ifdef USE_SHA256D
static size_t max_name_len = 0;
static char *name_spaces_pad = NULL;
#endif

const char *algo_names[] = {
#ifdef USE_SHA256D
	[ALGO_C]		= "c",
#ifdef WANT_SSE2_4WAY
	[ALGO_4WAY]		= "4way",
#endif
#ifdef WANT_VIA_PADLOCK
	[ALGO_VIA]		= "via",
#endif
	[ALGO_CRYPTOPP]		= "cryptopp",
#ifdef WANT_CRYPTOPP_ASM32
	[ALGO_CRYPTOPP_ASM32]	= "cryptopp_asm32",
#endif
#ifdef WANT_X8632_SSE2
	[ALGO_SSE2_32]		= "sse2_32",
#endif
#ifdef WANT_X8664_SSE2
	[ALGO_SSE2_64]		= "sse2_64",
#endif
#ifdef WANT_X8664_SSE4
	[ALGO_SSE4_64]		= "sse4_64",
#endif
#ifdef WANT_ALTIVEC_4WAY
    [ALGO_ALTIVEC_4WAY] = "altivec_4way",
#endif
#endif
#ifdef WANT_SCRYPT
    [ALGO_SCRYPT] = "scrypt",
#endif
#ifdef USE_SHA256D
	[ALGO_FASTAUTO] = "fastauto",
	[ALGO_AUTO] = "auto",
#endif
};

#ifdef USE_SHA256D
static const sha256_func sha256_funcs[] = {
	[ALGO_C]		= (sha256_func)scanhash_c,
#ifdef WANT_SSE2_4WAY
	[ALGO_4WAY]		= (sha256_func)ScanHash_4WaySSE2,
#endif
#ifdef WANT_ALTIVEC_4WAY
    [ALGO_ALTIVEC_4WAY] = (sha256_func) ScanHash_altivec_4way,
#endif
#ifdef WANT_VIA_PADLOCK
	[ALGO_VIA]		= (sha256_func)scanhash_via,
#endif
	[ALGO_CRYPTOPP]		=  (sha256_func)scanhash_cryptopp,
#ifdef WANT_CRYPTOPP_ASM32
	[ALGO_CRYPTOPP_ASM32]	= (sha256_func)scanhash_asm32,
#endif
#ifdef WANT_X8632_SSE2
	[ALGO_SSE2_32]		= (sha256_func)scanhash_sse2_32,
#endif
#ifdef WANT_X8664_SSE2
	[ALGO_SSE2_64]		= (sha256_func)scanhash_sse2_64,
#endif
#ifdef WANT_X8664_SSE4
	[ALGO_SSE4_64]		= (sha256_func)scanhash_sse4_64,
#endif
};
#endif

#ifdef USE_SHA256D
enum sha256_algos opt_algo = ALGO_FASTAUTO;
#endif

static bool forced_n_threads;

#ifdef USE_SHA256D
const uint32_t hash1_init[] = {
	0,0,0,0,0,0,0,0,
	0x80000000,
	  0,0,0,0,0,0,
	          0x100,
};


// Algo benchmark, crash-prone, system independent stage
double bench_algo_stage3(
	enum sha256_algos algo
)
{
	struct work work __attribute__((aligned(128)));

	get_benchmark_work(&work, false);

	static struct thr_info dummy;

	struct timeval end;
	struct timeval start;
	uint32_t max_nonce = opt_algo == ALGO_FASTAUTO ? (1<<8) : (1<<22);
	uint32_t last_nonce = 0;

	timer_set_now(&start);
			{
				sha256_func func = sha256_funcs[algo];
				(*func)(
					&dummy,
					&work,
					max_nonce,
					&last_nonce,
					0
				);
			}
	timer_set_now(&end);

	uint64_t usec_end = ((uint64_t)end.tv_sec)*1000*1000 + end.tv_usec;
	uint64_t usec_start = ((uint64_t)start.tv_sec)*1000*1000 + start.tv_usec;
	uint64_t usec_elapsed = usec_end - usec_start;

	double rate = -1.0;
	if (0<usec_elapsed) {
		rate = (1.0*(last_nonce+1))/usec_elapsed;
	}
	return rate;
}

#if defined(unix)

	// Change non-blocking status on a file descriptor
	static void set_non_blocking(
		int fd,
		int yes
	)
	{
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags<0) {
			perror("fcntl(GET) failed");
			exit(1);
		}
		flags = yes ? (flags|O_NONBLOCK) : (flags&~O_NONBLOCK);

		int r = fcntl(fd, F_SETFL, flags);
		if (r<0) {
			perror("fcntl(SET) failed");
			exit(1);
		}
	}

#endif // defined(unix)

// Algo benchmark, crash-safe, system-dependent stage
static double bench_algo_stage2(
	enum sha256_algos algo
)
{
	// Here, the gig is to safely run a piece of code that potentially
	// crashes. Unfortunately, the Right Way (tm) to do this is rather
	// heavily platform dependent :(

	double rate = -1.23457;

	#if defined(unix)

		// Make a pipe: [readFD, writeFD]
		int pfd[2];
		int r = pipe(pfd);
		if (r<0) {
			perror("pipe - failed to create pipe for --algo auto");
			exit(1);
		}

		// Make pipe non blocking
		set_non_blocking(pfd[0], 1);
		set_non_blocking(pfd[1], 1);

		// Don't allow a crashing child to kill the main process
		sighandler_t sr0 = signal(SIGPIPE, SIG_IGN);
		sighandler_t sr1 = signal(SIGPIPE, SIG_IGN);
		if (SIG_ERR==sr0 || SIG_ERR==sr1) {
			perror("signal - failed to edit signal mask for --algo auto");
			exit(1);
		}

		// Fork a child to do the actual benchmarking
		pid_t child_pid = fork();
		if (child_pid<0) {
			perror("fork - failed to create a child process for --algo auto");
			exit(1);
		}

		// Do the dangerous work in the child, knowing we might crash
		if (0==child_pid) {

			// TODO: some umask trickery to prevent coredumps

			// Benchmark this algorithm
			double r = bench_algo_stage3(algo);

			// We survived, send result to parent and bail
			int loop_count = 0;
			while (1) {
				ssize_t bytes_written = write(pfd[1], &r, sizeof(r));
				int try_again = (0==bytes_written || (bytes_written<0 && EAGAIN==errno));
				int success = (sizeof(r)==(size_t)bytes_written);

				if (success)
					break;

				if (!try_again) {
					perror("write - child failed to write benchmark result to pipe");
					exit(1);
				}

				if (5<loop_count) {
					applog(LOG_ERR, "child tried %d times to communicate with parent, giving up", loop_count);
					exit(1);
				}
				++loop_count;
				sleep(1);
			}
			exit(0);
		}

		// Parent waits for a result from child
		int loop_count = 0;
		while (1) {

			// Wait for child to die
			int status;
			int r = waitpid(child_pid, &status, WNOHANG);
			if ((child_pid==r) || (r<0 && ECHILD==errno)) {

				// Child died somehow. Grab result and bail
				double tmp;
				ssize_t bytes_read = read(pfd[0], &tmp, sizeof(tmp));
				if (sizeof(tmp)==(size_t)bytes_read)
					rate = tmp;
				break;

			} else if (r<0) {
				perror("bench_algo: waitpid failed. giving up.");
				exit(1);
			}

			// Give up on child after a ~60s
			if (60<loop_count) {
				kill(child_pid, SIGKILL);
				waitpid(child_pid, &status, 0);
				break;
			}

			// Wait a bit longer
			++loop_count;
			sleep(1);
		}

		// Close pipe
		r = close(pfd[0]);
		if (r<0) {
			perror("close - failed to close read end of pipe for --algo auto");
			exit(1);
		}
		r = close(pfd[1]);
		if (r<0) {
			perror("close - failed to close read end of pipe for --algo auto");
			exit(1);
		}

	#elif defined(WIN32)

		// Get handle to current exe
		HINSTANCE module = GetModuleHandle(0);
		if (!module) {
			applog(LOG_ERR, "failed to retrieve module handle");
			exit(1);
		}

		// Create a unique name
		char unique_name[33];
		snprintf(
			unique_name,
			sizeof(unique_name)-1,
			"bfgminer-%p",
			(void*)module
		);

		// Create and init a chunked of shared memory
		HANDLE map_handle = CreateFileMapping(
			INVALID_HANDLE_VALUE,   // use paging file
			NULL,                   // default security attributes
			PAGE_READWRITE,         // read/write access
			0,                      // size: high 32-bits
			4096,			// size: low 32-bits
			unique_name		// name of map object
		);
		if (NULL==map_handle) {
			applog(LOG_ERR, "could not create shared memory");
			exit(1);
		}

		void *shared_mem = MapViewOfFile(
			map_handle,	// object to map view of
			FILE_MAP_WRITE, // read/write access
			0,              // high offset:  map from
			0,              // low offset:   beginning
			0		// default: map entire file
		);
		if (NULL==shared_mem) {
			applog(LOG_ERR, "could not map shared memory");
			exit(1);
		}
		SetEnvironmentVariable("BFGMINER_SHARED_MEM", unique_name);
		CopyMemory(shared_mem, &rate, sizeof(rate));

		// Get path to current exe
		char cmd_line[256 + MAX_PATH];
		const size_t n = sizeof(cmd_line)-200;
		DWORD size = GetModuleFileName(module, cmd_line, n);
		if (0==size) {
			applog(LOG_ERR, "failed to retrieve module path");
			exit(1);
		}

		// Construct new command line based on that
		char buf[0x20];
		snprintf(buf, sizeof(buf), "%d", algo);
		SetEnvironmentVariable("BFGMINER_BENCH_ALGO", buf);

		// Launch a debug copy of BFGMiner
		STARTUPINFO startup_info;
		PROCESS_INFORMATION process_info;
		ZeroMemory(&startup_info, sizeof(startup_info));
		ZeroMemory(&process_info, sizeof(process_info));
		startup_info.cb = sizeof(startup_info);

		BOOL ok = CreateProcess(
			NULL,			// No module name (use command line)
			cmd_line,		// Command line
			NULL,			// Process handle not inheritable
			NULL,			// Thread handle not inheritable
			FALSE,			// Set handle inheritance to FALSE
			DEBUG_ONLY_THIS_PROCESS,// We're going to debug the child
			NULL,			// Use parent's environment block
			NULL,			// Use parent's starting directory
			&startup_info,		// Pointer to STARTUPINFO structure
			&process_info		// Pointer to PROCESS_INFORMATION structure
		);
		if (!ok) {
			applog(LOG_ERR, "CreateProcess failed with error %ld\n", (long)GetLastError() );
			exit(1);
		}

		// Debug the child (only clean way to catch exceptions)
		while (1) {

			// Wait for child to do something
			DEBUG_EVENT debug_event;
			ZeroMemory(&debug_event, sizeof(debug_event));

			BOOL ok = WaitForDebugEvent(&debug_event, 60 * 1000);
			if (!ok)
				break;

			// Decide if event is "normal"
			int go_on =
				CREATE_PROCESS_DEBUG_EVENT== debug_event.dwDebugEventCode	||
				CREATE_THREAD_DEBUG_EVENT == debug_event.dwDebugEventCode	||
				EXIT_THREAD_DEBUG_EVENT   == debug_event.dwDebugEventCode	||
				EXCEPTION_DEBUG_EVENT     == debug_event.dwDebugEventCode	||
				LOAD_DLL_DEBUG_EVENT      == debug_event.dwDebugEventCode	||
				OUTPUT_DEBUG_STRING_EVENT == debug_event.dwDebugEventCode	||
				UNLOAD_DLL_DEBUG_EVENT    == debug_event.dwDebugEventCode;
			if (!go_on)
				break;

			// Some exceptions are also "normal", apparently.
			if (EXCEPTION_DEBUG_EVENT== debug_event.dwDebugEventCode) {

				int go_on =
					EXCEPTION_BREAKPOINT== debug_event.u.Exception.ExceptionRecord.ExceptionCode;
				if (!go_on)
					break;
			}

			// If nothing unexpected happened, let child proceed
			ContinueDebugEvent(
				debug_event.dwProcessId,
				debug_event.dwThreadId,
				DBG_CONTINUE
			);
		}

		// Clean up child process
		TerminateProcess(process_info.hProcess, 1);
		CloseHandle(process_info.hProcess);
		CloseHandle(process_info.hThread);

		// Reap return value and cleanup
		CopyMemory(&rate, shared_mem, sizeof(rate));
		(void)UnmapViewOfFile(shared_mem);
		(void)CloseHandle(map_handle);

	#else

		// Not linux, not unix, not WIN32 ... do our best
		rate = bench_algo_stage3(algo);

	#endif // defined(unix)

	// Done
	return rate;
}

static void bench_algo(
	double            *best_rate,
	enum sha256_algos *best_algo,
	enum sha256_algos algo
)
{
	size_t n = max_name_len - strlen(algo_names[algo]);
	memset(name_spaces_pad, ' ', n);
	name_spaces_pad[n] = 0;

	applog(
		LOG_ERR,
		"\"%s\"%s : benchmarking algorithm ...",
		algo_names[algo],
		name_spaces_pad
	);

	double rate = bench_algo_stage2(algo);
	if (rate<0.0) {
		applog(
			LOG_ERR,
			"\"%s\"%s : algorithm fails on this platform",
			algo_names[algo],
			name_spaces_pad
		);
	} else {
		applog(
			LOG_ERR,
			"\"%s\"%s : algorithm runs at %.5f MH/s",
			algo_names[algo],
			name_spaces_pad,
			rate
		);
		if (*best_rate<rate) {
			*best_rate = rate;
			*best_algo = algo;
		}
	}
}

// Figure out the longest algorithm name
void init_max_name_len()
{
	size_t i;
	size_t nb_names = sizeof(algo_names)/sizeof(algo_names[0]);
	for (i=0; i<nb_names; ++i) {
		const char *p = algo_names[i];
		size_t name_len = p ? strlen(p) : 0;
		if (max_name_len<name_len)
			max_name_len = name_len;
	}

	name_spaces_pad = (char*) malloc(max_name_len+16);
	if (0==name_spaces_pad) {
		perror("malloc failed");
		exit(1);
	}
}

// Pick the fastest CPU hasher
static enum sha256_algos pick_fastest_algo()
{
	double best_rate = -1.0;
	enum sha256_algos best_algo = 0;
	applog(LOG_ERR, "benchmarking all sha256 algorithms ...");

	bench_algo(&best_rate, &best_algo, ALGO_C);

	#if defined(WANT_SSE2_4WAY)
		bench_algo(&best_rate, &best_algo, ALGO_4WAY);
	#endif

	#if defined(WANT_VIA_PADLOCK)
		bench_algo(&best_rate, &best_algo, ALGO_VIA);
	#endif

	bench_algo(&best_rate, &best_algo, ALGO_CRYPTOPP);

	#if defined(WANT_CRYPTOPP_ASM32)
		bench_algo(&best_rate, &best_algo, ALGO_CRYPTOPP_ASM32);
	#endif

	#if defined(WANT_X8632_SSE2)
		bench_algo(&best_rate, &best_algo, ALGO_SSE2_32);
	#endif

	#if defined(WANT_X8664_SSE2)
		bench_algo(&best_rate, &best_algo, ALGO_SSE2_64);
	#endif

	#if defined(WANT_X8664_SSE4)
		bench_algo(&best_rate, &best_algo, ALGO_SSE4_64);
	#endif

        #if defined(WANT_ALTIVEC_4WAY)
                bench_algo(&best_rate, &best_algo, ALGO_ALTIVEC_4WAY);
        #endif

	size_t n = max_name_len - strlen(algo_names[best_algo]);
	memset(name_spaces_pad, ' ', n);
	name_spaces_pad[n] = 0;
	applog(
		LOG_ERR,
		"\"%s\"%s : is fastest algorithm at %.5f MH/s",
		algo_names[best_algo],
		name_spaces_pad,
		best_rate
	);
	return best_algo;
}

char *set_algo(const char *arg, enum sha256_algos *algo)
{
	enum sha256_algos i;

	for (i = 0; i < ARRAY_SIZE(algo_names); i++) {
		if (algo_names[i] && !strcmp(arg, algo_names[i])) {
			*algo = i;
			return NULL;
		}
	}
	return "Unknown algorithm";
}

void show_algo(char buf[OPT_SHOW_LEN], const enum sha256_algos *algo)
{
	strncpy(buf, algo_names[*algo], OPT_SHOW_LEN);
}
#endif  /* USE_SHA256D */

char *force_nthreads_int(const char *arg, int *i)
{
	forced_n_threads = true;
	return set_int_range(arg, i, 0, 9999);
}

static int cpu_autodetect()
{
	RUNONCE(0);
	
	int i;

	// Reckon number of cores in the box
	#if defined(WIN32)
	{
		DWORD_PTR system_am;
		DWORD_PTR process_am;
		BOOL ok = GetProcessAffinityMask(
			GetCurrentProcess(),
			&system_am,
			&process_am
		);
		if (!ok) {
			applog(LOG_ERR, "couldn't figure out number of processors :(");
			num_processors = 1;
		} else {
			size_t n = 32;
			num_processors = 0;
			while (n--)
				if (process_am & (1<<n))
					++num_processors;
		}
	}
	#elif defined(_SC_NPROCESSORS_CONF)
		num_processors = sysconf(_SC_NPROCESSORS_CONF);
	#elif defined(CTL_HW) && defined(HW_NCPU)
		int req[] = { CTL_HW, HW_NCPU };
		size_t len = sizeof(num_processors);
		sysctl(req, 2, &num_processors, &len, NULL, 0);
	#else
		num_processors = 1;
	#endif /* !WIN32 */

	if (opt_n_threads < 0 || !forced_n_threads) {
			opt_n_threads = num_processors;
	}
	if (num_processors < 1)
		return 0;

	cpus = calloc(opt_n_threads, sizeof(struct cgpu_info));
	if (unlikely(!cpus))
		quit(1, "Failed to calloc cpus");
	for (i = 0; i < opt_n_threads; ++i) {
		struct cgpu_info *cgpu;

		cgpu = &cpus[i];
		cgpu->drv = &cpu_drv;
		cgpu->deven = DEV_ENABLED;
		cgpu->threads = 1;
#ifdef USE_SHA256D
		cgpu->kname = algo_names[opt_algo];
#endif
		add_cgpu(cgpu);
	}
	return opt_n_threads;
}

static void cpu_detect()
{
	noserial_detect_manual(&cpu_drv, cpu_autodetect);
}

static pthread_mutex_t cpualgo_lock;

static bool cpu_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	
	if (!(cgpu->device_id || thr->device_thread || cgpu->proc_id))
		mutex_init(&cpualgo_lock);
	
	thread_reportin(thr);

	return true;
}

static uint64_t cpu_can_limit_work(struct thr_info __maybe_unused *thr)
{
	return 0xffff;
}

static bool cpu_thread_init(struct thr_info *thr)
{
	const int thr_id = thr->id;
#ifdef USE_SHA256D
	struct cgpu_info *cgpu = thr->cgpu;

	mutex_lock(&cpualgo_lock);
	switch (opt_algo)
	{
		case ALGO_AUTO:
		case ALGO_FASTAUTO:
			opt_algo = pick_fastest_algo();
		default:
			break;
	}
	mutex_unlock(&cpualgo_lock);

	cgpu->kname = algo_names[opt_algo];
#endif
	
	/* Set worker threads to nice 19 and then preferentially to SCHED_IDLE
	 * and if that fails, then SCHED_BATCH. No need for this to be an
	 * error if it fails */
	setpriority(PRIO_PROCESS, 0, 19);
	drop_policy();
	/* Cpu affinity only makes sense if the number of threads is a multiple
	 * of the number of CPUs */
	if (num_processors > 1 && opt_n_threads % num_processors == 0)
		affine_to_cpu(dev_from_id(thr_id), dev_from_id(thr_id) % num_processors);
	return true;
}

static
float cpu_min_nonce_diff(struct cgpu_info * const proc, const struct mining_algorithm * const malgo)
{
	return minimum_pdiff;
}

static
bool scanhash_generic(struct thr_info * const thr, struct work * const work, const uint32_t max_nonce, uint32_t * const last_nonce, uint32_t n)
{
	struct mining_algorithm * const malgo = work_mining_algorithm(work);
	void (* const hash_data_f)(void *, const void *) = malgo->hash_data_f;
	uint8_t * const hash = work->hash;
	uint8_t *data = work->data;
	const uint8_t * const target = work->target;
	uint32_t * const out_nonce = (uint32_t *)&data[0x4c];
	bool ret = false;
	
	const uint32_t hash7_targ = le32toh(((const uint32_t *)target)[7]);
	uint32_t * const hash7_tmp = &((uint32_t *)hash)[7];
	
	while (true)
	{
		*out_nonce = n;
		
		hash_data_f(hash, data);
		
		if (unlikely(le32toh(*hash7_tmp) <= hash7_targ))
		{
			ret = true;
			break;
		}

		if ((n >= max_nonce) || thr->work_restart)
			break;

		n++;
	}
	
	*last_nonce = n;
	return ret;
}

static int64_t cpu_scanhash(struct thr_info *thr, struct work *work, int64_t max_nonce)
{
	uint32_t first_nonce = work->blk.nonce;
	uint32_t last_nonce;
	bool rc;

CPUSearch:
	last_nonce = first_nonce;
	rc = false;

	/* scan nonces for a proof-of-work hash */
	{
		sha256_func func = scanhash_generic;
		switch (work_mining_algorithm(work)->algo)
		{
#ifdef USE_SCRYPT
			case POW_SCRYPT:
				func = scanhash_scrypt;
				break;
#endif
#ifdef USE_SHA256D
			case POW_SHA256D:
				if (work->nonce_diff >= 1.)
					func = sha256_funcs[opt_algo];
				break;
#endif
			default:
				break;
		}
		if (unlikely(!func))
			applogr(0, LOG_ERR, "%"PRIpreprv": Unknown mining algorithm", thr->cgpu->proc_repr);
		rc = (*func)(
			thr,
			work,
			max_nonce,
			&last_nonce,
			work->blk.nonce
		);
	}

	/* if nonce found, submit work */
	if (unlikely(rc)) {
		applog(LOG_DEBUG, "%"PRIpreprv" found something?", thr->cgpu->proc_repr);
		submit_nonce(thr, work, le32toh(*(uint32_t*)&work->data[76]));
		work->blk.nonce = last_nonce + 1;
		goto CPUSearch;
	}
	else
	if (unlikely(last_nonce == first_nonce))
		return 0;

	work->blk.nonce = last_nonce + 1;
	return last_nonce - first_nonce + 1;
}

struct device_drv cpu_drv = {
	.dname = "cpu",
	.name = "CPU",
	.probe_priority = 120,
	.drv_min_nonce_diff = cpu_min_nonce_diff,
	.drv_detect = cpu_detect,
	.thread_prepare = cpu_thread_prepare,
	.can_limit_work = cpu_can_limit_work,
	.thread_init = cpu_thread_init,
	.scanhash = cpu_scanhash,
};
