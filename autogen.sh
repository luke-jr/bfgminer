#!/bin/sh
bs_dir="$(dirname $(readlink -f $0))"
rm -rf "${bs_dir}"/autom4te.cache "${bs_dir}"/aclocal.m4 "${bs_dir}"/ltmain.sh

autoreconf -fi "${bs_dir}"

if test -z "$NOCONFIGURE" ; then
	echo 'Configuring...'
	"$bs_dir"/configure "$@"
fi
