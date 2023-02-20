//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015, 2022  John Langner, WB2OSZ
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
 * File:	symbols.c
 *
 * Purpose:	Functions related to the APRS symbols
 *
 *------------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>	
#include <string.h>	
#include <ctype.h>	

#include "textcolor.h"
#include "symbols.h"
#include "tt_text.h"


/*
 * APRS symbol tables.
 *
 * Derived from http://www.aprs.org/symbols/symbolsX.txt
 * version of 20 Oct 2009.
 */

/*
 * Primary symbol table.
 */

#define SYMTAB_SIZE 95


static const struct {
		char xy[3];
		char *description;
	} primary_symtab[SYMTAB_SIZE] = {

	/*     00  */	{ "~~", "--no-symbol--" },
	/*  !  01  */	{ "BB", "Police, Sheriff" },
	/*  "  02  */	{ "BC", "reserved  (was rain)" },
	/*  #  03  */	{ "BD", "DIGI (white center)" },
	/*  $  04  */	{ "BE", "PHONE" },
	/*  %  05  */	{ "BF", "DX CLUSTER" },
	/*  &  06  */	{ "BG", "HF GATEway" },
	/*  '  07  */	{ "BH", "Small AIRCRAFT" },
	/*  (  08  */	{ "BI", "Mobile Satellite Station" },
	/*  )  09  */	{ "BJ", "Wheelchair (handicapped)" },
	/*  *  10  */	{ "BK", "SnowMobile" },
	/*  +  11  */	{ "BL", "Red Cross" },
	/*  ,  12  */	{ "BM", "Boy Scouts" },
	/*  -  13  */	{ "BN", "House QTH (VHF)" },
	/*  .  14  */	{ "BO", "X" },
	/*  /  15  */	{ "BP", "Red Dot" },
	/*  0  16  */	{ "P0", "# circle (obsolete)" },
	/*  1  17  */	{ "P1", "TBD" },
	/*  2  18  */	{ "P2", "TBD" },
	/*  3  19  */	{ "P3", "TBD" },
	/*  4  20  */	{ "P4", "TBD" },
	/*  5  21  */	{ "P5", "TBD" },
	/*  6  22  */	{ "P6", "TBD" },
	/*  7  23  */	{ "P7", "TBD" },
	/*  8  24  */	{ "P8", "TBD" },
	/*  9  25  */	{ "P9", "TBD" },
	/*  :  26  */	{ "MR", "FIRE" },
	/*  ;  27  */	{ "MS", "Campground (Portable ops)" },
	/*  <  28  */	{ "MT", "Motorcycle" },
	/*  =  29  */	{ "MU", "RAILROAD ENGINE" },
	/*  >  30  */	{ "MV", "CAR" },
	/*  ?  31  */	{ "MW", "SERVER for Files" },
	/*  @  32  */	{ "MX", "HC FUTURE predict (dot)" },
	/*  A  33  */	{ "PA", "Aid Station" },
	/*  B  34  */	{ "PB", "BBS or PBBS" },
	/*  C  35  */	{ "PC", "Canoe" },
	/*  D  36  */	{ "PD", "" },
	/*  E  37  */	{ "PE", "EYEBALL (Eye catcher!)" },
	/*  F  38  */	{ "PF", "Farm Vehicle (tractor)" },
	/*  G  39  */	{ "PG", "Grid Square (6 digit)" },
	/*  H  40  */	{ "PH", "HOTEL (blue bed symbol)" },
	/*  I  41  */	{ "PI", "TcpIp on air network stn" },
	/*  J  42  */	{ "PJ", "" },
	/*  K  43  */	{ "PK", "School" },
	/*  L  44  */	{ "PL", "PC user" },
	/*  M  45  */	{ "PM", "MacAPRS" },
	/*  N  46  */	{ "PN", "NTS Station" },
	/*  O  47  */	{ "PO", "BALLOON" },
	/*  P  48  */	{ "PP", "Police" },
	/*  Q  49  */	{ "PQ", "TBD" },
	/*  R  50  */	{ "PR", "REC. VEHICLE" },
	/*  S  51  */	{ "PS", "SHUTTLE" },
	/*  T  52  */	{ "PT", "SSTV" },
	/*  U  53  */	{ "PU", "BUS" },
	/*  V  54  */	{ "PV", "ATV" },
	/*  W  55  */	{ "PW", "National WX Service Site" },
	/*  X  56  */	{ "PX", "HELO" },
	/*  Y  57  */	{ "PY", "YACHT (sail)" },
	/*  Z  58  */	{ "PZ", "WinAPRS" },
	/*  [  59  */	{ "HS", "Human/Person (HT)" },
	/*  \  60  */	{ "HT", "TRIANGLE(DF station)" },
	/*  ]  61  */	{ "HU", "MAIL/PostOffice(was PBBS)" },
	/*  ^  62  */	{ "HV", "LARGE AIRCRAFT" },
	/*  _  63  */	{ "HW", "WEATHER Station (blue)" },
	/*  `  64  */	{ "HX", "Dish Antenna" },
	/*  a  65  */	{ "LA", "AMBULANCE" },
	/*  b  66  */	{ "LB", "BIKE" },
	/*  c  67  */	{ "LC", "Incident Command Post" },
	/*  d  68  */	{ "LD", "Fire dept" },
	/*  e  69  */	{ "LE", "HORSE (equestrian)" },
	/*  f  70  */	{ "LF", "FIRE TRUCK" },
	/*  g  71  */	{ "LG", "Glider" },
	/*  h  72  */	{ "LH", "HOSPITAL" },
	/*  i  73  */	{ "LI", "IOTA (islands on the air)" },
	/*  j  74  */	{ "LJ", "JEEP" },
	/*  k  75  */	{ "LK", "TRUCK" },
	/*  l  76  */	{ "LL", "Laptop" },
	/*  m  77  */	{ "LM", "Mic-E Repeater" },
	/*  n  78  */	{ "LN", "Node (black bulls-eye)" },
	/*  o  79  */	{ "LO", "EOC" },
	/*  p  80  */	{ "LP", "ROVER (puppy, or dog)" },
	/*  q  81  */	{ "LQ", "GRID SQ shown above 128 m" },
	/*  r  82  */	{ "LR", "Repeater" },
	/*  s  83  */	{ "LS", "SHIP (pwr boat)" },
	/*  t  84  */	{ "LT", "TRUCK STOP" },
	/*  u  85  */	{ "LU", "TRUCK (18 wheeler)" },
	/*  v  86  */	{ "LV", "VAN" },
	/*  w  87  */	{ "LW", "WATER station" },
	/*  x  88  */	{ "LX", "xAPRS (Unix)" },
	/*  y  89  */	{ "LY", "YAGI @ QTH" },
	/*  z  90  */	{ "LZ", "TBD" },
	/*  {  91  */	{ "J1", "" },
	/*  |  92  */	{ "J2", "TNC Stream Switch" },
	/*  }  93  */	{ "J3", "" },
	/*  ~  94  */	{ "J3", "TNC Stream Switch" } };

