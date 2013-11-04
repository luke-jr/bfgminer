This is a multi-threaded multi-pool GPU, FPGA and ASIC miner with ATI GPU
monitoring, (over)clocking and fanspeed support for bitcoin and derivative
coins. Do not use on multiple block chains at the same time!

This code is provided entirely free of charge by the programmer in his spare
time so donations would be greatly appreciated. Please consider donating to the
address below.

Con Kolivas <kernel@kolivas.org>
15qSxP1SQcUX3o4nhkfdbgyoWEFMomJ4rZ

DOWNLOADS:

http://ck.kolivas.org/apps/cgminer

GIT TREE:

https://github.com/ckolivas/cgminer

Support thread:

http://bitcointalk.org/index.php?topic=28402.0

IRC Channel:

irc://irc.freenode.net/cgminer

License: GPLv3.  See COPYING for details.

SEE ALSO API-README, ASIC-README, FGPA-README, GPU-README AND SCRYPT-README FOR
MORE INFORMATION ON EACH.

---

EXECUTIVE SUMMARY ON USAGE:

After saving configuration from the menu, you do not need to give cgminer any
arguments and it will load your configuration.

Any configuration file may also contain a single
	"include" : "filename"
to recursively include another configuration file.
Writing the configuration will save all settings from all files in the output.


Single pool:

cgminer -o http://pool:port -u username -p password

Multiple pools:

cgminer -o http://pool1:port -u pool1username -p pool1password -o http://pool2:port -u pool2usernmae -p pool2password

Single pool with a standard http proxy, regular desktop:

cgminer -o "http:proxy:port|http://pool:port" -u username -p password

Single pool with a socks5 proxy, regular desktop:

cgminer -o "socks5:proxy:port|http://pool:port" -u username -p password

Single pool with stratum protocol support:

cgminer -o stratum+tcp://pool:port -u username -p password

The list of proxy types are:
 http:    standard http 1.1 proxy
 http0:   http 1.0 proxy
 socks4:  socks4 proxy
 socks5:  socks5 proxy
 socks4a: socks4a proxy
 socks5h: socks5 proxy using a hostname

If you compile cgminer with a version of CURL before 7.19.4 then some of the above will
not be available. All are available since CURL version 7.19.4

If you specify the --socks-proxy option to cgminer, it will only be applied to all pools
that don't specify their own proxy setting like above

---
BUILDING CGMINER FOR YOURSELF

DEPENDENCIES:
Mandatory:
	curl dev library 	http://curl.haxx.se/libcurl/
	(libcurl4-openssl-dev)

	pkg-config		http://www.freedesktop.org/wiki/Software/pkg-config
	libtool			http://www.gnu.org/software/libtool/
Optional:
	curses dev library
	(libncurses5-dev or libpdcurses on WIN32 for text user interface)

	AMD APP SDK		http://developer.amd.com/sdks/AMDAPPSDK
	(This sdk is mandatory for GPU mining)

	AMD ADL SDK		http://developer.amd.com/sdks/ADLSDK
	(This sdk is mandatory for ATI GPU monitoring & clocking)

	libudev dev library (libudev-dev)
	(This is only required for ASIC+FPGA support and is linux only)

If building from git:
	autoconf
	automake


CGMiner specific configuration options:
  --enable-opencl         Enable support for GPU mining with opencl
  --disable-adl           Override detection and disable building with adl
  --enable-scrypt         Compile support for scrypt litecoin mining (default
                          disabled)
  --enable-avalon         Compile support for Avalon (default disabled)
  --enable-bflsc          Compile support for BFL ASICs (default disabled)
  --enable-bitforce       Compile support for BitForce FPGAs (default
                          disabled)
  --enable-bitfury        Compile support for BitFury ASICs (default disabled)
  --enable-hashfast       Compile support for Hashfast (default disabled)
  --enable-icarus         Compile support for Icarus (default disabled)
  --enable-knc            Compile support for KnC miners (default disabled)
  --enable-klondike       Compile support for Klondike (default disabled)
  --enable-modminer       Compile support for ModMiner FPGAs(default disabled)
  --without-curses        Compile support for curses TUI (default enabled)
  --with-system-libusb    Compile against dynamic system libusb (default use
                          included static libusb)

