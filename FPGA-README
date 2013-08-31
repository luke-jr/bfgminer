
This README contains extended details about FPGA mining with cgminer


For ModMinerQuad (MMQ) BitForce (BFL) and Icarus (ICA, BLT, LLT, AMU, CMR)
--------------------------------------------------------------------------

When mining on windows, the driver being used will determine if mining will work.

If the driver doesn't allow mining, you will get a "USB init," error message
i.e. one of:
 open device failed, err %d, you need to install a WinUSB driver for the device
or
 claim interface %d failed, err %d

The best solution for this is to use a tool called Zadig to set the driver:
 http://sourceforge.net/projects/libwdi/files/zadig/

This allows you set the driver for the device to be WinUSB which is usually
required to make it work if you're having problems

With Zadig, you may need to run it as administrator and if your device is
plugged in but you cannot see it, use the Menu: Options -> List All Devices

You must also make sure you are using the latest libusb-1.0.dll supplied
with cgminer (not the libusbx version)

When you first switch a device over to WinUSB with Zadig and it shows that
correctly on the left of the Zadig window, but it still gives permission
errors, you may need to unplug the USB miner and then plug it back in

-

When mining on linux, but not using 'sudo' and not logged into 'root' you
may get a USB priviledge error (-3), so you may also need to do the following:

 sudo cp 01-cgminer.rules /etc/udev/rules.d/

And also:
 sudo usermod -G plugdev -a `whoami`

If your linux distro doesn't have the 'plugdev' group, you can create it like:
 sudo groupadd plugdev

Then reboot ...

-

There is a hidden option in cgminer to dump out a lot of information
about USB that will help the developers to assist you if you are having
problems:

 --usb-dump 0

It will only help if you have a working FPGA device listed above


ModMinerQuad (MMQ)
------------------

The mining bitstream does not survive a power cycle, so cgminer will upload
it, if it needs to, before it starts mining (approx 7min 40sec)

The red LED also flashes while it is uploading the bitstream

-

If the MMQ doesn't respond to cgminer at all, or the red LED isn't flashing
then you will need to reset the MMQ

The red LED should always be flashing when it is mining or ready to mine

To reset the MMQ, you are best to press the left "RESET" button on the
backplane, then unplug and replug the USB cable

If your MMQ doesn't have a button on the "RESET" pad, you need to join
the two left pads of the "RESET" pad with conductive wire to reset it.
Cutting a small (metal) paper-clip in half works well for this

Then unplug the USB cable, wait for 5 seconds, then plug it back in

After you press reset, the red LED near the USB port should blink continuously

If it still wont work, power off, wait for 5 seconds, then power on the MMQ
This of course means it will upload the bitstream again when you start cgminer

-

Device 0 is on the power end of the board

-

You must make sure you have an approriate firmware in your MMQ
Read here for official details of changing the firmware:
 http://wiki.btcfpga.com/index.php?title=Firmware

The basics of changing the firmware are:
 You need two short pieces of conductive wire if your MMQ doesn't have
 buttons on the "RESET" and "ISP" pads on the backplane board
 Cutting a small (metal) paper-clip in half works well for this

 Join the 2 left pads of the "RESET" pad with wire and the led will dim
 Without disconnecting the "RESET", join the 2 left pads of the "ISP" pad
 with a wire and it will stay dim
 Release "RESET" then release "ISP" and is should still be dim
 Unplug the USB and when you plug it back in it will show up as a mass
 storage device
  Linux: (as one single line):
   mcopy -i /dev/disk/by-id/usb-NXP_LPC134X_IFLASH_ISP000000000-0:0
      modminer091012.bin ::/firmware.bin
  Windows: delete the MSD device file firmware.bin and copy in the new one
   rename the new file and put it under the same name 'firmware.bin'
 Disconnect the USB correctly (so writes are flushed first)
 Join and then disconnect "RESET" and then plug the USB back in and it's done

Best to update to one of the latest 2 listed below if you don't already
have one of them in your MMQ

The current latest different firmware are:

 Latest for support of normal or TLM bitstream:
  http://btcfpga.com/files/firmware/modminer092612-TLM.bin

 Latest with only normal bitstream support (Temps/HW Fix):
  http://btcfpga.com/files/firmware/modminer091012.bin

The code is currently tested on the modminer091012.bin firmware.
This comment will be updated when others have been tested

-

On many linux distributions there is an app called modem-manager that
may cause problems when it is enabled, due to opening the MMQ device
and writing to it

The problem will typically present itself by the flashing led on the
backplane going out (no longer flashing) and it takes a power cycle to
re-enable the MMQ firmware - which then can lead to the problem happening
again

You can either disable/uninstall modem-manager if you don't need it or:
a (hack) solution to this is to blacklist the MMQ USB device in
/lib/udev/rules.d/77-mm-usb-device-blacklist.rules

Adding 2 lines like this (just above APC) should help
# MMQ
ATTRS{idVendor}=="1fc9", ATTRS{idProduct}=="0003", ENV{ID_MM_DEVICE_IGNORE}="1"

The change will be lost and need to be re-done, next time you update the
modem-manager software

TODO: check that all MMQ's have the same product ID


BitForce (BFL)
--------------

--bfl-range         Use nonce range on bitforce devices if supported

