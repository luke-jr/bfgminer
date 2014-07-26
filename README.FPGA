
This README contains extended details about FPGA mining with BFGMiner


ModMiner (MMQ)
--------------

ModMiner does not have any persistent storage for bitstreams, so BFGMiner must
upload it after power on. For this to work, you must first download the
necessary bitstream file to BFGMiner's "bitstreams" directory, and give it the
name "fpgaminer_x6500-overclocker-0402.bit". You can download this bitstream
from FPGA Mining LLC's website:
    http://www.fpgamining.com/documentation/firmware

-

If the MMQ doesn't respond to BFGMiner at all, or the red LED isn't flashing
then you will need to reset the MMQ.

The red LED should always be flashing when it is mining or ready to mine.

To reset the MMQ, you are best to press the left "RESET" button on the
backplane, then unplug and replug the USB cable.

If your MMQ doesn't have a button on the "RESET" pad, you need to join the two
left pads of the "RESET" pad with conductive wire to reset it. Cutting a small
(metal) paper-clip in half works well for this.

Then unplug the USB cable, wait for 5 seconds, then plug it back in.

After you press reset, the red LED near the USB port should blink continuously.

If it still wont work, power off, wait for 5 seconds, then power on the MMQ
This of course means it will upload the bitstream again when you start BFGMiner.

-

Device 0 is on the power end of the board.

-

You must make sure you have an appropriate firmware in your MMQ
Read here for official details of changing the firmware:
 http://wiki.btcfpga.com/index.php?title=Firmware

The basics of changing the firmware are:
 You need two short pieces of conductive wire if your MMQ doesn't have buttons
 on the "RESET" and "ISP" pads on the backplane board.
 Cutting a small (metal) paper-clip in half works well for this.

 Join the 2 left pads of the "RESET" pad with wire and the led will dim.
 Without disconnecting the "RESET", join the 2 left pads of the "ISP" pad with
 a wire and it will stay dim.
 Release "RESET" then release "ISP" and is should still be dim.
 Unplug the USB and when you plug it back in it will show up as a mass storage
 device.
  Linux: (as one single line):
   mcopy -i /dev/disk/by-id/usb-NXP_LPC134X_IFLASH_ISP000000000-0:0
      modminer091012.bin ::/firmware.bin
  Windows: delete the MSD device file firmware.bin and copy in the new one
   rename the new file and put it under the same name 'firmware.bin'
 Disconnect the USB correctly (so writes are flushed first)
 Join and then disconnect "RESET" and then plug the USB back in and it's done.

Best to update to one of the latest 2 listed below if you don't already
have one of them in your MMQ.

The current latest different firmware are:

 Latest for support of normal or TLM bitstream:
  http://btcfpga.com/files/firmware/modminer092612-TLM.bin

 Latest with only normal bitstream support (Temps/HW Fix):
  http://btcfpga.com/files/firmware/modminer091012.bin

The code is currently tested on the modminer091012.bin firmware.
This comment will be updated when others have been tested.

-

On many Linux distributions there is an app called modem-manager that may cause
problems when it is enabled, due to opening the MMQ device and writing to it.

The problem will typically present itself by the flashing led on the backplane
going out (no longer flashing) and it takes a power cycle to re-enable the MMQ
firmware - which then can lead to the problem reoccurring.

You can either disable/uninstall modem-manager if you don't need it or:
a (hack) solution to this is to blacklist the MMQ USB device in
/lib/udev/rules.d/77-mm-usb-device-blacklist.rules

Adding 2 lines like this (just above APC) should help.
# MMQ
ATTRS{idVendor}=="1fc9", ATTRS{idProduct}=="0003", ENV{ID_MM_DEVICE_IGNORE}="1"

The change will be lost and need to be re-done, next time you update the
modem-manager software.


BitForce (BFL)
--------------

--bfl-range         Use nonce range on BitForce devices if supported

This option is only for BitForce devices. Earlier devices such as the single
did not have any way of doing small amounts of work which meant that a lot of
work could be lost across block changes. Some of the Mini Rigs have support
for doing this, so less work is lost across a longpoll. However, it comes at
a cost of 1% in overall hashrate so this feature is disabled by default. It
is only recommended you enable this if you are mining with a Mini Rig on
P2Pool.

