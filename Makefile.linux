#
# Makefile for Linux/Unix version of Dire Wolf.
#

# Expecting Linux, FreeBSD, or OpenBSD here.
# Would it be feasible to merge Mac OSX back in or has it diverged too much?

OS = $(shell uname)

# Default for Linux.  BSD does things differently.
# See https://github.com/wb2osz/direwolf/pull/92 for FreeBSD considerations.

PREFIX ?= /usr


APPS := direwolf decode_aprs text2tt tt2text ll2utm utm2ll aclients atest log2gpx gen_packets ttcalc kissutil cm108


all :  $(APPS) direwolf.desktop direwolf.conf
	@echo " "
	@echo "Next step - install with:"
	@echo " "
	@echo "        sudo make install"
	@echo " "

# Default to gcc if something else not specified, e.g. clang.

CC ?= gcc


# _XOPEN_SOURCE=600 and _DEFAULT_SOURCE=1 are needed for glibc >= 2.24.
# Explanation here:  https://github.com/wb2osz/direwolf/issues/62

# There are a few source files where it had been necessary to define __USE_XOPEN2KXSI,
# __USE_XOPEN, or _POSIX_C_SOURCE.  Doesn't seem to be needed after adding this.

# -D_BSD_SOURCE because Raspbian wheezy was missing declaration for strsep and definition of fd_set.
# That was not necessary (but did not hurt) for more recent Ubuntu and Raspbian Jessie.

# The first assignment to CFLAGS and LDFLAGS uses +=, rather than :=, so
# we will inherit options already set in build environment.
# Explanation - https://github.com/wb2osz/direwolf/pull/38

# For BSD, these are supplied externally.
# https://github.com/wb2osz/direwolf/pull/92 
# Why don't we just set them differently here?

ifeq ($(OS),Linux)
CFLAGS += -O3 -pthread -Igeotranz -D_XOPEN_SOURCE=600 -D_DEFAULT_SOURCE=1 -D_BSD_SOURCE -Wall
LDFLAGS += -lm -lpthread -lrt
else
CFLAGS ?= -O3 -pthread -Igeotranz -Wall
LDFLAGS ?= -lm -lpthread -lrt
endif



# If the compiler is generating code for the i386 target, we can
# get much better results by telling it we have at least a Pentium 3,
# which has the SSE instructions.
# For a more detailed description, see Dire-Wolf-Developer-Notes.pdf.

arch := $(shell echo | ${CC} -E -dM - | grep __i386__)
ifneq ($(arch),)
CFLAGS += -march=pentium3
endif


# Add -ffast-math option if the compiler recognizes it.
# This makes a big difference with x86_64 but has little impact on 32 bit targets.

useffast := $(shell ${CC} --help -v 2>/dev/null | grep ffast-math)
ifneq ($(useffast),)
CFLAGS += -ffast-math
endif



#
# Dire Wolf is known to work with ARM processors on the BeagleBone, CubieBoard2, CHIP, etc.
# The best compiler options will depend on the specific type of processor
# and the compiler target defaults.   Use the NEON instructions if available.
#

ifeq ($(OS),Linux)
neon := $(shell cat /proc/cpuinfo | grep neon)
ifneq ($(neon),)
CFLAGS += -mfpu=neon
endif
else
neon := $(shell machine | grep armv7)
ifneq ($(neon),)
CFLAGS += -mfloat-abi=hard -mfpu=neon
endif
endif




# Audio system:  We normally want to use ALSA for Linux.
# I heard that OSS will also work with Linux but I never tried it.
# ALSA is not an option for FreeBSD or OpenBSD.

ifeq ($(OS),Linux)
alsa = 1
else
alsa =
endif


# Make sure pthread.h is available.
# We use ${PREFIX}, rather than simply /usr, because BSD has it in some other place.

ifeq ($(wildcard ${PREFIX}/include/pthread.h),)
$(error /usr/include/pthread.h does not exist.  Install it with "sudo apt-get install libc6-dev" or "sudo yum install glibc-headers" )
endif


# Make sure we have required library for ALSA if that option is being used.

ifneq ($(alsa),)
CFLAGS += -DUSE_ALSA
LDFLAGS += -lasound
ifeq ($(wildcard /usr/include/alsa/asoundlib.h),)
$(error /usr/include/alsa/asoundlib.h does not exist.  Install it with "sudo apt-get install libasound2-dev" or "sudo yum install alsa-lib-devel" )
endif
ifeq ($(OS),OpenBSD)
# Use sndio via PortAudio Library (you can install it by pkg_add portaudio-svn)
LDFLAGS += -lportaudio -L/usr/local/lib
CFLAGS += -DUSE_PORTAUDIO -I/usr/local/include
endif
endif



# Enable GPS if header file is present.
# Finding libgps.so* is more difficult because it
# is in different places on different operating systems.

enable_gpsd := $(wildcard ${PREFIX}/include/gps.h)
ifneq ($(enable_gpsd),)
CFLAGS += -DENABLE_GPSD
LDFLAGS += -lgps
endif


