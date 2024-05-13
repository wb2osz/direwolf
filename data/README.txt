
tocalls.yaml contains the encoding for the device/system/software
identifier which created the packet.
Knowing what generated the packet is very useful for troubleshooting.
TNCs, digipeaters, and IGates must not change this. 

For MIC-E format, well... it's complicated.
See  Understanding-APRS-Packets.pdf.   Too long to repeat here.

For all other packet types, the AX.25 destination, or "tocall" field
contains a code for what generated the packet.
This is of the form AP????.    For example, APDW18 for direwolf 1.8.

The database of identifiers is currently maintained by Hessu, OH7LZB.

You can update your local copy by running:

wget https://raw.githubusercontent.com/aprsorg/aprs-deviceid/main/tocalls.yaml
