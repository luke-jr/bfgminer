dnl * Copyright 2011-2014 Luke Dashjr
dnl * Copyright 2011-2013 Con Kolivas
dnl * Copyright 2010-2011 Jeff Garzik
dnl * Copyright 2012 Xiangfu
dnl * Copyright 2011 Rusty Russell
dnl * Copyright 2011 Mark Crichton
dnl * Copyright 2013-2014 Nate Woolls
dnl *
dnl * This program is free software; you can redistribute it and/or modify it
dnl * under the terms of the GNU General Public License as published by the Free
dnl * Software Foundation; either version 3 of the License, or (at your option)
dnl * any later version.  See COPYING for more details.

##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_maj], [5])
m4_define([v_min], [3])
m4_define([v_mic], [0])
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_ver], [v_maj.v_min.v_mic])
m4_define([lt_rev], m4_eval(v_maj + v_min))
m4_define([lt_cur], v_mic)
m4_define([lt_age], v_min)
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##

AC_INIT([bfgminer], [v_ver], [luke-jr+bfgminer@utopios.org])

AC_PREREQ([2.59c])
AC_CANONICAL_SYSTEM
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([miner.c])
AC_CONFIG_HEADERS([config.h])

AH_BOTTOM([
#ifdef WIN32
#include "winhacks.h"
#endif
])

AM_INIT_AUTOMAKE([foreign subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_USE_SYSTEM_EXTENSIONS

##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_ifdef([v_rev], , [m4_define([v_rev], [0])])
m4_ifdef([v_rel], , [m4_define([v_rel], [])])
AC_DEFINE_UNQUOTED(CGMINER_MAJOR_VERSION, [v_maj], [Major version])
AC_DEFINE_UNQUOTED(CGMINER_MINOR_VERSION, [v_min], [Minor version])
AC_DEFINE_UNQUOTED(CGMINER_MINOR_SUBVERSION, [v_mic], [Micro version])
version_info="lt_rev:lt_cur:lt_age"
release_info="v_rel"
AC_SUBST(version_info)
AC_SUBST(release_info)
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
VMAJ=v_maj
AC_SUBST(VMAJ)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

dnl Make sure anyone changing configure.ac/Makefile.am has a clue
AM_MAINTAINER_MODE([enable])

dnl Checks for programs
AC_PROG_CC_C99
gl_EARLY
AC_PROG_GCC_TRADITIONAL
AM_PROG_CC_C_O
AC_PROG_RANLIB
AC_PROG_CPP

gl_INIT

AC_SYS_LARGEFILE

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(syslog.h)
AC_CHECK_HEADERS([sys/epoll.h])
AC_CHECK_HEADERS([sys/mman.h])
AC_CHECK_HEADERS([sys/prctl.h])
AC_CHECK_HEADERS([sys/file.h])
AC_CHECK_HEADERS([sys/termios.h])
AC_CHECK_HEADERS([linux/spi/spidev.h])

AC_CHECK_MEMBER([struct i2c_msg.buf],[
	true
],[
	dnl Note the member is different here to avoid caching screwing things up
	AC_CHECK_MEMBER([struct i2c_msg.len],[
		AC_DEFINE([NEED_LINUX_I2C_H],[1],[Defined if linux/i2c.h is needed to supplement linux/i2c-dev.h])
	],[
		true
	],[
		AC_INCLUDES_DEFAULT
		#include <linux/i2c.h>
		#include <linux/i2c-dev.h>
	])
],[
	AC_INCLUDES_DEFAULT
	#include <linux/i2c-dev.h>
])

# Setuid
AC_CHECK_HEADERS([pwd.h])

# Check for chroot support
AC_CHECK_FUNCS([chroot])

AC_CHECK_FUNCS([sleep])

AC_FUNC_ALLOCA

lowllist=
driverlist=
algolist=
optlist=

has_fpga=no
has_asic=no
need_binloader=no
need_dynclock=no
need_gc3355=no
need_linux_i2c_dev=no
need_lowl_vcom=no
need_lowlevel=no
need_lowl_ftdi=no
need_lowl_hid=no
need_lowl_mswin=no
need_lowl_pci=no
need_lowl_spi=no
need_lowl_usb=no
need_knc_asic=no
need_work2d=no
have_cygwin=false
have_win32=false
have_macho=false
use_udevrules=true
have_udevrules=false
AUTOSCAN_CPPFLAGS=""
AUTOSCAN_LIBS=""
DLOPEN_FLAGS="-ldl"
WS2_LIBS=""
MM_LIBS=""
MATH_LIBS="-lm"
RT_LIBS=""

case $target in
  amd64-* | x86_64-*)
    have_x86_32=false
    have_x86_64=true
    bitness="64"
    ;;
  i386-* | i486-* | i586-* | i686-* | x86-*)
    have_x86_32=true
    have_x86_64=false
    bitness="32"
    ;;
  *)
    have_x86_32=false
    have_x86_64=false
    ;;
esac

case $target in
  *-*-mingw*)
    have_win32=true
    use_udevrules=false
    DLOPEN_FLAGS=""
    WS2_LIBS="-lws2_32"
    MM_LIBS="-lwinmm"
    AC_DEFINE([_WIN32_WINNT], [0x0501], "WinNT version for XP+ support")
    AC_DEFINE([FD_SETSIZE], [4096], [Maximum sockets before fd_set overflows])
    ;;
  *-*-cygwin*)
	have_cygwin=true
    use_udevrules=false
	;;
  powerpc-*-darwin*)
    CFLAGS="$CFLAGS -faltivec"
		have_macho=true
    use_udevrules=false
    ;;
	*-*-darwin*)
		LDFLAGS="$LDFLAGS -framework IOKit -framework CoreFoundation"
		have_macho=true
    use_udevrules=false
		;;
esac


m4_define([BFG_INCLUDE],
	if test "x$2" = "x"; then
		$1=''
	else
		$1="[#]include <$2>"
	fi
)

m4_define([BFG_PREPROC_IFELSE],
	BFG_INCLUDE([headerinclude], $2)
	AC_COMPILE_IFELSE([
		AC_LANG_PROGRAM([
			${headerinclude}
		], [
			#if !( $1 )
			#error "$1 false in preprocessor"
			#endif
		])
	],[$3],[$4])
)

