dnl * Copyright 2012-2013 Luke Dashjr
dnl * Copyright 2011-2013 Con Kolivas
dnl * Copyright 2010-2011 Jeff Garzik
dnl * Copyright 2012 Xiangfu
dnl * Copyright 2011 Rusty Russell
dnl * Copyright 2011 Mark Crichton
dnl *
dnl * This program is free software; you can redistribute it and/or modify it
dnl * under the terms of the GNU General Public License as published by the Free
dnl * Software Foundation; either version 3 of the License, or (at your option)
dnl * any later version.  See COPYING for more details.

##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_maj], [3])
m4_define([v_min], [0])
m4_define([v_mic], [9])
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

AM_INIT_AUTOMAKE([foreign])
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
AM_MAINTAINER_MODE

dnl Checks for programs
AC_PROG_CC_C99
gl_EARLY
AC_PROG_GCC_TRADITIONAL
AM_PROG_CC_C_O
AC_PROG_RANLIB

gl_INIT

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(syslog.h)
AC_CHECK_HEADERS([sys/epoll.h])
AC_CHECK_HEADERS([sys/prctl.h])

AC_FUNC_ALLOCA

have_cygwin=false
have_win32=false
DLOPEN_FLAGS="-ldl"
WS2_LIBS=""
MATH_LIBS="-lm"

case $target in
  amd64-* | x86_64-*)
    have_x86_32=false
    have_x86_64=true
    ;;
  i386-* | i486-* | i586-* | i686-* | x86-*)
    have_x86_32=true
    have_x86_64=false
    ;;
  *)
    have_x86_32=false
    have_x86_64=false
    ;;
esac

case $target in
  *-*-mingw*)
    have_x86_64=false
    have_win32=true
    DLOPEN_FLAGS=""
    WS2_LIBS="-lws2_32"
    AC_DEFINE([_WIN32_WINNT], [0x0501], "WinNT version for XP+ support")
    AC_DEFINE([FD_SETSIZE], [4096], [Maximum sockets before fd_set overflows])
    ;;
  *-*-cygwin*)
	have_cygwin=true
	;;
  powerpc-*-darwin*)
    CFLAGS="$CFLAGS -faltivec"
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


cpumining="no"

AC_ARG_ENABLE([cpumining],
	[AC_HELP_STRING([--enable-cpumining],[Build with CPU mining support (default disabled)])],
	[cpumining=$enableval]
	)
if test "x$cpumining" = xyes; then
	AC_DEFINE_UNQUOTED([WANT_CPUMINE], [1], [Enable CPUMINING])
fi
AM_CONDITIONAL([HAS_CPUMINE], [test x$cpumining = xyes])

opencl="yes"

AC_ARG_ENABLE([opencl],
	[AC_HELP_STRING([--disable-opencl],[Build without support for OpenCL (default enabled)])],
	[opencl=$enableval]
	)
if test "x$opencl" = xyes; then
	AC_DEFINE([HAVE_OPENCL], [1], [Defined to 1 if OpenCL support is wanted])
else
	DLOPEN_FLAGS=""
fi
AM_CONDITIONAL([HAVE_OPENCL], [test x$opencl = xyes])

m4_define([BFG_PTHREAD_FLAG_CHECK],
	AC_MSG_CHECKING([for $1])
	for cflag in ' -pthread' ''; do
		for lib in ' -lpthread' ''; do
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
)

save_CFLAGS="${CFLAGS}"
save_LIBS="${LIBS}"
found_pthread=false
BFG_PTHREAD_FLAG_CHECK([pthread_cancel],[
	AC_DEFINE([HAVE_PTHREAD_CANCEL], [1], [Define if you have a native pthread_cancel])
])
if test "x${found_pthread}" = "xfalse"; then
	AC_MSG_RESULT([no])
	BFG_PTHREAD_FLAG_CHECK([pthread_create])
	if test "x${found_pthread}" = "xfalse"; then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Could not find pthread library - please install libpthread])
	fi
fi
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