Basic *nix build instructions:
	To actually build:

	./autogen.sh	# only needed if building from git repo
	CFLAGS="-O2 -Wall -march=native" ./configure <options>

	No installation is necessary. You may run cgminer from the build
	directory directly, but you may do make install if you wish to install
	cgminer to a system location or location you specified.

Native WIN32 build instructions: see windows-build.txt

---

Usage instructions:  Run "cgminer --help" to see options:

Usage: . [-atDdGCgIKklmpPQqrRsTouvwOchnV] 
Options for both config file and command line:
--api-allow         Allow API access (if enabled) only to the given list of [W:]IP[/Prefix] address[/subnets]
                    This overrides --api-network and you must specify 127.0.0.1 if it is required
                    W: in front of the IP address gives that address privileged access to all api commands
--api-description   Description placed in the API status header (default: cgminer version)
--api-groups        API one letter groups G:cmd:cmd[,P:cmd:*...]
                    See API-README for usage
--api-listen        Listen for API requests (default: disabled)
                    By default any command that does not just display data returns access denied
                    See --api-allow to overcome this
--api-network       Allow API (if enabled) to listen on/for any address (default: only 127.0.0.1)
--api-mcast         Enable API Multicast listener, (default: disabled)
                    The listener will only run if the API is also enabled
--api-mcast-addr <arg> API Multicast listen address, (default: 224.0.0.75)
--api-mcast-code <arg> Code expected in the API Multicast message, don't use '-' (default: "FTW")
--api-mcast-port <arg> API Multicast listen port, (default: 4028)
--api-port          Port number of miner API (default: 4028)
--auto-fan          Automatically adjust all GPU fan speeds to maintain a target temperature
--auto-gpu          Automatically adjust all GPU engine clock speeds to maintain a target temperature
--balance           Change multipool strategy from failover to even share balance
--benchmark         Run cgminer in benchmark mode - produces no shares
--compact           Use compact display without per device statistics
--debug|-D          Enable debug output
--device|-d <arg>   Select device to use, one value, range and/or comma separated (e.g. 0-2,4) default: all
--disable-rejecting Automatically disable pools that continually reject shares
--expiry|-E <arg>   Upper bound on how many seconds after getting work we consider a share from it stale (default: 120)
--failover-only     Don't leak work to backup pools when primary pool is lagging
--fix-protocol      Do not redirect to a different getwork protocol (eg. stratum)
--hotplug <arg>     Set hotplug check time to <arg> seconds (0=never default: 5) - only with libusb
--kernel-path|-K <arg> Specify a path to where bitstream and kernel files are (default: "/usr/local/bin")
--load-balance      Change multipool strategy from failover to quota based balance
--log|-l <arg>      Interval in seconds between log output (default: 5)
--lowmem            Minimise caching of shares for low memory applications
--monitor|-m <arg>  Use custom pipe cmd for output messages
--net-delay         Impose small delays in networking to not overload slow routers
--no-submit-stale   Don't submit shares if they are detected as stale
--pass|-p <arg>     Password for bitcoin JSON-RPC server
--per-device-stats  Force verbose mode and output per-device statistics
--protocol-dump|-P  Verbose dump of protocol-level activities
--queue|-Q <arg>    Minimum number of work items to have queued (0 - 10) (default: 1)
--quiet|-q          Disable logging output, display status and errors
--real-quiet        Disable all output
--remove-disabled   Remove disabled devices entirely, as if they didn't exist
--rotate <arg>      Change multipool strategy from failover to regularly rotate at N minutes (default: 0)
--round-robin       Change multipool strategy from failover to round robin on failure
--scan-time|-s <arg> Upper bound on time spent scanning current work, in seconds (default: 60)
--sched-start <arg> Set a time of day in HH:MM to start mining (a once off without a stop time)
--sched-stop <arg>  Set a time of day in HH:MM to stop mining (will quit without a start time)
--scrypt            Use the scrypt algorithm for mining (litecoin only)
--sharelog <arg>    Append share log to file
--shares <arg>      Quit after mining N shares (default: unlimited)
--socks-proxy <arg> Set socks4 proxy (host:port) for all pools without a proxy specified
--syslog            Use system log for output messages (default: standard error)
--temp-cutoff <arg> Temperature where a device will be automatically disabled, one value or comma separated list (default: 95)
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


