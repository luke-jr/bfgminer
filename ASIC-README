SUPPORTED DEVICES

Currently supported devices include the Avalon (including BitBurner and
Klondike), the Butterfly Labs SC range of devices, the ASICMINER block
erupters, the BF1 (bitfury) USB (red and blue) devices, KnCminer Mercury,
Saturn and Jupiter devices, and upcoming Hashfast devices.

No COM ports on windows or TTY devices will be used by cgminer as it
communicates directly with them via USB so it is normal for them to not exist or
be disconnected when cgminer is running.

The BFL devices should come up as one of the following:

BAJ: BFL ASIC Jalapeño
BAL: BFL ASIC Little Single
BAS: BFL ASIC Single
BAM: BFL ASIC Minirig

BFL devices need the --enable-bflsc option when compiling cgminer yourself.

Avalon will come up as AVA.

Avalon devices need the --enable-avalon option when compiling cgminer.

Klondike will come up as KLN.

Klondike devices need the --enable-klondike option when compiling cgminer.

ASICMINER block erupters will come up as AMU.

ASICMINER devices need the --enable-icarus option when compiling cgminer.
Also note that the AMU is managed by the Icarus driver which is detailed
in the FPGA-README. Configuring them uses the same mechanism as outlined
below for getting started with butterfly labs ASICs.


BITFURY devices

Bitfury devices need the --enable-bitfury option when compiling cgminer.

Currently only the BPMC BF1 devices AKA redfury/bluefury are supported and
come up as BF1. There are no options available for them. Bitfury device are
also set up as per the butterfly labs ASICs below.



GETTING STARTED WITH BUTTERFLY LABS ASICS

Unlike other software, cgminer uses direct USB communication instead of the
ancient serial USB communication to be much faster, more reliable and use a
lot less CPU. For this reason, setting up for mining with cgminer on these
devices requires different drivers.


WINDOWS:

On windows, the direct USB support requires the installation of a WinUSB
driver (NOT the ftdi_sio driver), and attach it to the Butterfly labs device.
The easiest way to do this is to use the zadig utility which will install the
drivers for you and then once you plug in your device you can choose the
"list all devices" from the "option" menu and you should be able to see the
device as something like: "BitFORCE SHA256 SC". Choose the install or replace
driver option and select WinUSB. You can either google for zadig or download
it from the cgminer directory in the DOWNLOADS link above.

When you first switch a device over to WinUSB with zadig and it shows that
correctly on the left of the zadig window, but it still gives permission
errors, you may need to unplug the USB miner and then plug it back in. Some
users may need to reboot at this point.


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


ASIC SPECIFIC COMMANDS

--avalon-auto       Adjust avalon overclock frequency dynamically for best hashrate
--avalon-cutoff <arg> Set avalon overheat cut off temperature (default: 60)
--avalon-fan <arg> Set fanspeed percentage for avalon, single value or range (default: 20-100)
--avalon-freq <arg> Set frequency range for avalon-auto, single value or range
--avalon-options <arg> Set avalon options baud:miners:asic:timeout:freq
--avalon-temp <arg> Set avalon target temperature (default: 50)
--bflsc-overheat <arg> Set overheat temperature where BFLSC devices throttle, 0 to disable (default: 90)
--bitburner-fury-options <arg> Override avalon-options for BitBurner Fury boards baud:miners:asic:timeout:freq
--bitburner-fury-voltage <arg> Set BitBurner Fury core voltage, in millivolts
--bitburner-voltage <arg> Set BitBurner (Avalon) core voltage, in millivolts
--klondike-options <arg> Set klondike options clock:temptarget


AVALON AND BITBURNER DEVICES

Currently all known Avalon devices come with their own operating system and
a preinstalled version of cgminer as part of the flash firmware, based on the
most current cgminer version so no configuration should be necessary. It is
possible to plug a USB cable from a PC into the Avalon device and mine using
cgminer as per any other device. It will autodetect and hotplug using default
options. You can customise the avalon behaviour by using the avalon-options
command, and adjust its fan control-temperature relationship with avalon-temp.
By default the avalon will also cut off when its temperature reaches 60
degrees.

All current BitBurner devices (BitBurner X, BitBurner XX and BitBurner Fury)
emulate Avalon devices, whether or not they use Avalon chips.

Avalon commands:

--avalon-auto       Adjust avalon overclock frequency dynamically for best hashrate
--avalon-cutoff <arg> Set avalon overheat cut off temperature (default: 60)
--avalon-fan <arg> Set fanspeed percentage for avalon, single value or range (default: 20-100)
--avalon-freq <arg> Set frequency range for avalon-auto, single value or range
--avalon-options <arg> Set avalon options baud:miners:asic:timeout:freq
--avalon-temp <arg> Set avalon target temperature (default: 50)
--bitburner-fury-options <arg> Override avalon-options for BitBurner Fury boards baud:miners:asic:timeout:freq
--bitburner-fury-voltage <arg> Set BitBurner Fury core voltage, in millivolts
--bitburner-voltage <arg> Set BitBurner (Avalon) core voltage, in millivolts


Avalon auto will enable dynamic overclocking gradually increasing and
decreasing the frequency till the highest hashrate that keeps hardware errors
under 2% is achieved. This WILL run your avalon beyond its normal specification
so the usual warnings apply. When avalon-auto is enabled, the avalon-options
for frequency and timeout are used as the starting point only.

