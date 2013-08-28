AM_CPPFLAGS = -I$(top_srcdir)/libusb
LDADD = ../libusb/libusb-1.0.la
noinst_PROGRAMS = listdevs hotplugtest testlibusb1

if HAVE_SIGACTION
noinst_PROGRAMS += dpfp

if THREADS_POSIX
dpfp_threaded_CFLAGS = $(AM_CFLAGS)
noinst_PROGRAMS += dpfp_threaded
endif

sam3u_benchmark_SOURCES = sam3u_benchmark.c
noinst_PROGRAMS += sam3u_benchmark
endif
