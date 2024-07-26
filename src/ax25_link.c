//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2016, 2017, 2018, 2023  John Langner, WB2OSZ
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//


/*------------------------------------------------------------------
 *
 * Name:	ax25_link
 *
 * Purpose:	Data Link State Machine.
 *		Establish connections and transfer data in the proper
 *		order with retries.
 *
 *		Using the term "data link" is rather unfortunate because it causes
 *		confusion to someone familiar with the OSI networking model.
 *		This corresponds to the layer 4 transport, not layer 2 data link.
 *
 * Description:	
 *
 *		Typical sequence for establishing a connection
 *		initiated by a client application.  Try version 2.2,
 *		get refused, and fall back to trying version 2.0.
 *
 *
 *	State		Client App		State Machine		Peer
 *	-----		----------		-------------		----
 *
 *	0 disc
 *			Conn. Req  --->
 *						SABME  --->
 *	5 await 2.2
 *								  <---	FRMR or DM *note
 *						SABM  --->
 *	1 await 2.0
 *								  <---	UA
 *					  <---	CONN Ind.
 *	3 conn
 *
 *
 *		Typical sequence when other end initiates connection.
 *
 *
 *	State		Client App		State Machine		Peer
 *	-----		----------		-------------		----
 *
 *	0 disc
 *								  <---	SABME or SABM
 *						UA  --->
 *					  <---	CONN Ind.
 *	3 conn
 *
 *
 *	*note:
 *
 * 	After carefully studying the v2.2 spec, I expected a 2.0 implementation to send
 *	FRMR in response to SABME.  This is important.  If a v2.2 implementation
 *	gets FRMR, in response to SABME, it switches to v2.0 and sends SABM instead.
 *
 *	The v2.0 protocol spec, section 2.3.4.3.3.1, states that FRMR should be sent when
 *	an invalid or not implemented command is received.  That all fits together.
 *
 *	In testing, I found that the KPC-3+ sent DM.
 *
 *	I can see where they might get that idea.
 *	The v2.0 spec says that when in disconnected mode, it should respond to any
 *	command other than SABM or UI frame with a DM response with P/F set to 1.
 *	I think it was implemented wrong.  2.3.4.3.3.1 should take precedence.
 *
 *	The TM-D710 does absolutely nothing in response to SABME.
 *	Not responding at all is just plain wrong.   To work around this, I put
 *	in a special hack to start sending SABM after a certain number of
 *	SABME go unanswered.   There is more discussion in the User Guide.
 *
 * References:
 *		* AX.25 Amateur Packet-Radio Link-Layer Protocol Version 2.0, October 1984
 *
 * 			https://www.tapr.org/pub_ax25.html
 *			http://lea.hamradio.si/~s53mv/nbp/nbp/AX25V20.pdf
 *
 *			At first glance, they look pretty much the same, but the second one
 *			is more complete with 4 appendices, including a state table.
 *
 *		* AX.25 Link Access Protocol for Amateur Packet Radio Version 2.2 Revision: July 1998
 *
 *			https://www.tapr.org/pdf/AX25.2.2.pdf
 *
 *		* AX.25 Link Access Protocol for Amateur Packet Radio Version 2.2 Revision: July 1998
 *
 *			http://www.ax25.net/AX25.2.2-Jul%2098-2.pdf
 *
 *			I accidentally stumbled across this one when searching for some sort of errata
 *			list for the original protocol specification. 
 *
 *				"This is a new version of the 1998 standard. It has had all figures
 *				 redone using Microsoft Visio. Errors in the SDL have been corrected."
 *
 *			The SDL diagrams are dated 2006.  I wish I had known about this version, with
 *			several corrections, before doing most of the implementation.  :-(
 *
 *			The title page still says July 1998 so it's not immediately obvious this
 *			is different than the one on the TAPR site.
 *
 *		* AX.25  ...  Latest revision, in progress.
 *
 *			http://www.nj7p.org/
 *
 *			This is currently being revised in cooperation with software authors
 *			who have noticed some issues during implementation.
 *
 *		The functions here are based on the SDL diagrams but turned inside out.
 *		It seems more intuitive to have a function for each type of input and then decide
 *		what to do depending on the state.  This also reduces duplicate code because we
 *		often see the same flow chart segments, for the same input, appearing in multiple states.
 *
 * Errata:	The protocol spec has many places that appear to be errors or are ambiguous so I wasn't
 *		sure what to do.  These should be annotated with "erratum" comments so we can easily go
 *		back and revisit them.
 *
 * X.25:	The AX.25 protocol is based on, but does not necessarily adhere to, the X.25 protocol.
 *		Consulting this might provide some insights where the AX.25 spec is not clear.
 *
 *			http://www.itu.int/rec/T-REC-X.25-199610-I/en/
 *  
 * Version 1.4, released April 2017:
 *
 *		Features tested reasonably well:
 *
 *			Connect to/from a KPC-3+ and send I frames in both directions.
 *			Same with TM-D710A.
 *			v2.2 connect between two instances of direwolf.  (Can't find another v2.2 for testing.)
 *			Modulo 8 & 128 sequence numbers.
 *			Recovery from simulated transmission errors using either REJ or SREJ.
 *			XID frame for parameter negotiation.
 *			Segments to allow data larger than max info part size.
 *
 *		Implemented but not tested properly:
 *
 *			Connecting thru digipeater(s).
 *			Acting as a digipeater.
 *			T3 timer.
 *			Compatibility with additional types of TNC.
 *
 * Version 1.5, December 2017:
 *
 *		Implemented Multi Selective Reject.
 *		More efficient generation of SREJ frames.
 *		Reduced number of duplicate I frames sent for both REJ and SREJ cases.
 *		Avoided unnecessary RR when I frame could take care of the ack.
 *		(This led to issue 132 where outgoing data sometimes got stuck in the queue.)
 *
 *------------------------------------------------------------------*/

#include "direwolf.h"


#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>


#include "ax25_pad.h"
#include "ax25_pad2.h"
#include "xid.h"
#include "textcolor.h"
#include "dlq.h"
#include "tq.h"
#include "ax25_link.h"
#include "dtime_now.h"
#include "server.h"
#include "ptt.h"


#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

// Debug switches for different types of information.
// Should have command line options instead of changing source and recompiling.

static int s_debug_protocol_errors = 0;	// Less serious Protocol errors.
					// Useful for debugging but unnecessarily alarming other times.
					// Was it intentially left on for release 1.6?

static int s_debug_client_app = 0;	// Interaction with client application.
					// dl_connect_request, dl_data_request, dl_data_indication, etc.

static int s_debug_radio = 0;		// Received frames and channel busy status.
					// lm_data_indication, lm_channel_busy

static int s_debug_variables = 0;	// Variables, state changes.

static int s_debug_retry = 0;		// Related to lost I frames, REJ, SREJ, timeout, resending.

static int s_debug_timers = 0;		// Timer details.

static int s_debug_link_handle = 0;	// Create data link state machine or pick existing one,
					// based on my address, peer address, client app index, and radio channel.

static int s_debug_stats = 0;		// Statistics when connection is closed.

static int s_debug_misc = 0;		// Anything left over that might be interesting.


/*
 * AX.25 data link state machine.
 *
 * One instance for each link identified by 
 *	[ client, channel, owncall, peercall ]
 */

enum dlsm_state_e { 
	state_0_disconnected = 0,
	state_1_awaiting_connection = 1,
	state_2_awaiting_release = 2,
	state_3_connected = 3,
	state_4_timer_recovery = 4,
	state_5_awaiting_v22_connection = 5 };
			

typedef struct ax25_dlsm_s {

	int magic1;				// Look out for bad pointer or corruption.
#define MAGIC1 0x11592201

	struct ax25_dlsm_s *next;		// Next in linked list.

	int stream_id;				// Unique number for each stream.
						// Internally we use a pointer but this is more user-friendly.

	int chan;				// Radio channel being used.

	int client;				// We have have multiple client applications, 
						// each with their own links.  We need to know
						// which client should receive the data or
						// notifications about state changes.


	char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
						// Up to 10 addresses, same order as in frame.

	int num_addr;				// Number of addresses.  Should be in range 2 .. 10.

#define OWNCALL AX25_SOURCE
						// addrs[OWNCALL] is owncall for this end of link.
						// Note that we are acting on behalf of
						// a client application so the APRS mycall
						// might not be relevant.

#define PEERCALL AX25_DESTINATION
						// addrs[PEERCALL] is call for other end.


	double start_time;			// Clock time when this was allocated.  Used only for
						// debug output for timestamps relative to start.

	enum dlsm_state_e state;		// Current state.
	
	int modulo;				// 8 or 128.
						// Determines whether we have one or two control
						// octets.  128 allows a much larger window size.

	enum srej_e srej_enable;		// Is other end capable of processing SREJ?  (Am I allowed to send it?)
						// Starts out as 'srej_none' for v2.0 or 'srej_single' for v2.2.
						// Can be changed to 'srej_multi' with XID exchange.
						// Should be used only with modulo 128.  (Is this enforced?)

	int n1_paclen;				// Maximum length of information field, in bytes.
						// Starts out as 256 but can be negotiated higher.
						// (Protocol Spec has this in bits.  It is in bytes here.)
						// "PACLEN" in configuration file.

	int n2_retry;				// Maximum number of retries permitted.
						// Typically 10.
						// "RETRY" parameter in configuration file.


	int k_maxframe;				// Window size. Defaults to 4 (mod 8) or 32 (mod 128).
						// Maximum number of unacknowledged information
						// frames that can be outstanding.
						// "MAXFRAME" or "EMAXFRAME" parameter in configuration file.

	int rc;					// Retry count.  Give up after n2.

	int vs;					// 4.2.4.1. Send State Variable V(S)
						// The send state variable exists within the TNC and is never sent. 
						// It contains the next sequential number to be assigned to the next
						// transmitted I frame. 
						// This variable is updated with the transmission of each I frame.

	int va;					// 4.2.4.5. Acknowledge State Variable V(A)
						// The acknowledge state variable exists within the TNC and is never sent.
						// It contains the sequence number of the last frame acknowledged by
						// its peer [V(A)-1 equals the N(S) of the last acknowledged I frame].

	int vr;					// 4.2.4.3. Receive State Variable V(R)
						// The receive state variable exists within the TNC. 
						// It contains the sequence number of the next expected received I frame
						// This variable is updated upon the reception of an error-free I frame
						// whose send sequence number equals the present received state variable value.

	int layer_3_initiated;			// SABM(E) was sent by request of Layer 3; i.e. DL-CONNECT request primitive.
						// I think this means that it is set only if we initiated the connection.
						// It would not be set if we are in the middle of accepting a connection from the other station.
	
// Next 5 are called exception conditions.

	int peer_receiver_busy;			// Remote station is busy and can't receive I frames. 

	int reject_exception;			// A REJ frame has been sent to the remote station. (boolean)

						// This is used only when receiving an I frame, in states 3 & 4, SREJ not enabled.
						// When an I frame has an unexpected N(S),
						//   - if not already set, set it and send REJ.
						// When an I frame with expected N(S) is received, clear it.
						// This would prevent us from sending additional REJ while
						// waiting for result from first one.
						// What happens if the REJ gets lost?   Is it resent somehow?

	int own_receiver_busy;			// Layer 3 is busy and can't receive I frames.
						// We have no API to convey this information so it should always be 0.

	int acknowledge_pending;		// I frames have been successfully received but not yet
						// acknowledged TO the remote station.
						// Set when receiving the next expected I frame and P=0.
						// This gets cleared by sending any I, RR, RNR, REJ.
						// Cleared when sending SREJ with F=1.

// Timing.

	float srt;				// Smoothed roundtrip time in seconds.
						// This is used to dynamically adjust t1v.
						// Sometimes the flow chart has SAT instead of SRT.
						// I think that is a typographical error.

	float t1v;				// How long to wait for an acknowledgement before resending.
						// Value used when starting timer T1, in seconds.
						// "FRACK" parameter in some implementations.
						// Typically it might be 3 seconds after frame has been
						// sent.  Add more for each digipeater in path.
						// Here it is dynamically adjusted.

// Set initial value for T1V.
// Multiply FRACK by 2*m+1, where m is number of digipeaters.

#define INIT_T1V_SRT	\
	    S->t1v = g_misc_config_p->frack * (2 * (S->num_addr - 2) + 1); \
	    S->srt = S->t1v / 2.0;


	int radio_channel_busy;			// Either due to DCD or PTT.


// Timer T1.

// Timer values all use the usual unix time() value but double precision
// so we can have fractions of seconds.

// T1 is used for retries along with the retry counter, "rc."
// When timer T1 is started, the value is obtained from t1v plus the current time.


// Appropriate functions should be used rather than accessing the values directly.



// This gets a little tricky because we need to pause the timers when the radio 
// channel is busy.  Suppose we sent an I frame and set T1 to 4 seconds so we could
// take corrective action if there is no response in a reasonable amount of time.
// What if some other station has the channel tied up for 10 seconds?   We don't want
// T1 to timeout and start a retry sequence.  The solution is to pause the timers while
// the channel is busy.  We don't want to get a timer expiry event when t1_exp is in
// the past if it is currently paused.  When it is un-paused, the expiration time is adjusted
// for the amount of time it was paused.


	double t1_exp;				// This is the time when T1 will expire or 0 if not running.

	double t1_paused_at;			// Time when it was paused or 0 if not paused.

	float t1_remaining_when_last_stopped;	// Number of seconds that were left on T1 when it was stopped.
						// This is used to fine tune t1v.
						// Set to negative initially to mean invalid, don't use in calculation.

	int t1_had_expired;			// Set when T1 expires.
						// Cleared for start & stop.


// Timer T3.

// T3 is used to terminate connection after extended inactivity.


// Similar to T1 except there is not mechanism to capture the remaining time when it is stopped
// and it is not paused when the channel is busy.


	double t3_exp;				// When it expires or 0 if not running.		

#define T3_DEFAULT 300.0			// Copied 5 minutes from Ax.25 for Linux.
						// http://www.linux-ax25.org/wiki/Run_time_configurable_parameters
						// D710A also defaults to 30*10 = 300 seconds.
						// Should it be user-configurable?
						// KPC-3+ and TM-D710A have "CHECK" command for this purpose.

// Statistics for testing purposes.

// Count how many frames of each type we received.
// This is easy to do because they all come in thru lm_data_indication.
// Counting outgoing could probably be done in lm_data_request so
// it would not have to be scattered all over the place.  TBD

	int count_recv_frame_type[frame_not_AX25+1];

	int peak_rc_value;			// Peak value of retry count (rc).


// For sending data.

	cdata_t *i_frame_queue;		// Connected data from client which has not been transmitted yet.
						// Linked list.
						// The name is misleading because these are just blocks of
						// data, not "I frames" at this point.  The name comes from
						// the protocol specification.

	cdata_t *txdata_by_ns[128];		// Data which has already been transmitted.
						// Indexed by N(S) in case it gets lost and needs to be sent again.
						// Cleared out when we get ACK for it.

	int magic3;				// Look out for out of bounds for above.
#define MAGIC3 0x03331301

	cdata_t *rxdata_by_ns[128];		// "Receive buffer"
						// Data which has been received out of sequence.
						// Indexed by N(S).

	int magic2;				// Look out for out of bounds for above.
#define MAGIC2 0x02221201



// "Management Data Link"  (MDL) state machine for XID exchange.


	enum mdl_state_e { mdl_state_0_ready=0, mdl_state_1_negotiating=1 } mdl_state;

	int mdl_rc;				// Retry count, waiting to get XID response.
						// The spec has provision for a separate maximum, NM201, but we
						// just use the regular N2 same as other retries.

	double tm201_exp;			// Timer.  Similar to T1.
						// The spec mentions a separate timeout value but
						// we will just use the same as T1.

	double tm201_paused_at;			// Time when it was paused or 0 if not paused.

// Segment reassembler.

	cdata_t *ra_buff;			// Reassembler buffer.  NULL when in ready state.

	int ra_following;			// Most recent number following to predict next expected.


} ax25_dlsm_t;


/*
 * List of current state machines for each link.
 * There is potential many client apps, each with multiple links
 * connected all at the same time.
 * 
 * Everything coming thru here should be from a single thread.
 * The Data Link Queue should serialize all processing.
 * Therefore, we don't have to worry about critical regions.
 */

static ax25_dlsm_t *list_head = NULL;


/*
 * Registered callsigns for incoming connections.
 */

#define RC_MAGIC 0x08291951

typedef struct reg_callsign_s {
	char callsign[AX25_MAX_ADDR_LEN];
	int chan;
	int client;
	struct reg_callsign_s *next;
	int magic;
} reg_callsign_t;

static reg_callsign_t *reg_callsign_list = NULL;


// Use these, rather than setting variables directly, to make debug out easier.

#define SET_VS(n) {	S->vs = (n);								\
		    	if (s_debug_variables) {						\
			  text_color_set(DW_COLOR_DEBUG);					\
		          dw_printf ("V(S) = %d at %s %d\n", S->vs, __func__, __LINE__);	\
		        }									\
			assert (S->vs >= 0 && S->vs < S->modulo);				\
		  }

// If other guy acks reception of an I frame, we should never get an REJ or SREJ
// asking for it again.  When we update V(A), we should be able to remove the saved
// transmitted data, and everything preceding it, from S->txdata_by_ns[].

#define SET_VA(n) {	S->va = (n);								\
		    	if (s_debug_variables) {						\
			  text_color_set(DW_COLOR_DEBUG);					\
		          dw_printf ("V(A) = %d at %s %d\n", S->va, __func__, __LINE__);	\
		        }									\
			assert (S->va >= 0 && S->va < S->modulo);				\
	                int x = AX25MODULO(n-1, S->modulo, __FILE__, __func__, __LINE__);	\
	                while (S->txdata_by_ns[x] != NULL) {					\
	                  cdata_delete (S->txdata_by_ns[x]);					\
	                  S->txdata_by_ns[x] = NULL;						\
	                  x = AX25MODULO(x-1, S->modulo, __FILE__, __func__, __LINE__);		\
	                }									\
		  }

#define SET_VR(n) {	S->vr = (n);								\
		    	if (s_debug_variables) {						\
			  text_color_set(DW_COLOR_DEBUG);					\
		          dw_printf ("V(R) = %d at %s %d\n", S->vr, __func__, __LINE__);	\
		        }									\
			assert (S->vr >= 0 && S->vr < S->modulo);				\
		  }

#define SET_RC(n) {	S->rc = (n);								\
	                if (s_debug_variables) {						\
			  text_color_set(DW_COLOR_DEBUG);					\
		          dw_printf ("rc = %d at %s %d, state = %d\n", S->rc, __func__, __LINE__, S->state);		\
		        }									\
		  }


//TODO:  Make this a macro so we can simplify calls yet keep debug output if something goes wrong.

#if 0
#define AX25MODULO(n) ax25modulo((n), S->modulo, __FILE__, __func__, __LINE__)
static int ax25modulo(int n, int m, const char *file, const char *func, int line)
#else
static int AX25MODULO(int n, int m, const char *file, const char *func, int line)
#endif
{
	if (m != 8 && m != 128) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR: %d modulo %d, %s, %s, %d\n", n, m, file, func, line);
	  m = 8;
	}
	// Use masking, rather than % operator, so negative numbers are handled properly.
	return (n & (m-1));
}


// Test whether we can send more or if we need to wait
// because we have reached 'maxframe' outstanding frames.
// Argument must be 'S'.

#define WITHIN_WINDOW_SIZE(x) (x->vs != AX25MODULO(x->va + x->k_maxframe, x->modulo, __FILE__, __func__, __LINE__))


// Timer macros to provide debug output with location from where they are called.

#define START_T1	start_t1(S, __func__, __LINE__)
#define IS_T1_RUNNING	is_t1_running(S, __func__, __LINE__)
#define STOP_T1		stop_t1(S, __func__, __LINE__)
#define PAUSE_T1	pause_t1(S, __func__, __LINE__)
#define RESUME_T1	resume_t1(S, __func__, __LINE__)

#define START_T3	start_t3(S, __func__, __LINE__)
#define STOP_T3		stop_t3(S, __func__, __LINE__)

#define START_TM201	start_tm201(S, __func__, __LINE__)
#define STOP_TM201	stop_tm201(S, __func__, __LINE__)
#define PAUSE_TM201	pause_tm201(S, __func__, __LINE__)
#define RESUME_TM201	resume_tm201(S, __func__, __LINE__)

// TODO: add SELECT_T1_VALUE	for debugging.


static void dl_data_indication (ax25_dlsm_t *S, int pid, char *data, int len);

static void i_frame (ax25_dlsm_t *S, cmdres_t cr, int p, int nr, int ns, int pid, char *info_ptr, int info_len);
static void i_frame_continued (ax25_dlsm_t *S, int p, int ns, int pid, char *info_ptr, int info_len);
static int is_ns_in_window (ax25_dlsm_t *S, int ns);
static void send_srej_frames (ax25_dlsm_t *S, int *resend, int count, int allow_f1);
static void rr_rnr_frame (ax25_dlsm_t *S, int ready, cmdres_t cr, int pf, int nr);
static void rej_frame (ax25_dlsm_t *S, cmdres_t cr, int pf, int nr);
static void srej_frame (ax25_dlsm_t *S, cmdres_t cr, int pf, int nr, unsigned char *info_ptr, int info_len);

static void sabm_e_frame (ax25_dlsm_t *S, int extended, int p);
static void disc_frame (ax25_dlsm_t *S, int f);
static void dm_frame (ax25_dlsm_t *S, int f);
static void ua_frame (ax25_dlsm_t *S, int f);
static void frmr_frame (ax25_dlsm_t *S);
static void ui_frame (ax25_dlsm_t *S, cmdres_t cr, int pf);
static void xid_frame (ax25_dlsm_t *S, cmdres_t cr, int pf, unsigned char *info_ptr, int info_len);
static void test_frame (ax25_dlsm_t *S, cmdres_t cr, int pf, unsigned char *info_ptr, int info_len);

static void t1_expiry (ax25_dlsm_t *S);
static void t3_expiry (ax25_dlsm_t *S);
static void tm201_expiry (ax25_dlsm_t *S);

static void nr_error_recovery (ax25_dlsm_t *S);
static void clear_exception_conditions (ax25_dlsm_t *S);
static void transmit_enquiry (ax25_dlsm_t *S);
static void select_t1_value (ax25_dlsm_t *S);
static void establish_data_link (ax25_dlsm_t *S);
static void set_version_2_0 (ax25_dlsm_t *S);
static void set_version_2_2 (ax25_dlsm_t *S);
static int  is_good_nr (ax25_dlsm_t *S, int nr);
static void i_frame_pop_off_queue (ax25_dlsm_t *S);
static void discard_i_queue (ax25_dlsm_t *S);
static void invoke_retransmission (ax25_dlsm_t *S, int nr_input);
static void check_i_frame_ackd (ax25_dlsm_t *S, int nr);
static void check_need_for_response (ax25_dlsm_t *S, ax25_frame_type_t frame_type, cmdres_t cr, int pf);
static void enquiry_response (ax25_dlsm_t *S, ax25_frame_type_t frame_type, int f);

static void enter_new_state(ax25_dlsm_t *S, enum dlsm_state_e new_state, const char *from_func, int from_line);

static void mdl_negotiate_request (ax25_dlsm_t *S);
static void initiate_negotiation (ax25_dlsm_t *S, struct xid_param_s *param);
static void negotiation_response (ax25_dlsm_t *S, struct xid_param_s *param);
static void complete_negotiation (ax25_dlsm_t *S, struct xid_param_s *param);


// Use macros above rather than calling these directly.

static void start_t1 (ax25_dlsm_t *S, const char *from_func, int from_line);
static void stop_t1 (ax25_dlsm_t *S, const char *from_func, int from_line);
static int is_t1_running (ax25_dlsm_t *S, const char *from_func, int from_line);
static void pause_t1 (ax25_dlsm_t *S, const char *from_func, int from_line);
static void resume_t1 (ax25_dlsm_t *S, const char *from_func, int from_line);

static void start_t3 (ax25_dlsm_t *S, const char *from_func, int from_line);
static void stop_t3 (ax25_dlsm_t *S, const char *from_func, int from_line);

static void start_tm201 (ax25_dlsm_t *S, const char *from_func, int from_line);
static void stop_tm201 (ax25_dlsm_t *S, const char *from_func, int from_line);
static void pause_tm201 (ax25_dlsm_t *S, const char *from_func, int from_line);
static void resume_tm201 (ax25_dlsm_t *S, const char *from_func, int from_line);



/*
 * Configuration settings from file or command line.
 */

static struct misc_config_s  *g_misc_config_p;


/*-------------------------------------------------------------------
 *
 * Name:        ax25_link_init
 *
 * Purpose:     Initialize the ax25_link module.
 *
 * Inputs:	pconfig		- misc. configuration from config file or command line.
 *				  Beacon stuff ended up here.
 *
 * Outputs:	Remember required information for future use.  That's all.
 *
 *--------------------------------------------------------------------*/

void ax25_link_init (struct misc_config_s *pconfig)
{

/* 
 * Save parameters for later use.
 */
	g_misc_config_p = pconfig;

} /* end ax25_link_init */




/*------------------------------------------------------------------------------
 *
 * Name:	get_link_handle
 * 
 * Purpose:	Find existing (or possibly create) state machine for a given link.
 *		It should be possible to have a large number of links active at the
 *		same time.  They are uniquely identified by 
 *		(owncall, peercall, client id, radio channel)
 *		Note that we could have multiple client applications, all sharing one
 *		TNC, on the same or different radio channels, completely unware of each other.
 *
 * Inputs:	addrs		- Owncall, peercall, and optional digipeaters.
 *				  For ease of passing this around, it is an array in the
 *				  same order as in the frame.
 *
 *		num_addr	- Number of addresses, 2 thru 10.
 *
 *		chan		- Radio channel number.
 *
 *		client		- Client app number.
 *				  We allow multiple concurrent applications with the 
 *				  AGW network protocol.  These are identified as 0, 1, ...
 *				  We don't know this for an incoming frame from the radio
 *				  so it is -1 at this point. At a later time will will
 *				  associate the stream with the right client.
 *
 *		create		- True if OK to create a new one.
 *				  Otherwise, return only one already existing.
 *
 *				  This should always be true for outgoing frames.
 *				  For incoming frames this would be true only for SABM(e)
 *				  with all digipeater fields marked as used.
 *
 *				  Here, we will also check to see if it is in our 
 *				  registered callsign list.
 *
 * Returns:	Handle for data link state machine.  
 *		NULL if not found and 'create' is false.
 *
 * Description:	Try to find an existing entry matching owncall, peercall, channel,
 *		and client.  If not found create a new one.
 *
 *------------------------------------------------------------------------------*/

static int next_stream_id = 0;

