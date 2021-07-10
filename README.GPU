EXECUTIVE SUMMARY ON GPU USAGE (SEE ALSO README.scrypt FOR SCRYPT MINING):

By default, BFGMiner will NOT mine on any GPUs unless in scrypt mode. If you
wish to use your GPU to mine SHA256d (generally not a good idea), you can
explicitly enable it with the -S opencl:auto option.

Single pool, regular desktop:

bfgminer -S opencl:auto -o http://pool:port -u username -p password

If you have configured your system properly, BFGMiner will mine on all GPUs in
"dynamic" mode which is designed to keep your system usable and sacrifice some
mining performance.

Single pool, dedicated miner:

bfgminer -S opencl:auto -o http://pool:port -u username -p password --set-device intensity=9

Single pool, first card regular desktop, 3 other dedicated cards:

bfgminer -S opencl:auto -o http://pool:port -u username -p password --set-device intensity=9 --set-device OCL0:intensity=d

Multiple pool, dedicated miner:

bfgminer -S opencl:auto -o http://pool1:port -u pool1username -p pool1password -o http://pool2:port -u pool2usernmae -p pool2password --set-device intensity=9

Add overclocking settings, GPU and fan control for all cards:

bfgminer -S opencl:auto -o http://pool:port -u username -p password --set-device intensity=9 --auto-fan --auto-gpu --set-device OCL:clock=750-950 --set-device OCL:memclock=300

Add overclocking settings, GPU and fan control with different engine settings for 4 cards:

bfgminer -S opencl:auto -o http://pool:port -u username -p password --set-device intensity=9 --auto-fan --auto-gpu --set-device OCL0:clock=750-950 --set-device OCL1:clock=945 --set-device OCL2:clock=700-930 --set-device OCL3:clock=960 --set-device OCL:memclock=300

READ WARNINGS AND DOCUMENTATION BELOW ABOUT OVERCLOCKING

To configure multiple displays on linux you need to configure your Xorg cleanly
to use them all:

sudo aticonfig --adapter=all -f --initial

On Linux you virtually always need to export your display settings before
starting to get all the cards recognised and/or temperature+clocking working:

export DISPLAY=:0

---
SETUP FOR GPU SUPPORT:

To setup GPU mining support:

Install the AMD APP sdk, ideal version (see FAQ!) - put it into a system
location.
Download the correct version for either 32 bit or 64 bit from here:
	http://developer.amd.com/tools/heterogeneous-computing/amd-accelerated-parallel-processing-app-sdk/downloads/

The best version for Radeon 5xxx and 6xxx is v2.5, while 7xxx cards need v2.6 or
later, 2.7 seems the best.

For versions 2.4 or earlier you will need to manually install them:
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


---
INTENSITY INFORMATION:

Intensity correlates with the size of work being submitted at any one time to
a GPU. The higher the number the larger the size of work. Generally speaking
finding an optimal value rather than the highest value is the correct approach
as hash rate rises up to a point with higher intensities but above that, the
device may be very slow to return responses, or produce errors.

NOTE: Running intensities above 9 with current hardware is likely to only
diminish return performance even if the hash rate might appear better. A good
starting baseline intensity to try on dedicated miners is 9. 11 is the upper
limit for intensity while Bitcoin mining, if the GPU_USE_SYNC_OBJECTS variable
is set (see FAQ). The upper limit for SHA256d mining is 14 and 20 for scrypt.


---
OVERCLOCKING WARNING AND INFORMATION

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
--set-device OCL:clock=950 --set-device OCL:memclock=825

will try to set all GPU engine clocks to 950 and all memory clocks to 825,
while:
--set-device OCL0:clock=950 --set-device OCL1:clock=945 --set-device OCL2:clock=930 --set-device OCL3:clock=960 --set-device OCL:memclock=300

will try to set the engine clock of card 0 to 950, 1 to 945, 2 to 930, 3 to
960 and all memory clocks to 300.

AUTO MODES:
There are two "auto" modes in BFGMiner, --auto-fan and --auto-gpu. These can be
used independently of each other and are complementary. Both auto modes are
designed to safely change settings while trying to maintain a target
temperature. By default this is set to 75 degrees C but can be changed with the
--set-device option. For example:

