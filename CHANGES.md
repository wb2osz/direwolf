
# Revision History #

----------

## Version 1.3  -- May 2016 ##

This is the same as the 1.3 beta test version with a few minor documentation updates.  If you are already using 1.3 beta test, there is no need to install this.

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
See "A-Better-APRS-Packet-Demodulator.pdf" for details.

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

- Added APRStt gateway capability.  For details, see:  
**APRStt-Implementation-Notes.pdf**


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

