# AXUDP output for dxlAPRS

Dire Wolf can send every received frame as an AXUDP v2 datagram, the format
used by the [dxlAPRS](https://github.com/oe5hpm/dxlAPRS) tools.  This allows
Dire Wolf to replace `afskmodem` in a dxlAPRS chain (for example
`sdrtst` -> modem -> `udpbox` / `udpgate4`) without a KISS bridge, and keeps
the per packet signal report that `udpgate4` shows in its heard list.

## Configuration

    AXUDP <channel> <host>:<port>

Example for two receive channels fed from `sdrtst` on stdin:

    ADEVICE null null
    ACHANNELS 2
    CHANNEL 0
    MODEM 1200
    CHANNEL 1
    MODEM 1200
    AGWPORT 0
    KISSPORT 0
    AXUDP 0 127.0.0.1:9201
    AXUDP 1 127.0.0.1:9202

    sdrtst ... -r 16000 -s aprspipe ...
    direwolf -c direwolf.conf -n 2 -r 16000 -b 16 -t 0 - < aprspipe

This is the same as `afskmodem ... -M 0 -c 0 -L 127.0.0.1:9201:0 -M 1 -c 1 -L 127.0.0.1:9202:0`.
Only receiving is supported; frames are not accepted from the UDP side.

## Datagram

    0x01 '0' "V<level> Q<quality> " 0x00 <AX.25 frame without FCS> <CRC low> <CRC high>

The CRC is the same one dxlAPRS uses for AXUDP and covers all preceding bytes.

* `V` - receive audio level in dB, converted to the `afskmodem` scale
  (20*log10 of the peak-to-peak sample value, minus 96.5).
* `Q` - decoder margin in percent.  This is **not** the bit jitter quality
  reported by `afskmodem`.  It is the share of the parallel demodulators and
  slicers that decoded the frame; frames that needed bit fixing count less.
  Compare it only between Dire Wolf instances.