# Enable hamlib support if header file is present.

enable_hamlib := $(wildcard /usr/include/hamlib/rig.h /usr/local/include/hamlib/rig.h)
ifneq ($(enable_hamlib),)
CFLAGS += -DUSE_HAMLIB
LDFLAGS += -lhamlib
endif


# Should enabling of this feature be strongly encouraged or
# is it quite specialized and of interest to a small audience?
# If, for some reason, can't obtain the libudev-dev package, or
# don't want to install it, comment out the next 5 lines.

ifeq ($(OS),Linux)
ifeq ($(wildcard /usr/include/libudev.h),)
$(error /usr/include/libudev.h does not exist.  Install it with "sudo apt-get install libudev-dev" or "sudo yum install libudev-devel" )
endif
endif


# Enable cm108 PTT support if libudev header file is present.

enable_cm108 := $(wildcard /usr/include/libudev.h)
ifneq ($(enable_cm108),)
CFLAGS += -DUSE_CM108
LDFLAGS += -ludev
endif


# Name of current directory.
# Used to generate zip file name for distribution.

z := $(notdir ${CURDIR})



# --------------------------------  Main application  -----------------------------------------

ifeq ($(OS),OpenBSD)
AUDIO_O := audio_portaudio.o
else
AUDIO_O := audio.o
endif


direwolf : direwolf.o config.o recv.o demod.o dsp.o demod_afsk.o demod_psk.o demod_9600.o hdlc_rec.o \
		hdlc_rec2.o multi_modem.o rdq.o rrbb.o dlq.o \
		fcs_calc.o ax25_pad.o  ax25_pad2.o xid.o \
		decode_aprs.o symbols.o server.o kiss.o kissserial.o kissnet.o kiss_frame.o hdlc_send.o fcs_calc.o \
		gen_tone.o $(AUDIO_O) audio_stats.o digipeater.o cdigipeater.o pfilter.o dedupe.o tq.o xmit.o morse.o \
		ptt.o beacon.o encode_aprs.o latlong.o encode_aprs.o latlong.o textcolor.o \
		dtmf.o aprs_tt.o tt_user.o tt_text.o igate.o waypoint.o serial_port.o log.o telemetry.o \
		dwgps.o dwgpsnmea.o dwgpsd.o dtime_now.o mheard.o ax25_link.o cm108.o \
		misc.a geotranz.a
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo " "
ifneq ($(enable_gpsd),)
	@echo "        >       This includes support for gpsd."
else
	@echo "        >       This does NOT include support for gpsd."
endif
ifneq ($(enable_hamlib),)
	@echo "        >       This includes support for hamlib."
else
	@echo "        >       This does NOT include support for hamlib."
endif
ifneq ($(enable_cm108),)
	@echo "        >       This includes support for CM108/CM119 PTT."
else
	@echo "        >       This does NOT include support for CM108/CM119 PTT."
endif
	@echo " "

# Optimization for slow processors.

demod.o      : fsk_fast_filter.h

demod_afsk.o : fsk_fast_filter.h


fsk_fast_filter.h : gen_fff
	./gen_fff > fsk_fast_filter.h

gen_fff : demod_afsk.c dsp.c textcolor.c
	echo " " > tune.h
	$(CC) $(CFLAGS) -DGEN_FFF -o $@ $^ $(LDFLAGS)


#
# The APRS AX.25 destination field is often used to identify the manufacturer/model.
# These are not hardcoded into Dire Wolf.  Instead they are read from
# a file called tocalls.txt at application start up time.
#
# The original permanent symbols are built in but the "new" symbols,
# using overlays, are often updated.  These are also read from files.
#
# You can obtain an updated copy by typing "make tocalls-symbols".
# This is not part of the normal build process.  You have to do this explicitly.
#
# The locations below appear to be the most recent.
# The copy at http://www.aprs.org/tocalls.txt is out of date.
#

.PHONY: tocalls-symbols
tocalls-symbols :
	cp tocalls.txt tocalls.txt~
	wget http://www.aprs.org/aprs11/tocalls.txt -O tocalls.txt
	-diff -Z tocalls.txt~ tocalls.txt
	cp symbols-new.txt symbols-new.txt~
	wget http://www.aprs.org/symbols/symbols-new.txt -O symbols-new.txt
	-diff -Z symbols-new.txt~ symbols-new.txt
	cp symbolsX.txt symbolsX.txt~
	wget http://www.aprs.org/symbols/symbolsX.txt -O symbolsX.txt
	-diff -Z symbolsX.txt~ symbolsX.txt


# ---------------------------------------- Other utilities included ------------------------------


# Separate application to decode raw data.

# First three use .c rather than .o because they depend on DECAMAIN definition.

