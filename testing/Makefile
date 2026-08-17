
.SILENT:

.PHONY: auto bugfixes cliargs cpus errors warnings

all: auto bugfixes cliargs cpus errors warnings

auto:
	echo "Performing self-tests..."
	echo
	make -C auto

bugfixes:
	echo "Testing bugfixes..."
	echo
	make -C bugfixes

cliargs:
	echo "Testing cliargs..."
	echo
	make -C cliargs

cpus:
	echo "Testing CPUs..."
	echo
	make -C cpus

errors:
	echo "Testing errors..."
	echo
	make -C errors

warnings:
	echo "Testing warnings..."
	echo
	make -C warnings
