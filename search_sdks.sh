#!/bin/bash
#
# This file is part of Dire Wolf, an amateur radio packet TNC.
#
# Bash script to search for SDKs on various MacOSX versions.
#
# Copyright (C) 2015 Robert Stiles
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

FILENAME="./use_this_sdk"
selected_sdk=""
valid_flag=0
system_sdk=""

if [ -f $FILENAME ]; then
    selected_sdk=`cat $FILENAME`
    if [ -d $selected_sdk ]; then
        valid_flag=1
    fi
fi

if [ $valid_flag -eq "0" ]; then
    echo " " >&2
    echo " " >&2
    echo "Searching for SDKs.... (Wait for results)" >&2
    echo " " >&2
    echo "Enter the number and press Enter/Return Key" >&2
    echo " " >&2
    echo " " >&2

    prompt="Select SDK to use:"

    loc1=( $(find /Applications/XCode.app -type d -name "MacOSX10.*.sdk") )
    loc2=( $(find /Developer/SDKs -maxdepth 1 -type d -name "MacOSX10.*.sdk") )

    options=("${loc1[@]}" "${loc2[@]}")

    if [ "${#options[@]}" -lt "2" ]; then
        echo "$options"
    fi

    PS3="$prompt "
    select opt in "${options[@]}" "Do not use any SDK" ; do
        if (( REPLY == 1 + ${#options[@]} )) ; then
            echo " "
            break
        elif (( REPLY > 0 && REPLY <= ${#options[@]} )) ; then
            selected_sdk="$opt"
            break
        fi
    done

    if [ ! -z "$selected_sdk" ]; then
        echo "$selected_sdk" > $FILENAME
    else
        echo " " > $FILENAME
    fi
fi

if [ ! -z "$selected_sdk" ]; then
    temp_str="$selected_sdk"
    min_str=""
    flag=true

    # Search for the last MacOSX in the string.
    while [ "${#temp_str}" -gt 4 ]; do
        temp_str="${temp_str#*MacOSX}"
        temp_str="${temp_str%%.sdk}"
        min_str="$temp_str"
        temp_str="${temp_str:1}"
    done

    # Remove the "u" if 10.4u Universal SDK is used.
    min_str="${min_str%%u}"

    system_sdk="-isystem ${selected_sdk} -mmacosx-version-min=${min_str}"
else
    system_sdk=" "
fi

echo " " >&2
echo "*******************************************************************" >&2

if [ -z "${system_sdk}" ]; then
    echo "SDK Selected: None" >&2
else
    echo "SDK Selected: ${system_sdk}" >&2
fi

echo "To change SDK version execute 'make clean' followed by 'make'." >&2
echo "*******************************************************************" >&2
echo " " >&2

echo ${system_sdk}


