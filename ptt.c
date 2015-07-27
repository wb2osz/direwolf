//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2013,2014  John Langner, WB2OSZ
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



/*------------------------------------------------------------------
 *
 * Module:      ptt.c
 *
 * Purpose:   	Activate the push to talk (PTT) signal to turn on transmitter.
 *		
 * Description:	Traditionally this is done with the RTS signal of the serial port.
 *
 *		If we have two radio channels and only one serial port, DTR
 *		can be used for the second channel.
 *
 *		If __WIN32__ is defined, we use the Windows interface.
 *		Otherwise we use the unix version suitable for either Cygwin or Linux.
 *
 * Version 0.9:	Add ability to use GPIO pins on Linux.
 *
 * Version 1.1: Add parallel printer port for x86 Linux only.
 *
 * References:	http://www.robbayer.com/files/serial-win.pdf
 *
 *		https://www.kernel.org/doc/Documentation/gpio.txt
 *
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>

#if __WIN32__
#include <windows.h>
#else
#include <sys/termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/* So we can have more common code for fd. */
typedef int HANDLE;
#define INVALID_HANDLE_VALUE (-1)

#endif

#include "direwolf.h"
#include "textcolor.h"
#include "audio.h"
#include "ptt.h"


#if __WIN32__

#define RTS_ON(fd) 	EscapeCommFunction(fd,SETRTS);
#define RTS_OFF(fd) 	EscapeCommFunction(fd,CLRRTS);
#define DTR_ON(fd)    	EscapeCommFunction(fd,SETDTR);
#define DTR_OFF(fd)	EscapeCommFunction(fd,CLRDTR);

#else

#define RTS_ON(fd) 	{ int stuff; ioctl (fd, TIOCMGET, &stuff); stuff |= TIOCM_RTS;  ioctl (fd, TIOCMSET, &stuff); }
#define RTS_OFF(fd) 	{ int stuff; ioctl (fd, TIOCMGET, &stuff); stuff &= ~TIOCM_RTS; ioctl (fd, TIOCMSET, &stuff); }
#define DTR_ON(fd)    	{ int stuff; ioctl (fd, TIOCMGET, &stuff); stuff |= TIOCM_DTR;  ioctl (fd, TIOCMSET, &stuff); }
#define DTR_OFF(fd)	{ int stuff; ioctl (fd, TIOCMGET, &stuff); stuff &= ~TIOCM_DTR;	ioctl (fd, TIOCMSET, &stuff); }

#endif

#define LPT_IO_ADDR 0x378


#if TEST
#define dw_printf printf
#endif



/*-------------------------------------------------------------------
 *
 * Name:        ptt_init
 *
 * Purpose:    	Open serial port(s) used for PTT signals and set to proper state.
 *
 * Inputs:	modem		- Structure with communication parameters.
 *
 *
 * Outputs:	Remember required information for future use.
 *
 * Description:	
 *
 *--------------------------------------------------------------------*/

static int ptt_num_channels;

static ptt_method_t ptt_method[MAX_CHANS];	/* Method for PTT signal. */
					/* PTT_METHOD_NONE - not configured.  Could be using VOX. */
					/* PTT_METHOD_SERIAL - serial (com) port. */
					/* PTT_METHOD_GPIO - general purpose I/O. */
					/* PTT_METHOD_LPT - Parallel printer port. */

static char ptt_device[MAX_CHANS][20];	/* Name of serial port device.  */
					/* e.g. COM1 or /dev/ttyS0. */

static ptt_line_t ptt_line[MAX_CHANS];	/* RTS or DTR when using serial port. */

static int ptt_gpio[MAX_CHANS];		/* GPIO number.  Only used for Linux. */
					/* Valid only when ptt_method is PTT_METHOD_GPIO. */
		
int ptt_lpt_bit[MAX_CHANS];		/* Bit number for parallel printer port.  */
					/* Bit 0 = pin 2, ..., bit 7 = pin 9. */
					/* Valid only when ptt_method is PTT_METHOD_LPT. */

