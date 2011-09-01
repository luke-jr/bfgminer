##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_maj], [1])
m4_define([v_min], [6])
m4_define([v_mic], [1])
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_ver], [v_maj.v_min.v_mic])
m4_define([lt_rev], m4_eval(v_maj + v_min))
m4_define([lt_cur], v_mic)
m4_define([lt_age], v_min)
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##

AC_INIT([cgminer], [v_ver], [kernel@kolivas.org])

AC_PREREQ(2.59)
AC_CANONICAL_SYSTEM
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_GNU_SOURCE

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
AC_PROG_CC
gl_EARLY
AC_PROG_GCC_TRADITIONAL
AM_PROG_CC_C_O
AC_PROG_RANLIB

gl_INIT

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(syslog.h)

AC_FUNC_ALLOCA

have_win32=false
PTHREAD_FLAGS="-pthread"
OPENCL_FLAGS="-lOpenCL"
WS2_LIBS=""

case $target in
  x86_64-*)
    have_x86_64=true
    ;;
  *)
    have_x86_64=false
    ;;
esac

case $target in
  *-*-mingw*)
    have_x86_64=false
    have_win32=true
    PTHREAD_FLAGS=""
    WS2_LIBS="-lws2_32"
    ;;
  *-*-darwin*)
    OPENCL_FLAGS="-framework OpenCL"
	;;
esac

AC_ARG_ENABLE([opencl],
	[AC_HELP_STRING([--disable-opencl],[Override detection and disable building with opencl])],
	[opencl=$enableval]
	)
if test "x$opencl" != xno; then
	# Check for OpenCL (the long way needed on mingw32 due to calling conventions)
	AC_MSG_CHECKING([for OpenCL])
	SAVED_LIBS=$LIBS
	LIBS="$LIBS $OPENCL_FLAGS"
	AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([[
		#ifdef __APPLE_CC__
		#include <OpenCL/opencl.h>
		#else
		#include <CL/cl.h>
		#endif
	]],
	[[return clSetKernelArg(0, 0, 0, 0); ]])],
	[AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_OPENCL], [1], [Defined to 1 if OpenCL is present on the system.])
	found_opencl=1
	OPENCL_LIBS=$OPENCL_FLAGS],
	[AC_MSG_RESULT(no)
	found_opencl=0])
	LIBS=$SAVED_LIBS
fi

AC_CHECK_LIB(pthread, pthread_create, ,
        AC_MSG_ERROR([Could not find pthread library - please install libpthread]))
PTHREAD_LIBS=-lpthread

AC_CHECK_LIB(jansson, json_loads, request_jansson=false, request_jansson=true)

AC_SEARCH_LIBS(addstr, ncurses pdcurses, ,
        AC_MSG_ERROR([Could not find curses library - please install libncurses-dev or pdcurses-dev]))

AC_CHECK_LIB(ncurses, addstr, NCURSES_LIBS=-lncurses)
AC_CHECK_LIB(pdcurses, addstr, PDCURSES_LIBS=-lpdcurses)

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

PKG_PROG_PKG_CONFIG()

PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.15.6], [AC_DEFINE([CURL_HAS_SOCKOPT], [1], [Defined if version of curl supports sockopts.])],
[PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.10.1], ,[AC_MSG_ERROR([Missing required libcurl dev >= 7.10.1])])])
AC_SUBST(LIBCURL_CPPFLAGS)
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
	prefix=/usr
fi

AC_DEFINE_UNQUOTED([CGMINER_PREFIX], ["$prefix/bin"], [Path to cgminer install])

AC_SUBST(OPENCL_LIBS)
AC_SUBST(JANSSON_LIBS)
AC_SUBST(PTHREAD_FLAGS)
AC_SUBST(PTHREAD_LIBS)
AC_SUBST(NCURSES_LIBS)
AC_SUBST(PDCURSES_LIBS)
AC_SUBST(WS2_LIBS)

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
if test "x$opencl" != xno; then
	if test $found_opencl = 1; then
		echo "  OpenCL...............: FOUND. GPU mining support enabled"
	else
		echo "  OpenCL...............: NOT FOUND. GPU mining support DISABLED"
	fi
else
	echo "  OpenCL...............: Detection overrided. GPU mining support DISABLED"
fi

echo "  ASM..................: $has_yasm"
echo
echo "Compilation............: make (or gmake)"
echo "  CPPFLAGS.............: $CPPFLAGS"
echo "  CFLAGS...............: $CFLAGS"
echo "  CXXFLAGS.............: $CXXFLAGS"
echo "  LDFLAGS..............: $LDFLAGS"
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
echo