if test "x$opencl" = xyes; then
	adl="yes"
	
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

AC_ARG_ENABLE([bitforce],
	[AC_HELP_STRING([--disable-bitforce],[Compile support for BitForce (default enabled)])],
	[bitforce=$enableval],
	[bitforce=yes]
	)
if test "x$bitforce" = xyes; then
	AC_DEFINE([USE_BITFORCE], [1], [Defined to 1 if BitForce support is wanted])
fi
AM_CONDITIONAL([HAS_BITFORCE], [test x$bitforce = xyes])

AC_ARG_ENABLE([icarus],
	[AC_HELP_STRING([--disable-icarus],[Compile support for Icarus (default enabled)])],
	[icarus=$enableval],
	[icarus=yes]
	)
if test "x$icarus" = xyes; then
	AC_DEFINE([USE_ICARUS], [1], [Defined to 1 if Icarus support is wanted])
fi
AM_CONDITIONAL([HAS_ICARUS], [test x$icarus = xyes])

avalon="no"

AC_ARG_ENABLE([avalon],
	[AC_HELP_STRING([--enable-avalon],[Compile support for Avalon (default disabled)])],
	[avalon=$enableval]
	)
if test "x$avalon" = xyes; then
	AC_DEFINE([USE_AVALON], [1], [Defined to 1 if Avalon support is wanted])
fi
AM_CONDITIONAL([HAS_AVALON], [test x$avalon = xyes])

AC_ARG_ENABLE([modminer],
	[AC_HELP_STRING([--disable-modminer],[Compile support for ModMiner (default enabled)])],
	[modminer=$enableval],
	[modminer=yes]
	)
if test "x$modminer" = xyes; then
	AC_DEFINE([USE_MODMINER], [1], [Defined to 1 if ModMiner support is wanted])
fi
AM_CONDITIONAL([HAS_MODMINER], [test x$modminer = xyes])

PKG_PROG_PKG_CONFIG()

