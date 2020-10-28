echo off 

set chan=%1
set msg=%2

sleep 1

"C:\Program Files (x86)\eSpeak\command_line\espeak.exe" -v en-sc %msg%