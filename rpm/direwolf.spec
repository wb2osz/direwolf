%global shorttag 0d2c175c
Name:           direwolf
Version:        1.6
Release:        0.4.20200419git%{shorttag}%{?dist}
Summary:        Sound Card-based AX.25 TNC

License:        GPLv2+
URL:            https://github.com/wb2osz/direwolf/
Source0:        https://github.com/wb2osz/direwolf/archive/%{version}/%{name}-%{version}.tar.gz
#Source0:        https://github.com/wb2osz/direwolf/archive/%{version}/%{name}-%{shorttag}.tar.gz

BuildRequires:  gcc gcc-c++
BuildRequires:  cmake
BuildRequires:  glibc-devel
BuildRequires:  alsa-lib-devel
BuildRequires:  gpsd-devel
BuildRequires:  hamlib-devel
BuildRequires:  systemd systemd-devel
Requires:       ax25-tools ax25-apps
Requires(pre):  shadow-utils


%description
Dire Wolf is a modern software replacement for the old 1980's style
TNC built with special hardware.  Without any additional software, it
can perform as an APRS GPS Tracker, Digipeater, Internet Gateway
(IGate), APRStt gateway. It can also be used as a virtual TNC for
other applications such as APRSIS32, UI-View32, Xastir, APRS-TW, YAAC,
UISS, Linux AX25, SARTrack, Winlink Express, BPQ32, Outpost PM, and many
others.


%prep
%autosetup -n %{name}-%{version}


%build
%cmake -DUNITTEST=1 -DENABLE_GENERIC=1 .


%check
ctest -V %{?_smp_mflags}


%install
%make_install

# Install service file
mkdir -p ${RPM_BUILD_ROOT}%{_unitdir}
cp %{_builddir}/%{buildsubdir}/systemd/%{name}.service ${RPM_BUILD_ROOT}%{_unitdir}/%{name}.service

# Install service config file
mkdir -p ${RPM_BUILD_ROOT}%{_sysconfdir}/sysconfig
cp %{_builddir}/%{buildsubdir}/systemd/%{name}.sysconfig ${RPM_BUILD_ROOT}%{_sysconfdir}/sysconfig/%{name}

# Install logrotate config file
mkdir -p ${RPM_BUILD_ROOT}%{_sysconfdir}/logrotate.d
cp %{_builddir}/%{buildsubdir}/systemd/%{name}.logrotate ${RPM_BUILD_ROOT}%{_sysconfdir}/logrotate.d/%{name}

# copy config file
cp ${RPM_BUILD_ROOT}%{_pkgdocdir}/conf/%{name}.conf ${RPM_BUILD_ROOT}/%{_sysconfdir}/%{name}.conf

# Make log directory
mkdir -m 0755 -p ${RPM_BUILD_ROOT}/var/log/%{name}

# Move udev rules to system dir
mkdir -p ${RPM_BUILD_ROOT}%{_udevrulesdir}
mv ${RPM_BUILD_ROOT}%{_sysconfdir}/udev/rules.d/99-direwolf-cmedia.rules ${RPM_BUILD_ROOT}%{_udevrulesdir}/99-direwolf-cmedia.rules

# Copy doc pngs
cp direwolf-block-diagram.png ${RPM_BUILD_ROOT}%{_pkgdocdir}/direwolf-block-diagram.png
cp tnc-test-cd-results.png    ${RPM_BUILD_ROOT}%{_pkgdocdir}/tnc-test-cd-results.png

# remove extraneous files
# This is not a desktop application, per the guidelines.  Running it in a terminal
# does not make it a desktop application.
rm ${RPM_BUILD_ROOT}/usr/share/applications/direwolf.desktop
rm ${RPM_BUILD_ROOT}%{_datadir}/pixmaps/direwolf_icon.png
rm ${RPM_BUILD_ROOT}%{_pkgdocdir}/CHANGES.md
rm ${RPM_BUILD_ROOT}%{_pkgdocdir}/LICENSE
rm ${RPM_BUILD_ROOT}%{_pkgdocdir}/README.md

# remove Windows external library directories
rm -r ${RPM_BUILD_ROOT}%{_pkgdocdir}/external