/*
 * Alternate symbol table.
 */

static const struct {
		char xy[3];
		char *description;
	} alternate_symtab[SYMTAB_SIZE] = {

	/*     00  */	{ "~~", "--no-symbol--" },
	/*  !  01  */	{ "OB", "EMERGENCY (!)" },
	/*  "  02  */	{ "OC", "reserved" },
	/*  #  03  */	{ "OD", "OVERLAY DIGI (green star)" },
	/*  $  04  */	{ "OE", "Bank or ATM  (green box)" },
	/*  %  05  */	{ "OF", "Power Plant with overlay" },
	/*  &  06  */	{ "OG", "I=Igte IGate R=RX T=1hopTX 2=2hopTX" },
	/*  '  07  */	{ "OH", "Crash (& now Incident sites)" },
	/*  (  08  */	{ "OI", "CLOUDY (other clouds w ovrly)" },
	/*  )  09  */	{ "OJ", "Firenet MEO, MODIS Earth Obs." },
	/*  *  10  */	{ "OK", "SNOW (& future ovrly codes)" },
	/*  +  11  */	{ "OL", "Church" },
	/*  ,  12  */	{ "OM", "Girl Scouts" },
	/*  -  13  */	{ "ON", "House (H=HF) (O = Op Present)" },
	/*  .  14  */	{ "OO", "Ambiguous (Big Question mark)" },
	/*  /  15  */	{ "OP", "Waypoint Destination" },
	/*  0  16  */	{ "A0", "CIRCLE (E/I/W=IRLP/Echolink/WIRES)" },
	/*  1  17  */	{ "A1", "" },
	/*  2  18  */	{ "A2", "" },
	/*  3  19  */	{ "A3", "" },
	/*  4  20  */	{ "A4", "" },
	/*  5  21  */	{ "A5", "" },
	/*  6  22  */	{ "A6", "" },
	/*  7  23  */	{ "A7", "" },
	/*  8  24  */	{ "A8", "802.11 or other network node" },
	/*  9  25  */	{ "A9", "Gas Station (blue pump)" },
	/*  :  26  */	{ "NR", "Hail (& future ovrly codes)" },
	/*  ;  27  */	{ "NS", "Park/Picnic area" },
	/*  <  28  */	{ "NT", "ADVISORY (one WX flag)" },
	/*  =  29  */	{ "NU", "APRStt Touchtone (DTMF users)" },
	/*  >  30  */	{ "NV", "OVERLAID CAR" },
	/*  ?  31  */	{ "NW", "INFO Kiosk  (Blue box with ?)" },
	/*  @  32  */	{ "NX", "HURRICANE/Trop-Storm" },
	/*  A  33  */	{ "AA", "overlayBOX DTMF & RFID & XO" },
	/*  B  34  */	{ "AB", "Blwng Snow (& future codes)" },
	/*  C  35  */	{ "AC", "Coast Guard" },
	/*  D  36  */	{ "AD", "Drizzle (proposed APRStt)" },
	/*  E  37  */	{ "AE", "Smoke (& other vis codes)" },
	/*  F  38  */	{ "AF", "Freezng rain (&future codes)" },
	/*  G  39  */	{ "AG", "Snow Shwr (& future ovrlys)" },
	/*  H  40  */	{ "AH", "Haze (& Overlay Hazards)" },
	/*  I  41  */	{ "AI", "Rain Shower" },
	/*  J  42  */	{ "AJ", "Lightning (& future ovrlys)" },
	/*  K  43  */	{ "AK", "Kenwood HT (W)" },
	/*  L  44  */	{ "AL", "Lighthouse" },
	/*  M  45  */	{ "AM", "MARS (A=Army,N=Navy,F=AF)" },
	/*  N  46  */	{ "AN", "Navigation Buoy" },
	/*  O  47  */	{ "AO", "Rocket" },
	/*  P  48  */	{ "AP", "Parking" },
	/*  Q  49  */	{ "AQ", "QUAKE" },
	/*  R  50  */	{ "AR", "Restaurant" },
	/*  S  51  */	{ "AS", "Satellite/Pacsat" },
	/*  T  52  */	{ "AT", "Thunderstorm" },
	/*  U  53  */	{ "AU", "SUNNY" },
	/*  V  54  */	{ "AV", "VORTAC Nav Aid" },
	/*  W  55  */	{ "AW", "# NWS site (NWS options)" },
	/*  X  56  */	{ "AX", "Pharmacy Rx (Apothicary)" },
	/*  Y  57  */	{ "AY", "Radios and devices" },
	/*  Z  58  */	{ "AZ", "" },
	/*  [  59  */	{ "DS", "W.Cloud (& humans w Ovrly)" },
	/*  \  60  */	{ "DT", "New overlayable GPS symbol" },
	/*  ]  61  */	{ "DU", "" },
	/*  ^  62  */	{ "DV", "# Aircraft (shows heading)" },
	/*  _  63  */	{ "DW", "# WX site (green digi)" },
	/*  `  64  */	{ "DX", "Rain (all types w ovrly)" },
	/*  a  65  */	{ "SA", "ARRL, ARES, WinLINK" },
	/*  b  66  */	{ "SB", "Blwng Dst/Snd (& others)" },
	/*  c  67  */	{ "SC", "CD triangle RACES/SATERN/etc" },
	/*  d  68  */	{ "SD", "DX spot by callsign" },
	/*  e  69  */	{ "SE", "Sleet (& future ovrly codes)" },
	/*  f  70  */	{ "SF", "Funnel Cloud" },
	/*  g  71  */	{ "SG", "Gale Flags" },
	/*  h  72  */	{ "SH", "Store. or HAMFST Hh=HAM store" },
	/*  i  73  */	{ "SI", "BOX or points of Interest" },
	/*  j  74  */	{ "SJ", "WorkZone (Steam Shovel)" },
	/*  k  75  */	{ "SK", "Special Vehicle SUV,ATV,4x4" },
	/*  l  76  */	{ "SL", "Areas      (box,circles,etc)" },
	/*  m  77  */	{ "SM", "Value Sign (3 digit display)" },
	/*  n  78  */	{ "SN", "OVERLAY TRIANGLE" },
	/*  o  79  */	{ "SO", "small circle" },
	/*  p  80  */	{ "SP", "Prtly Cldy (& future ovrlys)" },
	/*  q  81  */	{ "SQ", "" },
	/*  r  82  */	{ "SR", "Restrooms" },
	/*  s  83  */	{ "SS", "OVERLAY SHIP/boat (top view)" },
	/*  t  84  */	{ "ST", "Tornado" },
	/*  u  85  */	{ "SU", "OVERLAID TRUCK" },
	/*  v  86  */	{ "SV", "OVERLAID Van" },
	/*  w  87  */	{ "SW", "Flooding" },
	/*  x  88  */	{ "SX", "Wreck or Obstruction ->X<-" },
	/*  y  89  */	{ "SY", "Skywarn" },
	/*  z  90  */	{ "SZ", "OVERLAID Shelter" },
	/*  {  91  */	{ "Q1", "Fog (& future ovrly codes)" },
	/*  |  92  */	{ "Q2", "TNC Stream Switch" },
	/*  }  93  */	{ "Q3", "" },
	/*  ~  94  */	{ "Q4", "TNC Stream Switch" } };