decode_aprs : decode_aprs.c kiss_frame.c ax25_pad.c dwgpsnmea.o dwgps.o dwgpsd.o serial_port.o symbols.o textcolor.o fcs_calc.o latlong.o log.o telemetry.o tt_text.o misc.a
	$(CC) $(CFLAGS) -DDECAMAIN -o $@ $^ $(LDFLAGS)



# Convert between text and touch tone representation.

text2tt : tt_text.c misc.a
	$(CC) $(CFLAGS) -DENC_MAIN -o $@ $^ $(LDFLAGS)

tt2text : tt_text.c misc.a
	$(CC) $(CFLAGS) -DDEC_MAIN -o $@ $^ $(LDFLAGS)


# Convert between Latitude/Longitude and UTM coordinates.

ll2utm : ll2utm.c geotranz.a textcolor.o misc.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

utm2ll : utm2ll.c geotranz.a textcolor.o misc.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


# Convert from log file to GPX.

log2gpx : log2gpx.c textcolor.o misc.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


# Test application to generate sound.

gen_packets : gen_packets.c ax25_pad.c hdlc_send.c fcs_calc.c gen_tone.c morse.c dtmf.c textcolor.c dsp.c misc.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Unit test for AFSK demodulator

atest : atest.c demod.o demod_afsk.o demod_psk.o demod_9600.o \
		dsp.o hdlc_rec.o hdlc_rec2.o multi_modem.o rrbb.o \
		fcs_calc.o ax25_pad.o decode_aprs.o dwgpsnmea.o \
		dwgps.o dwgpsd.o serial_port.o telemetry.o dtime_now.o latlong.o symbols.o tt_text.o textcolor.o \
		misc.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


# Multiple AGWPE network or serial port clients to test TNCs side by side.

aclients : aclients.c ax25_pad.c fcs_calc.c textcolor.o misc.a
	$(CC) $(CFLAGS) -g -o $@ $^ 


# Talk to a KISS TNC.
# Note:  kiss_frame.c has conditional compilation on KISSUTIL.

kissutil : kissutil.c kiss_frame.c ax25_pad.o fcs_calc.o textcolor.o serial_port.o dtime_now.o dwsock.o misc.a
	$(CC) $(CFLAGS) -g -DKISSUTIL -o $@ $^ $(LDFLAGS)


# List USB audio adapters than can use GPIO for PTT.
# I don't think this will work on BSD.
# Rather than omitting cm108, I think it would be simpler and less confusing to build
# the application and have it say something like "not supported on this platform."
# The difference in behavior can depend on the -DUSE_CM108 compile option.

cm108 : cm108.c textcolor.o misc.a
	$(CC) $(CFLAGS) -g -DCM108_MAIN -o $@ $^ $(LDFLAGS)


# Touch Tone to Speech sample application.

ttcalc : ttcalc.o ax25_pad.o fcs_calc.o textcolor.o misc.a
	$(CC) $(CFLAGS) -g -o $@ $^ 


# -----------------------------------------  Libraries  --------------------------------------------

# UTM, USNG, MGRS conversions.

geotranz.a : error_string.o  mgrs.o  polarst.o  tranmerc.o  ups.o  usng.o  utm.o
	ar -cr $@ $^

error_string.o : geotranz/error_string.c
	$(CC) $(CFLAGS) -c -o $@ $^

mgrs.o : geotranz/mgrs.c
	$(CC) $(CFLAGS) -c -o $@ $^

polarst.o : geotranz/polarst.c
	$(CC) $(CFLAGS) -c -o $@ $^

tranmerc.o : geotranz/tranmerc.c
	$(CC) $(CFLAGS) -c -o $@ $^

ups.o : geotranz/ups.c
	$(CC) $(CFLAGS) -c -o $@ $^

usng.o : geotranz/usng.c
	$(CC) $(CFLAGS) -c -o $@ $^

utm.o : geotranz/utm.c
	$(CC) $(CFLAGS) -c -o $@ $^


# Provide our own copy of strlcpy and strlcat because they are not included with Linux.
# We don't need the others in that same directory.
# OpenBSD has the strl--- functions so misc.a can be empty.  
# I don't want to eliminate use of misc.a because other functions might be added in the future.

ifeq ($(OS),OpenBSD)
misc.a :
	ar -cr $@ $^	

else
misc.a : strlcpy.o strlcat.o
	ar -cr $@ $^	
 
strlcpy.o : misc/strlcpy.c
	$(CC) $(CFLAGS) -I. -c -o $@ $^

strlcat.o : misc/strlcat.c
	$(CC) $(CFLAGS) -I. -c -o $@ $^

endif


# -------------------------------------  Installation  ----------------------------------



# Generate apprpriate sample configuration file for this platform.
# Originally, there was one sample for all platforms.  It got too cluttered
# and confusing saying, this is for windows, and this is for Linux, and this ...
# Trying to maintain 3 different versions in parallel is error prone.
# We now have a single generic version which can be used to generate
# the various platform specific versions.

# generic.conf should be checked into source control.
# direwolf.conf should NOT.  It is generated when compiling on the target platform.

