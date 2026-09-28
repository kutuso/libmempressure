Name:       libmempressure
Version:    0.2.0
Release:    1%{?dist}
Summary:    Memory-pressure events from kernel PSI for C and C++ applications

License:    MIT
URL:        https://github.com/kutuso/libmempressure
Source0:    libmempressure-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make

%description
libmempressure turns the kernel's Pressure Stall Information (PSI) into
application-level memory-pressure events: a Linux answer to Android's
onTrimMemory(). Instead of being killed by the OOM killer, applications
learn that memory is getting tight and shed caches gracefully.

This package ships the shared library (C API plus the header-only C++
binding). Python and JVM bindings are built from the same source with
their language toolchains present.

%package devel
Summary:    Development files for libmempressure
Requires:   %{name}%{?_isa} = %{version}-%{release}

%description devel
Headers, pkg-config file and the versioned symlink needed to build
applications against libmempressure.

%prep
%autosetup -n libmempressure-%{version}

%build
%cmake -DMP_PYTHON=OFF -DMP_JAVA=OFF
%cmake_build

%install
%cmake_install

%check
%ctest

%files
%license LICENSE
%{_libdir}/libmempressure.so.0*

%files devel
%{_includedir}/mempressure.h
%{_includedir}/mempressure.hpp
%{_libdir}/libmempressure.so
%{_libdir}/pkgconfig/mempressure.pc

%changelog

* Mon Sep 28 2026 kutu OS contributors <release@kutu.so> - 0.2.0-1
- Quiescing unsubscribe, shutdown ordering, config/source validation, python gil fix, jni registry cleanup

* Wed Sep 09 2026 kutu OS contributors <release@kutu.so> - 0.1.0-1
- Initial package.