// Make sure the array is null terminated.
// If search order is changed, do the same in decode_aprs.c for consistency.

static const char *search_locations[] = {
	(const char *) "symbols-new.txt",		// CWD
	(const char *) "data/symbols-new.txt",		// Windows with Cmake
	(const char *) "../data/symbols-new.txt",	// ?
#ifndef __WIN32__
	(const char *) "/usr/local/share/direwolf/symbols-new.txt",
	(const char *) "/usr/share/direwolf/symbols-new.txt",
#endif
#if __APPLE__
	// https://groups.yahoo.com/neo/groups/direwolf_packet/conversations/messages/2458
	// Adding the /opt/local tree since macports typically installs there.  Users might want their
	// INSTALLDIR (see Makefile.macosx) to mirror that.  If so, then we need to search the /opt/local
	// path as well.
	(const char *) "/opt/local/share/direwolf/symbols-new.txt",
#endif
	(const char *) NULL		// Important - Indicates end of list.
};


/*------------------------------------------------------------------
 *
 * Function:	symbols_init
 *
 * Purpose:	Initialization for functions related to symbols.
 *
 * Inputs:	
 *
 * Global output:
 *		new_sym_ptr
 *		new_sym_size
 *		new_sym_len
 *
 * Description:	The primary and alternate symbol tables are constant
 *		so they are hardcoded.
 *		However the "new" sysmbols, which give new meanings to
 *		OVERLAID symbols, are always evolving.
 *		For maximum flexibility, we will read the
 *		data file at run time rather than compiling it in.
 *
 *		For the most recent version, download from:
 *
 *		http://www.aprs.org/symbols/symbols-new.txt
 *
 *		Windows version:  File must be in current working directory.
 *
 *		Linux version: Search order is current working directory
 *			then /usr/share/direwolf directory.
 *
 *------------------------------------------------------------------*/