--set-device OCL:temp-target=80
Sets all cards' target temperature to 80 degrees.

--set-device OCL0:temp-target=75 --set-device OCL1:temp-target=85
Sets card 0 target temperature to 75, and card 1 to 85 degrees.

AUTO FAN:
e.g.
--auto-fan (implies 85% upper limit)
--set-device OCL0:fan=25-85 --set-device OCL1:fan=65 --auto-fan

Fan control in auto fan works off the theory that the minimum possible fan
required to maintain an optimal temperature will use less power, make less
noise, and prolong the life of the fan. In auto-fan mode, the fan speed is
limited to 85% if the temperature is below "overheat" intentionally, as higher
fanspeeds on GPUs do not produce signficantly more cooling, yet significantly
shorten the lifespan of the fans. If temperature reaches the overheat value,
fanspeed will still be increased to 100%. The overheat value is set to 85
degrees by default and can be changed with the temp_overheat setting:

e.g.
--set-device OCL0:temp_overheat=75 --set-device OCL1:temp_overheat=85
Sets card 0 overheat threshold to 75 degrees and card 1 to 85.

AUTO GPU:
e.g.
--auto-gpu --set-device OCL:clock=750-950
--auto-gpu --set-device OCL0:clock=750-950 --set-device OCL1:clock=945 --set-device OCL2:clock=700-930 --set-device OCL3:clock=960

GPU control in auto gpu tries to maintain as high a clock speed as possible
while not reaching overheat temperatures. As a lower clock speed limit, the
auto-gpu mode checks the GPU card's "normal" clock speed and will not go below
this unless you have manually set a lower speed in the range. Also, unless a
higher clock speed was specified at startup, it will not raise the clockspeed.
If the temperature climbs, fanspeed is adjusted and optimised before GPU engine
clockspeed is adjusted. If fan speed control is not available or already
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

--set-device OCL0:temp-cutoff=95 --set-device OCL1:temp-cutoff=105
Sets card 0 cutoff temperature to 95 and card 1 to 105.

--set-device OCL:memdiff=-125
This setting will modify the memory speed whenever the GPU clock speed is
modified by --auto-gpu. In this example, it will set the memory speed to be 125
MHz lower than the GPU speed. This is useful for some cards like the 6970 which
normally don't allow a bigger clock speed difference. The 6970 is known to only
allow -125, while the 7970 only allows -150.


CHANGING SETTINGS:
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

STARTUP / SHUTDOWN:
When BFGMiner starts up, it tries to read off the current profile information
for clock and fan speeds and stores these values. When quitting BFGMiner, it
will then try to restore the original values. Changing settings outside of
BFGMiner while it's running may be reset to the startup BFGMiner values when
BFGMiner shuts down because of this.

---

GPU DEVICE ISSUES and use of --gpu-map

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

To cope with this, the ADVANCED option for --gpu-map is provided with BFGMiner.
DO NOT USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING. The default will work the
vast majority of the time unless you know you have a problem already.

To get useful information, start BFGMiner with just the -n option. You will get
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
GPU FAQ:

Q: Can I change the intensity settings individually for each GPU?
A: Yes, specify the devices by identifier: --set-device intensity=9 --set-device
OCL0:intensity=d --set-device OCL1:intensity=4 (be sure you set the catch-all
first!)

Q: The CPU usage is high.
A: The ATI drivers after 11.6 have a bug that makes them consume 100% of one
CPU core unnecessarily, so downgrade to 11.6. Binding BFGMiner to one CPU core
on windows can minimise it to 100% (instead of more than one core). Driver
version 11.11 on linux and 11.12 on windows appear to have fixed this issue.
Note that later drivers may have an apparent return of high CPU usage. Try
'export GPU_USE_SYNC_OBJECTS=1' on Linux before starting BFGMiner. You can also
set this variable in windows via a batch file or on the command line before
starting BFGMiner with 'setx GPU_USE_SYNC_OBJECTS 1'

