#!/usr/bin/env bash

# Why not simply "#!/bin/bash" ?

# For OpenBSD, the bash location is /usr/local/bin/bash.
# By using env here, bash is found based on the user's $PATH.
# I hope this does not break some other operating system.


# Run this from crontab periodically to start up
# Dire Wolf automatically.

# See User Guide for more discussion.
# For release 1.4 it is section 5.7 "Automatic Start Up After Reboot"
# but it could change in the future as more information is added.


# Versioning (this file, not direwolf version)
#-----------
# v1.4 - OK1BIL - added support for multiple instances, tweaked screen execution
# v1.3 - KI6ZHD - added variable support for direwolf binary location
# v1.2 - KI6ZHD - support different versions of VNC
# v1.1 - KI6ZHD - expanded version to support running on text-only displays with 
#        auto support; log placement change
# v1.0 - WB2OSZ - original version for Xwindow displays only


#-------------------------------------
# Configuration 
#-------------------------------------

#How are you running Direwolf : within a GUI (Xwindows / VNC) or CLI mode
#
#  AUTO mode is design to try starting direwolf with GUI support and then
#    if no GUI environment is available, it reverts to CLI support with screen
#
#  GUI mode is suited for users with the machine running LXDE/Gnome/KDE or VNC
#    which auto-logs on (sitting at a login prompt won't work)
#
#  CLI mode is suited for say a Raspberry Pi running the Jessie LITE version
#      where it will run from the CLI w/o requiring Xwindows - uses screen

RUNMODE="AUTO"

# Location of the direwolf binary.  Depends on $PATH as shown.
# change this if you want to use some other specific location.
# e.g.  DIREWOLF="/usr/local/bin/direwolf"

DIREWOLF="direwolf"

# In case direwolf is run in CLI, it is running in a screen session in the background.
# Each screen session has a name. If you want to run multiple instances of direwolf
# in parallel (i.e. two SDRs over STDIN), you need to specify unique names for the sessions.
# Note: screen is a linux tool to run user-interactive software in background.


INSTANCE="direwolf"


# Parameters for the direwolf binary can be set here. This replaces the DWCMD variable.
# Uncomment onlye on of the variables.

# 1. For normal operation as TNC, digipeater, IGate, etc.
#    Print audio statistics each 100 seconds for troubleshooting.
#    Change this command to however you wish to start Direwolf

DWPARAMS="-a 100"

# 2. FX.25 Forward Error Correction (FEC) will allow your signal to
#    go farther under poor radio conditions.  Add "-X 1" to the command line.

#DWPARAMS="-a 100 -X 1"

# 3. Alternative for running with SDR receiver. In case of using this, please pay attention
#    to variable DWSTDIN below.

#DWPARAMS="-c /etc/direwolf/ok1abc-sdr.conf -t 0 -r 24000 -D 1 -"

# A command to be fed into the STDIN of the direwolf binary. Main use for this is to configure rtl-sdr input.
# Leave commented if using soundcard inputs. If using rtl-sdr, uncomment folllowing lines and set parameters as needed.

#QRG="144.8M"
#GAIN="43"
#PPM="1"
#DWSTDIN="rtl_fm -f $QRG -g $GAIN -p $PPM -" 


# Where will logs go - needs to be writable by non-root users
LOGFILE="/var/tmp/dw-start.log"

# Startup delay in seconds - how long should the script wait before executing (default 30)
STARTUP_DELAY=30

#-------------------------------------
# Internal use functions
#-------------------------------------

# This function is to be recursively called from outside of this script. Its purpose 
# is to avoid passing commands to screen or bash as strings, which is tricky.

function RUN_CLI () {

   PARAMS=$1
   STDIN=$2

   if [ -z "$2" ]; then
      $DIREWOLF $PARAMS
   else
      $STDIN | $DIREWOLF $PARAMS
   fi
   exit 0

}

#-------------------------------------
# Main functions of the script
#-------------------------------------

#Status variables
SUCCESS=0