static int ptt_invert[MAX_CHANS];	/* Invert the signal.  */
					/* Normally higher voltage means transmit. */

static HANDLE ptt_fd[MAX_CHANS];	/* Serial port handle or fd.  */
					/* Could be the same for two channels */	
					/* if using both RTS and DTR. */



void ptt_init (struct audio_s *p_modem)
{
	int ch;
	HANDLE fd;
#if __WIN32__
#else
	int using_gpio;
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("ptt_init ( ... )\n");
#endif

/*
 * First copy everything from p_modem to local variables
 * so it is available for later use.
 *
 * Maybe all the PTT stuff should have its own structure.
 */

	ptt_num_channels = p_modem->num_channels;

	assert (ptt_num_channels >= 1 && ptt_num_channels <= MAX_CHANS);

	for (ch=0; ch<ptt_num_channels; ch++) {

	  ptt_method[ch] = p_modem->ptt_method[ch];
	  strcpy (ptt_device[ch], p_modem->ptt_device[ch]);
	  ptt_line[ch] = p_modem->ptt_line[ch];
	  ptt_gpio[ch] = p_modem->ptt_gpio[ch];
	  ptt_lpt_bit[ch] = p_modem->ptt_lpt_bit[ch];
	  ptt_invert[ch] = p_modem->ptt_invert[ch];
	  ptt_fd[ch] = INVALID_HANDLE_VALUE;
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
          dw_printf ("ch=%d, method=%d, device=%s, line=%d, gpio=%d, lpt_bit=%d, invert=%d\n",
		ch,
		ptt_method[ch], 
		ptt_device[ch],
		ptt_line[ch],
		ptt_gpio[ch],
		ptt_lpt_bit[ch],
		ptt_invert[ch]);
#endif
	}

/*
 * Set up serial ports.
 */

	for (ch=0; ch<ptt_num_channels; ch++) {

	  if (ptt_method[ch] == PTT_METHOD_SERIAL) {

#if __WIN32__
#else
	    /* Translate Windows device name into Linux name. */
	    /* COM1 -> /dev/ttyS0, etc. */

	    if (strncasecmp(ptt_device[ch], "COM", 3) == 0) {
	      int n = atoi (ptt_device[ch] + 3);
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Converted PTT device '%s'", ptt_device[ch]);
	      if (n < 1) n = 1;
	      sprintf (ptt_device[ch], "/dev/ttyS%d", n-1);
	      dw_printf (" to Linux equivalent '%s'\n", ptt_device[ch]);
	    }
#endif
	    /* Can't open the same device more than once so we */
	    /* need more logic to look for the case of both radio */
	    /* channels using different pins of the same COM port. */

	    /* TODO: Needs to be rewritten in a more general manner */
	    /* if we ever have more than 2 channels. */

	    if (ch == 1 && strcmp(ptt_device[0],ptt_device[1]) == 0) {
	      fd = ptt_fd[0];
	    }
	    else {
#if __WIN32__
	      char bettername[50];
	      // Bug fix in release 1.1 - Need to munge name for COM10 and up.
	      // http://support.microsoft.com/kb/115831

	      strcpy (bettername, ptt_device[ch]);
	      if (strncasecmp(bettername, "COM", 3) == 0) {
	        int n;
	        n = atoi(bettername+3);
	        if (n >= 10) {
	          strcpy (bettername, "\\\\.\\");
	          strcat (bettername, ptt_device[ch]);
	        }
	      }
	      fd = CreateFile(bettername,
			GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
#else

		/* O_NONBLOCK added in version 0.9. */
		/* https://bugs.launchpad.net/ubuntu/+source/linux/+bug/661321/comments/12 */

	      fd = open (ptt_device[ch], O_RDONLY | O_NONBLOCK);
#endif
            }

	    if (fd != INVALID_HANDLE_VALUE) {
	      ptt_fd[ch] = fd;
	    }
	    else {
#if __WIN32__
#else
	      int e = errno;
#endif
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("ERROR can't open device %s for channel %d PTT control.\n",
			ptt_device[ch], ch);
#if __WIN32__
#else
	      dw_printf ("%s\n", strerror(errno));
#endif
	      /* Don't try using it later if device open failed. */

	      ptt_method[ch] = PTT_METHOD_NONE;
	    }

/*
 * Set initial state of PTT off.
 * ptt_set will invert output signal if appropriate.
 */	  
	    ptt_set (ch, 0);

	  } 	/* if serial method. */

	}	/* For each channel. */


/* 
 * Set up GPIO - for Linux only.
 */

#if __WIN32__
#else

/*
 * Does any channel use GPIO?
 */

	using_gpio = 0;
	for (ch=0; ch<ptt_num_channels; ch++) {
	  if (ptt_method[ch] == PTT_METHOD_GPIO) {
	    using_gpio = 1;
	  }
	}

	if (using_gpio) {
	
	  struct stat finfo;
/*
 * Normally the device nodes are set up for access 
 * only by root.  Try to change it here so we don't
 * burden user with another configuration step.
 *
 * Does /sys/class/gpio/export even exist?
 */

	  if (stat("/sys/class/gpio/export", &finfo) < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("This system is not configured with the GPIO user interface.\n");
	    dw_printf ("Use a different method for PTT control.\n");
	    exit (1);
	  }

/*
 * Do we have permission to access it?
 *
 *	pi@raspberrypi /sys/class/gpio $ ls -l
 *	total 0
 *	--w------- 1 root root 4096 Aug 20 07:59 export
 *	lrwxrwxrwx 1 root root    0 Aug 20 07:59 gpiochip0 -> ../../devices/virtual/gpio/gpiochip0
 *	--w------- 1 root root 4096 Aug 20 07:59 unexport
 */
	  if (geteuid() != 0) {
	    if ( ! (finfo.st_mode & S_IWOTH)) {
	      int err;

	      /* Try to change protection. */
	      err = system ("sudo chmod go+w /sys/class/gpio/export /sys/class/gpio/unexport");
	      
	      if (stat("/sys/class/gpio/export", &finfo) < 0) {
	        /* Unexpected because we could do it before. */
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("This system is not configured with the GPIO user interface.\n");
	        dw_printf ("Use a different method for PTT control.\n");
	        exit (1);
	      }

	      /* Did we succeed in changing the protection? */
	      if ( ! (finfo.st_mode & S_IWOTH)) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Permissions do not allow ordinary users to access GPIO.\n");
	        dw_printf ("Log in as root and type this command:\n");
	        dw_printf ("    chmod go+w /sys/class/gpio/export /sys/class/gpio/unexport\n");
	        exit (1);
	      }	   
	    }
	  }
	}
/*
 * We should now be able to create the device nodes for 
 * the pins we want to use.
 */
	    
	for (ch=0; ch<ptt_num_channels; ch++) {
	  if (ptt_method[ch] == PTT_METHOD_GPIO) {
	    char stemp[80];
	    struct stat finfo;
	    int err;

	    fd = open("/sys/class/gpio/export", O_WRONLY);
	    if (fd < 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Permissions do not allow ordinary users to access GPIO.\n");
	      dw_printf ("Log in as root and type this command:\n");
	      dw_printf ("    chmod go+w /sys/class/gpio/export /sys/class/gpio/unexport\n");
	      exit (1);
	    }
	    sprintf (stemp, "%d", ptt_gpio[ch]);
	    if (write (fd, stemp, strlen(stemp)) != strlen(stemp)) {
	      int e = errno;
	      /* Ignore EBUSY error which seems to mean */
	      /* the device node already exists. */
	      if (e != EBUSY) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Error writing \"%s\" to /sys/class/gpio/export, errno=%d\n", stemp, e);
	        dw_printf ("%s\n", strerror(e));
	        exit (1);
	      }
	    }
	    close (fd);

/*
 * We will have the same permission problem if not root.
 * We only care about "direction" and "value".
 */
	    sprintf (stemp, "sudo chmod go+rw /sys/class/gpio/gpio%d/direction", ptt_gpio[ch]);
	    err = system (stemp);
	    sprintf (stemp, "sudo chmod go+rw /sys/class/gpio/gpio%d/value", ptt_gpio[ch]);
	    err = system (stemp);

	    sprintf (stemp, "/sys/class/gpio/gpio%d/value", ptt_gpio[ch]);

	    if (stat(stemp, &finfo) < 0) {
	      int e = errno;
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Failed to get status for %s \n", stemp);
	      dw_printf ("%s\n", strerror(e));
	      exit (1);
	    }

	    if (geteuid() != 0) {
	      if ( ! (finfo.st_mode & S_IWOTH)) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Permissions do not allow ordinary users to access GPIO.\n");
	        dw_printf ("Log in as root and type these commands:\n");
	        dw_printf ("    chmod go+rw /sys/class/gpio/gpio%d/direction", ptt_gpio[ch]);
	        dw_printf ("    chmod go+rw /sys/class/gpio/gpio%d/value", ptt_gpio[ch]);
	        exit (1);
	      }
	    }

