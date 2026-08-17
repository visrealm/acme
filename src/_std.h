// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2025 Marco Baye
// Have a look at "acme.c" for further info
//
// Platform specific stuff (in this case, for unknown OSes)
#ifndef platform_H
#define platform_H


// symbolic constants and macros

// called once on program startup (could register exit handler, if needed)
#define PLATFORM_INIT		platform_read_env_var()

// directory separator for search paths
#define DIRECTORY_SEPARATOR	'/'
#define ALTERNATIVE_DIR_SEP	'/'	// dummy

// string containing the prefix for accessing files from the library tree
// (use "--libpath" cli switch or "ACME" environment variable to set this)
#define PLATFORM_LIBPREFIX	NULL
#define PLATFORM_USE_ENV_VAR	1	// library access needs "ACME" environment variable
// if you are building an "ACME" package for a linux or bsd distribution, you
// could put the library files at /usr/share/acme/lib/ and use that path instead
// of "NULL" above. the user then only needs the "ACME" environment variable if
// they want to override the default path given here.

// setting the created files' types
#define PLATFORM_SETFILETYPE_APPLE(a)
#define PLATFORM_SETFILETYPE_CBM(a)
#define PLATFORM_SETFILETYPE_PLAIN(a)
#define PLATFORM_SETFILETYPE_TEXT(a)
#define PLATFORM_SETFILETYPE_HEX(a)

// platform specific message output
#define PLATFORM_INFO(a)
#define PLATFORM_WARNING(a)
#define PLATFORM_ERROR(a)
#define PLATFORM_SERIOUS(a)

// integer-to-character conversion
#define PLATFORM_UINT2CHAR(x)	\
do {				\
	x ^= x >> 16;		\
	x ^= x >>  8;		\
	x &= 255;		\
} while (0)

// output of platform-specific command line switches
#define PLATFORM_OPTION_HELP

// processing of platform-specific command line switches
#define PLATFORM_SHORTOPTION_CODE
#define PLATFORM_LONGOPTION_CODE


#endif
