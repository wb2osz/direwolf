#!/usr/bin/env bash

# Why not simply "#!/bin/bash" ?

# For OpenBSD, the bash location is /usr/local/bin/bash.
# By using env here, bash is found based on the user's $PATH.
# I hope this does not break some other operating system.


# Run this from crontab periodically to start up
# Dire Wolf automatically.

# See User Guide for more discussion.
# For release 1.6 it is section 5.7 "Automatic Start Up After Reboot"
# but it could change in the future as more information is added.


# Versioning (this file, not direwolf version)
#-----------
# v1.6.1 - KI6ZHD - Improved support for cron start, new checks startup method, 
#                   added STDOUT filtering to avoid chatty cron emails or logs
# v1.6   - KI6ZHD - Update to auto-detect binary paths and updated GUI start
# v1.3   - KI6ZHD - added variable support for direwolf binary location
# v1.2   - KI6ZHD - support different versions of VNC
# v1.1   - KI6ZHD - expanded version to support running on text-only displays with 
#                   auto support; log placement change
# v1.0   - WB2OSZ - original version for Xwindow displays only


#-------------------------------------
# User settable variables
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

RUNMODE=AUTO


# Location of the direwolf binary.  Depends on $PATH as shown and how the program
# was installed.  Change this if you want to use some other specific location.
# e.g.  DIREWOLF="/usr/bin/direwolf"

DIREWOLF="/usr/local/bin/direwolf"


#Direwolf start up command :: two examples where example one is enabled
#
# 1. For normal operation as TNC, digipeater, IGate, etc.
#    Print audio statistics each 100 seconds for troubleshooting.
#    Change this command to however you wish to start Direwolf
#
# 2. Alternative for running with SDR receiver.
#    Piping one application into another makes it a little more complicated.
#    We need to use bash for the | to be recognized.
#
#    DWCMD="bash -c 'rtl_fm -f 144.39M - | direwolf -c sdr.conf -r 24000 -D 1 -'"
#
# Config:
# This command assumes the direwolf.conf file is in the CURRENT directory that
# the user is within per the "pwd" command.  If not, enhance this command to 
# use the "-c" option such as "-c /home/pi/direwolf.conf" syntax
#
# Options:
#   Any other direwolf options such as turning on/off color, etc should go in here

DWCMD="$DIREWOLF -a 100 -t 0"


#Where will logs go - needs to be writable by non-root users

LOGFILE=/var/tmp/dw-start.log

#Script STDOUT and logging output control
#  Set this varaible to 0 if you won't want to see all STDOUT
#  logging once you get everything working properly

VERBOSE=1



#-------------------------------------
# Main functions of the script
#-------------------------------------

#Status variables
SUCCESS=0

function CHKERR {
   if [ $? -ne 0 ]; then
      echo -e "Last command failed"
      exit 1
   fi
}

function CLI {
   #Auto-determine if screen is installed
   SCREEN=`which screen`
   if [ $? -ne 0 ]; then
      echo -e "Error: screen is not installed but is required for CLI mode.  Aborting"
      exit 1
   fi

   if [ $VERBOSE -eq 1 ]; then
      echo "Direwolf in CLI mode start up.  All log output recorded to $LOGFILE"
      echo "Direwolf in CLI mode start up.  All log output recorded to $LOGFILE" >> $LOGFILE
   fi

   # Screen commands
   #  -d m :: starts the command in detached mode
   #  -S   :: name the session
   # --
   #  Options:
   #     1. Remove the "-d m" parameters if you don't want screen to automatically detach  
   #        and instead say in the foreground
   #     2. Remove the " >> $LOGFILE" if you rather keep all the Direwolf running output 
   #        in the screen session vs going to the log file
   #
   $SCREEN -d -m -S direwolf bash -c "echo 'All Direwolf output going to $LOGFILE.  Enter control-c to terminate Direwolf'; $DWCMD >> $LOGFILE"
   CHKERR
   SUCCESS=1
   if [ $VERBOSE -eq 1 ]; then
      echo " "
      echo " " >> $LOGFILE
   fi

   $SCREEN -list direwolf
   CHKERR
   $SCREEN -list direwolf >> $LOGFILE
   if [ $VERBOSE -eq 1 ]; then
      echo -e "\nYou can re-attach to the Direwolf screen with the following command run via user: $USER"
      echo -e "     screen -dr direwolf"
      echo -e "\nYou can re-attach to the Direwolf screen with the following command run via user: $USER" >> $LOGFILE
      echo -e "     screen -dr direwolf" >> $LOGFILE
      echo -e "\n-----------------------"
      echo -e "\n-----------------------" >> $LOGFILE
  fi
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
   if [ ! $DISPLAY ]; then
      if [ $VERBOSE -eq 1 ]; then
         echo "No initial X-windows DISPLAY variable set"
         #export DISPLAY=":0"
      fi
   fi

   #Reviewing for RealVNC sessions (stock in Raspbian Pixel)
   if [ -n "`ps -ef | grep vncserver-x11-serviced | grep -v grep`" ]; then
      sleep 0.1
      if [ $VERBOSE -eq 1 ]; then
         echo -e "\nRealVNC found - defaults to connecting to the :0 root window"
      fi
     elif [ -n "`ps -ef | grep Xtightvnc | grep -v grep`" ]; then
      #Reviewing for TightVNC sessions
      if [ $VERBOSE -eq 1 ]; then
         echo -e "\nTightVNC found - defaults to connecting to the :1 root window"
      fi
      v=`ps -ef | grep Xtightvnc | grep -v grep`
      d=`echo "$v" | sed 's/.*tightvnc *\(:[0-9]\).*/\1/'`
      export DISPLAY="$d"
   fi

   if [ $VERBOSE -eq 1 ]; then
      echo -e "Direwolf in GUI mode start up.  All log output recorded to $LOGFILE"
      echo -e "Direwolf in GUI mode start up.  All log output recorded to $LOGFILE" >> $LOGFILE
      echo -e "Xwindows display to be used for all Direwolf output: $DISPLAY\n" 
      echo -e "Xwindows display to be used for all Direwolf output: $DISPLAY\n" >> $LOGFILE
   fi

   if [ "$DISPLAY" != "" ]; then
      # 
      # Auto adjust the startup for your particular environment:  gnome-terminal, xterm, etc.
      #

      if [ $(which lxterminal) ]; then
         #echo "DEBUG: lxterminal stanza"
         $(which lxterminal) -l -t "Dire Wolf" -e "$DWCMD" &
         CHKERR
         SUCCESS=1
        elif [ $(which xterm) ]; then
         #echo "DEBUG: xterm stanza"
         $(which xterm) -bg white -fg black -e "$DWCMD" &
         CHKERR
         SUCCESS=1
        elif [ $(which x-terminal-emulator) ]; then
         #echo "DEBUG: x-xterm-emulator stanza"
         $(which x-terminal-emulator) -e "$DWCMD" &
         CHKERR
         SUCCESS=1
        else
         if [ $VERBOSE -eq 1 ]; then
            echo "Did not find a vaild X terminal program.  Reverting to CLI mode"
            echo "Did not find a vaild X terminal program.  Reverting to CLI mode" >> $LOGFILE
	 fi
         SUCCESS=0
      fi
     else
      if [ $VERBOSE -eq 1 ]; then
         echo -e "\nXwindows DISPLAY variable unable to be set.  Reverting to CLI mode"
         echo -e "\nXwindows DISPLAY variable unable to be set.  Reverting to CLI mode" >> $LOGFILE
      fi
   fi
}

