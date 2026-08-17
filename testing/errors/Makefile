ACMEFLAGS	= -v0 --color
FILES	:= $(wildcard *.a)
TESTS	= $(subst .a,.test,$(FILES))

.SILENT:

%.test: %.a
	echo "Testing error generation:" $<
	! acme $(ACMEFLAGS) $<
	echo "Ok."

all: $(TESTS)
	echo
	echo "Testing errors: PASSED"
	echo
