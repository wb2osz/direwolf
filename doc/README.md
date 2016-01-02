# Documentation for Dire Wolf #


## Essential Reading ##
 
- [User Guide](User-Guide.pdf)

	This is your primary source of information about installation, operation, and configuration.

- [Raspberry Pi APRS](Raspberry-Pi-APRS.pdf)

	The Raspberry Pi has some special considerations that
	make it different from other generic Linux systems.
	Start here if using the Raspberry Pi, Beaglebone Black, cubieboard2, or similar single board computers.

	
## Application Notes ##

These dive into more detail for specialized topics or typical usage scenarios.


- [APRStt Implementation Notes](APRStt-Implementation-Notes.pdf)

	Very few hams have portable equipment for APRS but nearly everyone has a handheld radio that can send DTMF tones.  APRStt allows a user, equipped with only DTMF (commonly known as Touch Tone) generation capability, to enter information into the global APRS data network.
	This document explains how the APRStt concept was implemented in the Dire Wolf application.  

- [APRStt Interface for SARTrack](APRStt-interface-for-SARTrack.pdf)

	This example illustrates how APRStt can be integrated with other applications such as SARTrack, APRSISCE/32, YAAC, or Xastir.  

- [APRStt Listening Example](APRStt-Listening-Example.pdf)

	WB4APR described a useful application for the [QIKCOM-2 Satallite Transponder](http://www.tapr.org/pipermail/aprssig/2015-November/045035.html). 

    Don’t have your own QIKCOM-2 Satellite Transponder?  No Problem.  You can do the same thing with an ordinary computer and the APRStt gateway built into Dire Wolf.   Here’s how.

- [Raspberry Pi SDR IGate](Raspberry-Pi-SDR-IGate.pdf)

	It's easy to build a receive-only APRS Internet Gateway (IGate) with only a Raspberry Pi and a software defined radio (RTL-SDR) dongle.  Here’s how.

- [APRS Telemetry Toolkit](APRS-Telemetry-Toolkit.pdf)

	Describes scripts and methods to generate telemetry.
	Includes a complete example of attaching an analog to 
	digital converter to a Raspberry Pi and transmitting 
	a measured voltage.

## Miscellaneous ##


- [A Better APRS Packet Demodulator, part 1, 1200 baud](A-Better-APRS-Packet-Demodulator-Part-1-1200-baud.pdf)

	Sometimes it's a little mystifying why an
APRS / AX.25 Packet TNC will decode some signals
and not others.  A weak signal,  buried in static,
might be fine while a nice strong clean sounding
signal is not decoded.  Here we will take a brief
look at what could cause this perplexing situation
and a couple things that can be done about it.	



- [A Better APRS Packet Demodulator, part 2, 9600 baud](A-Better-APRS-Packet-Demodulator-Part-2-9600-baud.pdf)

	In the first part of this series we discussed 1200 baud audio frequency shift keying (AFSK).  The mismatch 
	between FM 	transmitter pre-emphasis and the 
	receiver de-emphasis will 
	cause the amplitudes of the two tones to be different.
	This makes it more difficult to demodulate them accurately.
	9600 baud operation is an entirely different animal.  ...

- [WA8LMF TNC Test CD Results a.k.a. Battle of the TNCs](WA8LMF-TNC-Test-CD-Results.pdf)

	How can we compare how well the TNCs perform under real world conditions?
	The de facto standard of measurement is the number of packets decoded from [WA8LMF’s TNC Test CD](http://wa8lmf.net/TNCtest/index.htm).
	Many have published the number of packets they have been able to decode from this test. Here they are, all gathered in one place, for your reading pleasure.

- [A Closer Look at the WA8LMF TNC Test CD](A-Closer-Look-at-the-WA8LMF-TNC-Test-CD.pdf)

    Here, we take a closer look at some of the frames on the TNC Test CD in hopes of gaining some insights into why some are easily decoded and others are more difficult.
    There are a lot of ugly signals out there.   Many can be improved by decreasing the transmit volume.   Others are just plain weird and you have to wonder how they are being generated.