#!/bin/bash
for CPU in 6502 65c02 r65c02 w65c02 nmos6502 c64dtv2 65ce02 4502 m65 65816 ; do
	acme -v0 test-"$CPU".a || exit
	cmp out-"$CPU".o expected-"$CPU".o || exit
done
echo
echo "All CPU tests passed successfully."
echo