This option is only for bitforce devices. Earlier devices such as the single
did not have any way of doing small amounts of work which meant that a lot of
work could be lost across block changes. Some of the "minirigs" have support
for doing this, so less work is lost across a longpoll. However, it comes at
a cost of 1% in overall hashrate so this feature is disabled by default. It
is only recommended you enable this if you are mining with a minirig on
p2pool.

C source is included for a bitforce firmware flash utility on Linux only:
 bitforce-firmware-flash.c
Using this, you can change the bitstream firmware on bitforce singles.
It is untested with other devices. Use at your own risk!

To compile:
 make bitforce-firmware-flash
To flash your BFL, specify the BFL port and the flash file e.g.:
 sudo ./bitforce-firmware-flash /dev/ttyUSB0 alphaminer_832.bfl
It takes a bit under 3 minutes to flash a BFL and shows a progress % counter
Once it completes, you may also need to wait about 15 seconds,
then power the BFL off and on again

If you get an error at the end of the BFL flash process stating:
 "Error reading response from ZBX"
it may have worked successfully anyway.
Test mining on it to be sure if it worked or not.

You need to give cgminer about 10 minutes mining with the BFL to be sure of
the MH/s value reported with the changed firmware - and the MH/s reported
will be less than the firmware speed since you lose work on every block change.


Icarus (ICA, BLT, LLT, AMU, CMR)
--------------------------------

There are two hidden options in cgminer when Icarus support is compiled in:

--icarus-options <arg> Set specific FPGA board configurations - one set of values for all or comma separated
           baud:work_division:fpga_count

           baud           The Serial/USB baud rate - 115200 or 57600 only - default 115200
           work_division  The fraction of work divided up for each FPGA chip - 1, 2, 4 or 8
                          e.g. 2 means each FPGA does half the nonce range - default 2
           fpga_count     The actual number of FPGA working - this would normally be the same
                          as work_division - range is from 1 up to 'work_division'
                          It defaults to the value of work_division - or 2 if you don't specify
                          work_division

If you define fewer comma seperated values than Icarus devices, the last values will be used
for all extra devices

An example would be: --icarus-options 57600:2:1
This would mean: use 57600 baud, the FPGA board divides the work in half however
only 1 FPGA actually runs on the board (e.g. like an early CM1 Icarus copy bitstream)

--icarus-timing <arg> Set how the Icarus timing is calculated - one setting/value for all or comma separated
           default[=N]   Use the default Icarus hash time (2.6316ns)
           short=[N]     Calculate the hash time and stop adjusting it at ~315 difficulty 1 shares (~1hr)
           long=[N]      Re-calculate the hash time continuously
           value[=N]     Specify the hash time in nanoseconds (e.g. 2.6316) and abort time (e.g. 2.6316=80)

If you define fewer comma seperated values than Icarus devices, the last values will be used
for all extra devices

Icarus timing is required for devices that do not exactly match a default Icarus Rev3 in
processing speed
If you have an Icarus Rev3 you should not normally need to use --icarus-timing since the
default values will maximise the MH/s and display it correctly

Icarus timing is used to determine the number of hashes that have been checked when it aborts
a nonce range (including on a LongPoll)
It is also used to determine the elapsed time when it should abort a nonce range to avoid
letting the Icarus go idle, but also to safely maximise that time

'short' or 'long' mode should only be used on a computer that has enough CPU available to run
cgminer without any CPU delays (an active desktop or swapping computer would not be stable enough)
Any CPU delays while calculating the hash time will affect the result
'short' mode only requires the computer to be stable until it has completed ~315 difficulty 1 shares
'long' mode requires it to always be stable to ensure accuracy, however, over time it continually
corrects itself
The optional additional =N for 'short' or 'long' specifies the limit to set the timeout to in N * 100ms
thus if the timing code calculation is higher while running, it will instead use N * 100ms
This can be set to the appropriate value to ensure the device never goes idle even if the
calculation is negatively affected by system performance

When in 'short' or 'long' mode, it will report the hash time value each time it is re-calculated
In 'short' or 'long' mode, the scan abort time starts at 5 seconds and uses the default 2.6316ns
scan hash time, for the first 5 nonce's or one minute (whichever is longer)

In 'default' or 'value' mode the 'constants' are calculated once at the start, based on the default
value or the value specified
The optional additional =N specifies to set the default abort at N * 100ms, not the calculated
value, which is ~112 for 2.6316ns

To determine the hash time value for a non Icarus Rev3 device or an Icarus Rev3 with a different
bitstream to the default one, use 'long' mode and give it at least a few hundred shares, or use
'short' mode and take note of the final hash time value (Hs) calculated
You can also use the RPC API 'stats' command to see the current hash time (Hs) at any time

The Icarus code currently only works with an FPGA device that supports the same commands as
Icarus Rev3 requires and also is less than ~840MH/s and greater than 2MH/s
If an FPGA device does hash faster than ~840MH/s it should work correctly if you supply the
correct hash time nanoseconds value

The Icarus code will automatically detect Icarus, Lancelot, AsicminerUSB and Cairnsmore1
FPGA devices and set default settings to match those devices if you don't specify them

The timing code itself will affect the Icarus performance since it increases the delay after
work is completed or aborted until it starts again
The increase is, however, extremely small and the actual increase is reported with the
RPC API 'stats' command (a very slow CPU will make it more noticeable)
Using the 'short' mode will remove this delay after 'short' mode completes
The delay doesn't affect the calculation of the correct hash time
