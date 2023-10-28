
/*
 * grm_sym.h
 *
 * Symbol codes for use in $PGRMWPL sentence. 
 *
 * Copied from 
 *	Garmin Device Interface Specification
 *	May 19, 2006
 *	Drawing Number: 001-00063-00 Rev. C
 */


typedef unsigned short symbol_type_t;

enum symbol_type_e
{
/*---------------------------------------------------------------
Marine symbols
---------------------------------------------------------------*/
sym_anchor = 0, /* white anchor symbol */
sym_bell = 1, /* white bell symbol */
sym_diamond_grn = 2, /* green diamond symbol */
sym_diamond_red = 3, /* red diamond symbol */
sym_dive1 = 4, /* diver down flag 1 */
sym_dive2 = 5, /* diver down flag 2 */
sym_dollar = 6, /* white dollar symbol */
sym_fish = 7, /* white fish symbol */
sym_fuel = 8, /* white fuel symbol */
sym_horn = 9, /* white horn symbol */
sym_house = 10, /* white house symbol */
sym_knife = 11, /* white knife & fork symbol */
sym_light = 12, /* white light symbol */
sym_mug = 13, /* white mug symbol */
sym_skull = 14, /* white skull and crossbones symbol*/
sym_square_grn = 15, /* green square symbol */
sym_square_red = 16, /* red square symbol */
sym_wbuoy = 17, /* white buoy waypoint symbol */
sym_wpt_dot = 18, /* waypoint dot */
sym_wreck = 19, /* white wreck symbol */
sym_null = 20, /* null symbol (transparent) */
sym_mob = 21, /* man overboard symbol */
sym_buoy_ambr = 22, /* amber map buoy symbol */
sym_buoy_blck = 23, /* black map buoy symbol */
sym_buoy_blue = 24, /* blue map buoy symbol */
sym_buoy_grn = 25, /* green map buoy symbol */
sym_buoy_grn_red = 26, /* green/red map buoy symbol */
sym_buoy_grn_wht = 27, /* green/white map buoy symbol */
sym_buoy_orng = 28, /* orange map buoy symbol */
sym_buoy_red = 29, /* red map buoy symbol */
sym_buoy_red_grn = 30, /* red/green map buoy symbol */
sym_buoy_red_wht = 31, /* red/white map buoy symbol */
sym_buoy_violet = 32, /* violet map buoy symbol */
sym_buoy_wht = 33, /* white map buoy symbol */
sym_buoy_wht_grn = 34, /* white/green map buoy symbol */
sym_buoy_wht_red = 35, /* white/red map buoy symbol */
sym_dot = 36, /* white dot symbol */
sym_rbcn = 37, /* radio beacon symbol */
sym_boat_ramp = 150, /* boat ramp symbol */
sym_camp = 151, /* campground symbol */
sym_restrooms = 152, /* restrooms symbol */
sym_showers = 153, /* shower symbol */
sym_drinking_wtr = 154, /* drinking water symbol */
sym_phone = 155, /* telephone symbol */
sym_1st_aid = 156, /* first aid symbol */
sym_info = 157, /* information symbol */
sym_parking = 158, /* parking symbol */
sym_park = 159, /* park symbol */
sym_picnic = 160, /* picnic symbol */
sym_scenic = 161, /* scenic area symbol */
sym_skiing = 162, /* skiing symbol */
sym_swimming = 163, /* swimming symbol */
sym_dam = 164, /* dam symbol */
sym_controlled = 165, /* controlled area symbol */
sym_danger = 166, /* danger symbol */
sym_restricted = 167, /* restricted area symbol */
sym_null_2 = 168, /* null symbol */
sym_ball = 169, /* ball symbol */
sym_car = 170, /* car symbol */
sym_deer = 171, /* deer symbol */
sym_shpng_cart = 172, /* shopping cart symbol */
sym_lodging = 173, /* lodging symbol */
sym_mine = 174, /* mine symbol */
sym_trail_head = 175, /* trail head symbol */
sym_truck_stop = 176, /* truck stop symbol */
sym_user_exit = 177, /* user exit symbol */
sym_flag = 178, /* flag symbol */
sym_circle_x = 179, /* circle with x in the center */
sym_open_24hr = 180, /* open 24 hours symbol */
sym_fhs_facility = 181, /* U Fishing Hot Spots(tm) Facility */
sym_bot_cond = 182, /* Bottom Conditions */
sym_tide_pred_stn = 183, /* Tide/Current Prediction Station */
sym_anchor_prohib = 184, /* U anchor prohibited symbol */
sym_beacon = 185, /* U beacon symbol */
sym_coast_guard = 186, /* U coast guard symbol */
sym_reef = 187, /* U reef symbol */
sym_weedbed = 188, /* U weedbed symbol */
sym_dropoff = 189, /* U dropoff symbol */
sym_dock = 190, /* U dock symbol */
sym_marina = 191, /* U marina symbol */
sym_bait_tackle = 192, /* U bait and tackle symbol */
sym_stump = 193, /* U stump symbol */
/*---------------------------------------------------------------
User customizable symbols
The values from sym_begin_custom to sym_end_custom inclusive are
reserved for the identification of user customizable symbols.
---------------------------------------------------------------*/
sym_begin_custom = 7680, /* first user customizable symbol */
sym_end_custom = 8191, /* last user customizable symbol */
/*---------------------------------------------------------------
Land symbols
---------------------------------------------------------------*/
sym_is_hwy = 8192, /* interstate hwy symbol */
sym_us_hwy = 8193, /* us hwy symbol */
sym_st_hwy = 8194, /* state hwy symbol */
sym_mi_mrkr = 8195, /* mile marker symbol */
sym_trcbck = 8196, /* TracBack (feet) symbol */
sym_golf = 8197, /* golf symbol */
sym_sml_cty = 8198, /* small city symbol */
sym_med_cty = 8199, /* medium city symbol */
sym_lrg_cty = 8200, /* large city symbol */
sym_freeway = 8201, /* intl freeway hwy symbol */
sym_ntl_hwy = 8202, /* intl national hwy symbol */
sym_cap_cty = 8203, /* capitol city symbol (star) */
sym_amuse_pk = 8204, /* amusement park symbol */
sym_bowling = 8205, /* bowling symbol */
sym_car_rental = 8206, /* car rental symbol */
sym_car_repair = 8207, /* car repair symbol */
sym_fastfood = 8208, /* fast food symbol */
sym_fitness = 8209, /* fitness symbol */
sym_movie = 8210, /* movie symbol */
sym_museum = 8211, /* museum symbol */
sym_pharmacy = 8212, /* pharmacy symbol */
sym_pizza = 8213, /* pizza symbol */
sym_post_ofc = 8214, /* post office symbol */
sym_rv_park = 8215, /* RV park symbol */
sym_school = 8216, /* school symbol */
sym_stadium = 8217, /* stadium symbol */
sym_store = 8218, /* dept. store symbol */
sym_zoo = 8219, /* zoo symbol */
sym_gas_plus = 8220, /* convenience store symbol */
sym_faces = 8221, /* live theater symbol */
sym_ramp_int = 8222, /* ramp intersection symbol */
sym_st_int = 8223, /* street intersection symbol */
sym_weigh_sttn = 8226, /* inspection/weigh station symbol */
sym_toll_booth = 8227, /* toll booth symbol */
sym_elev_pt = 8228, /* elevation point symbol */
sym_ex_no_srvc = 8229, /* exit without services symbol */
sym_geo_place_mm = 8230, /* Geographic place name, man-made */
sym_geo_place_wtr = 8231, /* Geographic place name, water */
sym_geo_place_lnd = 8232, /* Geographic place name, land */
sym_bridge = 8233, /* bridge symbol */
sym_building = 8234, /* building symbol */
sym_cemetery = 8235, /* cemetery symbol */
sym_church = 8236, /* church symbol */
sym_civil = 8237, /* civil location symbol */
sym_crossing = 8238, /* crossing symbol */
sym_hist_town = 8239, /* historical town symbol */
sym_levee = 8240, /* levee symbol */
sym_military = 8241, /* military location symbol */
sym_oil_field = 8242, /* oil field symbol */
sym_tunnel = 8243, /* tunnel symbol */
sym_beach = 8244, /* beach symbol */
sym_forest = 8245, /* forest symbol */
sym_summit = 8246, /* summit symbol */
sym_lrg_ramp_int = 8247, /* large ramp intersection symbol */
sym_lrg_ex_no_srvc = 8248, /* large exit without services smbl */
sym_badge = 8249, /* police/official badge symbol */
sym_cards = 8250, /* gambling/casino symbol */
sym_snowski = 8251, /* snow skiing symbol */
sym_iceskate = 8252, /* ice skating symbol */
sym_wrecker = 8253, /* tow truck (wrecker) symbol */
sym_border = 8254, /* border crossing (port of entry) */
sym_geocache = 8255, /* geocache location */
sym_geocache_fnd = 8256, /* found geocache */
sym_cntct_smiley = 8257, /* Rino contact symbol, "smiley" */
sym_cntct_ball_cap = 8258, /* Rino contact symbol, "ball cap" */
sym_cntct_big_ears = 8259, /* Rino contact symbol, "big ear" */
sym_cntct_spike = 8260, /* Rino contact symbol, "spike" */
sym_cntct_goatee = 8261, /* Rino contact symbol, "goatee" */
sym_cntct_afro = 8262, /* Rino contact symbol, "afro" */
sym_cntct_dreads = 8263, /* Rino contact symbol, "dreads" */
sym_cntct_female1 = 8264, /* Rino contact symbol, "female 1" */
sym_cntct_female2 = 8265, /* Rino contact symbol, "female 2" */
sym_cntct_female3 = 8266, /* Rino contact symbol, "female 3" */
sym_cntct_ranger = 8267, /* Rino contact symbol, "ranger" */
sym_cntct_kung_fu = 8268, /* Rino contact symbol, "kung fu" */
sym_cntct_sumo = 8269, /* Rino contact symbol, "sumo" */
sym_cntct_pirate = 8270, /* Rino contact symbol, "pirate" */
sym_cntct_biker = 8271, /* Rino contact symbol, "biker" */
sym_cntct_alien = 8272, /* Rino contact symbol, "alien" */
sym_cntct_bug = 8273, /* Rino contact symbol, "bug" */
sym_cntct_cat = 8274, /* Rino contact symbol, "cat" */
sym_cntct_dog = 8275, /* Rino contact symbol, "dog" */
sym_cntct_pig = 8276, /* Rino contact symbol, "pig" */
sym_hydrant = 8282, /* water hydrant symbol */
sym_flag_blue = 8284, /* blue flag symbol */
sym_flag_green = 8285, /* green flag symbol */
sym_flag_red = 8286, /* red flag symbol */
sym_pin_blue = 8287, /* blue pin symbol */
sym_pin_green = 8288, /* green pin symbol */
sym_pin_red = 8289, /* red pin symbol */
sym_block_blue = 8290, /* blue block symbol */
sym_block_green = 8291, /* green block symbol */
sym_block_red = 8292, /* red block symbol */
sym_bike_trail = 8293, /* bike trail symbol */
sym_circle_red = 8294, /* red circle symbol */
sym_circle_green = 8295, /* green circle symbol */
sym_circle_blue = 8296, /* blue circle symbol */
sym_diamond_blue = 8299, /* blue diamond symbol */
sym_oval_red = 8300, /* red oval symbol */
sym_oval_green = 8301, /* green oval symbol */
sym_oval_blue = 8302, /* blue oval symbol */
sym_rect_red = 8303, /* red rectangle symbol */
sym_rect_green = 8304, /* green rectangle symbol */
sym_rect_blue = 8305, /* blue rectangle symbol */
sym_square_blue = 8308, /* blue square symbol */
sym_letter_a_red = 8309, /* red letter 'A' symbol */
sym_letter_b_red = 8310, /* red letter 'B' symbol */
sym_letter_c_red = 8311, /* red letter 'C' symbol */
sym_letter_d_red = 8312, /* red letter 'D' symbol */
sym_letter_a_green = 8313, /* green letter 'A' symbol */
sym_letter_c_green = 8314, /* green letter 'C' symbol */
sym_letter_b_green = 8315, /* green letter 'B' symbol */
sym_letter_d_green = 8316, /* green letter 'D' symbol */
sym_letter_a_blue = 8317, /* blue letter 'A' symbol */
sym_letter_b_blue = 8318, /* blue letter 'B' symbol */
sym_letter_c_blue = 8319, /* blue letter 'C' symbol */
sym_letter_d_blue = 8320, /* blue letter 'D' symbol */
sym_number_0_red = 8321, /* red number '0' symbol */
sym_number_1_red = 8322, /* red number '1' symbol */
sym_number_2_red = 8323, /* red number '2' symbol */
sym_number_3_red = 8324, /* red number '3' symbol */
sym_number_4_red = 8325, /* red number '4' symbol */
sym_number_5_red = 8326, /* red number '5' symbol */
sym_number_6_red = 8327, /* red number '6' symbol */
sym_number_7_red = 8328, /* red number '7' symbol */
sym_number_8_red = 8329, /* red number '8' symbol */
sym_number_9_red = 8330, /* red number '9' symbol */
sym_number_0_green = 8331, /* green number '0' symbol */
sym_number_1_green = 8332, /* green number '1' symbol */
sym_number_2_green = 8333, /* green number '2' symbol */
sym_number_3_green = 8334, /* green number '3' symbol */
sym_number_4_green = 8335, /* green number '4' symbol */
sym_number_5_green = 8336, /* green number '5' symbol */
sym_number_6_green = 8337, /* green number '6' symbol */
sym_number_7_green = 8338, /* green number '7' symbol */
sym_number_8_green = 8339, /* green number '8' symbol */
sym_number_9_green = 8340, /* green number '9' symbol */
sym_number_0_blue = 8341, /* blue number '0' symbol */
sym_number_1_blue = 8342, /* blue number '1' symbol */
sym_number_2_blue = 8343, /* blue number '2' symbol */
sym_number_3_blue = 8344, /* blue number '3' symbol */
sym_number_4_blue = 8345, /* blue number '4' symbol */
sym_number_5_blue = 8346, /* blue number '5' symbol */
sym_number_6_blue = 8347, /* blue number '6' symbol */
sym_number_7_blue = 8348, /* blue number '7' symbol */
sym_number_8_blue = 8349, /* blue number '8' symbol */
sym_number_9_blue = 8350, /* blue number '9' symbol */
sym_triangle_blue = 8351, /* blue triangle symbol */
sym_triangle_green = 8352, /* green triangle symbol */
sym_triangle_red = 8353, /* red triangle symbol */
sym_food_asian = 8359, /* asian food symbol */
sym_food_deli = 8360, /* deli symbol */
sym_food_italian = 8361, /* italian food symbol */
sym_food_seafood = 8362, /* seafood symbol */
sym_food_steak = 8363, /* steak symbol */
/*---------------------------------------------------------------
Aviation symbols
---------------------------------------------------------------*/
sym_airport = 16384, /* airport symbol */
sym_int = 16385, /* intersection symbol */
sym_ndb = 16386, /* non-directional beacon symbol */
sym_vor = 16387, /* VHF omni-range symbol */
sym_heliport = 16388, /* heliport symbol */
sym_private = 16389, /* private field symbol */
sym_soft_fld = 16390, /* soft field symbol */
sym_tall_tower = 16391, /* tall tower symbol */
sym_short_tower = 16392, /* short tower symbol */
sym_glider = 16393, /* glider symbol */
sym_ultralight = 16394, /* ultralight symbol */
sym_parachute = 16395, /* parachute symbol */
sym_vortac = 16396, /* VOR/TACAN symbol */
sym_vordme = 16397, /* VOR-DME symbol */
sym_faf = 16398, /* first approach fix */
sym_lom = 16399, /* localizer outer marker */
sym_map = 16400, /* missed approach point */
sym_tacan = 16401, /* TACAN symbol */
sym_seaplane = 16402, /* Seaplane Base */
};