USB device (ASIC and FPGA) options:

--icarus-options <arg> Set specific FPGA board configurations - one set of values for all or comma separated
--icarus-timing <arg> Set how the Icarus timing is calculated - one setting/value for all or comma separated
--usb <arg>         USB device selection (See below)
--usb-dump          (See FPGA-README)

See FGPA-README or ASIC-README for more information regarding these.


ASIC only options:

--avalon-auto       Adjust avalon overclock frequency dynamically for best hashrate
--avalon-fan <arg> Set fanspeed percentage for avalon, single value or range (default: 20-100)
--avalon-freq <arg> Set frequency range for avalon-auto, single value or range
--avalon-cutoff <arg> Set avalon overheat cut off temperature (default: 60)
--avalon-options <arg> Set avalon options baud:miners:asic:timeout:freq
--avalon-temp <arg> Set avalon target temperature (default: 50)
--bflsc-overheat <arg> Set overheat temperature where BFLSC devices throttle, 0 to disable (default: 90)
--bitburner-fury-options <arg> Override avalon-options for BitBurner Fury boards baud:miners:asic:timeout:freq
--bitburner-fury-voltage <arg> Set BitBurner Fury core voltage, in millivolts
--bitburner-voltage <arg> Set BitBurner (Avalon) core voltage, in millivolts
--klondike-options <arg> Set klondike options clock:temptarget

See ASIC-README for more information regarding these.


FPGA only options:

--bfl-range         Use nonce range on bitforce devices if supported

See FGPA-README for more information regarding this.


GPU only options:

--auto-fan          Automatically adjust all GPU fan speeds to maintain a target temperature
--auto-gpu          Automatically adjust all GPU engine clock speeds to maintain a target temperature
--disable-gpu|-G    Disable GPU mining even if suitable devices exist
--gpu-threads|-g <arg> Number of threads per GPU (1 - 10) (default: 2)
--gpu-dyninterval <arg> Set the refresh interval in ms for GPUs using dynamic intensity (default: 7)
--gpu-engine <arg>  GPU engine (over)clock range in Mhz - one value, range and/or comma separated list (e.g. 850-900,900,750-850)
--gpu-fan <arg>     GPU fan percentage range - one value, range and/or comma separated list (e.g. 25-85,85,65)
--gpu-map <arg>     Map OpenCL to ADL device order manually, paired CSV (e.g. 1:0,2:1 maps OpenCL 1 to ADL 0, 2 to 1)
--gpu-memclock <arg> Set the GPU memory (over)clock in Mhz - one value for all or separate by commas for per card.
--gpu-memdiff <arg> Set a fixed difference in clock speed between the GPU and memory in auto-gpu mode
--gpu-powertune <arg> Set the GPU powertune percentage - one value for all or separate by commas for per card.
--gpu-reorder       Attempt to reorder GPU devices according to PCI Bus ID
--gpu-vddc <arg>    Set the GPU voltage in Volts - one value for all or separate by commas for per card.
--intensity|-I <arg> Intensity of GPU scanning (d or -10 -> 10, default: d to maintain desktop interactivity)
--kernel|-k <arg>   Override kernel to use (diablo, poclbm, phatk or diakgcn) - one value or comma separated
--ndevs|-n          Enumerate number of detected GPUs and exit
--no-restart        Do not attempt to restart GPUs that hang
--temp-hysteresis <arg> Set how much the temperature can fluctuate outside limits when automanaging speeds (default: 3)
--temp-overheat <arg> Overheat temperature when automatically managing fan and GPU speeds (default: 85)
--temp-target <arg> Target temperature when automatically managing fan and GPU speeds (default: 75)
--vectors|-v <arg>  Override detected optimal vector (1, 2 or 4) - one value or comma separated list
--worksize|-w <arg> Override detected optimal worksize - one value or comma separated list