# TODO: Should have BSD variation with OSS device names.  Anything else?

direwolf.conf : generic.conf
	egrep '^C|^L' generic.conf | cut -c2-999 > direwolf.conf


# Where should we install it?

# Something built from source and installed locally would normally go in /usr/local/...
# If not specified on the make command line, this is our default.

DESTDIR ?= /usr/local

# However, if you are preparing a "binary" DEB or RPM package, the installation location
# would normally be  /usr/...  instead.   In this case, use a command line like this:
#
#	make  DESTDIR=/usr  install



# Command to "install" to system directories.
# Do we need to use "-m 999" instead of "--mode=999" for OpenBSD?

INSTALL ?= install
INSTALL_PROGRAM ?= ${INSTALL} -D --mode=755 
INSTALL_SCRIPT ?= ${INSTALL} -D --mode=755
INSTALL_DATA ?= ${INSTALL} -D --mode=644
INSTALL_MAN ?= ${INSTALL_DATA}


# direwolf.desktop was previously handcrafted for the Raspberry Pi.
# It was hardcoded with lxterminal, /home/pi, and so on.
# In version 1.2, try to customize this to match other situations better.

# TODO:  Test this better.


direwolf.desktop :
	@echo "Generating customized direwolf.desktop ..."
	@echo '[Desktop Entry]' > $@
	@echo 'Type=Application' >> $@
ifneq ($(wildcard ${PREFIX}/bin/lxterminal),)
	@echo "Exec=lxterminal -t \"Dire Wolf\" -e \"$(DESTDIR)/bin/direwolf\"" >> $@
else ifneq ($(wildcard ${PREFIX}/bin/lxterm),)
	@echo "Exec=lxterm -hold -title \"Dire Wolf\" -bg white -e \"$(DESTDIR)/bin/direwolf\"" >> $@
else
	@echo "Exec=xterm -hold -title \"Dire Wolf\" -bg white -e \"$(DESTDIR)/bin/direwolf\"" >> $@
endif
	@echo 'Name=Dire Wolf' >> $@
	@echo 'Comment=APRS Soundcard TNC' >> $@
	@echo 'Icon=$(DESTDIR)/share/direwolf/pixmaps/dw-icon.png' >> $@
	@echo "Path=$(HOME)" >> $@
	@echo '#Terminal=true' >> $@
	@echo 'Categories=HamRadio' >> $@
	@echo 'Keywords=Ham Radio;APRS;Soundcard TNC;KISS;AGWPE;AX.25' >> $@


# Installation into $(DESTDIR), usually /usr/local/... or /usr/...
# Needs to be run as root or with sudo.


.PHONY: install
install : $(APPS) direwolf.conf tocalls.txt symbols-new.txt symbolsX.txt dw-icon.png direwolf.desktop
#
# Applications, not installed with package manager, normally go in /usr/local/bin.
# /usr/bin is used instead when installing from .DEB or .RPM package.
#
	$(INSTALL_PROGRAM) direwolf $(DESTDIR)/bin/direwolf
	$(INSTALL_PROGRAM) decode_aprs $(DESTDIR)/bin/decode_aprs
	$(INSTALL_PROGRAM) text2tt $(DESTDIR)/bin/text2tt
	$(INSTALL_PROGRAM) tt2text $(DESTDIR)/bin/tt2text
	$(INSTALL_PROGRAM) ll2utm $(DESTDIR)/bin/ll2utm
	$(INSTALL_PROGRAM) utm2ll $(DESTDIR)/bin/utm2ll
	$(INSTALL_PROGRAM) aclients $(DESTDIR)/bin/aclients
	$(INSTALL_PROGRAM) log2gpx $(DESTDIR)/bin/log2gpx
	$(INSTALL_PROGRAM) gen_packets $(DESTDIR)/bin/gen_packets
	$(INSTALL_PROGRAM) atest $(DESTDIR)/bin/atest
	$(INSTALL_PROGRAM) ttcalc $(DESTDIR)/bin/ttcalc
	$(INSTALL_PROGRAM) kissutil $(DESTDIR)/bin/kissutil
	$(INSTALL_PROGRAM) cm108 $(DESTDIR)/bin/cm108
	$(INSTALL_PROGRAM) dwespeak.sh $(DESTDIR)/bin/dwspeak.sh
