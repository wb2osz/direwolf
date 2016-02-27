//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2015, 2016  John Langner, WB2OSZ
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
 * Purpose:   	Activate the output control lines for push to talk (PTT) and other purposes.
 *		
 * Description:	Traditionally this is done with the RTS signal of the serial port.
 *
 *		If we have two radio channels and only one serial port, DTR
 *		can be used for the second channel.
 *
 *		If __WIN32__ is defined, we use the Windows interface.
 *		Otherwise we use the Linux interface.
 *
 * Version 0.9:	Add ability to use GPIO pins on Linux.
 *
 * Version 1.1: Add parallel printer port for x86 Linux only.
 *
 *		This is hardcoded to use the primary motherboard parallel
 *		printer port at I/O address 0x378.  This might work with
 *		a PCI card configured to use the same address if the 
 *		motherboard does not have a built in parallel port.
 *		It won't work with a USB-to-parallel-printer-port adapter.
 *
 * Version 1.2: More than two radio channels.
 *		Generalize for additional signals besides PTT.
 *
 * Version 1.3:	HAMLIB support.
 *
 * References:	http://www.robbayer.com/files/serial-win.pdf
 *
 *		https://www.kernel.org/doc/Documentation/gpio.txt
 *
 *---------------------------------------------------------------*/

/*
	Idea for future enhancement:

	A growing number of people have been asking about support for the DMK URI.
	This uses a C-Media CM108/CM119 with one interesting addition, a GPIO
	pin is used to drive PTT.  Here is some related information.

	DMK URI:

		http://www.dmkeng.com/URI_Order_Page.htm
		http://dmkeng.com/images/URI%20Schematic.pdf
		http://www.repeater-builder.com/voip/pdf/cm119-datasheet.pdf

	Homebrew versions of the same idea:

		http://images.ohnosec.org/usbfob.pdf
		http://www.qsl.net/kb9mwr/projects/voip/usbfob-119.pdf
		http://rtpdir.weebly.com/uploads/1/6/8/7/1687703/usbfob.pdf
		http://www.repeater-builder.com/projects/fob/USB-Fob-Construction.pdf

	Applications that have support for this:

		http://docs.allstarlink.org/drupal/
		http://soundmodem.sourcearchive.com/documentation/0.16-1/ptt_8c_source.html
		https://github.com/N0NB/hamlib/blob/master/src/cm108.c#L190

	Information about the "hidraw" device:

		http://unix.stackexchange.com/questions/85379/dev-hidraw-read-permissions
		http://www.signal11.us/oss/udev/
		http://www.signal11.us/oss/hidapi/
		https://github.com/signal11/hidapi/blob/master/libusb/hid.c
		http://stackoverflow.com/questions/899008/howto-write-to-the-gpio-pin-of-the-cm108-chip-in-linux
		https://www.kernel.org/doc/Documentation/hid/hidraw.txt

	In version 1.3, we add HAMLIB support which should be able to do this.
 	(Linux only & haven't verified that it actually works yet!)
	
	Might want to have CM108 GPIO support built in, someday, for simpler building & configuration.
	Maybe even for Windows.  ;-)
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

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

#ifdef USE_HAMLIB
#include <hamlib/rig.h>
#endif

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

#define LPT_IO_ADDR 0x378

#endif


#if TEST
#define dw_printf printf
#endif


static int ptt_debug_level = 0;

void ptt_set_debug(int debug)
{
	ptt_debug_level = debug;
}

/*-------------------------------------------------------------------
 *
 * Name:	export_gpio
 *
 * Purpose:	Tell the GPIO subsystem to export a GPIO line for
 * 	us to use, and set the initial state of the GPIO.
 *
 * Inputs:	gpio		- GPIO line to export
 *		invert:		- Is the GPIO active low?
 *		direction:	- 0 for input, 1 for output
 *
 * Outputs:	None.
 *
 *------------------------------------------------------------------*/

#ifndef __WIN32__