m4_define([BFG_FIND_INCLUDE_PATH],[
	m4_pushdef([_rel_path],[$1])
	m4_pushdef([_header_file],[$2])
	m4_pushdef([_iffound],[$3])
	m4_pushdef([_ifnotfound],[$4])
	m4_pushdef([_includes],[$5])
	m4_pushdef([_result_var],[inclpath_[]patsubst(_header_file,[[./]],[_])])
	AC_CHECK_HEADER([_rel_path/_header_file],[
		AC_MSG_CHECKING([_header_file path])
		_result_var=`echo '[#]include <'"_rel_path"'/_header_file>' | ${CPP} -M - 2>/dev/null | sed [-E -e ':a' -e '/\\$/!b b' -e N -e 's/\\\n/ /' -e 't a' -e ':b' -e 's/^[^:]*:[[:space:]]*(([^[:space:]\]|\\.)*[[:space:]])*(([^[:space:]\]|\\.)*)]patsubst([_header_file],[\.],[\\.])[([[:space:]].*)?$/\3/' -e 't' -e d]`
		if test "x$_result_var" = "x"; then
			AC_MSG_RESULT([failed])
			AC_MSG_ERROR([Couldn't determine include path for _header_file])
		else
			AC_MSG_RESULT([$_result_var])
			_iffound
		fi
	],[
		true
		_ifnotfound
	],[
		_includes
	])
	m4_popdef([_rel_path],[_header_file],[_iffound],[_ifnotfound],[_result_var])
])

bfg_enableaction() {
	_var=$1
	eval "_missingdeps=\"\$${_var}_missingdeps\""
	if test -n "$_missingdeps"; then
		_enableaction=
		for _missingdep in $_missingdeps; do
			eval "_howto=\"\$howto_${_missingdep}\""
			if test "x$_howto" = "x"; then
				eval "_label=\"\${label_${_missingdep}-${_missingdep}}\""
				_howto="enable $_label"
			fi
			if test "$_howto" != "x"; then
				_enableaction="$_enableaction; $_howto"
			fi
		done
		_enableaction="${_enableaction:2}"
	else
		eval "_enableaction=\"\$${_var}_enableaction\""
	fi
	echo "$_enableaction"
}

dnl _BFG_AUTOITEM(list,driver,DriverName,dependency list,default:no/auto,CodeIfEnabled)
m4_define([_BFG_AUTOITEM],[
	m4_pushdef([_list],[$1])
	m4_pushdef([_this_drv],m4_default([$2],m4_tolower([$3])))
	m4_pushdef([_this_drvuc],m4_toupper(_this_drv))
	m4_pushdef([_this_USE],USE_[]_this_drvuc)
	m4_pushdef([_this_name],[$3])
	m4_pushdef([_deplist],[$4])
	m4_pushdef([_this_default],[$5])
	m4_pushdef([_this_enablecode],[$6])
	m4_case(_list,
		[algo],[algolist="$algolist []_this_name/[]_this_USE"],
		[driver],[driverlist="$driverlist []_this_drv/[]_this_USE"],
		[]
	)
	label_[]_this_drvuc="[]_this_name"
	AC_ARG_ENABLE([]_this_drv,
		m4_bmatch(_this_default,[.*no$],
			AC_HELP_STRING([--enable-[]_this_drv],[Compile support for ]_this_name[ (default disabled)]),
			AC_HELP_STRING([--disable-[]_this_drv],[Compile support for ]_this_name[ (default enabled)])),
		[[]_this_USE=$enableval],
		[[]_this_USE=[]_this_default]
		)
	[]_this_drv[]_opt=
	if test "x$[]_this_USE" = "xno"; then
		true
	else
		if test "x$[]_this_USE" = xauto; then
			_wasauto=yes
		else
			_wasauto=no
		fi
		if test "x$[]_this_USE" != xyes; then
			if test "x$[]_this_USE" != xauto; then
				[]_this_drv[]_opt="$[]_this_USE"
			fi
			[]_this_USE=yes
		fi
		for __drvdep in []m4_toupper(_deplist); do
			eval "_drvdep=\"\$USE_$__drvdep\""
			if test "x$_drvdep" != "xyes"; then
				[]_this_USE[]_missingdeps="$[]_this_USE[]_missingdeps $__drvdep"
				[]_this_USE=no
			fi
		done
		if test "x$[]_this_USE[]$_wasauto" = xnono; then
			_enableaction=`bfg_enableaction []_this_USE`
			AC_MSG_ERROR([To enable []_this_name: $_enableaction])
		fi
	fi
	if test "x$[]_this_USE" = "xyes"; then
		AC_DEFINE([]_this_USE,[1],[Defined to 1 if []_this_name support is wanted])
		[]_this_enablecode
	fi
	AM_CONDITIONAL([]_this_USE,[test x$[]_this_USE = xyes])
	m4_popdef([_this_drv],[_this_drvuc],[_this_USE],[_this_name],[_deplist],[_this_default],[_this_enablecode])
])

dnl BFG_ALGO(AlgoName,default:no/yes,CodeIfEnabled)
m4_define([BFG_ALGO],[
	_BFG_AUTOITEM([algo],,[$1],,[$2],[$3])
])

dnl BFG_DRIVER(driver,DriverName,dependency list,default:no/auto,CodeIfEnabled)
m4_define([BFG_DRIVER],[
	_BFG_AUTOITEM([driver],[$1],[$2],[$3],$dd[$4],[$5])
])


AC_CHECK_DECL([HASH_ITER],[
	AC_CHECK_DECL([DL_CONCAT],[
		true
	],[
		AC_MSG_ERROR([Could not find DL_FOREACH_SAFE - install uthash-dev 1.9.4+])
	],[
		#include <utlist.h>
	])
],[
	AC_MSG_ERROR([Could not find HASH_ITER - please install uthash-dev 1.9.4+])
],[
	#include <uthash.h>
])


ddyes=yes
ddauto=auto
ddno=no
AC_ARG_ENABLE([other-drivers],
	[AC_HELP_STRING([--disable-other-drivers],[Build without drivers by default unless explicitly enabled])],
	[
		if test x$enableval = xyes; then
			ddno=yes
		else
			ddyes=no
			ddauto=no
		fi
	]
)


broad_udevrules=false
AC_ARG_ENABLE([broad-udevrules],
	[AC_HELP_STRING([--enable-broad-udevrules],[Include udev rules for ambiguous devices which may not be miners])],
	[
		if test "x$enableval" = "xyes"; then
			broad_udevrules=true
		fi
	]
)

use_udevrules_group=true
udevrules_group="video"
AC_ARG_WITH([udevrules-group],
	[AC_HELP_STRING([--with-udevrules-group=groupname],[Configure mining devices to be owned by a specific group (default `video')])],
	[
		if test "x$withval" = "xno"; then
			use_udevrules_group=false
		else
			udevrules_group="$withval"
		fi
	]
)
AM_CONDITIONAL([USE_UDEVRULES_GROUP], [$use_udevrules_group])
AC_SUBST([UDEVRULES_GROUP], [$udevrules_group])


BFG_ALGO(Keccak,no)
BFG_ALGO(SHA256d,yes)
BFG_ALGO(scrypt,no)

lowl_pci=no
if test "x$ac_cv_header_sys_mman_h" = "xyes"; then
	AC_ARG_WITH([uio],
		[AC_HELP_STRING([--without-uio],[Compile support for PCI devices via Linux UIO interface (default enabled)])],
		[uio=$withval],
		[uio=yes])
	AC_ARG_WITH([vfio],
		[AC_HELP_STRING([--without-vfio],[Compile support for PCI devices via Linux VFIO interface (default enabled)])],
		[vfio=$withval],
		[vfio=auto])
	if test "x$vfio" != "xno"; then
		AC_CHECK_HEADER([linux/vfio.h],[
			vfio=yes
		],[
			if test "x$vfio" = "xyes"; then
				AC_MSG_ERROR([Unable to find linux/vfio.h])
			elif test "x$uio" = "xyes"; then
				AC_MSG_WARN([linux/vfio.h not found; PCI device support will require UIO (and root access)])
			else
				AC_MSG_WARN([linux/vfio.h not found; PCI device support will not be available])
			fi
			vfio=no
		])
	fi
	if test "x$vfio$uio" != xnono; then
		lowl_pci=yes
	fi
fi


BFG_DRIVER(cpumining,CPU mining,,no,[
	driverlist="$driverlist cpu:asm/has_yasm"
	driverlist="$driverlist cpu:sse2/have_sse2"
])
driverlist=`echo "${driverlist}" | sed -e 's/ cpumining/ cpu/'`

BFG_DRIVER(,OpenCL,,no)

m4_define([BFG_PTHREAD_FLAG_CHECK],
	AC_MSG_CHECKING([for $1])
	for cflag in ' -pthread' ''; do
		for lib in ' -lpthread' ' -lwinpthread' ''; do
			CFLAGS="${save_CFLAGS}${cflag}"
			LIBS="${save_LIBS}${lib}"
			AC_LINK_IFELSE([
				AC_LANG_PROGRAM([
					#include <pthread.h>
				], [
					void *f = $1;
				])
			], [
				found_pthread=true
				PTHREAD_FLAGS="${cflag}"
				PTHREAD_LIBS="${lib}"
				if test "x${cflag}${lib}" = "x"; then
					AC_MSG_RESULT([yes])
				else
					AC_MSG_RESULT([with${cflag}${lib}])
				fi
				$2
				break 2
			])
		done
	done
	if test "x${found_pthread}" = "xfalse"; then
		AC_MSG_RESULT([no])
	fi
)

save_CFLAGS="${CFLAGS}"
save_LIBS="${LIBS}"
found_pthread=false
BFG_PTHREAD_FLAG_CHECK([pthread_cancel],[
	AC_DEFINE([HAVE_PTHREAD_CANCEL], [1], [Define if you have a native pthread_cancel])
])
if test "x${found_pthread}" = "xfalse"; then
	BFG_PTHREAD_FLAG_CHECK([pthread_create])
	if test "x${found_pthread}" = "xfalse"; then
		AC_MSG_ERROR([Could not find pthread library - please install libpthread])
	fi
fi
# check for nanosleep here, since it is provided by winpthread
AC_CHECK_FUNCS([nanosleep])
CFLAGS="${save_CFLAGS}"
LIBS="${save_LIBS}"

PKG_CHECK_MODULES([JANSSON],[jansson],[
	true
],[
	AC_MSG_CHECKING([for jansson in system-default locations])
	LIBS="$LIBS -ljansson"
	AC_TRY_LINK([
		#include <jansson.h>
	],[
		json_object();
	],[
		AC_MSG_RESULT([found])
		JANSSON_LIBS=-ljansson
	],[
		AC_MSG_RESULT([not found])
		AC_MSG_ERROR([Could not find jansson library])
	])
	LIBS="${save_LIBS}"
])
AC_SUBST(JANSSON_CFLAGS)
AC_SUBST(JANSSON_LIBS)

if test "x$USE_OPENCL" = xyes; then
	adl="yes"
	
	if test "x$USE_KECCAK" = xyes; then
		AC_DEFINE([USE_OPENCL_FULLHEADER],[1],[Defined to 1 if OpenCL fullheader kernel interface is wanted])
	fi
	
driverlist="$driverlist opencl:sensors/with_sensors"
AC_ARG_WITH([sensors],
	[AC_HELP_STRING([--without-sensors],[Build with libsensors monitoring (default enabled)])],
	[true],[with_sensors=auto])
if test "x$USE_OPENCL" != xyes; then
	with_sensors=no
fi
if test "x$with_sensors" != xno; then
	AC_MSG_CHECKING([for libsensors])
	save_LIBS="${LIBS}"
	LIBS="$LIBS -lsensors"
	AC_LINK_IFELSE([AC_LANG_PROGRAM([
		#include <stddef.h>
		#include <sensors/sensors.h>
	],[
		const sensors_chip_name *cn;
		cn = sensors_get_detected_chips(NULL, NULL);
	])],[
		with_sensors=yes
		sensors_LIBS="-lsensors"
		AC_DEFINE([HAVE_SENSORS], [1], [Defined if libsensors was found])
		AC_MSG_RESULT([yes])
	],[
		with_sensors=no
		AC_MSG_RESULT([no])
		if ! $have_win32; then
			with_sensors_enableaction="install libsensors"
		fi
	])
	LIBS="$save_LIBS"
fi
AC_SUBST(sensors_LIBS)

driverlist="$driverlist opencl:adl/adl"
AC_ARG_ENABLE([adl],
	[AC_HELP_STRING([--disable-adl],[Build without ADL monitoring (default enabled)])],
	[adl=$enableval]
	)

	if test x$adl = xyes
		then
			AC_DEFINE([HAVE_ADL], [1], [Defined if ADL headers were found])
		fi
else
	adl="no"
fi

BFG_DRIVER(,BitForce,SHA256d,auto,[
	driverlist="$driverlist bitforce:pci/need_lowl_pci"
	if test "x$lowl_pci" = "xyes"; then
		need_lowl_pci=yes
	fi
	need_lowl_vcom=yes
	driverlist="$driverlist bitforce:mswin/need_lowl_mswin"
	if test x$have_win32 = xtrue; then
		need_lowl_mswin=yes
	fi
	has_fpga=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(,Icarus,SHA256d,auto,[
	need_dynclock=yes
	need_lowl_vcom=yes
	has_fpga=yes
	has_asic=yes
	have_udevrules=true
])
driverlist="$driverlist cairnsmore/USE_ICARUS erupter/USE_ICARUS antminer/USE_ICARUS"

BFG_DRIVER(,DualMiner,Icarus,auto,[
	need_gc3355=yes
	$broad_udevrules && have_udevrules=true
])

BFG_DRIVER(,ZeusMiner,scrypt Icarus,auto,[
	need_lowl_vcom=yes
	has_asic=yes
	$broad_udevrules && have_udevrules=true
])

BFG_DRIVER(,GridSeed,scrypt,auto,[
	need_gc3355=yes
	need_lowl_vcom=yes
	has_asic=yes
	$broad_udevrules && have_udevrules=true
])

BFG_DRIVER(,Avalon,SHA256d,auto,[
	need_lowl_vcom=yes
	has_asic=yes
	$broad_udevrules && have_udevrules=true
])

BFG_DRIVER(avalonmm,Avalon2/3,SHA256d,auto,[
	need_lowl_vcom=yes
	need_work2d=yes
	has_asic=yes
	$broad_udevrules && have_udevrules=true
])

BFG_DRIVER(,KnC,SHA256d,no,[
	need_linux_i2c_dev=yes
	need_lowl_spi=yes
])

BFG_DRIVER(kncasic,KnC gen 2,SHA256d,no,[
	kncasic_controller="$kncasic_opt"
	if test "x$kncasic_opt" = "x"; then
		kncasic_controller=BBB
	fi
	need_knc_asic=yes
	AC_DEFINE_UNQUOTED([CONTROLLER_BOARD_$kncasic_controller],[1])
])

AC_ARG_WITH([libmicrohttpd],
	[AC_HELP_STRING([--without-libmicrohttpd],[Compile support for libmicrohttpd getwork server (default enabled)])],
	[httpsrv=$withval],
	[httpsrv=$ddauto]
)
if test "x$httpsrv" != "xno"; then
	PKG_CHECK_MODULES([libmicrohttpd],[libmicrohttpd >= 0.9.5],[
		AC_DEFINE([USE_LIBMICROHTTPD],[1],[Defined to 1 if libmicrohttpd support is wanted])
		httpsrv=yes
	],[
		httpsrv=no
		httpsrv_enableaction="install libmicrohttpd 0.9.5+"
		need_bfg_driver_proxy_enableaction="install libmicrohttpd 0.9.5+"
		if test "x$httpsrv" = "xyes"; then
			AC_MSG_ERROR([Unable to find libmicrohttpd 0.9.5+])
		else
			AC_MSG_WARN([libmicrohttpd 0.9.5+ not found; getwork proxy will be unavailable])
		fi
	])
fi
AM_CONDITIONAL([USE_LIBMICROHTTPD], [test x$httpsrv = xyes])

AC_ARG_WITH([libevent],
	[AC_HELP_STRING([--without-libevent],[Compile support for libevent stratum server (default enabled)])],
	[libevent=$withval],
	[libevent=$ddauto]
)
if test "x$libevent" != "xno"; then
	PKG_CHECK_MODULES([libevent],[libevent >= 2.0.3],[
		AC_DEFINE([USE_LIBEVENT],[1],[Defined to 1 if libevent support is wanted])
		libevent=yes
	],[
		libevent=no
		libevent_enableaction="install libevent 2.0.3+"
		if test -n "$need_bfg_driver_proxy_enableaction"; then
			need_bfg_driver_proxy_enableaction="${need_bfg_driver_proxy_enableaction} (getwork) or libevent 2.0.3+ (stratum)"
		else
			need_bfg_driver_proxy_enableaction="install libevent 2.0.3+"
		fi
		if test "x$libevent" = "xyes"; then
			AC_MSG_ERROR([Unable to find libevent 2.0.3+])
		else
			AC_MSG_WARN([libevent 2.0.3+ not found; stratum proxy will be unavailable])
		fi
	])
	if test "x$libevent" = "xyes"; then
		need_work2d=yes
	fi
fi
AM_CONDITIONAL([USE_LIBEVENT], [test x$libevent = xyes])

driverlist="$driverlist proxy/need_bfg_driver_proxy"
if test x$libevent$httpsrv = xnono; then
	need_bfg_driver_proxy=no
else
	need_bfg_driver_proxy=yes
	driverlist="$driverlist proxy:getwork/httpsrv proxy:stratum/libevent"
fi
AM_CONDITIONAL([NEED_BFG_DRIVER_PROXY], [test x$libevent$httpsrv != xnono])

BFG_DRIVER(,ModMiner,SHA256d,auto,[
	need_dynclock=yes
	need_lowl_vcom=yes
	need_binloader=yes
	has_fpga=yes
	have_udevrules=true
])

PKG_PROG_PKG_CONFIG()

label_LIBUSB=libusb
USE_LIBUSB=no
libusb_include_path=""
AC_ARG_WITH([libusb],
	[AC_HELP_STRING([--without-libusb],[Compile using libusb (default enabled)])],
	[want_libusb=$withval],
	[want_libusb=auto]
	)
if test "x$want_libusb" != "xno"; then
	howto_LIBUSB="install libusb 1.0+"
PKG_CHECK_MODULES([LIBUSB], [libusb-1.0],[
	USE_LIBUSB=yes
],[
	for usb_lib in usb-1.0 usb; do
		AC_CHECK_LIB($usb_lib, libusb_init, [
			USE_LIBUSB=yes
			break
		])
	done
	if test "x$USE_LIBUSB" = xyes; then
			AC_CHECK_DECL([libusb_init],[
				LIBUSB_LIBS="-l$usb_lib"
			],[
				BFG_FIND_INCLUDE_PATH([libusb-1.0],[libusb.h],[
					LIBUSB_LIBS="-l$usb_lib"
					LIBUSB_CFLAGS="-I${inclpath_libusb_h}"
				],[
					USE_LIBUSB=no
				])
			],[#include <libusb.h>])
	fi
])
fi

BFG_DRIVER(,CoinTerra,SHA256d libusb,auto,[
	need_lowl_usb=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(,Klondike,SHA256d libusb,auto,[
	need_lowl_usb=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(titan,KnC Titan,scrypt,no,[
	if test "x$titan_opt" = "x"; then
		if test "x$USE_KNCASIC" = "xyes"; then
			titan_controller="$kncasic_controller"
		else
			titan_controller=RPI
		fi
	else
		titan_controller=$titan_opt
		if test "x$USE_KNCASIC" = "xyes"; then
			if test "x$kncasic_controller" != "$titan_controller"; then
				AC_MSG_ERROR([Can only build kncasic and titan drivers together for the same controller (kncasic=$kncasic_controller vs titan=$titan_controller)])
			fi
		fi
	fi
	need_knc_asic=yes
	AC_DEFINE_UNQUOTED([CONTROLLER_BOARD_$titan_controller],[1])
	AH_TEMPLATE([CONTROLLER_BOARD_BACKPLANE])
	AH_TEMPLATE([CONTROLLER_BOARD_BBB])
	AH_TEMPLATE([CONTROLLER_BOARD_RPI])
])

BFG_DRIVER(,X6500,SHA256d libusb,auto,[
	need_dynclock=yes
	need_lowl_ftdi=yes
	need_binloader=yes
	has_fpga=yes
	have_udevrules=true
])

BFG_DRIVER(,ZTEX,SHA256d libusb,auto,[
	need_dynclock=yes
	need_lowl_usb=yes
	need_binloader=yes
	has_fpga=yes
	have_udevrules=true
])

BFG_DRIVER(bifury,Bi*Fury,SHA256d,auto,[
	need_lowl_vcom=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(,Bitfury,SHA256d,auto,[
	need_lowl_spi=yes
])

BFG_DRIVER(,BFSB,SHA256d Bitfury,no)

BFG_DRIVER(bigpic,Big Picture Mining USB,SHA256d Bitfury,auto,[
	need_lowl_vcom=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(bfx,BFx2,SHA256d libusb Bitfury,auto,[
	need_lowl_ftdi=yes
	has_asic=yes
	$broad_udevrules && have_udevrules=true
])

BFG_DRIVER(,DrillBit,SHA256d Bitfury,auto,[
	need_lowl_vcom=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(,Twinfury,SHA256d Bitfury,auto,[
    need_lowl_vcom=yes
    has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(,LittleFury,SHA256d Bitfury,auto,[
	need_lowl_vcom=yes
	has_asic=yes
	have_udevrules=true
])


label_HIDAPI=hidapi
howto_HIDAPI="install hidapi"
USE_HIDAPI=no
for _hidapi_lib in hidapi hidapi-hidraw hidapi-libusb; do
	PKG_CHECK_MODULES([hidapi],[$_hidapi_lib],[
		USE_HIDAPI=yes
		break
	],[
		true
	])
done


BFG_DRIVER(,NanoFury,SHA256d hidapi Bitfury,auto,[
	need_lowl_hid=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(hashbuster,HashBuster Nano,SHA256d hidapi Bitfury,auto,[
	need_lowl_hid=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(hashbusterusb,HashBuster Micro,SHA256d libusb Bitfury,auto,[
	need_lowl_usb=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(,HashFast,SHA256d,auto,[
	need_lowl_vcom=yes
	has_asic=yes
	have_udevrules=true
])

BFG_DRIVER(,JingTian,SHA256d,no,[
	need_lowl_spi=yes
	has_asic=yes
])

BFG_DRIVER(,Metabank,SHA256d Bitfury,no)

BFG_DRIVER(minergate,Spondoolies minergate interface,SHA256d,no,[
	need_lowlevel=yes
	has_asic=yes
])

BFG_DRIVER(,Minion,SHA256d,no,[
	need_lowl_spi=yes
	has_asic=yes
])

BFG_DRIVER(,RockMiner,SHA256d,auto,[
	need_lowl_vcom=yes
	has_asic=yes
	have_udevrules=true
])


if test "x$need_lowl_vcom" != "xno"; then
	# Lowlevel VCOM doesn't need libusb, but it can take advantage of it to reattach drivers
	if test "x$USE_LIBUSB" != xno; then
		need_lowl_usb=yes
	elif test "x$USE_LIBUSB$want_libusb" = xnoyes; then
		AC_MSG_ERROR([Could not find libusb, which you specifically requested])
	fi
fi

lowllist="$lowllist ftdi/need_lowl_ftdi"
if test x$need_lowl_ftdi = xyes; then
	AC_DEFINE([NEED_BFG_LOWL_FTDI], [1], [Defined to 1 if lowlevel ftdi drivers are being used])
	need_lowl_usb=yes
fi

lowllist="$lowllist spi/need_lowl_spi"
if test x$need_lowl_spi = xyes; then
	AC_DEFINE([NEED_BFG_LOWL_SPI], [1], [Defined to 1 if lowlevel SPI drivers are being used])
fi

if test "x$need_lowl_usb" = "xno"; then
	USE_LIBUSB=no
	LIBUSB_LIBS=''
	LIBUSB_CFLAGS=''
fi
if test "x$USE_LIBUSB" = xyes; then
	AC_DEFINE([HAVE_LIBUSB], [1], [Define if you have libusb-1.0])
	save_CFLAGS="$CFLAGS"
	CFLAGS="$LIBUSB_CFLAGS $CFLAGS"
	AC_CHECK_DECLS([libusb_error_name],[true],[true],[#include <libusb.h>])
	CFLAGS="$save_CFLAGS"
fi


if test x$need_lowl_vcom = xyes; then
	AC_DEFINE([NEED_BFG_LOWL_VCOM], [1], [Defined to 1 if lowlevel VCOM drivers are being used])
	need_lowlevel=yes
	
	if $have_win32; then
		echo '#include <iospeeds.h>' >iospeeds_local.h
		
		found_ddkusb=false
		AC_CHECK_HEADER([usbiodef.h],[
			found_ddkusb=true
		],[
			BFG_FIND_INCLUDE_PATH([ddk],[usbiodef.h],[
				found_ddkusb=true
				AUTOSCAN_CPPFLAGS="-I${inclpath_usbiodef_h}"
			],[],[
				#include <windows.h>
				#include <ddk/usbioctl.h>
				AC_INCLUDES_DEFAULT
			])
		],[
			#include <windows.h>
			#include <usbioctl.h>
			AC_INCLUDES_DEFAULT
		])
		if $found_ddkusb; then
			AUTOSCAN_LIBS="-lsetupapi"
			AC_DEFINE([HAVE_WIN_DDKUSB],[1],[Defined to 1 if Windows DDK USB headers are being used])
		fi
	else
		AC_MSG_CHECKING([what baud rates your system supports])
		echo '#include <termios.h>' | ${CPP} -dM - 2>/dev/null | sed 's/.*[ 	]B\([0-9][0-9]*\)[ 	].*/IOSPEED(\1)/' | grep IOSPEED >iospeeds_local.h
		if grep -q IOSPEED iospeeds_local.h; then
			AC_MSG_RESULT([done])
		else
			AC_MSG_RESULT([failed, using standard POSIX])
			echo '#include <iospeeds_posix.h>' >iospeeds_local.h
		fi
	fi
fi

if test "x$USE_OPENCL$need_lowl_hid" = xnono; then
	DLOPEN_FLAGS=""
fi

lowllist="$lowllist hid/need_lowl_hid"
if test x$need_lowl_hid = xyes; then
	AC_DEFINE([NEED_BFG_LOWL_HID], [1], [Defined to 1 if lowlevel hid drivers are being used])
	need_lowlevel=yes
fi

lowllist="$lowllist mswin/need_lowl_mswin"
if test x$need_lowl_mswin = xyes; then
	AC_DEFINE([NEED_BFG_LOWL_MSWIN], [1], [Defined to 1 if lowlevel mswin drivers are being used])
	need_lowlevel=yes
fi

lowllist="$lowllist pci/need_lowl_pci"
if test x$need_lowl_pci = xyes; then
	AC_DEFINE([NEED_BFG_LOWL_PCI], [1], [Defined to 1 if lowlevel PCI drivers are being used])
	need_lowlevel=yes
	lowllist="$lowllist pci:uio/uio"
	if test x$uio = xyes; then
		AC_DEFINE([USE_UIO], [1], [Defined to 1 if lowlevel PCI drivers should support UIO])
	fi
	lowllist="$lowllist pci:vfio/vfio"
	if test x$vfio = xyes; then
		AC_DEFINE([USE_VFIO], [1], [Defined to 1 if lowlevel PCI drivers should support VFIO])
	fi
fi
AM_CONDITIONAL([USE_VFIO], [test x$need_lowl_pci$vfio = xyesyes])

lowllist="$lowllist usb/need_lowl_usb"
if test x$need_lowl_usb = xyes; then
	need_lowlevel=yes
fi

lowllist="$lowllist vcom/need_lowl_vcom"
if test x$need_lowl_vcom = xyes; then
	need_lowlevel=yes
fi

if test x$need_lowlevel = xyes; then
	AC_DEFINE([HAVE_BFG_LOWLEVEL], [1], [Defined to 1 if lowlevel drivers are being used])
fi

if test x$need_linux_i2c_dev = xyes; then
	AC_CHECK_HEADERS([linux/i2c-dev-user.h])
	AC_CHECK_DECL([i2c_smbus_read_word_data],[true],[
		AC_MSG_ERROR([linux/i2c-dev.h header from i2c-tools/libi2c-dev (NOT linux headers) is required for KnCMiner drivers])
	],[
		#include <stddef.h>
		#ifdef HAVE_LINUX_I2C_DEV_USER_H
		#include <linux/i2c-dev-user.h>
		#else
		#ifdef NEED_LINUX_I2C_H
		#include <linux/i2c.h>
		#endif
		#include <linux/i2c-dev.h>
		#endif
	])
fi


curses="auto"

AC_ARG_WITH([curses],
	[AC_HELP_STRING([--without-curses],[Compile support for curses TUI (default enabled)])],
	[curses=$withval]
	)
if test "x$curses" = "xno"; then
	optlist="$optlist curses"
else
	curses_enableaction="install a curses library"
	orig_libs="$LIBS"
	if test "x${curses}" = "xyes"; then
		preferl=''
	else
		preferl="${curses} ${curses}6 ${curses}5"
	fi
	for wideornot in w u ''; do
		for ncursesver in '' 6 5; do
			preferl="${preferl} ncurses${wideornot}${ncursesver}"
		done
		preferl="${preferl} pdcurses${wideornot}"
	done
	if test "x$cross_compiling" != "xyes"; then
		AC_MSG_CHECKING([for best native curses library])
		orig_cflags="$CFLAGS"
		for curses_lib in ${preferl}; do
			if ! ${curses_lib}-config --cflags >/dev/null 2>/dev/null; then
				continue
			fi
			CFLAGS="$orig_cflags $(${curses_lib}-config --cflags)"
			LIBS="$orig_libs $(${curses_lib}-config --libs)"
			AC_LINK_IFELSE([AC_LANG_PROGRAM([[
				#include <curses.h>
			]], [[
				WINDOW *w = NULL;
				mvwprintw(w, 2, 2, "Testing %s", "o hai");
			]])], [
				curses=yes
				optlist="$optlist ${curses_lib}/curses"
				AC_MSG_RESULT([$curses_lib])
				NCURSES_LIBS=`${curses_lib}-config --libs`
				NCURSES_CPPFLAGS=`${curses_lib}-config --cflags`
				break
			], [
				AC_MSG_WARN([${curses_lib} doesn't seem to be installed properly])
			])
		done
		CFLAGS="$orig_cflags"
		if test "x$curses" != "xyes"; then
			AC_MSG_RESULT([none?])
		fi
	fi
	if test "x$curses" != "xyes"; then
		sym=addstr
			AC_SEARCH_LIBS(${sym}, ${preferl}, [
				curses=yes
				eval "curseslib=\"\${ac_cv_search_${sym}}\""
				barelib=`echo "${curseslib}" | sed -e 's/-l//'`
				optlist="$optlist ${barelib}/curses"
				if test "x${curseslib}" != "xnone required"; then
					NCURSES_LIBS="${curseslib}"
				fi
				
				# Need to check for headers in subdirectories, to ensure we get wide stuff
				barelib=`echo "${barelib}" | sed -e 's/6//'`
				barelib=`echo "${barelib}" | sed -e 's/5//'`
				BFG_FIND_INCLUDE_PATH([${barelib}],[curses.h],[
					NCURSES_CPPFLAGS="-I${inclpath_curses_h}"
				])
				
				break
			], [
				if test "x$curses" = "xyes"; then
					AC_MSG_ERROR([Could not find curses library - please install libncurses-dev or pdcurses-dev (or configure --without-curses)])
				else
					AC_MSG_WARN([Could not find curses library - if you want a TUI, install libncurses-dev or pdcurses-dev])
					curses=no
					optlist="$optlist curses"
				fi
			])
	fi
	if test "x$curses" = "xyes"; then
		AC_DEFINE([HAVE_CURSES], [1], [Defined to 1 if curses TUI support is wanted])
		AC_MSG_CHECKING([whether curses library supports wide characters])
		orig_cflags="$CFLAGS"
		CFLAGS="$orig_cflags $NCURSES_CPPFLAGS"
		LIBS="$orig_libs $NCURSES_LIBS"
		AC_LINK_IFELSE([
			AC_LANG_PROGRAM([
				#define PDC_WIDE
				#include <curses.h>
			],[
				addwstr(L"test");
				add_wch(WACS_VLINE);
			])
		],[
			AC_MSG_RESULT([yes])
			AC_DEFINE([USE_UNICODE],[1],[Defined to 1 if curses supports wide characters])
		],[
			AC_MSG_RESULT([no])
		])
		CFLAGS="$orig_cflags"
	fi
	LIBS="$orig_libs"
fi


BFG_BUNDLED_LIB([libbase58],[libbase58],[auto],[libbase58],[base58],[],[--disable-tool --disable-static --enable-shared])
BFG_BUNDLED_LIB([libblkmaker],[libblkmaker_jansson-0.1],[no],[libblkmaker],[blkmaker_jansson-0.1 blkmaker-0.1],[libbase58])


$have_udevrules || use_udevrules=false
if $use_udevrules; then
	AC_ARG_WITH([udevrulesdir],
		AS_HELP_STRING([--with-udevrulesdir=DIR], [Install udev rules into this directory]),
		[], [
			if test -d /lib/udev/rules.d; then
				with_udevrulesdir=/lib/udev/rules.d
			else
				with_udevrulesdir=no
			fi
		]
	)
	if test "x$with_udevrulesdir" = "xno"; then
		use_udevrules=false
	fi
	if $use_udevrules; then
		AC_SUBST([udevrulesdir], [$with_udevrulesdir])
	fi
fi


AM_CONDITIONAL([NEED_BFG_BINLOADER], [test x$need_binloader = xyes])
AM_CONDITIONAL([NEED_DYNCLOCK], [test x$need_dynclock = xyes])
AM_CONDITIONAL([USE_GC3355], [test x$need_gc3355 = xyes])
AM_CONDITIONAL([NEED_BFG_LOWL_VCOM], [test x$need_lowl_vcom = xyes])
AM_CONDITIONAL([NEED_BFG_LOWL_FTDI], [test x$need_lowl_ftdi = xyes])
AM_CONDITIONAL([NEED_BFG_LOWL_HID], [test x$need_lowl_hid = xyes])
AM_CONDITIONAL([NEED_BFG_LOWL_MSWIN], [test x$need_lowl_mswin = xyes])
AM_CONDITIONAL([NEED_BFG_LOWL_PCI], [test x$need_lowl_pci = xyes])
AM_CONDITIONAL([NEED_BFG_LOWL_SPI], [test x$need_lowl_spi = xyes])
AM_CONDITIONAL([NEED_BFG_LOWLEVEL], [test x$need_lowlevel = xyes])
AM_CONDITIONAL([NEED_BFG_WORK2D], [test x$need_work2d = xyes])
AM_CONDITIONAL([NEED_KNC_ASIC], [test x$need_knc_asic = xyes])
AM_CONDITIONAL([HAVE_CURSES], [test x$curses = xyes])
AM_CONDITIONAL([HAVE_SENSORS], [test x$with_sensors = xyes])
AM_CONDITIONAL([HAVE_CYGWIN], [test x$have_cygwin = xtrue])
AM_CONDITIONAL([HAVE_LIBUSB], [test x$USE_LIBUSB = xyes])
AM_CONDITIONAL([HAVE_WINDOWS], [test x$have_win32 = xtrue])
AM_CONDITIONAL([HAVE_x86_64], [test x$have_x86_64 = xtrue])
AM_CONDITIONAL([HAVE_WIN_DDKUSB], [test x$found_ddkusb = xtrue])
AM_CONDITIONAL([HAS_FPGA], [test x$has_fpga != xno])
AM_CONDITIONAL([HAS_ASIC], [test x$has_asic != xno])
AM_CONDITIONAL([USE_UDEVRULES], [$use_udevrules])
AM_CONDITIONAL([BROAD_UDEVRULES], [$broad_udevrules])

dnl Find YASM
has_yasm=false
if test "x$have_x86_32$have_x86_64" != "xfalsefalse"; then
AC_PATH_PROG([YASM],[yasm],[false])
if test "x$YASM" != "xfalse" ; then
  has_yasm_enableaction="install yasm 1.0.1+"
  AC_MSG_CHECKING([if yasm version is greater than 1.0.1])
  yasmver=`"$YASM" --version | head -1 | cut -d\  -f2`
  yamajor=`echo $yasmver | cut -d. -f1`
  yaminor=`echo $yasmver | cut -d. -f2`
  yamini=`echo $yasmver | cut -d. -f3`
  if test "$yamajor" -ge "1" ; then
    if test "$yamajor" -eq "1" ; then
      if test "$yaminor" -ge "0" ; then
        if test "$yaminor" -eq "0"; then
          if test "$yamini" -ge "1"; then
            has_yasm=true
          fi
        else
          has_yasm=true
        fi
      fi
    fi
  else
    has_yasm=false
  fi
  if test "x$has_yasm" = "xtrue" ; then
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
  fi
fi
if test "x$has_yasm" = "xfalse" ; then
  AC_MSG_NOTICE([yasm is required for the assembly algorithms. They will be skipped.])
else
	AC_DEFINE([HAVE_YASM], [1], [Defined to 1 if yasm is being used])
	if test "x$have_win32$have_cygwin" != "xfalsefalse"; then
		if test "x$have_x86_64" = xtrue; then
			YASM_FMT="win64"
		else
			YASM_FMT="coff"
		fi
	elif test "x$have_macho" = "xtrue"; then
		YASM_FMT="macho$bitness"
	else
		YASM_FMT="elf$bitness"
	fi
fi

fi

AM_CONDITIONAL([HAS_YASM], [test x$has_yasm = xtrue])

have_sse2=no
if test "x$USE_CPUMINING$have_x86_32" = "xyestrue"; then
	AC_MSG_CHECKING([if SSE2 code compiles])
	save_CFLAGS="$CFLAGS"
	for flags in '' '-msse2'; do
		CFLAGS="$CFLAGS $flags"
		AC_TRY_LINK([
			#include <xmmintrin.h>
		],[
			int *i = (int *)0xdeadbeef;
			__m128i a, b;
			a = _mm_set1_epi32(i[0]);
			b = _mm_set_epi32(i[0], i[1], i[2], i[3]);
			a = _mm_add_epi32(a, b);
			a = _mm_andnot_si128(a, b);
			a = _mm_or_si128(a, b);
			a = _mm_slli_epi32(a, i[4]);
			a = _mm_and_si128(a, b);
			a = _mm_xor_si128(a, b);
		],[
			if test "x$flags" = "x"; then
				AC_MSG_RESULT([yes])
			else
				AC_MSG_RESULT([with $flags])
			fi
			SSE2_CFLAGS="$flags"
			have_sse2=yes
			break
		],[
			true
		])
	done
	CFLAGS="${save_CFLAGS}"
	if test "x$have_sse2" = "xyes"; then
		AC_DEFINE([HAVE_SSE2], [1], [Defined to 1 if yasm is being used])
	else
		AC_MSG_RESULT([no])
	fi
fi
AM_CONDITIONAL([HAVE_SSE2], [test "x$have_sse2" = "xyes"])

if test "x$need_lowl_vcom" = "xyes"; then
	AC_ARG_WITH([libudev], [AC_HELP_STRING([--without-libudev], [Autodetect FPGAs using libudev (default enabled)])],
		[libudev=$withval],
		[libudev=auto]
		)
	if test "x$libudev" != "xno"; then
		AC_CHECK_HEADER([libudev.h],[
			libudev=yes
			UDEV_LIBS=-ludev
			AC_DEFINE([HAVE_LIBUDEV], [1], [Defined to 1 if libudev is wanted])
		], [
			if test "x$libudev" = "xyes"; then
				AC_MSG_ERROR([libudev not found])
			fi
			libudev=no
		])
	fi
fi
AM_CONDITIONAL([HAVE_LIBUDEV], [test x$libudev != xno])

AC_SUBST(LIBUSB_LIBS)
AC_SUBST(LIBUSB_CFLAGS)

PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.18.2], ,[AC_MSG_ERROR([Missing required libcurl dev >= 7.18.2])])
if echo "$LIBCURL_CFLAGS" | grep '@CPPFLAG_CURL_STATICLIB@' >/dev/null 2>&1; then
	AC_MSG_WARN([Your libcurl pkgconfig file is broken, applying workaround])
	LIBCURL_CFLAGS=`echo "$LIBCURL_CFLAGS" | sed 's/@CPPFLAG_CURL_STATICLIB@//'`
fi
AC_SUBST(LIBCURL_LIBS)

AC_CHECK_FUNCS([setrlimit])

dnl CCAN wants to know a lot of vars.
# All the configuration checks.  Regrettably, the __attribute__ checks will
# give false positives on old GCCs, since they just cause warnings.  But that's
# fairly harmless.
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((cold)) cleanup(void) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_COLD], [1],
                                    [Define if __attribute__((cold))]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((const)) cleanup(void) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_CONST], [1],
                                    [Define if __attribute__((const))]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((noreturn)) cleanup(void) { exit(1); }])],
	[
			  AC_DEFINE([HAVE_ATTRIBUTE_NORETURN], [1],
                                    [Define if __attribute__((noreturn))])
		AC_DEFINE_UNQUOTED([NORETURN], [__attribute__((noreturn))], [Syntax of noreturn attribute])
	], [
		AC_DEFINE_UNQUOTED([NORETURN], [])
	]
)
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((format(__printf__, 1, 2))) cleanup(const char *fmt, ...) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_PRINTF], [1],
                                    [Define if __attribute__((format(__printf__)))]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((unused)) cleanup(void) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_UNUSED], [1],
                                    [Define if __attribute__((unused))]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((used)) cleanup(void) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_USED], [1],
                                    [Define if __attribute__((used))]))
AC_LINK_IFELSE([AC_LANG_SOURCE([int main(void) { return __builtin_constant_p(1) ? 0 : 1; }])],
			  AC_DEFINE([HAVE_BUILTIN_CONSTANT_P], [1],
                                    [Define if have __builtin_constant_p]))
AC_LINK_IFELSE([AC_LANG_SOURCE([int main(void) { return __builtin_types_compatible_p(char *, int) ? 1 : 0; }])],
			  AC_DEFINE([HAVE_BUILTIN_TYPES_COMPATIBLE_P], [1],
                                    [Define if have __builtin_types_compatible_p]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static int __attribute__((warn_unused_result)) func(int x) { return x; }])],
			  AC_DEFINE([HAVE_WARN_UNUSED_RESULT], [1],
                                    [Define if __attribute__((warn_unused_result))]))


# byteswap functions
AH_TEMPLATE([HAVE_BYTESWAP_H], [Define to use byteswap macros from byteswap.h])
AH_TEMPLATE([HAVE_ENDIAN_H], [Define to use byteswap macros from endian.h])
AH_TEMPLATE([HAVE_SYS_ENDIAN_H], [Define to use byteswap macros from sys/endian.h])
AH_TEMPLATE([HAVE_LIBKERN_OSBYTEORDER_H], [Define to use byteswap macros from libkern/OSByteOrder.h])
BSWAP=''
for sym in bswap_ __builtin_bswap __bswap_ __swap swap OSSwapInt; do
	AC_MSG_CHECKING([for ${sym}* functions])
	for headerfile in '' byteswap.h endian.h sys/endian.h libkern/OSByteOrder.h; do
		BFG_INCLUDE([headerinclude], [${headerfile}])
		AC_LINK_IFELSE([
			AC_LANG_PROGRAM([
				${headerinclude}
			], [
				(void) ${sym}16(0);
				(void) ${sym}32(0);
				(void) ${sym}64(0);
			])
		], [
			BSWAP="${sym}"
			if test "x${headerfile}" = "x"; then
				AC_MSG_RESULT([yes])
			else
				AC_MSG_RESULT([found in ${headerfile}])
				AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_$headerfile]), 1)
			fi
			break 2
		])
	done
	AC_MSG_RESULT([no])
done
if test "x$BSWAP" = "x"; then
	true  # Substitutes are provided in miner.h
elif test "x$BSWAP" = "xbswap_"; then
	AC_MSG_CHECKING([if bswap_16 is already a macro])
	BFG_PREPROC_IFELSE([defined(bswap_16)], $headerfile, [
		AC_MSG_RESULT([yes])
		BSWAP=""
	],[
		AC_MSG_RESULT([no])
	])
fi
if test "x$BSWAP" != "x"; then
	AC_DEFINE_UNQUOTED([bswap_16], ${BSWAP}16, [Define to 16-bit byteswap macro])
	AC_DEFINE_UNQUOTED([bswap_32], ${BSWAP}32, [Define to 16-bit byteswap macro])
	AC_DEFINE_UNQUOTED([bswap_64], ${BSWAP}64, [Define to 16-bit byteswap macro])
fi

# endian definition macros
AC_MSG_CHECKING([for platform endian])
found_endian=no
for headerfile in '' endian.h sys/endian.h sys/param.h; do
	for pfx in '' '__'; do
		BFG_PREPROC_IFELSE([defined(${pfx}BYTE_ORDER) && defined(${pfx}BIG_ENDIAN) && defined(${pfx}LITTLE_ENDIAN) && (${pfx}BYTE_ORDER == ${pfx}BIG_ENDIAN || ${pfx}BYTE_ORDER == ${pfx}LITTLE_ENDIAN)], ${headerfile}, [
			if test "x$headerfile" = "x"; then
				headerfilec=''
			else
				headerfilec=" (${headerfile})"
			fi
			BFG_PREPROC_IFELSE([${pfx}BYTE_ORDER == ${pfx}BIG_ENDIAN], ${headerfile}, [
				AC_MSG_RESULT([big endian${headerfilec}])
				AC_DEFINE(WORDS_BIGENDIAN, 1, [Define if your platform is big endian])
			], [
				AC_MSG_RESULT([little endian${headerfilec}])
			])
			found_endian=yes
			break 2
		],[true])
	done
done
if test "x$found_endian" = "xno"; then
	if $have_win32 || $have_cygwin; then
		AC_MSG_RESULT([assuming little endian (Windows)])
	else
		# AC_C_BIGENDIAN is reported to have problems, and invasive even if buried in a conditional, so don't use it
		AC_MSG_RESULT([unknown])
		AC_MSG_ERROR([Unable to identify platform endian])
	fi
fi


AC_MSG_CHECKING([if good static asserts compile])
AC_TRY_LINK([
	#include <stddef.h>
], [
	_Static_assert(1, ":)");
], [
	AC_MSG_RESULT([yes])
], [
	AC_MSG_RESULT([no])
	AC_DEFINE_UNQUOTED([_Static_assert(...)], [], [Turns _Static_assert into noop for compilers that don't support it])
])


AC_MSG_CHECKING([if GNU format attribute compiles])
AC_TRY_COMPILE([
	#define FORMAT_SYNTAX_CHECK(...) __attribute__(( format(__VA_ARGS__) ))
	int myfunc(char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 1, 2);
	int myfunc(char *fmt, ...) {
		return 42;
	}
], [
	myfunc("abc%d", 42);
], [
	AC_MSG_RESULT([yes])
	AC_DEFINE_UNQUOTED([FORMAT_SYNTAX_CHECK(...)], [__attribute__(( format(__VA_ARGS__) ))], [Syntax of format-checking attribute])
], [
	AC_MSG_RESULT([no])
	AC_DEFINE_UNQUOTED([FORMAT_SYNTAX_CHECK(...)], [])
])


AC_MSG_CHECKING([for clock_gettime(CLOCK_MONOTONIC)])
AC_TRY_COMPILE([
	#define _GNU_SOURCE
	#include <time.h>
],[
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
],[
	AC_MSG_RESULT([yes])
	AC_DEFINE([HAVE_CLOCK_GETTIME_MONOTONIC], [1], [Defined to 1 if clock_gettime(CLOCK_MONOTONIC) is defined])
	save_LIBS="${LIBS}"
	AC_SEARCH_LIBS([clock_gettime],[rt posix4],[
		if test "x${ac_cv_search_clock_gettime}" != "xnone required"; then
			RT_LIBS="${ac_cv_search_clock_gettime}"
		fi
	])
	LIBS="${save_LIBS}"
	AC_CHECK_FUNCS([clock_nanosleep])
	AC_MSG_CHECKING([for clock_gettime(CLOCK_MONOTONIC_RAW)])
	AC_TRY_COMPILE([
		#define _GNU_SOURCE
		#include <time.h>
	],[
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_CLOCK_GETTIME_MONOTONIC_RAW], [1], [Defined to 1 if clock_gettime(CLOCK_MONOTONIC_RAW) is defined])
	],[
		AC_MSG_RESULT([no])
	])
],[
	AC_MSG_RESULT([no])
])


save_LIBS="$LIBS"
LIBS="$LIBS $MATH_LIBS"
AC_CHECK_FUNCS([log2])
LIBS="$save_LIBS"


if test "x$prefix" = xNONE; then
	prefix=/usr/local
fi

AM_CONDITIONAL([NEED_BITSTREAM_FPGAMINER], [test x$USE_MODMINER$USE_X6500 != xnono])

dnl Hide unnecessary explanations, only after any fatal errors
howto_SHA256D=x
if test "x$USE_SHA256D" = xno; then
	howto_BITFURY=x
fi
if test "x$want_libusb" = xno; then
	howto_LIBUSB=x
fi

m4_define([BFG_PRINT_LIST],[
	eval _mylist="\$$2"
	_yeslist=
	_nolist=
	_enableactions=
	foundone=no
	for _opt in $_mylist; do
		IFS=/ read _opt _var <<EOF
$_opt
EOF
		test -n "$_var" || _var="$_opt"
		eval "_val=\"\$${_var}\""
		if test "x$_val" = "xno" || test "x$_val" = "xfalse"; then
			_nolist="$_nolist $_opt"
			_enableaction=`bfg_enableaction $_var`
			if test -n "$_enableaction"; then
				_enableactions="${_enableactions}~    To enable ${_opt}: ${_enableaction}"
			fi
		else
			_yeslist="$_yeslist $_opt"
			foundone=yes
		fi
	done
	_yeslist=`echo "$_yeslist" | tr ' ' '\n' | sort | tr '\n' ' '`
	_nolist=`echo "$_nolist" | tr ' ' '\n' | sort | tr '\n' ' '`
	AC_DEFINE_UNQUOTED(AS_TR_CPP([BFG_$2]),["$_yeslist"],[List of drivers being built])
	$2_print="  Enabled..$1..:${_yeslist}~  Disabled.$1..:${_nolist}${_enableactions}"
])
BFG_PRINT_LIST([Lowlevel..],[lowllist])
BFG_PRINT_LIST([Drivers...],[driverlist])
BFG_PRINT_LIST([Algorithms],[algolist])
if test "x$foundone" = "xno"; then
	AC_MSG_ERROR([No mining algorithms configured!])
fi
BFG_PRINT_LIST([Options...],[optlist])


AC_SUBST(AUTOSCAN_CPPFLAGS)
AC_SUBST(AUTOSCAN_LIBS)
AC_SUBST(PTHREAD_FLAGS)
AC_SUBST(DLOPEN_FLAGS)
AC_SUBST(PTHREAD_LIBS)
AC_SUBST(NCURSES_CPPFLAGS)
AC_SUBST(NCURSES_LIBS)
AC_SUBST(PDCURSES_LIBS)
AC_SUBST(WS2_LIBS)
AC_SUBST(MM_LIBS)
AC_SUBST(MATH_LIBS)
AC_SUBST(RT_LIBS)
AC_SUBST(UDEV_LIBS)
AC_SUBST(SSE2_CFLAGS)
AC_SUBST(YASM_FMT)

AC_CONFIG_FILES([
	Makefile
	x86_64/Makefile
	x86_32/Makefile
	ccan.bfg/Makefile
	lib/Makefile
	])

if $use_udevrules; then
	AC_CONFIG_FILES([70-bfgminer.rules])
fi

AC_OUTPUT
BFG_CUSTOM_SUBDIRS_OUTPUT


wordfilter ()
{
  echo "$1" | sed 's/ \+/ /g;s/^ *//;s/ *$//'
}

echo
echo "------------------------------------------------------------------------"
echo "$PACKAGE $VERSION configuration options summary"
echo "------------------------------------------------------------------------"
echo
echo "  CFLAGS...............: "`wordfilter "$CPPFLAGS $AUTOSCAN_CPPFLAGS $NCURSES_CPPFLAGS $PTHREAD_FLAGS $CFLAGS $LIBUSB_CFLAGS $JANSSON_CFLAGS $PTHREAD_FLAGS $libbase58_CFLAGS $libblkmaker_CFLAGS $hidapi_CFLAGS"`
echo "  LDFLAGS..............: "`wordfilter "$LDFLAGS $AUTOSCAN_LIBS $PTHREAD_FLAGS $PTHREAD_LIBS $LIBS $DLOPEN_FLAGS $LIBCURL_LIBS $JANSSON_LIBS $NCURSES_LIBS $PDCURSES_LIBS $WS2_LIBS $MATH_LIBS $UDEV_LIBS $LIBUSB_LIBS $RT_LIBS $sensors_LIBS $libbase58_LIBS $libblkmaker_LIBS"`
echo "  Installation.prefix..: $prefix"
echo
echo "${lowllist_print}" | tr '~' '\n'
echo "${driverlist_print}" | tr '~' '\n'
echo "${algolist_print}" | tr '~' '\n'
echo "${optlist_print}" | tr '~' '\n'
echo
