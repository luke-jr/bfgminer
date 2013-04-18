#!/bin/sh
bs_dir="$(dirname $(readlink -f $0))"
rm -rf "${bs_dir}"/autom4te.cache
rm -f "${bs_dir}"/aclocal.m4 "${bs_dir}"/ltmain.sh

echo 'Running autoreconf -if...'
autoreconf -if ${AC_FLAGS} "${bs_dir}" || exit 1
if test -z "$NOCONFIGURE" ; then
	echo 'Configuring...'
	"$bs_dir"/configure "$@"
fi