static ax25_dlsm_t *get_link_handle (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, int chan, int client, int create)
{

	ax25_dlsm_t *p;


	if (s_debug_link_handle) {
	  text_color_set(DW_COLOR_DECODED);
	  dw_printf ("get_link_handle (%s>%s, chan=%d, client=%d, create=%d)\n",
				addrs[AX25_SOURCE], addrs[AX25_DESTINATION], chan, client, create);
	}


// Look for existing.

	if (client == -1) {				// from the radio.
							// address order is reversed for compare.
	  for (p = list_head; p != NULL; p = p->next) {

	    if (p->chan == chan &&
	        strcmp(addrs[AX25_DESTINATION], p->addrs[OWNCALL]) == 0 &&
	        strcmp(addrs[AX25_SOURCE], p->addrs[PEERCALL]) == 0) {

	      if (s_debug_link_handle) {
	        text_color_set(DW_COLOR_DECODED);
	        dw_printf ("get_link_handle returns existing stream id %d for incoming.\n", p->stream_id);
	      }
	      return (p);
	    }
	  }
	}
	else {						// from client app
	  for (p = list_head; p != NULL; p = p->next) {

	    if (p->chan == chan &&
	        p->client == client &&
	        strcmp(addrs[AX25_SOURCE], p->addrs[OWNCALL]) == 0 &&
	        strcmp(addrs[AX25_DESTINATION], p->addrs[PEERCALL]) == 0) {

	      if (s_debug_link_handle) {
	        text_color_set(DW_COLOR_DECODED);
	        dw_printf ("get_link_handle returns existing stream id %d for outgoing.\n", p->stream_id);
	      }
	      return (p);
	    }
	  }
	}


// Could not find existing.  Should we create a new one?

	if ( ! create) {
	  if (s_debug_link_handle) {
	    text_color_set(DW_COLOR_DECODED);
	    dw_printf ("get_link_handle: Search failed. Do not create new.\n");
	  }
	  return (NULL);
	}


// If it came from the radio, search for destination our registered callsign list.

	int incoming_for_client = -1;		// which client app registered the callsign?


	if (client == -1) {				// from the radio.

	  reg_callsign_t *r, *found;

	  found = NULL;
	  for (r = reg_callsign_list; r != NULL && found == NULL; r = r->next) {

	    if (strcmp(addrs[AX25_DESTINATION], r->callsign) == 0 && chan == r->chan) {
	      found = r;
	      incoming_for_client = r->client;
	    }
	  }

	  if (found == NULL) {
	    if (s_debug_link_handle) {
	      text_color_set(DW_COLOR_DECODED);
	      dw_printf ("get_link_handle: not for me.  Ignore it.\n");
	    }
	    return (NULL);
	  }
	}

// Create new data link state machine.

	p = calloc (sizeof(ax25_dlsm_t), 1);
	if (p == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	p->magic1 = MAGIC1;
	p->start_time = dtime_now();
	p->stream_id = next_stream_id++;
	p->modulo = 8;

	p->chan = chan;
	p->num_addr = num_addr;

// If it came in over the radio, we need to swap source/destination and reverse any digi path.

	if (incoming_for_client >= 0) {
	  strlcpy (p->addrs[AX25_SOURCE],      addrs[AX25_DESTINATION], sizeof(p->addrs[AX25_SOURCE]));
	  strlcpy (p->addrs[AX25_DESTINATION], addrs[AX25_SOURCE],      sizeof(p->addrs[AX25_DESTINATION]));

	  int j = AX25_REPEATER_1;
	  int k = num_addr - 1;
	  while (k >= AX25_REPEATER_1) {
	    strlcpy (p->addrs[j],      addrs[k], sizeof(p->addrs[j]));
	    j++;
	    k--;
	  }

	  p->client = incoming_for_client;
	}
	else {
	  memcpy (p->addrs, addrs, sizeof(p->addrs));
	  p->client = client;
	}
	
	p->state = state_0_disconnected;
	p->t1_remaining_when_last_stopped = -999;		// Invalid, don't use.

	p->magic2 = MAGIC2;
	p->magic3 = MAGIC3;

	// No need for critical region because this should all be in one thread.
	p->next = list_head;
	list_head = p;

	if (s_debug_link_handle) {
	  text_color_set(DW_COLOR_DECODED);
	  dw_printf ("get_link_handle returns NEW stream id %d\n", p->stream_id);
	}

	return (p);
}


	



//###################################################################################
//###################################################################################
//
//  Data Link state machine for sending data in connected mode.
//  
//	Incoming:
//  
//		Requests from the client application.  Set s_debug_client_app for debugging.
//
//			dl_connect_request
//			dl_disconnect_request
//			dl_outstanding_frames_request	- (mine) Ask about outgoing queue for a link.
//			dl_data_request			- send connected data
//			dl_unit_data_request		- not implemented.  APRS & KISS bypass this
//			dl_flow_off			- not implemented.  Not in AGW API.
//			dl_flow_on			- not implemented.  Not in AGW API.
// 			dl_register_callsign		- Register callsigns(s) for incoming connection requests.
// 			dl_unregister_callsign		- Unregister callsigns(s) ...
//			dl_client_cleanup		- Clean up after client which has disappeared.
//
//		Stuff from the radio channel.  Set s_debug_radio for debugging.
//
//			lm_data_indication		- Received frame.
//			lm_channel_busy			- Change in PTT or DCD.
//			lm_seize_confirm		- We have started to transmit.
//
//		Timer expiration.  Set s_debug_timers for debugging.
//
//			dl_timer_expiry
//
//	Outgoing:
//
//		To the client application:
//
//			dl_data_indication		- received connected data.
//
//		To the transmitter:
//
//			lm_data_request			- Queue up a frame for transmission.
//
//			lm_seize_request		- Start transmitter when possible.
//							  lm_seize_confirm will be called when it has.
//
//
//  It is important that all requests come thru the data link queue so
//  everything is serialized.
//  We don't have to worry about being reentrant or critical regions.
//  Nothing here should consume a significant amount of time.
//  i.e. There should be no sleep delay or anything that would block waiting on someone else.
//
//###################################################################################
//###################################################################################



/*------------------------------------------------------------------------------
 *
 * Name:	dl_connect_request
 * 
 * Purpose:	Client app wants to connect to another station.
 *
 * Inputs:	E	- Event from the queue.
 *			  The caller will free it.
 *
 * Description:	
 *	
 *------------------------------------------------------------------------------*/

void dl_connect_request (dlq_item_t *E)
{
	ax25_dlsm_t *S;
	int ok_to_create = 1;
	int old_version;
	int n;

	if (s_debug_client_app) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dl_connect_request ()\n");
	}

	text_color_set(DW_COLOR_INFO);
	dw_printf ("Attempting connect to %s ...\n", E->addrs[PEERCALL]);

	S = get_link_handle (E->addrs, E->num_addr, E->chan, E->client, ok_to_create);

	switch (S->state) {

	  case 	state_0_disconnected:

	    INIT_T1V_SRT;

// See if destination station is in list for v2.0 only.

	    old_version = 0;
	    for (n = 0; n < g_misc_config_p->v20_count && ! old_version; n++) {
	      if (strcmp(E->addrs[AX25_DESTINATION],g_misc_config_p->v20_addrs[n]) == 0) {
	        old_version = 1;
	      }
	    }

	    if (old_version || g_misc_config_p->maxv22 == 0) {		// Don't attempt v2.2.

	      set_version_2_0 (S);

	      establish_data_link (S);
	      S->layer_3_initiated = 1;
	      enter_new_state (S, state_1_awaiting_connection, __func__, __LINE__);
	    }
	    else {					// Try v2.2 first, then fall back if appropriate.

	      set_version_2_2 (S);

	      establish_data_link (S);
	      S->layer_3_initiated = 1;
	      enter_new_state (S, state_5_awaiting_v22_connection, __func__, __LINE__);
	    }
	    break;

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:

	    discard_i_queue(S);
	    S->layer_3_initiated = 1;
	    // Keep current state.
	    break;
	    
	  case 	state_2_awaiting_release:

	    // Keep current state.
	    break;

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:

	    discard_i_queue(S);
	    establish_data_link(S);
	    S->layer_3_initiated = 1;
	    // My enhancement.  Original always sent SABM and went to state 1.
	    // If we were using v2.2, why not reestablish with that?
	    enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	    break;
	}

} /* end dl_connect_request */


/*------------------------------------------------------------------------------
 *
 * Name:	dl_disconnect_request
 * 
 * Purpose:	Client app wants to terminate connection with another station.
 *
 * Inputs:	E	- Event from the queue.
 *			  The caller will free it.
 *
 * Outputs:
 *
 * Description:	
 *	
 *------------------------------------------------------------------------------*/

void dl_disconnect_request (dlq_item_t *E)
{
	ax25_dlsm_t *S;
	int ok_to_create = 1;


	if (s_debug_client_app) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dl_disconnect_request ()\n");
	}

	text_color_set(DW_COLOR_INFO);
	dw_printf ("Disconnect from %s ...\n", E->addrs[PEERCALL]);

	S = get_link_handle (E->addrs, E->num_addr, E->chan, E->client, ok_to_create);

	switch (S->state) {

	  case 	state_0_disconnected:

	    // DL-DISCONNECT *confirm*
	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
	    server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);
	    break;

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:

// Erratum: The protocol spec says "requeue."  If we put disconnect req back in the
// queue we will probably get it back again here while still in same state.
// I don't think we would want to delay it until the next state transition.

// Suppose someone tried to connect to another station, which is not responding, and decided to cancel
// before all of the SABMe retries were used up.  I think we would want to transmit a DISC, send a disc
// notice to the user, and go directly into disconnected state, rather than into awaiting release.

// New code v1.7 dev, May 6 2023

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Stream %d: In progress connection attempt to %s terminated by user.\n", S->stream_id, S->addrs[PEERCALL]);
	    discard_i_queue (S);
	    SET_RC(0);
	    int p1 = 1;
	    int nopid0 = 0;
	    packet_t pp15 = ax25_u_frame (S->addrs, S->num_addr, cr_cmd, frame_type_U_DISC, p1, nopid0, NULL, 0);
	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp15);

	    STOP_T1;	// started in establish_data_link.
	    STOP_T3;	// probably don't need.
	    enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);
	    break;

	  case 	state_2_awaiting_release:
	    {
	      // We have previously started the disconnect sequence and are waiting
	      // for a UA from the other guy.  Meanwhile, the application got
	      // impatient and sent us another disconnect request.  What should
	      // we do?  Ignore it and let the disconnect sequence run its
	      // course?  Or should we complete the sequence without waiting
	      // for the other guy to ack?

	      // Erratum.  Flow chart simply says "DM (expedited)."
	      // This is the only place we have expedited.  Is this correct?

	      cmdres_t cr = cr_res;	// DM can only be response.
	      int p = 0;
	      int nopid = 0;		// PID applies only to I and UI frames.

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, cr, frame_type_U_DM, p, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_0_HI, pp);	// HI means expedited.

	      // Erratum: Shouldn't we inform the user when going to disconnected state?
	      // Notifying the application, here, is my own enhancement.

	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
	      server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);

	      STOP_T1;
	      enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    }
	    break;

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:

	    discard_i_queue (S);
	    SET_RC(0);			// I think this should be 1 but I'm not that worried about it.

	    cmdres_t cmd = cr_cmd;
	    int p = 1;
	    int nopid = 0;

	    packet_t pp = ax25_u_frame (S->addrs, S->num_addr, cmd, frame_type_U_DISC, p, nopid, NULL, 0);
	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	    STOP_T3;
	    START_T1;
	    enter_new_state (S, state_2_awaiting_release, __func__, __LINE__);

	    break;
	}

} /* end dl_disconnect_request */


/*------------------------------------------------------------------------------
 *
 * Name:	dl_data_request
 * 
 * Purpose:	Client app wants to send data to another station.
 *
 * Inputs:	E	- Event from the queue.
 *			  The caller will free it.
 *
 * Description:	Append the transmit data block to the I frame queue for later processing.
 *
 *		We also perform the segmentation handling here.
 *
 *		C6.1 Segmenter State Machine
 *		Only the following DL primitives will be candidates for modification by the segmented
 *		state machine:

 *		* DL-DATA Request. The user employs this primitive to provide information to be
 *		transmitted using connection-oriented procedures; i.e., using I frames. The
 *		segmenter state machine examines the quantity of data to be transmitted. If the
 *		quantity of data to be transmitted is less than or equal to the data link parameter
 *		N1, the segmenter state machine passes the primitive through transparently. If the
 *		quantity of data to be transmitted exceeds the data link parameter N1, the
 *		segmenter chops up the data into segments of length N1-2 octets. Each segment is
 *		prepended with a two octet header. (See Figures 3.1 and 3.2.) The segments are
 *		then turned over to the Data-link State Machine for transmission, using multiple DL
 *		Data Request primitives. All segments are turned over immediately; therefore the
 *		Data-link State Machine will transmit them consecutively on the data link.
 *
 * Erratum:	Not sure how to interpret that.  See example below for how it was implemented.
 *
 * Version 1.6:	Bug 252.  Segmentation was occurring for a V2.0 link.  From the spec:
 *			"The receipt of an XID response from the other station establishes that both
 *			stations are using AX.25 version 2.2 or higher and enables the use of the
 *			segmenter/reassembler and selective reject."
 *			"The segmenter/reassembler procedure is only enabled if both stations on the
 *			link are using AX.25 version 2.2 or higher."
 *
 *		The Segmenter Ready State SDL has no decision based on protocol version.
 *
 *------------------------------------------------------------------------------*/

static void data_request_good_size (ax25_dlsm_t *S, cdata_t *txdata);


void dl_data_request (dlq_item_t *E)
{
	ax25_dlsm_t *S;
	int ok_to_create = 1;


	S = get_link_handle (E->addrs, E->num_addr, E->chan, E->client, ok_to_create);

	if (s_debug_client_app) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dl_data_request (\"");
	  ax25_safe_print (E->txdata->data, E->txdata->len, 1);
	  dw_printf ("\") state=%d\n", S->state);
	}

	if (E->txdata->len <= S->n1_paclen) {
	  data_request_good_size (S, E->txdata);
	  E->txdata = NULL;	// Now part of transmit I frame queue.
	  return;
	}

#define DIVROUNDUP(a,b) (((a)+(b)-1) / (b))

// Erratum: Don't do V2.2 segmentation for a V2.0 link.
// In this case, we can just split it into multiple frames not exceeding the specified max size.
// Hopefully the receiving end treats it like a stream and doesn't care about length of each frame.

	if (S->modulo == 8) {

	  int num_frames = 0;
	  int remaining_len = E->txdata->len;
	  int offset = 0;

	  while (remaining_len > 0) {
	    int this_len = MIN(remaining_len, S->n1_paclen);

	    cdata_t *new_txdata = cdata_new(E->txdata->pid, E->txdata->data + offset, this_len);
	    data_request_good_size (S, new_txdata);

	    offset += this_len;
	    remaining_len -= this_len;
	    num_frames++;
	  }

	  if (num_frames != DIVROUNDUP(E->txdata->len, S->n1_paclen) || remaining_len != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("INTERNAL ERROR, Segmentation line %d, data length = %d, N1 = %d, num frames = %d, remaining len = %d\n",
					__LINE__, E->txdata->len, S->n1_paclen, num_frames, remaining_len);
	  }
	  cdata_delete (E->txdata);
	  E->txdata = NULL;
	  return;
	}

// More interesting case.
// It is too large to fit in one frame so we segment it.

// As an example, suppose we had 6 bytes of data "ABCDEF".

// If N1 >= 6, it would be sent normally.

//	(addresses)
//	(control bytes)
//	PID, typically 0xF0
//	'A'	- first byte of information field
//	'B'
//	'C'
//	'D'
//	'E'
//	'F'

// Now consider the case where it would not fit.
// We would change the PID to 0x08 meaning a segment.
// The information part is the segment identifier of this format:
//
//	x xxxxxxx
//	| ---+---
//	|    |
//	|    +- Number of additional segments to follow.
//	|
//	+- '1' means it is the first segment.

// If N1 = 4, it would be split up like this:

//	(addresses)
//	(control bytes)
//	PID = 0x08	means segment
//	0x82	- Start of info field.
//		  MSB set indicates FIRST segment.
//		  2, in lower 7 bits, means 2 more segments to follow.
//	0xF0	- original PID, typical value.
//	'A'	- For the FIRST segment, we have PID and N1-2 data bytes.
//	'B'

//	(addresses)
//	(control bytes)
//	PID = 0x08	means segment
//	0x01	- Means 1 more segment follows.
//	'C'	- For subsequent (not first) segments, we have up to N1-1 data bytes.
//	'D'
//	'E'

//	(addresses)
//	(control bytes)
//	PID = 0x08
//	0x00 - 0 means no more to follow.  i.e.  This is the last.
//	'E'


// Number of segments is ceiling( (datalen + 1 ) / (N1 - 1))

// we add one to datalen for the original PID.
// We subtract one from N1 for the segment identifier header.

#define DIVROUNDUP(a,b) (((a)+(b)-1) / (b))

// Compute number of segments.
// We will decrement this before putting it in the frame so the first
// will have one less than this number.

	int nseg_to_follow = DIVROUNDUP(E->txdata->len + 1, S->n1_paclen - 1);

	if (nseg_to_follow < 2 || nseg_to_follow > 128) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR, Segmentation line %d, data length = %d, N1 = %d, number of segments = %d\n",
					__LINE__, E->txdata->len, S->n1_paclen, nseg_to_follow);
	  cdata_delete (E->txdata);
	  E->txdata = NULL;
	  return;
	}

	int orig_offset = 0;
	int remaining_len = E->txdata->len;

// First segment.

	int seglen;
	struct {
	  char header;		// 0x80 + number of segments to follow.
	  char original_pid;
	  char segdata[AX25_N1_PACLEN_MAX];
	} first_segment;
	cdata_t *new_txdata;

	nseg_to_follow--;

	first_segment.header = 0x80 | nseg_to_follow;
	first_segment.original_pid = E->txdata->pid;
	seglen = MIN(S->n1_paclen - 2, remaining_len);

	if (seglen < 1 || seglen > S->n1_paclen - 2 || seglen > remaining_len || seglen > (int)(sizeof(first_segment.segdata))) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR, Segmentation line %d, data length = %d, N1 = %d, segment length = %d, number to follow = %d\n",
					__LINE__, E->txdata->len, S->n1_paclen, seglen, nseg_to_follow);
	  cdata_delete (E->txdata);
	  E->txdata = NULL;
	  return;
	}

	memcpy (first_segment.segdata, E->txdata->data + orig_offset, seglen);

	new_txdata = cdata_new(AX25_PID_SEGMENTATION_FRAGMENT, (char*)(&first_segment), seglen+2);

	data_request_good_size (S, new_txdata);

	orig_offset += seglen;
	remaining_len -= seglen;


// Subsequent segments.

	do {
	  struct {
	    char header;		// Number of segments to follow.
	    char segdata[AX25_N1_PACLEN_MAX];
	  } subsequent_segment;

	  nseg_to_follow--;

	  subsequent_segment.header = nseg_to_follow;
	  seglen = MIN(S->n1_paclen - 1, remaining_len);

	  if (seglen < 1 || seglen > S->n1_paclen - 1 || seglen > remaining_len || seglen > (int)(sizeof(subsequent_segment.segdata))) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("INTERNAL ERROR, Segmentation line %d, data length = %d, N1 = %d, segment length = %d, number to follow = %d\n",
					__LINE__, E->txdata->len, S->n1_paclen, seglen, nseg_to_follow);
	    cdata_delete (E->txdata);
	    E->txdata = NULL;
	    return;
	  }

	  memcpy (subsequent_segment.segdata, E->txdata->data + orig_offset, seglen);

	  new_txdata = cdata_new(AX25_PID_SEGMENTATION_FRAGMENT, (char*)(&subsequent_segment), seglen+1);

	  data_request_good_size (S, new_txdata);

	  orig_offset += seglen;
	  remaining_len -= seglen;

	} while (nseg_to_follow > 0);
	
	if (remaining_len != 0 || orig_offset != E->txdata->len) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR, Segmentation line %d, data length = %d, N1 = %d, remaining length = %d (not 0), orig offset = %d (not %d)\n",
					__LINE__, E->txdata->len, S->n1_paclen, remaining_len, orig_offset, E->txdata->len);
	}

	cdata_delete (E->txdata);
	E->txdata = NULL;

} /* end dl_data_request */


static void data_request_good_size (ax25_dlsm_t *S, cdata_t *txdata)
{
	switch (S->state) {

	  case 	state_0_disconnected:
	  case 	state_2_awaiting_release:
/*
 * Discard it.
 */
	    cdata_delete (txdata);
	    break;

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:
/*
 * Erratum?
 * The flow chart shows "push on I frame queue" if layer 3 initiated
 * is NOT set.  This seems backwards but I don't understand enough yet
 * to make a compelling argument that it is wrong.
 * Implemented as in flow chart.
 * TODO: Get better understanding of what'layer_3_initiated' means.
 */
	    if (S->layer_3_initiated) {
	      cdata_delete (txdata);
	      break;
	    }
	    // otherwise fall thru.

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:
/*
 * "push on I frame queue"
 * Append to the end would have been a better description because push implies a stack.
 */
 
	    if (S->i_frame_queue == NULL) {
	      txdata->next = NULL;
	      S->i_frame_queue = txdata;
	    }
	    else {
	      cdata_t *plast = S->i_frame_queue;
	      while (plast->next != NULL) {
	        plast = plast->next;
	      }
	      txdata->next = NULL;
	      plast->next = txdata;
	    }
	    break;
	}

	// v1.5 change in strategy.
	// New I frames, not sent yet, are delayed until after processing anything in the received transmission.
	// Give the transmit process a kick unless other side is busy or we have reached our window size.
	// Previously we had i_frame_pop_off_queue here which would start sending new stuff before we
	// finished dealing with stuff already in progress.

	switch (S->state) {

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:

	    if ( ( ! S->peer_receiver_busy ) &&
	            WITHIN_WINDOW_SIZE(S) ) {
	      S->acknowledge_pending = 1;
	      lm_seize_request (S->chan);
	    }
	    break;

	  default:
	    break;
	}

} /* end data_request_good_size */


/*------------------------------------------------------------------------------
 *
 * Name:	dl_register_callsign
 *		dl_unregister_callsign
 * 
 * Purpose:	Register / Unregister callsigns that we will accept connections for.
 *
 * Inputs:	E	- Event from the queue.
 *			  The caller will free it.
 *
 * Outputs:	New item is pushed on the head of the reg_callsign_list.
 *		We don't bother checking for duplicates so the most recent wins.
 *
 * Description:	The data link state machine does not use MYCALL from the APRS configuration.
 *		For outgoing frames, the client supplies the source callsign.
 *		For incoming connection requests, we need to know what address(es) to respond to.
 *
 *		Note that one client application can register multiple callsigns for 
 *		multiple channels.
 *		Different clients can register different different addresses on the same channel.
 *	
 *------------------------------------------------------------------------------*/

void dl_register_callsign (dlq_item_t *E)
{
	reg_callsign_t *r;

	if (s_debug_client_app) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dl_register_callsign (%s, chan=%d, client=%d)\n", E->addrs[0], E->chan, E->client);
	}

	r = calloc(sizeof(reg_callsign_t),1);
	if (r == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	strlcpy (r->callsign, E->addrs[0], sizeof(r->callsign));
	r->chan = E->chan;
	r->client = E->client;
	r->next = reg_callsign_list;
	r->magic = RC_MAGIC;

	reg_callsign_list = r;

} /* end dl_register_callsign */


void dl_unregister_callsign (dlq_item_t *E)
{
	reg_callsign_t *r, *prev;

	if (s_debug_client_app) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dl_unregister_callsign (%s, chan=%d, client=%d)\n", E->addrs[0], E->chan, E->client);
	}

	prev = NULL;
	r = reg_callsign_list;
	while (r != NULL) {

	  assert (r->magic == RC_MAGIC);

	  if (strcmp(r->callsign,E->addrs[0]) == 0 && r->chan == E->chan && r->client == E->client) {

	    if (r == reg_callsign_list) {

	      reg_callsign_list = r->next;
	      memset (r, 0, sizeof(reg_callsign_t));
	      free (r);
	      r = reg_callsign_list;
	    }
	    else {

	      prev->next = r->next;
	      memset (r, 0, sizeof(reg_callsign_t));
	      free (r);
	      r = prev->next;
	    }
	  }
	  else {
	    prev = r;
	    r = r->next;
	  }
	}

} /* end dl_unregister_callsign */



/*------------------------------------------------------------------------------
 *
 * Name:	dl_outstanding_frames_request
 *
 * Purpose:	Client app wants to know how many frames are still on their way
 *		to other station.  This is handy for flow control.  We would like
 *		to keep the pipeline filled sufficiently to take advantage of a
 *		large window size (MAXFRAMES).  It is also good to know that the
 *		the last packet sent was actually received before we commence
 *		the disconnect.
 *
 * Inputs:	E	- Event from the queue.
 *			  The caller will free it.
 *
 * Outputs:	This gets back to the AGW server which sends the 'Y' reply.
 *
 * Description:	This is the sum of:
 *		- Incoming connected data, from application still in the queue.
 *		- I frames which have been transmitted but not yet acknowledged.
 *
 * Confusion:	https://github.com/wb2osz/direwolf/issues/427
 *
 *		There are different, inconsistent versions of the protocol spec.
 *
 *		One of them simply has:
 *
 *			CallFrom is our call
 *			CallTo is the call of the other station
 *
 *		A more detailed version has the same thing in the table of fields:
 *
 *			CallFrom	10 bytes	Our CallSign
 *			CallTo		10 bytes	Other CallSign
 *
 *		(My first implementation went with that.)
 *		
 *		HOWEVER, shortly after that, is contradictory information:
 *
 *			Careful must be exercised to fill correctly both the CallFrom
 *			and CallTo fields to match the ones of an existing connection,
 *			otherwise AGWPE won’t return any information at all from this query.
 *
 *			The order of the CallFrom and CallTo is not trivial, it should
 *			reflect the order used to start the connection, so
 *
 *			  *  If we started the connection CallFrom=US and CallTo=THEM
 *			  *  If the other end started the connection CallFrom=THEM and CallTo=US
 *
 *		This seems to make everything unnecessarily more complicated.
 *		We should only care about the stream going from the local station to the
 *		remote station.  Why would it matter who reqested the link?  The state
 *		machine doesn't even contain this information so the TNC doesn't know.
 *		The client app interface needs to behave differently for the two cases.
 *
 *		The new code, below, May 2023, should handle both of those cases.
 *
 *------------------------------------------------------------------------------*/

void dl_outstanding_frames_request (dlq_item_t *E)
{
	ax25_dlsm_t *S;
	const int ok_to_create = 0;	// must exist already.
	int reversed_addrs = 0;

	if (s_debug_client_app) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dl_outstanding_frames_request ( to %s )\n", E->addrs[PEERCALL]);
	}

	S = get_link_handle (E->addrs, E->num_addr, E->chan, E->client, ok_to_create);
	if (S != NULL) {
	  reversed_addrs = 0;
	}
	else {
	  // Try swapping the addresses.
	  // this is communicating with the client app, not over the air,
	  // so we don't need to worry about digipeaters.

	  char swapped[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
	  memset (swapped, 0, sizeof(swapped));
	  strlcpy (swapped[PEERCALL], E->addrs[OWNCALL], sizeof(swapped[PEERCALL]));
	  strlcpy (swapped[OWNCALL], E->addrs[PEERCALL], sizeof(swapped[OWNCALL]));
	  S = get_link_handle (swapped, E->num_addr, E->chan, E->client, ok_to_create);
	  if (S != NULL) {
	    reversed_addrs = 1;
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Can't get outstanding frames for %s -> %s, chan %d\n", E->addrs[OWNCALL], E->addrs[PEERCALL], E->chan);
	    server_outstanding_frames_reply (E->chan, E->client, E->addrs[OWNCALL], E->addrs[PEERCALL], 0);
	    return;
	  }
	}

// Add up these
//
//	cdata_t *i_frame_queue;			// Connected data from client which has not been transmitted yet.
//						// Linked list.
//						// The name is misleading because these are just blocks of
//						// data, not "I frames" at this point.  The name comes from
//						// the protocol specification.
//
//	cdata_t *txdata_by_ns[128];		// Data which has already been transmitted.
//						// Indexed by N(S) in case it gets lost and needs to be sent again.
//						// Cleared out when we get ACK for it.

	int count1 = 0;
	cdata_t *incoming;
	for (incoming = S->i_frame_queue; incoming != NULL; incoming = incoming->next) {
	  count1++;
	}

	int count2 = 0;
	int k;
	for (k = 0; k < S->modulo; k++) {
	  if (S->txdata_by_ns[k] != NULL) {
	    count2++;
	  }
	}

	if (reversed_addrs) {
	  // Other end initiated the link.
	  server_outstanding_frames_reply (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], count1 + count2);
	}
	else {
	  server_outstanding_frames_reply (S->chan, S->client, S->addrs[OWNCALL], S->addrs[PEERCALL], count1 + count2);
	}

} // end dl_outstanding_frames_request



/*------------------------------------------------------------------------------
 *
 * Name:	dl_client_cleanup
 * 
 * Purpose:	Client app has gone away.  Clean up any data associated with it.
 *
 * Inputs:	E	- Event from the queue.
 *			  The caller will free it.
 *

 * Description:	By client application we mean something that attached with the 
 *		AGW network protocol.
 *
 *		Clean out anything related to the specified client application.
 *		This would include state machines and registered callsigns.
 *	
 *------------------------------------------------------------------------------*/

void dl_client_cleanup (dlq_item_t *E)
{
	ax25_dlsm_t *S;
	ax25_dlsm_t *dlprev;
	reg_callsign_t *r, *rcprev;


	if (s_debug_client_app) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("dl_client_cleanup (%d)\n", E->client);
	}


	dlprev = NULL;
	S = list_head;
	while (S != NULL) {

	  // Look for corruption or double freeing.

	  assert (S->magic1 == MAGIC1);
	  assert (S->magic2 == MAGIC2);
	  assert (S->magic3 == MAGIC3);

	  if (S->client == E->client ) {

	    int n;

	    if (s_debug_stats) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("%d  I frames received\n",    S->count_recv_frame_type[frame_type_I]);

	      dw_printf ("%d  RR frames received\n",   S->count_recv_frame_type[frame_type_S_RR]);
	      dw_printf ("%d  RNR frames received\n",  S->count_recv_frame_type[frame_type_S_RNR]);
	      dw_printf ("%d  REJ frames received\n",  S->count_recv_frame_type[frame_type_S_REJ]);
	      dw_printf ("%d  SREJ frames received\n", S->count_recv_frame_type[frame_type_S_SREJ]);

	      dw_printf ("%d  SABME frames received\n", S->count_recv_frame_type[frame_type_U_SABME]);
	      dw_printf ("%d  SABM frames received\n",  S->count_recv_frame_type[frame_type_U_SABM]);
	      dw_printf ("%d  DISC frames received\n",  S->count_recv_frame_type[frame_type_U_DISC]);
	      dw_printf ("%d  DM frames received\n",    S->count_recv_frame_type[frame_type_U_DM]);
	      dw_printf ("%d  UA frames received\n",    S->count_recv_frame_type[frame_type_U_UA]);
	      dw_printf ("%d  FRMR frames received\n",  S->count_recv_frame_type[frame_type_U_FRMR]);
	      dw_printf ("%d  UI frames received\n",    S->count_recv_frame_type[frame_type_U_UI]);
	      dw_printf ("%d  XID frames received\n",   S->count_recv_frame_type[frame_type_U_XID]);
	      dw_printf ("%d  TEST frames received\n",  S->count_recv_frame_type[frame_type_U_TEST]);

	      dw_printf ("%d  peak retry count\n",      S->peak_rc_value);
	    }

	    if (s_debug_client_app) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("dl_client_cleanup: remove %s>%s\n", S->addrs[AX25_SOURCE], S->addrs[AX25_DESTINATION]);
	    }

	    discard_i_queue (S);

	    for (n = 0; n < 128; n++) {
	      if (S->txdata_by_ns[n] != NULL) {
	        cdata_delete (S->txdata_by_ns[n]);
	        S->txdata_by_ns[n] = NULL;
	      }
	    }

	    for (n = 0; n < 128; n++) {
	      if (S->rxdata_by_ns[n] != NULL) {
	        cdata_delete (S->rxdata_by_ns[n]);
	        S->rxdata_by_ns[n] = NULL;
	      }
	    }

	    if (S->ra_buff != NULL) {
	      cdata_delete (S->ra_buff);
	      S->ra_buff = NULL;
	    }

	    // Put into disconnected state.
	    // If "connected" indicator (e.g. LED) was on, this will turn it off.

	    enter_new_state (S, state_0_disconnected, __func__, __LINE__);

	    // Take S out of list.

	    S->magic1 = 0;
	    S->magic2 = 0;
	    S->magic3 = 0;

	    if (S == list_head) {		// first one on list.

	      list_head = S->next;
	      free (S);
	      S = list_head;
	    }
	    else {				// not the first one.
	      dlprev->next = S->next;
	      free (S);
	      S = dlprev->next;
	    }
	  }
	  else {
	    dlprev = S;
	    S = S->next;
	  }
	}

