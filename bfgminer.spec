Summary: 	A bitcoin miner
Name: 		bfgminer
Version: 	3.0.2
Release: 	0%{?dist}
License: 	GPL
Group:		Applications/System
Source: 	http://luke.dashjr.org/programs/bitcoin/files/bfgminer/%{version}/bfgminer-%{version}.tbz2
Url: 		https://bitcointalk.org/?topic=78192
BuildRoot:  	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires:	autoconf, automake, libtool, jansson-devel, git, libcurl-devel
BuildRequires:	libusb1-devel, libudev-devel, yasm-devel, ncurses-devel

%description
This is a multi-threaded multi-pool GPU, FPGA and CPU miner with ATI GPU
monitoring, (over)clocking and fanspeed support for bitcoin and derivative
coins.

%prep
%setup -n bfgminer-%{version}


%build
./autogen.sh
./configure --prefix=%{_prefix} --bindir=%{_bindir} --libdir=%{_libdir} --includedir=%{_includedir} --enable-ztex --enable-bitforce --enable-icarus --enable-cpumining

%install
rm -rf $RPM_BUILD_ROOT/*
make DESTDIR=$RPM_BUILD_ROOT install

%files
%defattr(-,root,root,-)
%{_bindir}/bfgminer
%{_bindir}/bfgminer-rpc
%{_bindir}/bitforce-firmware-flash
%{_bindir}/bitstreams/COPYING_fpgaminer
%{_bindir}/bitstreams/COPYING_ztex
%{_bindir}/bitstreams/fpgaminer_x6500-overclocker-0402.bit
%{_bindir}/bitstreams/ztex_ufm1_15b1.bit
%{_bindir}/bitstreams/ztex_ufm1_15d1.bit
%{_bindir}/bitstreams/ztex_ufm1_15d3.bit
%{_bindir}/bitstreams/ztex_ufm1_15d4.bin
%{_bindir}/bitstreams/ztex_ufm1_15d4.bit
%{_bindir}/bitstreams/ztex_ufm1_15y1.bin
%{_bindir}/bitstreams/ztex_ufm1_15y1.bit
%{_bindir}/diablo121016.cl
%{_bindir}/diakgcn121016.cl
%{_bindir}/phatk121016.cl
%{_bindir}/poclbm121016.cl
%{_bindir}/scrypt121016.cl
%{_includedir}/libblkmaker-0.1/blkmaker.h
%{_includedir}/libblkmaker-0.1/blktemplate.h
%{_libdir}/libblkmaker-0.1.la
%{_libdir}/libblkmaker-0.1.so
%{_libdir}/libblkmaker-0.1.so.0
%{_libdir}/libblkmaker-0.1.so.0.3.1
%{_libdir}/libblkmaker_jansson-0.1.la
%{_libdir}/libblkmaker_jansson-0.1.so
%{_libdir}/libblkmaker_jansson-0.1.so.0
%{_libdir}/libblkmaker_jansson-0.1.so.0.3.1
%{_libdir}/pkgconfig/libblkmaker_jansson-0.1.pc

%changelog
* Sun Mar 24 2013 Arnoud Vermeer <rpms@freshway.biz> 3.0.2-0
- Initial packaging