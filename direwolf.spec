Name:           direwolf
Version:        1.1b1
Release:        1%{?dist}
Summary:        Soundcard based AX.25 TNC

Group:          Applications/Communications
License:        GPLv2
URL:            http://home.comcast.net/~wb2osz
Source0:        http://home.comcast.net/~wb2osz/Version%201.1/direwolf-%{version}.tgz
Packager:       David Ranch (KI6ZHD) <dranch@trinnet.net>
Distribution:   RedHat Linux

Patch0:         direwolf-makefile7.patch

BuildRequires:  automake
BuildRequires:  alsa-lib-devel


%description
Dire Wolf is a software "soundcard" modem/TNC and APRS encoder/decoder.   It can 
be used stand-alone to receive APRS messages, as a digipeater, APRStt gateway, 
or Internet Gateway (IGate).    It can also be used as a virtual TNC for other 
applications such as APRSIS32, UI-View32, Xastir, APRS-TW, YAAC, UISS, 
Linux AX25, SARTrack, RMS Express, and many others.

%prep

%setup -q -n %{name}-%{version}
%patch0 -p0

%build 
make -f Makefile.linux tocalls-symbols
make %{?_smp_mflags} -f Makefile.linux


%install
make -f Makefile.linux install DESTDIR=$RPM_BUILD_ROOT
make -f Makefile.linux install-conf DESTDIR=$RPM_BUILD_ROOT

# Install icon
mkdir -p ${RPM_BUILD_ROOT}%{_datadir}/pixmaps/
cp dw-icon.png ${RPM_BUILD_ROOT}%{_datadir}/pixmaps/
mv symbols-new.txt ${RPM_BUILD_ROOT}%{_docdir}/%{name}/
mv symbolsX.txt ${RPM_BUILD_ROOT}%{_docdir}/%{name}/
mv tocalls.txt ${RPM_BUILD_ROOT}%{_docdir}/%{name}/
desktop-file-install \
        --dir=${RPM_BUILD_ROOT}%{_datadir}/applications direwolf.desktop


%files
%{_sysconfdir}/ax25/direwolf.conf
%{_bindir}/* 
%{_datadir}/pixmaps/dw-icon.png
%{_datadir}/applications/%{name}.desktop
%{_datadir}/direwolf/*
%{_docdir}/%{name}/*



%changelog
* Sat Dec 20 2014 David Ranch <dranch@trinnet.net> - 1.1b1-1
- new spec file
