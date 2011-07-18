
ACLOCAL_AMFLAGS = -I m4

if WANT_JANSSON
JANSSON_INCLUDES= -I$(top_srcdir)/compat/jansson
else
JANSSON_INCLUDES=
endif

EXTRA_DIST	= example-cfg.json m4/gnulib-cache.m4

SUBDIRS		= lib compat ccan

INCLUDES	= $(PTHREAD_FLAGS) -fno-strict-aliasing $(JANSSON_INCLUDES)

bin_PROGRAMS	= cgminer

bin_SCRIPTS	= phatk110714.cl poclbm110717.cl

cgminer_SOURCES	= elist.h miner.h compat.h			\
		  main.c util.c					\
		  ocl.c ocl.h findnonce.c findnonce.h 		\
		  sha256_generic.c sha256_4way.c sha256_via.c	\
		  sha256_cryptopp.c sha256_sse2_amd64.c		\
		  sha256_sse4_amd64.c \
		  phatk110714.cl poclbm110717.cl

cgminer_LDFLAGS	= $(PTHREAD_FLAGS)
cgminer_LDADD	= @LIBCURL@ @JANSSON_LIBS@ @PTHREAD_LIBS@ @OPENCL_LIBS@ @NCURSES_LIBS@ @PDCURSES_LIBS@ lib/libgnu.a ccan/libccan.a
cgminer_CPPFLAGS = @LIBCURL_CPPFLAGS@ -I$(top_builddir)/lib -I$(top_srcdir)/lib

if HAVE_x86_64
if HAS_YASM
SUBDIRS		+= x86_64
cgminer_LDADD	+= x86_64/libx8664.a
AM_CFLAGS	= -DHAS_YASM
endif
endif