void export_gpio(int gpio, int invert, int direction)
{
	HANDLE fd;
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
	snprintf (stemp, sizeof(stemp), "%d", gpio);
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
	Idea for future:

	On the RPi, the device path for GPIO number XX is /sys/class/gpio/gpioXX.
	There was a report that it is different for the Cubieboard.  For instance
	GPIO 61 has gpio61_pi13 in the path.  This indicates connector "i" pin 13.

	For another similar single board computer, we find the same thing:
	https://www.olimex.com/wiki/A20-OLinuXino-LIME#GPIO_under_Linux

	How should we deal with this?  Some possibilities:

	(1) The user might explicitly mention the name in direwolf.conf.
	(2) We might be able to find the names in some system device config file.
	(3) Get a directory listing of /sys/class/gpio then search for a 
		matching name.  Suppose we wanted GPIO 61.  First look for an exact
		match to "gpio61".  If that is not found, look for something
		matching the pattern "gpio61_*".
*/

/*
 * We will have the same permission problem if not root.
 * We only care about "direction" and "value".
 */
	snprintf (stemp, sizeof(stemp), "sudo chmod go+rw /sys/class/gpio/gpio%d/direction", gpio);
	err = system (stemp);
	snprintf (stemp, sizeof(stemp), "sudo chmod go+rw /sys/class/gpio/gpio%d/value", gpio);
	err = system (stemp);
	(void)err;

	snprintf (stemp, sizeof(stemp), "/sys/class/gpio/gpio%d/value", gpio);

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
	    dw_printf ("    chmod go+rw /sys/class/gpio/gpio%d/direction", gpio);
	    dw_printf ("    chmod go+rw /sys/class/gpio/gpio%d/value", gpio);
	    exit (1);
	  }
	}

/*
 * Set output direction and initial state
 */

	snprintf (stemp, sizeof(stemp), "/sys/class/gpio/gpio%d/direction", gpio);
	fd = open(stemp, O_WRONLY);
	if (fd < 0) {
	  int e = errno;
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Error opening %s\n", stemp);
	  dw_printf ("%s\n", strerror(e));
	  exit (1);
	}

	char gpio_val[8];
	if (direction) {
	  if (invert) {
	    strlcpy (gpio_val, "high", sizeof(gpio_val));
	  }
	  else {
	    strlcpy (gpio_val, "low", sizeof(gpio_val));
	  }
	}
	else {
	  strlcpy (gpio_val, "in", sizeof(gpio_val));
	}
	if (write (fd, gpio_val, strlen(gpio_val)) != strlen(gpio_val)) {
	  int e = errno;
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Error writing initial state to %s\n", stemp);
	  dw_printf ("%s\n", strerror(e));
	  exit (1);
	}
	close (fd);
}

#endif   /* not __WIN32__ */


/*-------------------------------------------------------------------
 *
 * Name:        ptt_init
 *
 * Purpose:    	Open serial port(s) used for PTT signals and set to proper state.
 *
 * Inputs:	audio_config_p		- Structure with communication parameters.
 *
 *		    for each channel we have:
 *
 *			ptt_method	Method for PTT signal. 
 *					PTT_METHOD_NONE - not configured.  Could be using VOX. 
 *					PTT_METHOD_SERIAL - serial (com) port. 
 *					PTT_METHOD_GPIO - general purpose I/O. 
 *					PTT_METHOD_LPT - Parallel printer port. 
 *                  			PTT_METHOD_HAMLIB - HAMLib rig control.
 *			
 *			ptt_device	Name of serial port device.  
 *					 e.g. COM1 or /dev/ttyS0. 
 *					 HAMLIB can also use hostaddr:port.
 *			
 *			ptt_line	RTS or DTR when using serial port. 
 *			
 *			ptt_gpio	GPIO number.  Only used for Linux. 
 *					 Valid only when ptt_method is PTT_METHOD_GPIO. 
 *					
 *			ptt_lpt_bit	Bit number for parallel printer port.  
 *					 Bit 0 = pin 2, ..., bit 7 = pin 9. 
 *					 Valid only when ptt_method is PTT_METHOD_LPT. 
 *			
 *			ptt_invert	Invert the signal.  
 *					 Normally higher voltage means transmit or LED on. 
 *
 *			ptt_model	Only for HAMLIB.
 *					2 to communicate with rigctld.
 *					>= 3 for specific radio model.
 *					-1 guess at what is out there.  (AUTO option in config file.)
 *
 * Outputs:	Remember required information for future use.
 *
 * Description:	
 *
 *--------------------------------------------------------------------*/



static struct audio_s *save_audio_config_p;	/* Save config information for later use. */

static HANDLE ptt_fd[MAX_CHANS][NUM_OCTYPES];	
					/* Serial port handle or fd.  */
					/* Could be the same for two channels */	
					/* if using both RTS and DTR. */
#if USE_HAMLIB
static RIG *rig[MAX_CHANS][NUM_OCTYPES];
#endif

static char otnames[NUM_OCTYPES][8];

