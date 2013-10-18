#!/bin/sh
bs_dir="$(dirname $(readlink -f $0))"

#Some versions of libtoolize don't like there being no ltmain.sh file already
touch "${bs_dir}"/ltmain.sh
autoreconf -fi "${bs_dir}"

if test -z "$NOCONFIGURE" ; then
	echo 'Configuring...'
	"$bs_dir"/configure "$@"
fi
