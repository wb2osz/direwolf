

/* 
 * MGN_icon.h 
 *
 * Waypoint icon codes for use in the $PMGNWPL sentence.
 *
 * Derived from Data Transmission Protocol For Magellan Products - version 2.11, March 2003
 *
 * http://www.gpsinformation.org/mag-proto-2-11.pdf
 *
 *
 * That's 13 years ago.  There should be something newer available but I can't find it.
 *
 * The is based on the newer models at the time.  Earlier models had shorter incompatible icon lists.
 */



#define MGN_crossed_square "a"
#define MGN_box "b"
#define MGN_house "c"
#define MGN_aerial "d"
#define MGN_airport "e"
#define MGN_amusement_park "f"
#define MGN_ATM "g"
#define MGN_auto_repair "h"
#define MGN_boating "I"
#define MGN_camping "j"
#define MGN_exit_ramp "k"
#define MGN_first_aid "l"
#define MGN_nav_aid "m"
#define MGN_buoy "n"
#define MGN_fuel "o"
#define MGN_garden "p"
#define MGN_golf "q"
#define MGN_hotel "r"
#define MGN_hunting_fishing "s"
#define MGN_large_city "t"
#define MGN_lighthouse "u"
#define MGN_major_city "v"
#define MGN_marina "w"
#define MGN_medium_city "x"
#define MGN_museum "y"
#define MGN_obstruction "z"
#define MGN_park "aa"
#define MGN_resort "ab"
#define MGN_restaurant "ac"
#define MGN_rock "ad"
#define MGN_scuba "ae"
#define MGN_RV_service "af"
#define MGN_shooting "ag"
#define MGN_sight_seeing "ah"
#define MGN_small_city "ai"
#define MGN_sounding "aj"
#define MGN_sports_arena "ak"
#define MGN_tourist_info "al"
#define MGN_truck_service "am"
#define MGN_winery "an"
#define MGN_wreck "ao"
#define MGN_zoo "ap"


/*
 * Mapping from APRS symbols to Magellan.
 *
 * This is a bit of a challenge because there 
 * are no icons for moving objects.
 * We can use airport for flying things but 
 * what about wheeled transportation devices?
 */

// TODO:  NEEDS MORE WORK!!!


#define MGN_default MGN_crossed_square

#define SYMTAB_SIZE 95

