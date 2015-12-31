#!/bin/sh
# Generate sequence number as described here:
# https://github.com/wb2osz/direwolf/issues/9
#
SEQ=`cat /tmp/seq 2>/dev/null`
SEQ=$(expr \( $SEQ + 1 \) % 1000)
echo $SEQ | tee /tmp/seq