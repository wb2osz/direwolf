
# Revision History #


## Version 1.7  --  October 2023 ##


### New Documentation: ###

Additional documentation location to slow down growth of main repository.  [https://github.com/wb2osz/direwolf-doc](https://github.com/wb2osz/direwolf-doc) .   These are more oriented toward achieving a goal and understanding, as opposed to the User Guide which describes the functionality.

- ***APRS Digipeaters***

- ***Internal Packet Routing***

- ***Radio Interface Guide***

- ***Successful IGate Operation***

- ***Understanding APRS Packets***


### New Features: ###



- New ICHANNEL configuration option to map a KISS client application channel to APRS-IS. Packets from APRS-IS will be presented to client applications as the specified channel. Packets sent, by client applications, to that channel will go to APRS-IS rather than a radio channel.  Details in ***Internal-Packet-Routing.pdf***.

- New variable speed option for gen_packets. For example,  "-v 5,0.1" would generate packets from 5% too slow to 5% too fast with increments of 0.1.  Some implementations might have imprecise timing.  Use this to test how well TNCs tolerate sloppy timing.

- Improved Layer 2 Protocol [(IL2P)](https://en.wikipedia.org/wiki/FX.25_Forward_Error_Correction).    Compatible with Nino TNC for 1200 and 9600 bps.  Use "-I 1" on command line to enable transmit for first channel.  For more general case, add to config file (simplified version, see User Guide for more details):

    > After:   "CHANNEL 1"   (or other channel)
    >
    > Add:     "IL2PTX 1"

- Limited support for CM108/CM119 GPIO PTT on Windows.

- Dire Wolf now advertises itself using DNS Service Discovery.  This allows suitable APRS / Packet Radio applications to find a network KISS TNC without knowing the IP address or TCP port.    Thanks to Hessu for providing this.  Currently available only for Linux and Mac OSX.  [Read all about it here.](https://github.com/hessu/aprs-specs/blob/master/TCP-KISS-DNS-SD.md)

- The transmit calibration tone (-x) command line option now accepts a radio channel number and/or a single letter mode:  a = alternate tones, m = mark tone, s = space tone, p = PTT only no sound.

- The BEACON configuration now recognizes the SOURCE= option.  This replaces the AX.25 source address rather than using the MYCALL value for the channel.  This is useful for sending more than 5 analog telemetry channels.  Use two, or more, source addresses with up to 5 analog channels each.

- For more flexibility, the FX.25 transmit property can now be set individually by channel, rather than having a global setting for all channels.  The -X on the command line applies only to channel 0.  For other channels you need to add a new line to the configuration file.  You can specify a specific number of parity bytes (16, 32, 64) or 1 to choose automatically based on packet size.

    > After:   "CHANNEL 1"   (or other channel)
    >
    > Add:     "FX25TX 1" (or 16 or 32 or 64)



### Bugs Fixed: ###

- The t/m packet filter incorrectly included bulletins.  It now allows only "messages" to specific stations.  Use of t/m is discouraged.  i/180 is the preferred filter for messages to users recently heard locally.

- Packet filtering now skips over any third party header before classifying packet types.

- Fixed build for Alpine Linux.

### Notes: ###

The Windows binary distribution now uses gcc (MinGW) version 11.3.0.
The Windows version is built for both 32 and 64 bit operating systems.
Use the 64 bit version if possible; it runs considerably faster.

## Version 1.6  --  October 2020 ##

### New Build Procedure: ###


- Rather than trying to keep a bunch of different platform specific Makefiles in sync, "cmake" is now used for greater portability and easier maintenance.  This was contributed by Davide Gerhard.

- README.md has a quick summary of the process.  More details in the ***User Guide***.


### New Features: ###


- "-X" option enables FX.25 transmission.  FX.25 reception is always enabled so you don't need to do anything special.  "What is FX.25?" you might ask.  It is forward error correction (FEC) added in a way that is completely compatible with an ordinary AX.25 frame.  See new document ***AX25\_plus\_FEC\_equals\_FX25.pdf*** for details.

- Receive AIS location data from ships.  Enable by using "-B AIS" command line option or "MODEM AIS" in the configuration file.  AIS NMEA sentences are encapsulated in APRS user-defined data with a "{DA" prefix.  This uses 9600 bps so you need to use wide band audio, not what comes out of the speaker.  There is also a "-A" option to generate APRS Object Reports.

- Receive Emergency Alert System (EAS) Specific Area Message Encoding (SAME).  Enable by using "-B EAS" command line option or "MODEM EAS" in the configuration file.  EAS SAME messages are encapsulated in APRS user-defined data with a "{DE" prefix.  This uses low speed AFSK so speaker output is fine.

- "-t" option now accepts more values to accommodate inconsistent handling of text color control codes by different terminal emulators.  The default, 1, should work with most modern terminal types.  If the colors are not right, try "-t 9" to see the result of the different choices and pick the best one.  If none of them look right, file a bug report and specify: operating system version (e.g. Raspbian Buster), terminal emulator type and version (e.g.  LXTerminal 0.3.2).   Include a screen capture.


- "-g" option to force G3RUH mode for lower speeds where a different modem type may be the default.

- 2400 bps compatibility with MFJ-2400.  See ***2400-4800-PSK-for-APRS-Packet-Radio.pdf*** for details

- "atest -h" will display the frame in hexadecimal for closer inspection.

- Add support for Multi-GNSS NMEA sentences.



### Bugs Fixed: ###

- Proper counting of frames in transmit queue for AGW protocol 'Y' command.



### New Documentation: ###

- ***AX.25 + FEC = FX.25***

- ***AIS Reception***

- ***AX.25 Throughput: Why is 9600 bps Packet Radio only twice as fast as 1200?***

- [***Ham Radio of Things (HRoT) - IoT over Ham Radio***](https://github.com/wb2osz/hrot)

- [***EAS SAME to APRS Message Converter***](https://github.com/wb2osz/eas2aprs)

- [***Dire Wolf PowerPoint Slide Show***](https://github.com/wb2osz/direwolf-presentation)

### Notes: ###

The Windows binary distribution now uses gcc (MinGW) version 7.4.0.
The Windows version is built for both 32 and 64 bit operating systems.
Use the 64 bit version if possible; it runs considerably faster.



## Version 1.5  --  September 2018 ##


### New Features: ###

- PTT using GPIO pin of CM108/CM119 (e.g. DMK URI, RB-USB RIM), Linux only.

- More efficient error recovery for AX.25 connected mode.  Better generation and processing of REJ and SREJ to reduce unnecessary duplicate "**I**" frames.

- New configuration option, "**V20**", for listing stations known to not understand AX.25 v2.2.  This will speed up connection by going right to SABM and not trying SABME first and failing.

- New "**NOXID**" configuration file option to avoid sending XID command to listed station(s).  If other end is a partial v2.2 implementation, which recognizes SABME, but not XID, we would waste a lot of time resending XID many times before giving up.   This is less drastic than the "**V20**" option which doesn't even attempt to use v2.2 with listed station(s).

- New application "**kissutil**" for troubleshooting a KISS TNC or interfacing to an application via files.

- KISS "Set Hardware" command to report transmit queue length.

- TCP KISS can now handle multiple concurrent applications.

- Linux can use serial port for KISS in addition to a pseudo terminal.

- decode_aprs utility can now accept KISS frames and AX.25 frames as series of two digit hexadecimal numbers.

- Full Duplex operation.  (Put "FULLDUP ON" in channel section of configuration file.)

- Time slots for beaconing.

- Allow single log file with fixed name rather than starting a new one each day.



### Bugs Fixed: ###

- Possible crash when CDIGIPEAT did not include the optional alias.

- PACLEN configuration item no longer restricts length of received frames.

- Strange failures when trying to use multiple KISS client applications over TCP.  Only Linux was affected.  

- Under certain conditions, outgoing connected mode data would get stuck in a queue and not be transmitted.  This could happen if client application sends a burst of data larger than the "window" size (MAXFRAME or EMAXFRAME option).


- Little typographical / spelling errors in messages.


### Documentation: ###


- New document ***Bluetooth-KISS-TNC.pdf*** explaining how to use KISS over Bluetooth.

- Updates describing cheap SDR frequency inaccuracy and how to compensate for it.

### Notes: ###

Windows binary distribution now uses gcc (MinGW) version 6.3.0.

----------

## Version 1.4  -- April 2017 ##


### New Features: ###

- AX.25 v2.2 connected mode.  See chapter 10 of User Guide for details.

- New client side packet filter to select "messages" only to stations that have been heard nearby recently.  This is now the default if no IS to RF filter is specified.

- New beacon type, IBEACON, for sending IGate statistics.

- Expanded debug options so you can understand what is going on with packet filtering.

- Added new document ***Successful-APRS-IGate-Operation.pdf*** with IGate background, configuration, and troubleshooting tips.
- 2400 & 4800 bps PSK modems.  See ***2400-4800-PSK-for-APRS-Packet-Radio.pdf*** in the doc directory for discussion.

- The top speed of 9600 bps has been increased to 38400.  You will need a sound card capable of 96k or 192k samples per second for the higher rates.  Radios must also have adequate bandwidth.  See ***Going-beyond-9600-baud.pdf*** in the doc directory for more details.

- Better decoder performance for 9600 and higher especially for low audio sample rate to baud ratios.

- Generate waypoint sentences for use by AvMap G5 / G6 or other mapping devices or applications.   Formats include
 - $GPWPL	- NMEA generic with only location and name.
 - $PGRMW	- Garmin, adds altitude, symbol, and comment to previously named waypoint.
 - $PMGNWPL	- Magellan, more complete for stationary objects.
 - $PKWDWPL	- Kenwood with APRS style symbol but missing comment.


- DTMF tones can be sent by putting "DTMF" in the destination address,  similar to the way that Morse Code is sent.

- Take advantage of new 'gpio' group and new /sys/class/gpio ownership in Raspbian Jessie.

- Handle more complicated gpio naming for CubieBoard, etc.

- More flexible dw-start.sh start up script for both GUI and CLI environments.


 
### Bugs Fixed: ###

- The transmitter (PTT control) was being turned off too soon when sending Morse Code.

- The -qd (quiet decode) command line option now suppresses errors about improperly formed Telemetry packets.

- Longer tocall.txt files can now be handled.  

- Sometimes kissattach would have an issue with the Dire Wolf pseudo terminal.  This showed up most often on Raspbian but sometimes occurred with other versions of Linux.

	*kissattach: Error setting line discipline: TIOCSETD: Device or resource busy
	Are you sure you have enabled MKISS support in the kernel
	or, if you made it a module, that the module is loaded?*
	

- Sometimes writes to a pseudo terminal would block causing the received
frame processing thread to hang.   The first thing you will notice is that
received frames are not being printed.  After a while this message will appear:

  	*Received frame queue is out of control. Length=... Reader thread is probably 
  	frozen.  This can be caused by using a pseudo terminal (direwolf -p) where 
  	another application is not reading the frames from the other side.*

- -p command line option caused segmentation fault with glibc >= 2.24.


- The Windows version 1.3 would crash when starting to transmit on Windows XP. There have also been some other reports of erratic behavior on Windows. The crashing problem was fixed in in the 1.3.1 patch release.   Linux version was not affected.

- IGate did not retain nul characters in the information part of a packet.  This should never happen with a valid APRS packet but there are a couple cases where it has.  If we encounter these malformed packets, pass them along as-is, rather than truncating.

- Don't digipeat packets when the source is my call.



----------

## Version 1.3  -- May 2016 ##

### New Features: ###

- Support for Mac OS X. 

- Many APRStt enhancements including: Morse code and speech responses to to APRStt tone sequences, new 5 digit callsign suffix abbreviation, 
position ambiguity for latitude and longitude in object reports

- APRS Telemetry Toolkit.
 
- GPS Tracker beacons are now available for the Windows version.  Previously this was only in the Linux version.

- SATgate mode for IGate.  Packets heard directly are delayed before being sent
to the Internet Server.   This favors digipeated packets because the original
arrives later and gets dropped if there are duplicates.

- Added support for hamlib. This provides more flexible options for PTT control.

- Implemented AGW network protocol 'M' message for sending UNPROTO information without digipeater path.


- A list of all symbols available can be obtained with the -S
command line option.

- Command line option "-a n" to print audio device statistics each n seconds.  Previously this was always each 100 seconds on Linux and not available on Windows.

### Bugs Fixed: ###



- Fixed several cases where crashes were caused by unexpected packet contents:

 - When receiving packet with unexpected form of GPS NMEA sentence.

 - When receiving packet with comment of a few hundred characters.
 
 - Address in path, from Internet server, more than 9 characters.

- "INTERNAL ERROR: dlq_append NULL packet pointer." when using PASSALL.

- In Mac OSX version:  Assertion failed: (adev[a].inbuf_size_in_bytes >= 100 &&   adev[a].inbuf_size_in_bytes <= 32768), function audio_get, file audio_portaudio.c, line 917.

- Tracker beacons were not always updating the location properly.

- AGW network protocol now works properly for big-endian processors
such as PowerPC or MIPS.

- Packet filtering treated telemetry metadata as messages rather than telemetry.

----------

## Version 1.2  -- June 2015 ##

### New Features ###

- Improved decoder performance.  
Over 1000 error-free frames decoded from WA8LMF TNC Test CD.  
See ***A-Better-APRS-Packet-Demodulator-Part-1-1200-baud.pdf*** for details.

- Up to 3 soundcards and 6 radio channels can be handled at the same time.

- New framework for applications which listen for Touch Tone commands
and respond with voice.  A sample calculator application is included
as a starting point for building more interesting applications.  
For example, if it hears the DTMF sequence "2*3*4#" it will respond 
with the spoken words "Twenty Four."  

- Reduced latency for transfers to/from soundcards.

- More accurate transmit PTT timing.

- Packet filtering for digipeater and IGate.

- New command line -q (quiet) option to suppress some types of output.

- Attempted fixing of corrupted bits now works for 9600 baud.

- Implemented AGW network protocol 'y' message so applications can
throttle generation of packets when sending a large file.

- When using serial port RTS/DTR to activate transmitter, the two
control lines can now be driven with opposite polarity as required
by some interfaces.

- Data Carrier Detect (DCD) can be sent to an output line (just 
like PTT) to activate a carrier detect light.

- Linux "man" pages for on-line documentation.

- AGWPORT and KISSPORT can be set to 0 to disable the interfaces.

- APRStt gateway enhancements:  MGRS/USNG coordinates, new APRStt3
format call, satellite grid squares.


### Bugs fixed ###

- Fixed "gen_packets" so it now handles user-specified messages correctly.

- Under some circumstances PTT would be held on long after the transmit
audio was finished.



### Known problems ###

- Sometimes writes to a pseudo terminal will block causing the received
frame processing thread to hang.   The first thing you will notice is that
received frames are not being printed.  After a while this message will appear:

  Received frame queue is out of control. Length=... Reader thread is probably 
  frozen.  This can be caused by using a pseudo terminal (direwolf -p) where 
  another application is not reading the frames from the other side.

-----------

## Version 1.1  -- December 2014 ##

### New Features ###

- Logging of received packets and utility to convert log file
into GPX format.

- AGW network port formerly allowed only one connection at a 
time.  It can now accept 3 client applications at the same time.  
(Same has not yet been done for network KISS port.)

- Frequency / offset / tone standard formats are now recognized.
Non-standard attempts, in the comment, are often detected and
a message suggests the correct format.

- Telemetry is now recognized.  Messages are printed for
usage that does not adhere to the published standard.

- Tracker function transmits location from GPS position.
New configuration file options: TBEACON and SMARTBEACONING.
(For Linux only.   Warning - has not been well tested.)

- Experimental packet regeneration feature for HF use.
Will be documented later if proves to be useful...

- Several enhancements for trying to fix incorrect CRC: 
Additional types of attempts to fix a bad CRC.
Optimized code to reduce execution time.
Improved detection of duplicate packets from different fixup attempts.
Set limit on number of packets in fix up later queue.

- Beacon positions can be specified in either latitude / longitude
or UTM coordinates.

- It is still highly recommended, but no longer mandatory, that
beaconing be enabled for digipeating to work.

* Bugs fixed:

- For Windows version, maximum serial port was COM9.
It is now possible to use COM10 and higher.

- Fixed issue with KISS protocol decoder state that showed up
only with "binary" data in packets (e.g.  RMS Express).

- An extra 00 byte was being appended to packets from AGW
network protocol 'K' messages.

- Invalid data from an AGW client application could cause an
application crash.

- OSS (audio interface for non-Linux versions of Unix) should 
be better now.

### Known problems ###

- Sometimes kissattach fails to connect with "direwolf -p".
The User Guide and Raspberry Pi APRS document have a couple work-arounds.

-----------

## Version 1.0a	-- May 2014 ##

### Bug fixed ###

- Beacons sent directly to IGate server had incorrect source address.

-----------

## Version 1.0 -- May 2014 ##

### New Features ###

- Received audio can be obtained with a UDP socket or stdin.
This can be used to take audio from software defined radios
such as rtl_fm or gqrx.

- 9600 baud data rate.

- New PBEACON and OBEACON configuration options. Previously
it was necessary to handcraft beacons. 

- Less CPU power required for 300 baud.  This is important
if you want to run a bunch of decoders at the same time
to tolerate off-frequency HF SSB signals.

- Improved support for UTF-8 character set.

- Improved troubleshooting display for APRStt macros.

- In earlier versions, the DTMF decoder was always active because it 
took a negligible amount of CPU time.  Unfortunately this sometimes 
resulted in too many false positives from some other types of digital 
transmissions heard on HF. Starting in version 1.0, the DTMF decoder 
is enabled only when the APRStt gateway is configured.


-----------

## Version 0.9 --November 2013 ##

### New Features ###

- Selection of non-default audio device for Linux ALSA.

- Simplified audio device set up for Raspberry Pi.

- GPIO lines can be used for PTT on suitable Linux systems.

- Improved 1200 baud decoder.

- Multiple decoders per channel to tolerate HF SSB signals off frequency.

- Command line option "-t 0" to disable text colors.

- APRStt macros which allow short numeric only touch tone
sequences to be processed as much longer predefined sequences.


### Bugs Fixed ###

- Now works on 64 bit target.

### New Restriction for Windows version ###

- Minimum processor is now Pentium 3 or equivalent or later.
It's possible to run on something older but you will need
to rebuild it from source.


-----------

## Version 0.8	-- August 2013 ##

### New Features ###

- Internet Gateway (IGate) including IPv6 support.

- Compatibility with YAAC.

- Preemptive digipeating option.

- KISS TNC should now work with connected AX.25 protocols
(e.g. AX25 for Linux), not just APRS.


----------

## Version 0.7	-- March 2013 ##

### New Features: ###

- Added APRStt gateway capability.  For details, see  ***APRStt-Implementation-Notes.pdf***


-----------

## Version 0.6 --	February 2013 ##

### New Features ###

- Improved performance of AFSK demodulator.
Now decodes 965 frames from Track 2 of WA8LMF's TNC Test CD.

- KISS protocol now available thru a TCP socket.
Default port is 8001.
Change it with KISSPORT option in configuration file.

- Ability to salvage frames with bad FCS.
See section mentioning "bad apple" in the user guide.
Default of fixing 1 bit works well.  
Fixing more bits not recommended because there is a high
probability of occasional corrupted data getting thru.

- Added AGW "monitor" format messages.
Now compatible with APRS-TW for telemetry.


### Known Problem ###

- The Linux (but not Cygwin) version eventually hangs if nothing is
reading from the KISS pseudo terminal.  Some operating system
queue fills up, the application write blocks, and decoding stops.


### Workaround ###

- If another application is not using the serial KISS interface,
run this in another window:

	tail -f /tmp/kisstnc

-----------

## Version 0.5 -- March 2012 ##

- More error checking and messages for invalid APRS data.

-----------

## Version 0.4 -- September 2011 ##

- First general availability.

