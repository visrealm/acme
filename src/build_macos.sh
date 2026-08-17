#!/bin/sh

# from Claus:
# with the new ARM platforms it seems to make sense to build macOS
# binaries as universal x86/arm in the future. I did not find an
# elegant way to do it in a Makefile, but maybe you want to add this
# handy script to your repo?

make clean
make CFLAGS="-target x86_64-apple-macos10.12"
mv acme acme_x86
make clean
make CFLAGS="-target arm64-apple-macos11"
mv acme acme_arm
lipo -create -output acme acme_x86 acme_arm
rm acme_x86
rm acme_arm