# Move Telemetry Toolkit sample scripts into docs
mkdir -p ${RPM_BUILD_ROOT}%{_pkgdocdir}/telem/
mv ${RPM_BUILD_ROOT}%{_bindir}/telem* ${RPM_BUILD_ROOT}%{_pkgdocdir}/telem/
chmod 0644 ${RPM_BUILD_ROOT}%{_pkgdocdir}/telem/*


%package -n %{name}-doc
Summary:        Documentation for Dire Wolf
BuildArch:      noarch
Requires:       %{name} = %{version}-%{release}

%description -n %{name}-doc
Dire Wolf is a modern software replacement for the old 1980's style
TNC built with special hardware.  Without any additional software, it
can perform as an APRS GPS Tracker, Digipeater, Internet Gateway
(IGate), APRStt gateway. It can also be used as a virtual TNC for
other applications such as APRSIS32, UI-View32, Xastir, APRS-TW, YAAC,
UISS, Linux AX25, SARTrack, RMS Express, BPQ32, Outpost PM, and many
others.


%files
%license LICENSE
%{_udevrulesdir}/99-direwolf-cmedia.rules
%{_bindir}/* 
%{_mandir}/man1/*
%{_datadir}/%{name}/*
%dir %{_pkgdocdir}
%{_pkgdocdir}/conf/*
%{_pkgdocdir}/scripts/*
%{_pkgdocdir}/telem/*
%{_unitdir}/%{name}.service
%config(noreplace) %attr(0644,root,root) %{_sysconfdir}/sysconfig/%{name}
%config(noreplace) %attr(0644,root,root) %{_sysconfdir}/%{name}.conf
%config(noreplace) %attr(0644,root,root) %{_sysconfdir}/logrotate.d/%{name}
%dir %attr(0755, %{name}, %{name}) /var/log/%{name}

%files -n %{name}-doc
%{_pkgdocdir}/*.pdf
%{_pkgdocdir}/*.png

# At install, create a user in group audio (so can open sound card device files)
# and in group dialout (so can open serial device files)
%pre
getent group direwolf >/dev/null || groupadd -r direwolf
getent passwd direwolf >/dev/null || \
    useradd -r -g audio -G audio,dialout -d %{_datadir}/%{name} -s /sbin/nologin \
	    -c "Direwolf Sound Card-based AX.25 TNC" direwolf
exit 0


%changelog
* Mon Apr 20 2020 Matt Domsch <matt@domsch.com> - 1.6-0.3
- drop unneeded BR libax25-devel

* Mon Apr 20 2020 Matt Domsch <matt@domsch.com> - 1.6-0.2
- write stdout/err to /var/log/direwolf, logrotate 30 days.
- run ctest
- remove CPU instruction tests, leave architecture choice up to the distro

* Sun Apr 19 2020 Matt Domsch <matt@domsch.com> - 1.6-0.1
- upstream 1.6 prerelease
- drop obsolete patches, use cmake
- add systemd startup, direwolf user

* Tue Mar 31 2020 Richard Shaw <hobbes1069@gmail.com> - 1.5-6
- Rebuild for hamlib 4.

* Thu Feb 20 2020 Matt Domsch <matt@domcsh.com> - 1.5-5
- Remove unneeded dependency on python2-devel (#1805225)

* Tue Jan 28 2020 Fedora Release Engineering <releng@fedoraproject.org> - 1.5-4
- Rebuilt for https://fedoraproject.org/wiki/Fedora_32_Mass_Rebuild

* Wed Jul 24 2019 Fedora Release Engineering <releng@fedoraproject.org> - 1.5-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_31_Mass_Rebuild

* Wed Jul 03 2019 Björn Esser <besser82@fedoraproject.org> - 1.5-2
- Rebuild (gpsd)

* Sun Feb 17 2019 Matt Domsch <matt@domsch.com> - 1.5-1
- Upgrade to released version 1.5
- Apply upstream patch for newer gpsd API

* Thu Jan 31 2019 Fedora Release Engineering <releng@fedoraproject.org> - 1.5-0.2.beta4
- Rebuilt for https://fedoraproject.org/wiki/Fedora_30_Mass_Rebuild

* Mon Aug 27 2018 Matt Domsch <matt@domsch.com> - 1.5-0.1.beta4
- Fedora Packaging Guidelines, based on spec by David Ranch
  Moved Telemetry Toolkit examples into examples/ docs.
