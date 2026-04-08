%define DIRNAME smartmet-server-compare
%define SPECNAME smartmet-server-compare

Name: %{SPECNAME}
Version: 26.4.8
Release: 2%{?dist}.fmi
Summary: SmartMet Server comparison tool
License: MIT
URL: https://github.com/fmidev/smartmet-server-compare
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires: rpm-build
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: smartmet-library-spine-devel
BuildRequires: smartmet-library-macgyver-devel
BuildRequires: tinyxml2-devel
BuildRequires: jsoncpp-devel
BuildRequires: pkgconf-pkg-config

%if 0%{?rhel} >= 10
BuildRequires: gtkmm3.0-devel
Requires: gtkmm3.0
%else
BuildRequires: gtkmm30-devel
Requires: gtkmm30
%endif

Requires: smartmet-library-spine
Requires: smartmet-library-macgyver
Requires: tinyxml2
Requires: jsoncpp
Requires: adwaita-icon-theme

%description
SmartMet Server Comparison Tool for finding differences in responses between configured servers.

%prep
%setup -q -n %{DIRNAME}

%build
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT%{_bindir}
install -m 755 smartmet-server-compare $RPM_BUILD_ROOT%{_bindir}/

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{_bindir}/smartmet-server-compare

%changelog
* Wed Apr  8 2026 Andris Pavenis <andris.pavenis@fmi.fi> - 26.4.8-2
- Initial version