void ptt_init (struct audio_s *audio_config_p)
{
	int ch;
	HANDLE fd = INVALID_HANDLE_VALUE;
#if __WIN32__
#else
	int using_gpio;
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("ptt_init ( ... )\n");
#endif

	save_audio_config_p = audio_config_p;

	strlcpy (otnames[OCTYPE_PTT], "PTT", sizeof(otnames[OCTYPE_PTT]));
	strlcpy (otnames[OCTYPE_DCD], "DCD", sizeof(otnames[OCTYPE_DCD]));
	strlcpy (otnames[OCTYPE_FUTURE], "FUTURE", sizeof(otnames[OCTYPE_FUTURE]));


	for (ch = 0; ch < MAX_CHANS; ch++) {
	  int ot;

	  for (ot = 0; ot < NUM_OCTYPES; ot++) {

	    ptt_fd[ch][ot] = INVALID_HANDLE_VALUE;
#if USE_HAMLIB
	    rig[ch][ot] = NULL;
#endif
	    if (ptt_debug_level >= 2) {

	      text_color_set(DW_COLOR_DEBUG);
              dw_printf ("ch=%d, %s method=%d, device=%s, line=%d, gpio=%d, lpt_bit=%d, invert=%d\n",
		ch,
		otnames[ot],
		audio_config_p->achan[ch].octrl[ot].ptt_method, 
		audio_config_p->achan[ch].octrl[ot].ptt_device,
		audio_config_p->achan[ch].octrl[ot].ptt_line,
		audio_config_p->achan[ch].octrl[ot].ptt_gpio,
		audio_config_p->achan[ch].octrl[ot].ptt_lpt_bit,
		audio_config_p->achan[ch].octrl[ot].ptt_invert);
	    }
	  }
	}

/*
 * Set up serial ports.
 */

	for (ch = 0; ch < MAX_CHANS; ch++) {

	  if (audio_config_p->achan[ch].valid) {
	    int ot;

	    for (ot = 0; ot < NUM_OCTYPES; ot++) {

	      if (audio_config_p->achan[ch].octrl[ot].ptt_method == PTT_METHOD_SERIAL) {

#if __WIN32__
#else
	        /* Translate Windows device name into Linux name. */
	        /* COM1 -> /dev/ttyS0, etc. */

	        if (strncasecmp(audio_config_p->achan[ch].octrl[ot].ptt_device, "COM", 3) == 0) {
	          int n = atoi (audio_config_p->achan[ch].octrl[ot].ptt_device + 3);
	          text_color_set(DW_COLOR_INFO);
	          dw_printf ("Converted %s device '%s'", audio_config_p->achan[ch].octrl[ot].ptt_device, otnames[ot]);
	          if (n < 1) n = 1;
	          snprintf (audio_config_p->achan[ch].octrl[ot].ptt_device, sizeof(audio_config_p->achan[ch].octrl[ot].ptt_device), "/dev/ttyS%d", n-1);
	          dw_printf (" to Linux equivalent '%s'\n", audio_config_p->achan[ch].octrl[ot].ptt_device);
	        }
#endif
	        /* Can't open the same device more than once so we */
	        /* need more logic to look for the case of multiple radio */
	        /* channels using different pins of the same COM port. */

	        /* Did some earlier channel use the same device name? */

	        int same_device_used = 0;
	        int j, k;

	        for (j = ch; j >= 0; j--) {
	          if (audio_config_p->achan[j].valid) {
		    for (k = ((j==ch) ? (ot - 1) : (NUM_OCTYPES-1)); k >= 0; k--) {
	              if (strcmp(audio_config_p->achan[ch].octrl[ot].ptt_device,audio_config_p->achan[j].octrl[k].ptt_device) == 0) {
	                fd = ptt_fd[j][k];
	                same_device_used = 1;
	              }
	            }
	          }
	        }

	        if ( ! same_device_used) {
	
#if __WIN32__
	          char bettername[50];
	          // Bug fix in release 1.1 - Need to munge name for COM10 and up.
	          // http://support.microsoft.com/kb/115831

	          strlcpy (bettername, audio_config_p->achan[ch].octrl[ot].ptt_device, sizeof(bettername));
	          if (strncasecmp(bettername, "COM", 3) == 0) {
	            int n;
	            n = atoi(bettername+3);
	            if (n >= 10) {
	              strlcpy (bettername, "\\\\.\\", sizeof(bettername));
	              strlcat (bettername, audio_config_p->achan[ch].octrl[ot].ptt_device, sizeof(bettername));
	            }
	          }
	          fd = CreateFile(bettername,
			GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
#else

		  /* O_NONBLOCK added in version 0.9. */
		  /* Was hanging with some USB-serial adapters. */
		  /* https://bugs.launchpad.net/ubuntu/+source/linux/+bug/661321/comments/12 */

	          fd = open (audio_config_p->achan[ch].octrl[ot].ptt_device, O_RDONLY | O_NONBLOCK);
#endif
                }

	        if (fd != INVALID_HANDLE_VALUE) {
	          ptt_fd[ch][ot] = fd;
	        }
	        else {
#if __WIN32__
#else
	          int e = errno;
#endif
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("ERROR can't open device %s for channel %d PTT control.\n",
			audio_config_p->achan[ch].octrl[ot].ptt_device, ch);
#if __WIN32__
#else
	          dw_printf ("%s\n", strerror(e));
#endif
	          /* Don't try using it later if device open failed. */

	          audio_config_p->achan[ch].octrl[ot].ptt_method = PTT_METHOD_NONE;
	        }

/*
 * Set initial state off.
 * ptt_set will invert output signal if appropriate.
 */	  
	        ptt_set (ot, ch, 0);

	      }    /* if serial method. */
	    }	 /* for each output type. */
	  }    /* if channel valid. */
	}    /* For each channel. */


/* 
 * Set up GPIO - for Linux only.
 */

#if __WIN32__
#else

/*
 * Does any of them use GPIO?
 */

	using_gpio = 0;
	for (ch=0; ch<MAX_CHANS; ch++) {
	  if (save_audio_config_p->achan[ch].valid) {
	    int ot;
	    for (ot = 0; ot < NUM_OCTYPES; ot++) {
	      if (audio_config_p->achan[ch].octrl[ot].ptt_method == PTT_METHOD_GPIO) {
	        using_gpio = 1;
	      }
	    }
	    for (ot = 0; ot < NUM_ICTYPES; ot++) {
	      if (audio_config_p->achan[ch].ictrl[ot].method == PTT_METHOD_GPIO) {
	        using_gpio = 1;
	      }
	    }
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
	    
	for (ch = 0; ch < MAX_CHANS; ch++) {
	  if (save_audio_config_p->achan[ch].valid) {
	    int ot;
	    for (ot = 0; ot < NUM_OCTYPES; ot++) {
	      if (audio_config_p->achan[ch].octrl[ot].ptt_method == PTT_METHOD_GPIO) {
	        export_gpio(audio_config_p->achan[ch].octrl[ot].ptt_gpio, audio_config_p->achan[ch].octrl[ot].ptt_invert, 1);
	      }
	    }
	    for (ot = 0; ot < NUM_ICTYPES; ot++) {
	      if (audio_config_p->achan[ch].ictrl[ot].method == PTT_METHOD_GPIO) {
	        export_gpio(audio_config_p->achan[ch].ictrl[ot].gpio, audio_config_p->achan[ch].ictrl[ot].invert, 0);
	      }
	    }
	  }
	}
#endif



/*
 * Set up parallel printer port.
 * 
 * Restrictions:
 * 	Only the primary printer port.
 * 	For x86 Linux only.
 */

#if  ( defined(__i386__) || defined(__x86_64__) ) && ( defined(__linux__) || defined(__unix__) )

	for (ch = 0; ch < MAX_CHANS; ch++) {
	  if (save_audio_config_p->achan[ch].valid) {
	    int ot;
	    for (ot = 0; ot < NUM_OCTYPES; ot++) {
	      if (audio_config_p->achan[ch].octrl[ot].ptt_method == PTT_METHOD_LPT) {

	        /* Can't open the same device more than once so we */
	        /* need more logic to look for the case of mutiple radio */
	        /* channels using different pins of the LPT port. */

	        /* Did some earlier channel use the same ptt device name? */

	        int same_device_used = 0;
	        int j, k;
	
	        for (j = ch; j >= 0; j--) {
	          if (audio_config_p->achan[j].valid) {
		    for (k = ((j==ch) ? (ot - 1) : (NUM_OCTYPES-1)); k >= 0; k--) {
	              if (strcmp(audio_config_p->achan[ch].octrl[ot].ptt_device,audio_config_p->achan[j].octrl[k].ptt_device) == 0) {
	                fd = ptt_fd[j][k];
	                same_device_used = 1;
	              }
	            }
	          }
	        }

	        if ( ! same_device_used) {
	          fd = open ("/dev/port", O_RDWR | O_NDELAY);
	        }

	        if (fd != INVALID_HANDLE_VALUE) {
	          ptt_fd[ch][ot] = fd;
	        }
	        else {

	          int e = errno;

	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("ERROR - Can't open /dev/port for parallel printer port PTT control.\n");
	          dw_printf ("%s\n", strerror(e));
	          dw_printf ("You probably don't have adequate permissions to access I/O ports.\n");
	          dw_printf ("Either run direwolf as root or change these permissions:\n");
	          dw_printf ("  sudo chmod go+rw /dev/port\n");
	          dw_printf ("  sudo setcap cap_sys_rawio=ep `which direwolf`\n");

	          /* Don't try using it later if device open failed. */

	          audio_config_p->achan[ch].octrl[ot].ptt_method = PTT_METHOD_NONE;
	        }
	    

/*
 * Set initial state off.
 * ptt_set will invert output signal if appropriate.
 */	  
	        ptt_set (ot, ch, 0);

	      }       /* if parallel printer port method. */
	    }       /* for each output type */
	  }       /* if valid channel. */
	}	/* For each channel. */



#endif /* x86 Linux */

#ifdef USE_HAMLIB
	for (ch = 0; ch < MAX_CHANS; ch++) {
	  if (save_audio_config_p->achan[ch].valid) {
	    int ot;
	    for (ot = 0; ot < NUM_OCTYPES; ot++) {
	      if (audio_config_p->achan[ch].octrl[ot].ptt_method == PTT_METHOD_HAMLIB) {
	        if (ot == OCTYPE_PTT) {

	          /* For "AUTO" model, try to guess what is out there. */

	          if (audio_config_p->achan[ch].octrl[ot].ptt_model == -1) {
	            hamlib_port_t hport;

	            memset (&hport, 0, sizeof(hport));
	            strlcpy (hport.pathname, audio_config_p->achan[ch].octrl[ot].ptt_device, sizeof(hport.pathname));
	            rig_load_all_backends();
                    audio_config_p->achan[ch].octrl[ot].ptt_model = rig_probe(&hport);

	            if (audio_config_p->achan[ch].octrl[ot].ptt_model == RIG_MODEL_NONE) {
	              text_color_set(DW_COLOR_ERROR);
	              dw_printf ("Couldn't guess rig model number for AUTO option.  Run \"rigctl --list\" for a list of model numbers.\n");
	              continue;
	            }

	            text_color_set(DW_COLOR_INFO);
	            dw_printf ("Hamlib AUTO option detected rig model %d.  Run \"rigctl --list\" for a list of model numbers.\n",
							audio_config_p->achan[ch].octrl[ot].ptt_model);
	          }

	          rig[ch][ot] = rig_init(audio_config_p->achan[ch].octrl[ot].ptt_model);
	          if (rig[ch][ot] == NULL) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Unknown rig model %d for hamlib.  Run \"rigctl --list\" for a list of model numbers.\n", 
	                          audio_config_p->achan[ch].octrl[ot].ptt_model);
	            continue;
	          }

	          strlcpy (rig[ch][ot]->state.rigport.pathname, audio_config_p->achan[ch].octrl[ot].ptt_device, sizeof(rig[ch][ot]->state.rigport.pathname));
	          int err = rig_open(rig[ch][ot]);
	          if (err != RIG_OK) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Hamlib Rig open error %d: %s\n", err, rigerror(err));
	            rig_cleanup (rig[ch][ot]);
	            rig[ch][ot] = NULL;
	            continue;
	          }

       		  /* Successful.  Later code should check for rig[ch][ot] not NULL. */
	        }
	        else {
                  text_color_set(DW_COLOR_ERROR);
                  dw_printf ("HAMLIB can only be used for PTT.  Not DCD or other output.\n");
	        }
	      }
	    }
	  }
	}

#endif


/* Why doesn't it transmit?  Probably forgot to specify PTT option. */

	for (ch=0; ch<MAX_CHANS; ch++) {
	  if (audio_config_p->achan[ch].valid) {
	    if(audio_config_p->achan[ch].octrl[OCTYPE_PTT].ptt_method == PTT_METHOD_NONE) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Note: PTT not configured for channel %d. (Ignore this if using VOX.)\n", ch);
	    }
	  }
	}

} /* end ptt_init */


