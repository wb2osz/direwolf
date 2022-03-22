DATA_DIR=../build
cut -c22-41,54-56,59-61,64-66,69-71,74-76,79-81,84-86,89-90 $DATA_DIR/all.out >$DATA_DIR/all.cut
cut -d' ' -f5- $DATA_DIR/all.cut > $DATA_DIR/all.pkts
./pkttest < $DATA_DIR/all.pkts > $DATA_DIR/all.good
