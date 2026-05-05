%define DIRNAME smartmet-server-compare
%define SPECNAME smartmet-server-compare

Name: %{SPECNAME}
Version: 26.5.5
Release: 2%{?dist}.fmi
Summary: SmartMet Server comparison tool
License: MIT
URL: https://github.com/fmidev/smartmet-server-compare
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires: rpm-build
BuildRequires: autoconf
BuildRequires: automake
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: tinyxml2-devel
BuildRequires: jsoncpp-devel
BuildRequires: libcurl-devel
BuildRequires: pkgconf-pkg-config
BuildRequires: ImageMagick-c++-devel

%if 0%{?rhel} >= 10
BuildRequires: gtkmm3.0-devel
Requires: gtkmm3.0
%else
BuildRequires: gtkmm30-devel
Requires: gtkmm30
%endif

Requires: tinyxml2
Requires: jsoncpp
Requires: libcurl
Requires: ImageMagick-c++
Requires: adwaita-icon-theme

%description
SmartMet Server Comparison Tool for finding differences in responses between configured servers.

%prep
%setup -q -n %{DIRNAME}
autoreconf --install --force

%build
%configure
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
%make_install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{_bindir}/smartmet-server-compare

%changelog
* Tue May 5 2026 Andris Pavenis <andris.pavenis@fmi.fi> 26.5.5-2.fmi
- Added HTTP status filter

* Wed Apr 29 2026 Andris Pavēnis <andris.pavenis@fmi.fi> 26.4.29-1.fmi
- Support images. Many other improvements

* Fri Apr 17 2026 Andris Pavenis <andris.pavenis@fmi.fi> - 26.4.17-1
- Replace smartmet-library-spine with libcurl; add HTTPS support
- Changed to use GNU autotools build system

* Wed Apr  8 2026 Andris Pavenis <andris.pavenis@fmi.fi> - 26.4.8-2
- Initial version
