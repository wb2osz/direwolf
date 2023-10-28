
/*----------------------------------------------------------------------------
 * 
 * Name:	config.h
 *
 * Purpose:	
 *
 * Description:	
 *
 *-----------------------------------------------------------------------------*/


#ifndef CONFIG_H
#define CONFIG_H 1

#include "audio.h"		/* for struct audio_s */
#include "digipeater.h"		/* for struct digi_config_s */
#include "cdigipeater.h"		/* for struct cdigi_config_s */
#include "aprs_tt.h"		/* for struct tt_config_s */
#include "igate.h"		/* for struct igate_config_s */

/*
 * All the leftovers.
 * This wasn't thought out.  It just happened.
 */

enum beacon_type_e { BEACON_IGNORE, BEACON_POSITION, BEACON_OBJECT, BEACON_TRACKER, BEACON_CUSTOM, BEACON_IGATE };

enum sendto_type_e { SENDTO_XMIT, SENDTO_IGATE, SENDTO_RECV };


#define MAX_BEACONS 30
#define MAX_KISS_TCP_PORTS (MAX_CHANS+1)

struct misc_config_s {

	int agwpe_port;		/* TCP Port number for the "AGW TCPIP Socket Interface" */

	// Previously we allowed only a single TCP port for KISS.
	// An increasing number of people want to run multiple radios.
	// Unfortunately, most applications don't know how to deal with multi-radio TNCs.
	// They ignore the channel on receive and always transmit to channel 0.
	// Running multiple instances of direwolf is a work-around but this leads to
	// more complex configuration and we lose the cross-channel digipeating capability.
	// In release 1.7 we add a new feature to assign a single radio channel to a TCP port.
	// e.g.
	//	KISSPORT 8001		# default, all channels.  Radio channel = KISS channel.
	//
	//	KISSPORT 7000 0		# Only radio channel 0 for receive.
	//				# Transmit to radio channel 0, ignoring KISS channel.
	//
	//	KISSPORT 7001 1		# Only radio channel 1 for receive.  KISS channel set to 0.
	//				# Transmit to radio channel 1, ignoring KISS channel.

	int kiss_port[MAX_KISS_TCP_PORTS];	/* TCP Port number for the "TCP KISS" protocol. */
	int kiss_chan[MAX_KISS_TCP_PORTS];	/* Radio Channel number for this port or -1 for all.  */

	int kiss_copy;		/* Data from network KISS client is copied to all others. */
	int enable_kiss_pt;	/* Enable pseudo terminal for KISS. */
				/* Want this to be off by default because it hangs */
				/* after a while if nothing is reading from other end. */

	char kiss_serial_port[20];
				/* Serial port name for our end of the */
				/* virtual null modem for native Windows apps. */
				/* Version 1.5 add same capability for Linux. */

	int kiss_serial_speed;	/* Speed, in bps, for the KISS serial port. */
				/* If 0, just leave what was already there. */

	int kiss_serial_poll;	/* When using Bluetooth KISS, the /dev/rfcomm0 device */
				/* will appear and disappear as the remote application */
				/* opens and closes the virtual COM port. */
				/* When this is non-zero, we will check periodically to */
				/* see if the device has appeared and we will open it. */

	char gpsnmea_port[20];	/* Serial port name for reading NMEA sentences from GPS. */
				/* e.g. COM22, /dev/ttyACM0 */

	int gpsnmea_speed;	/* Speed for above, baud, default 4800. */

	char gpsd_host[20];	/* Host for gpsd server. */
				/* e.g. localhost, 192.168.1.2 */

	int gpsd_port;		/* Port number for gpsd server. */
				/* Default is  2947. */

				
	char waypoint_serial_port[20];	/* Serial port name for sending NMEA waypoint sentences */
				/* to a GPS map display or other mapping application. */
				/* e.g. COM22, /dev/ttyACM0 */
				/* Currently no option for setting non-standard speed. */
				/* This was done in 2014 and no one has complained yet. */

	char waypoint_udp_hostname[80];	/* Destination host when using UDP. */

	int waypoint_udp_portnum;	/* UDP port. */

	int waypoint_formats;	/* Which sentence formats should be generated? */

#define WPL_FORMAT_NMEA_GENERIC 0x01		/* N	$GPWPL */
#define WPL_FORMAT_GARMIN       0x02		/* G	$PGRMW */
#define WPL_FORMAT_MAGELLAN     0x04		/* M	$PMGNWPL */
#define WPL_FORMAT_KENWOOD      0x08		/* K	$PKWDWPL */
#define WPL_FORMAT_AIS          0x10		/* A	!AIVDM */


	int log_daily_names;	/* True to generate new log file each day. */

	char log_path[80];	/* Either directory or full file name depending on above. */

	int dns_sd_enabled;	/* DNS Service Discovery announcement enabled. */
	char dns_sd_name[64];	/* Name announced on dns-sd; defaults to "Dire Wolf on <hostname>" */