#define NEW_SYM_INIT_SIZE 20
#define NEW_SYM_DESC_LEN 29

typedef struct new_sym_s {
	char overlay;
	char symbol;
	char *description;
} new_sym_t;

static new_sym_t *new_sym_ptr = NULL;	/* Dynamically allocated array. */
static int new_sym_size = 0;		/* Number of elements allocated. */
static int new_sym_len = 0;			/* Number of elements used. */


void symbols_init (void)
{
	FILE *fp = NULL;

/*
 * We only care about lines with this format:
 *
 *  Column 1 - overlay character of / \ upper case or digit
 *  Column 2 - symbol in range of ! thru ~
 *  Column 3 - space
 *  Column 4 - equal sign
 *  Column 5 - space
 *  Column 6 - Start of description.
 */

#define COL1_OVERLAY 0
#define COL2_SYMBOL 1
#define COL3_SP 2
#define COL4_EQUAL 3
#define COL5_SP 4
#define COL6_DESC 5

	char stuff[200];
	int j;

// Feb. 2022 - Noticed that some lines have - rather than =.

// LD = LIght Rail or Subway (new Aug 2014)
// SD = Seaport Depot (new Aug 2014)
// DIGIPEATERS
// /# - Generic digipeater
// 1# - WIDE1-1 digipeater


#define GOOD_LINE(x) (strlen(x) > 6 && \
			(x[COL1_OVERLAY] == '/' || x[COL1_OVERLAY] == '\\' || isupper(x[COL1_OVERLAY]) || isdigit(x[COL1_OVERLAY])) \
			&& x[COL2_SYMBOL] >= '!' && x[COL2_SYMBOL] <= '~' \
			&& x[COL3_SP] == ' ' \
			&& (x[COL4_EQUAL] == '=' || x[COL4_EQUAL] == '-') \
			&& x[COL5_SP] == ' ' \
			&& x[COL6_DESC] != ' ')

	if (new_sym_ptr != NULL) {
	  return;			/* was called already. */
	}

// If search strategy changes, be sure to keep decode_tocall in sync.

	fp = NULL;
	j = 0;
	do {
	  if (search_locations[j] == NULL) break;
	  fp = fopen(search_locations[j++], "r");
	} while (fp == NULL);

	if (fp == NULL) {

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Warning: Could not open 'symbols-new.txt'.\n");
	  dw_printf ("The \"new\" OVERLAID character information will not be available.\n");

	  new_sym_size = 1;	
	  new_sym_ptr = calloc(new_sym_size, sizeof(new_sym_t));  /* Don't try again. */
	  new_sym_len = 0;	
	  return;
	}

/*
 * Count number of interesting lines and allocate storage.
 */
	while (fgets(stuff, sizeof(stuff), fp) != NULL) {
	  if (GOOD_LINE(stuff)) {
	    new_sym_size++;
	  }
	}

	new_sym_ptr = calloc(new_sym_size, sizeof(new_sym_t));

/*
 * Rewind, read again, and save contents of interesting lines. 
 */
	rewind (fp);

	while (fgets(stuff, sizeof(stuff), fp) != NULL) {

	  if (GOOD_LINE(stuff)) {
	    for (j = strlen(stuff+COL6_DESC) - 1; j>=0 && stuff[COL6_DESC+j] <= ' '; j--) {
	      stuff[COL6_DESC+j] = '\0';
	    }
	    new_sym_ptr[new_sym_len].overlay = stuff[COL1_OVERLAY];
	    new_sym_ptr[new_sym_len].symbol = stuff[COL2_SYMBOL];
	    new_sym_ptr[new_sym_len].description = strdup(stuff+COL6_DESC);
	    new_sym_len++;
	  }
	}
	fclose (fp);

	assert (new_sym_len == new_sym_size);

#if 0
	for (j=0; j<new_sym_len; j++) {
	  dw_printf ("%02d: %c %c '%s'\n", j, new_sym_ptr[j].overlay,
		new_sym_ptr[j].symbol, new_sym_ptr[j].description);
	}
#endif

} /* end symbols_init */


/*------------------------------------------------------------------
 *
 * Function:	symbols_list
 *
 * Purpose:	Print a list of all the symbols.
 *
 * Inputs:	none
 *
 *------------------------------------------------------------------*/

