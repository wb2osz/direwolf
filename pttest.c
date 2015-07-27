

/*------------------------------------------------------------------
 *
 * Module:      pttest.c
 *
 * Purpose:   	Test for pseudo terminal.
 *		
 * Input:	
 *
 * Outputs:	  
 *
 * Description:	The protocol name is an acronym for Keep it Simple Stupid.
 *		You would expect it to be simple but this caused a lot
 *		of effort on Linux.  The problem is that writes to a pseudo
 *		terminal eventually block if nothing at the other end 
 *		is removing the data.  This causes the application to
 *		hang and stop receiving after a while.
 *
 *		This is an attempt to demonstrate the problem in a small
 *		test case and, hopefully, find a solution.
 *	
 *
 * Instructions:
 *		First compile like this:
 *
 *
 *		Run it, noting the name of pseudo terminal, 
 *		typically /dev/pts/1.
 *
 *		In another window, type:
 *
 *			cat /dev/pts/1
 *
 *		This should run "forever" as long as something is
 *		reading from the slave side of the pseudo terminal.
 *
 *		If nothing is removing the data, this runs for a while
 *		and then blocks on the write.
 *		For this particular application we just want to discard
 *		excess data if no one is listening.
 *
 *
 * Failed experiments:
 *
 * 		Notice that ??? always returns 0 for amount of data
 *		in the queue.  
 *		Define TEST1 to make the device non-blocking.
 *		Write fails entirely.
 *
 *		Define TEST2 to use a different method.
 *		Also fails in the same way.
 *
 *		
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>


#define __USE_XOPEN2KXSI 1
#define __USE_XOPEN 1
//#define __USE_POSIX 1
#include <stdlib.h>

#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include <assert.h>
#include <string.h>

//#include "direwolf.h"
//#include "tq.h"
//#include "ax25_pad.h"
//#include "textcolor.h"
//#include "kiss.h"
//#include "xmit.h"



static MYFDTYPE pt_master_fd = -1;	/* File descriptor for my end. */

static MYFDTYPE pt_slave_fd = -1;	/* File descriptor for pseudo terminal */
						/* for use by application. */
static int msg_number;
static int total_bytes;


/*-------------------------------------------------------------------
 *
 * Name:        kiss_init
 *
 * Purpose:     Set up a pseudo terminal acting as a virtual KISS TNC.
 *		
 *
 * Inputs:	mc->nullmodem	- name of device for our end of nullmodem.
 *
 * Outputs:	
 *
 * Description:	(1) Create a pseudo terminal for the client to use.
 *		(2) Start a new thread to listen for commands from client app
 *		    so the main application doesn't block while we wait.
 *
 *
 *--------------------------------------------------------------------*/

static int kiss_open_pt (void);


main (int argc, char *argv)
{

	pt_master_fd = kiss_open_pt ();
	printf ("msg  total  qcount\n");
	
	msg_number = 0;
	total_bytes = 0;

#endif
}


/*
 * Returns fd for master side of pseudo terminal or MYFDERROR for error.
 */


static int kiss_open_pt (void)
{
	int fd;
	char *slave_device;
	struct termios ts;
	int e;
	int flags;

	fd = posix_openpt(O_RDWR|O_NOCTTY);

	if (fd == -1
	    || grantpt (fd) == -1
	    || unlockpt (fd) == -1
	    || (slave_device = ptsname (fd)) == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  printf ("ERROR - Could not create pseudo terminal.\n");
	  return (-1);
	}


	e = tcgetattr (fd, &ts);
	if (e != 0) { 
	  printf ("Can't get pseudo terminal attributes, err=%d\n", e);
	  perror ("pt tcgetattr"); 
	}

	cfmakeraw (&ts);
	
	ts.c_cc[VMIN] = 1;	/* wait for at least one character */
	ts.c_cc[VTIME] = 0;	/* no fancy timing. */
				

	e = tcsetattr (fd, TCSANOW, &ts);
	if (e != 0) { 
	  text_color_set(DW_COLOR_ERROR);
	  printf ("Can't set pseudo terminal attributes, err=%d\n", e);
	  perror ("pt tcsetattr"); 
	}

/*
 * After running for a while on Linux, the write eventually
 * blocks if no one is reading from the other side of
 * the pseudo terminal.  We get stuck on the kiss data
 * write and reception stops.
 *
 * I tried using ioctl(,TIOCOUTQ,) to see how much was in 
 * the queue but that always returned zero.  (Ubuntu)
 *
 * Let's try using non-blocking writes and see if we get
 * the EWOULDBLOCK status instead of hanging.
 */

#if TEST1 	
	// this is worse. all writes fail. errno = ? bad file descriptor
	flags = fcntl(fd, F_GETFL, 0);
	e = fcntl (fd, F_SETFL, flags | O_NONBLOCK);
	if (e != 0) { 
	  printf ("Can't set pseudo terminal to nonblocking, fcntl returns %d, errno = %d\n", e, errno);
	  perror ("pt fcntl"); 
	}
#endif
#if TEST2  
	// same  
	flags = 1;	
	e = ioctl (fd, FIONBIO, &flags);
	if (e != 0) { 
	  printf ("Can't set pseudo terminal to nonblocking, ioctl returns %d, errno = %d\n", e, errno);
	  perror ("pt ioctl"); 
	}
#endif

	printf("Virtual KISS TNC is available on %s\n", slave_device);
	

	// Sample code shows this. Why would we open it here?

	// pt_slave_fd = open(slave_device, O_RDWR|O_NOCTTY);


	return (fd);
}



/*-------------------------------------------------------------------
 *
 * Name:        kiss_send_rec_packet
 *
 * Purpose:     Send a received packet to the client app.
 *
 * Inputs:	chan		- Channel number where packet was received.
 *				  0 = first, 1 = second if any.
 *
 *		pp		- Identifier for packet object.
 *
 *		fbuf		- Address of raw received frame buffer.
 *		flen		- Length of raw received frame.
 *				  Not including the FCS.
 *		
 *
 * Description:	Send message to client.
 *		We really don't care if anyone is listening or not.
 *		I don't even know if we can find out.
 *
 *
 *--------------------------------------------------------------------*/


void kiss_send_rec_packet (void)
{


	char kiss_buff[100];

	int kiss_len;
	int q_count = 123;


	int j;

	strcpy (kiss_buff, "The quick brown fox jumps over the lazy dog.\n");
	kiss_len = strlen(kiss_buff);


	if (pt_master_fd != MYFDERROR) {
	  int err;

	  msg_number++;
	  total_bytes += kiss_len;

//#if DEBUG
	  printf ("%3d  %5d  %5d\n", msg_number, total_bytes, q_count);
//#endif
          err = write (pt_master_fd, kiss_buff, kiss_len);

	  if (err == -1 && errno == EWOULDBLOCK) {
//#if DEBUG 
	    printf ("Discarding message because write would block.\n");
//#endif
	  }
	  else if (err != kiss_len)
	  {
	    printf ("\nError sending message on pseudo terminal.  len=%d, write returned %d, errno = %d\n\n",
		kiss_len, err, errno);
	    perror ("pt write"); 
	  }

	}
//#endif


	
}


/* end pttest.c */
