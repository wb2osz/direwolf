
# Dire Wolf #

### Decoded Information from Radio Emissions for Windows Or Linux Fans ###

In the early days of Amateur Packet Radio, it was necessary to use a “Terminal Node Controller” (TNC) with specialized hardware.  Those days are gone.  You can now get better results at lower cost by connecting your radio to the “soundcard” interface of a computer and using software to decode the signals.
 
Dire Wolf is a software "soundcard" modem/TNC and [APRS](http://www.aprs.org/) encoder/decoder.   It can be used stand-alone to observe APRS traffic, as a digipeater, [APRStt](http://www.aprs.org/aprstt.html) gateway, or Internet Gateway (IGate).    It can also be used as a virtual TNC for other applications such as [APRSIS32](http://aprsisce.wikidot.com/), [UI-View32](http://www.ui-view.net/), [Xastir](http://xastir.org/index.php/Main_Page), [APRS-TW](http://aprstw.blandranch.net/), [YAAC](http://www.ka2ddo.org/ka2ddo/YAAC.html), [UISS](http://users.belgacom.net/hamradio/uiss.htm), [Linux AX25](http://www.linux-ax25.org/wiki/Main_Page), [SARTrack](http://www.sartrack.co.nz/index.html), [RMS Express](http://www.winlink.org/RMSExpress), and many others.
 
 
## Features ##

- Lower cost, higher performance alternative to hardware TNC.
Decodes more than 1000 error-free frames from [WA8LMF TNC Test CD](http://wa8lmf.net/TNCtest/).  

- Ideal for building a Raspberry Pi digipeater & IGate.

- 300, 1200, and 9600 baud operation.

- Interface with applications by
      - [AGW](http://uz7.ho.ua/includes/agwpeapi.htm) network protocol
      - [KISS](http://www.ax25.net/kiss.aspx) serial port
      - [KISS](http://www.ax25.net/kiss.aspx) network protocol
      
- Decoding of received information for troubleshooting.

- Logging and conversion to GPX file format.

- Beaconing for yourself or other nearby entities.

- Very flexible Digipeating with routing and filtering between up to 6 ports.

- APRStt gateway - converts touch tone sequences to APRS objects and voice responses.

- APRS Internet Gateway (IGate) with IPv6 support and special SATGate mode.

- APRS Telemetry Toolkit.

- Compatible with software defined radios (SDR) such as [gqrx](http://gqrx.dk/),  [rtl_fm](http://sdr.osmocom.org/trac/wiki/rtl-sdr), and SDR#.

- Includes separate raw packet decoder, decode_aprs.

- Open source so you can see how it works and make your own modifications.

- Runs in 3 different environments:
      - Microsoft Windows XP or later
      - Linux, regular PC or single board computer such as Raspberry Pi, BeagleBone Black, or cubieboard 2
      - Mac OS X
 
## Installation ##

### Windows ###

Go to the [releases page](https://github.com/wb2osz/direwolf/releases).   Download a zip file with "win" in its name, unzip it, and run direwolf.exe from a command window.

For more details see the **User Guide** in the [doc directory](https://github.com/wb2osz/direwolf/tree/master/doc).  


### Linux - Download with web browser ###

Go to the [releases page](https://github.com/wb2osz/direwolf/releases).  Chose desired release and download the source as zip or compressed tar file.  Unpack the files, with "unzip" or "tar xfz," and then:

	cd direwolf-*
	make
	sudo make install
	make install-conf

For more details see the **User Guide** in the [doc directory](https://github.com/wb2osz/direwolf/tree/master/doc).  Special considerations for the Raspberry Pi are found in **Raspberry-Pi-APRS.pdf**

### Linux - Using git clone ###

	cd ~
	git clone https://www.github.com/wb2osz/direwolf
	cd direwolf
	make
	sudo make install
	make install-conf

This should give you the most recent stable release.  If you want the latest (unstable) development version, use "git checkout dev" instead before the first "make" command.

For more details see the **User Guide** in the [doc directory](https://github.com/wb2osz/direwolf/tree/master/doc).  Special considerations for the Raspberry Pi are found in **Raspberry-Pi-APRS.pdf**

## Join the conversation  ##
 
Here are some good places to share information:

- [Dire Wolf packet TNC](https://groups.yahoo.com/neo/groups/direwolf_packet/info) 

- [Raspberry Pi 4 Ham Radio](https://groups.yahoo.com/neo/groups/Raspberry_Pi_4-Ham_RADIO/info)

- [linuxham](https://groups.yahoo.com/neo/groups/linuxham/info)

- [TAPR aprssig](http://www.tapr.org/pipermail/aprssig/)
 


