#!/usr/bin/perl

# Part of Dire Wolf APRS Telemetry Toolkit, WB2OSZ, 2015

if ($#ARGV+1 < 2 || $#ARGV+1 > 14) { 
	print STDERR "A callsign and 1 to 13 channel names must be provided.\n";
	usage(); 
}

# Separate out call and pad to 9 characters.
$call = shift @ARGV;
$call = substr($call . "         ", 0, 9);
	
print ":$call:PARM." . join (',', @ARGV) . "\n";
exit 0;

sub usage () 
{
	print STDERR "\n";
	print STDERR "telem-parm.pl - Generate PARM message with channel names.\n";
	print STDERR "\n";
	print STDERR "Usage:  telem-parm.pl  call aname1 ... aname5 dname1 .,, dname8\n";
	print STDERR "\n";
	print STDERR "Specify a callsign and up to 13 names for the analog & digital channels.\n";

	exit 1;
}