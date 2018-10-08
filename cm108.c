//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2017  John Langner, WB2OSZ
//
//    Parts of this were adapted from "hamlib" which contains the notice:
//
//	 *  Copyright (c) 2000-2012 by Stephane Fillod
//	 *  Copyright (c) 2011 by Andrew Errington
//	 *  CM108 detection code Copyright (c) Thomas Sailer used with permission
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
 * Module:      cm108.c
 *
 * Purpose:   	Use the CM108/CM119 (or compatible) GPIO pins for the Push To Talk (PTT) Control.
 *		
 * Description:
 *
 *	There is an incresing demand for using the GPIO pins of USB audio devices for PTT.
 *	We have a couple commercial products:
 *
 *		DMK URI			http://www.dmkeng.com/URI_Order_Page.htm
 *		RB-USB RIM		http://www.repeater-builder.com/products/usb-rim-lite.html
 *
 *	and homebrew projects which are all very similar.
 *
 *		http://www.qsl.net/kb9mwr/projects/voip/usbfob-119.pdf
 *		http://rtpdir.weebly.com/uploads/1/6/8/7/1687703/usbfob.pdf
 *		http://www.repeater-builder.com/projects/fob/USB-Fob-Construction.pdf
 *		https://irongarment.wordpress.com/2011/03/29/cm108-compatible-chips-with-gpio/
 *
 *	Usually GPIO 3 is used because it is easier to tack solder a wire to a pin on the end.
 *	
 *	Soundmodem and hamlib paved the way but didn't get too far.
 *	Dire Wolf 1.3 added HAMLIB support (Linux only) which theoretically allows this in a
 *	roundabout way.  This is documented in the User Guide, section called,
 *		 "Hamlib PTT Example 2: Use GPIO of USB audio adapter.  (e.g. DMK URI)"
 *
 *	It's rather involved and the explantion doesn't cover the case of multiple
 *	USB-Audio adapters.  It is not as straightforward as you might expect.  Here we have
 *	an example of 3 C-Media USB adapters, a SignaLink USB, a keyboard, and a mouse.
 *	
 *	
 *	    VID  PID   Product                          Sound                  ADEVICE         HID [ptt]
 *	    ---  ---   -------                          -----                  -------         ---------
 *	**  0d8c 000c  C-Media USB Headphone Set        /dev/snd/pcmC1D0c      plughw:1,0      /dev/hidraw0
 *	**  0d8c 000c  C-Media USB Headphone Set        /dev/snd/pcmC1D0p      plughw:1,0      /dev/hidraw0
 *	**  0d8c 000c  C-Media USB Headphone Set        /dev/snd/controlC1                     /dev/hidraw0
 *	    08bb 2904  USB Audio CODEC                  /dev/snd/pcmC2D0c      plughw:2,0      /dev/hidraw2
 *	    08bb 2904  USB Audio CODEC                  /dev/snd/pcmC2D0p      plughw:2,0      /dev/hidraw2
 *	    08bb 2904  USB Audio CODEC                  /dev/snd/controlC2                     /dev/hidraw2
 *	**  0d8c 000c  C-Media USB Headphone Set        /dev/snd/pcmC0D0c      plughw:0,0      /dev/hidraw1
 *	**  0d8c 000c  C-Media USB Headphone Set        /dev/snd/pcmC0D0p      plughw:0,0      /dev/hidraw1
 *	**  0d8c 000c  C-Media USB Headphone Set        /dev/snd/controlC0                     /dev/hidraw1
 *	**  0d8c 0008  C-Media USB Audio Device         /dev/snd/pcmC4D0c      plughw:4,0      /dev/hidraw6
 *	**  0d8c 0008  C-Media USB Audio Device         /dev/snd/pcmC4D0p      plughw:4,0      /dev/hidraw6
 *	**  0d8c 0008  C-Media USB Audio Device         /dev/snd/controlC4                     /dev/hidraw6
 *	    413c 2010  Dell USB Keyboard                                                       /dev/hidraw4
 *	    0461 4d15  USB Optical Mouse                                                       /dev/hidraw5
 *	
 *	
 *	The USB soundcards (/dev/snd/pcm...) have an associated Human Interface Device (HID)
 *	corresponding to the GPIO pins which are sometimes connected to pushbuttons.
 *	The mapping has no obvious pattern.
 *		
 *		Sound Card 0		HID 1
 *		Sound Card 1		HID 0
 *		Sound Card 2		HID 2
 *		Sound Card 4		HID 6
 *	
 *	That would be a real challenge if you had to figure that all out and configure manually.
 *	Dire Wolf version 1.5 makes this much more flexible and easier to use by supporting multiple
 *	sound devices and automatically determining the corresponding HID for the PTT signal. 
 *
 *---------------------------------------------------------------*/