libusb=no
libusb_include_path=""
PKG_CHECK_MODULES([LIBUSB], [libusb-1.0],[
	libusb=yes
],[
	for usb_lib in usb-1.0 usb; do
		AC_CHECK_LIB($usb_lib, libusb_init, [
			libusb=yes
			break
		])
	done
	if test "x$libusb" = xyes; then
			AC_CHECK_DECL([libusb_init],[
				true
			],[
				AC_PROG_CPP
				AC_MSG_CHECKING([whether libusb_init is declared in subdirectory])
				libusb_include_path=`echo '#include <libusb-1.0/libusb.h>' | ${CPP} -M - 2>/dev/null | tr -d '\\n\\r\\\\' | sed -e 's/^.*[[:space:]:]\(\/[^[:space:]]*\)libusb\.h[[:space:]].*$/\1/' -e t -e d`
				if test "x$libusb_include_path" != "x"; then
					LIBUSB_LIBS="-l$usb_lib"
					LIBUSB_CFLAGS="-I$libusb_include_path"
					AC_MSG_RESULT([yes])
				else
					libusb=no
					AC_MSG_RESULT([no])
				fi
			],[#include <libusb.h>])
	fi
])

AC_ARG_ENABLE([x6500],
	[AC_HELP_STRING([--disable-x6500],[Compile support for X6500 (default if libusb)])],
	[x6500=$enableval],
	[x6500=auto]
	)
if test "x$x6500$libusb" = xyesno; then
	AC_MSG_ERROR([Could not find libusb, required for X6500 support])
elif test "x$x6500" = xauto; then
	x6500="$libusb"
	if test "x$libusb" = xno; then
		AC_MSG_WARN([Could not find libusb, required for X6500 support])
		x6500warn=yes
	fi
fi
if test "x$x6500" = xyes; then
	AC_DEFINE([USE_X6500], [1], [Defined to 1 if X6500 support is wanted])
fi
AM_CONDITIONAL([HAS_X6500], [test x$x6500 = xyes])

AC_ARG_ENABLE([ztex],
	[AC_HELP_STRING([--disable-ztex],[Compile support for ZTEX (default if libusb)])],
	[ztex=$enableval],
	[ztex=auto]
	)
if test "x$ztex$libusb" = xyesno; then
	AC_MSG_ERROR([Could not find libusb, required for ZTEX support])
elif test "x$ztex" = xauto; then
	ztex="$libusb"
	if test "x$libusb" = xno; then
		AC_MSG_WARN([Could not find libusb, required for ZTEX support])
		ztexwarn=yes
	fi
fi
if test "x$ztex" = xyes; then
	AC_DEFINE([USE_ZTEX], [1], [Defined to 1 if ZTEX support is wanted])
fi
AM_CONDITIONAL([HAS_ZTEX], [test x$ztex = xyes])


if test "x$x6500$ztex" = "xnono"; then
	libusb=no
	LIBUSB_LIBS=''
	LIBUSB_CFLAGS=''
fi
if test "x$libusb" = xyes; then
	AC_DEFINE([HAVE_LIBUSB], [1], [Define if you have libusb-1.0])
	save_CFLAGS="$CFLAGS"
	CFLAGS="$LIBUSB_CFLAGS $CFLAGS"
	AC_CHECK_DECLS([libusb_error_name],[true],[true],[#include <libusb.h>])
	CFLAGS="$save_CFLAGS"
fi


scrypt="no"

AC_ARG_ENABLE([scrypt],
	[AC_HELP_STRING([--enable-scrypt],[Compile support for scrypt mining (default disabled)])],
	[scrypt=$enableval]
	)
if test "x$scrypt" = xyes; then
	AC_DEFINE([USE_SCRYPT], [1], [Defined to 1 if scrypt support is wanted])
fi


curses="auto"

AC_ARG_WITH([curses],
	[AC_HELP_STRING([--without-curses],[Compile support for curses TUI (default enabled)])],
	[curses=$withval]
	)
if test "x$curses" = "xno"; then
	cursesmsg='User specified --without-curses. TUI support DISABLED'
else
	orig_libs="$LIBS"
	if test "x${curses}" = "xyes"; then
		prefer=''
		preferl=''
	else
		prefer="${curses}"
		preferl="${curses} ${curses}6 ${curses}5"
	fi
	if test "x$cross_compiling" != "xyes"; then
		AC_MSG_CHECKING([for best native curses library])
		orig_cflags="$CFLAGS"
		for curses_lib in ${preferl} ncurses{w,}{,6,5}; do
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
				cursesmsg="FOUND: ${curses_lib}"
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

	AC_SEARCH_LIBS(addstr, ${prefer} ncursesw ncurses pdcurses, [
		curses=yes
		cursesmsg="FOUND: ${ac_cv_search_addstr}"
		cursesmsg="${cursesmsg/-l/}"
		if test "x${ac_cv_search_addstr}" != "xnone required"; then
			NCURSES_LIBS="${ac_cv_search_addstr}"
		fi
	], [
		if test "x$curses" = "xyes"; then
			AC_MSG_ERROR([Could not find curses library - please install libncurses-dev or pdcurses-dev (or configure --without-curses)])
		else
			AC_MSG_WARN([Could not find curses library - if you want a TUI, install libncurses-dev or pdcurses-dev])
			curses=no
			cursesmsg='NOT FOUND. TUI support DISABLED'
		fi
	])

	fi
	if test "x$curses" = "xyes"; then
		AC_DEFINE([HAVE_CURSES], [1], [Defined to 1 if curses TUI support is wanted])
	fi
	LIBS="$orig_libs"
fi


AC_ARG_WITH([system-libblkmaker], [AC_HELP_STRING([--with-system-libblkmaker], [Use system libblkmaker rather than bundled one (default disabled)])],[true],[with_system_libblkmaker=no])
if test "x$with_system_libblkmaker" = "xyes"; then
	PKG_CHECK_MODULES([libblkmaker],[libblkmaker_jansson-0.1],[
		true
	],[
		AC_MSG_ERROR([Could not find system libblkmaker])
	])
else
	save_LDFLAGS="$LDFLAGS"
	LDFLAGS="$LDFLAGS -Wl,-zorigin"
	origin_LDFLAGS=
	AC_MSG_CHECKING([whether the linker recognizes the -zorigin option])
	AC_TRY_LINK([],[],[
		AC_MSG_RESULT([yes])
		origin_LDFLAGS=',-zorigin'
	],[
		AC_MSG_RESULT([no])
	])
	LDFLAGS="$save_LDFLAGS"
	
	libblkmaker_CFLAGS='-Ilibblkmaker'
	libblkmaker_LDFLAGS='-Llibblkmaker/.libs -Wl,-rpath,\$$ORIGIN/libblkmaker/.libs'"$origin_LDFLAGS"
	libblkmaker_LIBS='-lblkmaker_jansson-0.1 -lblkmaker-0.1'
	AC_CONFIG_SUBDIRS([libblkmaker])
fi
AC_SUBST(libblkmaker_CFLAGS)
AC_SUBST(libblkmaker_LDFLAGS)
AC_SUBST(libblkmaker_LIBS)


AM_CONDITIONAL([NEED_LIBBLKMAKER], [test x$with_system_libblkmaker != xyes])
AM_CONDITIONAL([NEED_DYNCLOCK], [test x$icarus$modminer$x6500$ztex != xnonono])
AM_CONDITIONAL([NEED_FPGAUTILS], [test x$avalon$icarus$bitforce$modminer$x6500$ztex != xnononononono])
AM_CONDITIONAL([HAS_SCRYPT], [test x$scrypt = xyes])
AM_CONDITIONAL([HAVE_CURSES], [test x$curses = xyes])
AM_CONDITIONAL([HAVE_CYGWIN], [test x$have_cygwin = xtrue])
AM_CONDITIONAL([HAVE_WINDOWS], [test x$have_win32 = xtrue])
AM_CONDITIONAL([HAVE_x86_64], [test x$have_x86_64 = xtrue])

dnl Find YASM
has_yasm=false
if test "x$have_x86_32$have_x86_64" != "xfalsefalse"; then
AC_PATH_PROG([YASM],[yasm],[false])
if test "x$YASM" != "xfalse" ; then
  AC_MSG_CHECKING([if yasm version is greater than 1.0.1])
  yasmver=`yasm --version | head -1 | cut -d\  -f2`
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
fi
fi

AM_CONDITIONAL([HAS_YASM], [test x$has_yasm = xtrue])

if test "x$bitforce$modminer$icarus" != "xnonono"; then
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

AC_MSG_CHECKING([for roundl function])
save_LIBS="${LIBS}"
LIBS="$LIBS -lm"
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
	#include <math.h>
]], [[
	return (roundl(*(long double *)0xdeadbeef) == 1.0);
]])], [
	AC_MSG_RESULT([yes])
], [
	AC_MSG_RESULT([no])
	AC_DEFINE([NEED_ROUNDL], [1], [Defined to 1 if C99 roundl is missing])
])
LIBS="${save_LIBS}"


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