#
# Telemetry Toolkit executables.   Other .conf and .txt files will go into doc directory.
#
	$(INSTALL_SCRIPT) telemetry-toolkit/telem-balloon.pl $(DESTDIR)/bin/telem-balloon.pl
	$(INSTALL_SCRIPT) telemetry-toolkit/telem-bits.pl $(DESTDIR)/bin/telem-bits.pl
	$(INSTALL_SCRIPT) telemetry-toolkit/telem-data.pl $(DESTDIR)/bin/telem-data.pl
	$(INSTALL_SCRIPT) telemetry-toolkit/telem-data91.pl $(DESTDIR)/bin/telem-data91.pl
	$(INSTALL_SCRIPT) telemetry-toolkit/telem-eqns.pl $(DESTDIR)/bin/telem-eqns.pl
	$(INSTALL_SCRIPT) telemetry-toolkit/telem-parm.pl $(DESTDIR)/bin/telem-parm.pl
	$(INSTALL_SCRIPT) telemetry-toolkit/telem-seq.sh $(DESTDIR)/bin/telem-seq.sh
	$(INSTALL_SCRIPT) telemetry-toolkit/telem-unit.pl $(DESTDIR)/bin/telem-unit.pl
	$(INSTALL_SCRIPT) telemetry-toolkit/telem-volts.py $(DESTDIR)/bin/telem-volts.py
#
# Misc. data such as "tocall" to system mapping.
#
	$(INSTALL_DATA) tocalls.txt $(DESTDIR)/share/direwolf/tocalls.txt
	$(INSTALL_DATA) symbols-new.txt $(DESTDIR)/share/direwolf/symbols-new.txt
	$(INSTALL_DATA) symbolsX.txt $(DESTDIR)/share/direwolf/symbolsX.txt
#
# For desktop icon.
#
	$(INSTALL_DATA) dw-icon.png $(DESTDIR)/share/direwolf/pixmaps/dw-icon.png
	$(INSTALL_DATA) direwolf.desktop $(DESTDIR)/share/applications/direwolf.desktop
#
# Documentation.  Various plain text files and PDF.
#
	$(INSTALL_DATA) CHANGES.md $(DESTDIR)/share/doc/direwolf/CHANGES.md
	$(INSTALL_DATA) LICENSE-dire-wolf.txt $(DESTDIR)/share/doc/direwolf/LICENSE-dire-wolf.txt
	$(INSTALL_DATA) LICENSE-other.txt $(DESTDIR)/share/doc/direwolf/LICENSE-other.txt
#
# ./README.md is an overview for the project main page.
# Maybe we could stick it in some other place.
# doc/README.md contains an overview of the PDF file contents and is more useful here.
#
	$(INSTALL_DATA) doc/README.md $(DESTDIR)/share/doc/direwolf/README.md
	$(INSTALL_DATA) doc/2400-4800-PSK-for-APRS-Packet-Radio.pdf $(DESTDIR)/share/doc/direwolf/2400-4800-PSK-for-APRS-Packet-Radio.pdf
	$(INSTALL_DATA) doc/A-Better-APRS-Packet-Demodulator-Part-1-1200-baud.pdf $(DESTDIR)/share/doc/direwolf/A-Better-APRS-Packet-Demodulator-Part-1-1200-baud.pdf
	$(INSTALL_DATA) doc/A-Better-APRS-Packet-Demodulator-Part-2-9600-baud.pdf $(DESTDIR)/share/doc/direwolf/A-Better-APRS-Packet-Demodulator-Part-2-9600-baud.pdf
	$(INSTALL_DATA) doc/A-Closer-Look-at-the-WA8LMF-TNC-Test-CD.pdf $(DESTDIR)/share/doc/direwolf/A-Closer-Look-at-the-WA8LMF-TNC-Test-CD.pdf
	$(INSTALL_DATA) doc/APRS-Telemetry-Toolkit.pdf $(DESTDIR)/share/doc/direwolf/APRS-Telemetry-Toolkit.pdf
	$(INSTALL_DATA) doc/APRStt-Implementation-Notes.pdf $(DESTDIR)/share/doc/direwolf/APRStt-Implementation-Notes.pdf
	$(INSTALL_DATA) doc/APRStt-interface-for-SARTrack.pdf $(DESTDIR)/share/doc/direwolf/APRStt-interface-for-SARTrack.pdf
	$(INSTALL_DATA) doc/APRStt-Listening-Example.pdf $(DESTDIR)/share/doc/direwolf/APRStt-Listening-Example.pdf
	$(INSTALL_DATA) doc/Bluetooth-KISS-TNC.pdf $(DESTDIR)/share/doc/direwolf/Bluetooth-KISS-TNC.pdf
	$(INSTALL_DATA) doc/Going-beyond-9600-baud.pdf $(DESTDIR)/share/doc/direwolf/Going-beyond-9600-baud.pdf
	$(INSTALL_DATA) doc/Raspberry-Pi-APRS.pdf $(DESTDIR)/share/doc/direwolf/Raspberry-Pi-APRS.pdf
	$(INSTALL_DATA) doc/Raspberry-Pi-APRS-Tracker.pdf $(DESTDIR)/share/doc/direwolf/Raspberry-Pi-APRS-Tracker.pdf
	$(INSTALL_DATA) doc/Raspberry-Pi-SDR-IGate.pdf $(DESTDIR)/share/doc/direwolf/Raspberry-Pi-SDR-IGate.pdf
	$(INSTALL_DATA) doc/Successful-APRS-IGate-Operation.pdf $(DESTDIR)/share/doc/direwolf/Successful-APRS-IGate-Operation.pdf
	$(INSTALL_DATA) doc/User-Guide.pdf $(DESTDIR)/share/doc/direwolf/User-Guide.pdf
	$(INSTALL_DATA) doc/WA8LMF-TNC-Test-CD-Results.pdf $(DESTDIR)/share/doc/direwolf/WA8LMF-TNC-Test-CD-Results.pdf
	$(INSTALL_DATA) doc/Why-is-9600-only-twice-as-fast-as-1200.pdf $(DESTDIR)/share/doc/direwolf/Why-is-9600-only-twice-as-fast-as-1200.pdf
