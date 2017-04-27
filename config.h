
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

struct misc_config_s {

	int agwpe_port;		/* Port number for the "AGW TCPIP Socket Interface" */
	int kiss_port;		/* Port number for the "KISS" protocol. */
	int enable_kiss_pt;	/* Enable pseudo terminal for KISS. */
				/* Want this to be off by default because it hangs */
				/* after a while if nothing is reading from other end. */

	char nullmodem[20];	/* Serial port name for our end of the */
				/* virtual null modem for native Windows apps. */

	char gpsnmea_port[20];	/* Serial port name for reading NMEA sentences from GPS. */
				/* e.g. COM22, /dev/ttyACM0 */
				/* Currently no option for setting non-standard speed. */

	char gpsd_host[20];	/* Host for gpsd server. */
				/* e.g. localhost, 192.168.1.2 */

	int gpsd_port;		/* Port number for gpsd server. */
				/* Default is  2947. */

				
	char waypoint_port[20];	/* Serial port name for sending NMEA waypoint sentences */
				/* to a GPS map display or other mapping application. */
				/* e.g. COM22, /dev/ttyACM0 */
				/* Currently no option for setting non-standard speed. */

	int waypoint_formats;	/* Which sentence formats should be generated? */

#define WPT_FORMAT_NMEA_GENERIC 0x01		/* N	$GPWPT */
#define WPT_FORMAT_GARMIN       0x02		/* G	$PGRMW */
#define WPT_FORMAT_MAGELLAN     0x04		/* M	$PMGNWPL */
#define WPT_FORMAT_KENWOOD      0x08		/* K	$PKWDWPL */


	char logdir[80];	/* Directory for saving activity logs. */

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

	  int every;		/* Time between transmissions, seconds. */
				/* Remains fixed for PBEACON and OBEACON. */
				/* Dynamically adjusted for TBEACON. */

	  time_t next;		/* Unix time to transmit next one. */

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
	  float alt_m;		/* Altitude in meters. */

	  char symtab;		/* Symbol table: / or \ or overlay character. */
	  char symbol;		/* Symbol code. */

	  float power;		/* For PHG. */
	  float height;
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