void symbols_list (void) 
{
	int n;

	dw_printf ("\n");

	dw_printf ("\tPRIMARY SYMBOL TABLE\n");
	dw_printf ("\n");
	dw_printf ("sym  GPSxy  GPSCnn  APRStt  Icon\n");
	dw_printf ("---  -----  ------  ------  ----\n");
	for (n = 1; n < SYMTAB_SIZE; n++) {
	  dw_printf (" /%c     %s      %02d  AB1%02d   %s\n", n + ' ', primary_symtab[n].xy, n, n, primary_symtab[n].description);
	}

	dw_printf ("\n");
	dw_printf ("\tALTERNATE SYMBOL TABLE\n");
	dw_printf ("\n");
	dw_printf ("sym  GPSxy  GPSEnn  APRStt  Icon\n");
	dw_printf ("---  -----  ------  ------  ----\n");
	for (n = 1; n < SYMTAB_SIZE; n++) {
	  dw_printf (" \\%c     %s      %02d  AB2%02d   %s\n", n + ' ', alternate_symtab[n].xy, n, n, alternate_symtab[n].description);
	}

	dw_printf ("\n");
	dw_printf ("\tNEW SYMBOLS from symbols-new.txt\n");
	dw_printf ("\n");
	dw_printf ("sym  GPSxyz  GPSxnn  APRStt   Icon\n");
	dw_printf ("---  ------  ------  ------   ----\n");


	for (n = 0; n < new_sym_len; n++) {

	  int overlay = new_sym_ptr[n].overlay;
	  int symbol = new_sym_ptr[n].symbol;
	  char tones[12];

	  symbols_to_tones (overlay, symbol, tones, sizeof(tones));

	  if (overlay == '/') {

	    dw_printf (" %c%c     %s%c     C%02d  %-7s  %s\n", overlay, symbol, 
								primary_symtab[symbol - ' '].xy, ' ',
								symbol - ' ', tones,
								new_sym_ptr[n].description);
	  }
	  else if (isupper(overlay) || isdigit(overlay)) {

	    dw_printf (" %c%c     %s%c          %-7s  %s\n", overlay, symbol, 
								alternate_symtab[symbol - ' '].xy, overlay,
								tones,
								new_sym_ptr[n].description);
	  }
	  else {

	    dw_printf (" %c%c     %s%c     E%02d  %-7s  %s\n", overlay, symbol, 
								alternate_symtab[symbol - ' '].xy, ' ', 
								symbol - ' ', tones,
								new_sym_ptr[n].description);
	  }
	}
	dw_printf ("\n");
	dw_printf ("More information here: http://www.aprs.org/symbols.html\n");

} /* end symbols_list */



/*------------------------------------------------------------------
 *
 * Function:	symbols_from_dest_or_src
 *
 * Purpose:	Extract symbol from destination or source.
 *
 * Inputs:	dti	- Data type indicator.
 *
 *		src	- Source address with SSID.
 *		
 *		dest	- Destination address.
 *			  Don't care if SSID is present or not.
 *
 * Outputs:	*symtab
 *		*symbol
 *
 * Description:	There are 3 different ways to specify the symbol,
 *		in this order of precedence:
 *	
 *		- Information field.  This was done already in the
 *		  separate decoders for different message types.
 *
 *		If not set already,
 *
 *		- The destination address if it has certain formats
 *		  starting with GPS, SPC, or SYM which are equivalent
 *		  for our purposes.
 *		  (Not for MIC-E where destination has a special use.)
 *
 *		If all else fails,
 *
 *		- SSID of the source address.
 *
 *
 *------------------------------------------------------------------*/

static const char ssid_to_sym[16] = {
	  ' ',	/* 0 - No icon. */
	  'a',	/* 1 - Ambulance */
	  'U',	/* 2 - Bus */
	  'f',	/* 3 - Fire Truck */
	  'b',	/* 4 - Bicycle */
	  'Y',	/* 5 - Yacht */
	  'X',	/* 6 - Helicopter */
	  '\'',	/* 7 - Small Aircraft */
	  's',	/* 8 - Ship */
	  '>',	/* 9 - Car */
	  '<',	/* 10 - Motorcycle */
	  'O',	/* 11 - Ballon */
	  'j',	/* 12 - Jeep */
	  'R',	/* 13 - Recreational Vehicle */
	  'k',	/* 14 - Truck */
	  'v' 	/* 15 - Van */
	};


