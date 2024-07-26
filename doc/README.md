# Documentation for Dire Wolf #

Click on the document name to view in your web browser or the link following to download the PDF file.


## Slide Show ##

Brief summary of packet radio / APRS history and the capbilities of Dire Wolf.

[Power Point presentation](https://github.com/wb2osz/direwolf-presentation)  -- Why not give a talk at a local club meeting?

## Essential Reading ##
 
- [**User Guide**](User-Guide.pdf)  [ [*download*](../../../raw/master/doc/User-Guide.pdf) ]

	This is your primary source of information about installation, operation, and configuration.

- [**Raspberry Pi APRS**](Raspberry-Pi-APRS.pdf)   [ [*download*](../../../raw/master/doc/Raspberry-Pi-APRS.pdf) ]

	The Raspberry Pi has some special considerations that
	make it different from other generic Linux systems.
	Start here if using the Raspberry Pi, Beaglebone Black, cubieboard2, or similar single board computers.

	
## Application Notes ##

These dive into more detail for specialized topics or typical usage scenarios.



- [**AX.25 + FEC = FX.25**](AX25_plus_FEC_equals_FX25.pdf)  [ [*download*](../../../raw/dev/doc/AX25_plus_FEC_equals_FX25.pdf) ]

	What can you do if your radio signal isn’t quite strong enough to get through reliably?  Move to higher ground?  Get a better antenna?  More power?  Use very narrow bandwidth and very slow data?

    Sometimes those are not options.  Another way to improve communication reliability is to add redundant information so the message will still get through even if small parts are missing.  FX.25 adds forward error correction (FEC) which maintaining complete compatibility with older equipment.


- [**AX.25 Throughput: Why is 9600 bps Packet Radio only twice as fast as 1200?**](Why-is-9600-only-twice-as-fast-as-1200.pdf)  [ [*download*](../../../raw/dev/doc/Why-is-9600-only-twice-as-fast-as-1200.pdf) ]

	Simply switching to a higher data rate will probably result in great disappointment.  You might expect it to be 8 times faster but it can turn out to be only twice as fast.

    In this document, we look at why a large increase in data bit rate can produce a much smaller increase in throughput.  We will explore techniques that can be used to make large improvements and drastically speed up large data transfer.




- [**Successful APRS IGate Operation**](Successful-APRS-IGate-Operation.pdf)  [ [*download*](../../../raw/dev/doc/Successful-APRS-IGate-Operation.pdf) ]


	Dire Wolf can serve as a gateway between the APRS radio network and APRS-IS servers on the Internet.

    This explains how it all works, proper configuration, and troubleshooting.

- [**Bluetooth KISS TNC**](Bluetooth-KISS-TNC.pdf)  [ [*download*](../../../raw/master/doc/Bluetooth-KISS-TNC.pdf) ]

	Eliminate the cable between your TNC and application.  Use Bluetooth instead.  

- [**APRStt Implementation Notes**](APRStt-Implementation-Notes.pdf)  [ [*download*](../../../raw/master/doc/APRStt-Implementation-Notes.pdf) ]

	Very few hams have portable equipment for APRS but nearly everyone has a handheld radio that can send DTMF tones.  APRStt allows a user, equipped with only DTMF (commonly known as Touch Tone) generation capability, to enter information into the global APRS data network.
	This document explains how the APRStt concept was implemented in the Dire Wolf application.  

- [**APRStt Interface for SARTrack**](APRStt-interface-for-SARTrack.pdf) [ [*download*](../../../raw/master/doc/APRStt-interface-for-SARTrack.pdf) ]

	This example illustrates how APRStt can be integrated with other applications such as SARTrack, APRSISCE/32, YAAC, or Xastir.  

- [**APRStt Listening Example**](APRStt-Listening-Example.pdf)  [ [*download*](../../../raw/master/doc/APRStt-Listening-Example.pdf) ]


	WB4APR described a useful application for the [QIKCOM-2 Satallite Transponder](http://www.tapr.org/pipermail/aprssig/2015-November/045035.html). 

    Don’t have your own QIKCOM-2 Satellite Transponder?  No Problem.  You can do the same thing with an ordinary computer and the APRStt gateway built into Dire Wolf.   Here’s how.

- [**Raspberry Pi APRS Tracker**](Raspberry-Pi-APRS-Tracker.pdf)   [ [*download*](../../../raw/master/doc/Raspberry-Pi-APRS-Tracker.pdf) ]

	Build a tracking device which transmits position from a GPS receiver.

- [**Raspberry Pi SDR IGate**](Raspberry-Pi-SDR-IGate.pdf)   [ [*download*](../../../raw/master/doc/Raspberry-Pi-SDR-IGate.pdf) ]

	It's easy to build a receive-only APRS Internet Gateway (IGate) with only a Raspberry Pi and a software defined radio (RTL-SDR) dongle.  Here’s how.

- [**APRS Telemetry Toolkit**](APRS-Telemetry-Toolkit.pdf)   [ [*download*](../../../raw/master/doc/APRS-Telemetry-Toolkit.pdf) ]

	Describes scripts and methods to generate telemetry.
	Includes a complete example of attaching an analog to 
	digital converter to a Raspberry Pi and transmitting 
	a measured voltage.



- [**2400 & 4800 bps PSK for APRS / Packet Radio**](2400-4800-PSK-for-APRS-Packet-Radio.pdf)  [ [*download*](../../../raw/master/doc/2400-4800-PSK-for-APRS-Packet-Radio.pdf) ]


	Double or quadruple your data rate by sending  multiple bits at the same time.

- [**Going beyond 9600 baud**](Going-beyond-9600-baud.pdf) [ [*download*](../../../raw/master/doc/Going-beyond-9600-baud.pdf) ]


	Why stop at 9600 baud?  Go faster if your soundcard and radio can handle it.

- [**AIS Reception**](AIS-Reception.pdf) [ [*download*](../../../raw/dev/doc/AIS-Reception.pdf) ]


	AIS is an international tracking system for ships.  Messages can contain position, speed, course, name, destination, status, vessel dimensions, and many other types of information.  Learn how to receive these signals with an ordindary ham transceiver and display the ship locations with APRS applications or [OpenCPN](https://opencpn.org).

- **[EAS to APRS message converter](https://github.com/wb2osz/eas2aprs)**


	The [U.S. National Weather Service](https://www.weather.gov/nwr/) (NWS) operates more than 1,000 VHF FM radio stations that continuously transmit weather information.  These stations also transmit special warnings about severe weather, disasters (natural & manmade), and public safety.

    Alerts are sent in a digital form known as Emergency Alert System (EAS) Specific Area Message Encoding (SAME). [You can hear a sample here](https://en.wikipedia.org/wiki/Specific_Area_Message_Encoding).

    It is possible to buy radios that decode these messages but what fun is that? We are ham radio operators so we want to build our own from stuff that we already have sitting around.


## Miscellaneous ##

- **[Ham Radio of Things (HRoT)](https://github.com/wb2osz/hrot)**


	Now that billions of computers and mobile phones (which are handheld computers) are all connected by the Internet, the large growth is expected from the “Internet of Things.” What is a “thing?” It could be a temperature sensor, garage door opener, motion detector, flood water level, smoke alarm, antenna rotator, coffee maker, lights, home thermostat, …, just about anything you might want to monitor or control.

    There have been other occasional mentions of merging Ham Radio with the Internet of Things but only ad hoc incompatible narrowly focused applications. Here is a proposal for a standardized more flexible method so different systems can communicate with each other.

- [**A Better APRS Packet Demodulator, part 1, 1200 baud**](A-Better-APRS-Packet-Demodulator-Part-1-1200-baud.pdf) [ [*download*](../../../raw/master/doc/A-Better-APRS-Packet-Demodulator-Part-1-1200-baud.pdf) ]

	Sometimes it's a little mystifying why an
APRS / AX.25 Packet TNC will decode some signals
and not others.  A weak signal,  buried in static,
might be fine while a nice strong clean sounding
signal is not decoded.  Here we will take a brief
look at what could cause this perplexing situation
and a couple things that can be done about it.	



- [**A Better APRS Packet Demodulator, part 2, 9600 baud**](A-Better-APRS-Packet-Demodulator-Part-2-9600-baud.pdf) [ [*download*](../../../raw/master/doc/A-Better-APRS-Packet-Demodulator-Part-2-9600-baud.pdf) ]

	In the first part of this series we discussed 1200 baud audio frequency shift keying (AFSK).  The mismatch 
	between FM 	transmitter pre-emphasis and the 
	receiver de-emphasis will 
	cause the amplitudes of the two tones to be different.
	This makes it more difficult to demodulate them accurately.
	9600 baud operation is an entirely different animal.  ...

- [**WA8LMF TNC Test CD Results a.k.a. Battle of the TNCs**](WA8LMF-TNC-Test-CD-Results.pdf)  [ [*download*](../../../raw/master/doc/WA8LMF-TNC-Test-CD-Results.pdf) ]

	How can we compare how well the TNCs perform under real world conditions?
	The de facto standard of measurement is the number of packets decoded from [WA8LMF’s TNC Test CD](http://wa8lmf.net/TNCtest/index.htm).
	Many have published the number of packets they have been able to decode from this test. Here they are, all gathered in one place, for your reading pleasure.

- [**A Closer Look at the WA8LMF TNC Test CD**](A-Closer-Look-at-the-WA8LMF-TNC-Test-CD.pdf)  [ [*download*](../../../raw/master/doc/A-Closer-Look-at-the-WA8LMF-TNC-Test-CD.pdf) ]

    Here, we take a closer look at some of the frames on the TNC Test CD in hopes of gaining some insights into why some are easily decoded and others are more difficult.
    There are a lot of ugly signals out there.   Many can be improved by decreasing the transmit volume.   Others are just plain weird and you have to wonder how they are being generated.


## Additional Documentation for Dire Wolf Software TNC #


When there was little documentation, it was all added to the source code repository [https://github.com/wb2osz/direwolf/tree/master/doc](https://github.com/wb2osz/direwolf/tree/master/doc) 

The growing number of documentation files and revisions are making the source code repository very large which means long download times.  Additional documentation, not tied to a specific release, is now being added to  [https://github.com/wb2osz/direwolf-doc](https://github.com/wb2osz/direwolf-doc) 

## Questions?  Experiences to share?  ##
 
Here are some good places to ask questions and share your experiences:

- [Dire Wolf Software TNC](https://groups.io/g/direwolf)

- [Raspberry Pi 4 Ham Radio](https://groups.io/g/RaspberryPi-4-HamRadio)

- [linuxham](https://groups.io/g/linuxham)

- [TAPR aprssig](http://www.tapr.org/pipermail/aprssig/)
 

The github "issues" section is for reporting software defects and enhancement requests.  It is NOT a place to ask questions or have general discussions.  Please use one of the locations above.
