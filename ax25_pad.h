/*-------------------------------------------------------------------
 *
 * Name:	ax25_pad.h
 *
 * Purpose:	Header file for using ax25_pad.c
 *
 *------------------------------------------------------------------*/

#ifndef AX25_PAD_H
#define AX25_PAD_H 1


#define AX25_MAX_REPEATERS 8
#define AX25_MIN_ADDRS 2	/* Destinatin & Source. */
#define AX25_MAX_ADDRS 10	/* Destination, Source, 8 digipeaters. */	

#define AX25_DESTINATION  0	/* Address positions in frame. */
#define AX25_SOURCE       1	
#define AX25_REPEATER_1   2
#define AX25_REPEATER_2   3
#define AX25_REPEATER_3   4
#define AX25_REPEATER_4   5
#define AX25_REPEATER_5   6
#define AX25_REPEATER_6   7
#define AX25_REPEATER_7   8
#define AX25_REPEATER_8   9

#define AX25_MAX_ADDR_LEN 12	/* In theory, you would expect the maximum length */
				/* to be 6 letters, dash, 2 digits, and nul for a */
				/* total of 10.  However, object labels can be 10 */
				/* characters so throw in a couple extra bytes */
				/* to be safe. */

#define AX25_MIN_INFO_LEN 0	/* Previously 1 when considering only APRS. */
				
#define AX25_MAX_INFO_LEN 2048	/* Maximum size for APRS. */
				/* AX.25 starts out with 256 as the default max */
				/* length but the end stations can negotiate */
				/* something different. */
				/* version 0.8:  Change from 256 to 2028 to */
				/* handle the larger paclen for Linux AX25. */

				/* These don't include the 2 bytes for the */
				/* HDLC frame FCS. */

/* 
 * Previously, for APRS only.
 * #define AX25_MIN_PACKET_LEN ( 2 * 7 + 2 + AX25_MIN_INFO_LEN)
 * #define AX25_MAX_PACKET_LEN ( AX25_MAX_ADDRS * 7 + 2 + AX25_MAX_INFO_LEN)
 */

/* The more general case. */
/* An AX.25 frame can have a control byte and no protocol. */

#define AX25_MIN_PACKET_LEN ( 2 * 7 + 1 )

#define AX25_MAX_PACKET_LEN ( AX25_MAX_ADDRS * 7 + 2 + 3 + AX25_MAX_INFO_LEN)


/*
 * packet_t is a pointer to a packet object.
 *
 * The actual implementation is not visible outside ax25_pad.c.
 */

#define AX25_UI_FRAME 3		/* Control field value. */
#define AX25_NO_LAYER_3 0xf0	/* protocol ID */



#ifdef AX25_PAD_C	/* Keep this hidden - implementation could change. */

struct packet_s {

	int magic1;		/* for error checking. */
	int seq;		/* unique sequence number for debugging. */

#define MAGIC 0x41583235

	struct packet_s *nextp;	/* Pointer to next in queue. */

	int num_addr;		/* Number of addresses in frame. */
				/* Range of AX25_MIN_ADDRS .. AX25_MAX_ADDRS for AX.25. */	
				/* It will be 0 if it doesn't look like AX.25. */
				/* -1 is used temporarily at allocation to mean */
				/* not determined yet. */



				/* 
 				 * The 7th octet of each address contains:
			         *
				 * Bits:   H  R  R  SSID  0
				 *
				 *   H 		for digipeaters set to 0 intially.
				 *		Changed to 1 when position has been used.
 				 *
				 *		for source & destination it is called
				 *		command/response and is normally 1.
				 *
				 *   R	R	Reserved.  Normally set to 1 1.
				 *
				 *   SSID	Substation ID.  Range of 0 - 15.
				 *
				 *   0		Usually 0 but 1 for last address.
				 */

#define SSID_H_MASK	0x80
#define SSID_H_SHIFT	7

#define SSID_RR_MASK	0x60
#define SSID_RR_SHIFT	5

#define SSID_SSID_MASK	0x1e
#define SSID_SSID_SHIFT	1

#define SSID_LAST_MASK	0x01