/*
 * If there are no link state machines (streams) remaining, there should be no txdata items still allocated.
 */
	if (list_head == NULL) {
	  cdata_check_leak();
	}

/*
 * Remove registered callsigns for this client.
 */

	rcprev = NULL;
	r = reg_callsign_list;
	while (r != NULL) {

	  assert (r->magic == RC_MAGIC);

	  if (r->client == E->client) {

	    if (r == reg_callsign_list) {

	      reg_callsign_list = r->next;
	      memset (r, 0, sizeof(reg_callsign_t));
	      free (r);
	      r = reg_callsign_list;
	    }
	    else {

	      rcprev->next = r->next;
	      memset (r, 0, sizeof(reg_callsign_t));
	      free (r);
	      r = rcprev->next;
	    }
	  }
	  else {
	    rcprev = r;
	    r = r->next;
	  }
	}

} /* end dl_client_cleanup */



/*------------------------------------------------------------------------------
 *
 * Name:	dl_data_indication
 * 
 * Purpose:	send connected data to client application.
 *
 * Inputs:	pid		- Protocol ID.
 *
 *		data		- Pointer to array of bytes.
 *
 *		len		- Number of bytes in data.
 *
 *
 * Description:	TODO:  We perform reassembly of segments here if necessary.
 *	
 *------------------------------------------------------------------------------*/

static void dl_data_indication (ax25_dlsm_t *S, int pid, char *data, int len)
{

	

// Now it gets more interesting. We need to combine segments before passing it along.

// See example in dl_data_request.

	if (S->ra_buff == NULL) {

// Ready state.

	  if (pid != AX25_PID_SEGMENTATION_FRAGMENT) {
	    server_rec_conn_data (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], pid, data, len);
	    return;
	  } 
	  else if (data[0] & 0x80) {

// Ready state, First segment.

	    S->ra_following = data[0] & 0x7f;
	    int total = (S->ra_following + 1) * (len - 1) - 1;		// len should be other side's N1
	    S->ra_buff = cdata_new(data[1], NULL, total);
	    S->ra_buff->size = total;	// max that we are expecting.
	    S->ra_buff->len = len - 2;	// how much accumulated so far.
	    memcpy (S->ra_buff->data, data + 2, len - 2);
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Stream %d: AX.25 Reassembler Protocol Error Z: Not first segment in ready state.\n", S->stream_id);
	  }
	}
	else {

// Reassembling data state

	  if (pid != AX25_PID_SEGMENTATION_FRAGMENT) {
	  	  
	    server_rec_conn_data (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], pid, data, len);

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Stream %d: AX.25 Reassembler Protocol Error Z: Not segment in reassembling state.\n", S->stream_id);
	    cdata_delete(S->ra_buff);
	    S->ra_buff = NULL;
	    return;
	  }
	  else if (data[0] & 0x80) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Stream %d: AX.25 Reassembler Protocol Error Z: First segment in reassembling state.\n", S->stream_id);
	    cdata_delete(S->ra_buff);
	    S->ra_buff = NULL;
	    return;
	  }
	  else if ((data[0] & 0x7f) != S->ra_following - 1) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Stream %d: AX.25 Reassembler Protocol Error Z: Segments out of sequence.\n", S->stream_id);
	    cdata_delete(S->ra_buff);
	    S->ra_buff = NULL;
	    return;
	  }
	  else {

// Reassembling data state, Not first segment.

	    S->ra_following = data[0] & 0x7f;
	    if (S->ra_buff->len + len - 1 <= S->ra_buff->size) {
	      memcpy (S->ra_buff->data + S->ra_buff->len, data + 1, len - 1);
	      S->ra_buff->len += len - 1;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Reassembler Protocol Error Z: Segments exceed buffer space.\n", S->stream_id);
	      cdata_delete(S->ra_buff);
	      S->ra_buff = NULL;
	      return;
	    }

	    if (S->ra_following == 0) {
// Last one.
	      server_rec_conn_data (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], S->ra_buff->pid, S->ra_buff->data, S->ra_buff->len);
	      cdata_delete(S->ra_buff);
	      S->ra_buff = NULL;
	    }
	  }
	}


} /* end dl_data_indication */



/*------------------------------------------------------------------------------
 *
 * Name:	lm_channel_busy
 * 
 * Purpose:	Change in DCD or PTT status for channel so we know when it is busy.
 *
 * Inputs:	E	- Event from the queue.
 *
 *		E->chan		- Radio channel number.
 *
 *		E->activity	- OCTYPE_PTT for my transmission start/end.
 *				- OCTYPE_DCD if we hear someone else.
 *
 *		E->status	- 1 for active or 0 for quiet.
 *
 * Outputs:	S->radio_channel_busy
 *
 *		T1 & TM201 paused/resumed if running.
 *
 * Description:	We need to pause the timers when the channel is busy.
 *
 *------------------------------------------------------------------------------*/

static int dcd_status[MAX_CHANS];
static int ptt_status[MAX_CHANS];

void lm_channel_busy (dlq_item_t *E)
{
	int busy;

	assert (E->chan >= 0 && E->chan < MAX_CHANS);
	assert (E->activity == OCTYPE_PTT || E->activity == OCTYPE_DCD);
	assert (E->status == 1 || E->status == 0);

	switch (E->activity) {

	  case OCTYPE_DCD:

	    if (s_debug_radio) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("lm_channel_busy: DCD chan %d = %d\n", E->chan, E->status);
	    }

	    dcd_status[E->chan] = E->status;
	    break;

	  case OCTYPE_PTT:

	    if (s_debug_radio) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("lm_channel_busy: PTT chan %d = %d\n", E->chan, E->status);
	    }

	    ptt_status[E->chan] = E->status;
	    break;

	  default:
	    break;
	}

	busy = dcd_status[E->chan] | ptt_status[E->chan];

/*
 * We know if the given radio channel is busy or not.
 * This must be applied to all data link state machines associated with that radio channel.
 */

	ax25_dlsm_t *S;

	for (S = list_head; S != NULL; S = S->next) {

	  if (E->chan == S->chan) {

	    if (busy && ! S->radio_channel_busy) {
	      S->radio_channel_busy = 1;
	      PAUSE_T1;
	      PAUSE_TM201;
	    }
	    else if ( ! busy && S->radio_channel_busy) {
	      S->radio_channel_busy = 0;
	      RESUME_T1;
	      RESUME_TM201;
	    }
	  }
	}

} /* end lm_channel_busy */




/*------------------------------------------------------------------------------
 *
 * Name:	lm_seize_confirm
 * 
 * Purpose:	Notification the the channel is clear.
 *
 * Description:	C4.2.  This primitive indicates to the Data-link State Machine that
 *		the transmission opportunity has arrived.
 *
 * Version 1.5:	Originally this only invoked inquiry_response to provide an ack if not already
 *		taken care of by an earlier frame in this transmission.
 *		After noticing the unnecessary I frame duplication and differing N(R) in the same
 *		transmission, I came to the conclusion that we should delay sending of new
 *		(not resends as a result of rej or srej) frames until after after processing
 *		of everything in the incoming transmission.
 *		The protocol spec simply has "I frame pops off queue" without any indication about
 *		what might trigger this event.
 *
 *------------------------------------------------------------------------------*/

void lm_seize_confirm (dlq_item_t *E)
{

	assert (E->chan >= 0 && E->chan < MAX_CHANS);

	ax25_dlsm_t *S;

	for (S = list_head; S != NULL; S = S->next) {

	  if (E->chan == S->chan) {


	    switch (S->state) {

	      case 	state_0_disconnected:
	      case 	state_1_awaiting_connection:
	      case 	state_2_awaiting_release:
	      case 	state_5_awaiting_v22_connection:

	        break;

	      case 	state_3_connected:
	      case 	state_4_timer_recovery:

	        // v1.5 change in strategy.
	        // New I frames, not sent yet, are delayed until after processing anything in the received transmission.
	        // Previously we started sending new frames, from the client app, as soon as they arrived.
	        // Now, we first take care of those in progress before throwing more into the mix.

	        i_frame_pop_off_queue(S);

	        // Need an RR if we didn't have I frame send the necessary ack.

	        if (S->acknowledge_pending) {
	          S->acknowledge_pending = 0;
	          enquiry_response (S, frame_not_AX25, 0);
	        }

// Implementation difference: The flow chart for state 3 has LM-RELEASE Request here.
// I don't think I need it because the transmitter will turn off
// automatically once the queue is empty.

// Erratum: The original spec had LM-SEIZE request here, for state 4, which didn't seem right.
// The 2006 revision has LM-RELEASE Request so states 3 & 4 are the same.
	
	        break;
	    }
	  }
	}

} /* lm_seize_confirm */



/*------------------------------------------------------------------------------
 *
 * Name:	lm_data_indication
 * 
 * Purpose:	We received some sort of frame over the radio.
 *
 * Inputs:	E	- Event from the queue.
 *			  Caller is responsible for freeing it.
 *
 * Description:	First determine if is of interest to me.  Two cases:
 *
 *		(1) We already have a link handle for (from-addr, to-addr, channel).
 *			This could have been set up by an outgoing connect request.
 *
 *		(2) It is addressed to one of the registered callsigns.  This would
 *			catch the case of incoming connect requests.  The APRS MYCALL
 *			is not involved at all.  The attached client app might have
 *			much different ideas about what the station is called or
 *			aliases it might respond to.
 *	
 *------------------------------------------------------------------------------*/

void lm_data_indication (dlq_item_t *E)
{
	ax25_frame_type_t ftype;
	char desc[80];
	cmdres_t cr;
	int pf;
	int nr;
	int ns;
	ax25_dlsm_t *S;
	int client_not_applicable = -1;
	int n;
	int any_unused_digi;


	if (E->pp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal Error, packet pointer is null.  %s %s %d\n", __FILE__, __func__, __LINE__);
	  return;
	}

	E->num_addr = ax25_get_num_addr(E->pp);

// Digipeating is not done here so consider only those with no unused digipeater addresses.

	any_unused_digi = 0;

	for (n = AX25_REPEATER_1; n < E->num_addr; n++) {
	  if ( ! ax25_get_h(E->pp, n)) {
	    any_unused_digi = 1;
	  }
	}

	if (any_unused_digi) {
	  if (s_debug_radio) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("lm_data_indication (%d, %s>%s) - ignore due to unused digi address.\n", E->chan, E->addrs[AX25_SOURCE], E->addrs[AX25_DESTINATION]);
	  }
	  return;
	}

// Copy addresses from frame into event structure.

	for (n = 0; n < E->num_addr; n++) {
	  ax25_get_addr_with_ssid (E->pp, n, E->addrs[n]);
	}

	if (s_debug_radio) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("lm_data_indication (%d, %s>%s)\n", E->chan, E->addrs[AX25_SOURCE], E->addrs[AX25_DESTINATION]);
	}

// Look for existing, or possibly create new, link state matching addresses and channel.

// In most cases, we can ignore the frame if we don't have a corresponding
// data link state machine.  However, we might want to create a new one for SABM or SABME.
// get_link_handle will check to see if the destination matches my address.

// TODO: This won't work right because we don't know the modulo yet.
// Maybe we should have a shorter form that only returns the frame type.
// That is all we need at this point.

	ftype = ax25_frame_type (E->pp, &cr, desc, &pf, &nr, &ns);

	S = get_link_handle (E->addrs, E->num_addr, E->chan, client_not_applicable,
				(ftype == frame_type_U_SABM) | (ftype == frame_type_U_SABME));

	if (S == NULL) {
	  return;
	}

/*
 * There is not a reliable way to tell if a frame, out of context, has modulo 8 or 128 
 * sequence numbers.  This needs to be supplied from the data link state machine.
 *
 * We can't do this until we get the link handle.
 */

	ax25_set_modulo (E->pp, S->modulo);

/*
 * Now we need to use ax25_frame_type again because the previous results, for nr and ns, might be wrong.
 */

	ftype = ax25_frame_type (E->pp, &cr, desc, &pf, &nr, &ns);

// Gather statistics useful for testing.

	if (ftype <= frame_not_AX25) {
	  S->count_recv_frame_type[ftype]++;
	}

	switch (ftype) {

	  case frame_type_I:
	    if (cr != cr_cmd) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error S: %s must be COMMAND.\n", S->stream_id, desc);
	    }
	    break;

	  case frame_type_S_RR:
	  case frame_type_S_RNR:
	  case frame_type_S_REJ:
	    if (cr != cr_cmd && cr != cr_res) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error: %s must be COMMAND or RESPONSE.\n", S->stream_id, desc);
	    }
	    break;

	  case frame_type_U_SABME:
	  case frame_type_U_SABM:
	  case frame_type_U_DISC:
	    if (cr != cr_cmd) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error: %s must be COMMAND.\n", S->stream_id, desc);
	    }
	    break;

// Erratum: The AX.25 spec is not clear about whether SREJ should be command, response, or both.
// The underlying X.25 spec clearly says it is response only.  Let's go with that.

	  case frame_type_S_SREJ:
	  case frame_type_U_DM:
	  case frame_type_U_UA:
	  case frame_type_U_FRMR:
	    if (cr != cr_res) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error: %s must be RESPONSE.\n", S->stream_id, desc);
	    }
	    break;

	  case frame_type_U_XID:
	  case frame_type_U_TEST:
	    if (cr != cr_cmd && cr != cr_res) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error: %s must be COMMAND or RESPONSE.\n", S->stream_id, desc);
	    }
	    break;

	  case frame_type_U_UI:
	    // Don't test at this point in case an APRS frame gets thru.
	    // APRS doesn't specify what to put in the Source and Dest C bits.
	    // In practice we see all 4 possible combinations.
	    // I have an opinion about what would be "correct" (discussed elsewhere)
	    // but in practice no one seems to care.
	    break;

	  case frame_type_U:
	  case frame_not_AX25:
	    // not expected.
	    break;
	}


	switch (ftype) {

	  case frame_type_I:           // Information
	    {
	      int pid;
	      unsigned char *info_ptr;
	      int info_len;

	      pid = ax25_get_pid (E->pp);
	      info_len = ax25_get_info (E->pp, &info_ptr);

	      i_frame (S, cr, pf, nr, ns, pid, (char *)info_ptr, info_len);
	    }
	    break;

	  case frame_type_S_RR:        // Receive Ready - System Ready To Receive
	    rr_rnr_frame (S, 1, cr, pf, nr);
	    break;

	  case frame_type_S_RNR:       // Receive Not Ready - TNC Buffer Full
	    rr_rnr_frame (S, 0, cr, pf, nr);
	    break;

	  case frame_type_S_REJ:       // Reject Frame - Out of Sequence or Duplicate
	    rej_frame (S, cr, pf, nr);
	    break;

	  case frame_type_S_SREJ:      // Selective Reject - Ask for selective frame(s) repeat
	    {
	      unsigned char *info_ptr;
	      int info_len;

	      info_len = ax25_get_info (E->pp, &info_ptr);
	      srej_frame (S, cr, pf, nr, info_ptr, info_len);
	    }
	    break;

	  case frame_type_U_SABME:     // Set Async Balanced Mode, Extended
	    sabm_e_frame (S, 1, pf);
	    break;

	  case frame_type_U_SABM:      // Set Async Balanced Mode
	    sabm_e_frame (S, 0, pf);
	    break;

	  case frame_type_U_DISC:      // Disconnect
	    disc_frame (S, pf);
	    break;

	  case frame_type_U_DM:        // Disconnect Mode
	    dm_frame (S, pf);
	    break;

	  case frame_type_U_UA:        // Unnumbered Acknowledge
	    ua_frame (S, pf);
	    break;

	  case frame_type_U_FRMR:      // Frame Reject
	    frmr_frame (S);
	    break;

	  case frame_type_U_UI:        // Unnumbered Information
	    ui_frame (S, cr, pf);
	    break;

	  case frame_type_U_XID:       // Exchange Identification
	    {
	      unsigned char *info_ptr;
	      int info_len;

	      info_len = ax25_get_info (E->pp, &info_ptr);

	      xid_frame (S, cr, pf, info_ptr, info_len);
	    }
	    break;

	  case frame_type_U_TEST:      // Test
	    {
	      unsigned char *info_ptr;
	      int info_len;

	      info_len = ax25_get_info (E->pp, &info_ptr);

	      test_frame (S, cr, pf, info_ptr, info_len);
	    }
	    break;

	  case frame_type_U:           // other Unnumbered, not used by AX.25.
	    break;

	  case frame_not_AX25:         // Could not get control byte from frame.
	    break;
	}

// An incoming frame might have ack'ed frames we sent or indicated peer is no longer busy.
// Rather than putting this test in many places, where those conditions, may have changed,
// we will try to catch them all on this single path.
// Start transmission if we now have some outgoing data ready to go.
// (Added in 1.5 beta 3 for issue 132.)

	if ( S->i_frame_queue != NULL &&
		(S->state == state_3_connected || S->state == state_4_timer_recovery) &&
		( ! S->peer_receiver_busy ) &&
		WITHIN_WINDOW_SIZE(S) ) {

	  //S->acknowledge_pending = 1;
	  lm_seize_request (S->chan);
	}

} /* end lm_data_indication */



/*------------------------------------------------------------------------------
 *
 * Name:	i_frame
 * 
 * Purpose:	Process I Frame.
 *
 * Inputs:	S	- Data Link State Machine.
 *		cr	- Command or Response.  We have already issued an error if not command.
 *		p	- Poll bit.  Assuming we checked earlier that it was a command.
 *			  The meaning is described below.
 *		nr	- N(R) from the frame.  Next expected seq. for other end.
 *		ns	- N(S) from the frame.  Seq. number of this incoming frame.
 *		pid	- protocol id.
 *		info_ptr - pointer to information part of frame.
 *		info_len - Number of bytes in information part of frame.
 *			   Should be in range of 0 thru n1_paclen.
 *
 * Description:	
 *		6.4.2. Receiving I Frames
 *
 *		The reception of I frames that contain zero-length information fields is reported to the next layer; no information
 *		field will be transferred.
 *
 *		6.4.2.1. Not Busy
 *
 *		If a TNC receives a valid I frame (one with a correct FCS and whose send sequence number equals the
 *		receiver's receive state variable) and is not in the busy condition, it accepts the received I frame, increments its
 *		receive state variable, and acts in one of the following manners:
 *
 *		a) If it has an I frame to send, that I frame may be sent with the transmitted N(R) equal to its receive state
 *		variable V(R) (thus acknowledging the received frame). Alternately, the TNC may send an RR frame with N(R)
 *		equal to V(R), and then send the I frame.
 *
 *		or b) If there are no outstanding I frames, the receiving TNC sends an RR frame with N(R) equal to V(R). The
 *		receiving TNC may wait a small period of time before sending the RR frame to be sure additional I frames are
 *		not being transmitted.
 *
 *		6.4.2.2. Busy
 *
 *		If the TNC is in a busy condition, it ignores any received I frames without reporting this condition, other than
 *		repeating the indication of the busy condition.
 *		If a busy condition exists, the TNC receiving the busy condition indication polls the sending TNC periodically
 *		until the busy condition disappears.
 *		A TNC may poll the busy TNC periodically with RR or RNR frames with the P bit set to "1".
 *
 *		6.4.6. Receiving Acknowledgement
 *
 *		Whenever an I or S frame is correctly received, even in a busy condition, the N(R) of the received frame is
 *		checked to see if it includes an acknowledgement of outstanding sent I frames. The T1 timer is canceled if the
 *		received frame actually acknowledges previously unacknowledged frames. If the T1 timer is canceled and there
 *		are still some frames that have been sent that are not acknowledged, T1 is started again. If the T1 timer expires
 *		before an acknowledgement is received, the TNC proceeds with the retransmission procedure outlined in Section
 *		6.4.11.
 *
 *
 *		6.2. Poll/Final (P/F) Bit Procedures
 *
 *		The next response frame returned to an I frame with the P bit set to "1", received during the information
 *		transfer state, is an RR, RNR or REJ response with the F bit set to "1".
 *
 *		The next response frame returned to a S or I command frame with the P bit set to "1", received in the
 *		disconnected state, is a DM response frame with the F bit set to "1".
 *
 *------------------------------------------------------------------------------*/

static void i_frame (ax25_dlsm_t *S, cmdres_t cr, int p, int nr, int ns, int pid, char *info_ptr, int info_len)
{
	switch (S->state) {

	  case 	state_0_disconnected:

	    // Logic from flow chart for "all other commands."

	    if (cr == cr_cmd) {
	      cmdres_t r = cr_res;	// DM response with F taken from P.
	      int f = p;
	      int nopid = 0;		// PID applies only for I and UI frames.

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, r, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    }
	    break;

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:

	    // Ignore it.  Keep same state.
	    break;
 
	  case 	state_2_awaiting_release:

	    // Logic from flow chart for "I, RR, RNR, REJ, SREJ commands."

	    if (cr == cr_cmd && p == 1) {
	      cmdres_t r = cr_res;	// DM response with F = 1.
	      int f = 1;
	      int nopid = 0;		// PID applies only for I and UI frames.

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, r, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    }
	    break;

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:

	    // Look carefully.  The original had two tiny differences between the two states.
	    // In the 2006 version, these differences no longer exist.

	    // Erratum: SDL asks: Is information field length <= N1 (paclen).
	    // (github issue 102 - Thanks to KK6WHJ for pointing this out.)
	    // Just because we are limiting the size of our transmitted data, it doesn't mean
	    // that the other end will be doing the same.  With v2.2, the XID frame can be
	    // used to negotiate a maximum info length but with v2.0, there is no way for the
	    // other end to know our paclen value.

	    if (info_len >= 0 && info_len <= AX25_MAX_INFO_LEN) {

	      if (is_good_nr(S,nr)) {

	        // Erratum?
	        // I wonder if this difference is intentional or if only one place was
	        // was modified after a cut-n-paste of the flow chart segment.

		// Erratum: Discrepancy between original and 2006 version.

		// Pattern noticed:  Anytime we have "is_good_nr" which tests for V(A) <= N(R) <= V(S),
		// we should always call "check_i_frame_ackd" or at least set V(A) from N(R).

#if 0	// Erratum: original - states 3 & 4 differ here.

	        if (S->state == state_3_connected) {
	          // This sets "S->va = nr" and also does some timer stuff.
	          check_i_frame_ackd (S,nr);
	        }
	        else {
	          SET_VA(nr);
	        }

#else 	// 2006 version - states 3 & 4 same here.

	        // This sets "S->va = nr" and also does some timer stuff.

	        check_i_frame_ackd (S,nr);
#endif

// Erratum: v1.5 - My addition.
// I noticed that we sometimes got stuck in state 4 and rc crept up slowly even though
// we received 'I' frames with N(R) values indicating that the other side received everything
// that we sent.  Eventually rc could reach the limit and we would get an error.
// If we are in state 4, and other guy ack'ed last I frame we sent, transition to state 3.
// We had a similar situation for RR/RNR for cases other than response, F=1.

	        if (S->state == state_4_timer_recovery && S->va == S->vs) {

	          STOP_T1;
	          select_t1_value (S);
	          START_T3;
	          SET_RC(0);
	          enter_new_state (S, state_3_connected, __func__, __LINE__);
	        }

	        if (S->own_receiver_busy) {
	          // This should be unreachable because we currently don't have a way to set own_receiver_busy.
	          // But we might the capability someday so implement this while we are here.

	          if (p == 1) {
	            cmdres_t cr = cr_res;	// Erratum: The use of "F" in the flow chart implies that RNR is a response
						// in this case, but I'm not confident about that.  The text says frame.
	            int f = 1;
	            int nr = S->vr;
	            packet_t pp;
 
	            pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_RNR, S->modulo, nr, f, NULL, 0);

	            // I wonder if this difference is intentional or if only one place was
	            // was modified after a cut-n-paste of the flow chart segment.

#if 0 // Erratum: Original - state 4 has expedited.

	            if (S->state == state_4_timer_recovery) {
	              lm_data_request (S->chan, TQ_PRIO_0_HI, pp);		// "expedited"
	            }
	            else {
	              lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	            }
#else // 2006 version - states 3 & 4 the same.

	            lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

#endif
	            S->acknowledge_pending = 0;
	          }

	        }
	        else {		// Own receiver not busy.

		  i_frame_continued (S, p, ns, pid, info_ptr, info_len);
	        }


	      }
	      else {		// N(R) not in expected range.

	        nr_error_recovery (S);
	        // my enhancement.  See below.
	        enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	      }
	    }
	    else {		// Bad information length.
				// Wouldn't even get to CRC check if not octet aligned.

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error O: Information part length, %d, not in range of 0 thru %d.\n", S->stream_id, info_len, AX25_MAX_INFO_LEN);

	      establish_data_link (S);
	      S->layer_3_initiated = 0;

	      // The original spec always sent SABM and went to state 1.
	      // I was thinking, why not use v2.2 instead of we were already connected with v2.2?
	      // My version of establish_data_link combined the two original functions and
	      // already uses SABME or SABM based on S->modulo.

	      enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	    }
	    break;
	}

} /* end i_frame */



/*------------------------------------------------------------------------------
 *
 * Name:	i_frame_continued
 * 
 * Purpose:	The I frame processing logic gets pretty complicated.
 *		Some of it has been split out into a separate function to make
 *		things more readable.
 *		We have already done some error checking and processed N(R).
 *		This is used for both states 3 & 4.
 *
 * Inputs:	S	- Data Link State Machine.  We are in state 3.
 *		p	- Poll bit.
 *		ns	- N(S) from the frame.  Seq. number of this incoming frame.
 *		pid	- protocol id.
 *		info_ptr - pointer to information part of frame.
 *		info_len - Number of bytes in information part of frame.  Already verified.
 *			   
 * Description:	
 *
 *		4.3.2.3. Reject (REJ) Command and Response
 *
 *		The reject frame requests retransmission of I frames starting with N(R). Any frames sent with a sequence
 *		number of N(R)-1 or less are acknowledged. Additional I frames which may exist may be appended to the
 *		retransmission of the N(R) frame.
 *		Only one reject frame condition is allowed in each direction at a time. The reject condition is cleared by the
 *		proper reception of I frames up to the I frame that caused the reject condition to be initiated.
 *		The status of the TNC at the other end of the link is requested by sending a REJ command frame with the P bit
 *		set to one.
 *
 *		4.3.2.4. Selective Reject (SREJ) Command and Response
 *
 * 	(Erratum: SREJ is only response with F bit.)
 *
 *		The selective reject, SREJ, frame is used by the receiving TNC to request retransmission of the single I frame
 *		numbered N(R). If the P/F bit in the SREJ frame is set to "1", then I frames numbered up to N(R)-1 inclusive are
 *		considered as acknowledged. However, if the P/F bit in the SREJ frame is set to "0", then the N(R) of the SREJ
 *		frame does not indicate acknowledgement of I frames.
 *
 *		Each SREJ exception condition is cleared (reset) upon receipt of the I frame with an N(S) equal to the N(R)
 *		of the SREJ frame.
 *
 *		A receiving TNC may transmit one or more SREJ frames, each containing a different N(R) with the P bit set
 *		to "0", before one or more earlier SREJ exception conditions have been cleared. However, a SREJ is not
 *		transmitted if an earlier REJ exception condition has not been cleared as indicated in Section 4.5.4. (To do so
 *		would request retransmission of an I frame that would be retransmitted by the REJ operation.) Likewise, a REJ
 *		frame is not transmitted if one or more earlier SREJ exception conditions have not been cleared as indicated in
 *
 *		Section 4.5.4.
 *
 *		I frames transmitted following the I frame indicated by the SREJ frame are not retransmitted as the result of
 *		receiving a SREJ frame. Additional I frames awaiting initial transmission may be transmitted following the
 *		retransmission of the specific I frame requested by the SREJ frame.
 *
 *
 *		6.4.4. Reception of Out-of-Sequence Frames
 *
 *		6.4.4.1. Implicit Reject (REJ)
 *
 *		When an I frame is received with a correct FCS but its send sequence number N(S) does not match the current
 *		receiver's receive state variable, the frame is discarded. A REJ frame is sent with a receive sequence number
 *		equal to one higher than the last correctly received I frame if an uncleared N(S) sequence error condition has not
 *		been previously established. The received state variable and poll bit of the discarded frame is checked and acted
 *		upon, if necessary.
 *		This mode requires no frame queueing and frame resequencing at the receiver. However, because the mode
 *		requires transmission of frames that may not be in error, its throughput is not as high as selective reject. This
 *		mode is ineffective on systems with long round-trip delays and high data rates.
 *
 *		6.4.4.2. Selective Reject (SREJ)
 *
 *		When an I frame is received with a correct FCS but its send sequence number N(S) does not match the current
 *		receiver's receive state variable, the frame is retained. SREJ frames are sent with a receive sequence number
 *		equal to the value N(R) of the missing frame, and P=1 if an uncleared SREJ condition has not been previously
 *		established. If an SREJ condition is already pending, an SREJ will be sent with P=0. The received state variable
 *		and poll bit of the received frame are checked and acted upon, if necessary.
 *		This mode requires frame queueing and frame resequencing at the receiver. The holding of frames can
 *		consume precious buffer space, especially if the user device has limited memory available and several active
 *		links are operational.
 *
 *		6.4.4.3. Selective Reject-Reject (SREJ/REJ)
 *
 *	(Erratum: REJ/SREJ should not be mixed.  Basic (mod 8) allows only REJ.
 *		  Extended (mod 128) gives you a choice of one or the other for a link.)
 *
 *		When an I frame is received with a correct FCS but its send sequence number N(S) does not match the current
 *		receiver's receive state variable, and if N(S) indicates 2 or more frames are missing, a REJ frame is transmitted.
 *		All subsequently received frames are discarded until the lost frame is correctly received. If only one frame is
 *		missing, a SREJ frame is sent with a receive sequence number equal to the value N(R) of the missing frame. The
 *		received state variable and poll bit of the received frame are checked and acted upon. If another frame error
 *		occurs prior to recovery of the SREJ condition, the receiver saves all frames received after the first errored frame
 *		and discards frames received after the second errored frame until the first errored frame is recovered. Then, a
 *		REJ is issued to recover the second errored frame and all subsequent discarded frames.
 *
 *------------------------------------------------------------------------------*/