/*-------------------------------------------------------------------
 *
 * Name:        ptt_set
 *
 * Purpose:    	Turn output control line on or off.
 *		Originally this was just for PTT, hence the name.
 *		Now that it is more general purpose, it should
 *		probably be renamed something like octrl_set.
 *
 * Inputs:	ot		- Output control type:
 *				   OCTYPE_PTT, OCTYPE_DCD, OCTYPE_HAMLIB, OCTYPE_FUTURE
 *
 *		chan		- channel, 0 .. (number of channels)-1
 *
 *		ptt_signal	- 1 for transmit, 0 for receive.
 *
 *
 * Assumption:	ptt_init was called first.
 *
 * Description:	Set the RTS or DTR line or GPIO pin.
 *		More positive output corresponds to 1 unless invert is set.
 *
 *--------------------------------------------------------------------*/


void ptt_set (int ot, int chan, int ptt_signal)
{

	int ptt = ptt_signal;
	int ptt2 = ptt_signal;

	assert (ot >= 0 && ot < NUM_OCTYPES);
	assert (chan >= 0 && chan < MAX_CHANS);

	if (ptt_debug_level >= 1) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("%s %d = %d\n", otnames[ot], chan, ptt_signal);
	}

	assert (chan >= 0 && chan < MAX_CHANS);

	if ( ! save_audio_config_p->achan[chan].valid) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error, ptt_set ( %s, %d, %d ), did not expect invalid channel.\n", otnames[ot], chan, ptt);
	  return;
	}

