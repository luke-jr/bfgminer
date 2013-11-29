SUPPORTED DEVICES

Currently supported ASIC devices include Avalon, Bitfountain's Block Erupter
series (both USB and blades), a large variety of Bitfury-based miners,
Butterfly Labs' SC range of devices, HashBuster boards, Klondike modules, and
KnCminer's Mercury, Jupiter and Saturn.


AVALON
------

Currently, Avalon boards are supported only by connecting them directly (or via
a hub) to a regular PC running BFGMiner. It is also possible to install the
OpenWrt packages of BFGMiner to the Avalon's embedded controller, but this is
not a simple task due to its lack of available flash space.

To use the Avalon from a regular PC, you will need to specify two options:
First, add the -S option specifying the avalon driver specifically. For example,

-S avalon:\\.\COM9

Next, use the --avalon-options copying the command as used by the internal
router used by the Avalon. eg:

--avalon-options 115200:24:10:45:282

The values are baud : miners : asic count : timeout : frequency.

Baud:
The device is essentially hard coded to emulate 115200 baud so you shouldn't
change this.

Miners:
Most Avalons are 3 module devices, which come to 24 miners. 4 module devices
would use 32 here.

Asic count:
Virtually all have 10, so don't change this.

Timeout:
This defines how long the device will work on a work item before accepting new
work to replace it. It should be changed according to the frequency (last
setting).
It is possible to set this a little lower if you are trying to tune for short
block mining (eg p2pool) but much lower and the device will start creating
duplicate shares.

Sample settings for valid different frequencies (last 2 values):
34:375
36:350
39:325
43:300
45:282
47:270
50:256

Frequency:
This is the clock speed of the devices. Only specific values work, 256, 270,
282 (default), 300, 325, 350 and 375.

If you use the full curses based interface with Avalons you will get this
information:
AVA 0: 22/ 46C  60%/2400R

The values are:
ambient temp / highest device temp  set fan % / lowest detected fan RPM.

Check the API for more detailed information.


BFSB, MEGABIGPOWER, AND METABANK BITFURY BOARDS
-----------------------------------------------

Both BFSB and MegaBigPower (V2 only at this time) boards are supported with the
"bfsb" driver. Metabank boards are supported with the "metabank" driver. These
drivers are not enabled by default, since they must be run on a Raspberry Pi in
a specific hardware configuration with the boards. To enable them, you must
build with --enable-bfsb or --enable-metabank. Do not try to use these drivers
without the manufacturer-supported hardware configuration! Also note that these
drivers do not properly support thermal shutdown at this time, and without
sufficient cooling you may destroy your board or chips!

To start BFGMiner, ensure your Raspberry Pi's SPI is enabled (you can run the
raspi-config utility for this). For Metabank boards, you must also load the I2C
drivers (do not try to modprobe both with a single command; it won't work):
    modprobe i2c-bcm2708
    modprobe i2c-dev
Then you must run BFGMiner as root, with the proper driver selected.
For example:
    sudo bfgminer -S bfsb:auto


BI*FURY
-------

Bi*Fury should just work; you may need to use -S bifury:<path>

On Windows, you will need to install the standard USB CDC driver for it.
    http://store.bitcoin.org.pl/support

If you want to upgrade the firmware, unplug your device. You will need to
temporarily short a circuit. With the USB connector pointing forward, and the
heatsink down, look to the forward-right; you will see two tiny lights, a set of
2 terminals, and a set of 3 terminals. The ones you need to short are the set of
2. With them shorted, plug the device back into your computer. It will then
pretend to be a mass storage disk drive. If you use Windows, you can play along
and just overwrite the firmware.bin file. If you use Linux, you must use mcopy:
    mcopy -i /dev/disk/by-id/usb-NXP_LPC1XXX_IFLASH_ISP-0:0 firmware.bin \
        ::/firmware.bin
After this is complete, unplug the device again and un-short the 2 terminals.
This completes the upgrade and you can now plug it back in and start mining.


BIG PICTURE MINING BITFURY USB
------------------------------

These miners are sensitive to unexpected data. Usually you can re-plug them to
reset to a known-good initialisation state. To ensure they are properly detected
and used with BFGMiner, you must specify -S bigpic:all (or equivalent) options
prior to any other -S options (which might probe the device and confuse it).


BLOCK ERUPTER BLADE
-------------------

Blades communicate over Ethernet using the old but simple getwork mining
protocol. If you build BFGMiner with libmicrohttpd, you can have it work with
one or more blades. First, start BFGMiner with the --http-port option. For
example:
    bfgminer --http-port 8330
Then configure your blade to connect to your BFGMiner instance on the same port,
with a unique username per blade. It will then show up as a SGW device and
should work more or less like any other miner.


BLOCK ERUPTER USB
-----------------

These will autodetect if supported by the device; otherwise, you need to use
the '--scan-serial erupter:<device>' option to tell BFGMiner what device to
probe; if you know you have no other serial devices, or only ones that can
tolerate garbage, you can use '--scan-serial erupter:all' to probe all serial
ports. They communicate with the Icarus protocol, which has some additional
options in README.FPGA


KLONDIKE
--------

--klondike-options <arg> Set klondike options clock:temptarget


KNCMINER
--------

The KnC miner uses a BeagleBoneBlack(BBB) as the host, this is pluged into a
cape that holds the FPGA and connections for 4-6 ASICS depending on the cape
version. The BBB runs the Angstrom linux distribution, the following is a step
by step install for BFGMiner on this distro;

-----------------Start------------
cat >/etc/opkg/feeds.conf <<\EOF
src/gz noarch http://feeds.angstrom-distribution.org/feeds/v2013.06/ipk/eglibc/all/
src/gz base http://feeds.angstrom-distribution.org/feeds/v2013.06/ipk/eglibc/cortexa8hf-vfp-neon/base/
src/gz beaglebone http://feeds.angstrom-distribution.org/feeds/v2013.06/ipk/eglibc/cortexa8hf-vfp-neon/machine/beaglebone/
EOF

opkg update
opkg install angstrom-feed-configs
rm /etc/opkg/feeds.conf
opkg update

opkg install update-alternatives
opkg install automake autoconf make gcc cpp binutils git less pkgconfig-dev ncurses-dev libtool nano bash i2c-tools-dev
while ! opkg install libcurl-dev; do true; done
ln -s aclocal-1.12 /usr/share/aclocal

curl http://www.digip.org/jansson/releases/jansson-2.0.1.tar.bz2 | tar -xjvp
cd jansson-2.0.1
./configure --prefix=/usr CC=arm-angstrom-linux-gnueabi-gcc --disable-static NM=arm-angstrom-linux-gnueabi-nm
make install && ldconfig
cd ..

git clone git://github.com/luke-jr/bfgminer
cd bfgminer
./autogen.sh
git clone git://github.com/troydhanson/uthash
./configure --host=arm-angstrom-linux-gnueabi --enable-knc CFLAGS="-I$PWD/uthash/src -O0 -ggdb"
make AR=arm-angstrom-linux-gnueabi-ar

/etc/init.d/cgminer.sh stop
./bfgminer -S knc:auto -c /config/cgminer.conf

---------------END-------------

BFGMiner has also been incorporated into an unofficial firmware by uski01 called Bertmod this can be found on the kncminer forum.

---

This code is provided entirely free of charge by the programmer in his spare
time so donations would be greatly appreciated. Please consider donating to the
address below.

Luke-Jr <luke-jr+bfgminer@utopios.org>
1QATWksNFGeUJCWBrN4g6hGM178Lovm7Wh