static void i_frame_continued (ax25_dlsm_t *S, int p, int ns, int pid, char *info_ptr, int info_len)
{

	if (ns == S->vr) {

// The receive sequence number, N(S), is the same as what we were expecting, V(R).
// Send it to the application and increment the next expected.
// It is possible that this was resent and we tucked away others with the following
// sequence numbers.  If so, process them too.


	  SET_VR(AX25MODULO(S->vr + 1, S->modulo, __FILE__, __func__, __LINE__));
	  S->reject_exception = 0;


	  if (s_debug_client_app) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("call dl_data_indication() at %s %d, N(S)=%d, V(R)=%d, \"", __func__, __LINE__, ns, S->vr);
	    ax25_safe_print (info_ptr, info_len, 1);
	    dw_printf ("\"\n");
	  }

	  dl_data_indication (S, pid, info_ptr, info_len);

	  if (S->rxdata_by_ns[ns] != NULL) {
	    // There is a possibility that we might have another received frame stashed
	    // away from 8 or 128 (modulo) frames back.  Remove it so it doesn't accidentally
	    // show up at some future inopportune time.

	    cdata_delete (S->rxdata_by_ns[ns]);
	    S->rxdata_by_ns[ns] = NULL;

	  }


	  while (S->rxdata_by_ns[S->vr] != NULL) {

	    // dl_data_indication - send connected data to client application.

	    if (s_debug_client_app) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("call dl_data_indication() at %s %d, N(S)=%d, V(R)=%d, data=\"", __func__, __LINE__, ns, S->vr);
	      ax25_safe_print (S->rxdata_by_ns[S->vr]->data, S->rxdata_by_ns[S->vr]->len, 1);
	      dw_printf ("\"\n");
	    }

	    dl_data_indication (S, S->rxdata_by_ns[S->vr]->pid, S->rxdata_by_ns[S->vr]->data, S->rxdata_by_ns[S->vr]->len);

	    // Don't keep around anymore after sending it to client app.

	    cdata_delete (S->rxdata_by_ns[S->vr]);
	    S->rxdata_by_ns[S->vr] = NULL;

	    SET_VR(AX25MODULO(S->vr + 1, S->modulo, __FILE__, __func__, __LINE__));
	  }

	  if (p) {

// Mentioned in section 6.2.
// The next response frame returned to an I frame with the P bit set to "1", received during the information
// transfer state, is an RR, RNR or REJ response with the F bit set to "1".

	    int f = 1;
	    int nr = S->vr;		// Next expected sequence number.
	    cmdres_t cr = cr_res;	// response with F set to 1.
	    packet_t pp;

	    pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_RR, S->modulo, nr, f, NULL, 0);
	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    S->acknowledge_pending = 0;
	  }
	  else if ( ! S->acknowledge_pending) {

    	    S->acknowledge_pending = 1;  // Probably want to set this before the LM-SEIZE Request
					// in case the LM-SEIZE Confirm gets processed before we
					// return from it.
	    
	    // Force start of transmission even if the transmit frame queue is empty.
	    // Notify me, with lm_seize_confirm, when transmission has started.
	    // When that event arrives, we check acknowledge_pending and send something
	    // to be determined later.

	    lm_seize_request (S->chan);
	  }
	}
	else if (S->reject_exception) {

// This is not the sequence we were expecting.
// We previously sent REJ, asking for a resend so don't send another.
// In this case, send RR only if the Poll bit is set.
// Again, reference section 6.2.

	  if (p) {
	    int f = 1;
	    int nr = S->vr;		// Next expected sequence number.
	    cmdres_t cr = cr_res;	// response with F set to 1.
	    packet_t pp;

	    pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_RR, S->modulo, nr, f, NULL, 0);
	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    S->acknowledge_pending = 0;
	  }
	}
	else if (S->srej_enable == srej_none) {

// The received sequence number is not the expected one and we can't use SREJ.
// The old v2.0 approach is to send and REJ with the number we are expecting.
// This can be very inefficient.  For example if we received 1,3,4,5,6 in one transmission,
// we discard 3,4,5,6, and tell the other end to resend everything starting with 2.

// At one time, I had some doubts about when to use command or response for REJ.
// I now believe that response, as implied by setting F in the flow chart, is correct.

	  int f = p;
	  int nr = S->vr;		// Next expected sequence number.
	  cmdres_t cr = cr_res;		// response with F copied from P in I frame.
	  packet_t pp;

	  S->reject_exception = 1;

	  if (s_debug_retry) {
	      text_color_set(DW_COLOR_ERROR);	// make it more noticeable.
	      dw_printf ("sending REJ, at %s %d, SREJ not enabled case, V(R)=%d", __func__, __LINE__, S->vr);
	  }

	  pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_REJ, S->modulo, nr, f, NULL, 0);
	  lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	  S->acknowledge_pending = 0;
	} 
	else {

// Selective reject is enabled so we can use the more efficient method.
// This is normally enabled for v2.2 but XID can be used to change that.
// First we save the current frame so we can retrieve it later after getting the fill in.

	  if (S->modulo != 128) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("INTERNAL ERROR: Should not be sending SREJ in basic (modulo 8) mode.\n");
	  }

#if 1

// Erratum:  AX.25 protocol spec did not handle SREJ very well.
// Based on X.25 section 2.4.6.4.


	  if (is_ns_in_window(S, ns)) {

// X.25 2.4.6.4 (b)
// v(R) < N(S) < V(R)+k so it is in the expected range.
// Save it in the receive buffer.

	    if (S->rxdata_by_ns[ns] != NULL) {
	      cdata_delete (S->rxdata_by_ns[ns]);
	      S->rxdata_by_ns[ns] = NULL;
	    }
	    S->rxdata_by_ns[ns] = cdata_new(pid, info_ptr, info_len);

	    if (s_debug_misc) {
	      dw_printf ("%s %d, save to rxdata_by_ns N(S)=%d, V(R)=%d, \"", __func__, __LINE__, ns, S->vr);
	      ax25_safe_print (info_ptr, info_len, 1);
	      dw_printf ("\"\n");
	    }

	    if (p == 1) {
	      int f = 1;
	      enquiry_response (S, frame_type_I, f);
	    }
	    else if (S->own_receiver_busy) {
	      cmdres_t cr = cr_res;	// send RNR response
	      int f = 0;	// we know p=0 here.
	      int nr = S->vr;
	      packet_t pp;

	      pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_RNR, S->modulo, nr, f, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    }
	    else if (S->rxdata_by_ns[ AX25MODULO(ns - 1, S->modulo, __FILE__, __func__, __LINE__)] == NULL) {

// Ask for missing frames when we don't have N(S)-1 in the receive buffer.

// In version 1.4:
// We end up sending more SREJ than necessary and and get back redundant information.  Example:
// When we see 113 missing, we ask for a resend.
// When we see 115 & 116 missing, a cumulative SREJ asks for everything.
// The other end dutifully sends 113 twice.
//
// [0.4] DW1>DW0:(SREJ res, n(r)=113, f=0)
// [0.4] DW1>DW0:(SREJ res, n(r)=113, f=1)<0xe6><0xe8>
//
// [0L] DW0>DW1:(I cmd, n(s)=113, n(r)=11, p=0, pid=0xf0)0114 send data<0x0d>
// [0L] DW0>DW1:(I cmd, n(s)=113, n(r)=11, p=0, pid=0xf0)0114 send data<0x0d>
// [0L] DW0>DW1:(I cmd, n(s)=115, n(r)=11, p=0, pid=0xf0)0116 send data<0x0d>
// [0L] DW0>DW1:(I cmd, n(s)=116, n(r)=11, p=0, pid=0xf0)0117 send data<0x0d>


// Version 1.5:
// Don't generate duplicate requests for gaps in the same transmission.

// Ideally, we might wait until carrier drops and then use one Multi-SREJ for entire transmission but
// we will keep that for another day.
// Probably need a flag similar to acknowledge_pending (or ask_resend_count, here) and the ask_for_resend array.
// It could then be processed first in lm_seize_confirm.

	      int ask_for_resend[128];
	      int ask_resend_count = 0;
	      int x;

// Version 1.5
// Erratum:  AX.25 says use F=0 here.  Doesn't make sense.
// We would want to set F when sending N(R) = V(R).
//	      int allow_f1 = 0;		// F=1 from X.25 2.4.6.4 b) 3)
	      int allow_f1 = 1;		// F=1 from X.25 2.4.6.4 b) 3)

// send only for this gap, not cumulative from V(R).

	      int last = AX25MODULO(ns - 1, S->modulo, __FILE__, __func__, __LINE__);
	      int first = last;
	      while (first != S->vr && S->rxdata_by_ns[AX25MODULO(first - 1, S->modulo, __FILE__, __func__, __LINE__)] == NULL) {
	        first = AX25MODULO(first - 1, S->modulo, __FILE__, __func__, __LINE__);
	      }
	      x = first;
	      do {
	        ask_for_resend[ask_resend_count++] = AX25MODULO(x, S->modulo, __FILE__, __func__, __LINE__);
	        x = AX25MODULO(x + 1, S->modulo, __FILE__, __func__, __LINE__);
	      } while (x != AX25MODULO(last + 1, S->modulo, __FILE__, __func__, __LINE__));

	      send_srej_frames (S, ask_for_resend, ask_resend_count, allow_f1);
	    }
	  }
	  else {

// X.25 2.4.6.4 a)
// N(S) is not in expected range.  Discard it.  Send response if P=1.

	    if (p == 1) {
	      int f = 1;
	      enquiry_response (S, frame_type_I, f);
	    }

	  }

#else  // my earlier attempt before taking a close look at X.25 spec.
	// Keeping it around for a little while because I might want to
	// use earlier technique of sending only needed SREJ for any second
	// and later gaps in a single multiframe transmission.


	  if (S->rxdata_by_ns[ns] != NULL) {
	    cdata_delete (S->rxdata_by_ns[ns]);
	    S->rxdata_by_ns[ns] = NULL;
	  }
	  S->rxdata_by_ns[ns] = cdata_new(pid, info_ptr, info_len);

	  S->outstanding_srej[ns] = 0;	// Don't care if it was previously set or not.
					// We have this one so there is no outstanding SREJ for it.

	  if (s_debug_misc) {
	    dw_printf ("%s %d, save to rxdata_by_ns N(S)=%d, V(R)=%d, \"", __func__, __LINE__, ns, S->vr);
	    ax25_safe_print (info_ptr, info_len, 1);
	    dw_printf ("\"\n");
	  }




	  if (selective_reject_exception(S) == 0) {

// Erratum:  This is vastly different than the SDL in the AX.25 protocol spec.
// That would use SREJ if only one was missing and REJ instead.
// Here we do not mix the them.
// This agrees with the X.25 protocol spec that says use one or the other.  Not both.

// Suppose we had incoming I frames 0, 3, 7.
// 0 was already processed and V(R)=1 meaning that is the next expected.
// At this point we area processing N(S)=3.
// In this case, we need to ask for a resend of 1 & 2.
// More generally, the range of V(R) thru N(S)-1.

	    int ask_for_resend[128];
	    int ask_resend_count = 0;
	    int i;
	    int allow_f1 = 1;

text_color_set(DW_COLOR_ERROR);
dw_printf ("%s:%d, zero exceptions, V(R)=%d, N(S)=%d\n", __func__, __LINE__, S->vr, ns);

	    for (i = S->vr; i != ns; i = AX25MODULO(i+1, S->modulo, __FILE__, __func__, __LINE__)) {
	      ask_for_resend[ask_resend_count++] = i;
	    }

	    send_srej_frames (S, ask_for_resend, ask_resend_count, allow_f1);
	  }
	  else {

// Erratum: The SDL says ask for N(S) which is clearly wrong because that's what we just received.
// Instead we want to ask for any missing frames up to but not including N(S).

// Let's continue with the example above.  I frames with N(S) of 0, 3, 7.
// selective_reject_exception is non zero meaning there are outstanding requests to resend specified I frames.
// V(R) is still 1 because 0 is the last one received with contiguous N(S) values.
// 3 has been saved into S->rxdata_by_ns.
// We now have N(S)=7.   We want to ask for a resend of 4, 5, 6.
// This can be achieved by searching S->rxdata_by_ns, starting with N(S)-1, and counting
// how many empty slots we have before finding a saved frame.

	    int ask_resend_count = 0;
	    int first;

text_color_set(DW_COLOR_ERROR);
dw_printf ("%s:%d, %d srej exceptions, V(R)=%d, N(S)=%d\n", __func__, __LINE__, selective_reject_exception(S), S->vr, ns);

	    first = AX25MODULO(ns - 1, S->modulo, __FILE__, __func__, __LINE__);
	    while (S->rxdata_by_ns[first] == NULL) {
	      if (first == AX25MODULO(S->vr - 1, S->modulo, __FILE__, __func__, __LINE__)) {
	        //  Oops!  Went too far.  This I frame was already processed.
		text_color_set(DW_COLOR_ERROR);
	        dw_printf ("INTERNAL ERROR calculating what to put in SREJ, %s line %d\n", __func__, __LINE__);
	        dw_printf ("V(R)=%d, N(S)=%d, SREJ exception=%d, first=%d, ask_resend_count=%d\n", S->vr, ns, selective_reject_exception(S), first, ask_resend_count);
		int k;
	        for (k=0; k<128; k++) {
	          if (S->rxdata_by_ns[k] != NULL) {
	            dw_printf ("rxdata_by_ns[%d] has data\n", k);
	          }
	        }
	        break;
	      }
	      ask_resend_count++;
	      first = AX25MODULO(first - 1, S->modulo, __FILE__, __func__, __LINE__);
	    }

	    // Go beyond the slot where we already have an I frame.
	    first = AX25MODULO(first + 1, S->modulo, __FILE__, __func__, __LINE__);
	    
	    // The ask_resend_count could be 0.  e.g. We got 4 rather than 7 in this example.

	    if (ask_resend_count > 0) {
	      int ask_for_resend[128];
	      int n;
	      int allow_f1 = 1;

	      for (n = 0; n < ask_resend_count; n++) {
	        ask_for_resend[n] = AX25MODULO(first + n, S->modulo, __FILE__, __func__, __LINE__);;
	      }
	
	      send_srej_frames (S, ask_for_resend, ask_resend_count, allow_f1);
	    }

	  } /* end SREJ exception */

#endif	// my earlier attempt.




// Erratum:  original has following but 2006 rev does not.
// I think the 2006 version is correct.
// SREJ does not always satisfy the need for ack.
// There is a special case where F=1.  We take care of that inside of send_srej_frames.

#if 0
	  S->acknowledge_pending = 0;
#endif

	} /* end srej enabled */


} /* end i_frame_continued */



/*------------------------------------------------------------------------------
 *
 * Name:	is_ns_in_window
 * 
 * Purpose:	Is the N(S) value of the incoming I frame in the expected range?
 *
 * Inputs:	ns		- Sequence from I frame.
 *
 * Description:	With selective reject, it is possible that we could receive a repeat of
 *		an I frame with N(S) less than V(R).  In this case, we just want to
 *		discard it rather than getting upset and reestablishing the connection.
 *
 *		The X.25 spec,section 2.4.6.4 (b) asks whether  V(R) < N(S) < V(R)+k.
 *		
 *		The problem here is that it depends on the value of k for the other end.
 *		X.25 says that both sides need to agree on a common value of k ahead of time.
 *		We might have k=8 for our sending but the other side could have k=32 so
 *		this test could fail.
 *
 *		As a hack, we could use the value 63 here.  If too small we would discard
 *		I frames that are in the acceptable range because they would be >= V(R)+k.
 *		On the other hand, if this value is too big, the range < V(R) would not be
 *		large enough and we would accept frame we shouldn't.
 *		As a practical matter, using a window size that large is pretty unlikely.
 *		Maybe I could put a limit of 63, rather than 127 in the configuration.
 *
 *------------------------------------------------------------------------------*/

#define GENEROUS_K 63

static int is_ns_in_window (ax25_dlsm_t *S, int ns)
{
	int adjusted_vr, adjusted_ns, adjusted_vrpk;
	int result;

/* Shift all values relative to V(R) before comparing so we won't have wrap around. */

#define adjust_by_vr(x) (AX25MODULO((x) - S->vr, S->modulo, __FILE__, __func__, __LINE__))

	adjusted_vr   = adjust_by_vr(S->vr);	// A clever compiler would know it is zero.
	adjusted_ns   = adjust_by_vr(ns);
	adjusted_vrpk = adjust_by_vr(S->vr + GENEROUS_K);

	result = adjusted_vr < adjusted_ns && adjusted_ns < adjusted_vrpk;

	if (s_debug_retry) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("is_ns_in_window,  V(R) %d < N(S) %d < V(R)+k %d, returns %d\n", S->vr, ns, S->vr + GENEROUS_K, result);
	}

	return (result);
}


/*------------------------------------------------------------------------------
 *
 * Name:	send_srej_frames
 * 
 * Purpose:	Ask for a resend of I frames with specified sequence numbers.
 *
 * Inputs:	resend		- Array of N(S) values for missing I frames.
 *
 *		count		- Number of items in array.
 *
 *		allow_f1	- When true, set F=1 when asking for V(R).
 *
 *					X.25 section 2.4.6.4 b) 3) says F should be set to 0
 *						when receiving I frame out of sequence.
 *
 *					X.25 sections 2.4.6.11 & 2.3.5.2.2 say set F to 1 when 
 *						responding to command with P=1.  (our enquiry_response function). 
 *
 * Version 1.5:	The X.25 protocol spec allows additional sequence numbers in one frame
 *		by using the INFO part.
 *		By default that feature is off but can be negotiated with XID.
 *		We should be able to use this between two direwolf stations while
 *		maintaining compatibility with the original AX.25 v2.2.
 *
 *------------------------------------------------------------------------------*/


static void send_srej_frames (ax25_dlsm_t *S, int *resend, int count, int allow_f1)
{
	int f;			// Set if we are ack-ing one before.
	int nr;
	cmdres_t cr = cr_res;	// SREJ is always response.
	int i;

	packet_t pp;

	if (count <= 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR, count=%d, %s line %d\n", count, __func__, __LINE__);
	  return;
	}

	if (s_debug_retry) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("%s line %d\n", __func__, __LINE__);
	  //dw_printf ("state=%d, count=%d, k=%d, V(R)=%d, SREJ exception=%d\n", S->state, count, S->k_maxframe, S->vr, selective_reject_exception(S));
	  dw_printf ("state=%d, count=%d, k=%d, V(R)=%d\n", S->state, count, S->k_maxframe, S->vr);

	  dw_printf ("resend[]=");
	  for (i = 0; i < count; i++) {
	    dw_printf (" %d", resend[i]);
	  }
	  dw_printf ("\n");

	  dw_printf ("rxdata_by_ns[]=");
	  for (i = 0; i < 128; i++) {
	    if (S->rxdata_by_ns[i] != NULL) {
	      dw_printf (" %d", i);
	    }
	  }
	  dw_printf ("\n");
	}


// Something is wrong!  We ask for more than the window size.

	if (count > S->k_maxframe) {

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR - Extreme number of SREJ, %s line %d\n", __func__, __LINE__);
	  dw_printf ("state=%d, count=%d, k=%d, V(R)=%d\n", S->state, count, S->k_maxframe, S->vr);

	  dw_printf ("resend[]=");
	  for (i = 0; i < count; i++) {
	    dw_printf (" %d", resend[i]);
	  }
	  dw_printf ("\n");

	  dw_printf ("rxdata_by_ns[]=");
	  for (i = 0; i < 128; i++) {
	    if (S->rxdata_by_ns[i] != NULL) {
	      dw_printf (" %d", i);
	    }
	  }
	  dw_printf ("\n");
	}

// Multi-SREJ - Use info part for additional sequence number(s) instead of sending separate SREJ for each.

	if (S->srej_enable == srej_multi && count > 1) {

	  unsigned char info[128];
	  int info_len = 0;

	  for (i = 1; i < count; i++) {		// skip first one

	    if (resend[i] < 0 || resend[i] >= S->modulo) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("INTERNAL ERROR, additional nr=%d, modulo=%d, %s line %d\n", resend[i], S->modulo, __func__, __LINE__);
	    }

	    // There is also a form to specify a range but I don't
	    // think it is worth the effort to generate it.  Maybe later.

	    if (S->modulo == 8) {
	      info[info_len++] = resend[i] << 5;
	    }
	    else {
	      info[info_len++] = resend[i] << 1;
	    }
	  }

	  f = 0;
	  nr = resend[0];
	  f = allow_f1 && (nr == S->vr);
					// Possibly set if we are asking for the next after
					// the last one received in contiguous order.

					// This could only apply to the first in
					// the list so this would not go in the loop.

	  if (f) {			// In this case the other end is being
					// informed of my V(R) so no additional
					// RR etc. is needed.
					// TODO:  Need to think about this.
	    S->acknowledge_pending = 0;
	  }

	  if (nr < 0 || nr >= S->modulo) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("INTERNAL ERROR, nr=%d, modulo=%d, %s line %d\n", nr, S->modulo, __func__, __LINE__);
	    nr = AX25MODULO(nr, S->modulo, __FILE__, __func__, __LINE__);
	  }

	  pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_SREJ, S->modulo, nr, f, info, info_len);
	  lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	  return;
	}

// Multi-SREJ not enabled.  Send separate SREJ for each desired sequence number.

	for (i = 0; i < count; i++) {

	  nr = resend[i];
	  f = allow_f1 && (nr == S->vr);
					// Possibly set if we are asking for the next after
					// the last one received in contiguous order.

	  if (f) {
					// In this case the other end is being
					// informed of my V(R) so no additional 
					// RR etc. is needed.
	    S->acknowledge_pending = 0;
	  }

	  if (nr < 0 || nr >= S->modulo) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("INTERNAL ERROR, nr=%d, modulo=%d, %s line %d\n", nr, S->modulo, __func__, __LINE__);
	    nr = AX25MODULO(nr, S->modulo, __FILE__, __func__, __LINE__);
	  }

	  pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_SREJ, S->modulo, nr, f, NULL, 0);
	  lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	}

} /* end send_srej_frames */



/*------------------------------------------------------------------------------
 *
 * Name:	rr_rnr_frame
 * 
 * Purpose:	Process RR or RNR Frame.
 *		Processing is the essentially the same so they are handled by a single function.
 *
 * Inputs:	S	- Data Link State Machine.
 *		ready	- True for RR, false for RNR
 *		cr	- Is this command or response?
 *		pf	- Poll/Final bit.
 *		nr	- N(R) from the frame.
 *
 * Description:	4.3.2.1. Receive Ready (RR) Command and Response
 *
 *		Receive Ready accomplishes the following:
 *		a) indicates that the sender of the RR is now able to receive more I frames;
 *		b) acknowledges properly received I frames up to, and including N(R)-1;and
 *		c) clears a previously-set busy condition created by an RNR command having been sent.
 *		The status of the TNC at the other end of the link can be requested by sending an RR command frame with the
 *		P-bit set to one.
 *
 *		4.3.2.2. Receive Not Ready (RNR) Command and Response
 *
 *		Receive Not Ready indicates to the sender of I frames that the receiving TNC is temporarily busy and cannot
 *		accept any more I frames. Frames up to N(R)-1 are acknowledged. Frames N(R) and above that may have been
 *		transmitted are discarded and must be retransmitted when the busy condition clears.
 *		The RNR condition is cleared by the sending of a UA, RR, REJ or SABM(E) frame.
 *		The status of the TNC at the other end of the link is requested by sending an RNR command frame with the
 *		P bit set to one.
 *
 *------------------------------------------------------------------------------*/


static void rr_rnr_frame (ax25_dlsm_t *S, int ready, cmdres_t cr, int pf, int nr)
{

	// dw_printf ("rr_rnr_frame (ready=%d, cr=%d, pf=%d, nr=%d) state=%d\n", ready, cr, pf, nr, S->state);

	switch (S->state) {

	  case 	state_0_disconnected:

	    if (cr == cr_cmd) {
	      cmdres_t r = cr_res;	// DM response with F taken from P.
	      int f = pf;
	      int nopid = 0;		// PID only for I and UI frames.
	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, r, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    }
	    break;

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:

	    // do nothing.
	    break;

	  case 	state_2_awaiting_release:

	    // Logic from flow chart for "I, RR, RNR, REJ, SREJ commands."

	    if (cr == cr_cmd && pf == 1) {
	      cmdres_t r = cr_res;	// DM response with F = 1.
	      int f = 1;
	      int nopid = 0;		// PID applies only for I and UI frames.

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, r, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    }

// Erratum: We have a disagreement here between original and 2006 version.
// RR, RNR, REJ, SREJ responses would fall under "all other primitives."
// In the original, we simply ignore it and stay in state 2.
// The 2006 version, page 94,  says go into "1 awaiting connection" state.
// That makes no sense to me.

	    break;

	  case 	state_3_connected:

	    S->peer_receiver_busy = ! ready;

// Erratum: the flow charts have unconditional check_need_for_response here.
// I don't recall exactly why I added the extra test for command and P=1.
// It might have been because we were reporting error A for response with F=1.
// Other than avoiding that error message, this is functionally equivalent.

	    if (cr == cr_cmd && pf) {
	      check_need_for_response (S, ready ? frame_type_S_RR : frame_type_S_RNR, cr, pf);
	    }

	    if (is_good_nr(S,nr)) {
	      // dw_printf ("rr_rnr_frame (), line %d, state=%d, good nr=%d, calling check_i_frame_ackd\n", __LINE__, S->state, nr);

	      check_i_frame_ackd (S, nr);
	    }
	    else {
	      if (s_debug_retry) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("rr_rnr_frame (), line %d, state=%d, bad nr, calling nr_error_recovery\n", __LINE__, S->state);
	      }

	      nr_error_recovery (S);
	      // My enhancement.  Original always sent SABM and went to state 1.
	      enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	    }

	    break;

	  case 	state_4_timer_recovery:

	    S->peer_receiver_busy = ! ready;

	    if (cr == cr_res && pf == 1) {

// RR/RNR Response with F==1.

	      if (s_debug_retry) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("rr_rnr_frame (), Response, f=%d, line %d, state=%d, good nr, calling check_i_frame_ackd\n", pf, __LINE__, S->state);
	      }

	      STOP_T1;
	      select_t1_value(S);

	      if (is_good_nr(S,nr)) {

	        SET_VA(nr);
	        if (S->vs == S->va) {		// all caught up with ack from other guy.
	          START_T3;
	          SET_RC(0);			// My enhancement.  See Erratum note in select_t1_value.
	          enter_new_state (S, state_3_connected, __func__, __LINE__);
	        }
	        else {
	          invoke_retransmission (S, nr);
// my addition

// Erratum: We sent I frame(s) and want to timeout if no ack comes back.
// We also sent N(R) so no need for extra RR at the end only for that.

	          STOP_T3;
	          START_T1;
	          S->acknowledge_pending = 0;

// end of my addition
	        }
	      }
	      else {
	        nr_error_recovery (S);

// Erratum: Another case of my enhancement.
// The flow charts go into state 1 after nr_error_recovery.
// I use state 5 instead if we were oprating in extended (modulo 128) mode.

	        enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	      }
	    }
	    else {

// RR/RNR command, either P value.
// RR/RNR response, F==0

	      if (cr == cr_cmd && pf == 1) {
	        int f = 1;
	        enquiry_response (S, ready ? frame_type_S_RR : frame_type_S_RNR, f);
	      }

	      if (is_good_nr(S,nr)) {

	        SET_VA(nr);

// Erratum: v1.5 - my addition.
// I noticed that we sometimes got stuck in state 4 and rc crept up slowly even though
// we received RR frames with N(R) values indicating that the other side received everything
// that we sent.  Eventually rc could reach the limit and we would get an error.
// If we are in state 4, and other guy ack'ed last I frame we sent, transition to state 3.
// The same thing was done for receiving I frames after check_i_frame_ackd.

// Thought: Could we simply call check_i_frame_ackd, for consistency, rather than only setting V(A)?

	        if (cr == cr_res && pf == 0) {

	          if (S->vs == S->va) {		// all caught up with ack from other guy.
	            STOP_T1;
	            select_t1_value (S);
	            START_T3;
	            SET_RC(0);
	            enter_new_state (S, state_3_connected, __func__, __LINE__);
	          }
	        }
	      }
	      else {
	        nr_error_recovery (S);
	        enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	      }
	    }
	    break;
	}

} /* end rr_rnr_frame */