See GPU-README for more information regarding GPU mining.


SCRYPT only options:

--lookup-gap <arg>  Set GPU lookup gap for scrypt mining, comma separated
--shaders <arg>     GPU shaders per card for tuning scrypt, comma separated
--thread-concurrency <arg> Set GPU thread concurrency for scrypt mining, comma separated

See SCRYPT-README for more information regarding litecoin mining.


Cgminer should automatically find all of your Avalon ASIC, BFL ASIC, BitForce
FPGAs, Icarus bitstream FPGAs, Klondike ASIC, ASICMINER usb block erupters,
KnC ASICs, Hashfast ASICs and ModMiner FPGAs.

---

SETTING UP USB DEVICES

WINDOWS:

On windows, the direct USB support requires the installation of a WinUSB
driver (NOT the ftdi_sio driver), and attach it to your devices.
The easiest way to do this is to use the zadig utility which will install the
drivers for you and then once you plug in your device you can choose the
"list all devices" from the "option" menu and you should be able to see the
device as something like: "BitFORCE SHA256 SC". Choose the install or replace
driver option and select WinUSB. You can either google for zadig or download
it from the cgminer directoy in the DOWNLOADS link above.

LINUX:

On linux, the direct USB support requires no drivers at all. However due to
permissions issues, you may not be able to mine directly on the devices as a
regular user without giving the user access to the device or by mining as
root (administrator). In order to give your regular user access, you can make
him a member of the plugdev group with the following commands:

 sudo usermod -G plugdev -a `whoami`

If your distribution does not have the plugdev group you can create it with:

 sudo groupadd plugdev

In order for the BFL devices to instantly be owned by the plugdev group and
accessible by anyone from the plugdev group you can copy the file
"01-cgminer.rules" from the cgminer archive into the /etc/udev/rules.d
directory with the following command:

 sudo cp 01-cgminer.rules /etc/udev/rules.d/

After this you can either manually restart udev and re-login, or more easily
just reboot.

Advanced USB options:

The --usb option can restrict how many Avalon, BFL ASIC, BitForce FPGAs,
Klondike ASIC, ModMiner FPGAs or Icarus bitstream FPGAs it finds:

  --usb 1:2,1:3,1:4,1:*
or
  --usb BAS:1,BFL:1,MMQ:0,ICA:0,KLN:0
or
  --usb :10

You can only use one of the above 3

The first version
  --usb 1:2,1:3,1:4,1:*
allows you to select which devices to mine on with a list of USB
 bus_number:device_address
All other USB devices will be ignored
Hotplug will also only look at the devices matching the list specified and
find nothing new if they are all in use
You can specify just the USB bus_number to find all devices like 1:*
which means any devices on USB bus_number 1
This is useful if you unplug a device then plug it back in the same port,
it usually reappears with the same bus_number but a different device_address

You can see the list of all USB devices on linux with 'sudo lsusb'
Cgminer will list the recognised USB devices with the '-n' option or the
'--usb-dump 0' option
The '--usb-dump N' option with a value of N greater than 0 will dump a lot
of details about each recognised USB device
If you wish to see all USB devices, include the --usb-list-all option

The second version
  --usb BAS:1,BFL:1,MMQ:0,ICA:0,KLN:0
allows you to specify how many devices to choose based on each device
driver cgminer has - there are currently 5 USB drivers: BAS, BFL, MMQ.
ICA & KLN
N.B. you can only specify which device driver to limit, not the type of
each device, e.g. with BAS:n you can limit how many BFL ASIC devices will
be checked, but you cannot limit the number of each type of BFL ASIC
Also note that the MMQ count is the number of MMQ backplanes you have
not the number of MMQ FPGAs

The third version
  --usb :10
means only use a maximum of 10 devices of any supported USB devices
Once cgminer has 10 devices it will not configure any more and hotplug will
not scan for any more
If one of the 10 devices stops working, hotplug - if enabled, as is default
- will scan normally again until it has 10 devices

  --usb :0 will disable all USB I/O other than to initialise libusb

NOTE: The --device option will limit which devices are in use based on their
numbering order of the total devices, so if you hotplug USB devices regularly,
it will not reliably be the same devices.

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