	int frame_len;		/* Frame length without CRC. */


	unsigned char frame_data[AX25_MAX_PACKET_LEN+1];
				/* Raw frame contents, without the CRC. */
				

	int magic2;		/* Will get stomped on if above overflows. */
};




#else			/* Public view. */

struct packet_s {
	int secret;
};

#endif


typedef struct packet_s *packet_t;



#ifdef AX25_PAD_C	/* Keep this hidden - implementation could change. */

/*
 * APRS always has one control octet of 0x03 but the more
 * general AX.25 case is one or two control bytes depending on
 * "modulo 128 operation" is in effect.  Unfortunately, it seems
 * this can be determined only by examining the XID frames and 
 * keeping this information for each connection.
 * We can assume 1 for our current purposes.
 */

static inline int ax25_get_control_offset (packet_t this_p) 
{
	//return (0);
	return (this_p->num_addr*7);
}

static inline int ax25_get_num_control (packet_t this_p)
{
	return (1);	// TODO: always be 1 for U frame.  More complicated for I and S.
}



/*
 * APRS always has one protocol octet of 0xF0 meaning no level 3
 * protocol but the more general case is 0, 1 or 2 protocol ID octets.
 */

static inline int ax25_get_pid_offset (packet_t this_p) 
{
	return (ax25_get_control_offset (this_p) + ax25_get_num_control(this_p));
}

static int ax25_get_num_pid (packet_t this_p)
{
	int c;
	int pid;

	c = this_p->frame_data[ax25_get_control_offset(this_p)];

	if ( (c & 0x01) == 0 ||				/* I   xxxx xxx0 */
	     c == 0x03 || c == 0x13) {			/* UI  000x 0011 */

	  pid = this_p->frame_data[ax25_get_pid_offset(this_p)];
	  if (pid == 0xff) {
	    return (2);			/* pid 1111 1111 means another follows. */
	  }
	  return (1);		
	}
	return (0);
}


/*
 * AX.25 has info field for 5 frame types depending on the control field.
 *
 *	xxxx xxx0	I
 *	000x 0011	UI		(which includes APRS)
 *	101x 1111	XID
 *	111x 0011	TEST
 *	100x 0111	FRMR
 *
 * APRS always has an Information field with at least one octet for the Data Type Indicator.  
 */

static inline int ax25_get_info_offset (packet_t this_p) 
{
	return (ax25_get_control_offset (this_p) + ax25_get_num_control(this_p) + ax25_get_num_pid(this_p));
}

static int ax25_get_num_info (packet_t this_p)
{
	int len;
	
	/* assuming AX.25 frame. */

	len = this_p->frame_len - this_p->num_addr * 7 - ax25_get_num_control(this_p) - ax25_get_num_pid(this_p);
	if (len < 0) {
	  len = 0;		/* print error? */
	}

	return (len);
}

#endif


typedef enum ax25_modulo_e { modulo_8 = 8, modulo_128 = 128 } ax25_modulo_t;

typedef enum ax25_frame_type_e {

	frame_type_I,		// Information
	frame_type_RR,		// Receive Ready - System Ready To Receive
	frame_type_RNR,		// Receive Not Ready - TNC Buffer Full
	frame_type_REJ,		// Reject Frame - Out of Sequence or Duplicate
	frame_type_SREJ,	// Selective Reject - Request single frame repeat
	frame_type_SABME,	// Set Async Balanced Mode, Extended
	frame_type_SABM,	// Set Async Balanced Mode
	frame_type_DISC,	// Disconnect
	frame_type_DM,		// Disconnect Mode
	frame_type_UA,		// Unnumbered Acknowledge
	frame_type_FRMR,	// Frame Reject
	frame_type_UI,		// Unnumbered Information
	frame_type_XID,		// Exchange Identification
	frame_type_TEST,	// Test
	frame_type_U,		// other Unnumbered, not used by AX.25.
	frame_not_AX25		// Could not get control byte from frame.

} ax25_frame_type_t;
	