/* 
 * Inverted output? 
 */

	if (save_audio_config_p->achan[chan].octrl[ot].ptt_invert) {
	  ptt = ! ptt;
	}
	if (save_audio_config_p->achan[chan].octrl[ot].ptt_invert2) {
	  ptt2 = ! ptt2;
	}

/*
 * Using serial port?
 */
	if (save_audio_config_p->achan[chan].octrl[ot].ptt_method == PTT_METHOD_SERIAL && 
		ptt_fd[chan][ot] != INVALID_HANDLE_VALUE) {

	  if (save_audio_config_p->achan[chan].octrl[ot].ptt_line == PTT_LINE_RTS) {

	    if (ptt) {
	      RTS_ON(ptt_fd[chan][ot]);
	    }
	    else {
	      RTS_OFF(ptt_fd[chan][ot]);
	    }
	  }
	  else if (save_audio_config_p->achan[chan].octrl[ot].ptt_line == PTT_LINE_DTR) {

	    if (ptt) {
	      DTR_ON(ptt_fd[chan][ot]);
	    }
	    else {
	      DTR_OFF(ptt_fd[chan][ot]);
	    }
	  }

/* 
 * Second serial port control line?  Typically driven with opposite phase but could be in phase.
 */

	  if (save_audio_config_p->achan[chan].octrl[ot].ptt_line2 == PTT_LINE_RTS) {

	    if (ptt2) {
	      RTS_ON(ptt_fd[chan][ot]);
	    }
	    else {
	      RTS_OFF(ptt_fd[chan][ot]);
	    }
	  }
	  else if (save_audio_config_p->achan[chan].octrl[ot].ptt_line2 == PTT_LINE_DTR) {

	    if (ptt2) {
	      DTR_ON(ptt_fd[chan][ot]);
	    }
	    else {
	      DTR_OFF(ptt_fd[chan][ot]);
	    }
	  }
	  /* else neither one */

	}