/*
 * Mapping from APRS symbols to Garmin.
 */

// TODO:  NEEDS MORE WORK!!!


#define SYMTAB_SIZE 95

#define sym_default sym_diamond_grn


static const symbol_type_t grm_primary_symtab[SYMTAB_SIZE] =  {

	sym_default,		//     00  	 --no-symbol--
	sym_cntct_ranger,	//  !  01  	 Police, Sheriff
	sym_default,		//  "  02  	 reserved  (was rain)
	sym_rbcn,		//  #  03  	 DIGI (white center)
	sym_phone,		//  $  04  	 PHONE
	sym_rbcn,		//  %  05  	 DX CLUSTER
	sym_rbcn,		//  &  06  	 HF GATEway
	sym_glider,		//  '  07  	 Small AIRCRAFT
	sym_rbcn,		//  (  08  	 Mobile Satellite Station
	sym_default,		//  )  09  	 Wheelchair (handicapped)
	sym_car,		//  *  10  	 SnowMobile
	sym_1st_aid,		//  +  11  	 Red Cross
	sym_cntct_ball_cap,	//  ,  12  	 Boy Scouts
	sym_house,		//  -  13  	 House QTH (VHF)
	sym_default,		//  .  14  	 X
	sym_default,		//  /  15  	 Red Dot
	sym_default,		//  0  16  	 # circle (obsolete)
	sym_default,		//  1  17  	 TBD
	sym_default,		//  2  18  	 TBD
	sym_default,		//  3  19  	 TBD
	sym_default,		//  4  20  	 TBD
	sym_default,		//  5  21  	 TBD
	sym_default,		//  6  22  	 TBD
	sym_default,		//  7  23  	 TBD
	sym_default,		//  8  24  	 TBD
	sym_default,		//  9  25  	 TBD
	sym_default,		//  :  26  	 FIRE
	sym_camp,		//  ;  27  	 Campground (Portable ops)
	sym_cntct_biker,	//  <  28  	 Motorcycle
	sym_default,		//  =  29  	 RAILROAD ENGINE
	sym_car,		//  >  30  	 CAR
	sym_default,		//  ?  31  	 SERVER for Files
	sym_default,		//  @  32  	 HC FUTURE predict (dot)
	sym_1st_aid,		//  A  33  	 Aid Station
	sym_rbcn,		//  B  34  	 BBS or PBBS
	sym_boat_ramp,		//  C  35  	 Canoe
	sym_default,		//  D  36  	 
	sym_default,		//  E  37  	 EYEBALL (Eye catcher!)
	sym_default,		//  F  38  	 Farm Vehicle (tractor)
	sym_default,		//  G  39  	 Grid Square (6 digit)
	sym_lodging,		//  H  40  	 HOTEL (blue bed symbol)
	sym_rbcn,		//  I  41  	 TcpIp on air network stn
	sym_default,		//  J  42  	 
	sym_school,		//  K  43  	 School
	sym_default,		//  L  44  	 PC user
	sym_default,		//  M  45  	 MacAPRS
	sym_default,		//  N  46  	 NTS Station
	sym_parachute,		//  O  47  	 BALLOON
	sym_cntct_ranger,	//  P  48  	 Police
	sym_default,		//  Q  49  	 TBD
	sym_rv_park,		//  R  50  	 REC. VEHICLE
	sym_glider,		//  S  51  	 SHUTTLE
	sym_default,		//  T  52  	 SSTV
	sym_car,		//  U  53  	 BUS
	sym_cntct_biker,	//  V  54  	 ATV
	sym_default,		//  W  55  	 National WX Service Site
	sym_default,		//  X  56  	 HELO
	sym_default,		//  Y  57  	 YACHT (sail)
	sym_default,		//  Z  58  	 WinAPRS
	sym_cntct_smiley,	//  [  59  	 Human/Person (HT)
	sym_triangle_green,	//  \  60  	 TRIANGLE(DF station)
	sym_default,		//  ]  61  	 MAIL/PostOffice(was PBBS)
	sym_glider,		//  ^  62  	 LARGE AIRCRAFT
	sym_default,		//  _  63  	 WEATHER Station (blue)
	sym_rbcn,		//  `  64  	 Dish Antenna
	sym_1st_aid,		//  a  65  	 AMBULANCE
	sym_cntct_biker,	//  b  66  	 BIKE
	sym_default,		//  c  67  	 Incident Command Post
	sym_hydrant,		//  d  68  	 Fire dept
	sym_deer,		//  e  69  	 HORSE (equestrian)
	sym_hydrant,		//  f  70  	 FIRE TRUCK
	sym_glider,		//  g  71  	 Glider
	sym_1st_aid,		//  h  72  	 HOSPITAL
	sym_default,		//  i  73  	 IOTA (islands on the air)
	sym_car,		//  j  74  	 JEEP
	sym_car,		//  k  75  	 TRUCK
	sym_default,		//  l  76  	 Laptop
	sym_rbcn,		//  m  77  	 Mic-E Repeater
	sym_default,		//  n  78  	 Node (black bulls-eye)
	sym_default,		//  o  79  	 EOC
	sym_cntct_dog,		//  p  80  	 ROVER (puppy, or dog)
	sym_default,		//  q  81  	 GRID SQ shown above 128 m
	sym_rbcn,		//  r  82  	 Repeater
	sym_default,		//  s  83  	 SHIP (pwr boat)
	sym_truck_stop,		//  t  84  	 TRUCK STOP
	sym_truck_stop,		//  u  85  	 TRUCK (18 wheeler)
	sym_car,		//  v  86  	 VAN
	sym_drinking_wtr,	//  w  87  	 WATER station
	sym_default,		//  x  88  	 xAPRS (Unix)
	sym_tall_tower,		//  y  89  	 YAGI @ QTH
	sym_default,		//  z  90  	 TBD
	sym_default,		//  {  91  	 
	sym_default,		//  |  92  	 TNC Stream Switch
	sym_default,		//  }  93  	 
	sym_default };		//  ~  94  	 TNC Stream Switch