static const char mgn_primary_symtab[SYMTAB_SIZE][3] =  {

	MGN_default,		//     00  	 --no-symbol--
	MGN_default,		//  !  01  	 Police, Sheriff
	MGN_default,		//  "  02  	 reserved  (was rain)
	MGN_aerial,		//  #  03  	 DIGI (white center)
	MGN_default,		//  $  04  	 PHONE
	MGN_aerial,		//  %  05  	 DX CLUSTER
	MGN_aerial,		//  &  06  	 HF GATEway
	MGN_airport,		//  '  07  	 Small AIRCRAFT
	MGN_aerial,		//  (  08  	 Mobile Satellite Station
	MGN_default,		//  )  09  	 Wheelchair (handicapped)
	MGN_default,		//  *  10  	 SnowMobile
	MGN_default,		//  +  11  	 Red Cross
	MGN_default,		//  ,  12  	 Boy Scouts
	MGN_house,		//  -  13  	 House QTH (VHF)
	MGN_default,		//  .  14  	 X
	MGN_default,		//  /  15  	 Red Dot
	MGN_default,		//  0  16  	 # circle (obsolete)
	MGN_default,		//  1  17  	 TBD
	MGN_default,		//  2  18  	 TBD
	MGN_default,		//  3  19  	 TBD
	MGN_default,		//  4  20  	 TBD
	MGN_default,		//  5  21  	 TBD
	MGN_default,		//  6  22  	 TBD
	MGN_default,		//  7  23  	 TBD
	MGN_default,		//  8  24  	 TBD
	MGN_default,		//  9  25  	 TBD
	MGN_default,		//  :  26  	 FIRE
	MGN_camping,		//  ;  27  	 Campground (Portable ops)
	MGN_default,		//  <  28  	 Motorcycle
	MGN_default,		//  =  29  	 RAILROAD ENGINE
	MGN_default,		//  >  30  	 CAR
	MGN_default,		//  ?  31  	 SERVER for Files
	MGN_default,		//  @  32  	 HC FUTURE predict (dot)
	MGN_first_aid,		//  A  33  	 Aid Station
	MGN_aerial,		//  B  34  	 BBS or PBBS
	MGN_boating,		//  C  35  	 Canoe
	MGN_default,		//  D  36  	 
	MGN_default,		//  E  37  	 EYEBALL (Eye catcher!)
	MGN_default,		//  F  38  	 Farm Vehicle (tractor)
	MGN_default,		//  G  39  	 Grid Square (6 digit)
	MGN_hotel,		//  H  40  	 HOTEL (blue bed symbol)
	MGN_aerial,		//  I  41  	 TcpIp on air network stn
	MGN_default,		//  J  42  	 
	MGN_default,		//  K  43  	 School
	MGN_default,		//  L  44  	 PC user
	MGN_default,		//  M  45  	 MacAPRS
	MGN_aerial,		//  N  46  	 NTS Station
	MGN_airport,		//  O  47  	 BALLOON
	MGN_default,		//  P  48  	 Police
	MGN_default,		//  Q  49  	 TBD
	MGN_RV_service,		//  R  50  	 REC. VEHICLE
	MGN_airport,		//  S  51  	 SHUTTLE
	MGN_default,		//  T  52  	 SSTV
	MGN_default,		//  U  53  	 BUS
	MGN_default,		//  V  54  	 ATV
	MGN_default,		//  W  55  	 National WX Service Site
	MGN_default,		//  X  56  	 HELO
	MGN_boating,		//  Y  57  	 YACHT (sail)
	MGN_default,		//  Z  58  	 WinAPRS
	MGN_default,		//  [  59  	 Human/Person (HT)
	MGN_default,		//  \  60  	 TRIANGLE(DF station)
	MGN_default,		//  ]  61  	 MAIL/PostOffice(was PBBS)
	MGN_airport,		//  ^  62  	 LARGE AIRCRAFT
	MGN_default,		//  _  63  	 WEATHER Station (blue)
	MGN_aerial,		//  `  64  	 Dish Antenna
	MGN_default,		//  a  65  	 AMBULANCE
	MGN_default,		//  b  66  	 BIKE
	MGN_default,		//  c  67  	 Incident Command Post
	MGN_default,		//  d  68  	 Fire dept
	MGN_zoo,		//  e  69  	 HORSE (equestrian)
	MGN_default,		//  f  70  	 FIRE TRUCK
	MGN_airport,		//  g  71  	 Glider
	MGN_default,		//  h  72  	 HOSPITAL
	MGN_default,		//  i  73  	 IOTA (islands on the air)
	MGN_default,		//  j  74  	 JEEP
	MGN_default,		//  k  75  	 TRUCK
	MGN_default,		//  l  76  	 Laptop
	MGN_aerial,		//  m  77  	 Mic-E Repeater
	MGN_default,		//  n  78  	 Node (black bulls-eye)
	MGN_default,		//  o  79  	 EOC
	MGN_zoo,		//  p  80  	 ROVER (puppy, or dog)
	MGN_default,		//  q  81  	 GRID SQ shown above 128 m
	MGN_aerial,		//  r  82  	 Repeater
	MGN_default,		//  s  83  	 SHIP (pwr boat)
	MGN_default,		//  t  84  	 TRUCK STOP
	MGN_default,		//  u  85  	 TRUCK (18 wheeler)
	MGN_default,		//  v  86  	 VAN
	MGN_default,		//  w  87  	 WATER station
	MGN_aerial,		//  x  88  	 xAPRS (Unix)
	MGN_aerial,		//  y  89  	 YAGI @ QTH
	MGN_default,		//  z  90  	 TBD
	MGN_default,		//  {  91  	 
	MGN_default,		//  |  92  	 TNC Stream Switch
	MGN_default,		//  }  93  	 
	MGN_default };		//  ~  94  	 TNC Stream Switch


