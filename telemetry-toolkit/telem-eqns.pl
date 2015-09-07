#!/usr/bin/perl

# Part of Dire Wolf APRS Telemetry Toolkit, WB2OSZ, 2015

if ($#ARGV+1 != 4 && $#ARGV+1 != 7 && $#ARGV+1 != 10 && $#ARGV+1 != 13 && $#ARGV+1 != 16) { 
	print STDERR "A callsign and 1 to 5 sets of 3 coefficients must be provided.\n";
	usage(); 
}

# Separate out call and pad to 9 characters.
$call = shift @ARGV;
$call = substr($call . "         ", 0, 9);
	
print ":$call:EQNS." . join (',', @ARGV) . "\n";
exit 0;

sub usage () 
{
	print STDERR "\n";
	print STDERR "telem-eqns.pl - Generate EQNS message with scaling coefficients\n";
	print STDERR "\n";
	print STDERR "Usage:  telem-eqns.pl  call a1 b1 c1 [ a2 b2 c2 ... ]\n";
	print STDERR "\n";
	print STDERR "Specify a callsign and 1 to 5 sets of 3 coefficients.\n";
	print STDERR "See APRS protocol reference for their meaning.\n";

	exit 1;
}