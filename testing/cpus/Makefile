ACMEFLAGS	= -v0
CPUS	:= $(subst .a,,$(subst test-,,$(wildcard test-*.a)))
FILES	= $(foreach N,$(CPUS),$(N).exp)

.SILENT:

%.exp: test-%.a
	echo "Testing CPU:" $(subst .exp,,$@)
	acme $(ACMEFLAGS) -o test.o $<
	cmp test.o out-$@
	$(RM) test.o
	echo "Ok."

all: $(FILES)
	echo
	echo "Testing CPUs: PASSED"
	echo