#ifndef USE_CM108

#ifdef CM108_MAIN

#include "direwolf.h"
#include "textcolor.h"

int main (void)
{
	text_color_init (0);    // Turn off text color.
	dw_printf ("CM108 PTT support was disabled in Makefile.linux.\n");
	dw_printf ("It was excluded because /usr/include/libudev.h was missing.\n");
	dw_printf ("Install it with \"sudo apt-get install libudev-dev\" or\n");
	dw_printf ("\"sudo yum install libudev-devel\" then rebuild.\n");
	return (0);
}

#endif

#else	// USE_CM108 is defined.

#include "direwolf.h"

#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>			// ioctl, _IOR
#include <fcntl.h>
#include <errno.h>
#include <linux/hidraw.h>		// for HIDIOCGRAWINFO

#include "textcolor.h"
#include "cm108.h"

static int cm108_write (char *name, int iomask, int iodata);


// The CM108, CM109, and CM119 datasheets all say that idProduct can be in the range 
// of 0008 to 000f programmable by MSEL and MODE pin.  How can we tell the difference?

// CM108B is 0012.
// CM119B is 0013.
// CM108AH is 0139 programmable by MSEL and MODE pin.
// CM119A is 013A programmable by MSEL and MODE pin.

// To make matters even more confusing, these can be overridden
// with an external EEPROM.  Some have 8, rather than 4 GPIO.

#define CMEDIA_VID 0xd8c		// Vendor ID
#define CMEDIA_PID1_MIN 0x0008		// range for CM108, CM109, CM119 (no following letters)
#define CMEDIA_PID1_MAX 0x000f

#define CMEDIA_PID_CM108AH	0x0139		// CM108AH
#define CMEDIA_PID_CM108B	0x0012		// CM108B
#define CMEDIA_PID_CM119A	0x013a		// CM119A
#define CMEDIA_PID_CM119B	0x0013		// CM119B
#define CMEDIA_PID_HS100	0x013c		// HS100

// The SSS chips seem to be pretty much compatible but they have only two GPIO.
// https://irongarment.wordpress.com/2011/03/29/cm108-compatible-chips-with-gpio/
// Data sheet says VID/PID is from an EEPROM but mentions no default.

#define SSS_VID 0x0c76			// SSS1621, SSS1623
#define SSS_PID1 0x1605
#define SSS_PID2 0x1607
#define SSS_PID3 0x160b


//	Device		VID	PID		Number of GPIO
//	------		---	---		--------------
//	CM108		0d8c	0008-000f *	4	
//	CM108AH		0d8c	0139 *		3	Has GPIO 1,3,4 but not 2
//	CM108B		0d8c	0012		3	Has GPIO 1,3,4 but not 2
//	CM109		0d8c	0008-000f *	8
//	CM119		0d8c	0008-000f *	8
//	CM119A		0d8c	013a *		8
//	CM119B		0d8c	0013		8
//	HS100		0d8c	013c		0
//
//	SSS1621		0c76	1605		2 	per ZL3AME, Can't find data sheet
//	SSS1623		0c76	1607,160b	2	per ZL3AME, Not in data sheet.
//
//				* idProduct programmable by MSEL and MODE pin.
//

// 	CMedia pin	GPIO	Notes
//	----------	----	-----
//	43		1
//	11		2	N.C. for CM108AH, CM108B
//	13		3	Most popular for PTT because it is on the end.
//	15		4
//	16		5	CM109, CM119, CM119A, CM119B only
//	17		6	"
//	20		7	"
//	22		8	"

// Test for supported devices.