/*------------------------------------------------------------------------------
 *
 * Name:	rej_frame
 * 
 * Purpose:	Process REJ Frame.
 *
 * Inputs:	S	- Data Link State Machine.
 *		cr	- Is this command or response?
 *		pf	- Poll/Final bit.
 *		nr	- N(R) from the frame.
 *
 * Description:	4.3.2.2. Receive Not Ready (RNR) Command and Response
 *
 *		... The RNR condition is cleared by the sending of a UA, RR, REJ or SABM(E) frame. ...
 *
 *
 *		4.3.2.3. Reject (REJ) Command and Response
 *
 *		The reject frame requests retransmission of I frames starting with N(R). Any frames sent with a sequence
 *		number of N(R)-1 or less are acknowledged. Additional I frames which may exist may be appended to the
 *		retransmission of the N(R) frame.
 *		Only one reject frame condition is allowed in each direction at a time. The reject condition is cleared by the
 *		proper reception of I frames up to the I frame that caused the reject condition to be initiated.
 *		The status of the TNC at the other end of the link is requested by sending a REJ command frame with the P bit
 *		set to one.
 *
 *		4.4.3. Reject (REJ) Recovery
 *
 *		The REJ frame requests a retransmission of I frames following the detection of a N(S) sequence error. Only
 *		one outstanding "sent REJ" condition is allowed at a time. This condition is cleared when the requested I frame
 *		has been received.
 *		A TNC receiving the REJ command clears the condition by resending all outstanding I frames (up to the
 *		window size), starting with the frame indicated in N(R) of the REJ frame.
 *
 *
 *		4.4.5.1. T1 Timer Recovery
 *
 *		If a transmission error causes a TNC to fail to receive (or to receive and discard) a single I frame, or the last I
 *		frame in a sequence of I frames, then the TNC does not detect a send-sequence-number error and consequently
 *		does not transmit a REJ/SREJ. The TNC that transmitted the unacknowledged I frame(s) following the completion
 *		of timeout period T1, takes appropriate recovery action to determine when I frame retransmission as described
 *		in Section 6.4.10 should begin. This condition is cleared by the reception of an acknowledgement for the sent
 *		frame(s), or by the link being reset.
 *
 *		6.2. Poll/Final (P/F) Bit Procedures
 *
 *		The response frame returned by a TNC depends on the previous command received, as described in the
 *		following paragraphs.
 *		...
 *
 *		The next response frame returned to an I frame with the P bit set to "1", received during the information5
 *		transfer state, is an RR, RNR or REJ response with the F bit set to "1".
 *
 *		The next response frame returned to a supervisory command frame with the P bit set to "1", received during
 *		the information transfer state, is an RR, RNR or REJ response frame with the F bit set to "1".
 *		...
 *
 *		The P bit is used in conjunction with the timeout recovery condition discussed in Section 4.5.5.
 *		When not used, the P/F bit is set to "0".
 *
 *		6.4.4.1. Implicit Reject (REJ)
 *
 *		When an I frame is received with a correct FCS but its send sequence number N(S) does not match the current
 *		receiver's receive state variable, the frame is discarded. A REJ frame is sent with a receive sequence number
 *		equal to one higher than the last correctly received I frame if an uncleared N(S) sequence error condition has not
 *		been previously established. The received state variable and poll bit of the discarded frame is checked and acted
 *		upon, if necessary.
 *		This mode requires no frame queueing and frame resequencing at the receiver. However, because the mode
 *		requires transmission of frames that may not be in error, its throughput is not as high as selective reject. This
 *		mode is ineffective on systems with long round-trip delays and high data rates.
 *
 *		6.4.7. Receiving REJ
 *
 *		After receiving a REJ frame, the transmitting TNC sets its send state variable to the same value as the REJ
 *		frame's received sequence number in the control field. The TNC then retransmits any I frame(s) outstanding at
 *		the next available opportunity in accordance with the following:
 *
 *		a) If the TNC is not transmitting at the time and the channel is open, the TNC may begin retransmission of the
 *		I frame(s) immediately.
 *		b) If the TNC is operating on a full-duplex channel transmitting a UI or S frame when it receives a REJ frame,
 *		it may finish sending the UI or S frame and then retransmit the I frame(s).
 *		c) If the TNC is operating in a full-duplex channel transmitting another I frame when it receives a REJ frame,
 *		it may abort the I frame it was sending and start retransmission of the requested I frames immediately.
 *		d) The TNC may send just the one I frame outstanding, or it may send more than the one indicated if more I
 *		frames followed the first unacknowledged frame, provided that the total to be sent does not exceed the flowcontrol
 *		window (k frames).
 *		If the TNC receives a REJ frame with the poll bit set, it responds with either an RR or RNR frame with the
 *		final bit set before retransmitting the outstanding I frame(s).
 *
 *------------------------------------------------------------------------------*/

static void rej_frame (ax25_dlsm_t *S, cmdres_t cr, int pf, int nr)
{

	switch (S->state) {

	  case 	state_0_disconnected:

	    // states 0 and 2 are very similar with one tiny little difference.

	    if (cr == cr_cmd) {
	      cmdres_t r = cr_res;	// DM response with F taken from P.
	      int f = pf;
	      int nopid = 0;		// PID is only for I and UI.

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, r, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    }
	    break;

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:
	    // Do nothing.
	    break;

	  case 	state_2_awaiting_release:

	    if (cr == cr_cmd && pf == 1) {
	      cmdres_t r = cr_res;	// DM response with F = 1.
	      int f = 1;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, r, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    }

// Erratum: We have a disagreement here between original and 2006 version.
// RR, RNR, REJ, SREJ responses would fall under "all other primitives."
// In the original, we simply ignore it and stay in state 2.
// The 2006 version, page 94,  says go into "1 awaiting connection" state.
// That makes no sense to me.

	    break;

	  case 	state_3_connected:

	    S->peer_receiver_busy = 0;

// Receipt of the REJ "frame" (either command or response) causes us to
// start resending I frames at the specified number.

// I think there are 3 possibilities here:
// Response is used when incoming I frame processing detects one is missing.
// In this case, F is copied from the I frame P bit.  I don't think we care here.
// Command with P=1 is used during timeout recovery.
// The rule is that we are supposed to send a response with F=1 for I, RR, RNR, or REJ with P=1.

	    check_need_for_response (S, frame_type_S_REJ, cr, pf);

	    if (is_good_nr(S,nr)) {
	      SET_VA(nr);
	      STOP_T1;
	      STOP_T3;
	      select_t1_value(S);

	      invoke_retransmission (S, nr);

// my addition
// Erratum: We sent I frame(s) and want to timeout if no ack comes back.
// We also sent N(R) so no need for extra RR at the end only for that.

// We ran into cases where I frame(s) would be resent but lost.
// T1 was stopped so we just waited and waited and waited instead of trying again.
// I added the following after each invoke_retransmission.
// This seems clearer than hiding the timer stuff inside of it.

	      // T3 is already stopped.
	      START_T1;
	      S->acknowledge_pending = 0;
	    }
	    else {
	      nr_error_recovery (S);
	      enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	    }
	    break;

	  case 	state_4_timer_recovery:

	    S->peer_receiver_busy = 0;

	    if (cr == cr_res && pf == 1) {

	      STOP_T1;
	      select_t1_value(S);

	      if (is_good_nr(S,nr)) {

	        SET_VA(nr);
	        if (S->vs == S->va) {
	          START_T3;
	          SET_RC(0);			// My enhancement.  See Erratum note in select_t1_value.
	          enter_new_state (S, state_3_connected, __func__, __LINE__);
	        }
	        else {
	          invoke_retransmission (S, nr);
// my addition.
// Erratum: We sent I frame(s) and want to timeout if no ack comes back.
// We also sent N(R) so no need for extra RR at the end only for that.

	          STOP_T3;
	          START_T1;
	          S->acknowledge_pending = 0;
	        }
	      }
	      else {
	        nr_error_recovery (S);
	        enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	      }
	    }
	    else {
	      if (cr == cr_cmd && pf == 1) {
	        int f = 1;
	        enquiry_response (S, frame_type_S_REJ, f);
	      }

	      if (is_good_nr(S,nr)) {

	        SET_VA(nr);

	        if (S->vs != S->va) {
	          // Observation:  RR/RNR state 4 is identical but it doesn't have invoke_retransmission here.
	          invoke_retransmission (S, nr);
// my addition.
// Erratum: We sent I frame(s) and want to timeout if no ack comes back.
// We also sent N(R) so no need for extra RR at the end only for that.

	          STOP_T3;
	          START_T1;
	          S->acknowledge_pending = 0;
	        }

	      }
	      else {
	        nr_error_recovery (S);
	        enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	      }
	    }
	    break;
	}

} /* end rej_frame */


/*------------------------------------------------------------------------------
 *
 * Name:	srej_frame
 * 
 * Purpose:	Process SREJ Response.
 *
 * Inputs:	S	- Data Link State Machine.
 *		cr	- Is this command or response?
 *		f	- Final bit.   When set, it is ack-ing up thru N(R)-1
 *		nr	- N(R) from the frame.  Peer has asked for a resend of I frame with this N(S).
 *		info	- Information field, used only for Multi-SREJ
 *		info_len - Information field length, bytes.
 *
 * Description:	4.3.2.4. Selective Reject (SREJ) Command and Response
 *
 *		The selective reject, SREJ, frame is used by the receiving TNC to request retransmission of the single I frame
 *		numbered N(R). If the P/F bit in the SREJ frame is set to "1", then I frames numbered up to N(R)-1 inclusive are
 *		considered as acknowledged. However, if the P/F bit in the SREJ frame is set to "0", then the N(R) of the SREJ
 *		frame does not indicate acknowledgement of I frames.
 *
 *		Each SREJ exception condition is cleared (reset) upon receipt of the I frame with an N(S) equal to the N(R)
 *		of the SREJ frame.
 *
 *		A receiving TNC may transmit one or more SREJ frames, each containing a different N(R) with the P bit set
 *		to "0", before one or more earlier SREJ exception conditions have been cleared. However, a SREJ is not
 *		transmitted if an earlier REJ exception condition has not been cleared as indicated in Section 4.5.4. (To do so
 *		would request retransmission of an I frame that would be retransmitted by the REJ operation.) Likewise, a REJ
 *		frame is not transmitted if one or more earlier SREJ exception conditions have not been cleared as indicated in
 *		Section 4.5.4.
 *
 *		I frames transmitted following the I frame indicated by the SREJ frame are not retransmitted as the result of
 *		receiving a SREJ frame. Additional I frames awaiting initial transmission may be transmitted following the
 *		retransmission of the specific I frame requested by the SREJ frame.
 *
 * Erratum:	The section above always refers to SREJ "frames."  There doesn't seem to be any clue about when
 *		command vs. response would be used.  When we look in the flow charts, we see that we generate only
 *		responses but the code is there to process command and response slightly differently.
 *
 * Description:	4.4.4. Selective Reject (SREJ) Recovery
 *
 *		The SREJ command/response initiates more-efficient error recovery by requesting the retransmission of a
 *		single I frame following the detection of a sequence error. This is an advancement over the earlier versions in
 *		which the requested I frame was retransmitted together with all additional I frames subsequently transmitted and
 *		successfully received.
 *
 *		When a TNC sends one or more SREJ commands, each with the P bit set to "0" or "1", or one or more SREJ
 *		responses, each with the F bit set to "0", and the "sent SREJ" conditions are not cleared when the TNC is ready
 *		to issue the next response frame with the F bit set to "1", the TNC sends a SREJ response with the F bit set to "1",
 *		with the same N(R) as the oldest unresolved SREJ frame.
 *
 *		Because an I or S format frame with the F bit set to "1" can cause checkpoint retransmission, a TNC does not
 *		send SREJ frames until it receives at least one in-sequence I frame, or it perceives by timeout that the checkpoint
 *		retransmission will not be initiated at the remote TNC.
 *
 *		With respect to each direction of transmission on the data link, one or more "sent SREJ" exception conditions
 *		from a TNC to another TNC may be established at a time. A "sent SREJ" exception condition is cleared when
 *		the requested I frame is received.
 *
 *		The SREJ frame may be repeated when a TNC perceives by timeout that a requested I frame will not be
 *		received, because either the requested I frame or the SREJ frame was in error or lost.
 *
 *		When appropriate, a TNC receiving one or more SREJ frames initiates retransmission of the individual I
 *		frames indicated by the N(R) contained in each SREJ frame. After having retransmitted the above frames, new
 *		I frames are transmitted later if they become available.
 *
 *		When a TNC receives and acts on one or more SREJ commands, each with the P bit set to "0", or an SREJ
 *		command with the P bit set to "1", or one or more SREJ responses each with the F bit set to "0", it disables any
 *		action on the next SREJ response frame if that SREJ frame has the F bit set to "1" and has the same N(R) (i.e.,
 *		the same value and the same numbering cycle) as a previously actioned SREJ frame, and if the resultant
 *		retransmission was made following the transmission of the P bit set to a "1".
 *		When the SREJ mechanism is used, the receiving station retains correctly-received I frames and delivers
 *		them to the higher layer in sequence number order.
 *
 *
 *		6.4.4.2. Selective Reject (SREJ)
 *
 *		When an I frame is received with a correct FCS but its send sequence number N(S) does not match the current
 *		receiver's receive state variable, the frame is retained. SREJ frames are sent with a receive sequence number
 *		equal to the value N(R) of the missing frame, and P=1 if an uncleared SREJ condition has not been previously
 *		established. If an SREJ condition is already pending, an SREJ will be sent with P=0. The received state variable
 *		and poll bit of the received frame are checked and acted upon, if necessary.
 *
 *		This mode requires frame queueing and frame resequencing at the receiver. The holding of frames can
 *		consume precious buffer space, especially if the user device has limited memory available and several active
 *		links are operational.
 *
 *
 *		6.4.4.3. Selective Reject-Reject (SREJ/REJ)
 *
 *		When an I frame is received with a correct FCS but its send sequence number N(S) does not match the current
 *		receiver's receive state variable, and if N(S) indicates 2 or more frames are missing, a REJ frame is transmitted.
 *		All subsequently received frames are discarded until the lost frame is correctly received. If only one frame is
 *		missing, a SREJ frame is sent with a receive sequence number equal to the value N(R) of the missing frame. The
 *		received state variable and poll bit of the received frame are checked and acted upon. If another frame error
 *		occurs prior to recovery of the SREJ condition, the receiver saves all frames received after the first errored frame
 *		and discards frames received after the second errored frame until the first errored frame is recovered. Then, a
 *		REJ is issued to recover the second errored frame and all subsequent discarded frames.
 *
 * X.25:	States that SREJ is only response.  I'm following that and it simplifies matters.
 * 
 * X.25 2.4.6.6.1 & 2.4.6.6.2 make a distinction between F being 0 or 1 besides copying N(R) into V(A).
 *		They talk about sending a poll under some conditions.
 *		We don't do that here.  It seems to work reliably so leave well enough alone.
 *
 *------------------------------------------------------------------------------*/

static int resend_for_srej (ax25_dlsm_t *S, int nr, unsigned char *info, int info_len);

static void srej_frame (ax25_dlsm_t *S, cmdres_t cr, int f, int nr, unsigned char *info, int info_len)
{

	switch (S->state) {

	  case 	state_0_disconnected:

	    break;

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:
	    // Do nothing.

	    // Erratum: The original spec said stay in same state.  (Seems correct.)
	    // 2006 revision shows state 5 transitioning into 1.  I think that is wrong.
	    // probably a cut-n-paste from state 1 to 5 and that part not updated.
	    break;

	  case 	state_2_awaiting_release:

	    // Erratum: Flow chart says send DM(F=1) for "I, RR, RNR, REJ, SREJ commands" and P=1.
	    // It is wrong for two reasons.
	    // If SREJ was a command, the P flag has a different meaning than the other Supervisory commands.
	    // It means ack reception of frames up thru N(R)-1;  it is not a poll to get a response.

	    // Based on X.25, I don't think SREJ can be a command.
	    // It should say, "I, RR, RNR, REJ commands"

	    // Erratum: We have a disagreement here between original and 2006 version.
	    // RR, RNR, REJ, SREJ responses would fall under "all other primitives."
	    // In the original, we simply ignore it and stay in state 2.
	    // The 2006 version, page 94,  says go into "1 awaiting connection" state.
	    // That makes no sense to me.

	    break;

	  case 	state_3_connected:

	    S->peer_receiver_busy = 0;

	    // Erratum:  Flow chart has "check need for response here."

	    // check_need_for_response() does the following:
	    // - for command & P=1, send RR or RNR.
	    // - for response & F=1, error A.

	   // SREJ can only be a response.  We don't want to produce an error when F=1.

	    if (is_good_nr(S,nr)) {

	      if (f) {
	        SET_VA(nr);
	      }
	      STOP_T1;
	      START_T3;
	      select_t1_value(S);


	      int num_resent = resend_for_srej (S, nr, info, info_len);
	      if (num_resent) {

// my addition
// Erratum: We sent I frame(s) and want to timeout if no ack comes back.
// We also sent N(R), from V(R), so no need for extra RR at the end only for that.

// We would sometimes end up in a situation where T1 was stopped on
// both ends and everyone would wait for the other guy to timeout and do something.
// My solution was to Start T1 after every place we send an I frame if not already there.

	        STOP_T3;
	        START_T1;
	        S->acknowledge_pending = 0;
	      }
	      // keep same state.
	    }
	    else {
	      nr_error_recovery (S);
	      // Erratum?  Flow chart shows state 1 but that would not be appropriate if modulo is 128.
	      enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	    }
	    break;

	  case 	state_4_timer_recovery:

	    if (s_debug_timers) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("state 4 timer recovery, %s %d  nr=%d, f=%d\n", __func__, __LINE__, nr, f);
	    }

	    S->peer_receiver_busy = 0;

	    // Erratum:  Original Flow chart has "check need for response here."
	    // The 2006 version correctly removed it.

	    // check_need_for_response() does the following:
	    // - for command & P=1, send RR or RNR.
	    // - for response & F=1, error A.

	    // SREJ can only be a response.  We don't want to produce an error when F=1.
	

	    // The flow chart splits into two paths for command/response with a lot of duplication.
	    // Command path has been omitted because SREJ can only be response.

	    STOP_T1;
	    select_t1_value(S);

	    if (is_good_nr(S,nr)) {

	      if (f) {				// f=1 means ack up thru previous sequence.
						// Erratum:  2006 version tests "P".  Original has "F."
	        SET_VA(nr);

	        if (s_debug_timers) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("state 4 timer recovery, %s %d    set v(a)= %d\n", __func__, __LINE__, S->va);
	        }
	      }

	      if (S->vs == S->va) {		// ACKs all caught up.  Back to state 3.

			// Erratum:  I think this is unreachable.
			// If the other side is asking for I frame with sequence X, it must have
			// received X+1 or later.  That means my V(S) must be X+2 or greater.
			// So, I don't think we can ever have V(S) == V(A) here.
			// If we were to remove the 'if' test and true part, case 4 would then
			// be exactly the same as state 4.  We need to rely on RR to get us
			// back to state 3.

	        START_T3;
	        SET_RC(0);			// My enhancement.  See Erratum note in select_t1_value.
	        enter_new_state (S, state_3_connected, __func__, __LINE__);

	        // text_color_set(DW_COLOR_ERROR);
	        // dw_printf ("state 4 timer recovery, go to state 3 \n");
	      }
	      else {

// Erratum: Difference between two AX.25 revisions.

#if 1	// This is from the original protocol spec.
	// Resend I frame with N(S) equal to the N(R) in the SREJ.

	        //text_color_set(DW_COLOR_ERROR);
	        //dw_printf ("state 4 timer recovery, send requested frame(s) \n");

	        int num_resent = resend_for_srej (S, nr, info, info_len);
	        if (num_resent) {
// my addition
// Erratum: We sent I frame(s) and want to timeout if no ack comes back.
// We also sent N(R), from V(R), so no need for extra RR at the end only for that.

// We would sometimes end up in a situation where T1 was stopped on
// both ends and everyone would wait for the other guy to timeout and do something.
// My solution was to Start T1 after every place we send an I frame if not already there.

	          STOP_T3;
	          START_T1;
	          S->acknowledge_pending = 0;
	        }
#else	// Erratum!  This is from the 2006 revision.
	// We should resend only the single requested I frame.
	// I think there was a cut-n-paste from the REJ flow chart and this particular place did not get changed.
	
	        invoke_retransmission(S);
#endif
	      }
	    }
	    else {
	      nr_error_recovery (S);
	      enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	    }
	    break;
	}

} /* end srej_frame */

/*------------------------------------------------------------------------------
 *
 * Name:	resend_for_srej
 *
 * Purpose:	Resend the I frame(s) specified in SREJ response.
 *
 * Inputs:	S	- Data Link State Machine.
 *		nr	- N(R) from the frame.  Peer has asked for a resend of I frame with this N(S).
 *		info	- Information field, might contain additional sequence numbers for Multi-SREJ.
 *		info_len - Information field length, bytes.
 *
 * Returns:	Number of frames sent.  Should be at least one.
 *
 * Description:	Simply resend requested frame(s).
 *		The calling context will worry about the F bit and other state stuff.
 *
 *------------------------------------------------------------------------------*/

static int resend_for_srej (ax25_dlsm_t *S, int nr, unsigned char *info, int info_len)
{
	cmdres_t cr = cr_cmd;
	int i_frame_nr = S->vr;
	int i_frame_ns = nr;
	int p = 0;
	int num_resent = 0;

	// Resend I frame with N(S) equal to the N(R) in the SREJ.
	// Additional sequence numbers can be in optional information part.

	cdata_t *txdata = S->txdata_by_ns[i_frame_ns];

	if (txdata != NULL) {
	  packet_t pp = ax25_i_frame (S->addrs, S->num_addr, cr, S->modulo, i_frame_nr, i_frame_ns, p, txdata->pid, (unsigned char *)(txdata->data), txdata->len);
	  // dw_printf ("calling lm_data_request for I frame, %s line %d\n", __func__, __LINE__);
	  lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	  num_resent++;
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Stream %d: INTERNAL ERROR for SREJ.  I frame for N(S)=%d is not available.\n", S->stream_id, i_frame_ns);
	}

// Multi-SREJ if there is an information part.

	int j;
	for (j = 0; j < info_len; j++) {

		// We can have a single sequence number like this:
		//    	xxx00000	(mod 8)
		//	xxxxxxx0	(mod 128)
		// or we can have span (mod 128 only) like this, with the first and last:
		//	xxxxxxx1
		//	xxxxxxx1
		//
		// Note that the sequence number is shifted left by one
		// and if the LSB is set, there should be two adjacent bytes
		// with it set.

	  if (S->modulo == 8) {
	    i_frame_ns = (info[j] >> 5) & 0x07;	// no provision for span.
	  }
	  else {
	    i_frame_ns = (info[j] >> 1) & 0x7f;	// TODO: test LSB and possible loop here.
	  }

	  txdata = S->txdata_by_ns[i_frame_ns];
	  if (txdata != NULL) {
	    packet_t pp = ax25_i_frame (S->addrs, S->num_addr, cr, S->modulo, i_frame_nr, i_frame_ns, p, txdata->pid, (unsigned char *)(txdata->data), txdata->len);
	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    num_resent++;
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Stream %d: INTERNAL ERROR for Multi-SREJ.  I frame for N(S)=%d is not available.\n", S->stream_id, i_frame_ns);
	  }
	}
	return (num_resent);

} /* end resend_for_srej */




/*------------------------------------------------------------------------------
 *
 * Name:	sabm_e_frame
 * 
 * Purpose:	Process SABM or SABME Frame.
 *
 * Inputs:	S	- Data Link State Machine.
 *
 *		extended - True for SABME.  False for SABM.
 *
 *		p	- Poll bit.   TODO:  What does it mean in this case?
 *
 * Description: This is a request, from the other end, to establish a connection.
 *
 *		4.3.3.1. Set Asynchronous Balanced Mode (SABM) Command
 *
 *		The SABM command places two Terminal Node Comtrollers (TNC) in the asynchronous balanced mode
 *		(modulo 8). This a balanced mode of operation in which both devices are treated as equals or peers.
 *
 *		Information fields are not allowed in SABM commands. Any outstanding I frames left when the SABM
 *		command is issued remain unacknowledged.
 *
 *		The TNC confirms reception and acceptance of a SABM command by sending a UA response frame at the
 *		earliest opportunity. If the TNC is not capable of accepting a SABM command, it responds with a DM frame if
 *		possible.
 *
 *		4.3.3.2. Set Asynchronous Balanced Mode Extended (SABME) Command
 *
 *		The SABME command places two TNCs in the asynchronous balanced mode extended (modulo 128). This
 *		is a balanced mode of operation in which both devices are treated as equals or peers.
 *		Information fields are not allowed in SABME commands. Any outstanding I frames left when the SABME
 *		command is issued remains unacknowledged.
 *
 *		The TNC confirms reception and acceptance of a SABME command by sending a UA response frame at the
 *		earliest opportunity. If the TNC is not capable of accepting a SABME command, it responds with a DM frame.
 *
 *		A TNC that uses a version of AX.25 prior to v2.2 responds with a FRMR. ** (see note below)
 *
 *
 * Note:	The KPC-3+, which does not appear to support v2.2, responds with a DM.
 *		The 2.0 spec, section 2.3.4.3.5, states, "While a DXE is in the disconnected mode, it will respond
 *		to any command other than a SABM or UI frame with a DM response with the P/F bit set to 1."
 *		I think it is a bug in the KPC but I can see how someone might implement it that way.
 *		However, another place says FRMR is sent for any unrecognized frame type.  That would seem to take priority.
 *
 *------------------------------------------------------------------------------*/

static void sabm_e_frame (ax25_dlsm_t *S, int extended, int p)
{

	switch (S->state) {

	  case 	state_0_disconnected:

	    // Flow chart has decision: "Able to establish?"
	    // I think this means, are we willing to accept connection requests?
	    // We are always willing to accept connections.
	    // Of course, we wouldn't get this far if local callsigns were not "registered."

	    if (extended) {
	      set_version_2_2 (S);
	    }
	    else {
	      set_version_2_0 (S);
	    }

	    cmdres_t res = cr_res;
	    int f = p;			// I don't understand the purpose of "P" in SABM/SABME
					// but we dutifully copy it into "F" for the UA response.
	    int nopid = 0;		// PID is only for I and UI.

	    packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_UA, f, nopid, NULL, 0);
	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	    clear_exception_conditions (S);

	    SET_VS(0);
	    SET_VA(0);
	    SET_VR(0);

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Stream %d: Connected to %s.  (%s)\n", S->stream_id, S->addrs[PEERCALL], extended ? "v2.2" : "v2.0");

	    // dl connect indication - inform the client app.
	    int incoming = 1;
	    server_link_established (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], incoming);

	    INIT_T1V_SRT;

	    START_T3;
	    SET_RC(0);			// My enhancement.  See Erratum note in select_t1_value.
	    enter_new_state (S, state_3_connected, __func__, __LINE__);
	    break;

	  case 	state_1_awaiting_connection:

	    // Don't combine with state 5.  They are slightly different.

	    if (extended) {		// SABME - respond with DM, enter state 5.
	      cmdres_t res = cr_res;
	      int f = p;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	      enter_new_state (S, state_5_awaiting_v22_connection, __func__, __LINE__);
	    }
	    else {			// SABM - respond with UA.
	
	      // Erratum!  2006 version shows SAMBE twice for state 1.
	      // First one should be SABM in last page of Figure C4.2
	      // Original appears to be correct.

	      cmdres_t res = cr_res;
	      int f = p;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_UA, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	      // stay in state 1.
	    }
	    break;

	  case 	state_5_awaiting_v22_connection:

	    if (extended) {		// SABME - respond with UA
	      cmdres_t res = cr_res;
	      int f = p;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_UA, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	      // stay in state 5
	    }
	    else {			// SABM, respond with UA, enter state 1
	      cmdres_t res = cr_res;
	      int f = p;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_UA, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	      enter_new_state (S, state_1_awaiting_connection, __func__, __LINE__);
	    }
	    break;

	  case 	state_2_awaiting_release:

	    // Erratum! Flow charts don't list SABME for state 2.
	    // Probably just want to treat it the same as SABM here.

	    {
	      cmdres_t res = cr_res;
	      int f = p;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_0_HI, pp);	// expedited
	      // stay in state 2.
	    }
	    break;

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:

	    {
	      cmdres_t res = cr_res;
	      int f = p;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_UA, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	      // State 3 & 4 handling are the same except for this one difference.
	      if (S->state == state_4_timer_recovery) {
	        if (extended) {
	          set_version_2_2 (S);
	        }
	        else {
	          set_version_2_0 (S);
	        }
	      }

	      clear_exception_conditions (S);
	      if (s_debug_protocol_errors) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Stream %d: AX.25 Protocol Error F: Data Link reset; i.e. SABM(e) received in state %d.\n", S->stream_id, S->state);
	      }
	      if (S->vs != S->va) {
	        discard_i_queue (S);
	        // dl connect indication
	        int incoming = 1;
	        server_link_established (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], incoming);
	      }
	      STOP_T1;
	      START_T3;
	      SET_VS(0);
	      SET_VA(0);
	      SET_VR(0);
	      SET_RC(0);			// My enhancement.  See Erratum note in select_t1_value.
	      enter_new_state (S, state_3_connected, __func__, __LINE__);
	    }
	    break;
	}

} /* end sabm_e_frame */



