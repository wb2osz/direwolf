#%global git_commit b2548ec58f44f4b651626757a166b9f4f18d8000
%global git_commit 37179479caf0bf36adf8c9bc0fde641884edaeac
%global git_date 20171216

%global git_short_commit %(echo %{git_commit} | cut -c -8)
%global git_suffix %{git_date}git%{git_short_commit}

Name:           direwolf
Version:        1.5Beta
Release:        1.%{git_suffix}%{?dist}
Summary:        Soundcard based AX.25 TNC

Group:          Applications/Communications
License:        GPLv2
URL:            https://github.com/wb2osz/direwolf
#Source0:        https://github.com/wb2osz/direwolf/archive/%{name}-%{version}.tar.gz
Source:         %{name}-%{version}-%{git_suffix}.tgz
Packager:       David Ranch (KI6ZHD) <dranch@trinnet.net>
Distribution:   RedHat Linux

Patch0:         direwolf-1.5-makefile.patch

BuildRequires:  automake
BuildRequires:  alsa-lib-devel

#If the gpsd and gpsd-devel packages are installed, Direwolf will add gps support



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

make -f Makefile.linux
#make -f Makefile.linux tocalls-symbols
make %{?_smp_mflags}


%install
make install INSTALLDIR=$RPM_BUILD_ROOT/usr
make install-conf INSTALLDIR=$RPM_BUILD_ROOT/usr

# Install icon
mkdir -p ${RPM_BUILD_ROOT}%{_datadir}/pixmaps/direwolf/
cp dw-icon.png ${RPM_BUILD_ROOT}%{_datadir}/pixmaps/direwolf/
mv symbols-new.txt ${RPM_BUILD_ROOT}%{_docdir}/%{name}/
mv symbolsX.txt ${RPM_BUILD_ROOT}%{_docdir}/%{name}/
mv tocalls.txt ${RPM_BUILD_ROOT}%{_docdir}/%{name}/
desktop-file-install \
        --dir=${RPM_BUILD_ROOT}%{_datadir}/applications direwolf.desktop
#temp bug
#non echo "fixing $RPM_BUILD_ROOT/%{_bindir}/bin"
#non rm -f $RPM_BUILD_ROOT/usr/bin


%files
%{_sysconfdir}/ax25/direwolf.conf
%{_sysconfdir}/ax25/sdr.conf
%{_sysconfdir}/ax25/telemetry-toolkit/telem-balloon.conf
%{_sysconfdir}/ax25/telemetry-toolkit/telem-m0xer-3.txt
%{_sysconfdir}/ax25/telemetry-toolkit/telem-volts.conf
%{_sysconfdir}/udev/rules.d/99-direwolf-cmedia.rules
%{_bindir}/* 
%{_datadir}/pixmaps/direwolf/dw-icon.png
%{_datadir}/applications/%{name}.desktop
%{_datadir}/direwolf/*
%{_docdir}/*
%{_mandir}/man1/*



%changelog
* Sat Dec 16 2017 David Ranch <dranch@trinnet.net> - 1.5-1
- New 1.5-Beta version from Git 
* Sun Apr 2 2017 David Ranch <dranch@trinnet.net> - 1.4-1
- New 1.4-Beta1 version from Git 
* Sun Mar 5 2017 David Ranch <dranch@trinnet.net> - 1.4-1
- New 1.4-H Alpha version from Git version
* Fri Aug 26 2016 David Ranch <dranch@trinnet.net> - 1.4-1
- New version
* Fri May 06 2016 David Ranch <dranch@trinnet.net> - 1.3-1
- New version
* Sat Sep 12 2015 David Ranch <dranch@trinnet.net> - 1.3F-1
- New version with new features
* Sun May 10 2015 David Ranch <dranch@trinnet.net> - 1.2E-1
- New version that supports a PASSALL function
- Updated the Makefile.linux patch
* Sat Mar 21 2015 David Ranch <dranch@trinnet.net> - 1.2C-1
- changed to support different make installation variable
* Sat Feb 14 2015 David Ranch <dranch@trinnet.net> - 1.2b-1
- new spec file
* Sat Dec 20 2014 David Ranch <dranch@trinnet.net> - 1.1b1-1
- new spec file