#
# Various sample config and other files go into examples under the doc directory.
# When building from source, these can be put in home directory with "make install-conf".
# When installed from .DEB or .RPM package, the user will need to copy these to
# the home directory or other desired location.
#
	$(INSTALL_DATA)   direwolf.conf $(DESTDIR)/share/doc/direwolf/examples/direwolf.conf
	$(INSTALL_SCRIPT) dw-start.sh $(DESTDIR)/share/doc/direwolf/examples/dw-start.sh
	$(INSTALL_DATA)   sdr.conf $(DESTDIR)/share/doc/direwolf/examples/sdr.conf
	$(INSTALL_DATA)   telemetry-toolkit/telem-m0xer-3.txt $(DESTDIR)/share/doc/direwolf/examples/telem-m0xer-3.txt
	$(INSTALL_DATA)   telemetry-toolkit/telem-balloon.conf $(DESTDIR)/share/doc/direwolf/examples/telem-balloon.conf
	$(INSTALL_DATA)   telemetry-toolkit/telem-volts.conf $(DESTDIR)/share/doc/direwolf/examples/telem-volts.conf
#
# "man" pages
#
	$(INSTALL_MAN) man1/aclients.1 $(DESTDIR)/share/man/man1/aclients.1
	$(INSTALL_MAN) man1/atest.1 $(DESTDIR)/share/man/man1/atest.1
	$(INSTALL_MAN) man1/decode_aprs.1 $(DESTDIR)/share/man/man1/decode_aprs.1
	$(INSTALL_MAN) man1/direwolf.1 $(DESTDIR)/share/man/man1/direwolf.1
	$(INSTALL_MAN) man1/gen_packets.1 $(DESTDIR)/share/man/man1/gen_packets.1
	$(INSTALL_MAN) man1/kissutil.1 $(DESTDIR)/share/man/man1/kissutil.1
	$(INSTALL_MAN) man1/ll2utm.1 $(DESTDIR)/share/man/man1/ll2utm.1
	$(INSTALL_MAN) man1/log2gpx.1 $(DESTDIR)/share/man/man1/log2gpx.1
	$(INSTALL_MAN) man1/text2tt.1 $(DESTDIR)/share/man/man1/text2tt.1
	$(INSTALL_MAN) man1/tt2text.1 $(DESTDIR)/share/man/man1/tt2text.1
	$(INSTALL_MAN) man1/utm2ll.1 $(DESTDIR)/share/man/man1/utm2ll.1
#
# Set group and mode of HID devices corresponding to C-Media USB Audio adapters.
# This will allow us to use the CM108/CM119 GPIO pins for PTT.
# I don't think this is applicable to BSD.
#
ifeq ($(OS),Linux)
	$(INSTALL_DATA) 99-direwolf-cmedia.rules /etc/udev/rules.d/99-direwolf-cmedia.rules
endif
#
	@echo " "
	@echo "If this is your first install, not an upgrade, type this to put a copy"
	@echo "of the sample configuration file (direwolf.conf) in your home directory:"
	@echo " "
	@echo "        make install-conf"
	@echo " "


# Put sample configuration & startup files in home directory.
# This step would be done as ordinary user.
# Some people like to put the direwolf config file in /etc/ax25.
# Note that all of these are also in $(DESTDIR)/share/doc/direwolf/examples/.

# The Raspberry Pi has ~/Desktop but Ubuntu does not.

# TODO: Handle Linux variations correctly.

# Version 1.4 - Add "-n" option to avoid clobbering existing, probably customized, config files.

# dw-start.sh is greatly improved in version 1.4.
# It was moved from isntall-rpi to install-conf because it is not just for the RPi.

.PHONY: install-conf
install-conf : direwolf.conf
	cp -n direwolf.conf ~
	cp -n sdr.conf ~
	cp -n telemetry-toolkit/telem-m0xer-3.txt ~
	cp -n telemetry-toolkit/telem-*.conf ~
	chmod +x dw-start.sh
	cp -n dw-start.sh ~
ifneq ($(wildcard $(HOME)/Desktop),)
	@echo " "
	@echo "This will add a desktop icon on some systems."
	@echo "This is known to work on Raspberry Pi but might not be compatible with other desktops."
	@echo " "
	@echo "        make install-rpi"
	@echo " "
endif