# -----------------------------------------------------------
# Main Script start
# -----------------------------------------------------------

# When running from cron, we have a very minimal environment
# including PATH=/usr/bin:/bin.
#
export PATH=/usr/local/bin:$PATH

if [ ! -x $DIREWOLF ]; then
   echo -e "\nError: Direwolf program not found per the DIREWOLF variable in script.  Aborting.\n"
   echo -e "\nError: Direwolf program not found per the DIREWOLF variable in script.  Aborting.\n" >> $LOGFILE
   exit 1
fi
if [ ! -f direwolf.conf ]; then
   echo -e "\nError: direwolf.conf config file not found in `pwd`.  Aborting.\n"
   echo -e "\nError: direwolf.conf config file not found in `pwd`.  Aborting.\n" >> $LOGFILE
   exit 1
fi

# First wait a little while in case we just rebooted
# and the desktop hasn't started up yet.
#
if [ $VERBOSE -eq 1 ]; then
   echo -e "\ndw-start.sh"
   echo -e "\ndw-start.sh" >> $LOGFILE
   echo -e "-----------"
   echo -e "-----------" >> $LOGFILE
   #Log the start of the script run and re-run
   date 
   date >> $LOGFILE
   echo -e "Running in verbose mode.  Change the VERBOSE variable in the dw-script.sh to not see this and other text output"
   echo -e "Running in verbose mode.  Change the VERBOSE variable in the dw-script.sh to not see this and other text output" >> $LOGFILE
   echo -e "Sleeping for 30 seconds to let any boot/reboot delays conclude"
   echo -e "Sleeping for 30 seconds to let any boot/reboot delays conclude" >> $LOGFILE
fi
sleep 30


#
# Nothing to do if Direwolf is already running.
#

a=`ps ax | grep direwolf | grep -vi -e bash -e screen -e grep | awk '{print $1}'`
if [ -n "$a" ]; then
  if [ $VERBOSE -eq 1 ]; then
     # Don't send this to STDOUT if running from cron or you'll geta cron email saying that
     # direwolf is already running every minute!  Ok to send to the log file if you turn on
     # VERBOSE
     #date >> /tmp/dw-start.log
     echo "Direwolf already running.  Not starting a new instance." >> $LOGFILE
  fi
     exit
fi

# Main execution of the script

if [ $RUNMODE == "AUTO" ];then 
   GUI
   if [ $SUCCESS -eq 0 ]; then
      #if [ $VERBOSE -eq 1 ]; then
      #   echo "GUI mode startup failed.  Reverting to CLI mode"
      #   echo "GUI mode startup failed.  Reverting to CLI mode" >> $LOGFILE
      #fi
      CLI
   fi
  elif [ $RUNMODE == "GUI" ];then
   GUI
  elif [ $RUNMODE == "CLI" ];then
   CLI
  else
   echo -e "ERROR: illegal dw-start run mode configured.  Giving up"
   exit 1
fi