	int sb_configured;	/* TRUE if SmartBeaconing is configured. */
	int sb_fast_speed;	/* MPH */
	int sb_fast_rate;	/* seconds */
	int sb_slow_speed;	/* MPH */
	int sb_slow_rate;	/* seconds */
	int sb_turn_time;	/* seconds */
	int sb_turn_angle;	/* degrees */
	int sb_turn_slope;	/* degrees * MPH */

// AX.25 connected mode.

	int frack;		/* Number of seconds to wait for ack to transmission. */

	int retry;		/* Number of times to retry before giving up. */

	int paclen;		/* Max number of bytes in information part of frame. */

	int maxframe_basic;	/* Max frames to send before ACK.  mod 8 "Window" size. */

	int maxframe_extended;	/* Max frames to send before ACK.  mod 128 "Window" size. */

	int maxv22;		/* Maximum number of unanswered SABME frames sent before */
				/* switching to SABM.  This is to handle the case of an old */
				/* TNC which simply ignores SABME rather than replying with FRMR. */

	char **v20_addrs;	/* Stations known to understand only AX.25 v2.0 so we don't */
				/* waste time trying v2.2 first. */

	int v20_count;		/* Number of station addresses in array above. */

	char **noxid_addrs;	/* Stations known not to understand XID command so don't */
				/* waste time sending it and eventually giving up. */
				/* AX.25 for Linux is the one known case, so far, where */
				/* SABME is implemented but XID is not. */

	int noxid_count;	/* Number of station addresses in array above. */


// Beacons.
 			
	int num_beacons;	/* Number of beacons defined. */

	struct beacon_s {

	  enum beacon_type_e btype;	/* Position or object. */

	  int lineno;		/* Line number from config file for later error messages. */

	  enum sendto_type_e sendto_type;

				/* SENDTO_XMIT	- Usually beacons go to a radio transmitter. */
				/*		  chan, below is the channel number. */
				/* SENDTO_IGATE	- Send to IGate, probably to announce my position */
				/* 		  rather than relying on someone else to hear */
				/* 		  me on the radio and report me. */
				/* SENDTO_RECV	- Pretend this was heard on the specified */
				/* 		  radio channel.  Mostly for testing. It is a */
				/* 		  convenient way to send packets to attached apps. */

	  int sendto_chan;	/* Transmit or simulated receive channel for above.  Should be 0 for IGate. */

	  int delay;		/* Seconds to delay before first transmission. */

	  int slot;		/* Seconds after hour for slotted time beacons. */
				/* If specified, it overrides any 'delay' value. */

	  int every;		/* Time between transmissions, seconds. */
				/* Remains fixed for PBEACON and OBEACON. */
				/* Dynamically adjusted for TBEACON. */

	  time_t next;		/* Unix time to transmit next one. */

	  char *source;		/* NULL or explicit AX.25 source address to use */
				/* instead of the mycall value for the channel. */

	  char *dest;		/* NULL or explicit AX.25 destination to use */
				/* instead of the software version such as APDW11. */

	  int compress;		/* Use more compact form? */

	  char objname[10];	/* Object name.  Any printable characters. */

	  char *via;		/* Path, e.g. "WIDE1-1,WIDE2-1" or NULL. */

	  char *custom_info;	/* Info part for handcrafted custom beacon. */
				/* Ignore the rest below if this is set. */

	  char *custom_infocmd;	/* Command to generate info part. */
				/* Again, other options below are then ignored. */

	  int messaging;	/* Set messaging attribute for position report. */
				/* i.e. Data Type Indicator of '=' rather than '!' */

	  double lat;		/* Latitude and longitude. */
	  double lon;
	  int ambiguity;	/* Number of lower digits to trim from location. 0 (default), 1, 2, 3, 4. */
	  float alt_m;		/* Altitude in meters. */

	  char symtab;		/* Symbol table: / or \ or overlay character. */
	  char symbol;		/* Symbol code. */

	  float power;		/* For PHG. */
	  float height;		/* HAAT in feet */
	  float gain;		/* Original protocol spec was unclear. */
				/* Addendum 1.1 clarifies it is dBi not dBd. */

	  char dir[3];		/* 1 or 2 of N,E,W,S, or empty for omni. */

	  float freq;		/* MHz. */
	  float tone;		/* Hz. */
	  float offset;		/* MHz. */
	
	  char *comment;	/* Comment or NULL. */
	  char *commentcmd;	/* Command to append more to Comment or NULL. */


	} beacon[MAX_BEACONS];

};


#define MIN_IP_PORT_NUMBER 1024
#define MAX_IP_PORT_NUMBER 49151


#define DEFAULT_AGWPE_PORT 8000		/* Like everyone else. */
#define DEFAULT_KISS_PORT 8001		/* Above plus 1. */


#define DEFAULT_NULLMODEM "COM3"  	/* should be equiv. to /dev/ttyS2 on Cygwin */




extern void config_init (char *fname, struct audio_s *p_modem, 
			struct digi_config_s *digi_config,
			struct cdigi_config_s *cdigi_config,
			struct tt_config_s *p_tt_config,
			struct igate_config_s *p_igate_config,
			struct misc_config_s *misc_config);



#endif /* CONFIG_H */

/* end config.h */


