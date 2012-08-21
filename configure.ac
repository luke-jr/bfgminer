##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_maj], [2])
m4_define([v_min], [7])
m4_define([v_mic], [1])
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

have_win32=false
PTHREAD_FLAGS="-lpthread"
USB_LIBS=""
USB_FLAGS=""
DLOPEN_FLAGS="-ldl"
WS2_LIBS=""
TIMER_LIBS=""
MATH_LIBS="-lm"

case $target in
  x86_64-*)
    have_x86_64=true
    ;;
  *)
    have_x86_64=false
    ;;
esac

case $target in
	x86_64-w64-mingw32)
	have_x86_64=true
	have_win32=true
	PTHREAD_FLAGS=""
	DLOPEN_FLAGS=""
	WS2_LIBS="-lws2_32"
	TIMER_LIBS="-lwinmm"
	;;
  *-*-mingw*)
    have_x86_64=false
    have_win32=true
    PTHREAD_FLAGS=""
    DLOPEN_FLAGS=""
    WS2_LIBS="-lws2_32"
	TIMER_LIBS="-lwinmm"
    ;;
  powerpc-*-darwin*)
    CFLAGS="$CFLAGS -faltivec"
    PTHREAD_FLAGS=""
    ;;
  *-*-darwin*)
    PTHREAD_FLAGS=""
	;;
esac

cpumining="no"

AC_ARG_ENABLE([cpumining],
	[AC_HELP_STRING([--enable-cpumining],[Build with cpu mining support(default disabled)])],
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

AC_CHECK_LIB(pthread, pthread_create, ,
        AC_MSG_ERROR([Could not find pthread library - please install libpthread]))
PTHREAD_LIBS=-lpthread

AC_CHECK_LIB(jansson, json_loads, request_jansson=false, request_jansson=true)

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

AC_ARG_ENABLE([modminer],
	[AC_HELP_STRING([--disable-modminer],[Compile support for ModMiner (default enabled)])],
	[modminer=$enableval],
	[modminer=yes]
	)
if test "x$modminer" = xyes; then
	AC_DEFINE([USE_MODMINER], [1], [Defined to 1 if ModMiner support is wanted])
fi
AM_CONDITIONAL([HAS_MODMINER], [test x$modminer = xyes])

AC_CHECK_LIB(usb-1.0, libusb_init,
	[
		libusb=yes
		AC_DEFINE([HAVE_LIBUSB], [1], [Defined to 1 if libusb is wanted])
		USB_LIBS="-lusb-1.0"
		USB_FLAGS=""
	],
	[libusb=no]
)

AC_ARG_ENABLE([ztex],
	[AC_HELP_STRING([--disable-ztex],[Compile support for Ztex (default if libusb)])],
	[ztex=$enableval],
	[ztex=auto]
	)
if test "x$ztex$libusb" = xyesno; then
	AC_MSG_ERROR([Could not find libusb, required for Ztex support])
elif test "x$ztex" = xauto; then
	ztex="$libusb"
	if test "x$libusb" = xno; then
		AC_MSG_WARN([Could not find libusb, required for Ztex support])
		ztexwarn=yes
	fi
fi
if test "x$ztex" = xyes; then
	AC_DEFINE([USE_ZTEX], [1], [Defined to 1 if Ztex support is wanted])
fi
AM_CONDITIONAL([HAS_ZTEX], [test x$ztex = xyes])


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
	AC_SEARCH_LIBS(addstr, ncurses pdcurses, [
		curses=yes
		cursesmsg="FOUND: ${ac_cv_search_addstr:2}"
		AC_DEFINE([HAVE_CURSES], [1], [Defined to 1 if curses TUI support is wanted])
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


AM_CONDITIONAL([NEED_FPGAUTILS], [test x$icarus$bitforce$modminer$ztex != xnononono])
AM_CONDITIONAL([HAS_SCRYPT], [test x$scrypt = xyes])
AM_CONDITIONAL([HAVE_CURSES], [test x$curses = xyes])
AM_CONDITIONAL([WANT_JANSSON], [test x$request_jansson = xtrue])
AM_CONDITIONAL([HAVE_WINDOWS], [test x$have_win32 = xtrue])
AM_CONDITIONAL([HAVE_x86_64], [test x$have_x86_64 = xtrue])

if test x$request_jansson = xtrue
then
	JANSSON_LIBS="compat/jansson/libjansson.a"
else
	JANSSON_LIBS=-ljansson
fi

dnl Find YASM
has_yasm=false
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

AM_CONDITIONAL([HAS_YASM], [test x$has_yasm = xtrue])

if test "x$bitforce$modminer" != xnono; then
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

PKG_PROG_PKG_CONFIG()

PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.15.6], [AC_DEFINE([CURL_HAS_SOCKOPT], [1], [Defined if version of curl supports sockopts.])],
[PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.10.1], ,[AC_MSG_ERROR([Missing required libcurl dev >= 7.10.1])])])
AC_SUBST(LIBCURL_LIBS)

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
			  AC_DEFINE([HAVE_ATTRIBUTE_NORETURN], [1],
                                    [Define if __attribute__((noreturn))]))
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

