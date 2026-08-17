#ACMEFLAGS	= -v0

.PHONY: dialects outformats

.SILENT:

all: dialects outformats
	echo
	echo "Testing cliargs: PASSED"
	echo

dialects: dialect085.a dialect086.a dialect09406.a dialect09408.a dialect09412.a dialect097.a
	acme --strict --dialect 0.85 dialect085.a
	acme --strict --dialect 0.86 dialect086.a
	acme --strict --dialect 0.94.6 dialect09406.a
	acme --strict --dialect 0.94.8 dialect09408.a
	acme --strict --dialect 0.94.12 dialect09412.a
	acme --strict --dialect 0.97 dialect097.a
	$(RM) test.o

outformats: outformats.a outformats.sh outformat-*.exp
	echo "Testing output formats:"
	bash outformats.sh
