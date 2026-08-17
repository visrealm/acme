#!/bin/bash

check() {
	#echo "$@"
	FILE="$1"
	shift
	acme "$@" outformats.a || exit 1
	cmp test.o "$FILE" || exit 1
	rm test.o
}

# if neither -o nor -f are given, use format from "!to", which defaults to cbm:
check outformat-cbm.exp   -DFORMAT=0
check outformat-plain.exp -DFORMAT=1
check outformat-cbm.exp   -DFORMAT=2
check outformat-apple.exp -DFORMAT=3

# if -o or -f are given, format from "!to" should be ignored:
for f in 0 1 2 3 ; do
	check outformat-plain.exp -DFORMAT=$f -f plain
	check outformat-cbm.exp   -DFORMAT=$f -f cbm
	check outformat-apple.exp -DFORMAT=$f -f apple
	check outformat-plain.exp -DFORMAT=$f -o test.o	# defaults to plain
	check outformat-plain.exp -DFORMAT=$f -o test.o -f plain
	check outformat-cbm.exp   -DFORMAT=$f -o test.o -f cbm
	check outformat-apple.exp -DFORMAT=$f -o test.o -f apple
done