eg:
--avalon-fan 50
--avalon-fan 40-80

By default the avalon fans will be adjusted to maintain a target temperature
over a range from 20 to 100% fanspeed. avalon-fan allows you to limit the
range of fanspeeds to a single value or a range of values.

eg:
--avalon-freq 300-350

In combination with the avalon-auto command, the avalon-freq command allows you
to limit the range of frequencies which auto will adjust to.

eg:
--avalon-temp 55

This will adjust fanspeed to keep the temperature at or slightly below 55.
If you wish the fans to run at maximum speed, setting the target temperature
very low such as 0 will achieve this. This option can be added to the "More
options" entry in the web interface if you do not have a direct way of setting
it.

eg:
--avalon-cutoff 65

This will cut off the avalon should it get up to 65 degrees and will then
re-enable it when it gets to the target temperature as specified by avalon-temp.

eg:
--avalon-options 115200:24:10:45:282

The values are baud : miners : asic count : timeout : frequency.

Baud:
The device is pretty much hard coded to emulate 115200 baud so you shouldn't
change this.

Miners:
Most Avalons are 3 module devices, which come to 24 miners. 4 module devices
would use 32 here.

For BitBurner X and BitBurner XX devices you should use twice the number of
boards in the stack.  e.g. for a two-board stack you would use 4.  For
BitBurner Fury devices you should use the total number of BitFury chips in the
stack (i.e. 16 times the number of boards).  e.g. for a two-board stack you
would use 32.

Asic count:
Virtually all have 10, so don't change this.  BitBurner devices use 10 here
even if the boards have some other number of ASICs.

Timeout:
This is how long the device will work on a work item before accepting new work
to replace it. It should be changed according to the frequency (last setting).
It is possible to set this a little lower if you are trying to tune for short
block mining (eg p2pool) but much lower and the device will start creating
duplicate shares.
A value of 'd' means cgminer will calculate it for you based on the frequency

Sample settings for valid different frequencies (last 2 values):
34:375 *
36:350 *
39:325 *
43:300
45:282 (default)
47:270
50:256

Frequency:
This is the clock speed of the devices. For Avalon devices, only specific
values work, 256, 270, 282 (default), 300, 325, 350 and 375.  For BitBurner
devices, other values can be used.

Note that setting a value with an asterisk next to it will be using your
avalon outside its spec and you do so at your own risk.

The default frequency for BitBurner X and BitBurner XX boards is 282.  The
default frequency for BitBurner Fury boards is 256.  Overclocking is
possible - please consult the product documentation and/or manufacturer for
information on safe values.  Values outside this range are used at your own
risk.  Underclocking is also possible, at least with the X and XX boards.

eg:
--bitburner-fury-options <arg> Override avalon-options for BitBurner Fury boards baud:miners:asic:timeout:freq

This option takes the same format as --avalon-options.  When specified, it
will be used for BitBurner Fury boards in preference to the values specified
in --avalon-options.  (If not specified, BitBurner Fury boards will be
controlled by the values used in --avalon options.)  See --avalon-options for
a detailed description of the fields.

This option is particularly useful when using a mixture of different BitBurner
devices as BitBurner Fury devices generally require significantly different
clock frequencies from Avalon-based devices.  This option is only available
for boards with recent firmware that are recognized by cgminer as BBF.

eg:
--bitburner-fury-voltage <arg> Set BitBurner Fury core voltage, in millivolts

Sets the core voltage for the BitBurner Fury boards.  The default value is
900.  Overvolting is possible - please consult the product documentation
and/or manufaturer about the safe range of values.  Values outside this range
are used at your own risk.

This option is only available for boards with recent firmware that are
recognized by cgminer as BBF.  For boards recognized as BTB, see
--bitburner-voltage

eg:
--bitburner-voltage <arg> Set BitBurner (Avalon) core voltage, in millivolts

Sets the core voltage for the Avalon-based BitBurner X and BitBurner XX
boards.  The default value is 1200.  Overvolting and undervolting is
possible - please consult the product documentation and/or the manufacturer
for information about the safe range.  Values outside this range are used at
your own risk.

Older BitBurner Fury firmware emulates a BitBurner XX board and is identified
by cgminer as BTB.  On these devices, --bitburner-voltage is used to control
the voltage of the BitBurner Fury board.  The actual core voltage will be
300mV less than the requested voltage, so to run a BitBurner Fury board at
950mV use --bitburner-voltage 1250.  The default value of 1200 therefore
corresponds to the default core voltage of 900mV.


If you use the full curses based interface with Avalons you will get this
information:
AVA 0: 22/ 46C  2400R

The values are:
ambient temp / highest device temp  lowest detected ASIC cooling fan RPM.

Use the API for more detailed information than this.


BFLSC Devices

--bflsc-overheat <arg> Set overheat temperature where BFLSC devices throttle, 0 to disable (default: 90)

This will allow you to change or disable the default temperature where cgminer
throttles BFLSC devices by allowing them to temporarily go idle.


---

This code is provided entirely free of charge by the programmer in his spare
time so donations would be greatly appreciated. Please consider donating to the
address below.

Con Kolivas <kernel@kolivas.org>
15qSxP1SQcUX3o4nhkfdbgyoWEFMomJ4rZ
