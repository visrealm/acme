ACMEFLAGS	= -v0 --color
FILES		:= $(wildcard *.a)
FAILTESTS	= $(subst .a,.fail,$(FILES))
SUCCTESTS	= $(subst .a,.succ,$(FILES))

.SILENT:

%.fail: %.a
	echo "Testing warning with --strict:" $<
	! acme $(ACMEFLAGS) --strict $<
	echo "Ok."

%.succ: %.a
	echo "Testing warning:" $<
	acme $(ACMEFLAGS) $<
	echo "Ok."

all: $(FAILTESTS) $(SUCCTESTS)
	echo
	echo "Testing warnings: PASSED"
	echo