function CLI {
   SCREEN=`which screen`
   if [ $? -ne 0 ]; then
      echo -e "Error: screen is not installed but is required for CLI mode.  Aborting"
      exit 1
   fi

   echo "Direwolf in CLI mode start up"
   echo "Direwolf in CLI mode start up" >> $LOGFILE

   # Screen commands
   #  -d m :: starts the command in detached mode
   #  -S   :: name the session
   # 
   # Screen is instructed to run this script again with a parameter (to execute direwolf with
   # necessary parameters). This way commands do not need to be passed as string. Additionally,
   # this allows for some pre-flight checks withing the screen session, should they be needed.
   $SCREEN -d -m -S $INSTANCE $0 -runcli >> $LOGFILE
   SUCCESS=1

   $SCREEN -list $INSTANCE
   $SCREEN -list $INSTANCE >> $LOGFILE

   echo "-----------------------"
   echo "-----------------------" >> $LOGFILE
}

function GUI {
   # In this case
   # In my case, the Raspberry Pi is not connected to a monitor.
   # I access it remotely using VNC as described here:
   # http://learn.adafruit.com/adafruit-raspberry-pi-lesson-7-remote-control-with-vnc
   #
   # If VNC server is running, use its display number.
   # Otherwise default to :0 (the Xwindows on the HDMI display)
   #
   export DISPLAY=":0"

   #Reviewing for RealVNC sessions (stock in Raspbian Pixel)
   if [ -n "`ps -ef | grep vncserver-x11-serviced | grep -v grep`" ]; then
      sleep 0.1
      echo -e "\nRealVNC found - defaults to connecting to the :0 root window"
     elif [ -n "`ps -ef | grep Xtightvnc | grep -v grep`" ]; then
      #Reviewing for TightVNC sessions
      echo -e "\nTightVNC found - defaults to connecting to the :1 root window"
      v=`ps -ef | grep Xtightvnc | grep -v grep`
      d=`echo "$v" | sed 's/.*tightvnc *\(:[0-9]\).*/\1/'`
      export DISPLAY="$d"
   fi

   echo "Direwolf in GUI mode start up"
   echo "Direwolf in GUI mode start up" >> $LOGFILE
   echo "DISPLAY=$DISPLAY" 
   echo "DISPLAY=$DISPLAY" >> $LOGFILE

   # 
   # Auto adjust the startup for your particular environment:  gnome-terminal, xterm, etc.
   #

   if [ -x /usr/bin/lxterminal ]; then
      /usr/bin/lxterminal -t "Dire Wolf" -e "$DWCMD" &
      SUCCESS=1
     elif [ -x /usr/bin/xterm ]; then
      /usr/bin/xterm -bg white -fg black -e "$DWCMD" &
      SUCCESS=1
     elif [ -x /usr/bin/x-terminal-emulator ]; then
      /usr/bin/x-terminal-emulator -e "$DWCMD" &
      SUCCESS=1
     else
      echo "Did not find an X terminal emulator.  Reverting to CLI mode"
      SUCCESS=0
   fi
   echo "-----------------------"
   echo "-----------------------" >> $LOGFILE
}

# -----------------------------------------------------------
# Main Script start
# -----------------------------------------------------------

# When running from cron, we have a very minimal environment
# including PATH=/usr/bin:/bin.
#
export PATH=/usr/local/bin:$PATH

#Log the start of the script run and re-run
date >> $LOGFILE

# First wait a little while in case we just rebooted
# and the desktop hasn't started up yet.
#
sleep $STARTUP_DELAY


#
# Nothing to do if Direwolf is already running.
#

a=`ps ax | grep $INSTANCE | grep -vi -e bash -e screen -e grep | awk '{print $1}'`
if [ -n "$a" ] 
then
  #date >> /tmp/dw-start.log
  #echo "Direwolf already running." >> $LOGFILE
  exit
fi

# Check for parameter to recursively run this script and execute direwolf in cli
if [ $# -eq 1 ] && [ $1 == "-runcli" ]; then
  RUN_CLI "$DWPARAMS" "$DWSTDIN"
  exit 0
fi

# Main execution of the script

if [ $RUNMODE == "AUTO" ];then 
   GUI
   if [ $SUCCESS -eq 0 ]; then
      CLI
   fi
  elif [ $RUNMODE == "GUI" ];then
   GUI
  elif [ $RUNMODE == "CLI" ];then
   CLI
  else
   echo -e "ERROR: illegal run mode given.  Giving up"
   exit 1
fi

