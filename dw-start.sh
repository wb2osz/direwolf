#!/bin/bash

#
# Run this from crontab periodically to start up
# Dire Wolf automatically.
#
# I prefer this method instead of putting something
# in ~/.config/autostart.  That would start an application
# only when the desktop first starts up.
#
# This method will restart the application if it
# crashes or stops for any other reason.
#
# This script has some specifics the Raspberry Pi.
# Some adjustments might be needed for other Linux variations.
#

#
# For normal operation as TNC, digipeater, IGate, etc.
# Print audio statistics each 100 seconds for troubleshooting.
#

DWCMD="direwolf -a 100"

#
# Set the logfile location
#

LOGFILE=/tmp/dw-start.log

#
# When running from cron, we have a very minimal environment
# including PATH=/usr/bin:/bin.
#

export PATH=/usr/local/bin:$PATH

#
# If we are going to use screen, we put our screen binary in
# the USESCREEN variable, otherwise, set it to 0

USESCREEN=/usr/bin/screen


#
# Nothing to do if it is already running.
#

a=`pgrep direwolf`
if [ "$a" != "" ] 
then
  #date >> /tmp/dw-start.log
  #echo "Already running." >> $LOGFILE
  exit
fi

# First wait a little while in case we just rebooted
# and the desktop hasn't started up yet.
#

sleep 30

#
# If we are going the SCREEN route, then we need to 
# see if we have a session open and if not, open it.
#
if [ -x $USESCREEN ]
then

  # If there is no screen running, then we need one to attach to
  #
  if screen -list | awk '{print $1}' | grep -q "direwolf$"; then
    echo "screen direwolf already exists" >> $LOGFILE
  else
    echo "creating direwolf screen session" >> $LOGFILE
    screen -d -m -S direwolf
  fi
  sleep 1

  screen -S direwolf -X screen -t Direwolf $DWCMD 
  exit 0

fi


#
# In my case, the Raspberry Pi is not connected to a monitor.
# I access it remotely using VNC as described here:
# http://learn.adafruit.com/adafruit-raspberry-pi-lesson-7-remote-control-with-vnc
#
# If VNC server is running, use its display number.
# Otherwise default to :0.
#

date >> $LOGFILE

export DISPLAY=":0"

v=`ps -ef | grep Xtightvnc | grep -v grep`
if [ "$v" != "" ]
then
  d=`echo "$v" | sed 's/.*tightvnc *\(:[0-9]\).*/\1/'`
  export DISPLAY="$d"
fi

echo "DISPLAY=$DISPLAY" >> $LOGFILE

echo "Start up application." >> $LOGFILE


# Alternative for running with SDR receiver.
# Piping one application into another makes it a little more complicated.
# We need to use bash for the | to be recognized. 

#DWCMD="bash -c 'rtl_fm -f 144.39M - | direwolf -c sdr.conf -r 24000 -D 1 -'"

# 
# Adjust for your particular situation:  gnome-terminal, xterm, etc.
#


if [ -x /usr/bin/lxterminal ]
then
  /usr/bin/lxterminal -t "Dire Wolf" -e "$DWCMD" &
elif [ -x /usr/bin/xterm ] 
then
  /usr/bin/xterm -bg white -fg black -e "$DWCMD" &
elif [ -x /usr/bin/x-terminal-emulator ]
then
  /usr/bin/x-terminal-emulator -e "$DWCMD" &
else
  echo "Did not find an X terminal emulator."
fi

echo "-----------------------" >> $LOGFILE