#define GOOD_DEVICE(v,p) 	( (v == CMEDIA_VID && ((p >= CMEDIA_PID1_MIN && p <= CMEDIA_PID1_MAX) \
							|| p == CMEDIA_PID_CM108AH \
							|| p == CMEDIA_PID_CM108B \
							|| p == CMEDIA_PID_CM119A \
							|| p == CMEDIA_PID_CM119B )) \
				 || \
				  (v == SSS_VID && (p == SSS_PID1 || p == SSS_PID2 || p == SSS_PID3))  )

// Look out for null source pointer, and avoid buffer overflow on destination.

#define SAFE_STRCPY(to,from)	{ if (from != NULL) { strncpy(to,from,sizeof(to)); to[sizeof(to)-1] = '\0'; } }


// Used to process regular expression matching results.

static void inline substr_se (char *dest, const char *src, int start, int endp1)
{
	int len = endp1 - start;

	if (start < 0 || endp1 < 0 || len <= 0) {
	  dest[0] = '\0';
	  return;
	}
	memcpy (dest, src + start, len);
	dest[len] = '\0';

} /* end substr_se */


/*
 * Result of taking inventory of USB soundcards and USB HIDs.
 */

struct thing_s {
	int vid;		// vendor id
	int pid;		// product id
	char product[32];	// product name (e.g. manufacturer, model)
	char devnode_sound[22];	// e.g. /dev/snd/pcmC0D0p
	char plughw[15];	// Above in more familiar format e.g. plughw:0,0
	char devnode_hidraw[17]; // e.g. /dev/hidraw3
	char devnode_usb[25];	// e.g. /dev/bus/usb/001/012
};

int cm108_inventory (struct thing_s *things, int max_things);


/*-------------------------------------------------------------------
 *
 * Name:	main
 *
 * Purpose:	Test program to list USB audio and HID devices.
 *
 *		sudo apt-get install libudev-dev
 *		gcc -DCM108_MAIN textcolor.c -l udev
 *
 *------------------------------------------------------------------*/

#define MAXX_THINGS 60

#ifdef CM108_MAIN

int main (void)
{
	struct thing_s things[MAXX_THINGS];
	int num_things;
	int i;

	text_color_init (0);    // Turn off text color.

	num_things = cm108_inventory (things, MAXX_THINGS);

	dw_printf ("    VID  PID   %-*s %-*s %-*s %-*s"
#if EXTRA
						" %-*s"
#endif
						"\n", 	(int)sizeof(things[0].product),	"Product",
							(int)sizeof(things[0].devnode_sound), "Sound",
							(int)sizeof(things[0].plughw), "ADEVICE",
							(int)sizeof(things[0].devnode_hidraw), "HID [ptt]"
#if EXTRA
							, (int)sizeof(things[0].devnode_usb), "USB"
#endif
							);

	dw_printf ("    ---  ---   %-*s %-*s %-*s %-*s"
#if EXTRA
						" %-*s"
#endif
						"\n", 	(int)sizeof(things[0].product),	"-------",
							(int)sizeof(things[0].devnode_sound), "-----",
							(int)sizeof(things[0].plughw), "-------",
							(int)sizeof(things[0].devnode_hidraw), "---------"
#if EXTRA
							, (int)sizeof(things[0].devnode_usb), "---"
#endif
							);
	for (i = 0; i < num_things; i++) {

	  dw_printf ("%2s  %04x %04x  %-*s %-*s %-*s %-*s"
#if EXTRA
						" %-*s"
#endif
						"\n", 
							GOOD_DEVICE(things[i].vid,things[i].pid) ? "**" : "  ",
							things[i].vid, things[i].pid,
							(int)sizeof(things[i].product),	things[i].product,
							(int)sizeof(things[i].devnode_sound), things[i].devnode_sound,
							(int)sizeof(things[0].plughw), things[i].plughw,
							(int)sizeof(things[i].devnode_hidraw), things[i].devnode_hidraw
#if EXTRA
							, (int)sizeof(things[i].devnode_usb), things[i].devnode_usb
#endif
							);
	}

	return (0);
}

#endif 	// CM108_MAIN