void symbols_from_dest_or_src (char dti, char *src, char *dest, char *symtab, char *symbol)
{
	char *p;


/*
 * This part does not apply to MIC-E format because the destination
 * is used to encode latitude and other information.
 */
	if (dti != '\'' && dti != '`') {

/* 
 * For GPSCnn, nn is the index into the primary symbol table.
 */

	  if (strncmp(dest, "GPSC", 4) == 0)
	  {
	    int nn;
	  
	    nn = atoi(dest+4);
	    if (nn >= 1 && nn <= 94) {
	      *symtab = '/';		/* Primary */
	      *symbol = ' ' + nn;	
	      return;
	    }
	  }

/* 
 * For GPSEnn, nn is the index into the primary symbol table.
 */

	  if (strncmp(dest, "GPSE", 4) == 0)
	  {
	    int nn;
	  
	    nn = atoi(dest+4);
	    if (nn >= 1 && nn <= 94) {
	      *symtab = '\\';		/* Alternate. */
	      *symbol = ' ' + nn;	
	      return;
	    }
	  }

/*
 * For GPSxy or SPCxy or SYMxy, look up xy in the translation tables.
 * First search the primary table.
 */

	  if (strncmp(dest, "GPS", 3) == 0 ||
	      strncmp(dest, "SPC", 3) == 0 ||
	      strncmp(dest, "SYM", 3) == 0) 
	  {
	    char xy[3];
	    int nn;
	  
	    xy[0] = dest[3];
	    xy[1] = dest[4];
	    xy[2] = '\0';

	    for (nn = 1; nn <= 94; nn++) {
	      if (strcmp(xy, primary_symtab[nn].xy) == 0) {
	        *symtab = '/';		/* Primary. */
	        *symbol = ' ' + nn;
	        return;
	      }
	    }
	  }			

/*
 * Next, search the alternate table.
 * This time, we can have the format ...xyz, where z is an overlay character.
 * Only upper case letters and digits are valid overlay characters.
 */

	  if (strncmp(dest, "GPS", 3) == 0 ||
	      strncmp(dest, "SPC", 3) == 0 ||
	      strncmp(dest, "SYM", 3) == 0) 
	  {
	    char xy[3];
	    char z;
	    int nn;
	  
	    xy[0] = dest[3];
	    xy[1] = dest[4];
	    xy[2] = '\0';
	    z = dest[5];

	    for (nn = 1; nn <= 94; nn++) {
	      if (strcmp(xy, alternate_symtab[nn].xy) == 0) {
	        *symtab = '\\';		/* Alternate. */
	        *symbol = ' ' + nn;
		if (isupper((int)z) || isdigit((int)z)) {
	          *symtab = z;
	        }
	        return;
	      }
	    }
	  }

	}  /* end not MIC-E */

/*
 * When all else fails, use source SSID.
 * This is totally non-obvious and confusing, but it is in the APRS protocol spec.
 * Chapter 20, "Symbol in the Source Address SSID"
 */

// January 2022 - Every time this shows up, it confuses people terribly.
// e.g. An APRS "message" shows up with Bus or Motorcycle in the description.

// The position and object formats all contain a proper symbol and table.
// There doesn't seem to be much reason to have a symbol for something without
// a position because it would not show up on a map.
// This just seems to be a remnant of something used long ago and no longer needed.
// The protocol spec mentions a "MIM tracker" but I can't find any references to it.

// If this was completely removed, no one would probably ever notice.
// The only possible useful case I can think of would be someone sending a
// NMEA string directly from a GPS receiver and wanting to keep the destination field
// for the system type.

	if (dti == '$') {

	  p = strchr (src, '-');
	  if (p != NULL) {
	    int ssid = atoi(p+1);
	    if (ssid >= 1 && ssid <= 15) {
	      *symtab = '/';		/* All in Primary table. */
	      *symbol = ssid_to_sym[ssid];
	      return;
	    }
	  }
	}

} /* symbols_from_dest_or_src */



/*------------------------------------------------------------------
 *
 * Function:	symbols_into_dest
 *
 * Purpose:	Encode symbol for destination field.
 *
 * Inputs:	symtab		/, \, 0-9, A-Z
 *		symbol		any printable character ! to ~ 
 *
 * Outputs:	dest		6 character "destination" of the forms 
 *					GPSCnn  - primary table.
 *					GPSEnn  - alternate table.
 *					GPSxyz  - alternate with overlay.
 *
 * Returns:	0 for success, 1 for error.
 *
 *------------------------------------------------------------------*/

int symbols_into_dest (char symtab, char symbol, char *dest)
{

	if (symbol >= '!' && symbol <= '~' && symtab == '/') {
	  
	  /* Primary Symbol table. */
	  snprintf (dest, 7, "GPSC%02d", symbol - ' ');
	  return (0);
	}
	else if (symbol >= '!' && symbol <= '~' && symtab == '\\') {
	  
	  /* Alternate Symbol table. */
	  snprintf (dest, 7, "GPSE%02d", symbol - ' ');
	  return (0);
	}
	else if (symbol >= '!' && symbol <= '~' && (isupper(symtab) || isdigit(symtab))) {

	  /* Alternate Symbol table with overlay. */
	  snprintf (dest, 7, "GPS%s%c", alternate_symtab[symbol - ' '].xy, symtab);
	  return (0);
	}


	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Could not convert symbol \"%c%c\" to GPSxyz destination format.\n",
			symtab, symbol);

	strlcpy (dest, "GPS???", sizeof(dest));	/* Error. */
	return (1);
}


/*------------------------------------------------------------------
 *
 * Function:	symbols_get_description
 *
 * Purpose:	Get description for given symbol table/code/overlay.
 *
 * Inputs:	symtab		/, \, 0-9, A-Z
 *		symbol		any printable character ! to ~ 
 *
 *		desc_size	Size of description provided by caller
 *				so we can avoid buffer overflow.
 *
 * Outputs:	description	Text description.
 *				"--no-symbol--"  if error.
 *
 *	 
 * Description:	This is used for the monitoring and the 
 *		decode_aprs utility.
 *
 *------------------------------------------------------------------*/

