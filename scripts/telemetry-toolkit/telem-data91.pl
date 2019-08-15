#!/usr/bin/perl

# Part of Dire Wolf APRS Telemetry Toolkit, WB2OSZ, 2015

# For explanation of encoding see:
# http://he.fi/doc/aprs-base91-comment-telemetry.txt


if ($#ARGV+1 < 2 || $#ARGV+1 > 7) { 
	print STDERR "2 to 7 command line arguments must be provided.\n";
	usage(); 
}


if ($#ARGV+1 == 7) {
	if ( ! ($ARGV[6] =~ m/^[01]{8}$/)) {
		print STDERR "The sixth value must be 8 binary digits.\n";
		usage(); 
	}
	# Convert binary digits to value.
	$ARGV[6] = oct("0b" . reverse($ARGV[6]));
}

$result = "|";

for ($n = 0 ; $n <= $#ARGV; $n++) {
	#print $n . " = " . $ARGV[$n] . "\n";
	$v = $ARGV[$n];
	if ($v != int($v) || $v < 0 || $v > 8280) {
		print STDERR "$v is not an integer in range of 0 to 8280.\n";
		usage(); 
	}

	$result .= base91($v);
}

$result .= "|";
print "$result\n";
exit 0;


sub base91 ()
{
	my $x = @_[0];

	my $d1 = int ($x / 91);
	my $d2 = $x % 91;

	return chr($d1+33) . chr($d2+33);
}


sub usage () 
{
	print STDERR "\n";
	print STDERR "telem-data91.pl - Format data into compressed base 91 telemetry.\n";
	print STDERR "\n";
	print STDERR "Usage:  telem-data91.pl  sequence value1 [ value2 ... ]\n";
	print STDERR "\n";
	print STDERR "A sequence number and up to 5 analog values can be specified.\n";
	print STDERR "Any sixth value must be 8 binary digits.\n";
	print STDERR "Values must be integers in range of 0 to 8280.\n";

	exit 1;
}