/*------------------------------------------------------------------------------
 *
 * Name:	disc_frame
 * 
 * Purpose:	Process DISC command.
 *
 * Inputs:	S	- Data Link State Machine.
 *		p	- Poll bit.
 *
 * Description:	4.3.3.3. Disconnect (DISC) Command
 *
 *		The DISC command terminates a link session between two stations. An information field is not permitted in
 *		a DISC command frame.
 *
 *		Prior to acting on the DISC frame, the receiving TNC confirms acceptance of the DISC by issuing a UA
 *		response frame at its earliest opportunity. The TNC sending the DISC enters the disconnected state when it
 *		receives the UA response.
 *
 *		Any unacknowledged I frames left when this command is acted upon remain unacknowledged.
 *
 *
 *		6.3.4. Link Disconnection
 *
 *		While in the information-transfer state, either TNC may indicate a request to disconnect the link by transmitting
 *		a DISC command frame and starting timer T1.
 *
 *		After receiving a valid DISC command, the TNC sends a UA response frame and enters the disconnected
 *		state. After receiving a UA or DM response to a sent DISC command, the TNC cancels timer T1 and enters the
 *		disconnected state.
 *
 *		If a UA or DM response is not correctly received before T1 times out, the DISC frame is sent again and T1 is
 *		restarted. If this happens N2 times, the TNC enters the disconnected state.
 *
 *------------------------------------------------------------------------------*/

static void disc_frame (ax25_dlsm_t *S, int p)
{

	switch (S->state) {

	  case 	state_0_disconnected:
	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:

	    {
	      cmdres_t res = cr_res;
	      int f = p;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    }
	    // keep current state, 0, 1, or 5.
	    break;

	  case 	state_2_awaiting_release:

	    {
	      cmdres_t res = cr_res;
	      int f = p;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_UA, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_0_HI, pp);		// expedited
	    }
	    // keep current state, 2.
	    break;

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:

	    {
	      discard_i_queue (S);

	      cmdres_t res = cr_res;
	      int f = p;
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_UA, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	      // dl disconnect *indication*
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
	      server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);

	      STOP_T1;
	      STOP_T3;
	      enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    }
	    break;
	}

} /* end disc_frame */



/*------------------------------------------------------------------------------
 *
 * Name:	dm_frame
 * 
 * Purpose:	Process DM Response Frame.
 *
 * Inputs:	S	- Data Link State Machine.
 *		f	- Final bit.
 *
 * Description:	4.3.3.1. Set Asynchronous Balanced Mode (SABM) Command
 *
 *		The TNC confirms reception and acceptance of a SABM command by sending a UA response frame at the
 *		earliest opportunity. If the TNC is not capable of accepting a SABM command, it responds with a DM frame if
 *		possible.
 *
 *		The TNC confirms reception and acceptance of a SABME command by sending a UA response frame at the
 *		earliest opportunity. If the TNC is not capable of accepting a SABME command, it responds with a DM frame.
 *
 *		A TNC that uses a version of AX.25 prior to v2.2 responds with a FRMR.
 *		( I think the KPC-3+ has a bug - it replies with DM - WB2OSZ )
 *
 *		4.3.3.5. Disconnected Mode (DM) Response
 *
 *		The disconnected mode response is sent whenever a TNC receives a frame other than a SABM(E) or UI
 *		frame while in a disconnected mode. The disconnected mode response also indicates that the TNC cannot
 *		accept a connection at the moment. The DM response does not have an information field.
 *		Whenever a SABM(E) frame is received and it is determined that a connection is not possible, a DM frame is
 *		sent. This indicates that the called station cannot accept a connection at that time.
 *		While a TNC is in the disconnected mode, it responds to any command other than a SABM(E) or UI frame
 *		with a DM response with the P/F bit set to "1".
 *
 *		4.3.3.6. Unnumbered Information (UI) Frame
 *
 *		A received UI frame with the P bit set causes a response to be transmitted. This response is a DM frame when
 *		in the disconnected state, or an RR (or RNR, if appropriate) frame in the information transfer state.
 *
 *		6.3.1. AX.25 Link Connection Establishment
 *
 *		If the distant TNC receives a SABM command and cannot enter the indicated state, it sends a DM frame.
 *		When the originating TNC receives a DM response to its SABM(E) frame, it cancels its T1 timer and does
 *		not enter the information-transfer state.
 *
 *		6.3.4. Link Disconnection
 *
 *		After receiving a valid DISC command, the TNC sends a UA response frame and enters the disconnected
 *		state. After receiving a UA or DM response to a sent DISC command, the TNC cancels timer T1 and enters the
 *		disconnected state.
 *
 *		6.5. Resetting Procedure
 *
 *		If a DM response is received, the TNC enters the disconnected state and stops timer T1. If timer T1 expires
 *		before a UA or DM response frame is received, the SABM(E) is retransmitted and timer T1 restarted. If timer T1
 *		expires N2 times, the TNC enters the disconnected state. Any previously existing link conditions are cleared.
 *		Other commands or responses received by the TNC before completion of the reset procedure are discarded.
 *
 * Erratum:	The flow chart shows the same behavior for states 1 and 5.
 *		For state 5, I think we should treat DM the same as FRMR.
 *
 *------------------------------------------------------------------------------*/


static void dm_frame (ax25_dlsm_t *S, int f)
{
	switch (S->state) {

	  case 	state_0_disconnected:
	    // Do nothing.
	    break;

	  case 	state_1_awaiting_connection:

	    if (f == 1) {
	      discard_i_queue (S);
	      // dl disconnect *indication*
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
	      server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);
	      STOP_T1;
	      enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    }
	    else {
	      // keep current state.
	    }
	    break;

	  case 	state_2_awaiting_release:

	    if (f == 1) {

	      // Erratum! Original flow chart, page 91, shows DL-CONNECT confirm.
	      // It should clearly be DISconnect rather than Connect.

	      // 2006 has DISCONNECT *Indication*.
	      // Should it be indication or confirm?  Not sure.

	      // dl disconnect *confirm*
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
	      server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);
	      STOP_T1;
	      enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    }
	    else {
	      // keep current state.
	    }
	    break;

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:

	    if (s_debug_protocol_errors) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error E: DM received in state %d.\n", S->stream_id, S->state);
	    }
	    // dl disconnect *indication*
	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
	    server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);
	    discard_i_queue (S);
	    STOP_T1;
	    STOP_T3;
	    enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    break;

	  case 	state_5_awaiting_v22_connection:

#if 0
	    // Erratum: The flow chart says we should do this.
	    // I'm not saying it is wrong.  I just found it necessary to change this
	    // to work around an apparent bug in a popular hardware TNC.

	    if (f == 1) {
	      discard_i_queue (S);
	      // dl disconnect *indication*
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
	      server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);
	      STOP_T1;
	      enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    }
	    else {
	      // keep current state.
	    }
#else
	    // Erratum: This is not in original spec.  It's copied from the FRMR case.

	    // I was expecting FRMR to mean the other end did not understand v2.2.
	    // Experimentation, with KPC-3+, revealed that we get DM instead.
	    // One part of the the 2.0 spec sort of indicates this might be intentional.
	    // But another part more clearly states it should be FRMR.

	    // At first I thought it was an error in the protocol spec.
	    // Later, I tend to believe it was just implemented wrong in the KPC-3+.

	    if (f == 1) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("%s doesn't understand AX.25 v2.2.  Trying v2.0 ...\n", S->addrs[PEERCALL]);
	
	      INIT_T1V_SRT;

	      // Erratum: page 105.   We are in state 5 so I think that means modulo is 128,
	      // k is probably something > 7, and selective reject is enabled.
	      // At the end of this we go to state 1.  
	      // It seems to me, that we really want to set version 2.0 in here so we have
	      // compatible settings.

	      set_version_2_0 (S);

	      establish_data_link (S);
	      S->layer_3_initiated = 1;
	      enter_new_state (S, state_1_awaiting_connection, __func__, __LINE__);
	    }
#endif
	    break;
	}

} /* end dm_frame */




/*------------------------------------------------------------------------------
 *
 * Name:	UA_frame
 * 
 * Purpose:	Process UA Response Frame.
 *
 * Inputs:	S	- Data Link State Machine.
 *		f	- Final bit.
 *
 * Description:	4.3.3.4. Unnumbered Acknowledge (UA) Response
 *
 *		The UA response frame acknowledges the reception and acceptance of a SABM(E) or DISC command
 *		frame. A received command is not actually processed until the UA response frame is sent. Information fields are
 *		not permitted in a UA frame.
 *
 *		4.4.1. TNC Busy Condition
 *
 *		When a TNC is temporarily unable to receive I frames (e.g., when receive buffers are full), it sends a Receive
 *		Not Ready (RNR) frame. This informs the sending TNC that the receiving TNC cannot handle any more I
 *		frames at the moment. This receiving TNC clears this condition by the sending a UA, RR, REJ or SABM(E)
 *		command frame.
 *
 *		6.2. Poll/Final (P/F) Bit Procedures
 *
 *		The response frame returned by a TNC depends on the previous command received, as described in the
 *		following paragraphs.
 *		The next response frame returned by the TNC to a SABM(E) or DISC command with the P bit set to "1" is a
 *		UA or DM response with the F bit set to "1".
 *
 *		6.3.1. AX.25 Link Connection Establishment
 *
 *		To connect to a distant TNC, the originating TNC sends a SABM command frame to the distant TNC and
 *		starts its T1 timer. If the distant TNC exists and accepts the connect request, it responds with a UA response
 *		frame and resets all of its internal state variables (V(S), V(A) and V(R)). Reception of the UA response frame by
 *		the originating TNC causes it to cancel the T1 timer and set its internal state variables to "0".
 *
 *		6.5. Resetting Procedure
 *
 *		A TNC initiates a reset procedure whenever it receives an unexpected UA response frame, or after receipt of
 *		a FRMR frame from a TNC using an older version of the protocol.
 *
 *------------------------------------------------------------------------------*/

static void ua_frame (ax25_dlsm_t *S, int f)
{
	switch (S->state) {

	  case 	state_0_disconnected:

	    // Erratum: flow chart says errors C and D.  Neither one really makes sense.
	    // "Unexpected UA in states 3, 4, or 5."	We are in state 0 here.
	    // "UA received without F=1 when SABM or DISC was sent P=1."

	    if (s_debug_protocol_errors) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error C: Unexpected UA in state %d.\n", S->stream_id, S->state);
	    }
	    break;

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:

	    if (f == 1) {
	      if (S->layer_3_initiated) {
	        text_color_set(DW_COLOR_INFO);
	        // TODO: add via if appropriate.
	        dw_printf ("Stream %d: Connected to %s.  (%s)\n", S->stream_id, S->addrs[PEERCALL], S->state == state_5_awaiting_v22_connection ? "v2.2" : "v2.0");
	        // There is a subtle difference here between connect confirm and indication.
	        // connect *confirm* means "has been made"
		// The AGW API distinguishes between incoming (initiated by other station) and
		// outgoing (initiated by me) connections.
	        int incoming = 0;
	        server_link_established (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], incoming);
	      }
	      else if (S->vs != S->va) {
#if 1
		// Erratum: 2006 version has this.

	        INIT_T1V_SRT;

		START_T3;		// Erratum:  Rather pointless because we immediately stop it below.
					// In the original flow chart, that is.
					// I think there is an error as explained below.
					// In my version this is still pointless because we start T3 later.

#else
	        // Erratum: Original version has this.
	        // I think this could be harmful.
	        // The client app might have been impatient and started sending
	        // information already.  I don't see why we would want to discard it.

	        discard_i_queue (S);
#endif
	        text_color_set(DW_COLOR_INFO);
	        dw_printf ("Stream %d: Connected to %s.  (%s)\n", S->stream_id, S->addrs[PEERCALL], S->state == state_5_awaiting_v22_connection ? "v2.2" : "v2.0");

	        // Erratum: 2006 version says DL-CONNECT *confirm* but original has *indication*.

	        // connect *indication* means "has been requested".
	        // *confirm* seems right because we got a reply from the other side.

		int incoming = 0;
	        server_link_established (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], incoming);
	      }

	      STOP_T1;
#if 1				// My version.
	      START_T3;
#else				// As shown in flow chart.
	      STOP_T3;		// Erratum?  I think this is wrong.
				// We are about to enter state 3.   When in state 3 either T1 or T3 should be
				// running.  In state 3, we always see start one / stop the other pairs except where
				// we are about to enter a different state.
				// Since there is nothing outstanding where we expect a response, T1 would
				// not be started.
#endif
	      SET_VS(0);
	      SET_VA(0);
	      SET_VR(0);
	      select_t1_value (S);

// Erratum:  mdl_negotiate_request does not appear in the SDL flow chart.
// It is mentioned here:
//
// C5.3 Internal Operation of the Machine
//
// The Management Data link State Machine handles the negotiation/notification of
// operational parameters. It uses a single command/response exchange to negotiate the
// final values of negotiable parameters.
//
// The station initiating the AX.25 connection will send an XID command after it receives
// the UA frame. If the other station is using a version of AX.25 earlier than 2.2, it will
// respond with an FRMR of the XID command and the default version 2.0 parameters will
// be used. If the other station is using version 2.2 or better, it will respond with an XID
// response.

	      if (S->state == state_5_awaiting_v22_connection) {
	        mdl_negotiate_request (S);
	      }

	      SET_RC(0);			// My enhancement.  See Erratum note in select_t1_value.
	      enter_new_state (S, state_3_connected, __func__, __LINE__);
	    }
	    else {
	      if (s_debug_protocol_errors) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Stream %d: AX.25 Protocol Error D: UA received without F=1 when SABM or DISC was sent P=1.\n", S->stream_id);
	      }
	      // stay in current state, either 1 or 5.
	    }
	    break;

	  case 	state_2_awaiting_release:

	    // Erratum: 2006 version is missing yes/no labels on this test.
	    // DL-ERROR Indication does not mention error D.

	    if (f == 1) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
	      server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);
	      STOP_T1;
	      enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    }
	    else {
	      if (s_debug_protocol_errors) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Stream %d: AX.25 Protocol Error D: UA received without F=1 when SABM or DISC was sent P=1.\n", S->stream_id);
	      }
	      // stay in same state.
	    }
	    break;

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:

	    if (s_debug_protocol_errors) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error C: Unexpected UA in state %d.\n", S->stream_id, S->state);
	    }	    
	    establish_data_link (S);
	    S->layer_3_initiated = 0;

	    // Erratum? Flow chart goes to state 1.  Wouldn't we want this to be state 5 if modulo is 128?
	    enter_new_state (S, S->modulo == 128 ? state_5_awaiting_v22_connection : state_1_awaiting_connection, __func__, __LINE__);
	    break;
	}

} /* end ua_frame */



/*------------------------------------------------------------------------------
 *
 * Name:	frmr_frame
 * 
 * Purpose:	Process FRMR Response Frame.
 *
 * Inputs:	S	- Data Link State Machine.
 *
 * Description:	4.3.3.2. Set Asynchronous Balanced Mode Extended (SABME) Command
 *		...
 *		The TNC confirms reception and acceptance of a SABME command by sending a UA response frame at the
 *		earliest opportunity. If the TNC is not capable of accepting a SABME command, it responds with a DM frame.
 *		A TNC that uses a version of AX.25 prior to v2.2 responds with a FRMR.
 *
 *		4.3.3.9. FRMR Response Frame
 *
 *		The FRMR response is removed from the standard for the following reasons:
 *		a) UI frame transmission was not allowed during FRMR recovery;
 *		b) During FRMR recovery, the link could not be reestablished by the station that sent the FRMR;
 *		c) The above functions are better handled by simply resetting the link with a SABM(E) + UA exchange;
 *		d) An implementation that receives and process FRMRs but does not transmit them is compatible with older
 *		   versions of the standard; and
 *		e) SDL is simplified and removes the need for one state.
 *		This version of AX.25 operates with previous versions of AX.25. It does not generate a FRMR Response
 *		frame, but handles error conditions by resetting the link.
 *
 *		6.3.2. Parameter Negotiation Phase
 *
 *		Parameter negotiation occurs at any time. It is accomplished by sending the XID command frame and
 *		receiving the XID response frame. Implementations of AX.25 prior to version 2.2 respond to an XID command
 *		frame with a FRMR response frame. The TNC receiving the FRMR uses a default set of parameters compatible
 *		with previous versions of AX.25.
 *
 *		6.5. Resetting Procedure
 *
 *		The link resetting procedure initializes both directions of data flow after a unrecoverable error has occurred.
 *		This resetting procedure is used only in the information-transfer state of an AX.25 link.
 *		A TNC initiates a reset procedure whenever it receives an unexpected UA response frame, or after receipt of
 *		a FRMR frame from a TNC using an older version of the protocol.
 *
 *------------------------------------------------------------------------------*/


static void frmr_frame (ax25_dlsm_t *S)
{
	switch (S->state) {

	  case 	state_0_disconnected:
	  case 	state_1_awaiting_connection:
	  case 	state_2_awaiting_release:
	    // Ignore it.  Keep current state.
	    break;

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:
	    
	    if (s_debug_protocol_errors) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error K: FRMR not expected in state %d.\n", S->stream_id, S->state);
	    }

	    set_version_2_0 (S);	// Erratum: FRMR can only be sent by v2.0.
					// Need to force v2.0.  Should be added to flow chart.
	    establish_data_link (S);
	    S->layer_3_initiated = 0;
	    enter_new_state (S, state_1_awaiting_connection, __func__, __LINE__);
	    break;

	  case 	state_5_awaiting_v22_connection:

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("%s doesn't understand AX.25 v2.2.  Trying v2.0 ...\n", S->addrs[PEERCALL]);
	
	    INIT_T1V_SRT;

	    set_version_2_0 (S);		// Erratum: Need to force v2.0.  This is not in flow chart.

	    establish_data_link (S);
	    S->layer_3_initiated = 1;		// Erratum?  I don't understand the difference here.
						// State 1 clears it.  State 5 sets it.  Why not the same?

	    enter_new_state (S, state_1_awaiting_connection, __func__, __LINE__);
	    break;
	}

// part of state machine for the XID negotiation.

// I would not expect this to happen.
// To get here:
//	We sent SABME.  (not SABM)
//	Other side responded with UA so it understands v2.2.
//	We sent XID command which puts us int the negotiating state.
// Presumably this is in response to the XID and not something else.

// Anyhow, we will fall back to v2.0 parameters.

	switch (S->mdl_state) {

	  case 	mdl_state_0_ready:
	    break;

	  case 	mdl_state_1_negotiating:

	    set_version_2_0 (S);
	    S->mdl_state = mdl_state_0_ready;
	    break;
	}

} /* end frmr_frame */


/*------------------------------------------------------------------------------
 *
 * Name:	ui_frame
 * 
 * Purpose:	Process XID frame for negotiating protocol parameters.
 *
 * Inputs:	S	- Data Link State Machine.
 *
 *		cr	- Is it command or response?
 *
 *		pf	- Poll/Final bit.
 *
 * Description:	4.3.3.6. Unnumbered Information (UI) Frame
 *
 *		The Unnumbered Information frame contains PID and information fields and passes information along the
 *		link outside the normal information controls. This allows information fields to be exchanged on the link, bypassing
 *		flow control.
 *
 *		Because these frames cannot be acknowledged, if one such frame is obliterated, it cannot be recovered.
 *		A received UI frame with the P bit set causes a response to be transmitted. This response is a DM frame when
 *		in the disconnected state, or an RR (or RNR, if appropriate) frame in the information transfer state.
 *
 * Reality:	The data link state machine was an add-on after APRS and client APIs were already done.
 *		UI frames don't go thru here for normal operation.
 *		The only reason we have this function is so that we can send a response to a UI command with P=1.
 *
 *------------------------------------------------------------------------------*/

static void ui_frame (ax25_dlsm_t *S, cmdres_t cr, int pf)
{
	if (cr == cr_cmd && pf == 1) {

	  switch (S->state) {

	    case 	state_0_disconnected:
	    case 	state_1_awaiting_connection:
	    case 	state_2_awaiting_release:
	    case 	state_5_awaiting_v22_connection:
	      {
	        cmdres_t r = cr_res;	// DM response with F taken from P.
	        int nopid = 0;		// PID applies only for I and UI frames.

	        packet_t pp = ax25_u_frame (S->addrs, S->num_addr, r, frame_type_U_DM, pf, nopid, NULL, 0);
	        lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	      }
	      break;

	    case 	state_3_connected:
	    case 	state_4_timer_recovery:

	      enquiry_response (S, frame_type_U_UI, pf);
	      break;
	  }
	}

} /* end ui_frame */



/*------------------------------------------------------------------------------
 *
 * Name:	xid_frame
 * 
 * Purpose:	Process XID frame for negotiating protocol parameters.
 *
 * Inputs:	S	- Data Link State Machine.
 *
 *		cr	- Is it command or response?
 *
 *		pf	- Poll/Final bit.
 *
 * Description:	4.3.3.7 Exchange Identification (XID) Frame
 *
 *		The Exchange Identification frame causes the addressed station to identify itself, and
 *		to provide its characteristics to the sending station. An information field is optional within
 *		the XID frame. A station receiving an XID command returns an XID response unless a UA
 *		response to a mode setting command is awaiting transmission, or a FRMR condition
 *		exists.
 *
 *		The XID frame complies with ISO 8885. Only those fields applicable to AX.25 are
 *		described. All other fields are set to an appropriate value. This implementation is
 *		compatible with any implementation which follows ISO 8885. Only the general-purpose
 *		XID information field identifier is required in this version of AX.25.
 *
 *		The information field consists of zero or more information elements. The information
 *		elements start with a Format Identifier (FI) octet. The second octet is the Group Identifier
 *		(GI). The third and forth octets form the Group Length (GL). The rest of the information
 *		field contains parameter fields.
 *
 *		The FI takes the value 82 hex for the general-purpose XID information. The GI takes
 *		the value 80 hex for the parameter-negotiation identifier. The GL indicates the length of
 *		the associated parameter field. This length is expressed as a two-octet binary number
 *		representing the length of the associated parameter field in octets. The high-order bits of
 *		length value are in the first of the two octets. A group length of zero indicates the lack of
 *		an associated parameter field and that all parameters assume their default values. The GL
 *		does not include its own length or the length of the GI.
 *
 *		The parameter field contains a series of Parameter Identifier (PI), Parameter Length
 *		(PL), and Parameter Value (PV) set structures, in that order. Each PI identifies a
 *		parameter and is one octet in length. Each PL indicates the length of the associated PV in
 *		octets, and is one octet in length. Each PV contains the parameter value and is PL octets
 *		in length. The PL does not include its own length or the length of its associated PI. A PL
 *		value of zero indicates that the associated PV is absent; the parameter assumes the
 *		default value. A PI/PL/PV set may be omitted if it is not required to convey information, or
 *		if present values for the parameter are to be used. The PI/PL/PV fields are placed into the
 *		information field of the XID frame in ascending order. There is only one entry for each
 *		PI/PL/PV field used. A parameter field containing an unrecognized PI is ignored. An
 *		omitted parameter field assumes the currently negotiated value.
 *
 *------------------------------------------------------------------------------*/


static void xid_frame (ax25_dlsm_t *S, cmdres_t cr, int pf, unsigned char *info_ptr, int info_len)
{
	struct xid_param_s param;
	char desc[150];
	int ok;
	unsigned char xinfo[40];
	int xlen;
	cmdres_t res = cr_res;
	int f = 1;
	int nopid = 0;
	packet_t pp;


	switch (S->mdl_state) {

	  case 	mdl_state_0_ready:

	    if (cr == cr_cmd) {

	      if (pf == 1) {

// Take parameters sent by other station.
// Generally we take minimum of what he wants and what I can do.
// Adjust my working configuration and send it back.

	        ok = xid_parse (info_ptr, info_len, &param, desc, sizeof(desc));
		
		if (ok) {
		  negotiation_response (S, &param);

	          xlen = xid_encode (&param, xinfo, res);

	          pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_XID, f, nopid, xinfo, xlen);
	          lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	        }
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Stream %d: AX.25 Protocol Error MDL-A: XID command without P=1.\n", S->stream_id);
	      }
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Stream %d: AX.25 Protocol Error MDL-B: Unexpected XID response.\n", S->stream_id);
	    }
	    break;

	  case 	mdl_state_1_negotiating:
 
	    if (cr == cr_res) {

	      if (pf == 1) {

// Got expected response.  Copy into my working parameters.

	        ok = xid_parse (info_ptr, info_len, &param, desc, sizeof(desc));

	        if (ok) {
		  complete_negotiation (S, &param);
	        }

	        S->mdl_state = mdl_state_0_ready;
	        STOP_TM201;

//#define TEST_TEST 1

#if TEST_TEST		// Send TEST command to see how it responds.
			// We currently have no Client API for sending this or reporting result.
		{
	          char info[80] = "The quick brown fox jumps over the lazy dog.";
	          cmdres_t cmd = cr_cmd;
	          int p = 0;
	          int nopid = 0;
	          packet_t pp;

	          pp = ax25_u_frame (S->addrs, S->num_addr, cmd, frame_type_U_TEST, p, nopid, (unsigned char *)info, (int)strlen(info));
	          lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	        }
#endif
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Stream %d: AX.25 Protocol Error MDL-D: XID response without F=1.\n", S->stream_id);
	      }
	    }
	    else {
	      // Not expecting to receive a command when I sent one.
	      // Flow chart says requeue but I just drop it.
	      // The other end can retry and maybe I will be back to ready state by then.
	    }
	    break;
	}

} /* end xid_frame */


/*------------------------------------------------------------------------------
 *
 * Name:	test_frame
 * 
 * Purpose:	Process TEST command for checking link.
 *
 * Inputs:	S	- Data Link State Machine.
 *
 *		cr	- Is it command or response?
 *
 *		pf	- Poll/Final bit.
 *
 * Description:	4.3.3.8. Test (TEST) Frame
 *
 *		The Test command causes the addressed station to respond with the TEST response at the first respond
 *		opportunity; this performs a basic test of the data-link control. An information field is optional with the TEST
 *		command. If present, the received information field is returned, if possible, by the addressed station, with the
 *		TEST response. The TEST command has no effect on the mode or sequence variables maintained by the station.
 *
 *		A FRMR condition may be established if the received TEST command information field exceeds the maximum
 *		defined storage capability of the station. If a FRMR response is not returned for this condition, a TEST response
 *		without an information field is returned.
 *
 *		The station considers the data-link layer test terminated on receipt of the TEST response, or when a time-out
 *		period has expired. The results of the TEST command/response exchange are made available for interrogation
 *		by a higher layer.
 *
 * Erratum:	TEST frame is not mentioned in the SDL flow charts.
 *		Don't know how P/F is supposed to be used.
 *		Here, the response sends back what was received in the command.
 *
 *------------------------------------------------------------------------------*/


static void test_frame (ax25_dlsm_t *S, cmdres_t cr, int pf, unsigned char *info_ptr, int info_len)
{
	cmdres_t res = cr_res;
	int f = pf;
	int nopid = 0;
	packet_t pp;

	if (cr == cr_cmd) {
	  pp = ax25_u_frame (S->addrs, S->num_addr, res, frame_type_U_TEST, f, nopid, info_ptr, info_len);
	  lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	}

} /* end test_frame */



/*------------------------------------------------------------------------------
 *
 * Name:	dl_timer_expiry
 * 
 * Purpose:	Some timer expired.  Figure out which one and act accordingly.
 *
 * Inputs:	none.
 *
 *------------------------------------------------------------------------------*/

void dl_timer_expiry (void)
{
	ax25_dlsm_t *p;
	double now = dtime_now();

// Examine all of the data link state machines.
// Process only those where timer:
//	- is running.
//	- is not paused.
//	- expiration time has arrived or passed.

	for (p = list_head; p != NULL; p = p->next) {
	  if (p->t1_exp != 0 && p->t1_paused_at == 0 && p->t1_exp <= now) {
	    p->t1_exp = 0;
	    p->t1_paused_at = 0;
	    p->t1_had_expired = 1;
	    t1_expiry (p);
	  }
	}

	for (p = list_head; p != NULL; p = p->next) {
	  if (p->t3_exp != 0 && p->t3_exp <= now) {
	    p->t3_exp = 0;
	    t3_expiry (p);
	  }
	}

	for (p = list_head; p != NULL; p = p->next) {
	  if (p->tm201_exp != 0 && p->tm201_paused_at == 0 && p->tm201_exp <= now) {
	    p->tm201_exp = 0;
	    p->tm201_paused_at = 0;
	    tm201_expiry (p);
	  }
	}

} /* end dl_timer_expiry */