/*
 * Set output direction with initial state off.
 */

	    sprintf (stemp, "/sys/class/gpio/gpio%d/direction", ptt_gpio[ch]);
	    fd = open(stemp, O_WRONLY);
	    if (fd < 0) {
	      int e = errno;
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Error opening %s\n", stemp);
	      dw_printf ("%s\n", strerror(e));
	      exit (1);
	    }

	    char hilo[8];
	    if (ptt_invert[ch]) {
	      strcpy (hilo, "high");
	    }
	    else {
	      strcpy (hilo, "low");
	    }
	    if (write (fd, hilo, strlen(hilo)) != strlen(hilo)) {
	      int e = errno;
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Error writing initial state to %s\n", stemp);
	      dw_printf ("%s\n", strerror(e));
	      exit (1);
	    }
	    close (fd);
	  }
	}
#endif



/*
 * Set up parallel printer port.
 * Hardcoded for single port.
 * For x86 Linux only.
 */

#if  ( defined(__i386__) || defined(__x86_64__) ) && ( defined(__linux__) || defined(__unix__) )

	for (ch=0; ch<ptt_num_channels; ch++) {

	  if (ptt_method[ch] == PTT_METHOD_LPT) {


	    /* Can't open the same device more than once so we */
	    /* need more logic to look for the case of both radio */
	    /* channels using different pins of the same LPT port. */

	    /* TODO: Needs to be rewritten in a more general manner */
	    /* if we ever have more than 2 channels. */

	    if (ch == 1 && strcmp(ptt_device[0],ptt_device[1]) == 0) {
	      fd = ptt_fd[0];
	    }
	    else {
	      fd = open ("/dev/port", O_RDWR | O_NDELAY);
            }

	    if (fd != INVALID_HANDLE_VALUE) {
	      ptt_fd[ch] = fd;
	    }
	    else {

	      int e = errno;

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("ERROR - Can't open /dev/port for parallel printer port PTT control.\n");
	      dw_printf ("%s\n", strerror(errno));
	      dw_printf ("You probably don't have adequate permissions to access I/O ports.\n");
	      dw_printf ("Either run direwolf as root or change these permissions:\n");
	      dw_printf ("  sudo chmod go+rw /dev/port\n");
	      dw_printf ("  sudo setcap cap_sys_rawio=ep `which direwolf`\n");

	      /* Don't try using it later if device open failed. */

	      ptt_method[ch] = PTT_METHOD_NONE;
	    }

/*
 * Set initial state of PTT off.
 * ptt_set will invert output signal if appropriate.
 */	  
	    ptt_set (ch, 0);

	  } 	/* if parallel printer port method. */

	}	/* For each channel. */



