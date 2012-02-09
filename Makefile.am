
ACLOCAL_AMFLAGS = -I m4

if WANT_JANSSON
JANSSON_INCLUDES= -I$(top_srcdir)/compat/jansson
else
JANSSON_INCLUDES=
endif

EXTRA_DIST	= example.conf m4/gnulib-cache.m4 linux-usb-cgminer \
		  ADL_SDK/readme.txt api-example.php miner.php	\
		  API.class API.java api-example.c

SUBDIRS		= lib compat ccan

INCLUDES	= $(PTHREAD_FLAGS) -fno-strict-aliasing $(JANSSON_INCLUDES)

bin_PROGRAMS	= cgminer

bin_SCRIPTS	= *.cl

cgminer_LDFLAGS	= $(PTHREAD_FLAGS)
cgminer_LDADD	= $(DLOPEN_FLAGS) @LIBCURL_LIBS@ @JANSSON_LIBS@ @PTHREAD_LIBS@ \
		  @OPENCL_LIBS@ @NCURSES_LIBS@ @PDCURSES_LIBS@ @WS2_LIBS@ \
		  @MATH_LIBS@ lib/libgnu.a ccan/libccan.a
cgminer_CPPFLAGS = -I$(top_builddir)/lib -I$(top_srcdir)/lib @OPENCL_FLAGS@

# common sources

# this is the original monolithic main.c, kept as reference
# cgminer_SOURCES	:= main.c

# this is the original main.c without extracted CPU and GPU code
cgminer_SOURCES := cgminer.c

cgminer_SOURCES	+= elist.h miner.h compat.h bench_block.h	\
		   util.c uthash.h				\
		   sha2.c sha2.h api.c

# GPU sources, TODO: make them selectable
# the GPU portion extracted from original main.c
cgminer_SOURCES += device-gpu.h device-gpu.c

# the original GPU related sources, unchanged
cgminer_SOURCES += ocl.c ocl.h findnonce.c findnonce.h
cgminer_SOURCES += adl.c adl.h adl_functions.h
cgminer_SOURCES += *.cl

if HAS_CPUMINE
# original CPU related sources, unchanged
cgminer_SOURCES	+= \
		  sha256_generic.c sha256_4way.c sha256_via.c	\
		  sha256_cryptopp.c sha256_sse2_amd64.c		\
		  sha256_sse4_amd64.c sha256_sse2_i386.c	\
		  sha256_altivec_4way.c

# the CPU portion extracted from original main.c
cgminer_SOURCES += device-cpu.h device-cpu.c

if HAS_YASM
AM_CFLAGS	= -DHAS_YASM
if HAVE_x86_64
SUBDIRS		+= x86_64
cgminer_LDADD	+= x86_64/libx8664.a
else # HAVE_x86_64
SUBDIRS		+= x86_32
cgminer_LDADD	+= x86_32/libx8632.a
endif # HAVE_x86_64
endif # HAS_YASM
endif # HAS_CPUMINE

if HAS_BITFORCE
cgminer_SOURCES += bitforce.c
endif