[Q]ueue: 1
[S]cantime: 60
[E]xpiry: 120
[W]rite config file
[C]gminer restart


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

GPU 0: [124.2 / 191.3 Mh/s] [A:77  R:33  HW:0  U:1.73/m  WU 1.73/m]
Temp: 67.0 C
Fan Speed: 35% (2500 RPM)
Engine Clock: 960 MHz
Memory Clock: 480 Mhz
Vddc: 1.200 V
Activity: 93%
Powertune: 0%
Last initialised: [2011-09-06 12:03:56]
Thread 0: 62.4 Mh/s Enabled ALIVE
Thread 1: 60.2 Mh/s Enabled ALIVE

[E]nable [D]isable [R]estart GPU [C]hange settings
Or press any other key to continue


The running log shows output like this:

 [2012-10-12 18:02:20] Accepted f0c05469 Diff 1/1 GPU 0 pool 1
 [2012-10-12 18:02:22] Accepted 218ac982 Diff 7/1 GPU 1 pool 1
 [2012-10-12 18:02:23] Accepted d8300795 Diff 1/1 GPU 3 pool 1
 [2012-10-12 18:02:24] Accepted 122c1ff1 Diff 14/1 GPU 1 pool 1

The 8 byte hex value are the 2nd 8 bytes of the share being submitted to the
pool. The 2 diff values are the actual difficulty target that share reached
followed by the difficulty target the pool is currently asking for.

---
Also many issues and FAQs are covered in the forum thread
dedicated to this program,
	http://forum.bitcoin.org/index.php?topic=28402.0

The output line shows the following:
(5s):1713.6 (avg):1707.8 Mh/s | A:729  R:8  HW:0  WU:22.53/m

Each column is as follows:
5s:  A 5 second exponentially decaying average hash rate
avg: An all time average hash rate
A:  The total difficulty of Accepted shares
R:  The total difficulty of Rejected shares
HW:  The number of HardWare errors
WU:  The Work Utility defined as the number of diff1 shares work / minute
     (accepted or rejected).

 GPU 1: 73.5C 2551RPM | 427.3/443.0Mh/s | A:8 R:0 HW:0 WU:4.39/m

Each column is as follows:
Temperature (if supported)
Fanspeed (if supported)
A 5 second exponentially decaying average hash rate
An all time average hash rate
The total difficulty of accepted shares
The total difficulty of rejected shares
The number of hardware erorrs
The work utility defined as the number of diff1 shares work / minute

The cgminer status line shows:
 ST: 1  SS: 0  NB: 1  LW: 8  GF: 1  RF: 1

