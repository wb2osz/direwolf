
/* aprs_tt.h */

#ifndef APRS_TT_H
#define APRS_TT_H 1



/*
 * For holding location format specifications from config file.
 * Same thing is also useful for macro definitions.
 * We have exactly the same situation of looking for a pattern
 * match and extracting fixed size groups of digits.
 */

struct ttloc_s {
	enum { TTLOC_POINT, TTLOC_VECTOR, TTLOC_GRID, TTLOC_UTM, TTLOC_MGRS, TTLOC_USNG, TTLOC_MACRO, TTLOC_MHEAD, TTLOC_SATSQ, TTLOC_AMBIG } type;

	char pattern[20];	/* e.g. B998, B5bbbdddd, B2xxyy, Byyyxxx, BAxxxx */
				/* For macros, it should be all fixed digits, */
				/* and the letters x, y, z.  e.g.  911, xxyyyz */

	union {

	  struct {	
	    double lat;		/* Specific locations. */
	    double lon;
	  } point;

	  struct {
	    double lat;		/* For bearing/direction. */
	    double lon;
	    double scale;	/* conversion to meters */
	  } vector;

	  struct {
	    double lat0;	/* yyy all zeros. */
	    double lon0;	/* xxx */
	    double lat9;	/* yyy all nines. */
	    double lon9;	/* xxx */
	  } grid;

	  struct {
	    double scale;
	    double x_offset;
	    double y_offset;
	    long lzone;		/* UTM zone, should be 1-60 */
	    char latband;	/* Latitude band if specified, otherwise space or - */
	    char hemi;		/* UTM Hemisphere, should be 'N' or 'S'. */
	  } utm;

	  struct {
	    char zone[8];	/* Zone and square for USNG/MGRS */
	  } mgrs;

	  struct {
	    char prefix[24];	/* should be 10, 6, or 4 digits to be */
				/* prepended to the received sequence. */
	  } mhead;

	  struct {
	    char *definition;
	  } macro;

	};
};


/* Error codes for sending responses to user. */

#define TT_ERROR_OK		0	/* Success. */
#define TT_ERROR_D_MSG		1	/* D was first char of field.  Not implemented yet. */
#define TT_ERROR_INTERNAL	2	/* Internal error.  Shouldn't be here. */
#define TT_ERROR_MACRO_NOMATCH	3	/* No definition for digit sequence. */
#define TT_ERROR_BAD_CHECKSUM	4	/* Bad checksum on call. */
#define TT_ERROR_INVALID_CALL	5	/* Invalid callsign. */
#define TT_ERROR_INVALID_OBJNAME 6	/* Invalid object name. */
#define TT_ERROR_INVALID_SYMBOL	7	/* Invalid symbol specification. */
#define TT_ERROR_INVALID_LOC	8	/* Invalid location. */
#define TT_ERROR_NO_CALL	9	/* No call or object name included. */
#define TT_ERROR_INVALID_MHEAD	10	/* Invalid Maidenhead Locator. */
#define TT_ERROR_INVALID_SATSQ	11	/* Satellite square must be 4 digits. */
#define TT_ERROR_SUFFIX_NO_CALL 12	/* No known callsign for suffix. */

#define TT_ERROR_MAXP1		13	/* Number of items above.  i.e. Last number plus 1. */


#if CONFIG_C		/* Is this being included from config.c? */

/* Must keep in sync with above !!! */

static const char *tt_msg_id[TT_ERROR_MAXP1] = {
	"OK",
	"D_MSG",
	"INTERNAL",
	"MACRO_NOMATCH",
	"BAD_CHECKSUM",
	"INVALID_CALL",
	"INVALID_OBJNAME",
	"INVALID_SYMBOL",
	"INVALID_LOC",
	"NO_CALL",
	"INVALID_MHEAD",
	"INVALID_SATSQ",
	"SUFFIX_NO_CALL"
};

#endif

/* 
 * Configuration options for APRStt.
 */

#define TT_MAX_XMITS 10

#define TT_MTEXT_LEN 64


struct tt_config_s {

	int gateway_enabled;		/* Send DTMF sequences to APRStt gateway. */

	int obj_recv_chan;		/* Channel to listen for tones. */

	int obj_xmit_chan;		/* Channel to transmit object report. */
					/* -1 for none.  This could happen if we */
					/* are only sending to application */
					/* and/or IGate. */

	int obj_send_to_app;		/* send to attached application(s). */

	int obj_send_to_ig;		/* send to IGate. */

	char obj_xmit_via[AX25_MAX_REPEATERS * (AX25_MAX_ADDR_LEN+1)];	
					/* e.g.  empty or "WIDE2-1,WIDE1-1" */
	
	int retain_time;		/* Seconds to keep information about a user. */

	int num_xmits;			/* Number of times to transmit object report. */
				
	int xmit_delay[TT_MAX_XMITS];	/* Delay between them. */
					/* e.g.  3 seconds before first transmission then */
					/* delays of 16, 32, seconds etc. in between repeats. */

	struct ttloc_s *ttloc_ptr;	/* Pointer to variable length array of above. */
	int ttloc_size;			/* Number of elements allocated. */
	int ttloc_len;			/* Number of elements actually used. */

	double corral_lat;		/* The "corral" for unknown locations. */
	double corral_lon;
	double corral_offset;
	int corral_ambiguity;

	char status[10][TT_MTEXT_LEN];		/* Up to 9 status messages. e.g.  "/enroute" */
						/* Position 0 means none and can't be changed. */

	struct {
	  char method[AX25_MAX_ADDR_LEN];	/* SPEECH or MORSE[-n] */
	  char mtext[TT_MTEXT_LEN];		/* Message text. */
	} response[TT_ERROR_MAXP1];

	char ttcmd[80];			/* Command to generate custom audible response. */
};



	
void aprs_tt_init (struct tt_config_s *p_config, int debug);

void aprs_tt_button (int chan, char button);





#define APRSTT_LOC_DESC_LEN 32		/* Need at least 26 */

#define APRSTT_DEFAULT_SYMTAB '\\'
#define APRSTT_DEFAULT_SYMBOL 'A'


void aprs_tt_dao_to_desc (char *dao, char *str);

void aprs_tt_sequence (int chan, char *msg);

int dw_run_cmd (char *cmd, int oneline, char *result, size_t resultsiz);


#endif

/* end aprs_tt.h */