/*------------------------------------------------------------------------------
 *
 * Name:	t1_expiry
 * 
 * Purpose:	Handle T1 timer expiration for outstanding I frame or P-bit.
 *
 * Inputs:	S	- Data Link State Machine.
 *
 * Description:	4.4.5.1. T1 Timer Recovery
 *
 *		If a transmission error causes a TNC to fail to receive (or to receive and discard) a single I frame, or the last I
 *		frame in a sequence of I frames, then the TNC does not detect a send-sequence-number error and consequently
 *		does not transmit a REJ/SREJ. The TNC that transmitted the unacknowledged I frame(s) following the completion
 *		of timeout period T1, takes appropriate recovery action to determine when I frame retransmission as described
 *		in Section 6.4.10 should begin. This condition is cleared by the reception of an acknowledgement for the sent
 *		frame(s), or by the link being reset.
 *
 *		6.7.1.1. Acknowledgment Timer T1
 *
 *		T1, the Acknowledgement Timer, ensures that a TNC does not wait indefinitely for a response to a frame it
 *		sends. This timer cannot be expressed in absolute time; the time required to send frames varies greatly with the
 *		signaling rate used at Layer 1. T1 should take at least twice the amount of time it would take to send maximum
 *		length frame to the distant TNC and get the proper response frame back from the distant TNC. This allows time
 *		for the distant TNC to do some processing before responding.
 *		If Layer 2 repeaters are used, the value of T1 should be adjusted according to the number of repeaters through
 *		which the frame is being transferred.
 *
 *------------------------------------------------------------------------------*/

// Make timer start, stop, expiry a different color to stand out.

#define DW_COLOR_DEBUG_TIMER DW_COLOR_ERROR


static void t1_expiry (ax25_dlsm_t *S)
{

	if (s_debug_timers) {
	  double now = dtime_now();

	  text_color_set(DW_COLOR_DEBUG_TIMER);
	  dw_printf ("t1_expiry (), [now=%.3f], state=%d, rc=%d\n", now - S->start_time, S->state, S->rc);
	}

	switch (S->state) {

	  case 	state_0_disconnected:

	    // Ignore it.
	    break;

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:

	    // MAXV22 hack.
	    // If we already sent the maximum number of SABME, fall back to v2.0 SABM.

	    if (S->state == state_5_awaiting_v22_connection && S->rc == g_misc_config_p->maxv22) {
	      set_version_2_0 (S);
	      enter_new_state (S, state_1_awaiting_connection, __func__, __LINE__);
	    }

	    if (S->rc == S->n2_retry) {
	      discard_i_queue(S);
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Failed to connect to %s after %d tries.\n", S->addrs[PEERCALL], S->n2_retry);
	      server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 1);
	      enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    }
	    else {
	      cmdres_t cmd = cr_cmd;
	      int p = 1;
	      int nopid = 0;

	      packet_t pp;

	      SET_RC(S->rc+1);
	      if (S->rc > S->peak_rc_value) S->peak_rc_value = S->rc;	// Keep statistics.

	      pp = ax25_u_frame (S->addrs, S->num_addr, cmd, (S->state == state_5_awaiting_v22_connection) ? frame_type_U_SABME : frame_type_U_SABM, p, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	      select_t1_value(S);
	      START_T1;
	      // Keep same state.
	    }
	    break;
	    
	  case 	state_2_awaiting_release:

	    if (S->rc == S->n2_retry) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Stream %d: Disconnected from %s.\n", S->stream_id, S->addrs[PEERCALL]);
	      server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 0);
	      enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    }
	    else {
	      cmdres_t cmd = cr_cmd;
	      int p = 1;
	      int nopid = 0;

	      packet_t pp;

	      SET_RC(S->rc+1);
	      if (S->rc > S->peak_rc_value) S->peak_rc_value = S->rc;

	      pp = ax25_u_frame (S->addrs, S->num_addr, cmd, frame_type_U_DISC, p, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	      select_t1_value(S);
	      START_T1;
	      // stay in same state
	    }
	    break;

	  case 	state_3_connected:

	    SET_RC(1);
	    transmit_enquiry (S);
	    enter_new_state (S, state_4_timer_recovery, __func__, __LINE__);
	    break;

	  case 	state_4_timer_recovery:

	    if (S->rc == S->n2_retry) {

// Erratum: 2006 version, page 103, is missing yes/no labels on decision blocks.

	      if (S->va != S->vs) {

	        if (s_debug_protocol_errors) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Stream %d: AX.25 Protocol Error I: %d timeouts: unacknowledged sent data.\n", S->stream_id, S->n2_retry);
	        }
	      }
	      else if (S->peer_receiver_busy) {

	        if (s_debug_protocol_errors) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Stream %d: AX.25 Protocol Error U: %d timeouts: extended peer busy condition.\n", S->stream_id, S->n2_retry);
	        }
	      }
	      else {

	        if (s_debug_protocol_errors) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Stream %d: AX.25 Protocol Error T: %d timeouts: no response to enquiry.\n", S->stream_id, S->n2_retry);
	        }
	      }

	      // Erratum:  Flow chart says DL-DISCONNECT "request" in both original and 2006 revision.
	      // That is clearly wrong because a "request" would come FROM the higher level protocol/client app.
	      // I think it should be "indication" rather than "confirm" because the peer condition is unknown.

	      // dl disconnect *indication*
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Stream %d: Disconnected from %s due to timeouts.\n", S->stream_id, S->addrs[PEERCALL]);
	      server_link_terminated (S->chan, S->client, S->addrs[PEERCALL], S->addrs[OWNCALL], 1);

	      discard_i_queue (S);

	      cmdres_t cr = cr_res;	// DM can only be response.
	      int f = 0;		// Erratum: Assuming F=0 because it is not response to P=1
	      int nopid = 0;

	      packet_t pp = ax25_u_frame (S->addrs, S->num_addr, cr, frame_type_U_DM, f, nopid, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	      enter_new_state (S, state_0_disconnected, __func__, __LINE__);
	    }
	    else {
	      SET_RC(S->rc+1);
	      if (S->rc > S->peak_rc_value) S->peak_rc_value = S->rc;	// gather statistics.

	      transmit_enquiry (S);
	      // Keep same state.
	    }
	    break;
	}

} /* end t1_expiry */


/*------------------------------------------------------------------------------
 *
 * Name:	t3_expiry
 * 
 * Purpose:	Handle T3 timer expiration.
 *
 * Inputs:	S	- Data Link State Machine.
 *
 * Description:	TODO:  still don't understand this.
 *
 *		4.4.5.2. Timer T3 Recovery
 *
 *		Timer T3 ensures that the link is still functional during periods of low information transfer. When T1 is not
 *		running (no outstanding I frames), T3 periodically causes the TNC to poll the other TNC of a link. When T3
 *		times out, an RR or RNR frame is transmitted as a command with the P bit set, and then T1 is started. When a
 *		response to this command is received, T1 is stopped and T3 is started. If T1 expires before a response is
 *		received, then the waiting acknowledgement procedure (Section 6.4.11) is executed.
 *
 *		6.7.1.3. Inactive Link Timer T3
 *
 *		T3, the Inactive Link Timer, maintains link integrity whenever T1 is not running. It is recommended that
 *		whenever there are no outstanding unacknowledged I frames or P-bit frames (during the information-transfer
 *		state), an RR or RNR frame with the P bit set to "1" be sent every T3 time units to query the status of the other
 *		TNC. The period of T3 is locally defined, and depends greatly on Layer 1 operation. T3 should be greater than
 *		T1; it may be very large on channels of high integrity.
 *
 *------------------------------------------------------------------------------*/

static void t3_expiry (ax25_dlsm_t *S)
{

	if (s_debug_timers) {
	  double now = dtime_now();

	  text_color_set(DW_COLOR_DEBUG_TIMER);
	  dw_printf ("t3_expiry (), [now=%.3f]\n", now - S->start_time);
	}

	switch (S->state) {

	  case 	state_0_disconnected:
	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:
	  case 	state_2_awaiting_release:
	  case 	state_4_timer_recovery:

	    break;

	  case 	state_3_connected:

// Erratum: Original sets RC to 0, 2006 revision sets RC to 1 which makes more sense.

	    SET_RC(1);
	    transmit_enquiry (S);
	    enter_new_state (S, state_4_timer_recovery, __func__, __LINE__);
	    break;
	}

} /* end t3_expiry */



/*------------------------------------------------------------------------------
 *
 * Name:	tm201_expiry
 * 
 * Purpose:	Handle TM201 timer expiration.
 *
 * Inputs:	S	- Data Link State Machine.
 *
 * Description:	This is used when waiting for a response to an XID command.
 *
 *------------------------------------------------------------------------------*/


static void tm201_expiry (ax25_dlsm_t *S)
{

	struct xid_param_s param;
	unsigned char xinfo[40];
	int xlen;
	cmdres_t cmd = cr_cmd;
	int p = 1;
	int nopid = 0;
	packet_t pp;


	if (s_debug_timers) {
	  double now = dtime_now();

	  text_color_set(DW_COLOR_DEBUG_TIMER);
	  dw_printf ("tm201_expiry (), [now=%.3f], state=%d, rc=%d\n", now - S->start_time, S->state, S->rc);
	}

	switch (S->mdl_state) {

	  case 	mdl_state_0_ready:

// Timer shouldn't be running when in this state.

	    break;

	  case 	mdl_state_1_negotiating:

	    S->mdl_rc++;
	    if (S->mdl_rc > S->n2_retry) {
              text_color_set(DW_COLOR_ERROR);
              dw_printf ("Stream %d: AX.25 Protocol Error MDL-C: Management retry limit exceeded.\n", S->stream_id);
	      S->mdl_state = mdl_state_0_ready;
	    }
	    else {
	      // No response.  Ask again.

              initiate_negotiation (S, &param);

	      xlen = xid_encode (&param, xinfo, cmd);

	      pp = ax25_u_frame (S->addrs, S->num_addr, cmd, frame_type_U_XID, p, nopid, xinfo, xlen);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	      START_TM201;
	    }
	    break;
	}

} /* end tm201_expiry */


//###################################################################################
//###################################################################################
//
//		Subroutines from protocol spec, pages 106 - 109 
//
//###################################################################################
//###################################################################################

// FIXME: continue review here.


/*------------------------------------------------------------------------------
 *
 * Name:	nr_error_recovery
 * 
 * Purpose:	Try to recover after receiving an expected N(r) value.
 *
 *------------------------------------------------------------------------------*/

static void nr_error_recovery (ax25_dlsm_t *S)
{
	if (s_debug_protocol_errors) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Stream %d: AX.25 Protocol Error J: N(r) sequence error.\n", S->stream_id);
	}
	establish_data_link (S);
	S->layer_3_initiated = 0;
	  
} /* end nr_error_recovery */


/*------------------------------------------------------------------------------
 *
 * Name:	establish_data_link
 *		(Combined with "establish extended data link")
 * 
 * Purpose:	Send SABM or SABME to other station.
 *
 * Inputs:	S->
 *			addrs		destination, source, and optional digi addresses.
 *			num_addr	Number of addresses.  Should be 2 .. 10.
 *			modulo		Determines if we send SABME or SABM.
 *
 * Description:	Original spec had two different functions that differed
 *		only by sending SABM or SABME.  Here they are combined into one.
 *
 *------------------------------------------------------------------------------*/

static void establish_data_link (ax25_dlsm_t *S)
{
	cmdres_t cmd = cr_cmd;
	int p = 1;
	packet_t pp;
	int nopid = 0;

	clear_exception_conditions (S);

// Erratum:  We have an off-by-one error here.
// Flow chart shows setting RC to 0 and we end up sending SAMB(e) 11 times when N2 (RETRY) is 10.
// It should be 1 rather than 0.

	SET_RC(1);
	pp = ax25_u_frame (S->addrs, S->num_addr, cmd, (S->modulo == 128) ? frame_type_U_SABME : frame_type_U_SABM, p, nopid, NULL, 0);
	lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	STOP_T3;
	START_T1;
	  
} /* end establish_data_link */



/*------------------------------------------------------------------------------
 *
 * Name:	clear_exception_conditions
 * 
 *------------------------------------------------------------------------------*/

static void clear_exception_conditions (ax25_dlsm_t *S)
{
	S->peer_receiver_busy = 0;	
	S->reject_exception = 0;
	S->own_receiver_busy = 0;
	S->acknowledge_pending = 0;

// My enhancement.  If we are establishing a new connection, we should discard any saved out of sequence incoming I frames.

	int n;

	for (n = 0; n < 128; n++) {
	  if (S->rxdata_by_ns[n] != NULL) {
	    cdata_delete (S->rxdata_by_ns[n]);
	    S->rxdata_by_ns[n] = NULL;
	  }
	}

// We retain the transmit I frame queue so we can continue after establishing a new connection.
	  
} /* end clear_exception_conditions */


/*------------------------------------------------------------------------------
 *
 * Name:	transmit_enquiry, page 106
 *
 * Purpose:	This is called only when a timer expires.
 *
 *		T1:	We sent I frames and timed out waiting for the ack.
 *			Poke the other end to determine how much it got so far
 *			so we know where to continue.
 *
 *		T3:	Not activity for substantial amount of time.
 *			Poke the other end to see if it is still there.
 *			
 * 
 * Observation:	This is the only place where we send RR command with P=1.
 *
 * Sequence of events:
 *
 *		We send some I frames to the other guy.
 *		There are outstanding sent I frames for which we did not receive ACK.
 *
 *		Timer 1 expires when we are in state 3: send RR/RNR command P=1 (here). Enter state 4.
 *		Timer 1 expires when we are in state 4: same until max retry count is exceeded.
 *
 *					Other guy gets RR/RNR command P=1.
 *					Same action for either state 3 or 4.
 *					Whether he has outstanding un-ack'ed sent I frames is irrelevant.
 *					He calls "enquiry response" which sends RR/RNR response F=1.
 *					(Read about detour 1 below and in enquiry_response.)
 *
 *		I get back RR/RNR response F=1.  Still in state 4.
 *		Of course, N(R) gets copied into V(A).
 *		Now here is the interesting part.
 *		If the ACKs are caught up, i.e.  V(A) == V(S), stop T1 and enter state 3.
 *		Otherwise, "invoke retransmission" to resend everything after N(R).
 *
 *
 * Detour 1:	You were probably thinking, "Suppose SREJ is enabled and the other guy
 *		had a record of the SREJ frames sent which were not answered by filled in
 *		I frames.  Why not send the SREJ again instead of backing up and resending
 *		stuff which already got there OK?"
 *
 *		The code to handle incoming SREJ in state 4 is there but stop T1 is in the 
 *		wrong place as mentioned above.
 *
 *------------------------------------------------------------------------------*/

static void transmit_enquiry (ax25_dlsm_t *S)
{
	int p = 1;
	int nr = S->vr;
	cmdres_t cmd = cr_cmd;
	packet_t pp;

	if (s_debug_retry) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n****** TRANSMIT ENQUIRY   RR/RNR cmd P=1 ****** state=%d, rc=%d\n\n", S->state, S->rc);
	}

// This is the ONLY place that we send RR/RNR *command* with P=1.
// Everywhere else should be response.
// I don't think we ever use RR/RNR command P=0 but need to check on that.

	pp = ax25_s_frame (S->addrs, S->num_addr, cmd, S->own_receiver_busy ? frame_type_S_RNR : frame_type_S_RR, S->modulo, nr, p, NULL, 0);

	lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	S->acknowledge_pending = 0;
	START_T1;

} /* end transmit_enquiry */



/*------------------------------------------------------------------------------
 *
 * Name:	enquiry_response
 *
 * Inputs:	frame_type	- Type of frame received or frame_not_AX25 for LM seize confirm.
 *				  I think that this function is being called from too many
 *				  different contexts where it really needs to react differently.
 *				  So pass in more information about where we are coming from.
 *
 *		F 		- Always specified as parameter in the references.
 *
 * Description:	This is called for:
 *		- UI command with P=1 then F=1.
 *		- LM seize confirm with ack pending then F=0.  (TODO: not clear on this yet.)
 *			TODO:  I think we want to ensure that this function is called ONLY
 *			for RR/RNR/I command with P=1.  LM Seize confirm can do its own thing and
 *			not get involved in this complication.
 *		- check_need_for_response(), command & P=1, then F=1
 *		- RR/RNR/REJ command & P=1, then F=1
 *
 *		In all cases, we see that F has been specified, usually 1 because it is
 *		a response to a command with P=1.
 *		Specifying F would imply response when the flow chart says RR/RNR command.
 *		The documentation says:
 *
 *		6.2. Poll/Final (P/F) Bit Procedures
 *
 *		The next response frame returned to an I frame with the P bit set to "1", received during the information
 *		transfer state, is an RR, RNR or REJ response with the F bit set to "1".
 *
 *		The next response frame returned to a supervisory command frame with the P bit set to "1", received during
 *		the information transfer state, is an RR, RNR or REJ response frame with the F bit set to "1".
 *
 * Erattum!	The flow chart says RR/RNR *command* but I'm confident it should be response.
 *
 * Erratum:	Ax.25 spec has nothing here for SREJ.  See X.25 2.4.6.11 for explanation.
 * 
 *------------------------------------------------------------------------------*/

static void enquiry_response (ax25_dlsm_t *S, ax25_frame_type_t frame_type, int f)
{
	cmdres_t cr = cr_res;		// Response, not command as seen in flow chart.
	int nr = S->vr;
	packet_t pp;


	if (s_debug_retry) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n****** ENQUIRY RESPONSE  F=%d ******\n\n", f);
	}

#if 1			// Detour 1

			// My addition,  Based on X.25 2.4.6.11.
			// Only for RR, RNR, I.
			// See sequence of events in transmit_enquiry comments.

	if (f == 1 && (frame_type == frame_type_S_RR || frame_type == frame_type_S_RNR || frame_type == frame_type_I)) {
 
	  if (S->own_receiver_busy) {

// I'm busy.

	    pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_RNR, S->modulo, nr, f, NULL, 0);
	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	    S->acknowledge_pending = 0;		// because we sent N(R) from V(R).
	  }

	  else if (S->srej_enable == srej_single || S->srej_enable == srej_multi) {


// SREJ is enabled. This is based on X.25 2.4.6.11.

	    if (S->modulo != 128) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("INTERNAL ERROR: enquiry response should not be sending SREJ for modulo 8.\n");
	    }

// Suppose we received I frames with N(S) of 0, 3, 7.
// V(R) is still 1 because 0 is the last one received with contiguous N(S) values.
// 3 and 7 have been saved into S->rxdata_by_ns.
// We have outstanding requests to resend 1, 2, 4, 5, 6.
// Either those requests or the replies got lost.
// The other end timed out and asked us what is happening by sending RR/RNR command P=1.

// First see if we have any out of sequence frames in the receive buffer.

	    int last;
	    last = AX25MODULO(S->vr - 1, S->modulo, __FILE__, __func__, __LINE__);
	    while (last != S->vr && S->rxdata_by_ns[last] == NULL) {
	      last = AX25MODULO(last - 1, S->modulo, __FILE__, __func__, __LINE__);
	    }
	    
	    if (last != S->vr) {

// Ask for missing frames to be sent again.		X.25 2.4.6.11 b) & 2.3.5.2.2

	      int resend[128];
	      int count = 0;
	      int j;
	      int allow_f1 = 1;

	      j = S->vr;
	      while (j != last) {
	        if (S->rxdata_by_ns[j] == NULL) {
	          resend[count++] = j;
	        }
	        j = AX25MODULO(j + 1, S->modulo, __FILE__, __func__, __LINE__);
	      }

	      send_srej_frames (S, resend, count, allow_f1);
	    }
	    else {

// Not waiting for fill in of missing frames.		X.25 2.4.6.11 c)

	      pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_RR, S->modulo, nr, f, NULL, 0);
	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	      S->acknowledge_pending = 0;
	    }

	  } else {

// SREJ not enabled.
// One might get the idea that it would make sense send REJ here if the reject exception is set.
// However, I can't seem to find that buried in X.25 2.4.5.9.
// And when we look at what happens when RR response, F=1 is received in state 4, it is
// effectively REJ when N(R) is not the same as V(S).

	    if (s_debug_retry) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\n****** ENQUIRY RESPONSE srej not enbled, sending RR resp F=%d ******\n\n", f);
	    }

	    pp = ax25_s_frame (S->addrs, S->num_addr, cr, frame_type_S_RR, S->modulo, nr, f, NULL, 0);
	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	    S->acknowledge_pending = 0;
	  }

	} // end of RR,RNR,I cmd with P=1

	else {

// For cases other than (RR, RNR, I) command, P=1.

	  pp = ax25_s_frame (S->addrs, S->num_addr, cr, S->own_receiver_busy ? frame_type_S_RNR : frame_type_S_RR, S->modulo, nr, f, NULL, 0);
	  lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	  S->acknowledge_pending = 0;
	}

#else

// As found in AX.25 spec.
// Erratum:  This is woefully inadequate when SREJ is enabled.
// Erratum:  Flow chart says RR/RNR command but I'm confident it should be response.

	pp = ax25_s_frame (S->addrs, S->num_addr, cr, S->own_receiver_busy ? frame_type_S_RNR : frame_type_S_RR, S->modulo, nr, f, NULL, 0);
	lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	S->acknowledge_pending = 0;

# endif

} /* end enquiry_response */




/*------------------------------------------------------------------------------
 *
 * Name:	invoke_retransmission
 *
 * Inputs:	nr_input	- Resend starting with this.
 *			  	  Continue will all up to and including current V(S) value.
 * 
 * Description:	Resend one or more frames that have already been sent.
 *		Should always send at least one.
 *
 *		This is probably the result of getting REJ asking for a resend.
 *
 * Context:	I would expect the caller to clear 'acknowledge_pending' after calling this
 *		because we sent N(R), from V(R), to ack what was received from other guy.
 *		I would also expect Stop T3 & Start T1 at the same place.
 *
 *------------------------------------------------------------------------------*/

static void invoke_retransmission (ax25_dlsm_t *S, int nr_input)
{

// Original flow chart showed saving V(S) into temp variable x,
// using V(S) as loop control variable, and finally restoring it
// to original value before returning.
// Here we just a local variable instead of messing with it.
// This should be equivalent but safer.

	int local_vs;
	int sent_count = 0;

	if (s_debug_misc) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("invoke_retransmission(): starting with %d, state=%d, rc=%d, \n", nr_input, S->state, S->rc);
	}

// I don't think we should be here if SREJ is enabled.
// TODO: Figure out why this happens occasionally.

//	if (S->srej_enable != srej_none) {
//	  text_color_set(DW_COLOR_ERROR);
//	  dw_printf ("Internal Error, Did not expect to be here when SREJ enabled.  %s %s %d\n", __FILE__, __func__, __LINE__);
//	}
	
	if (S->txdata_by_ns[nr_input] == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal Error, Can't resend starting with N(S) = %d.  It is not available.  %s %s %d\n", nr_input, __FILE__, __func__, __LINE__);
	  return;
	}


	local_vs = nr_input;
	do {

	  if (S->txdata_by_ns[local_vs] != NULL) {

	    cmdres_t cr = cr_cmd;
	    int ns = local_vs;
	    int nr = S->vr;
 	    int p = 0;

	    if (s_debug_misc) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("invoke_retransmission(): Resending N(S) = %d\n", ns);
	    }

	    packet_t pp = ax25_i_frame (S->addrs, S->num_addr, cr, S->modulo, nr, ns, p,
		S->txdata_by_ns[ns]->pid, (unsigned char *)(S->txdata_by_ns[ns]->data), S->txdata_by_ns[ns]->len);

	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp);
	    // Keep it around in case we need to send again.

	    sent_count++;
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal Error, state=%d, need to retransmit N(S) = %d for REJ but it is not available.  %s %s %d\n", S->state, local_vs, __FILE__, __func__, __LINE__);
	  }
	  local_vs = AX25MODULO(local_vs + 1, S->modulo, __FILE__, __func__, __LINE__);

	} while (local_vs != S->vs);

	if (sent_count == 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal Error, Nothing to retransmit. N(R)=%d, %s %s %d\n", nr_input, __FILE__, __func__, __LINE__);
	}

}  /* end invoke_retransmission */



/*------------------------------------------------------------------------------
 *
 * Name:	check_i_frame_ackd
 *
 * Purpose:
 *
 * Inputs:	nr	- N(R) from I or S frame, acknowledging receipt thru N(R)-1.
 *			  i.e. The next one expected by the peer is N(R).
 *
 * Outputs:	S->va	- updated from nr.
 *
 * Description:	This is called for:
 *		- 'I' frame received and N(R) is in expected range, states 3 & 4.
 *		- RR/RNR command with p=1 received and N(R) is in expected range, state 3 only.
 * 
 *------------------------------------------------------------------------------*/

static void check_i_frame_ackd (ax25_dlsm_t *S, int nr)
{
	if (S->peer_receiver_busy) {
	  SET_VA(nr);

	  // Erratum?  This looks odd to me.
	  // It doesn't seem right that we would have T3 and T1 running at the same time.
	  // Normally we stop one when starting the other.
	  // Should this be Stop T3 instead?

	  START_T3;
	  if ( ! IS_T1_RUNNING) {
	    START_T1;
	  }
	}
	else if (nr == S->vs) {
	  SET_VA(nr);
	  STOP_T1;
	  START_T3;
	  select_t1_value (S);
	}
	else if (nr != S->va) {

	  if (s_debug_misc) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("check_i_frame_ackd n(r)=%d, v(a)=%d,  Set v(a) to new value %d\n", nr, S->va, nr);
	  }

	  SET_VA(nr);
	  START_T1;			// Erratum?  Flow chart says "restart" rather than "start."
					// Is this intentional, what is the difference?
	}

} /* check_i_frame_ackd */



/*------------------------------------------------------------------------------
 *
 * Name:	check_need_for_response
 *
 * Inputs:	frame_type	- frame_type_S_RR, etc.
 *
 *		cr		- Is it a command or response?
 *
 *		pf		- P/F from the frame.
 *
 * Description:	This is called for RR, RNR, and REJ frames.
 *		If it is a command with P=1, we reply with RR or RNR with F=1.
 * 
 *------------------------------------------------------------------------------*/

static void check_need_for_response (ax25_dlsm_t *S, ax25_frame_type_t frame_type, cmdres_t cr, int pf)
{
	if (cr == cr_cmd && pf == 1) {
	  int f = 1;
	  enquiry_response (S, frame_type, f);
	}
	else if (cr == cr_res && pf == 1) {
	  if (s_debug_protocol_errors) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Stream %d: AX.25 Protocol Error A: F=1 received but P=1 not outstanding.\n", S->stream_id);
	  }
	}

} /* end check_need_for_response */



/*------------------------------------------------------------------------------
 *
 * Name:	ui_check
 *
 * Description:	I don't think we need this because UI frames are processed
 *		without going thru the data link state machine.
 * 
 *------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
 *
 * Name:	select_t1_value
 *
 * Purpose:	Dynamically adjust the T1 timeout value, commonly a fixed time known as FRACK.
 *
 * Inputs:	S->rc			Retry counter.
 *
 *		S->srt			Smoothed roundtrip time in seconds.
 *
 *		S->t1_remaining_when_last_stopped
 *					Seconds left on T1 when it is stopped.
 *
 * Outputs:	S->srt			New smoothed roundtrip time.
 *
 *		S->t1v			How long to wait for an acknowledgement before resending.
 *					Value used when starting timer T1, in seconds.
 *					Here it is dynamically adjusted.
 *
 * Description:	How long should we wait for an ACK before sending again or giving up?
 *		some implementations have a fixed length time.  This is usually the FRACK parameter,
 *		typically 3 seconds (D710A) or 4 seconds (KPC-3+).
 *
 *		This should be increased for each digipeater in the path.
 *		Here it is dynamically adjusted by taking the average time it takes to get a response
 *		and then we double it.
 *
 * Rambling:	It seems like a good idea to adapt to channel conditions, such as digipeater delays,
 *		but it is fraught with peril if you are not careful.
 *
 *		For example, if we accept an incoming connection and only receive some I frames and
 *		send no I frames, T1 never gets started.  In my earlier attempt, 't1_remaining_when_last_stopped'
 *		had the initial value of 0 lacking any reason to set it differently.   The calculation here
 *		then kept pushing t1v up up up.  After receiving 20 I frames and sending none, 
 *		t1v was over 300 seconds!!!
 *
 *		We need some way to indicate that 't1_remaining_when_last_stopped' is not valid and
 *		not to use it.  Rather than adding a new variable, it is set to a negative value
 *		initially to mean it has not been set yet.  That solves one problem.
 *
 *		T1 is paused whenever the channel is busy, either transmitting or receiving,
 *		so the measured time could turn out to be a tiny fraction of a second, much less than
 *		the frame transmission time.
 *		If this gets too low, an unusually long random delay, before the sender's transmission,
 *		could exceed this.  I put in a lower limit for t1v, currently 1 second.
 *
 *		What happens if we get multiple timeouts because we don't get a response?
 *		For example, when we try to connect to a station which is not there, a KPC-3+ will give
 *		up and report failure after 10 tries x 4 sec = 40 seconds.
 *
 *		The algorithm in the AX.25 protocol spec shows increasing timeout values.
 *		It might seem like a good idea but either it was not thought out very well
 *		or I am not understanding it.  If it is doubled each time, it gets awful large
 *		very quickly.   If we try to connect to a station which is not there, 
 *		we want to know within a minute, not an hour later.
 *
 *		Keeping with the spirit of increasing the time but keeping it sane,
 *		I increase the time linearly by a fraction of a second.
 *
 *------------------------------------------------------------------------------*/


