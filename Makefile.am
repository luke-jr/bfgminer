# Copyright 2012-2014 Luke Dashjr
# Copyright 2012 zefir
# Copyright 2011-2013 Con Kolivas
# Copyright 2013 James Z.M. Gao
# Copyright 2013-2014 Nate Woolls
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

sbin_SCRIPTS =

if HAVE_WINDOWS
else
bin_SCRIPTS = start-bfgminer.sh
endif

# first, our own stuff
bfgminer_LDADD    = lib/libgnu.a ccan.bfg/libccan.a
bfgminer_LDFLAGS  =
bfgminer_CPPFLAGS = -I$(top_builddir)/lib -I$(top_srcdir)/lib -I$(top_srcdir)/ccan-upstream

# second, potentially embedded libs
bfgminer_LDADD    += $(libbase58_LIBS)   $(libblkmaker_LIBS)
bfgminer_CPPFLAGS += $(libbase58_CFLAGS) $(libblkmaker_CFLAGS)

# finally, everything else
bfgminer_LDFLAGS += $(PTHREAD_FLAGS)
bfgminer_LDADD   += $(DLOPEN_FLAGS) @LIBCURL_LIBS@ @JANSSON_LIBS@ @PTHREAD_LIBS@ \
		  @NCURSES_LIBS@ @PDCURSES_LIBS@ @WS2_LIBS@ \
		  @UDEV_LIBS@ @LIBUSB_LIBS@ @MM_LIBS@ @RT_LIBS@ \
                    $(MATH_LIBS)
bfgminer_CPPFLAGS += @LIBUSB_CFLAGS@ @LIBCURL_CFLAGS@

bfgminer_CPPFLAGS += -DCGMINER_PREFIX='"$(pkgdatadir)"'

bfgminer_CPPFLAGS += @JANSSON_CFLAGS@
bfgminer_CPPFLAGS += $(PTHREAD_FLAGS)
bfgminer_CPPFLAGS += $(NCURSES_CPPFLAGS)

bfgminer_CPPFLAGS += $(AUTOSCAN_CPPFLAGS)
bfgminer_LDADD += $(AUTOSCAN_LIBS)

# common sources
bfgminer_SOURCES := miner.c

bfgminer_SOURCES += miner.h compat.h  \
	deviceapi.c deviceapi.h \
		   util.c util.h logging.h		\
		   sha2.c sha2.h api.c
EXTRA_bfgminer_DEPENDENCIES =

TESTS = test-bfgminer.sh
EXTRA_DIST += test-bfgminer.sh
SH_LOG_COMPILER = /bin/sh
AM_TESTS_ENVIRONMENT = PATH='$(srcdir)':"$$PATH"; export PATH;
TESTS_ENVIRONMENT = $(AM_TESTS_ENVIRONMENT)
TEST_EXTENSIONS = .sh

.PHONY: update-version
update-version:
	( cd $(top_srcdir) && $(top_srcdir)/gen-version.sh ) >version.h.new
	cmp $(top_srcdir)/version.h version.h.new && rm version.h.new || mv version.h.new $(top_srcdir)/version.h
version.h: update-version
bfgminer_SOURCES += version.c version.h
BUILT_SOURCES = version.h

SUBDIRS += $(libbase58_SUBDIRS) $(libblkmaker_SUBDIRS)
EXTRA_bfgminer_DEPENDENCIES += $(libbase58_EXTRADEPS) $(libblkmaker_EXTRADEPS)
@BUNDLED_LIB_RULES@

bfgminer_SOURCES	+= logging.c

if HAVE_WINDOWS
bfgminer_SOURCES += winhacks.h
endif

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


if USE_OPENCL
bfgminer_SOURCES += driver-opencl.h driver-opencl.c
bfgminer_SOURCES += ocl.c ocl.h findnonce.c findnonce.h
bfgminer_SOURCES += adl.c adl.h adl_functions.h

kernelsdir = $(pkgdatadir)/opencl
dist_kernels_DATA =

dist_doc_DATA += README.GPU

if HAVE_SENSORS
bfgminer_LDADD += $(sensors_LIBS)
endif

endif


if USE_KECCAK
bfgminer_SOURCES += malgo/keccak.c

if USE_OPENCL
dist_kernels_DATA += $(top_srcdir)/opencl/keccak.cl
endif
endif

if USE_SHA256D
bfgminer_SOURCES += malgo/sha256d.c

if USE_OPENCL
dist_kernels_DATA += \
	$(top_srcdir)/opencl/diablo.cl  \
	$(top_srcdir)/opencl/diakgcn.cl  \
	$(top_srcdir)/opencl/phatk.cl  \
	$(top_srcdir)/opencl/poclbm.cl
endif

if USE_CPUMINING
bfgminer_SOURCES	+= \
		  sha256_generic.c sha256_via.c	\
		  sha256_cryptopp.c sha256_sse2_amd64.c		\
		  sha256_sse4_amd64.c 	\
		  sha256_altivec_4way.c

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
endif # USE_CPUMINING
endif # USE_SHA256D

if USE_SCRYPT
bfgminer_SOURCES += malgo/scrypt.c malgo/scrypt.h
dist_doc_DATA += README.scrypt

if USE_OPENCL
dist_kernels_DATA += \
	$(top_srcdir)/opencl/psw.cl  \
	$(top_srcdir)/opencl/scrypt.cl  \
	$(top_srcdir)/opencl/zuikkis.cl