if test "x$prefix" = xNONE; then
	prefix=/usr/local
fi

AC_DEFINE_UNQUOTED([CGMINER_PREFIX], ["$prefix/bin"], [Path to bfgminer install])

AC_DEFINE_UNQUOTED([PHATK_KERNNAME], ["phatk121016"], [Filename for phatk kernel])
AC_DEFINE_UNQUOTED([POCLBM_KERNNAME], ["poclbm121016"], [Filename for poclbm kernel])
AC_DEFINE_UNQUOTED([DIAKGCN_KERNNAME], ["diakgcn121016"], [Filename for diakgcn kernel])
AC_DEFINE_UNQUOTED([DIABLO_KERNNAME], ["diablo121016"], [Filename for diablo kernel])
AC_DEFINE_UNQUOTED([SCRYPT_KERNNAME], ["scrypt121016"], [Filename for scrypt kernel])


AC_SUBST(PTHREAD_FLAGS)
AC_SUBST(DLOPEN_FLAGS)
AC_SUBST(PTHREAD_LIBS)
AC_SUBST(NCURSES_CPPFLAGS)
AC_SUBST(NCURSES_LIBS)
AC_SUBST(PDCURSES_LIBS)
AC_SUBST(WS2_LIBS)
AC_SUBST(MATH_LIBS)
AC_SUBST(UDEV_LIBS)