#endif /* x86 Linux */


/* Why doesn't it transmit?  Probably forgot to specify PTT option. */

	for (ch=0; ch<ptt_num_channels; ch++) {
	  if(ptt_method[ch] == PTT_METHOD_NONE) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Note: PTT not configured for channel %d. (Ignore this if using VOX.)\n", ch);
	  }
	}
} /* end ptt_init */


/*-------------------------------------------------------------------
 *
 * Name:        ptt_set
 *
 * Purpose:    	Turn transmitter on or off.
 *
 * Inputs:	chan	channel, 0 .. (number of channels)-1
 *
 *		ptt	1 for transmit, 0 for receive.
 *
 *
 * Assumption:	ptt_init was called first.
 *
 * Description:	Set the RTS or DTR line or GPIO pin.
 *		More positive output means transmit unless invert is set.
 *
 *--------------------------------------------------------------------*/


void ptt_set (int chan, int ptt)
{

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("ptt_set ( %d, %d )\n", chan, ptt);
#endif

/* 
 * Inverted output? 
 */

	if (ptt_invert[chan]) {
	  ptt = ! ptt;
	}

	assert (chan >= 0 && chan < MAX_CHANS);

/*
 * Using serial port?
 */
	if (ptt_method[chan] == PTT_METHOD_SERIAL && 
		ptt_fd[chan] != INVALID_HANDLE_VALUE) {

	  if (ptt_line[chan] == PTT_LINE_RTS) {

	    if (ptt) {
	      RTS_ON(ptt_fd[chan]);
	    }
	    else {
	      RTS_OFF(ptt_fd[chan]);
	    }
	  }
	  else if (ptt_line[chan] == PTT_LINE_DTR) {

	    if (ptt) {
	      DTR_ON(ptt_fd[chan]);
	    }
	    else {
	      DTR_OFF(ptt_fd[chan]);
	    }
	  }
	}

/*
 * Using GPIO? 
 */

#if __WIN32__
#else

	if (ptt_method[chan] == PTT_METHOD_GPIO) {
	  int fd;
	  char stemp[80];

	  sprintf (stemp, "/sys/class/gpio/gpio%d/value", ptt_gpio[chan]);

	  fd = open(stemp, O_WRONLY);
	  if (fd < 0) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error opening %s to set PTT signal.\n", stemp);
	    dw_printf ("%s\n", strerror(e));
	    return;
	  }

	  sprintf (stemp, "%d", ptt);

	  if (write (fd, stemp, 1) != 1) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error setting GPIO %d for PTT\n", ptt_gpio[chan]);
	    dw_printf ("%s\n", strerror(e));
	  }
	  close (fd);

	}