/*
 * Using GPIO? 
 */

#if __WIN32__
#else

	if (save_audio_config_p->achan[chan].octrl[ot].ptt_method == PTT_METHOD_GPIO) {
	  int fd;
	  char stemp[80];

	  snprintf (stemp, sizeof(stemp), "/sys/class/gpio/gpio%d/value", save_audio_config_p->achan[chan].octrl[ot].ptt_gpio);

	  fd = open(stemp, O_WRONLY);
	  if (fd < 0) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error opening %s to set %s signal.\n", stemp, otnames[ot]);
	    dw_printf ("%s\n", strerror(e));
	    return;
	  }

	  snprintf (stemp, sizeof(stemp), "%d", ptt);

	  if (write (fd, stemp, 1) != 1) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error setting GPIO %d for %s\n", save_audio_config_p->achan[chan].octrl[ot].ptt_gpio, otnames[ot]);
	    dw_printf ("%s\n", strerror(e));
	  }
	  close (fd);

	}
#endif
	
/*
 * Using parallel printer port?
 */

#if  ( defined(__i386__) || defined(__x86_64__) ) && ( defined(__linux__) || defined(__unix__) )

	if (save_audio_config_p->achan[chan].octrl[ot].ptt_method == PTT_METHOD_LPT && 
		ptt_fd[chan][ot] != INVALID_HANDLE_VALUE) {

	  char lpt_data;
	  //ssize_t n;		

	  lseek (ptt_fd[chan][ot], (off_t)LPT_IO_ADDR, SEEK_SET);
	  if (read (ptt_fd[chan][ot], &lpt_data, (size_t)1) != 1) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error reading current state of LPT for channel %d %s\n", chan, otnames[ot]);
	    dw_printf ("%s\n", strerror(e));
	  }

	  if (ptt) {
	    lpt_data |= ( 1 << save_audio_config_p->achan[chan].octrl[ot].ptt_lpt_bit );
	  }
	  else {
	    lpt_data &= ~ ( 1 << save_audio_config_p->achan[chan].octrl[ot].ptt_lpt_bit );
	  }

	  lseek (ptt_fd[chan][ot], (off_t)LPT_IO_ADDR, SEEK_SET);
	  if (write (ptt_fd[chan][ot], &lpt_data, (size_t)1) != 1) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error writing to LPT for channel %d %s\n", chan, otnames[ot]);
	    dw_printf ("%s\n", strerror(e));
	  }
	}

