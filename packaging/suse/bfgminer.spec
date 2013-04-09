#
# Copyright (c) 2013 Christian Berendt.
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.
#
# Please submit bugfixes or comments via http://bugs.opensuse.org/
#

Name:           bfgminer
Version:        3.0.0
Release:        0
Summary:        A BitCoin miner
License:        GPL-3.0
Group:          Productivity/Other
Url:            https://github.com/luke-jr/bfgminer
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
Source:         http://luke.dashjr.org/programs/bitcoin/files/bfgminer/%{version}/%{name}-%{version}.tbz2
Patch0:         Makefile.in.patch
Patch1:         Makefile.am.patch

BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  pkg-config
BuildRequires:  make
BuildRequires:  gcc
BuildRequires:  yasm
BuildRequires:  libjansson-devel
BuildRequires:  libcurl-devel
BuildRequires:  libusb-devel
BuildRequires:  libudev-devel
BuildRequires:  ncurses-devel

%description
This is a multi-threaded multi-pool FPGA, GPU and CPU miner with ATI GPU
monitoring, (over)clocking and fanspeed support for bitcoin and derivative
coins.

%package devel
Summary:        A BitCoin miner
Group:          Development/Libraries/C and C++
Requires:       %{name} = %{version}-%{release}

%description devel
This is a multi-threaded multi-pool FPGA, GPU and CPU miner with ATI GPU
monitoring, (over)clocking and fanspeed support for bitcoin and derivative
coins.

%prep
%setup -q
%patch0 -p1
%patch1 -p1
%configure \
  --enable-cpumining \
  --enable-scrypt

%build
make %{?_smp_mflags}

%install
%make_install

install -d -m 755 %{buildroot}/%{_datadir}/%{name}
mv %{buildroot}%{_bindir}/*.cl %{buildroot}/%{_datadir}/%{name}
mv %{buildroot}%{_bindir}/bitstreams %{buildroot}/%{_datadir}/%{name}

%clean
rm -rf %{buildroot}

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_bindir}/*
%{_libdir}/*
%dir %{_datadir}/%{name}
%{_datadir}/%{name}/*

%files devel
%defattr(-,root,root,-)
%{_includedir}/*

%changelog