#endif
	
/*
 * Using parallel printer port?
 */

#if  ( defined(__i386__) || defined(__x86_64__) ) && ( defined(__linux__) || defined(__unix__) )

	if (ptt_method[chan] == PTT_METHOD_LPT && 
		ptt_fd[chan] != INVALID_HANDLE_VALUE) {

	  char lpt_data;
	  ssize_t n;		

	  lseek (ptt_fd[chan], (off_t)LPT_IO_ADDR, SEEK_SET);
	  if (read (ptt_fd[chan], &lpt_data, (size_t)1) != 1) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error reading current state of LPT for channel %d PTT\n", chan);
	    dw_printf ("%s\n", strerror(e));
	  }

	  if (ptt) {
	    lpt_data |= ( 1 << ptt_lpt_bit[chan] );
	  }
	  else {
	    lpt_data &= ~ ( 1 << ptt_lpt_bit[chan] );
	  }

	  lseek (ptt_fd[chan], (off_t)LPT_IO_ADDR, SEEK_SET);
	  if (write (ptt_fd[chan], &lpt_data, (size_t)1) != 1) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error writing to LPT for channel %d PTT\n", chan);
	    dw_printf ("%s\n", strerror(e));
	  }
	}

#endif /* x86 Linux */


} /* end ptt_set */



/*-------------------------------------------------------------------
 *
 * Name:        ptt_term
 *
 * Purpose:    	Make sure PTT is turned off when we exit.
 *
 * Inputs:	none
 *
 * Description:	
 *
 *--------------------------------------------------------------------*/

