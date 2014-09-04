#!/bin/sh -e
# Written by Luke Dashjr in 2012-2014
# This program is released under the terms of the Creative Commons "CC0 1.0 Universal" license and/or copyright waiver.

bs_dir="$(dirname "$0")"

if test -z "$NOSUBMODULES" ; then
	echo 'Getting submodules...'
	
	# Older versions had INSTALL in git; remove it so git can update cleanly
	rm -f libblkmaker/INSTALL
	
	(
		cd "${bs_dir}"
		git submodule update --init
	)
fi

echo 'Running autoreconf -if...'
(
	cd "${bs_dir}"
	rm -rf autom4te.cache
	rm -f aclocal.m4 ltmain.sh
	autoreconf -if ${AC_FLAGS}
)

echo 'Updating version.h...'
if ./gen-version.sh >version.h.new; then
	cmp version.h version.h.new && rm version.h.new || mv version.h.new version.h
fi
