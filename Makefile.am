# Copyright 2012-2013 Luke Dashjr
# Copyright 2012 zefir
# Copyright 2011-2013 Con Kolivas
# Copyright 2013 James Z.M. Gao
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option)
# any later version.  See COPYING for more details.

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST	= \
	m4/gnulib-cache.m4 \
	linux-usb-bfgminer \
	windows-build.txt

dist_doc_DATA = \
		  AUTHORS COPYING HACKING NEWS README \
		  example.conf \
		  README.RPC

rpcexamplesdir = $(docdir)/rpc-examples
dist_rpcexamples_DATA = \
		  api-example.php miner.php \
		  api-example.c \
                  api-example.py

SUBDIRS		= lib ccan.bfg

# Without a redirected rule, code depending on different lib/*.h files may try to build dependencies of that in parallel, which can fail
lib/%: lib_directory
	@test -e $@
lib_directory:
	$(MAKE) -C lib
ccan.bfg/libccan.a:
	$(MAKE) -C ccan.bfg $*

INCLUDES	= -fno-strict-aliasing

bin_PROGRAMS	= bfgminer

bfgminer_LDFLAGS	= $(PTHREAD_FLAGS)
bfgminer_LDADD	= $(DLOPEN_FLAGS) @LIBCURL_LIBS@ @JANSSON_LIBS@ @PTHREAD_LIBS@ \
		  @NCURSES_LIBS@ @PDCURSES_LIBS@ @WS2_LIBS@ \
		  @UDEV_LIBS@ @LIBUSB_LIBS@ @MM_LIBS@ @RT_LIBS@ \
		  @MATH_LIBS@ lib/libgnu.a ccan.bfg/libccan.a
bfgminer_CPPFLAGS = -I$(top_builddir)/lib -I$(top_srcdir)/lib @LIBUSB_CFLAGS@ @LIBCURL_CFLAGS@

bfgminer_CPPFLAGS += -DCGMINER_PREFIX='"$(pkgdatadir)"'

bfgminer_CPPFLAGS += -I$(top_srcdir)/ccan-upstream
bfgminer_CPPFLAGS += @JANSSON_CFLAGS@
bfgminer_CPPFLAGS += $(PTHREAD_FLAGS)
bfgminer_CPPFLAGS += $(NCURSES_CPPFLAGS)

bfgminer_CPPFLAGS += $(AUTOSCAN_CPPFLAGS)
bfgminer_LDADD += $(AUTOSCAN_LIBS)

bfgminer_LDADD += $(libblkmaker_LIBS)
bfgminer_LDFLAGS += $(libblkmaker_LDFLAGS)
bfgminer_CPPFLAGS += $(libblkmaker_CFLAGS)

# common sources
bfgminer_SOURCES := miner.c

bfgminer_SOURCES	+= miner.h compat.h bench_block.h	\
	deviceapi.c deviceapi.h \
		   util.c util.h logging.h		\
		   sha2.c sha2.h api.c
EXTRA_bfgminer_DEPENDENCIES =

if NEED_LIBBLKMAKER
SUBDIRS           += libblkmaker
EXTRA_bfgminer_DEPENDENCIES += libblkmaker_directory
libblkmaker_directory:
	cd libblkmaker && $(MAKE)

if HAVE_CYGWIN
EXTRA_bfgminer_DEPENDENCIES += cygblkmaker-0.1-0.dll cygblkmaker_jansson-0.1-0.dll

cyg%.dll: libblkmaker/.libs/cyg%.dll
	cp -p $< $@
endif

endif

bfgminer_SOURCES	+= logging.c

if USE_UDEVRULES
dist_udevrules_DATA = 70-bfgminer.rules
endif

if NEED_BFG_DRIVER_PROXY
bfgminer_SOURCES += driver-proxy.c driver-proxy.h
endif

if USE_LIBMICROHTTPD
bfgminer_SOURCES += httpsrv.c httpsrv.h driver-getwork.c
bfgminer_LDADD += $(libmicrohttpd_LIBS)
bfgminer_LDFLAGS += $(libmicrohttpd_LDFLAGS)
bfgminer_CPPFLAGS += $(libmicrohttpd_CFLAGS)
endif

if USE_LIBEVENT
bfgminer_SOURCES  += driver-stratum.c
bfgminer_LDADD    += $(libevent_LIBS)
bfgminer_LDFLAGS  += $(libevent_LDFLAGS)
bfgminer_CPPFLAGS += $(libevent_CFLAGS)
endif


if HAVE_OPENCL
bfgminer_SOURCES += driver-opencl.h driver-opencl.c
bfgminer_SOURCES += ocl.c ocl.h findnonce.c findnonce.h
bfgminer_SOURCES += adl.c adl.h adl_functions.h
bfgminer_SOURCES += *.cl

