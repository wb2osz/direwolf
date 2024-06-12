#!/bin/bash

# Simplified, CLI-only direwolf start-up script

# Run this from crontab periodically to check whether direwolf is running
# and force it to start up automatically.

# In order to execute this script periodically using crontab, do the following:
#	1.- Configure the script to your liking and save changes.
#	2.- Type "crontab -e" without quotes in the terminal
#	  2a.- If this is your first time editing crontab, select your preferred text editor. My favorite is nano.
#	3.- Add the line below to the crontab file. This will force the script to run every minute.
#
#		* * * * * /absolute/path/to/this/file/dw-start-cli.sh >/dev/null 2>&1
#
#	4.- Save and close the crontab file. 
#	5.- Optional: Use top to check whether direwolf was started. I highly recommend
#		testing this script by manually executing it before adding it to the crontab.
#		Execute the script manually by typing: ./dw-start-cli.sh

# Versioning (this file, not direwolf version)
#-----------
# v1.4 - KE5WSG - Removed support for the GUI and vastly simplified the script.
#					Improved error checking and logging. 
# v1.3 - KI6ZHD - added variable support for direwolf binary location
# v1.2 - KI6ZHD - support different versions of VNC
# v1.1 - KI6ZHD - expanded version to support running on text-only displays with 
#        auto support; log placement change
# v1.0 - WB2OSZ - original version for Xwindow displays only

# Enable (true) / disable (false) logging everything to the log file.
# Setting this to 'false' is recommended to keep from unnecessarily filling the log file.
DEBUG=false

#Where will be stored. The directory must be writable by non-root users.
LOGFILE=/var/tmp/dw-start.log

# Location of the direwolf binary.  Depends on $PATH as shown.
# Change this if you want to use some other specific location.
# e.g.  DWPATH="/usr/local/bin/direwolf"

DWPATH="/home/pi/direwolf"

# Location of the direwolf configuration file (direwolf.conf).
# Change this if you want to customize or protect where your configuration
# file is stored.
# e.g. DWCONFIG="/home/pi/direwolf"

DWCONFIG="/home/pi/direwolf"

# Name of the direwolf configuration file.

DWCONFIGFILE="direwolf.conf"

# Direwolf start up command. Examples for both a simple and SDR config are provided.
#
# 1. For normal operation as TNC, digipeater, IGate, etc.
#    Print audio statistics each 100 seconds for troubleshooting.
#    Change this command to however you wish to start Direwolf.
#	 Be sure to use variables as necessary when building your command.

DWCMD="$DWPATH/direwolf -a 100 -c $DWCONFIG/$DWCONFIGFILE"

#---------------------------------------------------------------
#
# 2. Alternative for running with SDR receiver.
#    Piping one application into another makes it a little more complicated.
#    We need to use bash for the | to be recognized.

#DWCMD="bash -c 'rtl_fm -f 144.39M - | $DWPATH/direwolf -c $DWCONFIG/sdr.conf -r 24000 -D 1 -'"

# When running from cron, we have a very minimal environment
# including PATH=/usr/bin:/bin.
export PATH=/usr/local/bin:$PATH

# Error checking before attempting to start direwolf

# Check to see whether the direwolf directory exists
if ! [ -d "$DWPATH" ]; then
	echo -e "ERROR: The direwolf path (DWPATH) is invalid!  Aborting."
	echo "-----------------------" >> $LOGFILE
	date >> $LOGFILE
	echo "ERROR: The direwolf path (DWPATH) is invalid!  Aborting." >> $LOGFILE
	exit 1
fi

# Check to see whether the direwolf application exists
if ! [ -f "$DWPATH/direwolf" ]; then
	echo -e "ERROR: The direwolf application (DWPATH/direwolf) cannot be found!  Aborting."
	echo "-----------------------" >> $LOGFILE
	date >> $LOGFILE
	echo "ERROR: The direwolf application (DWPATH/direwolf) cannot be found!  Aborting." >> $LOGFILE
	exit 1
fi

# Check to see whether the direwolf configuration file exists
if ! [ -f "$DWCONFIG/$DWCONFIGFILE" ]; then
	echo -e "ERROR: The direwolf configuration file specified cannot be found!  Aborting."
	echo "-----------------------" >> $LOGFILE
	date >> $LOGFILE
	echo "ERROR: The direwolf configuration file specified cannot be found!  Aborting." >> $LOGFILE
	exit 1
fi

# Check to see whether screen is installed
if ! type "screen" > /dev/null; then
	echo -e "ERROR: screen is not installed. Please install using 'sudo apt-get install screen'  Aborting."
	echo "-----------------------" >> $LOGFILE
	date >> $LOGFILE
	echo "ERROR: screen is not installed." >> $LOGFILE
	exit 1
fi

# Check to see if there's already a screen session named "direwolf"
if screen -list | grep -q "direwolf"; then
	echo "A screen session named 'direwolf' is already running.  Direwolf is likely already running.  Exiting."
	if $DEBUG; then
		echo "-----------------------" >> $LOGFILE
		date >> $LOGFILE
		echo "A screen session named 'direwolf' was found. Exiting." >> $LOGFILE
	fi
	exit
fi

# It looks like none of the errors forced the application to exit. Let's try running direwolf!

# Wait a little while in case we just rebooted and you're using an old RPi.
# Feel free to adjust this delay as needed.
sleep 1

# Print status messages
echo "Direwolf starting up..."
echo "-----------------------" >> $LOGFILE
date >> $LOGFILE
echo "Direwolf starting up..." >> $LOGFILE

# Start direwolf in a screen session named 'direwolf'
screen -S direwolf -d -m $DWCMD

# Print direwolf screen information to the screen/log
screen -list direwolf
screen -list direwolf >> $LOGFILE

echo "-----------------------"
echo "-----------------------" >> $LOGFILE