/*-------------------------------------------------------------------
 *
 * Name:	cm108_inventory
 *
 * Purpose:	Take inventory of USB audio and HID.
 *
 * Inputs:	max_things	- Maximum number of items to collect.
 *
 * Outputs:	things		- Array of items collected.
 *				  Corresponding sound device and HID are merged into one item.
 *
 * Returns:	Number of items placed in things array.
 *		Should be in the range of 0 thru max_things.
 *		-1 for a bad unexpected error.
 *
 *------------------------------------------------------------------*/


int cm108_inventory (struct thing_s *things, int max_things)
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	struct udev_device *parentdev;

	int num_things = 0;
	memset (things, 0, sizeof(struct thing_s) * max_things);

/* 
 * First get a list of the USB audio devices.
 * This is based on the example in http://www.signal11.us/oss/udev/
 */
	udev = udev_new();
	if (!udev) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("INTERNAL ERROR: Can't create udev.\n");
	  return (-1);
	}
	
	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "sound");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(dev_list_entry, devices) {
	  const char *path;
	  path = udev_list_entry_get_name(dev_list_entry);
	  dev = udev_device_new_from_syspath(udev, path);
	  char const *devnode = udev_device_get_devnode(dev);
	  if (devnode != NULL) {
	    parentdev = udev_device_get_parent_with_subsystem_devtype( dev, "usb", "usb_device"); 
	    if (parentdev != NULL) {
	      char const *p;
	      int vid = 0;
	      int pid = 0;

	      p = udev_device_get_sysattr_value(parentdev,"idVendor");
	      if (p != NULL) vid = strtol(p, NULL, 16);
	      p = udev_device_get_sysattr_value(parentdev,"idProduct");
	      if (p != NULL) pid = strtol(p, NULL, 16);

	      if (num_things < max_things) {
	        things[num_things].vid = vid;
	        things[num_things].pid = pid;
	        SAFE_STRCPY (things[num_things].product, udev_device_get_sysattr_value(parentdev,"product"));
	        SAFE_STRCPY (things[num_things].devnode_sound, devnode);
	        SAFE_STRCPY (things[num_things].devnode_usb, udev_device_get_devnode(parentdev));
	        num_things++;
	      }
	      udev_device_unref(parentdev);
	    }
	  }
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

/* 
 * Now merge in all of the USB HID.
 */
	udev = udev_new();
	if (!udev) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("INTERNAL ERROR: Can't create udev.\n");
	  return (-1);
	}

	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "hidraw");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(dev_list_entry, devices) {
	  const char *path;
	  path = udev_list_entry_get_name(dev_list_entry);
	  dev = udev_device_new_from_syspath(udev, path);
	  char const *devnode = udev_device_get_devnode(dev);
	  if (devnode != NULL) {
	    parentdev = udev_device_get_parent_with_subsystem_devtype( dev, "usb", "usb_device"); 
	    if (parentdev != NULL) {
	      char const *p;
	      int vid = 0;
	      int pid = 0;

	      p = udev_device_get_sysattr_value(parentdev,"idVendor");
	      if (p != NULL) vid = strtol(p, NULL, 16);
	      p = udev_device_get_sysattr_value(parentdev,"idProduct");
	      if (p != NULL) pid = strtol(p, NULL, 16);

	      int j, matched = 0;
	      char const *usb = udev_device_get_devnode(parentdev);

	      // Add hidraw name to any matching existing.
	      for (j = 0; j < num_things; j++) {
	        if (things[j].vid == vid && things[j].pid == pid && usb != NULL && strcmp(things[j].devnode_usb,usb) == 0) {
	          matched = 1;
	          SAFE_STRCPY (things[j].devnode_hidraw, devnode);
	        }
	      }

	      // If it did not match to existing, add new entry.
	      if (matched == 0 && num_things < max_things) {
	        things[num_things].vid = vid;
	        things[num_things].pid = pid;
	        SAFE_STRCPY (things[num_things].product, udev_device_get_sysattr_value(parentdev,"product"));
	        SAFE_STRCPY (things[num_things].devnode_hidraw, devnode);
	        SAFE_STRCPY (things[num_things].devnode_usb, usb);
	        num_things++;
	      }
	      udev_device_unref(parentdev);
	    }
	  }
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

/*
 * Seeing the form /dev/snd/pcmC4D0p will be confusing to many because we
 * would generally something like plughw:4,0 for in the direwolf configuration file.
 * Construct the more familiar form.
 */
	int i;
	regex_t pcm_re;
	char emsg[100];
	int e = regcomp (&pcm_re, "pcmC([0-9]+)D([0-9]+)[cp]", REG_EXTENDED);
	if (e) {
	  regerror (e, &pcm_re, emsg, sizeof(emsg));
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("INTERNAL ERROR:  %s:%d: %s\n", __FILE__, __LINE__, emsg);
	  return (-1);
	}

	for (i = 0; i < num_things; i++) {
	  regmatch_t match[3];

	  if (regexec (&pcm_re, things[i].devnode_sound, 3, match, 0) == 0) {
	    char c[32], d[32];
	    substr_se (c, things[i].devnode_sound, match[1].rm_so, match[1].rm_eo);
	    substr_se (d, things[i].devnode_sound, match[2].rm_so, match[2].rm_eo);
	    snprintf (things[i].plughw, sizeof(things[i].plughw), "plughw:%s,%s", c, d);
	  }
	}

	return (num_things);

} /* end cm108_inventory */


