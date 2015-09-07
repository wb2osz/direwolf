#!/usr/bin/perl

# Part of Dire Wolf APRS Telemetry Toolkit, WB2OSZ, 2015

if ($#ARGV+1 < 2 || $#ARGV+1 > 7) { 
	print STDERR "2 to 7 command line arguments must be provided.\n";
	usage(); 
}

if ($#ARGV+1 == 7) {
	if ( ! ($ARGV[6] =~ m/^[01]{8}$/)) {
		print STDERR "The sixth value must be 8 binary digits.\n";
		usage(); 
	}
}
	
print "T#" . join (',', @ARGV) . "\n";
exit 0;

sub usage () 
{
	print STDERR "\n";
	print STDERR "telem-data.pl - Format data into Telemetry Report format.\n";
	print STDERR "\n";
	print STDERR "Usage:  telem-data.pl  sequence value1 [ value2 ... ]\n";
	print STDERR "\n";
	print STDERR "A sequence number and up to 5 analog values can be specified.\n";
	print STDERR "Any sixth value must be 8 binary digits.\n";

	exit 1;
}