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
# First wait a little while in case we just rebooted
# and the desktop hasn't started up yet.
#

sleep 30

#
# Nothing to do if it is already running.
#

a=`ps -ef | grep direwolf | grep -v grep`
if [ "$a" != "" ] 
then
  #date >> /tmp/dw-start.log
  #echo "Already running." >> /tmp/dw-start.log
  exit
fi

#
# In my case, the Raspberry Pi is not connected to a monitor.
# I access it remotely using VNC as described here:
# http://learn.adafruit.com/adafruit-raspberry-pi-lesson-7-remote-control-with-vnc
#
# If VNC server is running, use its display number.
# Otherwise default to :0.
#

date >> /tmp/dw-start.log

export DISPLAY=":0"

v=`ps -ef | grep Xtightvnc | grep -v grep`
if [ "$v" != "" ]
then
  d=`echo "$v" | sed 's/.*tightvnc *\(:[0-9]\).*/\1/'`
  export DISPLAY="$d"
fi

echo "DISPLAY=$DISPLAY" >> /tmp/dw-start.log

echo "Start up application." >> /tmp/dw-start.log

# 
# Adjust for your particular situation:  gnome-terminal, xterm, etc.
#

if [ -x /usr/bin/lxterminal ]
then
  /usr/bin/lxterminal -t "Dire Wolf" -e "/usr/local/bin/direwolf" &
elif [ -x /usr/bin/xterm ] 
then
  /usr/bin/xterm -bg white -fg black -e /usr/local/bin/direwolf &
elif [ -x /usr/bin/x-terminal-emulator ]
then
  /usr/bin/x-terminal-emulator -e  /usr/local/bin/direwolf &
else
  echo "Did not find an X terminal emulator."
fi

echo "-----------------------" >> /tmp/dw-start.log