/*-------------------------------------------------------------------
 *
 * Name:	cm108_find_ptt
 *
 * Purpose:	Try to find /dev/hidraw corresponding to plughw:n,n
 *
 * Inputs:	output_audio_device
 *				- Device name used in the ADEVICE configuration.
 *				  This would generally be something like plughw:1,0
 *
 *		ptt_device_size	- Size of result area to avoid buffer overflow.
 *
 * Outputs:	ptt_device	- Device name, something like /dev/hidraw2.
 *				  Will be emptry string if no match found.
 *
 * Returns:	none
 *
 *------------------------------------------------------------------*/

void cm108_find_ptt (char *output_audio_device, char *ptt_device,  int ptt_device_size)
{
	struct thing_s things[MAXX_THINGS];
	int num_things;
	int i;

	strlcpy (ptt_device, "", ptt_device_size);
	num_things = cm108_inventory (things, MAXX_THINGS);

	for (i = 0; i < num_things; i++) {

	  if (GOOD_DEVICE(things[i].vid,things[i].pid) ) {
	    if (strcmp(output_audio_device, things[i].plughw) == 0) {
	      strlcpy (ptt_device, things[i].devnode_hidraw, ptt_device_size);
	    }
	  }
	}
	
}  /* end cm108_find_ptt */



/*-------------------------------------------------------------------
 *
 * Name:	cm108_set_gpio_pin
 *
 * Purpose:	Set one GPIO pin of the CM108 or similar.
 *
 * Inputs:	name		- Name of device such as /dev/hidraw2.
 *
 *		num		- GPIO number, range 1 thru 8.
 *
 *		state		- 1 for on, 0 for off.
 *
 * Returns:	0 for success.  -1 for error.
 *
 * Errors:	A descriptive error message will be printed for any problem.
 *
 * Future:	For our initial implementation we are making the simplifying
 *		restriction of using only one GPIO pin per device and limit
 *		configuratin to PTT only.
 *		Longer term, we might want to have DCD, and maybe other
 *		controls thru the same chip.
 *		In this case, we would need to retain bit masks for each
 *		device so new data can be merged with old before sending it out.
 *
 *------------------------------------------------------------------*/

int cm108_set_gpio_pin (char *name, int num, int state)
{
	int iomask;
	int iodata;

	if (num < 1 || num > 8) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("%s CM108 GPIO number %d must be in range of 1 thru 8.\n", name, num);
	  return (-1);
	}

	if (state != 0 && state != 1) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("%s CM108 GPIO state %d must be 0 or 1.\n", name, state);
	  return (-1);
	}

	iomask = 1 << (num - 1);
	iodata = state << (num - 1);

	return (cm108_write (name, iomask, iodata));

}  /* end cm108_set_gpio_pin */


/*-------------------------------------------------------------------
 *
 * Name:	cm108_write
 *
 * Purpose:	Set the GPIO pins of the CM108 or similar.
 *
 * Inputs:	name		- Name of device such as /dev/hidraw2.
 *
 *		iomask		- Bit mask for I/O direction.
 *				  LSB is GPIO1, bit 1 is GPIO2, etc.
 *				  1 for output, 0 for input.
 *
 *		iodata		- Output data, same bit order as iomask.
 *
 * Returns:	0 for success.  -1 for error.
 *
 * Errors:	A descriptive error message will be printed for any problem.
 *
 * Description:	This is the lowest level function.
 *		An application probably wants to use cm108_set_gpio_pin.
 *
 *------------------------------------------------------------------*/

