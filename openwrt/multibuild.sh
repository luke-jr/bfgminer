#!/bin/bash
# Copyright 2013 Luke Dashjr
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option)
# any later version.  See COPYING for more details.

set -e
set -x
reporoot="$1"  # .../files/bfgminer/BFGMINER_VERSION/openwrt/OPENWRT_VERSION
test -n "$reporoot"
reporoot="$(realpath "$reporoot")"
test -n "$reporoot"
cd "openwrt-src/"
test -d "$reporoot"
vcfgdir='vanilla_configs'
vcfglist="$(
	ls -d "$vcfgdir"/*.config* |
	 perl -ple 's[.*/][]' |
	 sort -n
)"
BITSTREAM_PKG_PATH='../../../../bitstreams/openwrt/'  # Relative to reporoot
BITSTREAMS=(
	fpgaminer_402-1
	ztex-ufm1_15b1_121126-1
	ztex-ufm1_15d4_121126-1
	ztex-ufm1_15y1_121126-1
)

if [ -d "${reporoot}/${BITSTREAM_PKG_PATH}" ]; then
(
	for bs in ${BITSTREAMS[@]}; do
		if ! [ -r "${reporoot}/${BITSTREAM_PKG_PATH}/bitstream-${bs}_all.ipk" ]; then
			echo "Cannot find ${bs} bitstream package" >&2
			exit 1
		fi
	done
)
else
	echo 'Cannot find bitstreams directory' >&2
	exit 1
fi

plat1=''
for cfn in $vcfglist; do
	plat="$(perl -ple 's/^(\d+)\.config\.(\w+?)_\w+$/$2/ or $_=""' <<<"$cfn")"
	test -n "$plat" ||
		continue
	platlist+=("$plat")
	cp -v "$vcfgdir/$cfn" .config
	yes '' | make oldconfig
	make {tools,toolchain}/install package/bfgminer/{clean,compile}
	mkdir "$reporoot/$plat" -pv
	cp -v "bin/$plat/packages/"bfgminer*_${plat}.ipk "$reporoot/$plat/"
	if [ -d "$reporoot/${BITSTREAM_PKG_PATH}" ]; then
	(
		cd "$reporoot/$plat"
		for bs in ${BITSTREAMS[@]}; do
			ln -vfs "../${BITSTREAM_PKG_PATH}/bitstream-${bs}_all.ipk" .
		done
	)
	fi
	staging_dir/host/bin/ipkg-make-index "$reporoot/$plat/" > "$reporoot/$plat/Packages"
	gzip -9 < "$reporoot/$plat/Packages" > "$reporoot/$plat/Packages.gz"
done