void symbols_get_description (char symtab, char symbol, char *description, size_t desc_size)
{
	char tmp2[2];
	int j;

	symbols_init();


// The symbol table identifier should be 
//	/	for symbol from primary table
//	\	for symbol from alternate table
//	0-9,A-Z	for alternate symbol table with overlay character

	if (symtab != '/' &&
	    symtab != '\\' &&
	    ! isdigit(symtab) &&
	    ! isupper(symtab)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Symbol table identifier is not '/' (primary), '\\' (alternate), or valid overlay character.\n");
	
	  /* Possibilities: */
	  /* Select primary table and keep going, or */
	  /* report no symbol.  It IS an error. */
	  /* We do the latter. */

	  symbol = ' ';
	  strlcpy (description, primary_symtab[symbol-' '].description, desc_size);
	  return;
	}

// Bounds check before using symbol as index into table.

	if (symbol < ' ' || symbol > '~') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Symbol code is not a printable character.\n");
	  symbol = ' ';		/* Avoid subscript out of bounds. */	  
	}

// First try to match with the "new" symbols.

	for (j=0; j<new_sym_len; j++) {
	  if (symtab == new_sym_ptr[j].overlay && symbol == new_sym_ptr[j].symbol) {
	    strlcpy (description, new_sym_ptr[j].description, desc_size);
	    return;
	  }
	}  

// Otherwise use the original symbol tables.

	if (symtab == '/') {
	  strlcpy (description, primary_symtab[symbol-' '].description, desc_size);
	}
	else {
	  strlcpy (description, alternate_symtab[symbol-' '].description, desc_size);
	  if (symtab != '\\') {
	    strlcat (description, " w/overlay ", desc_size);
	    tmp2[0] = symtab;
	    tmp2[1] = '\0';
	    strlcat (description, tmp2, desc_size);
	  }
	}

} /* end symbols_get_description */


/*------------------------------------------------------------------
 *
 * Function:	symbols_code_from_description
 *
 * Purpose:	Find a suitable table/symbol based on given description.
 *
 * Inputs:	overlay		Explicit overlay or space.
 *		description	Substring of one of the descriptions.
 *				Example: dog
 *
 * Outputs:	symtab		/, \, 0-9, A-Z
 *		symbol		any printable character ! to ~ 
 *
 * Returns:	1 for success, 0 for failure.
 *
 *------------------------------------------------------------------*/

int symbols_code_from_description (char overlay, char *description, char *symtab, char *symbol)
{
	int j;

	symbols_init();

/*
 * If user specified a particular overlay (i.e. for config file BEACON), 
 * first try the alternate symbol table.
 * If that fails should we give up or ignore the overlay and keep trying?
 */

	if (isupper(overlay) || isdigit(overlay)) {

	  for (j=0; j<SYMTAB_SIZE; j++) {
	    if (strcasestr(alternate_symtab[j].description, description) != NULL) {
	      *symtab = overlay;
	      *symbol = j + ' ';
	      return (1);
	    }
	  }
	  /* If that fails should we give up or ignore the overlay and keep trying? */
	}

/*
 * Search primary table.
 */
	for (j=0; j<SYMTAB_SIZE; j++) {
	  if (strcasestr(primary_symtab[j].description, description) != NULL) {
	    *symtab = '/';
	    *symbol = j + ' ';
	    return (1);
	  }
	}

/*
 * Search alternate table.
 */
	for (j=0; j<SYMTAB_SIZE; j++) {
	  if (strcasestr(alternate_symtab[j].description, description) != NULL) {
	    *symtab = '\\';
	    *symbol = j + ' ';
	    return (1);
	  }
	}

/*
 * Finally, the "new" symbols.
 * Probably want this last so get get the most standard and
 * generic for queries such as "house" or ...
 */
	for (j=0; j<new_sym_len; j++) {
	  if (strcasestr(new_sym_ptr[j].description, description) != NULL) {
	    *symtab = new_sym_ptr[j].overlay;
	    *symbol = new_sym_ptr[j].symbol;
	    return (1);
	  }
	}

/*
 * Default to something generic like house.  
 * Caller is responsible for issuing error message.
 */
	*symtab = '/';
	*symbol = '-';
	return (0);

} /* end symbols_code_from_description */



/*------------------------------------------------------------------
 *
 * Function:	symbols_to_tones
 *
 * Purpose:	Convert symbol to APRStt tone sequence.
 *
 * Inputs:	symtab/overlay
 *		symbol
 *		tonessiz	- Amount of space available for result.
 *
 * Output:	tones	- string of AB...		
 *		
 * Description: 
 *
 *		Primary: 	AB1nn		nn = same number as GPSCnn
 *		Alternate:	AB2nn 		nn = same number as GPSEnn
 *		with overlay:	AB0nntt   	nn = same as with alternate
 *						tt = one or two tones from two key method.
 *
 *------------------------------------------------------------------*/

void symbols_to_tones (char symtab, char symbol, char *tones, size_t tonessiz)
{

	if (symtab == '/') {

	  snprintf (tones, tonessiz, "AB1%02d", symbol - ' ');
	}
	else if (isupper(symtab) || isdigit(symtab)) {

	  char text[2];
	  char tt[8];

	  text[0] = symtab;
	  text[1] = '\0';

	  tt_text_to_two_key (text, 0, tt);

	  snprintf (tones, tonessiz, "AB0%02d%s", symbol - ' ', tt);
	}
	else {
	 
	  snprintf (tones, tonessiz, "AB2%02d", symbol - ' ');
	}

}  /* end symbols_to_tones */