AC_CONFIG_FILES([
	Makefile
	x86_64/Makefile
	x86_32/Makefile
	ccan/Makefile
	lib/Makefile
	])
AC_OUTPUT


echo
echo
echo
echo "------------------------------------------------------------------------"
echo "$PACKAGE $VERSION"
echo "------------------------------------------------------------------------"
echo
echo
echo "Configuration Options Summary:"
echo

echo "  curses.TUI...........: $cursesmsg"
if test "x$scrypt" != xno; then
	echo "  scrypt...............: Enabled"
else
	echo "  scrypt...............: Disabled"
fi

echo

if test "x$opencl" = xyes; then
	echo "  OpenCL...............: Enabled"
else
	echo "  OpenCL...............: Disabled"
fi

if test "x$adl" = xyes; then
	echo "    ADL.monitoring.....: Enabled"
elif test "x$opencl" = xyes; then
	echo "    ADL.monitoring.....: Disabled"
else
	echo "    ADL.monitoring.....: n/a"
fi

if test "x$avalon" = xyes; then
	echo "  Avalon.ASICs.........: Enabled"
else
	echo "  Avalon.ASICs.........: Disabled"
fi

if test "x$bitforce" = xyes; then
	echo "  BitForce.devices.....: Enabled"
else
	echo "  BitForce.devices.....: Disabled"
fi

if test "x$icarus" = xyes; then
	echo "  Icarus.FPGAs.........: Enabled"
else
	echo "  Icarus.FPGAs.........: Disabled"
fi

if test "x$modminer" = xyes; then
	echo "  ModMiner.FPGAs.......: Enabled"
else
	echo "  ModMiner.FPGAs.......: Disabled"
fi

if test "x$x6500" = xyes; then
	echo "  X6500.FPGAs..........: Enabled"
elif test "x$ztexwarn" = xyes; then
	echo "  X6500.FPGAs..........: Disabled (libusb not found)"
else
	echo "  X6500.FPGAs..........: Disabled"
fi

if test "x$ztex" = xyes; then
	echo "  ZTEX.FPGAs...........: Enabled"
elif test "x$ztexwarn" = xyes; then
	echo "  ZTEX.FPGAs...........: Disabled (libusb not found)"
else
	echo "  ZTEX.FPGAs...........: Disabled"
fi

if test "x$bitforce$modminer" != xnono; then
	echo "  libudev.detection....: $libudev"
fi

echo
if test "x$cpumining" = xyes; then
	echo "  CPU Mining...........: Enabled"
	echo "  ASM.(for CPU mining).: $has_yasm"
else
	echo "  CPU Mining...........: Disabled"
fi

echo
echo "Compilation............: make (or gmake)"
echo "  CPPFLAGS.............:" $CPPFLAGS $NCURSES_CPPFLAGS $PTHREAD_FLAGS
echo "  CFLAGS...............:" $CFLAGS $LIBUSB_CFLAGS $JANSSON_CFLAGS $PTHREAD_FLAGS $libblkmaker_CFLAGS
echo "  LDFLAGS..............:" $LDFLAGS $PTHREAD_FLAGS $PTHREAD_LIBS $libblkmaker_LDFLAGS
echo "  LDADD................:" $LIBS $DLOPEN_FLAGS $LIBCURL_LIBS $JANSSON_LIBS $NCURSES_LIBS $PDCURSES_LIBS $WS2_LIBS $MATH_LIBS $UDEV_LIBS $LIBUSB_LIBS $libblkmaker_LIBS
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
echo