BitFORCE FPGA Single units can have their bitstream modified using the
bitforce-firmware-flash utility on Linux, which can be obtained from:
    https://github.com/luke-jr/bitforce-fpga-firmware-flash
It is untested with other devices. Use at your own risk! Windows users may use
Butterfly Labs EasyMiner to change firmware.

To compile:
 make bitforce-firmware-flash
To flash your BFL, specify the BFL port and the flash file e.g.:
 sudo ./bitforce-firmware-flash /dev/ttyUSB0 alphaminer_832.bfl
It takes a bit under 3 minutes to flash a BFL and shows a progress % counter
Once it completes, you may also need to wait about 15 seconds, then power the
BFL off and on again.

If you get an error at the end of the BFL flash process stating:
 "Error reading response from ZBX"
it may have worked successfully anyway.
Test mining on it to be sure if it worked or not.

You need to give BFGMiner about 10 minutes mining with the BFL to be sure of
the Mh/s value reported with the changed firmware - and the Mh/s reported will
be less than the firmware speed since you lose work on every block change.


Icarus (ICA)
------------

There are a number of options for Icarus-compatible devices which can be used
with --set-devices (or the RPC pgaset method):

    baud=N           The serial baud rate (default 115200)
    work_division=N  The fraction of work divided up for each processor: 1, 2,
                     4, or 8. e.g. 2 means each does half the nonce range
                     (default 2)
    fpga_count=N     The actual number of processors working; this would
                     normally be the same as work_division. Range is from 1 up
                     to <work_division>. It defaults to the value of
                     work_division, or 2 if you don't specify work_division.
    reopen=MODE      Controls how often the driver reopens the device to
                     workaround issues. Choices are 'never', on 'timeout' only
                     (default), or every 'cycle'.
    timing=MODE      Set how the timing is calculated:
                         default[=N]   Use the default hash time
                         short[=N]     Calculate the hash time and stop
                                       adjusting it at ~315 difficulty 1 shares
                                       (~1hr)
                         long=[N]      Re-calculate the hash time continuously
                         value[=N]     Specify the hash time in nanoseconds
                                       (e.g. 2.6316) and abort time (e.g.
                                       2.6316=80).

An example would be: --set-device ECM:baud=57600 --set-device
ECM:work_division=2 --set-device DCM:fpga_count=1 --set-device ECM:reopen=never
This would mean: use 57600 baud, the FPGA board divides the work in half however
only 1 FPGA actually runs on the board, and don't reopen the device (e.g. like
an early CM1 Icarus copy bitstream).

Icarus timing is used to determine the number of hashes that have been checked
when it aborts a nonce range (including on a longpoll).
It is also used to determine the elapsed time when it should abort a nonce
range to avoid letting the Icarus go idle, but also to safely maximise that
time.

'short' or 'long' mode should only be used on a computer that has enough CPU
available to run BFGMiner without any CPU delays.
Any CPU delays while calculating the hash time will affect the result
'short' mode only requires the computer to be stable until it has completed
~315 difficulty 1 shares, 'long' mode requires it to always be stable to ensure
accuracy, however, over time it continually corrects itself.
The optional additional =N for 'short' or 'long' specifies the limit to set the
timeout to in deciseconds; thus if the timing code calculation is higher while
running, it will instead use the limit.
This can be set to the appropriate value to ensure the device never goes idle
even if the calculation is negatively affected by system performance.

When in 'short' or 'long' mode, it will report the hash time value each time it
is re-calculated.
In 'short' or 'long' mode, the scan abort time starts at 5 seconds and uses the
default 2.6316ns scan hash time, for the first 5 nonces or one minute
(whichever is longer).

In 'default' or 'value' mode the 'constants' are calculated once at the start,
based on the default value or the value specified.
The optional additional =N specifies to set the default abort at N 1/10ths of a
second, not the calculated value, which is 112 for 2.6316ns

To determine the hash time value for a non Icarus Rev3 device or an Icarus Rev3
with a different bitstream to the default one, use 'long' mode and give it at
least a few hundred shares, or use 'short' mode and take note of the final hash
time value (Hs) calculated.
You can also use the RPC API 'stats' command to see the current hash time (Hs)
at any time.