endif
endif

if USE_CPUMINING
dist_doc_DATA += README.CPU
bfgminer_SOURCES += driver-cpu.h driver-cpu.c
endif # USE_CPUMINING

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

if NEED_BFG_WORK2D
bfgminer_SOURCES  += work2d.c work2d.h
endif

if HAS_FPGA
dist_doc_DATA += README.FPGA
endif

if HAS_ASIC
dist_doc_DATA += README.ASIC
endif

if USE_ALCHEMIST
bfgminer_SOURCES += driver-alchemist.c
endif

if USE_BITFORCE
bfgminer_SOURCES += driver-bitforce.c
endif

if USE_BITMAIN
bfgminer_SOURCES += driver-bitmain.c driver-bitmain.h
endif

if USE_BIGPIC
bfgminer_SOURCES += driver-bigpic.c driver-bigpic.h
endif

if USE_DRILLBIT
bfgminer_SOURCES += driver-drillbit.c
endif

if USE_TWINFURY
bfgminer_SOURCES += driver-twinfury.c driver-twinfury.h
endif

if USE_ICARUS
bfgminer_SOURCES += driver-icarus.c driver-icarus.h
bfgminer_SOURCES += driver-cairnsmore.c
bfgminer_SOURCES += driver-erupter.c
bfgminer_SOURCES += driver-antminer.c
endif

if USE_GC3355
bfgminer_SOURCES += gc3355.c gc3355.h
endif

if USE_GRIDSEED
bfgminer_SOURCES += driver-gridseed.c
endif

if USE_DUALMINER
bfgminer_SOURCES += driver-dualminer.c
endif

if USE_ZEUSMINER
bfgminer_SOURCES += driver-zeusminer.c
endif

if USE_AVALON
bfgminer_SOURCES += driver-avalon.c driver-avalon.h
endif

if USE_AVALONMM
bfgminer_SOURCES += driver-avalonmm.c
endif

if USE_KNC
bfgminer_SOURCES += driver-knc.c
endif

if USE_KNCASIC
bfgminer_SOURCES += driver-kncasic.c
endif

if USE_KLONDIKE
bfgminer_SOURCES += driver-klondike.c driver-klondike.h driver-hashbusteravalon.c
endif

if USE_MODMINER
bfgminer_SOURCES += driver-modminer.c
endif

if USE_TITAN
bfgminer_SOURCES += driver-titan.c titan-asic.c titan-asic.h
endif

if NEED_KNC_ASIC
bfgminer_SOURCES += knc-asic/knc-asic.c knc-asic/knc-spimux.c knc-asic/knc-transport-spimux.c knc-asic/include/knc-asic.h knc-asic/include/knc-transport.h
endif

if USE_X6500
bfgminer_SOURCES += driver-x6500.c jtag.c jtag.h
endif

if USE_ZTEX
bfgminer_SOURCES += driver-ztex.c libztex.c libztex.h
endif

if USE_BIFURY
bfgminer_SOURCES += driver-bifury.c
endif

if USE_BITFURY
bfgminer_SOURCES += driver-bitfury.c driver-bitfury.h libbitfury.c libbitfury.h

if USE_BFSB
bfgminer_SOURCES += driver-bfsb.c
endif

if USE_BFX
bfgminer_SOURCES += driver-bfx.c
endif

if USE_METABANK
bfgminer_SOURCES += driver-metabank.c tm_i2c.h tm_i2c.c
endif

if USE_LITTLEFURY
bfgminer_SOURCES += driver-littlefury.c
endif

if USE_NANOFURY
bfgminer_SOURCES += driver-nanofury.c mcp2210.c mcp2210.h
endif

if USE_HASHBUSTER
bfgminer_SOURCES += driver-hashbuster.c
endif

if USE_HASHBUSTERUSB
bfgminer_SOURCES += driver-hashbusterusb.c
endif

endif

if USE_COINTERRA
bfgminer_SOURCES += driver-cointerra.c driver-cointerra.h
endif

if USE_HASHFAST
bfgminer_SOURCES += driver-hashfast.c
endif

if USE_JINGTIAN
bfgminer_SOURCES += driver-aan.c driver-aan.h
bfgminer_SOURCES += driver-jingtian.c
endif

if USE_MINERGATE
bfgminer_SOURCES += driver-minergate.c
endif

if USE_MINION
bfgminer_SOURCES += driver-minion.c
endif

if USE_ROCKMINER
bfgminer_SOURCES += driver-rockminer.c
endif

if NEED_BFG_LOWL_FTDI
bfgminer_SOURCES += lowl-ftdi.c lowl-ftdi.h
endif

if NEED_BFG_LOWL_HID
bfgminer_SOURCES += lowl-hid.c lowl-hid.h
bfgminer_CPPFLAGS += $(hidapi_CFLAGS)
endif

if NEED_BFG_LOWL_MSWIN
bfgminer_SOURCES += lowl-mswin.c lowl-mswin.h
endif

if NEED_BFG_LOWL_PCI
bfgminer_SOURCES += lowl-pci.c lowl-pci.h

if USE_VFIO
sbin_SCRIPTS += setup-vfio
endif
endif

if NEED_BFG_LOWL_SPI
bfgminer_SOURCES += lowl-spi.h lowl-spi.c
endif

bin_PROGRAMS += bfgminer-rpc
bfgminer_rpc_SOURCES = api-example.c
bfgminer_rpc_LDADD = @WS2_LIBS@
