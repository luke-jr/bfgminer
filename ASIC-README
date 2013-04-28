SUPPORTED DEVICES

Currently supported devices include the Avalon and the Butterfly Labs SC range
of devices. The BFL devices should come up as one of the following:

BAJ: BFL ASIC Jalapeño
BAL: BFL ASIC Little Single
BAS: BFL ASIC Single
BAM: BFL ASIC Minirig


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
accessible by anyone from the plugdev group you can either copy the file
"01-cgminer.rules" from the cgminer archive into the /etc/udev/rules.d
directory with the following command:

 sudo cp 01-cgminer.rules /etc/udev/rules.d/

Or you can manually create a file/add to a rules.d file with following rules
(most users won't want to do this manually):
ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6014", SUBSYSTEMS=="usb", ACTION=="add", MODE="0666", GROUP="plugdev"
ATTRS{idVendor}=="1fc9", ATTRS{idProduct}=="0003", SUBSYSTEMS=="usb", ACTION=="add", MODE="0666", GROUP="plugdev"

After this you can either manually restart udev and re-login, or more easily
just reboot.


AVALON DEVICES

Currently all known Avalon devices come with their own operating system and
a preinstalled version of cgminer as part of the flash firmware, based on the
most current cgminer version so no configuration should be necessary. It is
possible to plug a USB cable from a PC into the Avalon device and use the
--avalon-options copying the command as used by the internal router used by the
Avalon. However since the Avalon code still currently uses the old serial usb
interface and is being rewritten to use direct USB, it is prudent to not be
dependent on this command long term, assuming it will go away.

---

This code is provided entirely free of charge by the programmer in his spare
time so donations would be greatly appreciated. Please consider donating to the
address below.

Con Kolivas <kernel@kolivas.org>
15qSxP1SQcUX3o4nhkfdbgyoWEFMomJ4rZ
