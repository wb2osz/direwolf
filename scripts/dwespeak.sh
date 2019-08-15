#!/bin/bash
chan=$1
msg=$2
sleep 1
espeak -v en-sc "$msg"