#if 0

/* Quick, incomplete, unit test. */
/* gcc -g symbols.c textcolor.c misc.a */

int main (int argc, char *argv[])
{
	char symtab;
	char symbol;
	char dest[8];
	char description[50];

	symbols_init ();



	symbols_from_dest_or_src ('T', "W1ABC", "GPSC43", &symtab, &symbol);
	if (symtab != '/' || symbol != 'K') dw_printf ("ERROR 1-1\n");

	symbols_from_dest_or_src ('T', "W1ABC", "GPSE87", &symtab, &symbol);
	if (symtab != '\\' || symbol != 'w') dw_printf ("ERROR 1-2\n");

	symbols_from_dest_or_src ('T', "W1ABC", "SPCBL", &symtab, &symbol);
	if (symtab != '/' || symbol != '+') dw_printf ("ERROR 1-3\n");

	symbols_from_dest_or_src ('T', "W1ABC", "SYMST", &symtab, &symbol);
	if (symtab != '\\' || symbol != 't') dw_printf ("ERROR 1-4\n");

	symbols_from_dest_or_src ('T', "W1ABC", "GPSOD9", &symtab, &symbol);
	if (symtab != '9' || symbol != '#') dw_printf ("ERROR 1-5\n");

	symbols_from_dest_or_src ('T', "W1ABC-14", "XXXXXX", &symtab, &symbol);
	if (symtab != '/' || symbol != 'k') dw_printf ("ERROR 1-6\n");

	symbols_from_dest_or_src ('T', "W1ABC", "GPS???", &symtab, &symbol);
	/* Outputs are left alone if symbol can't be determined. */
	if (symtab != '/' || symbol != 'k') dw_printf ("ERROR 1-7\n");


	symbols_into_dest ('/', 'K', dest);
	if (strcmp(dest, "GPSC43") != 0) dw_printf ("ERROR 2-1\n");

	symbols_into_dest ('\\', 'w', dest);
	if (strcmp(dest, "GPSE87") != 0) dw_printf ("ERROR 2-2\n");

	symbols_into_dest ('3', 'A', dest);
	if (strcmp(dest, "GPSAA3") != 0) dw_printf ("ERROR 2-3\n");

// Expect to see this:
//   Could not convert symbol " A" to GPSxyz destination format.
//   Could not convert symbol "/ " to GPSxyz destination format.

	symbols_into_dest (' ', 'A', dest);
	if (strcmp(dest, "GPS???") != 0) dw_printf ("ERROR 2-4\n");

	symbols_into_dest ('/', ' ', dest);
	if (strcmp(dest, "GPS???") != 0) dw_printf ("ERROR 2-5\n");



	symbols_get_description ('J', 's', description);
	if (strcmp(description, "Jet Ski") != 0) dw_printf ("ERROR 3-1\n");

	symbols_get_description ('/', 'O', description);
	if (strcmp(description, "BALLOON") != 0) dw_printf ("ERROR 3-2\n");

	symbols_get_description ('\\', 'T', description);
	if (strcmp(description, "Thunderstorm") != 0) dw_printf ("ERROR 3-3\n");

	symbols_get_description ('5', 'T', description);
	if (strcmp(description, "Thunderstorm w/overlay 5") != 0) dw_printf ("ERROR 3-4\n");

// Expect to see this:
//   Symbol table identifier is not '/' (primary), '\' (alternate), or valid overlay character.

	symbols_get_description (' ', 'T', description);
	if (strcmp(description, "--no-symbol--") != 0) dw_printf ("ERROR 3-5\n");

	symbols_get_description ('/', ' ', description);
	if (strcmp(description, "--no-symbol--") != 0) dw_printf ("ERROR 3-6\n");



	symbols_code_from_description ('5', "girl scouts", &symtab, &symbol);
	if (symtab != '5' || symbol != ',') dw_printf ("ERROR 4-1\n");

	symbols_code_from_description (' ', "scouts", &symtab, &symbol);
	if (symtab != '/' || symbol != ',') dw_printf ("ERROR 4-2\n");

	symbols_code_from_description (' ', "girl scouts", &symtab, &symbol);
	if (symtab != '\\' || symbol != ',') dw_printf ("ERROR 4-3\n");

	symbols_code_from_description (' ', "jet ski", &symtab, &symbol);
	if (symtab != 'J' || symbol != 's') dw_printf ("ERROR 4-4\n");

	symbols_code_from_description (' ', "girl scouts", &symtab, &symbol);
	if (symtab != '\\' || symbol != ',') dw_printf ("ERROR 4-5\n");

	symbols_code_from_description (' ', "yen", &symtab, &symbol);
	if (symtab != 'Y' || symbol != '$') dw_printf ("ERROR 4-6\n");

	symbols_code_from_description (' ', "taco bell", &symtab, &symbol);
	if (symtab != 'T' || symbol != 'R') dw_printf ("ERROR 4-7\n");


} /* end main */

#endif

/* end symbols.c */
