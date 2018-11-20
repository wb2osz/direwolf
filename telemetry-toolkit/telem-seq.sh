#!/bin/sh
# Generate sequence number as described here:
# https://github.com/wb2osz/direwolf/issues/9
#
if [ ! -f /tmp/seq ]; then
echo 0 > /tmp/seq
fi

SEQ=`cat /tmp/seq 2>/dev/null`
SEQ=$(expr \( $SEQ + 1 \) % 1000)
echo $SEQ | tee /tmp/seq