The Icarus code currently only works with devices that support the same commands
as Icarus Rev3 requires and also is less than ~840Mh/s and greater than 2Mh/s.
If your device does hash faster than ~840Mh/s it should work correctly if you
supply the correct hash time nanoseconds value.

The timing code itself will affect the Icarus performance since it increases
the delay after work is completed or aborted until it starts again.
The increase is, however, extremely small and the actual increase is reported
with the RPC API 'stats' command (a very slow CPU will make it more noticeable).
Using the 'short' mode will remove this delay after 'short' mode completes.
The delay doesn't affect the calculation of the correct hash time.


X6500
-----

Since X6500 FPGAs do not use serial ports for communication, the --scan-serial
option instead works with product serial numbers. By default, any devices with
the X6500 USB product id will be used, but some X6500s may have shipped without
this product id being configured. If you have any of these, you will need to
specify their serial numbers explicitly, and also add -S x6500:auto if you
still want to use the autodetection for other properly-configured FPGAs.
The serial number of X6500s is usually found on a label applied to the ATX
power connector slot. If yours is missing, devices seen by the system can be
displayed by starting bfgminer in debug mode. To get a simple list of devices,
with the debug output shown, you can use: bfgminer -D -d? -T

X6500 does not have any persistent storage for bitstreams, so BFGMiner must
upload it after power on. For this to work, you must first download the
necessary bitstream file to BFGMiner's "bitstreams" directory, and give it the
name "fpgaminer_x6500-overclocker-0402.bit". You can download this bitstream
from FPGA Mining LLC's website:
    http://www.fpgamining.com/documentation/firmware


ZTEX FPGA Boards
----------------

http://www.ztex.de sells two boards suitable for mining: the 1.15x with 1 FPGA
and the 1.15y with 4 FPGAs. ZTEX distributes their own mining software and
drivers. BFGMiner has full support for these boards, as long as they have at
least the "dummy" mining bitstreams installed on them.

If your boards do not have a mining bitstream yet, you must first, install
ZTEX's BTCMiner (requires Java JDK version 6 or later) and install one.

=== WINDOWS NOTE ===
Upon first powering up and connecting the board via USB, windows will attempt
and fail to find the appropriate drivers.  To load the initial firmware on the
board, you'll need the EZ-USB FX2 SDK from here:
    http://www.ztex.de/downloads/#firmware_kit
Extract the firmware kit and use the driver within libusb-win32/ztex.inf.
Windows should now recognize the board and you're ready to program it.
=== END OF WINDOWS ===

Grab the latest miner jar from http://www.ztex.de/btcminer/#download and program
the appropriate dummy firmware for your board.  The command should look
something like (for a single FPGA board):
    java -cp ZtexBTCMiner-120417.jar BTCMiner -m p -f **FILENAME** -s 01-02-01
For ZTEX 1.15x boards, the dummy bitstream filename is ztex_ufm1_15d.ihx
For ZTEX 1.15y boards, the dummy bitstream filename is ztex_ufm1_15y.ihx

=== WINDOWS NOTE ===
To mine using BFGMiner, you'll have to swap the USB drivers. The BFGMiner-
compatible WinUSB drivers for the board can be generated with this tool:
    http://sourceforge.net/projects/libwdi/files/zadig/
Basic usage instructions for Zadig can be found here:
    https://github.com/pbatard/libwdi/wiki/Zadig
Once Zadig generates and installs a WinUSB driver, ensure everything is working
by running:
    bfgminer -D -d? -T
You should see something like this in the output:
    [2013-01-22 20:19:11] Found 1 ztex board
    [2013-01-22 20:19:11] ZTX 0: Found Ztex (ZTEX 0001-02-01-1)
=== END OF WINDOWS ===

If you have installed a dummy bitstream, you will now need to copy the main
mining bitstream where BFGMiner can find it. This are usually the same as the
dummy bitstream filename, but with a number added to the end. Extract the
ZtexBTCMiner-120417.jar file using any unzip utility, and look for the proper
*.ihx and *.bit files (the latter will be inside the 'fpga' directory of the
jar). Copy them to BFGMiner's "bitstreams" directory, and you're ready to start
mining.
