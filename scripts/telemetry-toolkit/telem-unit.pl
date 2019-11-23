#!/usr/bin/perl

# Part of Dire Wolf APRS Telemetry Toolkit, WB2OSZ, 2015

if ($#ARGV+1 < 2 || $#ARGV+1 > 14) { 
	print STDERR "A callsign and 1 to 13 units/labels must be provided.\n";
	usage(); 
}

# Separate out call and pad to 9 characters.
$call = shift @ARGV;
$call = substr($call . "         ", 0, 9);
	
print ":$call:UNIT." . join (',', @ARGV) . "\n";
exit 0;

sub usage () 
{
	print STDERR "\n";
	print STDERR "telem-unit.pl - Generate UNIT message with channel units/labels.\n";
	print STDERR "\n";
	print STDERR "Usage:  telem-unit.pl  call unit1 ... unit5 label1 .,, label8\n";
	print STDERR "\n";
	print STDERR "Specify a callsign and up to 13 names for the units/labels.\n";

	exit 1;
}