Q: My GPU hangs and I have to reboot it to get it going again?
A: The more aggressively the mining software uses your GPU, the less overclock
you will be able to run. You are more likely to hit your limits with BFGMiner
and you will find you may need to overclock your GPU less aggressively. The
software cannot be responsible and make your GPU hang directly. If you simply
cannot get it to ever stop hanging, try decreasing the intensity, and if even
that fails, try changing to the poclbm kernel with --set-device
OCL:kernel=poclbm, though you will sacrifice performance. BFGMiner is designed
to try and safely restart GPUs as much as possible, but NOT if that restart
might actually crash the rest of the GPUs mining, or even the machine. It tries
to restart them with a separate thread and if that separate thread dies, it
gives up trying to restart any more GPUs.

Q: Can you change the autofan/autogpu to change speeds in a different manner?
A: The defaults are sane and safe. I'm not interested in changing them further.
The starting fan speed is set to 50% in auto-fan mode as a safety precaution.

Q: I upgraded BFGMiner version and my hashrate suddenly dropped!
A: No, you upgraded your SDK version unwittingly between upgrades of BFGMiner
and that caused your hashrate to drop. Please see the next question.

Q: I upgraded my ATI driver/SDK/BFGMiner and my hashrate suddenly dropped!
A: The hashrate performance in BFGMiner is tied to the version of the ATI SDK
that is installed only for the very first time BFGMiner is run. This generates
binaries that are used by the GPU every time after that. Any upgrades to the
SDK after that time will have no effect on the binaries. However, if you
install a fresh version of BFGMiner, and have since upgraded your SDK, new
binaries will be built. It is known that the 2.6 ATI SDK has a huge hashrate
penalty on generating new binaries. It is recommended to not use this SDK at
this time unless you are using an ATI 7xxx card that needs it.

Q: Which AMD SDK is the best for BFGMiner?
A: At the moment, versions 2.4 and 2.5 work the best for 5xxx and 6xxx GPUs. SDK
2.6 or 2.7 works best for 7xxx. SDK 2.8 is known to have many problems. If you
need to use the 2.6+ SDK (7xxx and later), the phatk kernel will perform poorly,
while the diablo or (modified) poclbm kernel are optimised for it.

Q: Which AMD driver is the best?
A: Unfortunately AMD has a history of having quite a few releases with issues
when it comes to mining, either in terms of breaking mining, increasing CPU
usage or very low hashrates. Only experimentation can tell you for sure, but
some good releases were 11.6, 11.12, 12.4 and 12.8. Note that older cards may
not work with the newer drivers.

Q: I have multiple SDKs installed, can I choose which one it uses?
A: Run bfgminer with the -n option and it will list all the platforms currently
installed. Then you can tell BFGMiner which platform to use with --gpu-platform.

Q: BFGMiner reports no devices or only one device on startup on Linux although
I have multiple devices and drivers+SDK installed properly?
A: Try "export DISPLAY=:0" before running BFGMiner.

Q: BFGMiner crashes immediately on startup.
A: One of the common reasons for this is that you have mixed files on your
machine for the driver or SDK. Windows has a nasty history of not cleanly
uninstalling files so you may have to use third party tools like driversweeper
to remove old versions. The other common reason for this is windows antivirus
software is disabling one of the DLLs from working. If BFGMiner starts with the
-T option but never starts without it, this is a sure fire sign you have this
problem and will have to disable your antivirus or set up some exceptions in it
if possible.

Q: Is it faster to mine on Windows or Linux?
A: It makes no difference. It comes down to choice of operating system for their
various features. Linux offers much better long term stability and remote
monitoring and security, while Windows offers you overclocking tools that can
achieve much more than BFGMiner can do on Linux.

Q: BFGMiner cannot see any of my GPUs even though I have configured them all to
be enabled and installed OpenCL (+/- Xorg is running and the DISPLAY variable is
exported on Linux)?
A: Check the output of 'bfgminer -S opencl:auto -d?', it will list what OpenCL
devices your installed SDK recognises. If it lists none, you have a problem with
your version or installation of the SDK.

Q: BFGMiner is mining on the wrong GPU, I want it on the AMD but it's mining on
my on board GPU?
A: Make sure the AMD OpenCL SDK is installed, check the output of 'bfgminer -S
opencl:auto -d?' and use the appropriate parameter with --gpu-platform.

