dnl * Copyright 2014 Luke Dashjr
dnl *
dnl * This program is free software; you can redistribute it and/or modify it
dnl * under the terms of the GNU General Public License as published by the Free
dnl * Software Foundation; either version 3 of the License, or (at your option)
dnl * any later version.  See COPYING for more details.

m4_divert_text([DEFAULTS], [
origin_LDFLAGS=
origin_LDFLAGS_checked=false
maybe_ldconfig=
maybe_ldconfig_checked=false
BUNDLED_LIB_RULES=
])

AC_SUBST(BUNDLED_LIB_RULES)

AC_DEFUN([BFG_CHECK_LD_ORIGIN],[
if ! $origin_LDFLAGS_checked; then
	save_LDFLAGS="$LDFLAGS"
	LDFLAGS="$LDFLAGS -Wl,-zorigin"
	AC_MSG_CHECKING([whether the linker recognizes the -zorigin option])
	AC_TRY_LINK([],[],[
		AC_MSG_RESULT([yes])
		origin_LDFLAGS=',-zorigin'
	],[
		AC_MSG_RESULT([no])
	])
	LDFLAGS="$save_LDFLAGS"
	origin_LDFLAGS_checked=true
fi
])

AC_DEFUN([BFG_CHECK_LDCONFIG],[
if ! $maybe_ldconfig_checked; then
	_ROOTPATH=$PATH$PATH_SEPARATOR`echo $PATH | sed s/bin/sbin/g`
	possible_ldconfigs="${target}-ldconfig"
	if test "x$cross_compiling" != "xyes"; then
		possible_ldconfigs="${possible_ldconfigs} ldconfig"
	fi
	AC_CHECK_PROGS([LDCONFIG],[${possible_ldconfigs}],[],[$_ROOTPATH])
	if test "x$LDCONFIG" != "x"; then
		maybe_ldconfig=" && $LDCONFIG"
	fi
	maybe_ldconfig_checked=true
fi
])

AC_DEFUN([BFG_BUNDLED_LIB_VARS],[
	BFG_CHECK_LD_ORIGIN
	_AC_SRCDIRS(["$ac_dir"])
	$1_CFLAGS='-I'"${ac_abs_top_srcdir}"'/$2'
	$1_LIBS='-L'"${ac_abs_top_srcdir}"'/$2/.libs -Wl,-rpath,\$$ORIGIN/$2/.libs'"$origin_LDFLAGS"' m4_foreach_w([mylib],[$3],[ -l[]mylib])'
	$1_SUBDIRS=$2
	$1_EXTRADEPS=$1_directory
	BUNDLED_LIB_RULES="$BUNDLED_LIB_RULES
$1_directory:
	\$(MAKE) -C $2
"
	AM_SUBST_NOTMAKE([BUNDLED_LIB_RULES])
	if $have_cygwin; then
		$1_EXTRADEPS="$$1_EXTRADEPS m4_foreach_w([mylib],[$3],[ cyg[]mylib[]-0.dll])"
		BUNDLED_LIB_RULES="$BUNDLED_LIB_RULES[]m4_foreach_w([mylib],[$3],[
cyg[]mylib[]-%.dll: $2/.libs/cyg[]mylib[]-%.dll
	cp -p \$< \$[]@
])"
	fi
])

dnl BFG_BUNDLED_LIB([PKG-NAME],PKGCONF-NAME],[DEFAULT:YES/NO/AUTO],[PATH],[LIBS],[DEPENDENT-PKGS],[CONFIGURE-ARGS],[CONFIGURE-ARGS])
AC_DEFUN([BFG_BUNDLED_LIB],[
	AC_ARG_WITH([system-$1],[ifelse([$3],[no],AC_HELP_STRING([--with-system-$1], [Use system $1 rather than bundled one (default disabled)]),AC_HELP_STRING([--without-system-$1], [Use bundled $1 rather than system one]))],[true],[with_system_$1=$3])
	if test "x$with_system_$1" != "xno"; then
		PKG_CHECK_MODULES([$1],[$2],[
			with_system_$1=yes
		],[
			if test "x$with_system_$1" = "xyes"; then
				AC_MSG_ERROR([Could not find system $1])
			else
				AC_MSG_NOTICE([Didn't find system $1, using bundled copy])
				with_system_$1=no
			fi
		])
	fi
	if test "x$with_system_$1" = "xno"; then
		BFG_BUNDLED_LIB_VARS([$1],[$4],[$5])
		BFG_CUSTOM_SUBDIR([$4],[$7],[$8 m4_foreach_w([mydep],[$6],[ mydep[]_LIBS='$mydep[]_LIBS' mydep[]_CFLAGS='$mydep[]_CFLAGS'])])
		BFG_CHECK_LDCONFIG
	else
		$1_SUBDIRS=
		$1_EXTRADEPS=
	fi
	AC_SUBST($1_CFLAGS)
	AC_SUBST($1_LIBS)
	AC_SUBST($1_SUBDIRS)
	AC_SUBST($1_EXTRADEPS)
	AM_CONDITIONAL(NEED_[]m4_toupper([$1]), [test x$with_system_$1 != xyes])
])