static int cm108_write (char *name, int iomask, int iodata)
{
	int fd;
	struct hidraw_devinfo info;
	char io[5];
	int n;

	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("TEMP DEBUG cm108_write:  %s %d %d\n", name, iomask, iodata);

/*
 * By default, the USB HID are accessible only by root:
 *
 *	crw------- 1 root root 249, 1 ... /dev/hidraw1
 *
 * How should we handle this?
 * Manually changing it will revert back on the next reboot or
 * when the device is removed and reinserted.
 *
 * According to various articles on the Internet, we should be able to
 * add a file to /etc/udev/rules.d.  "99-direwolf-cmedia.rules" would be a
 * suitable name.  The leading number is the order.  We want this to be
 * near the end.  I think the file extension must be ".rules."
 *
 * We could completely open it up to everyone like this:
 *
 *	# Allow ordinary user to access CMedia GPIO for PTT.
 *	SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0d8c", MODE="0666"
 *
 * Whenever we have CMedia USB audio adapter, it should be accessible by everyone.
 * This would not apply to other /dev/hidraw* corresponding to keyboard, mouse, etc.
 *
 * Notice the == (double =) for testing and := for setting a property.
 *
 * If you are concerned about security, you could restrict access to 
 * a particular group, something like this:
 *
 *	SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0d8c", GROUP="audio", MODE="0660"
 *
 * I figure "audio" makes more sense than "gpio" because we need to be part of
 * audio group to use the USB Audio adapter for sound.
 */

	fd = open (name, O_WRONLY);
	if (fd == -1) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not open %s for write, errno=%d\n", name, errno);
	  if (errno == EACCES) {		// 13
	    dw_printf ("Type \"ls -l %s\" and verify that it has audio group rw similar to this:\n", name);
	    dw_printf ("    crw-rw---- 1 root audio 247, 0 Oct  6 19:24 %s\n", name);
	    dw_printf ("rather than root-only access like this:\n");
	    dw_printf ("    crw------- 1 root root 247, 0 Sep 24 09:40 %s\n", name);
	  }
	  return (-1);
	}

	// Just for fun, let's get the device information.

#if 1
	n = ioctl(fd, HIDIOCGRAWINFO, &info);
	if (n == 0) {
	  if ( ! GOOD_DEVICE(info.vendor, info.product)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("ioctl HIDIOCGRAWINFO failed for %s. errno = %d.\n", name, errno);
	  }
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("%s is not a supported device type.  Proceed at your own risk.  vid=%04x pid=%04x\n", name, info.vendor, info.product);
	}
#endif
	// To make a long story short, I think we need 0 for the first two bytes.

	io[0] = 0;
	io[1] = 0;
	io[2] = iomask;
	io[3] = iodata;
	io[4] = 0;

	// Writing 4 bytes fails with errno 32, EPIPE, "broken pipe."
	// Hamlib writes 5 bytes which I don't understand.
	// Writing 5 bytes works.
	// I have no idea why.  From the CMedia datasheet it looks like we need 4.

	n = write (fd, io, sizeof(io));
	if (n != sizeof(io)) {
	  //  Errors observed during development.
	  //  as pi		EACCES          13      /* Permission denied */
	  //  as root		EPIPE           32      /* Broken pipe - Happens if we send 4 bytes */

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Write to %s failed, n=%d, errno=%d\n", name, n, errno);

	  if (errno == EACCES) {
	    dw_printf ("Type \"ls -l %s\" and verify that it has audio group rw similar to this:\n", name);
	    dw_printf ("    crw-rw---- 1 root audio 247, 0 Oct  6 19:24 %s\n", name);
	    dw_printf ("rather than root-only access like this:\n");
	    dw_printf ("    crw------- 1 root root 247, 0 Sep 24 09:40 %s\n", name);
	  }

	  close (fd);
	  return (-1);
	}

	close (fd);
	return (0);

}  /* end cm108_write */

#endif   // ifdef USE_CM108

/* end cm108.c */

	