#!/bin/bash

bs_dir="$(dirname $(readlink -f $0))"
build_dir="$PWD"
rm -rf "${bs_dir}"/autom4te.cache
rm -f "${bs_dir}"/aclocal.m4 "${bs_dir}"/ltmain.sh

echo 'Running autoreconf -ifv...'
autoreconf -ifv -I "/usr/local/share/aclocal/" "$bs_dir" || exit 1

if test -z "$NOCONFIGURE" ; then
   echo 'Configuring...'

   if [[ "$bs_dir" != "`pwd`" ]]; then
      export CPPFLAGS+=" -I $bs_dir"
   fi

   if [[ ! -z "$CGMINER_SDK" ]]; then
      export CPPFLAGS="-I $CGMINER_SDK/include $CPPFLAGS"
      export LDFLAGS="-L $CGMINER_SDK/lib $LDFLAGS"
      export PKG_CONFIG_PATH="$CGMINER_SDK/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
      export ADL_SDK="$CGMINER_SDK/include/ADL_SDK"
   fi

   CFLAGS="-O3 -msse2" \
   "$bs_dir"/configure \
      --prefix="$build_dir"/opt \
      --enable-cpumining \
      --enable-scrypt \
      --enable-bitforce \
      --enable-icarus \
      --enable-modminer \
      --enable-ztex \
      $@
fi