.PHONY: install-rpi
install-rpi :
ifeq ($(OS),Linux)
	ln -f -s $(DESTDIR)/share/applications/direwolf.desktop ~/Desktop/direwolf.desktop
else
	ln -f -s ${PREFIX}/share/applications/direwolf.desktop ~/Desktop/direwolf.desktop
endif


# ----------------------------------  Automated Smoke Test  --------------------------------



# Combine some unit tests into a single regression sanity check.


check : dtest ttest tttexttest pftest tlmtest lltest enctest kisstest pad2test xidtest dtmftest check-modem1200 check-modem300 check-modem9600 check-modem19200 check-modem2400-a check-modem2400-b check-modem2400-g check-modem4800

# Can we encode and decode at popular data rates?

check-modem1200 : gen_packets atest
	./gen_packets -n 100 -o /tmp/test12.wav
	./atest -F0 -PE -L63 -G71 /tmp/test12.wav
	./atest -F1 -PE -L70 -G75 /tmp/test12.wav
	rm /tmp/test12.wav

check-modem300 : gen_packets atest
	./gen_packets -B300 -n 100 -o /tmp/test3.wav
	./atest -B300 -F0 -L68 -G69 /tmp/test3.wav
	./atest -B300 -F1 -L73 -G75 /tmp/test3.wav
	rm /tmp/test3.wav

check-modem9600 : gen_packets atest
	./gen_packets -B9600 -n 100 -o /tmp/test96.wav
	./atest -B9600 -F0 -L61 -G65 /tmp/test96.wav
	./atest -B9600 -F1 -L62 -G66 /tmp/test96.wav
	rm /tmp/test96.wav

check-modem19200 : gen_packets atest
	./gen_packets -r 96000 -B19200 -n 100 -o /tmp/test19.wav
	./atest -B19200 -F0 -L60 -G64 /tmp/test19.wav
	./atest -B19200 -F1 -L64 -G68 /tmp/test19.wav
	rm /tmp/test19.wav

check-modem2400-a : gen_packets atest
	./gen_packets -B2400 -j -n 100 -o /tmp/test24-a.wav
	./atest -B2400 -j -F0 -L76 -G80 /tmp/test24-a.wav
	./atest -B2400 -j -F1 -L84 -G88 /tmp/test24-a.wav
	rm /tmp/test24-a.wav

check-modem2400-b : gen_packets atest
	./gen_packets -B2400 -J -n 100 -o /tmp/test24-b.wav
	./atest -B2400 -J -F0 -L79 -G83 /tmp/test24-b.wav
	./atest -B2400 -J -F1 -L87 -G91 /tmp/test24-b.wav
	rm /tmp/test24-b.wav

check-modem2400-g : gen_packets atest
	./gen_packets -B2400 -g -n 100 -o /tmp/test24-g.wav
	./atest -B2400 -g -F0 -L99 -G100 /tmp/test24-g.wav
	rm /tmp/test24-g.wav

check-modem4800 : gen_packets atest
	./gen_packets -B4800 -n 100 -o /tmp/test48.wav
	./atest -B4800 -F0 -L70 -G74 /tmp/test48.wav
	./atest -B4800 -F1 -L79 -G84 /tmp/test48.wav
	rm /tmp/test48.wav


# Unit test for inner digipeater algorithm

.PHONY : dtest
dtest : digipeater.c dedupe.c pfilter.c \
		ax25_pad.o fcs_calc.o tq.o textcolor.o \
		decode_aprs.o dwgpsnmea.o dwgps.o dwgpsd.o serial_port.o latlong.o telemetry.o symbols.o tt_text.o misc.a
	$(CC) $(CFLAGS) -DDIGITEST -o $@ $^ $(LDFLAGS)
	./dtest
	rm dtest


# Unit test for APRStt tone sequence parsing.

.PHONY : ttest
ttest : aprs_tt.c tt_text.c latlong.o textcolor.o misc.a geotranz.a misc.a
	$(CC) $(CFLAGS) -DTT_MAIN  -o $@ $^ $(LDFLAGS)
	./ttest
	rm ttest


# Unit test for APRStt tone sequence / text conversions.

.PHONY: tttexttest
tttexttest : tt_text.c textcolor.o misc.a
	$(CC) $(CFLAGS) -DTTT_TEST -o $@ $^ $(LDFLAGS)
	./tttexttest
	rm tttexttest


# Unit test for Packet Filtering.

.PHONY: pftest
pftest : pfilter.c ax25_pad.o textcolor.o fcs_calc.o decode_aprs.o dwgpsnmea.o dwgps.o dwgpsd.o serial_port.o latlong.o symbols.o telemetry.o tt_text.o misc.a 
	$(CC) $(CFLAGS) -DPFTEST -o $@ $^ $(LDFLAGS)
	./pftest
	rm pftest

# Unit test for telemetry decoding.

.PHONY: tlmtest
tlmtest : telemetry.c ax25_pad.o fcs_calc.o textcolor.o misc.a
	$(CC) $(CFLAGS) -DTEST -o $@ $^ $(LDFLAGS)
	./tlmtest
	rm tlmtest

