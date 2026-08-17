ACMEFLAGS	= -v0
FILES		:= $(wildcard *.a)
TESTS		= $(subst .a,.test,$(FILES))

.SILENT:

%.test: %.a
	echo "Testing bugfix:" $<
	acme $(ACMEFLAGS) $<
	echo "Ok."

all: $(TESTS)
	echo
	echo "Testing bugfixes: PASSED"
	echo
