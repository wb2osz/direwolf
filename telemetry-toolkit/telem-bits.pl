#!/usr/bin/perl

# Part of Dire Wolf APRS Telemetry Toolkit, WB2OSZ, 2015

if ($#ARGV+1 < 2 || $#ARGV+1 > 3) { 
	print STDERR "A callsign, bit sense string, and optional project title must be provided.\n";
	usage(); 
}

# Separate out call and pad to 9 characters.
$call = shift @ARGV;
$call = substr($call . "         ", 0, 9);

if ( ! ($ARGV[0] =~ m/^[01]{8}$/)) {
	print STDERR "The bit-sense value must be 8 binary digits.\n";
	usage();
}
	
print ":$call:BITS." . join (',', @ARGV) . "\n";
exit 0;

sub usage () 
{
	print STDERR "\n";
	print STDERR "telem-bits.pl - Generate BITS message with bit polarity and optional project title.\n";
	print STDERR "\n";
	print STDERR "Usage:  telem-bits.pl  call bit-sense [ project-title ]\n";
	print STDERR "\n";
	print STDERR "Bit-sense is string of 8 binary digits.\n";
	print STDERR "If project title contains any spaces, enclose it in quotes.\n";

	exit 1;
}