Name: sandbox-spawner
Version: 1.0.0
Release: 1%{?dist}
Summary: Server for spawning SLATE sandboxes
License: MIT
URL: https://github.com/slateci/sandbox-spawner

Source0: %{name}-%{version}.tar.gz

BuildRequires: gcc-c++ boost-devel zlib-devel openssl-devel libcurl-devel cmake3
Requires: boost zlib openssl libcurl

%description
SLATE API Server

%prep
%setup -c -n %{name}-%{version}.tar.gz 

%build
cd %{name}
mkdir build
cd build
cmake3 -DCMAKE_INSTALL_PREFIX="$RPM_BUILD_ROOT/usr/" ..
make -j3

%install
cd %{name}
cd build
rm -rf "$RPM_BUILD_ROOT"
echo "$RPM_BUILD_ROOT"
make install
cd ..

%clean
rm -rf "$RPM_BUILD_ROOT"

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%files
%{_bindir}/sandbox-spawner

%defattr(-,root,root,-)