void ptt_term (void)
{
	int n;

	for (n = 0; n < ptt_num_channels; n++) {
	  ptt_set (n, 0);
	}

	for (n = 0; n < ptt_num_channels; n++) {
	  if (ptt_fd[n] != INVALID_HANDLE_VALUE) {
#if __WIN32__
	    CloseHandle (ptt_fd[n]);
#else
	    close(ptt_fd[n]);
#endif
	    ptt_fd[n] = INVALID_HANDLE_VALUE;
	  }
	}
}




/*
 * Quick stand-alone test for above.
 *
 *    gcc -DTEST -o ptest ptt.c ; ./ptest
 *
 */


#if TEST

void text_color_set (dw_color_t c)  {  }

#define dw_printf printf

main ()
{
	struct audio_s modem;
	int n;
	int chan;

	memset (&modem, 0, sizeof(modem));

	modem.num_channels = 2;

	modem.ptt_method[0] = PTT_METHOD_SERIAL;
	//strcpy (modem.ptt_device[0], "COM1");
	strcpy (modem.ptt_device[0], "/dev/ttyUSB0");
	modem.ptt_line[0] = PTT_LINE_RTS;

	modem.ptt_method[1] = PTT_METHOD_SERIAL;
	//strcpy (modem.ptt_device[1], "COM1");
	strcpy (modem.ptt_device[1], "/dev/ttyUSB0");
	modem.ptt_line[1] = PTT_LINE_DTR;


/* initialize - both off */

	ptt_init (&modem);

	SLEEP_SEC(2);

/* flash each a few times. */

	dw_printf ("turn on RTS a few times...\n");

	chan = 0;
	for (n=0; n<3; n++) {
	  ptt_set (chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (chan, 0);
	  SLEEP_SEC(1);
	}

	dw_printf ("turn on DTR a few times...\n");

	chan = 1;
	for (n=0; n<3; n++) {
	  ptt_set (chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (chan, 0);
	  SLEEP_SEC(1);
	}

	ptt_term();

/* Same thing again but invert RTS. */

	modem.ptt_invert[0] = 1;

	ptt_init (&modem);

	SLEEP_SEC(2);

	dw_printf ("INVERTED -  RTS a few times...\n");

	chan = 0;
	for (n=0; n<3; n++) {
	  ptt_set (chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (chan, 0);
	  SLEEP_SEC(1);
	}

	dw_printf ("turn on DTR a few times...\n");

	chan = 1;
	for (n=0; n<3; n++) {
	  ptt_set (chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (chan, 0);
	  SLEEP_SEC(1);
	}

	ptt_term ();


/* Test GPIO */

#if __arm__

	memset (&modem, 0, sizeof(modem));
	modem.num_channels = 1;
	modem.ptt_method[0] = PTT_METHOD_GPIO;
	modem.ptt_gpio[0] = 25;

	dw_printf ("Try GPIO %d a few times...\n", modem.ptt_gpio[0]);

	ptt_init (&modem);

	SLEEP_SEC(2);
	chan = 0;
	for (n=0; n<3; n++) {
	  ptt_set (chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (chan, 0);
	  SLEEP_SEC(1);
	}

	ptt_term ();
#endif


	memset (&modem, 0, sizeof(modem));
	modem.num_channels = 2;
	modem.ptt_method[0] = PTT_METHOD_LPT;
	modem.ptt_lpt_bit[0] = 0;
	modem.ptt_method[1] = PTT_METHOD_LPT;
	modem.ptt_lpt_bit[1] = 1;

	dw_printf ("Try LPT bits 0 & 1 a few times...\n");

	ptt_init (&modem);

	for (n=0; n<8; n++) {
	  ptt_set (0, n & 1);
	  ptt_set (1, (n>>1) & 1);
	  SLEEP_SEC(1);
	}

	ptt_term ();	

/* Parallel printer port. */

#if  ( defined(__i386__) || defined(__x86_64__) ) && ( defined(__linux__) || defined(__unix__) )



#endif


}

#endif

/* end ptt.c */



