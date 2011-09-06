
ACLOCAL_AMFLAGS = -I m4

if WANT_JANSSON
JANSSON_INCLUDES= -I$(top_srcdir)/compat/jansson
else
JANSSON_INCLUDES=
endif

EXTRA_DIST	= example-cfg.json m4/gnulib-cache.m4 linux-usb-cgminer \
		  ADL_SDK/readme.txt

SUBDIRS		= lib compat ccan

INCLUDES	= $(PTHREAD_FLAGS) -fno-strict-aliasing $(JANSSON_INCLUDES)

bin_PROGRAMS	= cgminer

bin_SCRIPTS	= phatk110817.cl poclbm110817.cl

cgminer_SOURCES	= elist.h miner.h compat.h bench_block.h	\
		  main.c util.c uthash.h			\
		  ocl.c ocl.h findnonce.c findnonce.h 		\
		  sha256_generic.c sha256_4way.c sha256_via.c	\
		  sha256_cryptopp.c sha256_sse2_amd64.c		\
		  sha256_sse4_amd64.c sha256_sse2_i386.c	\
		  adl.c	adl.h adl_functions.h			\
		  phatk110817.cl poclbm110817.cl

cgminer_LDFLAGS	= $(PTHREAD_FLAGS)
cgminer_LDADD	= @LIBCURL_LIBS@ @JANSSON_LIBS@ @PTHREAD_LIBS@ @OPENCL_LIBS@ @NCURSES_LIBS@ @PDCURSES_LIBS@ @WS2_LIBS@ lib/libgnu.a ccan/libccan.a
cgminer_CPPFLAGS = -I$(top_builddir)/lib -I$(top_srcdir)/lib

if HAVE_x86_64
if HAS_YASM
SUBDIRS		+= x86_64
cgminer_LDADD	+= x86_64/libx8664.a
AM_CFLAGS	= -DHAS_YASM
endif
else
if HAS_YASM
SUBDIRS		+= x86_32
cgminer_LDADD	+= x86_32/libx8632.a
AM_CFLAGS	= -DHAS_YASM
endif
endif
