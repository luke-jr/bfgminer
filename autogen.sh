#!/bin/sh
bs_dir="$(dirname $(readlink -f $0))"
rm -rf "${bs_dir}"/autom4te.cache
rm -f "${bs_dir}"/aclocal.m4 "${bs_dir}"/ltmain.sh

libusb_dir="${bs_dir}"/compat/libusb-1.0/
rm -rf "${libusb_dir}"/autom4te.cache "${libusb_dir}"/aclocal.m4 "${libusb_dir}"/ltmain.sh

jansson_dir="${bs_dir}"/compat/jansson-2.5/
rm -rf "${jansson_dir}"/autom4te.cache "${jansson_dir}"/aclocal.m4 "${jansson_dir}"/ltmain.sh

echo 'Running autoreconf -if...'
aclocal --force -I m4
libtoolize --install --copy --force
autoheader --force
automake --add-missing --copy --force-missing
autoconf --force

autoreconf -fi "${libusb_dir}"
autoreconf -fi "${jansson_dir}"

if test -z "$NOCONFIGURE" ; then
	echo 'Configuring...'
	"$bs_dir"/configure "$@"
fi