ST is STaged work items (ready to use).
SS is Stale Shares discarded (detected and not submitted so don't count as rejects)
NB is New Blocks detected on the network
LW is Locally generated Work items
GF is Getwork Fail Occasions (server slow to provide work)
RF is Remote Fail occasions (server slow to accept work)

The block display shows:
Block: 0074c5e482e34a506d2a051a...  Started: [17:17:22]  Best share: 2.71K

This shows a short stretch of the current block, when the new block started,
and the all time best difficulty share you've found since starting cgminer
this time.


---
MULTIPOOL

FAILOVER STRATEGIES WITH MULTIPOOL:
A number of different strategies for dealing with multipool setups are
available. Each has their advantages and disadvantages so multiple strategies
are available by user choice, as per the following list:

FAILOVER:
The default strategy is failover. This means that if you input a number of
pools, it will try to use them as a priority list, moving away from the 1st
to the 2nd, 2nd to 3rd and so on. If any of the earlier pools recover, it will
move back to the higher priority ones.

ROUND ROBIN:
This strategy only moves from one pool to the next when the current one falls
idle and makes no attempt to move otherwise.

ROTATE:
This strategy moves at user-defined intervals from one active pool to the next,
skipping pools that are idle.

LOAD BALANCE:
This strategy sends work to all the pools on a quota basis. By default, all
pools are allocated equal quotas unless specified with --quota. This
apportioning of work is based on work handed out, not shares returned so is
independent of difficulty targets or rejected shares. While a pool is disabled
or dead, its quota is dropped until it is re-enabled. Quotas are forward
looking, so if the quota is changed on the fly, it only affects future work.
If all pools are set to zero quota or all pools with quota are dead, it will
fall back to a failover mode. See quota below for more information.

The failover-only flag has special meaning in combination with load-balance
mode and it will distribute quota back to priority pool 0 from any pools that
are unable to provide work for any reason so as to maintain quota ratios
between the rest of the pools.

BALANCE:
This strategy monitors the amount of difficulty 1 shares solved for each pool
and uses it to try to end up doing the same amount of work for all pools.


---
QUOTAS

The load-balance multipool strategy works off a quota based scheduler. The
quotas handed out by default are equal, but the user is allowed to specify any
arbitrary ratio of quotas. For example, if all the quota values add up to 100,
each quota value will be a percentage, but if 2 pools are specified and pool0
is given a quota of 1 and pool1 is given a quota of 9, pool0 will get 10% of
the work and pool1 will get 90%. Quotas can be changed on the fly by the API,
and do not act retrospectively. Setting a quota to zero will effectively
disable that pool unless all other pools are disabled or dead. In that
scenario, load-balance falls back to regular failover priority-based strategy.
While a pool is dead, it loses its quota and no attempt is made to catch up
when it comes back to life.

To specify quotas on the command line, pools should be specified with a
semicolon separated --quota(or -U) entry instead of --url. Pools specified with
--url are given a nominal quota value of 1 and entries can be mixed.

For example:
--url poola:porta -u usernamea -p passa --quota "2;poolb:portb" -u usernameb -p passb
Will give poola 1/3 of the work and poolb 2/3 of the work.

Writing configuration files with quotas is likewise supported. To use the above
quotas in a configuration file they would be specified thus:

"pools" : [
        {
                "url" : "poola:porta",
                "user" : "usernamea",
                "pass" : "passa"
        },
        {
                "quota" : "2;poolb:portb",
                "user" : "usernameb",
                "pass" : "passb"
        }
]


---
LOGGING

cgminer will log to stderr if it detects stderr is being redirected to a file.
To enable logging simply add 2>logfile.txt to your command line and logfile.txt
will contain the logged output at the log level you specify (normal, verbose,
debug etc.)

In other words if you would normally use:
./cgminer -o xxx -u yyy -p zzz
if you use
./cgminer -o xxx -u yyy -p zzz 2>logfile.txt
it will log to a file called logfile.txt and otherwise work the same.

There is also the -m option on linux which will spawn a command of your choice
and pipe the output directly to that command.

The WorkTime details 'debug' option adds details on the end of each line
displayed for Accepted or Rejected work done. An example would be:

 <-00000059.ed4834a3 M:X D:1.0 G:17:02:38:0.405 C:1.855 (2.995) W:3.440 (0.000) S:0.461 R:17:02:47

The first 2 hex codes are the previous block hash, the rest are reported in
seconds unless stated otherwise:
The previous hash is followed by the getwork mode used M:X where X is one of
P:Pool, T:Test Pool, L:LP or B:Benchmark,
then D:d.ddd is the difficulty required to get a share from the work,
then G:hh:mm:ss:n.nnn, which is when the getwork or LP was sent to the pool and
the n.nnn is how long it took to reply,
followed by 'O' on it's own if it is an original getwork, or 'C:n.nnn' if it was
a clone with n.nnn stating how long after the work was recieved that it was cloned,
(m.mmm) is how long from when the original work was received until work started,
W:n.nnn is how long the work took to process until it was ready to submit,
(m.mmm) is how long from ready to submit to actually doing the submit, this is
usually 0.000 unless there was a problem with submitting the work,
S:n.nnn is how long it took to submit the completed work and await the reply,
R:hh:mm:ss is the actual time the work submit reply was received

If you start cgminer with the --sharelog option, you can get detailed
information for each share found. The argument to the option may be "-" for
standard output (not advisable with the ncurses UI), any valid positive number
for that file descriptor, or a filename.

To log share data to a file named "share.log", you can use either:
./cgminer --sharelog 50 -o xxx -u yyy -p zzz 50>share.log
./cgminer --sharelog share.log -o xxx -u yyy -p zzz

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

RPC API

For RPC API details see the API-README file

---

FAQ

Q: Can I mine on servers from different networks (eg smartcoin and bitcoin) at
the same time?
A: No, cgminer keeps a database of the block it's working on to ensure it does
not work on stale blocks, and having different blocks from two networks would
make it invalidate the work from each other.

Q: Can I configure cgminer to mine with different login credentials or pools
for each separate device?
A: No.

Q: Can I put multiple pools in the config file?
A: Yes, check the example.conf file. Alternatively, set up everything either on
the command line or via the menu after startup and choose settings->write
config file and the file will be loaded one each startup.

Q: The build fails with gcc is unable to build a binary.
A: Remove the "-march=native" component of your CFLAGS as your version of gcc
does not support it.

Q: Can you implement feature X?
A: I can, but time is limited, and people who donate are more likely to get
their feature requests implemented.

Q: Work keeps going to my backup pool even though my primary pool hasn't
failed?
A: Cgminer checks for conditions where the primary pool is lagging and will
pass some work to the backup servers under those conditions. The reason for
doing this is to try its absolute best to keep the GPUs working on something
useful and not risk idle periods. You can disable this behaviour with the
option --failover-only.

Q: Is this a virus?
A: Cgminer is being packaged with other trojan scripts and some antivirus
software is falsely accusing cgminer.exe as being the actual virus, rather
than whatever it is being packaged with. If you installed cgminer yourself,
then you do not have a virus on your computer. Complain to your antivirus
software company. They seem to be flagging even source code now from cgminer
as viruses, even though text source files can't do anything by themself.

Q: Can you modify the display to include more of one thing in the output and
less of another, or can you change the quiet mode or can you add yet another
output mode?
A: Everyone will always have their own view of what's important to monitor.
The defaults are very sane and I have very little interest in changing this
any further.

Q: What are the best parameters to pass for X pool/hardware/device.
A: Virtually always, the DEFAULT parameters give the best results. Most user
defined settings lead to worse performance. The ONLY thing most users should
need to set is the Intensity for GPUs.

Q: What happened to CPU mining?
A: Being increasingly irrelevant for most users, and a maintenance issue, it is
no longer under active development and will not be supported. No binary builds
supporting CPU mining will be released. Virtually all remaining users of CPU
mining are as back ends for illegal botnets. The main reason cgminer is being
inappopriately tagged as a virus by antivirus software is due to the trojans
packaging a CPU mining capable version of it. There is no longer ANY CPU mining
code in cgminer. If you are mining bitcoin with CPU today, you are spending
1000x more in electricity costs than you are earning in bitcoin.

Q: GUI version?
A: No. The RPC interface makes it possible for someone else to write one
though.

Q: I'm having an issue. What debugging information should I provide?
A: Start cgminer with your regular commands and add -D -T --verbose and provide
the full startup output and a summary of your hardware, operating system, ATI
driver version and ATI stream version.

Q: Why don't you provide win64 builds?
A: Win32 builds work everywhere and there is precisely zero advantage to a
64 bit build on windows.

Q: Is it faster to mine on windows or linux?
A: It makes no difference. It comes down to choice of operating system for
their various features. Linux offers much better long term stability and
remote monitoring and security, while windows offers you overclocking tools
that can achieve much more than cgminer can do on linux.

Q: Can I mine with cgminer on a MAC?
A: cgminer will compile on OSX, but the performance of GPU mining is
compromised due to the opencl implementation on OSX, there is no temperature
or fanspeed monitoring, and the cooling design of most MACs, despite having
powerful GPUs, will usually not cope with constant usage leading to a high
risk of thermal damage. It is highly recommended not to mine on a MAC unless
it is to a USB device.

Q: I'm trying to mine litecoin but cgminer shows MH values instead of kH and
submits no shares?
A: Add the --scrypt parameter.

Q: I switch users on windows and my mining stops working?
A: That's correct, it does. It's a permissions issue that there is no known
fix for due to monitoring of GPU fanspeeds and temperatures. If you disable
the monitoring with --no-adl it should switch okay.

Q: My network gets slower and slower and then dies for a minute?
A; Try the --net-delay option.

Q: How do I tune for p2pool?
A: p2pool has very rapid expiration of work and new blocks, it is suggested you
decrease intensity by 1 from your optimal value, and decrease GPU threads to 1
with -g 1. It is also recommended to use --failover-only since the work is
effectively like a different block chain. If mining with a minirig, it is worth
adding the --bfl-range option.

Q: Are OpenCL kernels from other mining software useable in cgminer?
A: No, the APIs are slightly different between the different software and they
will not work.

Q: I run PHP on windows to access the API with the example miner.php. Why does
it fail when php is installed properly but I only get errors about Sockets not
working in the logs?
A: http://us.php.net/manual/en/sockets.installation.php

Q: What is a PGA?
A: At the moment, cgminer supports 3 FPGAs: BitForce, Icarus and ModMiner.
They are Field-Programmable Gate Arrays that have been programmed to do Bitcoin
mining. Since the acronym needs to be only 3 characters, the "Field-" part has
been skipped.

Q: What is an ASIC?
A: Cgminer currently supports 2 ASICs: Avalon and BitForce SC devices. They
are Application Specify Integrated Circuit devices and provide the highest
performance per unit power due to being dedicated to only one purpose.

Q: Can I mine scrypt with FPGAs or ASICs?
A: No.

Q: What is stratum and how do I use it?
A: Stratum is a protocol designed for pooled mining in such a way as to
minimise the amount of network communications, yet scale to hardware of any
speed. With versions of cgminer 2.8.0+, if a pool has stratum support, cgminer
will automatically detect it and switch to the support as advertised if it can.
If you input the stratum port directly into your configuration, or use the
special prefix "stratum+tcp://" instead of "http://", cgminer will ONLY try to
use stratum protocol mining. The advantages of stratum to the miner are no
delays in getting more work for the miner, less rejects across block changes,
and far less network communications for the same amount of mining hashrate. If
you do NOT wish cgminer to automatically switch to stratum protocol even if it
is detected, add the --fix-protocol option.

Q: Why don't the statistics add up: Accepted, Rejected, Stale, Hardware Errors,
Diff1 Work, etc. when mining greater than 1 difficulty shares?
A: As an example, if you look at 'Difficulty Accepted' in the RPC API, the number
of difficulty shares accepted does not usually exactly equal the amount of work
done to find them. If you are mining at 8 difficulty, then you would expect on
average to find one 8 difficulty share, per 8 single difficulty shares found.
However, the number is actually random and converges over time, it is an average,
not an exact value, thus you may find more or less than the expected average.

Q: Why do the scrypt diffs not match with the current difficulty target?
A: The current scrypt block difficulty is expressed in terms of how many
multiples of the BTC difficulty it currently is (eg 28) whereas the shares of
"difficulty 1" are actually 65536 times smaller than the BTC ones. The diff
expressed by cgminer is as multiples of difficulty 1 shares.

Q: Can I make a donation in litecoin?
A: Yes, see SCRYPT-README for the address, but the author prefers bitcoin if
possible.

Q: My keyboard input momentarily pauses or repeats keys every so often on
windows while mining?
A: The USB implementation on windows can be very flaky on some hardware and
every time cgminer looks for new hardware to hotplug it it can cause these
sorts of problems. You can disable hotplug with:
--hotplug 0

Q: What should my Work Utility (WU) be?
A: Work utility is the product of hashrate * luck and only stabilises over a
very long period of time. Assuming all your work is valid work, bitcoin mining
should produce a work utility of approximately 1 per 71.6MH. This means at
5GH you should have a WU of 5000 / 71.6 or ~ 69. You cannot make your machine
do "better WU" than this - it is luck related. However you can make it much
worse if your machine produces a lot of hardware errors producing invalid work.


---

This code is provided entirely free of charge by the programmer in his spare
time so donations would be greatly appreciated. Please consider donating to the
address below.

Con Kolivas <kernel@kolivas.org>
15qSxP1SQcUX3o4nhkfdbgyoWEFMomJ4rZ