kernelsdir = $(pkgdatadir)/opencl
dist_kernels_DATA = $(top_srcdir)/*.cl

dist_doc_DATA += README.GPU

if HAVE_SENSORS
bfgminer_LDADD += $(sensors_LIBS)
endif

endif


if HAS_SCRYPT
bfgminer_SOURCES += scrypt.c scrypt.h
dist_doc_DATA += README.scrypt
endif

if HAS_CPUMINE
dist_doc_DATA += README.CPU
bfgminer_SOURCES	+= \
		  sha256_generic.c sha256_via.c	\
		  sha256_cryptopp.c sha256_sse2_amd64.c		\
		  sha256_sse4_amd64.c 	\
		  sha256_altivec_4way.c

# the CPU portion extracted from original main.c
bfgminer_SOURCES += driver-cpu.h driver-cpu.c

if HAVE_SSE2
bfgminer_LDADD  += libsse2cpuminer.a
noinst_LIBRARIES = libsse2cpuminer.a
libsse2cpuminer_a_SOURCES = sha256_4way.c
libsse2cpuminer_a_CFLAGS = $(bfgminer_CPPFLAGS) $(SSE2_CFLAGS)
endif

if HAS_YASM

AM_CFLAGS	= -DHAS_YASM
if HAVE_x86_64
SUBDIRS		+= x86_64
x86_64/libx8664.a:
	$(MAKE) -C x86_64 $*
bfgminer_LDADD	+= x86_64/libx8664.a
else # HAVE_x86_64
SUBDIRS		+= x86_32
x86_32/libx8632.a:
	$(MAKE) -C x86_32 $*
bfgminer_LDADD	+= x86_32/libx8632.a

if HAVE_SSE2
libsse2cpuminer_a_SOURCES +=  sha256_sse2_i386.c
endif

endif # HAVE_x86_64
endif # HAS_YASM
endif # HAS_CPUMINE

if NEED_BFG_LOWL_VCOM
bfgminer_SOURCES += lowl-vcom.c lowl-vcom.h
if HAVE_WINDOWS
else
bfgminer_SOURCES += iospeeds.h iospeeds_posix.h
endif
endif

if HAVE_LIBUSB
bfgminer_SOURCES += lowl-usb.c lowl-usb.h
endif

if NEED_BFG_BINLOADER
bfgminer_SOURCES += binloader.c binloader.h
endif

if NEED_BFG_LOWLEVEL
bfgminer_SOURCES += lowlevel.c lowlevel.h
endif

if NEED_DYNCLOCK
bfgminer_SOURCES += dynclock.c dynclock.h
endif

if HAS_FPGA
dist_doc_DATA += README.FPGA
endif

if HAS_ASIC
dist_doc_DATA += README.ASIC
endif

if HAS_BITFORCE
bfgminer_SOURCES += driver-bitforce.c

if HAVE_WINDOWS
else
bin_PROGRAMS += bitforce-firmware-flash
bitforce_firmware_flash_SOURCES = bitforce-firmware-flash.c
endif
endif

if HAS_BIGPIC
bfgminer_SOURCES += driver-bigpic.c driver-bigpic.h
endif

if USE_DRILLBIT
bfgminer_SOURCES += driver-drillbit.c
endif

if HAS_TWINFURY
bfgminer_SOURCES += driver-twinfury.c driver-twinfury.h
endif

if HAS_ICARUS
bfgminer_SOURCES += driver-icarus.c icarus-common.h
bfgminer_SOURCES += driver-cairnsmore.c
bfgminer_SOURCES += driver-erupter.c
bfgminer_SOURCES += driver-antminer.c
endif

if HAS_AVALON
bfgminer_SOURCES += driver-avalon.c driver-avalon.h hexdump.c
endif

if USE_KNC
bfgminer_SOURCES += driver-knc.c
endif

if HAS_KLONDIKE
bfgminer_SOURCES += driver-klondike.c driver-klondike.h driver-hashbusteravalon.c
endif

if HAS_MODMINER
bfgminer_SOURCES += driver-modminer.c
endif

if HAS_X6500
bfgminer_SOURCES += driver-x6500.c ft232r.c ft232r.h jtag.c jtag.h
endif

if HAS_ZTEX
bfgminer_SOURCES += driver-ztex.c libztex.c libztex.h
endif

if USE_BIFURY
bfgminer_SOURCES += driver-bifury.c
endif

if HAS_BITFURY
bfgminer_SOURCES += driver-bitfury.c driver-bitfury.h libbitfury.c libbitfury.h spidevc.h spidevc.c

if HAS_BFSB
bfgminer_SOURCES += driver-bfsb.c
endif

if HAS_METABANK
bfgminer_SOURCES += driver-metabank.c tm_i2c.h tm_i2c.c
endif

if HAS_LITTLEFURY
bfgminer_SOURCES += driver-littlefury.c
endif

if HAS_NANOFURY
bfgminer_SOURCES += driver-nanofury.c mcp2210.c mcp2210.h
endif

if USE_HASHBUSTER
bfgminer_SOURCES += driver-hashbuster.c
endif

if USE_HASHBUSTERUSB
bfgminer_SOURCES += driver-hashbusterusb.c
endif

endif

if USE_HASHFAST
bfgminer_SOURCES += driver-hashfast.c
endif

if NEED_BFG_LOWL_HID
bfgminer_SOURCES += lowl-hid.c lowl-hid.h
bfgminer_CPPFLAGS += $(hidapi_CFLAGS)
endif

if NEED_BFG_LOWL_PCI
bfgminer_SOURCES += lowl-pci.c lowl-pci.h
endif

bin_PROGRAMS += bfgminer-rpc
bfgminer_rpc_SOURCES = api-example.c
bfgminer_rpc_LDADD = @WS2_LIBS@