#endif /* x86 Linux */

#ifdef USE_HAMLIB
/*
 * Using hamlib?
 */

	if (save_audio_config_p->achan[chan].octrl[ot].ptt_method == PTT_METHOD_HAMLIB) {

	  if (rig[chan][ot] != NULL) {

	    int retcode = rig_set_ptt(rig[chan][ot], RIG_VFO_CURR, ptt ? RIG_PTT_ON : RIG_PTT_OFF);

	    if (retcode != RIG_OK) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Error sending rig_set_ptt command for channel %d %s\n", chan, otnames[ot]);
	      dw_printf ("%s\n", rigerror(retcode));
	    }
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Can't use rig_set_ptt for channel %d %s because rig_open failed.\n", chan, otnames[ot]);
	  }
	}
#endif


} /* end ptt_set */

/*-------------------------------------------------------------------
 *
 * Name:	get_input
 *
 * Purpose:	Read the value of an input line
 *
 * Inputs:	it	- Input type (ICTYPE_TCINH supported so far)
 * 		chan	- Audio channel number
 * 
 * Outputs:	0 = inactive, 1 = active, -1 = error
 *
 * ------------------------------------------------------------------*/

int get_input (int it, int chan)
{
	assert (it >= 0 && it < NUM_ICTYPES);
	assert (chan >= 0 && chan < MAX_CHANS);

	if ( ! save_audio_config_p->achan[chan].valid) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error, get_input ( %d, %d ), did not expect invalid channel.\n", it, chan);
	  return -1;
	}
	
#if __WIN32__
#else
	if (save_audio_config_p->achan[chan].ictrl[it].method == PTT_METHOD_GPIO) {
	  int fd;
	  char stemp[80];

	  snprintf (stemp, sizeof(stemp), "/sys/class/gpio/gpio%d/value", save_audio_config_p->achan[chan].ictrl[it].gpio);

	  fd = open(stemp, O_RDONLY);
	  if (fd < 0) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error opening %s to check input.\n", stemp);
	    dw_printf ("%s\n", strerror(e));
	    return -1;
	  }

	  char vtemp[2];
	  if (read (fd, vtemp, 1) != 1) {
	    int e = errno;
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error getting GPIO %d value\n", save_audio_config_p->achan[chan].ictrl[it].gpio);
	    dw_printf ("%s\n", strerror(e));
	  }
	  close (fd);

	  if (atoi(vtemp) != save_audio_config_p->achan[chan].ictrl[it].invert) {
	    return 1;
	  }
	  else {
	    return 0;
	  }
	}
#endif

	return -1;	/* Method was none, or something went wrong */
}

/*-------------------------------------------------------------------
 *
 * Name:        ptt_term
 *
 * Purpose:    	Make sure PTT and others are turned off when we exit.
 *
 * Inputs:	none
 *
 * Description:	
 *
 *--------------------------------------------------------------------*/