if test "x$prefix" = xNONE; then
	prefix=/usr/local
fi

AC_DEFINE_UNQUOTED([CGMINER_PREFIX], ["$prefix/bin"], [Path to bfgminer install])

AC_DEFINE_UNQUOTED([PHATK_KERNNAME], ["phatk120724"], [Filename for phatk kernel])
AC_DEFINE_UNQUOTED([POCLBM_KERNNAME], ["poclbm120724"], [Filename for poclbm kernel])
AC_DEFINE_UNQUOTED([DIAKGCN_KERNNAME], ["diakgcn120724"], [Filename for diakgcn kernel])
AC_DEFINE_UNQUOTED([DIABLO_KERNNAME], ["diablo120724"], [Filename for diablo kernel])
AC_DEFINE_UNQUOTED([SCRYPT_KERNNAME], ["scrypt120724"], [Filename for scrypt kernel])


AC_SUBST(JANSSON_LIBS)
AC_SUBST(PTHREAD_FLAGS)
AC_SUBST(DLOPEN_FLAGS)
AC_SUBST(PTHREAD_LIBS)
AC_SUBST(NCURSES_LIBS)
AC_SUBST(PDCURSES_LIBS)
AC_SUBST(WS2_LIBS)
AC_SUBST(TIMER_LIBS)
AC_SUBST(MATH_LIBS)
AC_SUBST(UDEV_LIBS)
AC_SUBST(USB_LIBS)
AC_SUBST(USB_FLAGS)

AC_CONFIG_FILES([
	Makefile
	compat/Makefile
	compat/jansson/Makefile
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

if test "x$bitforce" = xyes; then
	echo "  BitForce.FPGAs.......: Enabled"
else
	echo "  BitForce.FPGAs.......: Disabled"
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

if test "x$ztex" = xyes; then
	echo "  Ztex.FPGAs...........: Enabled"
elif test "x$ztexwarn" = xyes; then
	echo "  Ztex.FPGAs...........: Disabled (libusb not found)"
else
	echo "  Ztex.FPGAs...........: Disabled"
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
echo "  CPPFLAGS.............: $CPPFLAGS"
echo "  CFLAGS...............: $CFLAGS"
echo "  LDFLAGS..............: $LDFLAGS $PTHREAD_FLAGS $USB_FLAGS"
echo "  LDADD................: $DLOPEN_FLAGS $LIBCURL_LIBS $JANSSON_LIBS $PTHREAD_LIBS $NCURSES_LIBS $PDCURSES_LIBS $WS2_LIBS $TIMER_LIBS $MATH_LIBS $UDEV_LIBS $USB_LIBS"
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
echo

