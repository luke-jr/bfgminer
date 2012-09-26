
ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST	= example.conf m4/gnulib-cache.m4 linux-usb-bfgminer \
		  api-example.php miner.php	\
		  API.class API.java api-example.c windows-build.txt \
		  bitstreams/* API-README FPGA-README SCRYPT-README

SUBDIRS		= lib ccan

INCLUDES	= $(PTHREAD_FLAGS) -fno-strict-aliasing $(USB_FLAGS)

bin_PROGRAMS	= bfgminer

bin_SCRIPTS	= *.cl

bfgminer_LDFLAGS	= $(PTHREAD_FLAGS)
bfgminer_LDADD	= $(DLOPEN_FLAGS) @LIBCURL_LIBS@ @JANSSON_LIBS@ @PTHREAD_LIBS@ \
		  @NCURSES_LIBS@ @PDCURSES_LIBS@ @WS2_LIBS@ \
		  @TIMER_LIBS@ \
		  @UDEV_LIBS@ @USB_LIBS@ \
		  @MATH_LIBS@ lib/libgnu.a ccan/libccan.a
bfgminer_CPPFLAGS = -I$(top_builddir)/lib -I$(top_srcdir)/lib

# common sources
bfgminer_SOURCES := miner.c

bfgminer_SOURCES	+= elist.h miner.h compat.h bench_block.h	\
		   util.c uthash.h logging.h			\
		   sha2.c sha2.h api.c

if NEED_LIBBLKMAKER
SUBDIRS           += libblkmaker
bfgminer_CPPFLAGS += -Ilibblkmaker
bfgminer_LDFLAGS  += -Llibblkmaker/.libs -Wl,-rpath,libblkmaker/.libs
bfgminer_LDADD    += -lblkmaker-0.1 -lblkmaker_jansson-0.1
endif

bfgminer_SOURCES	+= logging.c

# GPU sources, TODO: make them selectable
# the GPU portion extracted from original main.c
bfgminer_SOURCES += driver-opencl.h driver-opencl.c

# the original GPU related sources, unchanged
bfgminer_SOURCES += ocl.c ocl.h findnonce.c findnonce.h
bfgminer_SOURCES += adl.c adl.h adl_functions.h
bfgminer_SOURCES += *.cl

if HAS_SCRYPT
bfgminer_SOURCES += scrypt.c scrypt.h
endif

if HAS_CPUMINE
# original CPU related sources, unchanged
bfgminer_SOURCES	+= \
		  sha256_generic.c sha256_4way.c sha256_via.c	\
		  sha256_cryptopp.c sha256_sse2_amd64.c		\
		  sha256_sse4_amd64.c sha256_sse2_i386.c	\
		  sha256_altivec_4way.c

# the CPU portion extracted from original main.c
bfgminer_SOURCES += driver-cpu.h driver-cpu.c

if HAS_YASM
AM_CFLAGS	= -DHAS_YASM
if HAVE_x86_64
SUBDIRS		+= x86_64
bfgminer_LDADD	+= x86_64/libx8664.a
else # HAVE_x86_64
SUBDIRS		+= x86_32
bfgminer_LDADD	+= x86_32/libx8632.a
endif # HAVE_x86_64
endif # HAS_YASM
endif # HAS_CPUMINE

if NEED_FPGAUTILS
bfgminer_SOURCES += fpgautils.c fpgautils.h
endif

if NEED_DYNCLOCK
bfgminer_SOURCES += dynclock.c dynclock.h
endif

if HAS_BITFORCE
bfgminer_SOURCES += driver-bitforce.c

if HAVE_WINDOWS
else
bin_PROGRAMS += bitforce-firmware-flash
bitforce_firmware_flash_SOURCES = bitforce-firmware-flash.c
endif
endif

if HAS_ICARUS
bfgminer_SOURCES += driver-icarus.c icarus-common.h
bfgminer_SOURCES += driver-cairnsmore.c
endif

if HAS_MODMINER
bfgminer_SOURCES += driver-modminer.c
bitstreamsdir = $(bindir)/bitstreams
dist_bitstreams_DATA = bitstreams/*
endif

if HAS_ZTEX
bfgminer_SOURCES += driver-ztex.c libztex.c libztex.h
bitstreamsdir = $(bindir)/bitstreams
dist_bitstreams_DATA = bitstreams/*
endif
