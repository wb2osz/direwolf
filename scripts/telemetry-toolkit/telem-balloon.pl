#!/usr/bin/perl

# Part of Dire Wolf APRS Telemetry Toolkit, WB2OSZ, 2015

# In a real situation this would obtain data from sensors.
# For demonstration purposes we use historical data supplied on the command line.

if ($#ARGV+1 != 5) { 
	print STDERR "5 command line arguments must be provided.\n";
	usage(); 
}

($seq,$vbat,$vsolar,$temp,$sat) = @ARGV;

# Scale to integer in range of 0 to 8280.
# This must be the inverse of the mapping in the EQNS message.

$vbat = int(($vbat * 1000) + 0.5);
$vsolar = int(($vsolar * 1000) + 0.5);
$temp = int((($temp + 273.2) * 10) + 0.5);

exit system("telem-data91.pl $seq $vbat $vsolar $temp $sat");


sub usage () 
{
	print STDERR "\n";
	print STDERR "balloon.pl - Format data into Compressed telemetry format.\n";
	print STDERR "\n";
	print STDERR "In a real situation this would obtain data from sensors.\n";
	print STDERR "For demonstration purposes we use historical data supplied on the command line.\n";
	print STDERR "\n";
	print STDERR "Usage:  balloon.pl  seq vbat vsolar temp sat\n";
	print STDERR "\n";
	print STDERR "Where,\n";
	print STDERR "    seq     is a sequence number.\n";
	print STDERR "    vbat    is battery voltage.\n";
	print STDERR "    vsolar  is solar cell voltage.\n";
	print STDERR "    temp    is temperature, degrees C.\n";
	print STDERR "    sat     is number of GPS satellites visible.\n";

	exit 1;
}