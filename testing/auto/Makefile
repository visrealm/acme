ACMEFLAGS	= -v0
FILES		:= $(wildcard *.a)
TESTS		= $(subst .a,.test,$(FILES))

.SILENT:

%.test: %.a
	echo "Test:" $<
	acme $(ACMEFLAGS) $<
	echo "Ok."

all: $(TESTS)
	echo
	echo "Self-tests: PASSED"
	echo