static const symbol_type_t grm_alternate_symtab[SYMTAB_SIZE] =  {

	sym_default,		//     00  	 --no-symbol--
	sym_default,		//  !  01  	 EMERGENCY (!)
	sym_default,		//  "  02  	 reserved
	sym_default,		//  #  03  	 OVERLAY DIGI (green star)
	sym_default,		//  $  04  	 Bank or ATM  (green box)
	sym_default,		//  %  05  	 Power Plant with overlay
	sym_rbcn,		//  &  06  	 I=Igte IGate R=RX T=1hopTX 2=2hopTX
	sym_default,		//  '  07  	 Crash (& now Incident sites)
	sym_default,		//  (  08  	 CLOUDY (other clouds w ovrly)
	sym_hydrant,		//  )  09  	 Firenet MEO, MODIS Earth Obs.
	sym_default,		//  *  10  	 SNOW (& future ovrly codes)
	sym_default,		//  +  11  	 Church
	sym_cntct_female1,	//  ,  12  	 Girl Scouts
	sym_house,		//  -  13  	 House (H=HF) (O = Op Present)
	sym_default,		//  .  14  	 Ambiguous (Big Question mark)
	sym_default,		//  /  15  	 Waypoint Destination
	sym_default,		//  0  16  	 CIRCLE (E/I/W=IRLP/Echolink/WIRES)
	sym_default,		//  1  17  	 
	sym_default,		//  2  18  	 
	sym_default,		//  3  19  	
	sym_default,		//  4  20  
	sym_default,		//  5  21 
	sym_default,		//  6  22
	sym_default,		//  7  23
	sym_default,		//  8  24  	 802.11 or other network node
	sym_default,		//  9  25  	 Gas Station (blue pump)
	sym_default,		//  :  26  	 Hail (& future ovrly codes)
	sym_park,		//  ;  27  	 Park/Picnic area
	sym_default,		//  <  28  	 ADVISORY (one WX flag)
	sym_rbcn,		//  =  29  	 APRStt Touchtone (DTMF users)
	sym_car,		//  >  30  	 OVERLAID CAR
	sym_default,		//  ?  31  	 INFO Kiosk  (Blue box with ?)
	sym_default,		//  @  32  	 HURRICANE/Trop-Storm
	sym_default,		//  A  33  	 overlayBOX DTMF & RFID & XO
	sym_default,		//  B  34  	 Blwng Snow (& future codes)
	sym_coast_guard,	//  C  35  	 Coast Guard
	sym_default,		//  D  36  	 Drizzle (proposed APRStt)
	sym_default,		//  E  37  	 Smoke (& other vis codes)
	sym_default,		//  F  38  	 Freezng rain (&future codes)
	sym_default,		//  G  39  	 Snow Shwr (& future ovrlys)
	sym_default,		//  H  40  	 Haze (& Overlay Hazards)
	sym_default,		//  I  41  	 Rain Shower
	sym_default,		//  J  42  	 Lightning (& future ovrlys)
	sym_rbcn,		//  K  43  	 Kenwood HT (W)
	sym_light,		//  L  44  	 Lighthouse
	sym_default,		//  M  45  	 MARS (A=Army,N=Navy,F=AF)
	sym_default,		//  N  46  	 Navigation Buoy
	sym_default,		//  O  47  	 Rocket
	sym_default,		//  P  48  	 Parking
	sym_default,		//  Q  49  	 QUAKE
	sym_default,		//  R  50  	 Restaurant
	sym_rbcn,		//  S  51  	 Satellite/Pacsat
	sym_default,		//  T  52  	 Thunderstorm
	sym_default,		//  U  53  	 SUNNY
	sym_default,		//  V  54  	 VORTAC Nav Aid
	sym_default,		//  W  55  	 # NWS site (NWS options)
	sym_pharmacy,		//  X  56  	 Pharmacy Rx (Apothicary)
	sym_rbcn,		//  Y  57  	 Radios and devices
	sym_default,		//  Z  58  	 
	sym_default,		//  [  59  	 W.Cloud (& humans w Ovrly)
	sym_default,		//  \  60  	 New overlayable GPS symbol
	sym_default,		//  ]  61  	 
	sym_glider,		//  ^  62  	 # Aircraft (shows heading)
	sym_default,		//  _  63  	 # WX site (green digi)
	sym_default,		//  `  64  	 Rain (all types w ovrly)
	sym_default,		//  a  65  	 ARRL, ARES, WinLINK
	sym_default,		//  b  66  	 Blwng Dst/Snd (& others)
	sym_default,		//  c  67  	 CD triangle RACES/SATERN/etc
	sym_default,		//  d  68  	 DX spot by callsign
	sym_default,		//  e  69  	 Sleet (& future ovrly codes)
	sym_default,		//  f  70  	 Funnel Cloud
	sym_default,		//  g  71  	 Gale Flags
	sym_default,		//  h  72  	 Store. or HAMFST Hh=HAM store
	sym_default,		//  i  73  	 BOX or points of Interest
	sym_default,		//  j  74  	 WorkZone (Steam Shovel)
	sym_car,		//  k  75  	 Special Vehicle SUV,ATV,4x4
	sym_default,		//  l  76  	 Areas      (box,circles,etc)
	sym_default,		//  m  77  	 Value Sign (3 digit display)
	sym_default,		//  n  78  	 OVERLAY TRIANGLE
	sym_default,		//  o  79  	 small circle
	sym_default,		//  p  80  	 Prtly Cldy (& future ovrlys)
	sym_default,		//  q  81  	 
	sym_restrooms,		//  r  82  	 Restrooms
	sym_default,		//  s  83  	 OVERLAY SHIP/boat (top view)
	sym_default,		//  t  84  	 Tornado
	sym_car,		//  u  85  	 OVERLAID TRUCK
	sym_car,		//  v  86  	 OVERLAID Van
	sym_default,		//  w  87  	 Flooding
	sym_wreck,		//  x  88  	 Wreck or Obstruction ->X<-
	sym_default,		//  y  89  	 Skywarn
	sym_default,		//  z  90  	 OVERLAID Shelter
	sym_default,		//  {  91  	 Fog (& future ovrly codes)
	sym_default,		//  |  92  	 TNC Stream Switch
	sym_default,		//  }  93  	 
	sym_default };		//  ~  94  	 TNC Stream Switch

