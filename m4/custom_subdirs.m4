dnl * Copyright 2014 Luke Dashjr
dnl *
dnl * This program is free software; you can redistribute it and/or modify it
dnl * under the terms of the GNU General Public License as published by the Free
dnl * Software Foundation; either version 3 of the License, or (at your option)
dnl * any later version.  See COPYING for more details.

m4_divert_text([DEFAULTS], [
custom_subdirs=
])

AC_DEFUN([BFG_CUSTOM_SUBDIR],[
	if false; then
		AC_CONFIG_SUBDIRS([$1])
	fi
	custom_subdirs="$custom_subdirs $1"
	custom_subdir_[]AS_TR_SH([$1])_args="$2"
	custom_subdir_[]AS_TR_SH([$1])_forceargs="$3"
])

AC_DEFUN([BFG_CUSTOM_SUBDIRS_OUTPUT],[
	if test "$no_recursion" != yes; then
		orig_subdirs="$subdirs"
		orig_ac_configure_args="$ac_configure_args"
		for custom_subdir in $custom_subdirs; do
			subdirs="$custom_subdir"
			custom_subdir_base="AS_TR_SH([$custom_subdir])"
			eval 'ac_configure_args="$custom_subdir_'"$custom_subdir_base"'_args $orig_ac_configure_args $custom_subdir_'"$custom_subdir_base"'_forceargs"'
			_AC_OUTPUT_SUBDIRS
		done
		subdirs="$orig_subdirs"
		ac_configure_args="$orig_ac_configure_args"
	fi
])