Q: I'm getting much lower hashrates than I should be for my GPU?
A: Look at your driver/SDK combination and disable power saving options for your
GPU. Specifically look to disable ULPS. Make sure not to set intensity above 11
for Bitcoin mining.

Q: Can I mine with AMD while running Nvidia or Intel GPUs at the same time?
A: If you can install both drivers successfully (easier on windows) then yes,
using the --gpu-platform option.

Q: Can I mine with Nvidia or Intel GPUs?
A: Yes, but the hashrate on these is very poor and it is likely you'll be using
much more energy than you'll be earning in coins.

Q: Can I mine on both Nvidia and AMD GPUs at the same time?
A: No, you must run one instance of BFGMiner with the --gpu-platform option for
each.

Q: Can I mine on Linux without running Xorg?
A: With Nvidia you can, but with AMD you cannot.

Q: I'm trying to mine a scrypt cryptocurrency but BFGMiner shows MH values
instead of kH and submits no shares?
A: Add the --scrypt parameter to your BFGMiner startup command.

Q: I can't get anywhere near enough hashrate for scrypt compared to other
people?
A: You may not have enough system RAM, as this is also required.

Q: My scrypt hashrate is high but the pool reports only a tiny proportion of my
hashrate?
A: You are generating garbage hashes due to your choice of settings. Try
decreasing your intensity, do not increase the number of gpu-threads, and
consider adding system RAM to match your GPU ram. You may also be using a bad
combination of driver and/or SDK.

Q: Scrypt fails to initialise the kernel every time?
A: Your parameters are too high. Don't add GPU threads, don't set intensity too
high, decrease thread concurrency. See the README.scrypt for a lot more help.

Q: Are OpenCL kernels from other mining software useable in BFGMiner?
A: The interfaces are often slightly different between the different software,
so in most cases they will not work without modifying BFGMiner itself. However,
if the kernel is compatible with one BFGMiner supports, you can edit it in a
text editor and add a line that looks like this to the top:
    // kernel-interface: poclbm
This will instruct BFGMiner to use the poclbm kernel interface. Then to use the
kernel, just use --set OCL:kernel=mykernel where the filename is mykernel.cl.

Q: BFGMiner stops mining (or my GPUs go DEAD) and I can't close it?
A: Once the driver has crashed, there is no way for BFGMiner to close cleanly.
You will have to kill it, and depending on how corrupted your driver state has
gotten, you may even need to reboot. Windows is known to reset drivers when they
fail and BFGMiner will be stuck trying to use the old driver instance.

Q: I can't get any monitoring of temperatures or fanspeed with BFGMiner when I
start it remotely?
A: With Linux, make sure to export the DISPLAY variable. On Windows, you cannot
access these monitoring values via RDP. This should work with TightVNC or
TeamViewer, though.

Q: I change my GPU engine/memory/voltage and BFGMiner reports back no change?
A: BFGMiner asks the GPU using the ATI Display Library to change settings, but
the driver and hardware are free to do what it wants with that query, including
ignoring it. Some GPUs are locked with one or more of those properties as well.
The most common of these is that many GPUs only allow a fixed difference
between the engine clock speed and the memory clock speed (such as the memory
being no lower than the engine - 150). Other 3rd party tools have unofficial
data on these devices on windows and can get the memory clock speed down
further but BFGMiner does not have access to these means.

Q: I have multiple GPUs and although many devices show up, it appears to be
working only on one GPU splitting it up.
A: Your driver setup is failing to properly use the accessory GPUs. Your driver
may be misconfigured or you have a driver version that needs a dummy plug on all
the GPUs that aren't connected to a monitor.

Q: I have some random GPU performance related problem not addressed above.
A: Seriously, it's the driver and/or SDK. Uninstall them and start again,
also noting there is no clean way to uninstall them so you will likely have to
use extra tools or do it manually.

Q: Do I need to recompile after updating my driver/SDK?
A: No. The software is unchanged regardless of which driver/SDK/ADL version you
are running.

Q: I do not want BFGMiner to modify my engine/clock/fanspeed?
A: BFGMiner only modifies values if you tell it to via the parameters.
Otherwise it will just monitor the values.

Q: Should I use crossfire/SLI?
A: It does not benefit mining at all and depending on the GPU may actually
worsen performance.