static void select_t1_value (ax25_dlsm_t *S)
{
	float old_srt = S->srt;


// Erratum:  I don't think this test for RC == 0 is valid.
// We would need to set RC to 0 whenever we enter state 3 and we don't do that.
// I think a more appropriate test would be to check if we are in state 3.
// When things are going smoothly, it makes sense to fine tune timeout based on smoothed round trip time.
// When in some other state, we might want to slowly increase the time to minimize collisions.
// Maybe the solution is to set RC=0 when we enter state 3.

// TODO: come back and revisit this.

	if (S->rc == 0) {

	  if (S->t1_remaining_when_last_stopped >= 0) {		// Negative means invalid, don't use it.

	    // This is an IIR low pass filter.
	    // Algebraically equivalent to version in AX.25 protocol spec but I think the
	    // original intent is clearer by having 1/8 appear only once. 

	    S->srt = 7./8. * S->srt + 1./8. * ( S->t1v - S->t1_remaining_when_last_stopped );
	  }

	  // We pause T1 when the channel is busy.
	  // This includes both receiving someone else and us transmitting.
	  // This can result in the round trip time going down to almost nothing.
	  // My enhancement is to prevent srt from going below one second so
	  // t1v should never be less than 2 seconds.
	  // When t1v was allowed to go down to 1, we got occastional timeouts
	  // even under ideal conditions, probably due to random CSMA delay time.

	  if (S->srt < 1) {

	    S->srt = 1;

	    // Add another 2 seconds for each digipeater in path.

	    if (S->num_addr > 2) {
	      S->srt += 2 * (S->num_addr - 2);
	    }
	  }

	  S->t1v = S->srt * 2;
	}
	else {
	
	  if (S->t1_had_expired) {

	    // This goes up exponentially if implemented as documented!
	    // For example, if we were trying to connect to a station which is not there, we
	    // would retry after 3, then 8, 16, 32, ...  and not time out for over an hour.
	    // That's ridiculous.   Let's try increasing it by a quarter second each time.
	    // We now give up after about a minute.

	    // NO! S->t1v = powf(2, S->rc+1) * S->srt;

	    S->t1v = S->rc * 0.25 + S->srt * 2;
	  }
	}

	if (s_debug_timers) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("Stream %d: select_t1_value, rc = %d, t1 remaining = %.3f, old srt = %.3f, new srt = %.3f, new t1v = %.3f\n",
		S->stream_id, S->rc, S->t1_remaining_when_last_stopped, old_srt, S->srt, S->t1v);
	}


// See  https://groups.io/g/direwolf/topic/100782658#8542
// Perhaps the demands of file transfer lead to this problem.

// "Temporary" hack.
// Automatic fine tuning of t1v generally works well, but on very rare occasions, it gets wildly out of control.
// Until I have more time to properly diagnose this, add some guardrails so it does not go flying off a cliff.

// The initial value of t1v is frack + frack * 2 (number of digipeateers in path)
// If anything, it should automatically be adjusted down.
// Let's say, something smells fishy if it exceeds twice that initial value.

// TODO: Add some instrumentation to record where this was called from and all the values in the printf below.

#if 1
	if (S->t1v < 0.25 || S->t1v > 2 * (g_misc_config_p->frack * (2 * (S->num_addr - 2) + 1)) ) {
	    INIT_T1V_SRT;
	}
#else
	if (S->t1v < 0.99 || S->t1v > 30) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR?  Stream %d: select_t1_value, rc = %d, t1 remaining = %.3f, old srt = %.3f, new srt = %.3f, Extreme new t1v = %.3f\n",
		S->stream_id, S->rc, S->t1_remaining_when_last_stopped, old_srt, S->srt, S->t1v);
	}
#endif
} /* end select_t1_value */


/*------------------------------------------------------------------------------
 *
 * Name:	set_version_2_0
 *
 * Erratum:	Flow chart refers to T2 which doesn't appear anywhere else.
 * 
 *------------------------------------------------------------------------------*/

static void set_version_2_0 (ax25_dlsm_t *S)
{
	S->srej_enable = srej_none;
	S->modulo = 8;
	S->n1_paclen = g_misc_config_p->paclen;
	S->k_maxframe = g_misc_config_p->maxframe_basic;
	S->n2_retry = g_misc_config_p->retry;

} /* end set_version_2_0 */


/*------------------------------------------------------------------------------
 *
 * Name:	set_version_2_2
 * 
 *------------------------------------------------------------------------------*/

static void set_version_2_2 (ax25_dlsm_t *S)
{
	S->srej_enable = srej_single;	// Start with single.
					// Can be increased to multi with XID exchange.
	S->modulo = 128;
	S->n1_paclen = g_misc_config_p->paclen;
	S->k_maxframe = g_misc_config_p->maxframe_extended;
	S->n2_retry = g_misc_config_p->retry;

} /* end set_version_2_2 */




/*------------------------------------------------------------------------------
 *
 * Name:	is_good_nr
 *
 * Purpose:	Evaluate condition "V(a) <= N(r) <= V(s)" which appears in flow charts
 *		for incoming I, RR, RNR, REJ, and SREJ frames.
 *
 * Inputs:	S		- state machine.  Contains V(a) and V(s).
 *
 *		nr		- N(r) found in the incoming frame.
 *
 * Description:	This determines whether the Received Sequence Number, N(R), is in
 *		the expected range for normal processing or if we have an error 
 *		condition that needs recovery.
 *
 *		This gets tricky due to the wrap around of sequence numbers.
 *
 *		4.2.4.4. Received Sequence Number N(R)
 *
 *		The received sequence number exists in both I and S frames.
 *		Prior to sending an I or S frame, this variable is updated to equal that
 *		of the received state variable, thus implicitly acknowledging the proper
 *		reception of all frames up to and including N(R)-1.
 *
 * Pattern noticed:  Anytime we have "is_good_nr" returning true, we should always
 *			- set V(A) from N(R) or
 *			- call "check_i_frame_ackd" which does the same and some timer stuff.
 *
 *------------------------------------------------------------------------------*/


static int is_good_nr (ax25_dlsm_t *S, int nr)
{
	int adjusted_va, adjusted_nr, adjusted_vs;
	int result;

/* Adjust all values relative to V(a) before comparing so we won't have wrap around. */

#define adjust_by_va(x) (AX25MODULO((x) - S->va, S->modulo, __FILE__, __func__, __LINE__))

	adjusted_va = adjust_by_va(S->va);	// A clever compiler would know it is zero.
	adjusted_nr = adjust_by_va(nr);
	adjusted_vs = adjust_by_va(S->vs);

	result = adjusted_va <= adjusted_nr && adjusted_nr <= adjusted_vs;

	if (s_debug_misc) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("is_good_nr,  V(a) %d <= nr %d <= V(s) %d, returns %d\n", S->va, nr, S->vs, result);
	}

	return (result);

} /* end is_good_nr */



/*------------------------------------------------------------------------------
 *
 * Name:	i_frame_pop_off_queue
 * 
 * Purpose:	Transmit an I frame if we have one in the queue and conditions are right.
 *		This appears two slightly different ways in the flow charts:
 *		"frame pop off queue"
 *		"I frame pops off queue"
 *
 * Inputs:	i_frame_queue		- Remove items from here.
 *		peer_receiver_busy	- If other end not busy.
 *		V(s)			- and we haven't reached window size.
 *		V(a)
 *		k
 *
 * Outputs:	v(s) is incremented for each processed.
 *		acknowledge_pending = 0
 *
 *------------------------------------------------------------------------------*/

static void i_frame_pop_off_queue (ax25_dlsm_t *S)
{


	if (s_debug_misc) {
	  //text_color_set(DW_COLOR_DEBUG);
	  //dw_printf ("i_frame_pop_off_queue () state=%d\n", S->state);
	}

// TODO:  Were we expecting something in the queue?
// or is empty an expected situation?

	if (S->i_frame_queue == NULL) {

	  if (s_debug_misc) {
	    // TODO: add different switch for I frame queue.
	    //text_color_set(DW_COLOR_DEBUG);
	    //dw_printf ("i_frame_pop_off_queue () queue is empty get out, line %d\n", __LINE__);
	  }

	  // I Frame queue is empty.
	  // Nothing to see here, folks.  Move along.
	  return;
	}

	switch (S->state) {

	  case 	state_1_awaiting_connection:
	  case 	state_5_awaiting_v22_connection:

	    if (s_debug_misc) {
	      //text_color_set(DW_COLOR_DEBUG);
	      //dw_printf ("i_frame_pop_off_queue () line %d\n", __LINE__);
	    }

	    // This seems to say remove the I Frame from the queue and discard it if "layer 3 initiated" is set.

	    // For the case of removing it from the queue and putting it back in we just leave it there.

	    // Erratum?  The flow chart seems to be backwards.
	    // It would seem like we want to keep it if we are further along in the connection process.
	    // I don't understand the intention here, and can't make a compelling argument on why it
	    // is backwards, so it is implemented as documented.
	    
	    if (S->layer_3_initiated) {
	      cdata_t *txdata;

	      if (s_debug_misc) {
	        //text_color_set(DW_COLOR_DEBUG);
	        //dw_printf ("i_frame_pop_off_queue () discarding due to L3 init. line %d\n", __LINE__);
	      }
	      txdata = S->i_frame_queue;		// Remove from head of list.
	      S->i_frame_queue = txdata->next;
	      cdata_delete (txdata);
	    }
	    break;

	  case 	state_3_connected:
	  case 	state_4_timer_recovery:

	    if (s_debug_misc) {
	      //text_color_set(DW_COLOR_DEBUG);
	      //dw_printf ("i_frame_pop_off_queue () state %d, line %d\n", S->state, __LINE__);
	    }
    
	    while ( ( ! S->peer_receiver_busy ) &&
	            S->i_frame_queue != NULL &&
	            WITHIN_WINDOW_SIZE(S) ) {

	      cdata_t *txdata;

	      txdata = S->i_frame_queue;		// Remove from head of list.
	      S->i_frame_queue = txdata->next;
	      txdata->next = NULL;

	      cmdres_t cr = cr_cmd;
	      int ns = S->vs;
	      int nr = S->vr;
 	      int p = 0;

	      if (s_debug_misc || s_debug_radio) {
	        //dw_printf ("i_frame_pop_off_queue () ns=%d, queue for transmit \"", ns);
	        //ax25_safe_print (txdata->data, txdata->len, 1);
	        //dw_printf ("\"\n");
	      }
	      packet_t pp = ax25_i_frame (S->addrs, S->num_addr, cr, S->modulo, nr, ns, p, txdata->pid, (unsigned char *)(txdata->data), txdata->len);

	      if (s_debug_misc) {
	        //text_color_set(DW_COLOR_DEBUG);
	        //dw_printf ("calling lm_data_request for I frame, %s line %d\n", __func__, __LINE__);
	      }

	      lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	      // Stash in sent array in case it gets lost and needs to be sent again.

	      if (S->txdata_by_ns[ns] != NULL) {
	        cdata_delete (S->txdata_by_ns[ns]);
	      }
	      S->txdata_by_ns[ns] = txdata;

	      SET_VS(AX25MODULO(S->vs + 1, S->modulo, __FILE__, __func__, __LINE__));		// increment sequence of last sent.

	      S->acknowledge_pending = 0;

// Erratum:  I think we always want to restart T1 when an I frame is sent.
// Otherwise we could time out too soon.
#if 1
	      STOP_T3;
	      START_T1;
#else
	      if ( ! IS_T1_RUNNING) {
	        STOP_T3;
	        START_T1;
	      }
#endif
	    }
	    break;

	  case 	state_0_disconnected:
	  case 	state_2_awaiting_release:

	    // Do nothing.
	    break;
	}

} /* end i_frame_pop_off_queue */




/*------------------------------------------------------------------------------
 *
 * Name:	discard_i_queue
 * 
 * Purpose:	Discard any data chunks waiting to be sent.
 *
 *------------------------------------------------------------------------------*/


static void discard_i_queue (ax25_dlsm_t *S)
{
	cdata_t *t;

	while (S->i_frame_queue != NULL) {

	  t = S->i_frame_queue;
	  S->i_frame_queue = S->i_frame_queue->next;
	  cdata_delete (t);
	}

} /* end discard_i_queue */



/*------------------------------------------------------------------------------
 *
 * Name:	enter_new_state
 * 
 * Purpose:	Switch to new state.
 *
 * Description:	Use a function, rather than setting variable directly, so we have
 *		one common point for debug output and possibly other things we
 *		might want to do at a state change.
 *
 *------------------------------------------------------------------------------*/

// TODO:  requeuing???

static void enter_new_state (ax25_dlsm_t *S, enum dlsm_state_e new_state, const char *from_func, int from_line)
{

	if (s_debug_variables) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n");
	  dw_printf (">>> NEW STATE = %d, previously %d, called from %s %d <<<\n", new_state, S->state, from_func, from_line);
	  dw_printf ("\n");
	}

	assert (new_state >= 0 && new_state <= 5);


	if (( new_state == state_3_connected || new_state == state_4_timer_recovery) &&
	       S->state != state_3_connected &&  S->state != state_4_timer_recovery ) {

	  ptt_set (OCTYPE_CON, S->chan, 1);		// Turn on connected indicator if configured.
	}
	else if (( new_state != state_3_connected && new_state != state_4_timer_recovery) &&
	         (  S->state == state_3_connected ||  S->state == state_4_timer_recovery ) ) {

	  ptt_set (OCTYPE_CON, S->chan, 0);		// Turn off connected indicator if configured.
							// Ideally we should look at any other link state machines
							// for this channel and leave the indicator on if any
							// are connected.  I'm not that worried about it.
	}

	S->state = new_state;

} /* end enter_new_state */



/*------------------------------------------------------------------------------
 *
 * Name:	mdl_negotiate_request
 * 
 * Purpose:	After receiving UA, in response to SABME, this starts up the XID exchange.
 *
 * Description:	Send XID command.
 *		Start timer TM201 so we can retry if timeout waiting for response.
 *		Enter MDL negotiating state.
 *
 *------------------------------------------------------------------------------*/

static void mdl_negotiate_request (ax25_dlsm_t *S)
{
	struct xid_param_s param;
	unsigned char xinfo[40];
	int xlen;
	cmdres_t cmd = cr_cmd;
	int p = 1;
	int nopid = 0;
	packet_t pp;
	int n;

// At least one known [partial] v2.2 implementation understands SABME but not XID.
// Rather than wasting time, sending XID repeatedly until giving up, we have a workaround.
// The configuration file can contain a list of stations known not to respond to XID.
// Obviously this applies only to v2.2 because XID was not part of v2.0.

	for (n = 0; n < g_misc_config_p->noxid_count; n++) {
	  if (strcmp(S->addrs[PEERCALL],g_misc_config_p->noxid_addrs[n]) == 0) {
	    return;
	  }
	}

	switch (S->mdl_state) {

	  case 	mdl_state_0_ready:

	    initiate_negotiation (S, &param);

	    xlen = xid_encode (&param, xinfo, cmd);

	    pp = ax25_u_frame (S->addrs, S->num_addr, cmd, frame_type_U_XID, p, nopid, xinfo, xlen);
	    lm_data_request (S->chan, TQ_PRIO_1_LO, pp);

	    S->mdl_rc = 0;
	    START_TM201;
	    S->mdl_state = mdl_state_1_negotiating;

	    break;

	  case 	mdl_state_1_negotiating:

	    // SDL says "requeue" but I don't understand how it would be useful or how to do it.
	    break;
	}

} /* end mdl_negotiate_request */


/*------------------------------------------------------------------------------
 *
 * Name:	initiate_negotiation
 * 
 * Purpose:	Used when preparing the XID *command*.
 *
 * Description:	Prepare set of parameters to request from the other station.
 *
 *------------------------------------------------------------------------------*/

static void initiate_negotiation (ax25_dlsm_t *S, struct xid_param_s *param)
{
	    param->full_duplex = 0;
	    switch (S->srej_enable) {
	      case srej_single:
	      case srej_multi:
	        param->srej = srej_multi;	// see if other end reconizes it.
	        break;
	      case srej_none:
	      default:
	        param->srej = srej_none;
	        break;
	    }

	    param->modulo = S->modulo;
	    param->i_field_length_rx = S->n1_paclen;	// Hmmmm.  Should we ask for what the user
							// specified for PACLEN or offer the maximum
							// that we can handle, AX25_N1_PACLEN_MAX?
	    param->window_size_rx = S->k_maxframe;
	    param->ack_timer = (int)(g_misc_config_p->frack * 1000);
	    param->retries = S->n2_retry;
}


/*------------------------------------------------------------------------------
 *
 * Name:	negotiation_response
 * 
 * Purpose:	Used when receiving the XID command and preparing the XID response.
 *
 * Description:	Take what other station has asked for and reduce if we have lesser capabilities.
 *		For example if other end wants 8k information part we reduce it to 2k.
 *		Ack time and retries are the opposite, we take the maximum. 
 *
 * Question:	If the other send leaves anything undefined should we leave it
 *		undefined or fill in what we would like before sending it back?
 *
 *		The original version of the protocol spec left this open.
 *		The 2006 revision, in red, says we should fill in defaults for anything
 *		not specified.  This makes sense.  We send back a complete set of parameters
 *		so both ends should agree.
 *
 *------------------------------------------------------------------------------*/

static void negotiation_response (ax25_dlsm_t *S, struct xid_param_s *param)
{

// TODO: Integrate with new full duplex capability in v1.5.

	param->full_duplex = 0;

// Other end might want 8.
// Seems unlikely.  If it implements XID it should have modulo 128.

	if (param->modulo == modulo_unknown) {
	  param->modulo = 8;			// Not specified.  Set default.
	}
	else {
	  param->modulo = MIN(param->modulo, 128);
	}

// We can do REJ or SREJ but won't combine them.
// Erratum:  2006 version, section, 4.3.3.7 says default selective reject - reject.
// We can't do that.

	if (param->srej == srej_not_specified) {
	  param->srej = (param->modulo == 128) ? srej_single : srej_none;	// not specified, set default
	}

// We can currently do up to 2k.
// Take minimum of that and what other guy asks for.

	if (param->i_field_length_rx == G_UNKNOWN) {
	  param->i_field_length_rx = 256;	// Not specified, take default.
	}
	else {
	  param->i_field_length_rx = MIN(param->i_field_length_rx, AX25_N1_PACLEN_MAX);
	}

// In theory extended mode can have window size of 127 but
// I'm limiting it to 63 for the reason mentioned in the SREJ logic.

	if (param->window_size_rx == G_UNKNOWN) {
	  param->window_size_rx = (param->modulo == 128) ? 32 : 4;	// not specified, set default.
	}
	else {
	  if (param->modulo == 128)
	    param->window_size_rx = MIN(param->window_size_rx, AX25_K_MAXFRAME_EXTENDED_MAX);
	  else
	    param->window_size_rx = MIN(param->window_size_rx, AX25_K_MAXFRAME_BASIC_MAX);
	}

// Erratum: Unclear.  Is the Acknowledgement Timer before or after compensating for digipeaters
// in the path?  e.g.  Typically TNCs use the FRACK parameter for this and it often defaults to 3.
// However, the actual timeout value might be something like FRACK*(2*m+1) where m is the number of
// digipeaters in the path.  I'm assuming this is the FRACK value and any additional time, for 
// digipeaters will be added in locally at each end on top of this exchanged value.

	if (param->ack_timer == G_UNKNOWN) {
	  param->ack_timer = 3000;		// not specified, set default.
	}
	else {
	  param->ack_timer = MAX(param->ack_timer, (int)(g_misc_config_p->frack * 1000));
	}

	if (param->retries == G_UNKNOWN) {
	  param->retries = 10;		// not specified, set default.
	}
	else {
	  param->retries = MAX(param->retries, S->n2_retry);
	}

// IMPORTANT:  Take values we have agreed upon and put into my running configuration.

	complete_negotiation(S, param);
}


/*------------------------------------------------------------------------------
 *
 * Name:	complete_negotiation
 * 
 * Purpose:	Used when preparing or receiving the XID *response*.
 *
 * Description:	Take set of parameters which we have agreed upon and apply
 *		to the running configuration.
 *
 * TODO:	Should do some checking here in case other station
 *		sends something crazy.
 *
 *------------------------------------------------------------------------------*/

static void complete_negotiation (ax25_dlsm_t *S, struct xid_param_s *param)
{
	if (param->srej != srej_not_specified) {
	  S->srej_enable = param->srej;
	}

	if (param->modulo != modulo_unknown) {
	  // Disaster if aren't agreeing on this.
	  S->modulo = param->modulo;
	}

	if (param->i_field_length_rx != G_UNKNOWN) {
	  S->n1_paclen = param->i_field_length_rx;
	}

	if (param->window_size_rx != G_UNKNOWN) {
	  S->k_maxframe = param->window_size_rx;
	}

	if (param->ack_timer != G_UNKNOWN) {
	  S->t1v = param->ack_timer * 0.001;
	}

	if (param->retries != G_UNKNOWN) {
	    S->n2_retry = param->retries;
	}
}





//###################################################################################
//###################################################################################
//
//  Timers.
//
//	Start.
//	Stop.
//	Pause (when channel busy) & resume.
//	Is it running?
//	Did it expire before being stopped?
//	When will next one expire?
//
//###################################################################################
//###################################################################################


static void start_t1 (ax25_dlsm_t *S, const char *from_func, int from_line)
{
	double now = dtime_now();

	if (s_debug_timers) {
	  text_color_set(DW_COLOR_DEBUG_TIMER);
	  dw_printf ("Start T1 for t1v = %.3f sec, rc = %d, [now=%.3f] from %s %d\n", S->t1v, S->rc, now - S->start_time, from_func, from_line);
	}

	S->t1_exp = now + S->t1v;
	if (S->radio_channel_busy) {
	  S->t1_paused_at = now;
	}
	else {
	  S->t1_paused_at = 0;
	}
	S->t1_had_expired = 0;

} /* end start_t1 */

	  
static void stop_t1 (ax25_dlsm_t *S, const char *from_func, int from_line)
{
	double now = dtime_now();

	RESUME_T1;		// adjust expire time if paused.

	if (S->t1_exp == 0.0) {
	  // Was already stopped.
	}
	else {
	  S->t1_remaining_when_last_stopped = S->t1_exp - now;
	  if (S->t1_remaining_when_last_stopped < 0) S->t1_remaining_when_last_stopped = 0;
	}

// Normally this would be at the top but we don't know time remaining at that point.

	if (s_debug_timers) {
	  text_color_set(DW_COLOR_DEBUG_TIMER);
	  if (S->t1_exp == 0.0) {
	    dw_printf ("Stop T1. Wasn't running, [now=%.3f] from %s %d\n", now - S->start_time, from_func, from_line);
	  }
	  else {
	    dw_printf ("Stop T1, %.3f remaining, [now=%.3f] from %s %d\n", S->t1_remaining_when_last_stopped, now - S->start_time, from_func, from_line);
	  }
	}

	S->t1_exp = 0.0;		// now stopped.
	S->t1_had_expired = 0;		// remember that it did not expire.

} /* end stop_t1 */


static int is_t1_running (ax25_dlsm_t *S, const char *from_func, int from_line)
{
	int result = S->t1_exp != 0.0;

	if (s_debug_timers) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("is_t1_running?  returns %d\n", result);
	}

	return (result);

} /* end is_t1_running */


static void pause_t1 (ax25_dlsm_t *S, const char *from_func, int from_line)
{

	if (S->t1_exp == 0.0) {
	  // Stopped so there is nothing to do.
	}
	else if (S->t1_paused_at == 0.0) {
	  // Running and not paused.

	  double now = dtime_now();

	  S->t1_paused_at = now;

	  if (s_debug_timers) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Paused T1 with %.3f still remaining, [now=%.3f] from %s %d\n", S->t1_exp - now, now - S->start_time, from_func, from_line);
	  }
	}
	else {
	  if (s_debug_timers) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("T1 error: Didn't expect pause when already paused.\n");
	  }
	}

} /* end pause_t1 */


static void resume_t1 (ax25_dlsm_t *S, const char *from_func, int from_line)
{
	if (S->t1_exp == 0.0) {
	  // Stopped so there is nothing to do.
	}
	else if (S->t1_paused_at == 0.0) {
	  // Running but not paused.
	}
	else {
	  double now = dtime_now();
	  double paused_for_sec = now - S->t1_paused_at;

	  S->t1_exp += paused_for_sec;
	  S->t1_paused_at = 0.0;

	  if (s_debug_timers) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Resumed T1 after pausing for %.3f sec, %.3f still remaining, [now=%.3f]\n", paused_for_sec, S->t1_exp - now, now - S->start_time);
	  }
	}

} /* end resume_t1 */




// T3 is a lot simpler.
// Here we are talking about minutes of inactivity with the peer
// rather than expecting a response within seconds where timing is more critical.
// We don't need to capture remaining time when stopped.
// I don't think there is a need to pause it due to the large time frame.


static void start_t3 (ax25_dlsm_t *S, const char *from_func, int from_line)
{
	double now = dtime_now();

	if (s_debug_timers) {
	  text_color_set(DW_COLOR_DEBUG_TIMER);
	  dw_printf ("Start T3 for %.3f sec, [now=%.3f] from %s %d\n", T3_DEFAULT, now - S->start_time, from_func, from_line);
	}

	S->t3_exp = now + T3_DEFAULT;
}
	  
static void stop_t3 (ax25_dlsm_t *S, const char *from_func, int from_line)
{
	if (s_debug_timers) {
	  double now = dtime_now();

	  text_color_set(DW_COLOR_DEBUG_TIMER);
	  if (S->t3_exp == 0.0) {
	    dw_printf ("Stop T3. Wasn't running.\n");
	  }
	  else {
	    dw_printf ("Stop T3, %.3f remaining, [now=%.3f] from %s %d\n", S->t3_exp - now, now - S->start_time, from_func, from_line);
	  }
	}
	S->t3_exp = 0.0;
}



// TM201 is similar to T1.
// It needs to be paused whent the channel is busy.
// Simpler because we don't need to keep track of time remaining when stopped.



static void start_tm201 (ax25_dlsm_t *S, const char *from_func, int from_line)
{
	double now = dtime_now();

	if (s_debug_timers) {
	  text_color_set(DW_COLOR_DEBUG_TIMER);
	  dw_printf ("Start TM201 for t1v = %.3f sec, rc = %d, [now=%.3f] from %s %d\n", S->t1v, S->rc, now - S->start_time, from_func, from_line);
	}

	S->tm201_exp = now + S->t1v;
	if (S->radio_channel_busy) {
	  S->tm201_paused_at = now;
	}
	else {
	  S->tm201_paused_at = 0;
	}

} /* end start_tm201 */

	  
static void stop_tm201 (ax25_dlsm_t *S, const char *from_func, int from_line)
{
	double now = dtime_now();

	if (s_debug_timers) {
	  text_color_set(DW_COLOR_DEBUG_TIMER);
	  dw_printf ("Stop TM201.  [now=%.3f] from %s %d\n", now - S->start_time, from_func, from_line);
	}

	S->tm201_exp = 0.0;		// now stopped.

} /* end stop_tm201 */



static void pause_tm201 (ax25_dlsm_t *S, const char *from_func, int from_line)
{

	if (S->tm201_exp == 0.0) {
	  // Stopped so there is nothing to do.
	}
	else if (S->tm201_paused_at == 0.0) {
	  // Running and not paused.

	  double now = dtime_now();

	  S->tm201_paused_at = now;

	  if (s_debug_timers) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Paused TM201 with %.3f still remaining, [now=%.3f] from %s %d\n", S->tm201_exp - now, now - S->start_time, from_func, from_line);
	  }
	}
	else {
	  if (s_debug_timers) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("TM201 error: Didn't expect pause when already paused.\n");
	  }
	}

} /* end pause_tm201 */


static void resume_tm201 (ax25_dlsm_t *S, const char *from_func, int from_line)
{
	if (S->tm201_exp == 0.0) {
	  // Stopped so there is nothing to do.
	}
	else if (S->tm201_paused_at == 0.0) {
	  // Running but not paused.
	}
	else {
	  double now = dtime_now();
	  double paused_for_sec = now - S->tm201_paused_at;

	  S->tm201_exp += paused_for_sec;
	  S->tm201_paused_at = 0.0;

	  if (s_debug_timers) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Resumed TM201 after pausing for %.3f sec, %.3f still remaining, [now=%.3f]\n", paused_for_sec, S->tm201_exp - now, now - S->start_time);
	  }
	}

} /* end resume_tm201 */





double ax25_link_get_next_timer_expiry (void)
{
	double tnext = 0;
	ax25_dlsm_t *p;

	for (p = list_head; p != NULL; p = p->next) {
	  
	  // Consider if running and not paused.

	  if (p->t1_exp != 0 && p->t1_paused_at == 0) {
	    if (tnext == 0) {
	      tnext = p->t1_exp;
	    }
	    else if (p->t1_exp < tnext) {
	      tnext = p->t1_exp;
	    }
	  }

	  if (p->t3_exp != 0) {
	    if (tnext == 0) {
	      tnext = p->t3_exp;
	    }
	    else if (p->t3_exp < tnext) {
	      tnext = p->t3_exp;
	    }
	  }

	  if (p->tm201_exp != 0 && p->tm201_paused_at == 0) {
	    if (tnext == 0) {
	      tnext = p->tm201_exp;
	    }
	    else if (p->tm201_exp < tnext) {
	      tnext = p->tm201_exp;
	    }
	  }

	}

	if (s_debug_timers > 1) {
	  text_color_set(DW_COLOR_DEBUG);
	  if (tnext == 0.0) {
	    dw_printf ("ax25_link_get_next_timer_expiry returns none.\n");
	  }
	  else {
	    dw_printf ("ax25_link_get_next_timer_expiry returns %.3f sec from now.\n",
				tnext - dtime_now());
	  }
	}

	return (tnext);

} /* end ax25_link_get_next_timer_expiry */


/* end ax25_link.c */
