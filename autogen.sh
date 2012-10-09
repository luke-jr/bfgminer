#!/bin/sh -e
bs_dir="$(dirname "$0")"

if test -z "$NOSUBMODULES" ; then
	echo 'Getting submodules...'
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
	autoreconf -if
)

if test -z "$NOCONFIGURE" ; then
	echo 'Configuring...'
	"${bs_dir}"/configure "$@"
fi
