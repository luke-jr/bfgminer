This is a multi-threaded multi-pool ASIC, FPGA, GPU and CPU miner with ATI GPU
monitoring, (over)clocking and fanspeed support for bitcoin and derivative
coins. Do not use on multiple block chains at the same time!

This code is provided entirely free of charge by the programmer in his spare
time so donations would be greatly appreciated. Please consider donating to the
address below.

[Luke-Jr](mailto://luke-jr+bfgminer@utopios.org)

1QATWksNFGeUJCWBrN4g6hGM178Lovm7Wh

[Downloads](http://luke.dashjr.org/programs/bitcoin/files/bfgminer)

[Git tree](https://github.com/luke-jr/bfgminer)

[Bug reports](https://github.com/luke-jr/bfgminer/issues)

[IRC Channel](irc://irc.freenode.net/eligius)

License: GPLv3.  See COPYING for details.

# READ EXECUTIVE SUMMARY BELOW FOR FIRST TIME USERS!

# Dependencies:

	autoconf             http://www.gnu.org/software/autoconf/
	automake             http://www.gnu.org/software/automake/
	libtool              http://www.gnu.org/software/libtool/
	pkg-config           http://www.freedesktop.org/wiki/Software/pkg-config
	...or pkgconf        https://github.com/pkgconf/pkgconf
	libcurl4-gnutls-dev  http://curl.haxx.se/libcurl/
	libjansson-dev 2.0+  http://www.digip.org/jansson/

# Optional Dependencies:
Text-User-Interface (TUI): curses dev library; any one of:

    libncurses5-dev    http://www.gnu.org/software/ncurses/ (Linux and Mac)
	  libncursesw5-dev       ^ same
	  libpdcurses        http://pdcurses.sourceforge.net/ (Linux/Mac/Windows)

Multiple FPGA autodetection: any one of:

	  sysfs              (builtin to most Linux kernels, just mount on /sys)
	  libudev-dev        http://www.freedesktop.org/software/systemd/libudev/

X6500 and ZTEX FPGA boards:

	  libusb-1.0-0-dev   http://www.libusb.org/

ATi/AMD video card GPU mining:

	  AMD APP SDK        http://developer.amd.com/tools/heterogeneous-computing/amd-accelerated-parallel-processing-app-sdk/

CPU mining optimized assembly algorithms:

	  yasm 1.0.1+        http://yasm.tortall.net/


BFGMiner specific configuration options:

	--enable-avalon         Compile support for Avalon (default disabled)
	--enable-cpumining      Build with cpu mining support(default disabled)
	--disable-opencl        Build without support for OpenCL (default enabled)
	--disable-adl           Build without ADL monitoring (default enabled)
	--disable-bitforce      Compile support for BitForce (default enabled)
	--disable-icarus        Compile support for Icarus (default enabled)
	--disable-modminer      Compile support for ModMiner (default enabled)
	--disable-x6500         Compile support for X6500 (default enabled)
	--disable-ztex          Compile support for ZTEX (default if libusb)
	--enable-scrypt         Compile support for scrypt mining (default disabled)
	--without-curses        Compile support for curses TUI (default enabled)
	--without-libudev       Autodetect FPGAs using libudev (default enabled)

---

# To build with GPU mining support:

Install AMD APP sdk, ideal version (see [FAQ](#faq)!) - put it into a system location.
Download the correct version for either 32 bit or 64 bit from here:

    http://developer.amd.com/tools/heterogeneous-computing/amd-accelerated-parallel-processing-app-sdk/downloads/

This will give you a file with a name like:

    AMD-APP-SDK-v2.4-lnx64.tgz (64-bit)

or

    AMD-APP-SDK-v2.4-lnx32.tgz (32-bit)

Then:

    sudo -i
    cd /opt
    tar xf /path/to/AMD-APP-SDK-v2.4-lnx##.tgz
    cd /
    tar xf /opt/AMD-APP-SDK-v2.4-lnx##/icd-registration.tgz
    ln -s /opt/AMD-APP-SDK-v2.4-lnx##/include/CL /usr/include
    ln -s /opt/AMD-APP-SDK-v2.4-lnx##/lib/x86_64/* /usr/lib/
    ldconfig

Where ## is 32 or 64, depending on the bitness of the SDK you downloaded.
If you are on 32 bit, x86_64 in the 2nd last line should be x86

# Basic *nix build instructions:

    ./autogen.sh    # only needed if building from git repo
    ./configure     # NOT needed if autogen.sh used
    make

On Mac OS X, you can use Homebrew to install the dependency libraries. When you
are ready to build BFGMiner, you may need to point the configure script at one
or more pkg-config paths. For example:

	./configure PKG_CONFIG_PATH=/usr/local/opt/curl/lib/pkgconfig

# Native WIN32 build instructions: see windows-build.txt

If you build BFGMiner from source, it is recommended that you run it from the
build directory. On *nix, you will usually need to prepend your command with a
path like this (if you are in the bfgminer directory already): ./bfgminer

---

# Usage instructions:
Run `bfgminer --help` to see options:

    Usage: . [-atDdGCgIKklmpPQqrRsTouvwOchnV]
    Options for both config file and command line:
    --api-allow         Allow API access (if enabled) only to the given list of [W:]IP[/Prefix] address[/subnets]
                        This overrides --api-network and you must specify 127.0.0.1 if it is required
                        W: in front of the IP address gives that address privileged access to all api commands
    --api-description   Description placed in the API status header (default: BFGMiner version)
    --api-groups        API one letter groups G:cmd:cmd[,P:cmd:*...]
                        See README.RPC for usage
    --api-listen        Listen for API requests (default: disabled)
                        By default any command that does not just display data returns access denied
                        See --api-allow to overcome this
    --api-network       Allow API (if enabled) to listen on/for any address (default: only 127.0.0.1)
    --api-port          Port number of miner API (default: 4028)
    --balance           Change multipool strategy from failover to even share balance
    --benchmark         Run BFGMiner in benchmark mode - produces no shares
    --coinbase-addr <arg> Set coinbase payout address for solo mining
    --coinbase-sig <arg> Set coinbase signature when possible
    --compact           Use compact display without per device statistics
    --debug|-D          Enable debug output
    --debuglog          Enable debug logging
    --device|-d <arg>   Select device to use, (Use repeat -d for multiple devices, default: all)
    --disable-rejecting Automatically disable pools that continually reject shares
    --expiry|-E <arg>   Upper bound on how many seconds after getting work we consider a share from it stale (w/o longpoll active) (default: 120)
    --expiry-lp <arg>   Upper bound on how many seconds after getting work we consider a share from it stale (with longpoll active) (default: 3600)
    --failover-only     Don't leak work to backup pools when primary pool is lagging
    --force-dev-init    Always initialize devices when possible (such as bitstream uploads to some FPGAs)
    --kernel-path|-K <arg> Specify a path to where bitstream and kernel files are (default: "/usr/local/bin")
    --load-balance      Change multipool strategy from failover to efficiency based balance
    --log|-l <arg>      Interval in seconds between log output (default: 5)
    --monitor|-m <arg>  Use custom pipe cmd for output messages
    --net-delay         Impose small delays in networking to not overload slow routers
    --no-gbt            Disable getblocktemplate support
    --no-getwork        Disable getwork support
    --no-longpoll       Disable X-Long-Polling support
    --no-restart        Do not attempt to restart devices that hang
    --no-stratum        Disable Stratum detection
    --no-submit-stale   Don't submit shares if they are detected as stale
    --pass|-p <arg>     Password for bitcoin JSON-RPC server
    --per-device-stats  Force verbose mode and output per-device statistics
    --pool-proxy|-x     Proxy URI to use for connecting to just the previous-defined pool
    --protocol-dump|-P  Verbose dump of protocol-level activities
    --queue|-Q <arg>    Minimum number of work items to have queued (0 - 10) (default: 1)
    --quiet|-q          Disable logging output, display status and errors
    --real-quiet        Disable all output
    --remove-disabled   Remove disabled devices entirely, as if they didn't exist
    --request-diff <arg> Request a specific difficulty from pools (default: 1.0)
    --retries <arg>     Number of times to retry failed submissions before giving up (-1 means never) (default: -1)
    --rotate <arg>      Change multipool strategy from failover to regularly rotate at N minutes (default: 0)
    --round-robin       Change multipool strategy from failover to round robin on failure
    --scan-time|-s <arg> Upper bound on time spent scanning current work, in seconds (default: 60)
    --sched-start <arg> Set a time of day in HH:MM to start mining (a once off without a stop time)
    --sched-stop <arg>  Set a time of day in HH:MM to stop mining (will quit without a start time)
    --scrypt            Use the scrypt algorithm for mining (non-bitcoin)
    --sharelog <arg>    Append share log to file
    --shares <arg>      Quit after mining N shares (default: unlimited)
    --show-processors   Show per processor statistics in summary
    --skip-security-checks <arg> Skip security checks sometimes to save bandwidth; only check 1/<arg>th of the time (default: never skip)
    --socks-proxy <arg> Set socks4 proxy (host:port) for all pools without a proxy specified
    --submit-threads    Minimum number of concurrent share submissions (default: 64)
    --syslog            Use system log for output messages (default: standard error)
    --temp-cutoff <arg> Temperature where a device will be automatically disabled, one value or comma separated list (default: 95)
    --temp-hysteresis <arg> Set how much the temperature can fluctuate outside limits when automanaging speeds (default: 3)
    --temp-target <arg> Target temperature when automatically managing fan and clock speeds
    --text-only|-T      Disable ncurses formatted screen output
    --url|-o <arg>      URL for bitcoin JSON-RPC server
    --user|-u <arg>     Username for bitcoin JSON-RPC server
    --verbose           Log verbose output to stderr as well as status output
    --userpass|-O <arg> Username:Password pair for bitcoin JSON-RPC server
    Options for command line only:
    --config|-c <arg>   Load a JSON-format configuration file
    See example.conf for an example configuration.
    --help|-h           Print this message
    --version|-V        Display version and exit

# GPU only options:

    --auto-fan          Automatically adjust all GPU fan speeds to maintain a target temperature
    --auto-gpu          Automatically adjust all GPU engine clock speeds to maintain a target temperature
    --disable-gpu|-G    Disable GPU mining even if suitable devices exist
    --gpu-threads|-g <arg> Number of threads per GPU (1 - 10) (default: 2)
    --gpu-dyninterval <arg> Set the refresh interval in ms for GPUs using dynamic intensity (default: 7)
    --gpu-engine <arg>  GPU engine (over)clock range in MHz - one value, range and/or comma separated list (e.g. 850-900,900,750-850)
    --gpu-fan <arg>     GPU fan percentage range - one value, range and/or comma separated list (e.g. 25-85,85,65)
    --gpu-map <arg>     Map OpenCL to ADL device order manually, paired CSV (e.g. 1:0,2:1 maps OpenCL 1 to ADL 0, 2 to 1)
    --gpu-memclock <arg> Set the GPU memory (over)clock in MHz - one value for all or separate by commas for per card.
    --gpu-memdiff <arg> Set a fixed difference in clock speed between the GPU and memory in auto-gpu mode
    --gpu-platform <arg> Select OpenCL platform ID to use for GPU mining
    --gpu-powertune <arg> Set the GPU powertune percentage - one value for all or separate by commas for per card.
    --gpu-reorder       Attempt to reorder GPU devices according to PCI Bus ID
    --gpu-vddc <arg>    Set the GPU voltage in Volts - one value for all or separate by commas for per card.
    --intensity|-I <arg> Intensity of GPU scanning (d or -10 -> 10, default: d to maintain desktop interactivity)
    --kernel|-k <arg>   Override kernel to use (diablo, poclbm, phatk or diakgcn) - one value or comma separated
    --ndevs|-n          Enumerate number of detected GPUs and exit
    --no-adl            Disable the ATI display library used for monitoring and setting GPU parameters
    --temp-overheat <arg> Overheat temperature when automatically managing fan and GPU speeds (default: 85)
    --vectors|-v <arg>  Override detected optimal vector (1, 2 or 4) - one value or comma separated list
    --worksize|-w <arg> Override detected optimal worksize - one value or comma separated list


# scrypt only options:

    --lookup-gap <arg>  Set GPU lookup gap for scrypt mining, comma separated
    --thread-concurrency <arg> Set GPU thread concurrency for scrypt mining, comma separated

See README.scrypt for more information regarding (non-bitcoin) scrypt mining.


# FPGA mining boards (BitForce, Icarus, ModMiner, X6500, ZTEX) only options:

    --scan-serial|-S <arg> Serial port to probe for FPGA mining device

This option is only for BitForce, Icarus, and/or ModMiner FPGAs

To use FPGAs, you will need to be sure the user BFGMiner is running as has
appropriate permissions. This varies by operating system.

On Gentoo:

    sudo usermod <username> -a -G uucp

On Ubuntu:

    sudo usermod <username> -a -G dialout

Note that on GNU/Linux systems, you will usually need to login again before
group changes take effect.

By default, BFGMiner will scan for autodetected FPGAs unless at least one -S is
specified for that driver. If you specify `-S` and still want BFGMiner to scan,
you must also use `-S auto`. If you want to prevent BFGMiner from scanning
without specifying a device, you can use `-S noauto`. Note that presently,
autodetection only works on Linux, and might only detect one device depending
on the version of udev being used. If you want to scan all serial ports, you
can use `-S all`; note that this may write data to non-mining devices which may
then behave in unexpected ways!

On Linux, `<arg>` is usually of the format:

    /dev/ttyUSBn

On Windows, `<arg>` is usually of the format `\\.\COMn`
(where n = the correct device number for the FPGA device)

The official supplied binaries are compiled with support for all FPGAs.
To force the code to only attempt detection with a specific driver,
prepend the argument with the driver name followed by a colon.
For example, `icarus:/dev/ttyUSB0` or `bitforce:\\.\COM5`
or using the short name: `ica:/dev/ttyUSB0` or `bfl:\\.\COM5`

For other FPGA details see the README.FPGA


CPU only options (not included in binaries):

    --algo|-a <arg>     Specify sha256 implementation for CPU mining:
            auto            Benchmark at startup and pick fastest algorithm
            c               Linux kernel sha256, implemented in C
            4way            tcatm's 4-way SSE2 implementation
            via             VIA padlock implementation
            cryptopp        Crypto++ C/C++ implementation
            sse2_64         SSE2 64 bit implementation for x86_64 machines
            sse4_64         SSE4.1 64 bit implementation for x86_64 machines (default: sse2_64)
    --cpu-threads|-t <arg> Number of miner CPU threads (default: 4)
    --enable-cpu|-C     Enable CPU mining with other mining (default: no CPU mining if other devices exist)



---

# EXECUTIVE SUMMARY ON USAGE:

After saving configuration from the menu, you do not need to give BFGMiner any
arguments and it will load your configuration.

Any configuration file may also contain a single

	"include" : "filename"

to recursively include another configuration file.
Writing the configuration will save all settings from all files in the output.


Single pool, regular desktop:

    bfgminer -o http://pool:port -u username -p password

Single pool, dedicated miner:

    bfgminer -o http://pool:port -u username -p password -I 9

Single pool, first card regular desktop, 3 other dedicated cards:

    bfgminer -o http://pool:port -u username -p password -I d,9,9,9

Multiple pool, dedicated miner:

    bfgminer -o http://pool1:port -u pool1username -p pool1password -o http://pool2:port -u pool2usernmae -p pool2password -I 9

Add overclocking settings, GPU and fan control for all cards:

    bfgminer -o http://pool:port -u username -p password -I 9 --auto-fan --auto-gpu --gpu-engine 750-950 --gpu-memclock 300

Add overclocking settings, GPU and fan control with different engine settings for 4 cards:

    bfgminer -o http://pool:port -u username -p password -I 9 --auto-fan --auto-gpu --gpu-engine 750-950,945,700-930,960 --gpu-memclock 300

Single pool with a standard http proxy, regular desktop:

    bfgminer -o http://pool:port -x http://proxy:port -u username -p password

Single pool with a socks5 proxy, regular desktop:

    bfgminer -o http://pool:port -x socks5://proxy:port -u username -p password

The list of proxy types are:

    http:    standard http 1.1 proxy
    socks4:  socks4 proxy
    socks5:  socks5 proxy
    socks4a: socks4a proxy
    socks5h: socks5 proxy using a hostname

Proxy support requires cURL version 7.21.7 or newer.

If you specify the `--socks-proxy` option to BFGMiner, it will only be applied to
all pools that don't specify their own proxy setting like above

# READ WARNINGS AND DOCUMENTATION BELOW ABOUT OVERCLOCKING

On Linux you virtually always need to export your display settings before
starting to get all the cards recognised and/or temperature+clocking working:

    export DISPLAY=:0

---

WHILE RUNNING:

The following options are available while running with a single keypress:

    [P]ool management [G]PU management [S]ettings [D]isplay options [Q]uit

P gives you:

    Current pool management strategy: Failover
    [F]ailover only disabled
    [A]dd pool [R]emove pool [D]isable pool [E]nable pool
    [C]hange management strategy [S]witch pool [I]nformation

S gives you:

    [L]ongpoll: On
    [Q]ueue: 1
    [S]cantime: 60
    [E]xpiry: 120
    [R]etries: -1
    [W]rite config file
    [B]FGMiner restart

D gives you:

    [N]ormal [C]lear [S]ilent mode (disable all output)
    [D]ebug:off
    [P]er-device:off
    [Q]uiet:off
    [V]erbose:off
    [R]PC debug:off
    [W]orkTime details:off
    co[M]pact: off
    [L]og interval:5

Q quits the application.

G gives you something like:

    GPU 0: [124.2 / 191.3 Mh/s] [Q:212  A:77  R:33  HW:0  E:36%  U:1.73/m]
    Temp: 67.0 C
    Fan Speed: 35% (2500 RPM)
    Engine Clock: 960 MHz
    Memory Clock: 480 MHz
    Vddc: 1.200 V
    Activity: 93%
    Powertune: 0%
    Last initialised: [2011-09-06 12:03:56]
    Thread 0: 62.4 Mh/s Enabled ALIVE
    Thread 1: 60.2 Mh/s Enabled ALIVE

    [E]nable [D]isable [R]estart GPU [C]hange settings
    Or press any other key to continue

The running log shows output like this:

    [2013-02-13 00:26:30] Accepted 1758e8df BFL 0  pool 0 Diff 10/1
    [2013-02-13 00:26:32] Accepted 1d9a2199 MMQ 0a pool 0 Diff 8/1
    [2013-02-13 00:26:33] Accepted b1304924 ZTX 0  pool 0 Diff 1/1
    [2013-02-13 00:26:33] Accepted c3ad22f4 XBS 0b pool 0 Diff 1/1

The 8 byte hex value are the 2nd set of 32 bits from the share submitted to the
pool. The 2 diff values are the actual difficulty target that share reached
followed by the difficulty target the pool is currently asking for.

---

Also many issues and FAQs are covered in the forum thread
dedicated to this program:

[BFG Miner thread on BitcointTalk](https://bitcointalk.org/?topic=78192

The output line shows the following:

    5s:1713.6 avg:1707.8 u:1710.2 Mh/s | A:729 R:8 S:0 HW:0 U:22.53/m

Each column is as follows:

    5s:  A 5 second exponentially decaying average hash rate
    avg: An all time average hash rate
    u:   An all time average hash rate based on actual accepted shares
    A:   The number of Accepted shares
    R:   The number of Rejected shares
    S:   Stale shares discarded (not submitted so don't count as rejects)
    HW:  The number of HardWare errors
    U:   The Utility defined as the number of shares / minute

     GPU 1: 73.5C 2551RPM | 427.3/443.0/442.1Mh/s | A:8 R:0 HW:0 U:4.39/m

Each column is as follows:

    Temperature (if supported)
    Fanspeed (if supported)
    A 5 second exponentially decaying average hash rate
    An all time average hash rate
    An all time average hash rate based on actual accepted shares
    The number of accepted shares
    The number of rejected shares
    The number of hardware erorrs
    The utility defines as the number of shares / minute

The BFGMiner status line shows:

     ST: 1  DW: 0  GW: 301  LW: 8  GF: 1  NB: 1  AS: 0  RF: 1  E: 2.42

    ST is STaged work items (ready to use).
    DW is Discarded Work items (work from block no longer valid to work on)
    GW is GetWork requested (work items from pools)
    LW is Locally generated Work items
    GF is Getwork Fail Occasions (server slow to provide work)
    NB is New Blocks detected on the network
    AS is Active Submissions (shares in the process of submitting)
    RF is Remote Fail occasions (server slow to accept work)
    E  is Efficiency defined as number of shares accepted (multiplied by their
              difficulty) per 2 KB of bandwidth

NOTE: Running intensities above 9 with current hardware is likely to only
diminish return performance even if the hash rate might appear better. A good
starting baseline intensity to try on dedicated miners is 9. Higher values are
there to cope with future improvements in hardware.

The block display shows:

    Block: ...1b89f8d3 #217364  Diff:2.98M  Started: [17:17:22]  Best share: 2.71K

This shows a short stretch of the current block, the next block's height and
difficulty, when the search for the new block started, and the all time best
difficulty share you've found since starting BFGMiner this time.

---

# MULTIPOOL

## FAILOVER STRATEGIES WITH MULTIPOOL:
A number of different strategies for dealing with multipool setups are
available. Each has their advantages and disadvantages so multiple strategies
are available by user choice, as per the following list:

### FAILOVER:
The default strategy is failover. This means that if you input a number of
pools, it will try to use them as a priority list, moving away from the 1st
to the 2nd, 2nd to 3rd and so on. If any of the earlier pools recover, it will
move back to the higher priority ones.

### ROUND ROBIN:
This strategy only moves from one pool to the next when the current one falls
idle and makes no attempt to move otherwise.

### ROTATE:
This strategy moves at user-defined intervals from one active pool to the next,
skipping pools that are idle.

### LOAD BALANCE:
This strategy sends work to all the pools to maintain optimum load. The most
efficient pools will tend to get a lot more shares. If any pool falls idle, the
rest will tend to take up the slack keeping the miner busy.

### BALANCE:
This strategy monitors the amount of difficulty 1 shares solved for each pool
and uses it to try to end up doing the same amount of work for all pools.


---

# SOLO MINING

BFGMiner supports solo mining with any GBT-compatible bitcoin node (such as
`bitcoind`). To use this mode, you need to specify the URL of your bitcoind node
using the usual pool options (`--url`, `--userpass`, etc), and the `--coinbase-addr`
option to specify the Bitcoin address you wish to receive the block rewards
mined. If you are solo mining with more than one instance of BFGMiner (or any
other software) per payout address, you must also specify data using the
`--coinbase-sig` option to ensure each miner is working on unique work. Note
that this data will be publicly seen if your miner finds a block using any
GBT-enabled pool, even when not solo mining (such as failover). If your
bitcoin node does not support longpolling (for example, bitcoind 0.7.x), you
should consider setting up a failover pool to provide you with block
notifications.

Example solo mining usage:

    bfgminer -o http://localhost:8332 -u username -p password \
        --coinbase-addr 1QATWksNFGeUJCWBrN4g6hGM178Lovm7Wh \
        --coinbase-sig "rig1: This is Joe's block!"

---

# LOGGING

BFGMiner will log to stderr if it detects `stderr` is being redirected to a file.
To enable logging simply add `2>logfile.txt` to your command line and `logfile.txt`
will contain the logged output at the log level you specify (normal, verbose,
debug etc.)

In other words if you would normally use:

    ./bfgminer -o xxx -u yyy -p zzz

if you use

    ./bfgminer -o xxx -u yyy -p zzz 2>logfile.txt

it will log to a file called `logfile.txt` and otherwise work the same.

There is also the `-m` option on linux which will spawn a command of your choice
and pipe the output directly to that command.

The WorkTime details `debug` option adds details on the end of each line
displayed for Accepted or Rejected work done. An example would be:

    <-00000059.ed4834a3 M:X D:1.0 G:17:02:38:0.405 C:1.855 (2.995) W:3.440 (0.000) S:0.461 R:17:02:47

The first 2 hex codes are the previous block hash, the rest are reported in
seconds unless stated otherwise:

    The previous hash is followed by the getwork mode used M:X where X is one of
    P:Pool, T:Test Pool, L:LP or B:Benchmark,
    then D:d.ddd is the difficulty required to get a share from the work,
    then G:hh:mm:ss:n.nnn, which is when the getwork or LP was sent to the pool and
    the n.nnn is how long it took to reply,
    followed by 'O' on its own if it is an original getwork, or 'C:n.nnn' if it was
    a clone with n.nnn stating how long after the work was recieved that it was
    cloned, (m.mmm) is how long from when the original work was received until work
    started,
    W:n.nnn is how long the work took to process until it was ready to submit,
    (m.mmm) is how long from ready to submit to actually doing the submit, this is
    usually 0.000 unless there was a problem with submitting the work,
    S:n.nnn is how long it took to submit the completed work and await the reply,
    R:hh:mm:ss is the actual time the work submit reply was received

If you start BFGMiner with the `--sharelog` option, you can get detailed
information for each share found. The argument to the option may be "-" for
standard output (not advisable with the ncurses UI), any valid positive number
for that file descriptor, or a filename.

To log share data to a file named `share.log`, you can use either:

    ./bfgminer --sharelog 50 -o xxx -u yyy -p zzz 50>share.log
    ./bfgminer --sharelog share.log -o xxx -u yyy -p zzz

For every share found, data will be logged in a CSV (Comma Separated Value)
format:

    timestamp,disposition,target,pool,dev,thr,sharehash,sharedata

For example (this is wrapped, but it's all on one line for real):

    1335313090,reject,
    ffffffffffffffffffffffffffffffffffffffffffffffffffffffff00000000,
    http://localhost:8337,GPU0,0,
    6f983c918f3299b58febf95ec4d0c7094ed634bc13754553ec34fc3800000000,
    00000001a0980aff4ce4a96d53f4b89a2d5f0e765c978640fe24372a000001c5
    000000004a4366808f81d44f26df3d69d7dc4b3473385930462d9ab707b50498
    f681634a4f1f63d01a0cd43fb338000000000080000000000000000000000000
    0000000000000000000000000000000000000000000000000000000080020000

---

# OVERCLOCKING WARNING AND INFORMATION

AS WITH ALL OVERCLOCKING TOOLS YOU ARE ENTIRELY RESPONSIBLE FOR ANY HARM YOU
MAY CAUSE TO YOUR HARDWARE. OVERCLOCKING CAN INVALIDATE WARRANTIES, DAMAGE
HARDWARE AND EVEN CAUSE FIRES. THE AUTHOR ASSUMES NO RESPONSIBILITY FOR ANY
DAMAGE YOU MAY CAUSE OR UNPLANNED CHILDREN THAT MAY OCCUR AS A RESULT.

The GPU monitoring, clocking and fanspeed control incorporated into BFGMiner
comes through use of the ATI Display Library. As such, it only supports ATI
GPUs. Even if ADL support is successfully built into BFGMiner, unless the card
and driver supports it, no GPU monitoring/settings will be available.

BFGMiner supports initial setting of GPU engine clock speed, memory clock
speed, voltage, fanspeed, and the undocumented powertune feature of 69x0+ GPUs.
The setting passed to BFGMiner is used by all GPUs unless separate values are
specified. All settings can all be changed within the menu on the fly on a
per-GPU basis.

For example:

    --gpu-engine 950 --gpu-memclock 825

will try to set all GPU engine clocks to 950 and all memory clocks to 825,
while:

    --gpu-engine 950,945,930,960 --gpu-memclock 300

will try to set the engine clock of card 0 to 950, 1 to 945, 2 to 930, 3 to
960 and all memory clocks to 300.

You can substitute 0 to leave the engine clock of a card at its default.
For example, to keep the 2nd GPU to its default clocks:

    --gpu-engine 950,0,930,960 --gpu-memclock 300,0,300,300

## AUTO MODES:

There are two "auto" modes in BFGMiner, `--auto-fan` and `--auto-gpu`. These can be
used independently of each other and are complementary. Both auto modes are
designed to safely change settings while trying to maintain a target
temperature. By default this is set to 75 degrees C but can be changed with:

    --temp-target

e.g.

    --temp-target 80

Sets all cards' target temperature to 80 degrees.

    --temp-target 75,85

Sets card 0 target temperature to 75, and card 1 to 85 degrees.

### AUTO FAN:

e.g.

    --auto-fan (implies 85% upper limit)
    --gpu-fan 25-85,65 --auto-fan

Fan control in auto fan works off the theory that the minimum possible fan
required to maintain an optimal temperature will use less power, make less
noise, and prolong the life of the fan. In auto-fan mode, the fan speed is
limited to 85% if the temperature is below "overheat" intentionally, as higher
fanspeeds on GPUs do not produce signficantly more cooling, yet significantly
shorten the lifespan of the fans. If temperature reaches the overheat value,
fanspeed will still be increased to 100%. The overheat value is set to 85
degrees by default and can be changed with:

    --temp-overheat

e.g.

    --temp-overheat 75,85

Sets card 0 overheat threshold to 75 degrees and card 1 to 85.

### AUTO GPU:
e.g.

    --auto-gpu --gpu-engine 750-950
    --auto-gpu --gpu-engine 750-950,945,700-930,960

GPU control in auto gpu tries to maintain as high a clock speed as possible
while not reaching overheat temperatures. As a lower clock speed limit, the
auto-gpu mode checks the GPU card's "normal" clock speed and will not go below
this unless you have manually set a lower speed in the range. Also, unless a
higher clock speed was specified at startup, it will not raise the clockspeed.
If the temperature climbs, fanspeed is adjusted and optimised before GPU engin
e clockspeed is adjusted. If fan speed control is not available or already
optimal, then GPU clock speed is only decreased if it goes over the target
temperature by the hysteresis amount, which is set to 3 by default and can be
changed with:

    --temp-hysteresis

If the temperature drops below the target temperature, and engine clock speed
is not at the highest level set at startup, BFGMiner will raise the clock speed.
If at any time you manually set an even higher clock speed successfully in
BFGMiner, it will record this value and use it as its new upper limit (and the
same for low clock speeds and lower limits). If the temperature goes over the
cutoff limit (95 degrees by default), BFGMiner will completely disable the GPU
from mining and it will not be re-enabled unless manually done so. The cutoff
temperature can be changed with:

    --temp-cutoff

e.g.

    --temp-cutoff 95,105

Sets card 0 cutoff temperature to 95 and card 1 to 105.

    --gpu-memdiff -125

This setting will modify the memory speed whenever the GPU clock speed is
modified by `--auto-gpu`. In this example, it will set the memory speed to be 125
MHz lower than the GPU speed. This is useful for some cards like the 6970 which
normally don't allow a bigger clock speed difference. The 6970 is known to only
allow -125, while the 7970 only allows -150.


# CHANGING SETTINGS:

When setting values, it is important to realise that even though the driver
may report the value was changed successfully, and the new card power profile
information contains the values you set it to, that the card itself may
refuse to use those settings. As the performance profile changes dynamically,
querying the "current" value on the card can be wrong as well. So when changing
values in BFGMiner, after a pause of 1 second, it will report to you the current
values where you should check that your change has taken. An example is that
6970 reference cards will accept low memory values but refuse to actually run
those lower memory values unless they're within 125 of the engine clock speed.
In that scenario, they usually set their real speed back to their default.

BFGMiner reports the so-called "safe" range of whatever it is you are modifying
when you ask to modify it on the fly. However, you can change settings to values
outside this range. Despite this, the card can easily refuse to accept your
changes, or worse, to accept your changes and then silently ignore them. So
there is absolutely to know how far to/from where/to it can set things safely or
otherwise, and there is nothing stopping you from at least trying to set them
outside this range. Being very conscious of these possible failures is why
BFGMiner will report back the current values for you to examine how exactly the
card has responded. Even within the reported range of accepted values by the
card, it is very easy to crash just about any card, so it cannot use those
values to determine what range to set. You have to provide something meaningful
manually for BFGMiner to work with through experimentation.

# STARTUP / SHUTDOWN:

When BFGMiner starts up, it tries to read off the current profile information
for clock and fan speeds and stores these values. When quitting BFGMiner, it
will then try to restore the original values. Changing settings outside of
BFGMiner while it's running may be reset to the startup BFGMiner values when
BFGMiner shuts down because of this.

---

# RPC API

For RPC API details see the README.RPC file

---

# GPU DEVICE ISSUES and use of `--gpu-map`

GPUs mine with OpenCL software via the GPU device driver. This means you need
to have both an OpenCL SDK installed, and the GPU device driver RUNNING (i.e.
Xorg up and running configured for all devices that will mine on linux etc.)
Meanwhile, the hardware monitoring that BFGMiner offers for AMD devices relies
on the ATI Display Library (ADL) software to work. OpenCL DOES NOT TALK TO THE
ADL. There is no 100% reliable way to know that OpenCL devices are identical
to the ADL devices, as neither give off the same information. BFGMiner does its
best to correlate these devices based on the order that OpenCL and ADL numbers
them. It is possible that this will fail for the following reasons:

1. The device order is listed differently by OpenCL and ADL (rare), even if the
number of devices is the same.
2. There are more OpenCL devices than ADL. OpenCL stupidly sees one GPU as two
devices if you have two monitors connected to the one GPU.
3. There are more ADL devices than OpenCL. ADL devices include any ATI GPUs,
including ones that can't mine, like some older R4xxx cards.

To cope with this, the ADVANCED option for `--gpu-map` is provided with BFGMiner.
*DO NOT USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING*. The default will work the
vast majority of the time unless you know you have a problem already.

To get useful information, start BFGMiner with just the `-n` option. You will get
output that looks like this:

    [2012-04-25 13:17:34] CL Platform 0 vendor: Advanced Micro Devices, Inc.
    [2012-04-25 13:17:34] CL Platform 0 name: AMD Accelerated Parallel Processing
    [2012-04-25 13:17:34] CL Platform 0 version: OpenCL 1.1 AMD-APP (844.4)
    [2012-04-25 13:17:34] Platform 0 devices: 3
    [2012-04-25 13:17:34]   0       Tahiti
    [2012-04-25 13:17:34]   1       Tahiti
    [2012-04-25 13:17:34]   2       Cayman
    [2012-04-25 13:17:34] GPU 0 AMD Radeon HD 7900 Series  hardware monitoring enabled
    [2012-04-25 13:17:34] GPU 1 AMD Radeon HD 7900 Series  hardware monitoring enabled
    [2012-04-25 13:17:34] GPU 2 AMD Radeon HD 6900 Series hardware monitoring enabled
    [2012-04-25 13:17:34] 3 GPU devices max detected

Note the number of devices here match, and the order is the same. If devices 1
and 2 were different between Tahiti and Cayman, you could run BFGMiner with:

    --gpu-map 2:1,1:2

And it would swap the monitoring it received from ADL device 1 and put it to
OpenCL device 2 and vice versa.

If you have 2 monitors connected to the first device it would look like this:

    [2012-04-25 13:17:34] Platform 0 devices: 4
    [2012-04-25 13:17:34]   0       Tahiti
    [2012-04-25 13:17:34]   1       Tahiti
    [2012-04-25 13:17:34]   2       Tahiti
    [2012-04-25 13:17:34]   3       Cayman
    [2012-04-25 13:17:34] GPU 0 AMD Radeon HD 7900 Series  hardware monitoring enabled
    [2012-04-25 13:17:34] GPU 1 AMD Radeon HD 7900 Series  hardware monitoring enabled
    [2012-04-25 13:17:34] GPU 2 AMD Radeon HD 6900 Series hardware monitoring enabled

To work around this, you would use:

    -d 0 -d 2 -d 3 --gpu-map 2:1,3:2

If you have an older card as well as the rest it would look like this:

    [2012-04-25 13:17:34] Platform 0 devices: 3
    [2012-04-25 13:17:34]   0       Tahiti
    [2012-04-25 13:17:34]   1       Tahiti
    [2012-04-25 13:17:34]   2       Cayman
    [2012-04-25 13:17:34] GPU 0 AMD Radeon HD 4500 Series  hardware monitoring enabled
    [2012-04-25 13:17:34] GPU 1 AMD Radeon HD 7900 Series  hardware monitoring enabled
    [2012-04-25 13:17:34] GPU 2 AMD Radeon HD 7900 Series  hardware monitoring enabled
    [2012-04-25 13:17:34] GPU 3 AMD Radeon HD 6900 Series hardware monitoring enabled

To work around this you would use:

    --gpu-map 0:1,1:2,2:3


---

<a id="faq"></a>
# FAQ

**Q: Why can't BFGMiner find `lib<something>` even after I installed it from source
code?**

A: On UNIX-like operating systems, you often need to run some command to reload
its library caches such as `ldconfig` or similar. A couple of systems (such as
Fedora) ship with `/usr/local/lib` missing from their library search path. In
this case, you can usually add it like this:

    echo /usr/local/lib >/etc/ld.so.conf.d/local.conf

Please note that if your libraries installed into `lib64` instead of `lib`, you
should use that in the `ld.so` config file above instead.

**Q: BFGMiner segfaults when I change my shell window size.**
A: Older versions of libncurses have a bug to do with refreshing a window
after a size change. Upgrading to a new version of curses will fix it.

**Q: Can I mine on servers from different networks (eg smartcoin and bitcoin) at
the same time?**
A: No, BFGMiner keeps a database of the block it's working on to ensure it does
not work on stale blocks, and having different blocks from two networks would
make it invalidate the work from each other.

**Q: Can I change the intensity settings individually for each GPU?**
A: Yes, pass a list separated by commas such as `-I d,4,9,9`

*Q: Can I put multiple pools in the config file?*   *
A: Yes, check the `example.conf` file. Alternatively, set up everything either on
the command line or via the menu after startup and choose `settings`->`write config file` and the file will be loaded one each startup.

Q: The build fails with gcc is unable to build a binary.
A: Remove the "-march=native" component of your CFLAGS as your version of GCC
does not support it.

Q: The CPU usage is high.
A: The ATI drivers after 11.6 have a bug that makes them consume 100% of one
CPU core unnecessarily so downgrade to 11.6. Binding BFGMiner to one CPU core on
windows can minimise it to 100% (instead of more than one core). Driver version
11.11 on linux and 11.12 on windows appear to have fixed this issue. Note that
later drivers may have an apparent return of high CPU usage. Try
'export GPU_USE_SYNC_OBJECTS=1' on Linux before starting BFGMiner. You can also
set this variable in windows via a batch file or on the command line before
starting BFGMiner with 'setx GPU_USE_SYNC_OBJECTS 1'

Q: Can you implement feature X?
A: I can, but time is limited, and people who donate are more likely to get
their feature requests implemented.

Q: My GPU hangs and I have to reboot it to get it going again?
A: The more aggressively the mining software uses your GPU, the less overclock
you will be able to run. You are more likely to hit your limits with BFGMiner
and you will find you may need to overclock your GPU less aggressively. The
software cannot be responsible and make your GPU hang directly. If you simply
cannot get it to ever stop hanging, try decreasing the intensity, and if even
that fails, try changing to the poclbm kernel with -k poclbm, though you will
sacrifice performance. BFGMiner is designed to try and safely restart GPUs as
much as possible, but NOT if that restart might actually crash the rest of the
GPUs mining, or even the machine. It tries to restart them with a separate
thread and if that separate thread dies, it gives up trying to restart any more
GPUs.

Q: Work keeps going to my backup pool even though my primary pool hasn't
failed?
A: BFGMiner checks for conditions where the primary pool is lagging and will
pass some work to the backup servers under those conditions. The reason for
doing this is to try its absolute best to keep the GPUs working on something
useful and not risk idle periods. You can disable this behaviour with the
option --failover-only.

Q: Is this a virus?
A: BFGMiner is being packaged with other trojan scripts and some antivirus
software is falsely accusing bfgminer.exe as being the actual virus, rather
than whatever it is being packaged with. If you installed BFGMiner yourself,
then you do not have a virus on your computer. Complain to your antivirus
software company. They seem to be flagging even source code now from BFGMiner
as viruses, even though text source files can't do anything by themself.

Q: Can you modify the display to include more of one thing in the output and
less of another, or can you change the quiet mode or can you add yet another
output mode?
A: Everyone will always have their own view of what's important to monitor.
The defaults are very sane and I have very little interest in changing this
any further.

Q: Can you change the autofan/autogpu to change speeds in a different manner?
A: The defaults are sane and safe. I'm not interested in changing them further.
The starting fan speed is set to 50% in auto-fan mode as a safety precaution.

Q: Why is my efficiency above/below 1.00?
A: Efficiency simply means how many shares you return for the amount of
bandwidth used. It does not correlate with efficient use of your hardware, and
is a measure of a combination of hardware speed, block luck, pool design and
other factors.

Q: What are the best parameters to pass for X pool/hardware/device.
A: Virtually always, the DEFAULT parameters give the best results. Most user
defined settings lead to worse performance. The ONLY thing most users should
need to set is the Intensity.

Q: What happened to CPU mining?
A: Being increasingly irrelevant for most users, and a maintenance issue, it is
no longer under active development and will not be supported unless someone
steps up to help maintain it. No binary builds supporting CPU mining will be
released but CPU mining can be built into BFGMiner when it is compiled.

Q: I upgraded BFGMiner version and my hashrate suddenly dropped!
A: No, you upgraded your SDK version unwittingly between upgrades of BFGMiner
and that caused  your hashrate to drop. See the next question.

Q: I upgraded my ATI driver/SDK/BFGMiner and my hashrate suddenly dropped!
A: The hashrate performance in BFGMiner is tied to the version of the ATI SDK
that is installed only for the very first time BFGMiner is run. This generates
binaries that are used by the GPU every time after that. Any upgrades to the
SDK after that time will have no effect on the binaries. However, if you
install a fresh version of BFGMiner, and have since upgraded your SDK, new
binaries will be built. It is known that the 2.6 ATI SDK has a huge hashrate
penalty on generating new binaries. It is recommended to not use this SDK at
this time unless you are using an ATI 7xxx card that needs it.

Q: Which ATI SDK is the best for BFGMiner?
A: At the moment, versions 2.4 and 2.5 work the best. If you are forced to use
the 2.6 SDK, the phatk kernel will perform poorly, while the diablo or my
custom modified poclbm kernel are optimised for it.

Q: I have multiple SDKs installed, can I choose which one it uses?
A: Run bfgminer with the -n option and it will list all the platforms currently
installed. Then you can tell BFGMiner which platform to use with --gpu-platform.

Q: GUI version?
A: No. The RPC interface makes it possible for someone else to write one
though.

Q: I'm having an issue. What debugging information should I provide?
A: Start BFGMiner with your regular commands and add -D -T --verbose and provide
the full startup output and a summary of your hardware, operating system, ATI
driver version and ATI stream version.

Q: BFGMiner reports no devices or only one device on startup on Linux although
I have multiple devices and drivers+SDK installed properly?
A: Try "export DISPLAY=:0" before running BFGMiner.

Q: My network gets slower and slower and then dies for a minute?
A; Try the --net-delay option.

Q: How do I tune for P2Pool?
A: P2Pool has very rapid expiration of work and new blocks, it is suggested you
decrease intensity by 1 from your optimal value, and decrease GPU threads to 1
with -g 1. It is also recommended to use --failover-only since the work is
effectively like a different block chain. If mining with a Mini Rig, it is worth
adding the --bfl-range option.

Q: Are kernels from other mining software useable in BFGMiner?
A: No, the APIs are slightly different between the different software and they
will not work.

Q: I run PHP on windows to access the API with the example miner.php. Why does
it fail when php is installed properly but I only get errors about Sockets not
working in the logs?
A: http://us.php.net/manual/en/sockets.installation.php

Q: What is a PGA?
A: At the moment, BFGMiner supports 5 FPGAs: BitForce, Icarus, ModMiner, X6500,
and ZTEX.
They are Field-Programmable Gate Arrays that have been programmed to do Bitcoin
mining. Since the acronym needs to be only 3 characters, the "Field-" part has
been skipped.

Q: How do I get my BFL/Icarus/Lancelot/Cairnsmore device to auto-recognise?
A: On Linux, if the /dev/ttyUSB* devices don't automatically appear, the only
thing that needs to be done is to load the driver for them:
BFL: sudo modprobe ftdi_sio vendor=0x0403 product=0x6014
Icarus: sudo modprobe pl2303 vendor=0x067b product=0x230
Lancelot: sudo modprobe ftdi_sio vendor=0x0403 product=0x6001
Cairnsmore: sudo modprobe ftdi_sio product=0x8350 vendor=0x0403
On windows you must install the pl2303 or ftdi driver required for the device
pl2303: http://prolificusa.com/pl-2303hx-drivers/
ftdi: http://www.ftdichip.com/Drivers/VCP.htm

Q: On Linux I can see the /dev/ttyUSB* devices for my ICA/BFL/MMQ FPGA, but
BFGMiner can't mine on them
A: Make sure you have the required priviledges to access the /dev/ttyUSB*
devices:
 sudo ls -las /dev/ttyUSB*
will give output like:
 0 crw-rw---- 1 root dialout 188, 0 2012-09-11 13:49 /dev/ttyUSB0
This means your account must have the group 'dialout' or root priviledges
To permanently give your account the 'dialout' group:
 sudo usermod -G dialout -a `whoami`
Then logout and back in again

Q: What is stratum and how do I use it?
A: Stratum is a protocol designed to reduce resources for mining pools at the
cost of keeping the miner in the dark and blindly transferring his mining
authority to the pool. It is a return to the problems of the old centralized
"getwork" protocol, but capable of scaling to hardware of any speed like the
standard GBT protocol. If a pool uses stratum instead of GBT, BFGMiner will
automatically detect it and switch to the support as advertised if it can.
Stratum uses direct TCP connections to the pool and thus it will NOT currently
work through a http proxy but will work via a socks proxy if you need to use
one. If you input the stratum port directly into your configuration, or use the
special prefix "stratum+tcp://" instead of "http://", BFGMiner will ONLY try to
use stratum protocol mining.

Q: Why don't the statistics add up: Accepted, Rejected, Stale, Hardware Errors,
Diff1 Work, etc. when mining greater than 1 difficulty shares?
A: As an example, if you look at 'Difficulty Accepted' in the RPC API, the number
of difficulty shares accepted does not usually exactly equal the amount of work
done to find them. If you are mining at 8 difficulty, then you would expect on
average to find one 8 difficulty share, per 8 single difficulty shares found.
However, the number is actually random and converges over time, it is an average,
not an exact value, thus you may find more or less than the expected average.

---

This code is provided entirely free of charge by the programmer in his spare
time so donations would be greatly appreciated. Please consider donating to the
address below.

Luke-Jr <luke-jr+bfgminer@utopios.org>
1QATWksNFGeUJCWBrN4g6hGM178Lovm7Wh