void ptt_term (void)
{
	int n;

	for (n = 0; n < MAX_CHANS; n++) {
	  if (save_audio_config_p->achan[n].valid) {
	    int ot;
	    for (ot = 0; ot < NUM_OCTYPES; ot++) {
	      ptt_set (ot, n, 0);
	    }
	  }
	}

	for (n = 0; n < MAX_CHANS; n++) {
	  if (save_audio_config_p->achan[n].valid) {
	    int ot;
	    for (ot = 0; ot < NUM_OCTYPES; ot++) {
	      if (ptt_fd[n][ot] != INVALID_HANDLE_VALUE) {
#if __WIN32__
	        CloseHandle (ptt_fd[n][ot]);
#else
	        close(ptt_fd[n][ot]);
#endif
	        ptt_fd[n][ot] = INVALID_HANDLE_VALUE;
	      }
	    }
	  }
	}

#ifdef USE_HAMLIB

	for (n = 0; n < MAX_CHANS; n++) {
	  if (save_audio_config_p->achan[n].valid) {
	    int ot;
	    for (ot = 0; ot < NUM_OCTYPES; ot++) {
	      if (rig[n][ot] != NULL) {

	        rig_close(rig[n][ot]);
	        rig_cleanup(rig[n][ot]);
	        rig[n][ot] = NULL;
	      }
	    }
	  }
	}
#endif
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
	struct audio_s my_audio_config;
	int n;
	int chan;

	memset (&my_audio_config, 0, sizeof(my_audio_config));

	my_audio_config.adev[0].num_channels = 2;

	my_audio_config.valid[0] = 1;
	my_audio_config.adev[0].octrl[OCTYPE_PTT].ptt_method = PTT_METHOD_SERIAL;
	//strlcpy (my_audio_config.ptt_device, "COM1", sizeof(my_audio_config.ptt_device));
	strlcpy (my_audio_config.ptt_device, "/dev/ttyUSB0", sizeof(my_audio_config.ptt_device));
	my_audio_config.adev[0].octrl[OCTYPE_PTT].ptt_line = PTT_LINE_RTS;

	my_audio_config.valid[1] = 1;
	my_audio_config.adev[1].octrl[OCTYPE_PTT].ptt_method = PTT_METHOD_SERIAL;
	//strlcpy (my_audio_config.adev[1].octrl[OCTYPE_PTT].ptt_device, "COM1", sizeof(my_audio_config.adev[1].octrl[OCTYPE_PTT].ptt_device));
	strlcpy (my_audio_config.adev[1].octrl[OCTYPE_PTT].ptt_device, "/dev/ttyUSB0", sizeof(my_audio_config.adev[1].octrl[OCTYPE_PTT].ptt_device));
	my_audio_config.adev[1].octrl[OCTYPE_PTT].ptt_line = PTT_LINE_DTR;


/* initialize - both off */

	ptt_init (&my_audio_config);

	SLEEP_SEC(2);

/* flash each a few times. */

	dw_printf ("turn on RTS a few times...\n");

	chan = 0;
	for (n=0; n<3; n++) {
	  ptt_set (OCTYPE_PTT, chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (OCTYPE_PTT, chan, 0);
	  SLEEP_SEC(1);
	}

	dw_printf ("turn on DTR a few times...\n");

	chan = 1;
	for (n=0; n<3; n++) {
	  ptt_set (OCTYPE_PTT, chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (OCTYPE_PTT, chan, 0);
	  SLEEP_SEC(1);
	}

	ptt_term();

/* Same thing again but invert RTS. */

	my_audio_config.adev[0].octrl[OCTYPE_PTT].ptt_invert = 1;

	ptt_init (&my_audio_config);

	SLEEP_SEC(2);

	dw_printf ("INVERTED -  RTS a few times...\n");

	chan = 0;
	for (n=0; n<3; n++) {
	  ptt_set (OCTYPE_PTT, chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (OCTYPE_PTT, chan, 0);
	  SLEEP_SEC(1);
	}

	dw_printf ("turn on DTR a few times...\n");

	chan = 1;
	for (n=0; n<3; n++) {
	  ptt_set (OCTYPE_PTT, chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (OCTYPE_PTT, chan, 0);
	  SLEEP_SEC(1);
	}

	ptt_term ();


/* Test GPIO */

#if __arm__

	memset (&my_audio_config, 0, sizeof(my_audio_config));
	my_audio_config.adev[0].num_channels = 1;
	my_audio_config.valid[0] = 1;
	my_audio_config.adev[0].octrl[OCTYPE_PTT].ptt_method = PTT_METHOD_GPIO;
	my_audio_config.adev[0].octrl[OCTYPE_PTT].ptt_gpio = 25;

	dw_printf ("Try GPIO %d a few times...\n", my_audio_config.ptt_gpio[0]);

	ptt_init (&my_audio_config);

	SLEEP_SEC(2);
	chan = 0;
	for (n=0; n<3; n++) {
	  ptt_set (OCTYPE_PTT, chan, 1);
	  SLEEP_SEC(1);
	  ptt_set (OCTYPE_PTT, chan, 0);
	  SLEEP_SEC(1);
	}

	ptt_term ();
#endif


	memset (&my_audio_config, 0, sizeof(my_audio_config));
	my_audio_config.num_channels = 2;
	my_audio_config.valid[0] = 1;
	my_audio_config.adev[0].octrl[OCTYPE_PTT].ptt_method = PTT_METHOD_LPT;
	my_audio_config.adev[0].octrl[OCTYPE_PTT].ptt_lpt_bit = 0;
	my_audio_config.valid[1] = 1;
	my_audio_config.adev[1].octrl[OCTYPE_PTT].ptt_method = PTT_METHOD_LPT;
	my_audio_config.adev[1].octrl[OCTYPE_PTT].ptt_lpt_bit = 1;

	dw_printf ("Try LPT bits 0 & 1 a few times...\n");

	ptt_init (&my_audio_config);

	for (n=0; n<8; n++) {
	  ptt_set (OCTYPE_PTT, 0, n & 1);
	  ptt_set (OCTYPE_PTT, 1, (n>>1) & 1);
	  SLEEP_SEC(1);
	}

	ptt_term ();	

/* Parallel printer port. */

#if  ( defined(__i386__) || defined(__x86_64__) ) && ( defined(__linux__) || defined(__unix__) )

	// TODO

#endif


}

#endif  /* TEST */

/* end ptt.c */