/* 
 * Originally this was a single number. 
 * Let's try something new in version 1.2.
 * Also collect AGC values from the mark and space filters.
 */

typedef struct alevel_s {

	int rec;
	int mark;
	int space;
	//float ms_ratio;	// TODO: take out after temporary investigation.
} alevel_t;


#define AX25MEMDEBUG 1



#if AX25MEMDEBUG	// to investigate a memory leak problem


extern void ax25memdebug_set(void);
extern int ax25memdebug_get (void);
extern int ax25memdebug_seq (packet_t this_p);


extern packet_t ax25_from_text_debug (char *monitor, int strict, char *src_file, int src_line);
#define ax25_from_text(m,s) ax25_from_text_debug(m,s,__FILE__,__LINE__)

extern packet_t ax25_from_frame_debug (unsigned char *data, int len, alevel_t alevel, char *src_file, int src_line);
#define ax25_from_frame(d,l,a) ax25_from_frame_debug(d,l,a,__FILE__,__LINE__);

extern packet_t ax25_dup_debug (packet_t copy_from, char *src_file, int src_line);
#define ax25_dup(p) ax25_dup_debug(p,__FILE__,__LINE__);

extern void ax25_delete_debug (packet_t pp, char *src_file, int src_line);
#define ax25_delete(p) ax25_delete_debug(p,__FILE__,__LINE__);

#else

extern packet_t ax25_from_text (char *monitor, int strict);

extern packet_t ax25_from_frame (unsigned char *data, int len, alevel_t alevel);

extern packet_t ax25_dup (packet_t copy_from);

extern void ax25_delete (packet_t pp);

#endif


extern int ax25_parse_addr (int position, char *in_addr, int strict, char *out_addr, int *out_ssid, int *out_heard);
extern int ax25_check_addresses (packet_t pp);

extern packet_t ax25_unwrap_third_party (packet_t from_pp);

extern void ax25_set_addr (packet_t pp, int, char *);
extern void ax25_insert_addr (packet_t this_p, int n, char *ad);
extern void ax25_remove_addr (packet_t this_p, int n);

extern int ax25_get_num_addr (packet_t pp);
extern int ax25_get_num_repeaters (packet_t this_p);

extern void ax25_get_addr_with_ssid (packet_t pp, int n, char *station);
extern void ax25_get_addr_no_ssid (packet_t pp, int n, char *station);

extern int ax25_get_ssid (packet_t pp, int n);
extern void ax25_set_ssid (packet_t this_p, int n, int ssid);

extern int ax25_get_h (packet_t pp, int n);

extern void ax25_set_h (packet_t pp, int n);

extern int ax25_get_heard(packet_t this_p);

extern int ax25_get_first_not_repeated(packet_t pp);

extern int ax25_get_info (packet_t pp, unsigned char **paddr);

extern void ax25_set_nextp (packet_t this_p, packet_t next_p);

extern int ax25_get_dti (packet_t this_p);

extern packet_t ax25_get_nextp (packet_t this_p);

extern void ax25_format_addrs (packet_t pp, char *);

extern int ax25_pack (packet_t pp, unsigned char result[AX25_MAX_PACKET_LEN]);

extern ax25_frame_type_t ax25_frame_type (packet_t this_p, ax25_modulo_t modulo, char *desc, int *pf, int *nr, int *ns); 

extern void ax25_hex_dump (packet_t this_p);

extern int ax25_is_aprs (packet_t pp);

extern int ax25_get_control (packet_t this_p); 
extern int ax25_get_c2 (packet_t this_p); 

extern int ax25_get_pid (packet_t this_p);

extern unsigned short ax25_dedupe_crc (packet_t pp);

extern unsigned short ax25_m_m_crc (packet_t pp);

extern void ax25_safe_print (char *, int, int ascii_only);

#define AX25_ALEVEL_TO_TEXT_SIZE 32	// overkill but safe.
extern int ax25_alevel_to_text (alevel_t alevel, char text[AX25_ALEVEL_TO_TEXT_SIZE]);


#endif /* AX25_PAD_H */

/* end ax25_pad.h */