static const char mgn_alternate_symtab[SYMTAB_SIZE][3] =  {

	MGN_default,		//     00  	 --no-symbol--
	MGN_default,		//  !  01  	 EMERGENCY (!)
	MGN_default,		//  "  02  	 reserved
	MGN_aerial,		//  #  03  	 OVERLAY DIGI (green star)
	MGN_ATM,		//  $  04  	 Bank or ATM  (green box)
	MGN_default,		//  %  05  	 Power Plant with overlay
	MGN_aerial,		//  &  06  	 I=Igte IGate R=RX T=1hopTX 2=2hopTX
	MGN_default,		//  '  07  	 Crash (& now Incident sites)
	MGN_default,		//  (  08  	 CLOUDY (other clouds w ovrly)
	MGN_aerial,		//  )  09  	 Firenet MEO, MODIS Earth Obs.
	MGN_default,		//  *  10  	 SNOW (& future ovrly codes)
	MGN_default,		//  +  11  	 Church
	MGN_default,		//  ,  12  	 Girl Scouts
	MGN_house,		//  -  13  	 House (H=HF) (O = Op Present)
	MGN_default,		//  .  14  	 Ambiguous (Big Question mark)
	MGN_default,		//  /  15  	 Waypoint Destination
	MGN_default,		//  0  16  	 CIRCLE (E/I/W=IRLP/Echolink/WIRES)
	MGN_default,		//  1  17  	 
	MGN_default,		//  2  18  	 
	MGN_default,		//  3  19  	
	MGN_default,		//  4  20  
	MGN_default,		//  5  21 
	MGN_default,		//  6  22
	MGN_default,		//  7  23
	MGN_aerial,		//  8  24  	 802.11 or other network node
	MGN_fuel,		//  9  25  	 Gas Station (blue pump)
	MGN_default,		//  :  26  	 Hail (& future ovrly codes)
	MGN_park,		//  ;  27  	 Park/Picnic area
	MGN_default,		//  <  28  	 ADVISORY (one WX flag)
	MGN_default,		//  =  29  	 APRStt Touchtone (DTMF users)
	MGN_default,		//  >  30  	 OVERLAID CAR
	MGN_tourist_info,	//  ?  31  	 INFO Kiosk  (Blue box with ?)
	MGN_default,		//  @  32  	 HURRICANE/Trop-Storm
	MGN_box,		//  A  33  	 overlayBOX DTMF & RFID & XO
	MGN_default,		//  B  34  	 Blwng Snow (& future codes)
	MGN_boating,		//  C  35  	 Coast Guard
	MGN_default,		//  D  36  	 Drizzle (proposed APRStt)
	MGN_default,		//  E  37  	 Smoke (& other vis codes)
	MGN_default,		//  F  38  	 Freezng rain (&future codes)
	MGN_default,		//  G  39  	 Snow Shwr (& future ovrlys)
	MGN_default,		//  H  40  	 Haze (& Overlay Hazards)
	MGN_default,		//  I  41  	 Rain Shower
	MGN_default,		//  J  42  	 Lightning (& future ovrlys)
	MGN_default,		//  K  43  	 Kenwood HT (W)
	MGN_lighthouse,		//  L  44  	 Lighthouse
	MGN_default,		//  M  45  	 MARS (A=Army,N=Navy,F=AF)
	MGN_buoy,		//  N  46  	 Navigation Buoy
	MGN_airport,		//  O  47  	 Rocket
	MGN_default,		//  P  48  	 Parking
	MGN_default,		//  Q  49  	 QUAKE
	MGN_restaurant,		//  R  50  	 Restaurant
	MGN_aerial,		//  S  51  	 Satellite/Pacsat
	MGN_default,		//  T  52  	 Thunderstorm
	MGN_default,		//  U  53  	 SUNNY
	MGN_nav_aid,		//  V  54  	 VORTAC Nav Aid
	MGN_default,		//  W  55  	 # NWS site (NWS options)
	MGN_default,		//  X  56  	 Pharmacy Rx (Apothicary)
	MGN_aerial,		//  Y  57  	 Radios and devices
	MGN_default,		//  Z  58  	 
	MGN_default,		//  [  59  	 W.Cloud (& humans w Ovrly)
	MGN_default,		//  \  60  	 New overlayable GPS symbol
	MGN_default,		//  ]  61  	 
	MGN_airport,		//  ^  62  	 # Aircraft (shows heading)
	MGN_default,		//  _  63  	 # WX site (green digi)
	MGN_default,		//  `  64  	 Rain (all types w ovrly)
	MGN_aerial,		//  a  65  	 ARRL, ARES, WinLINK
	MGN_default,		//  b  66  	 Blwng Dst/Snd (& others)
	MGN_default,		//  c  67  	 CD triangle RACES/SATERN/etc
	MGN_default,		//  d  68  	 DX spot by callsign
	MGN_default,		//  e  69  	 Sleet (& future ovrly codes)
	MGN_default,		//  f  70  	 Funnel Cloud
	MGN_default,		//  g  71  	 Gale Flags
	MGN_default,		//  h  72  	 Store. or HAMFST Hh=HAM store
	MGN_box,		//  i  73  	 BOX or points of Interest
	MGN_default,		//  j  74  	 WorkZone (Steam Shovel)
	MGN_default,		//  k  75  	 Special Vehicle SUV,ATV,4x4
	MGN_default,		//  l  76  	 Areas      (box,circles,etc)
	MGN_default,		//  m  77  	 Value Sign (3 digit display)
	MGN_default,		//  n  78  	 OVERLAY TRIANGLE
	MGN_default,		//  o  79  	 small circle
	MGN_default,		//  p  80  	 Prtly Cldy (& future ovrlys)
	MGN_default,		//  q  81  	 
	MGN_default,		//  r  82  	 Restrooms
	MGN_default,		//  s  83  	 OVERLAY SHIP/boat (top view)
	MGN_default,		//  t  84  	 Tornado
	MGN_default,		//  u  85  	 OVERLAID TRUCK
	MGN_default,		//  v  86  	 OVERLAID Van
	MGN_default,		//  w  87  	 Flooding
	MGN_wreck,		//  x  88  	 Wreck or Obstruction ->X<-
	MGN_default,		//  y  89  	 Skywarn
	MGN_default,		//  z  90  	 OVERLAID Shelter
	MGN_default,		//  {  91  	 Fog (& future ovrly codes)
	MGN_default,		//  |  92  	 TNC Stream Switch
	MGN_default,		//  }  93  	 
	MGN_default };		//  ~  94  	 TNC Stream Switch