# Unit test for location coordinate conversion.

.PHONY: lltest
lltest : latlong.c textcolor.o misc.a
	$(CC) $(CFLAGS) -DLLTEST -o $@ $^ $(LDFLAGS)
	./lltest
	rm lltest

# Unit test for encoding position & object report.

.PHONY: enctest
enctest : encode_aprs.c latlong.c textcolor.c misc.a
	$(CC) $(CFLAGS) -DEN_MAIN -o $@ $^ $(LDFLAGS)
	./enctest
	rm enctest


# Unit test for KISS encapsulation.

.PHONY: kisstest
kisstest : kiss_frame.c
	$(CC) $(CFLAGS) -DKISSTEST -o $@ $^ $(LDFLAGS)
	./kisstest
	rm kisstest

# Unit test for constructing frames besides UI.

.PHONY: pad2test
pad2test : ax25_pad2.c ax25_pad.c fcs_calc.o textcolor.o misc.a
	$(CC) $(CFLAGS) -DPAD2TEST -o $@ $^ $(LDFLAGS)
	./pad2test
	rm pad2test


# Unit Test for XID frame encode/decode.

.PHONY: xidtest
xidtest : xid.c textcolor.o misc.a
	$(CC) $(CFLAGS) -DXIDTEST -o $@ $^  $(LDFLAGS)
	./xidtest
	rm xidtest


# Unit Test for DTMF encode/decode.

.PHONY: dtmftest
dtmftest : dtmf.c textcolor.o
	$(CC) $(CFLAGS) -DDTMF_TEST -o $@ $^  $(LDFLAGS)
	./dtmftest
	rm dtmftest



#  -----------------------------  Manual tests and experiments  ---------------------------

# These are not included in a normal build.  Might be broken.

# Unit test for IGate

itest : igate.c textcolor.c ax25_pad.c fcs_calc.c textcolor.o misc.a
	$(CC) $(CFLAGS) -DITEST -o $@ $^
	./itest

# Unit test for UDP reception with AFSK demodulator.
# Temporary during development.  Might not be useful anymore.

udptest : udp_test.c demod.o dsp.o demod_afsk.o demod_psk.o demod_9600.o hdlc_rec.o hdlc_rec2.o multi_modem.o rrbb.o \
		fcs_calc.o ax25_pad.o decode_aprs.o symbols.o textcolor.o misc.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	./udptest

# For demodulator tweaking experiments.
# Dependencies of demod*.c, rather than .o, are intentional.

demod.o      : tune.h

demod_afsk.o : tune.h

demod_9600.o : tune.h

demod_psk.o  : tune.h

tune.h :
	echo " " > tune.h


testagc : atest.c demod.c dsp.c demod_afsk.c demod_psk.c demod_9600.c hdlc_rec.o hdlc_rec2.o multi_modem.o rrbb.o \
		fcs_calc.o ax25_pad.o decode_aprs.o telemetry.o dtime_now.o latlong.o symbols.o tune.h textcolor.o misc.a
	$(CC) $(CFLAGS) -o atest $^ $(LDFLAGS)
	./atest 02_Track_2.wav | grep "packets decoded in" > atest.out


testagc96 : atest.c fsk_fast_filter.h tune.h demod.c demod_afsk.c demod_psk.c demod_9600.c \
		dsp.o hdlc_rec.o hdlc_rec2.o multi_modem.o \
		rrbb.o fcs_calc.o ax25_pad.o decode_aprs.o \
		dwgpsnmea.o dwgps.o dwgpsd.o serial_port.o latlong.o \
		symbols.o tt_text.o textcolor.o telemetry.o dtime_now.o \
		misc.a
	rm -f atest96
	$(CC) $(CFLAGS) -o atest96 $^ $(LDFLAGS)
	./atest96 -B 9600 ../walkabout9600c.wav | grep "packets decoded in" >atest.out
	#./atest96 -B 9600 noisy96.wav | grep "packets decoded in" >atest.out
	#./atest96 -B 9600 19990303_0225_9600_8bis_22kHz.wav | grep "packets decoded in" >atest.out
	#./atest96 -B 9600  19990303_0225_9600_16bit_22kHz.wav | grep "packets decoded in" >atest.out
	#./atest96 -B 9600 -P + z8-22k.wav| grep "packets decoded in" >atest.out
	#./atest96 -B 9600 test9600.wav | grep "packets decoded in" >atest.out
	echo " " > tune.h


# -----------------------------------------------------------------------------------------


.PHONY: clean
clean :
	rm -f $(APPS) gen_fff tune.h fsk_fast_filter.h *.o *.a direwolf.desktop


depend : $(wildcard *.c)
	makedepend -f $(lastword $(MAKEFILE_LIST)) -- $(CFLAGS) -- $^


#
# The following is updated by "make depend"
#
# DO NOT DELETE


