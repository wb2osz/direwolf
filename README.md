
# Dire Wolf #

### Decoded Information from Radio Emissions for Windows Or Linux Fans ###

In the early days of Amateur Packet Radio, it was necessary to use an expensive “Terminal Node Controller” (TNC) with specialized hardware.  Those days are gone.  You can now get better results at lower cost by connecting your radio to the “soundcard” interface of a computer and using software to decode the signals.
 
Dire Wolf is a software "soundcard" modem/TNC and [APRS](http://www.aprs.org/) encoder/decoder.   It can be used stand-alone to observe APRS traffic, as a digipeater, [APRStt](http://www.aprs.org/aprstt.html) gateway, or Internet Gateway (IGate).    It can also be used as a virtual TNC for other applications such as [APRSIS32](http://aprsisce.wikidot.com/), [UI-View32](http://www.ui-view.net/), [Xastir](http://xastir.org/index.php/Main_Page), [APRS-TW](http://aprstw.blandranch.net/), [YAAC](http://www.ka2ddo.org/ka2ddo/YAAC.html), [UISS](http://users.belgacom.net/hamradio/uiss.htm), [Linux AX25](http://www.linux-ax25.org/wiki/Main_Page), [SARTrack](http://www.sartrack.co.nz/index.html), [RMS Express](http://www.winlink.org/RMSExpress), [BPQ32](http://www.cantab.net/users/john.wiseman/Documents/BPQ32.html), [Outpost PM](http://www.outpostpm.org/) and many others.
 
 
## Features & Benefits ##

- Lower cost, higher performance alternative to hardware TNC.
Decodes more than 1000 error-free frames from [WA8LMF TNC Test CD](http://wa8lmf.net/TNCtest/).  

- Ideal for building a Raspberry Pi digipeater & IGate.

- Data rates: 300 AFSK, 1200 AFSK, 2400 QPSK, 4800 8PSK, and 9600/19200/38400 bps K9NG/G3RUH.

- Interface with applications by
      - [AGW](http://uz7.ho.ua/includes/agwpeapi.htm) network protocol
      - [KISS](http://www.ax25.net/kiss.aspx) serial port
      - [KISS](http://www.ax25.net/kiss.aspx) TCP network protocol
      
- Decoding of received information for troubleshooting.

- Conversion from APRS to waypoint sentences in popular formats:  $GPWPL, $PGRMW, $PMGNWPL, $PKWDWPL.

- Logging and conversion to GPX file format.

- Beaconing for yourself or other nearby entities.

- Very flexible Digipeating with routing and filtering between up to 6 ports.

- APRStt gateway - converts touch tone sequences to APRS objects and voice responses.

- APRS Internet Gateway (IGate) with IPv6 support and special SATGate mode.

- APRS Telemetry Toolkit.

- Compatible with software defined radios (SDR) such as [gqrx](http://gqrx.dk/),  [rtl_fm](http://sdr.osmocom.org/trac/wiki/rtl-sdr), and SDR#.

- Includes separate raw packet decoder, decode_aprs.

- AX.25 v2.2 connected mode.  (New in version 1.4.)

- Open source so you can see how it works and make your own modifications.

- Runs in 3 different environments:
      - Microsoft Windows XP or later
      - Linux, regular PC/laptop or single board computer such as Raspberry Pi, BeagleBone Black, cubieboard 2, or C.H.I.P.
      - Mac OS X

## Documentation ##

[Stable Version](https://github.com/wb2osz/direwolf/tree/master/doc)

[Latest Development Version](https://github.com/wb2osz/direwolf/tree/dev/doc)


## Installation ##

### Windows ###

Go to the [**releases** page](https://github.com/wb2osz/direwolf/releases).   Download a zip file with "win" in its name, unzip it, and run direwolf.exe from a command window.

For more details see the **User Guide** in the [**doc** directory](https://github.com/wb2osz/direwolf/tree/master/doc).  




### Linux - Using git clone (recommended) ###

	cd ~
	git clone https://www.github.com/wb2osz/direwolf
	cd direwolf
	make
	sudo make install
	make install-conf

This should give you the most recent stable release.  If you want the latest (possibly unstable) development version, use "git checkout dev" before the first "make" command.

For more details see the **User Guide** in the [**doc** directory](https://github.com/wb2osz/direwolf/tree/master/doc).  Special considerations for the Raspberry Pi are found in **Raspberry-Pi-APRS.pdf**


### Linux - Using apt-get (Debian flavor operating systems) ###

Results will vary depending on your hardware platform and operating system version because it depends on various volunteers who perform the packaging.  

	sudo apt-get update
    apt-cache showpkg direwolf
	sudo apt-get install direwolf


### Linux - Using yum (Red Hat flavor operating systems) ###

Results will vary depending on your hardware platform and operating system version because it depends on various volunteers who perform the packaging.  

	sudo yum check-update
    sudo yum list direwolf
	sudo yum install direwolf

### Linux - Download source in tar or zip file ###

Go to the [releases page](https://github.com/wb2osz/direwolf/releases).  Chose desired release and download the source as zip or compressed tar file.  Unpack the files, with "unzip" or "tar xfz," and then:

	cd direwolf-*
	make
	sudo make install
	make install-conf

For more details see the **User Guide** in the [**doc** directory](https://github.com/wb2osz/direwolf/tree/master/doc).  Special considerations for the Raspberry Pi are found in **Raspberry-Pi-APRS.pdf**

## Join the conversation  ##
 
Here are some good places to ask questions and share your experience:

- [Dire Wolf packet TNC](https://groups.yahoo.com/neo/groups/direwolf_packet/info) 

- [Raspberry Pi 4 Ham Radio](https://groups.yahoo.com/neo/groups/Raspberry_Pi_4-Ham_RADIO/info)

- [linuxham](https://groups.yahoo.com/neo/groups/linuxham/info)

- [TAPR aprssig](http://www.tapr.org/pipermail/aprssig/)
 

The github "issues" section is for reporting software defects and enhancement requests.  It is NOT a place to ask questions or have general discussions.  Please use one of the locations above.
