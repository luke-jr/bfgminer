#!/bin/sh
bs_dir="$(dirname $(readlink -f $0))"
rm -rf "${bs_dir}"/autom4te.cache
rm -f "${bs_dir}"/aclocal.m4 "${bs_dir}"/ltmain.sh

libusb_dir="${bs_dir}"/compat/libusb-1.0/
rm -rf "${libusb_dir}"/autom4te.cache
rm -rf "${libusb_dir}"/aclocal.m4 "${libusb_dir}"/ltmain.sh

echo 'Running autoreconf -if...'
aclocal --force -I m4
libtoolize --install --copy --force
autoheader --force
automake --add-missing --copy --force-missing
autoconf --force

autoreconf -fi "${libusb_dir}"

if test -z "$NOCONFIGURE" ; then
	echo 'Configuring...'
	"$bs_dir"/configure "$@"
fi
