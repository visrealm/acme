// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2025 Marco Baye
// Have a look at "acme.c" for further info
//
// Platform specific stuff
#include "platform.h"
#include "global.h"	// for config


// Amiga
#ifdef _AMIGA
#include "_amiga.c"
#endif

// DOS, OS/2 and Windows
#if defined(__DJGPP__) || defined(__OS2__) || defined(__Windows__) || defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(_WIN64) || defined(__MINGW32__) || defined(__MINGW64__)
#include "_dos.c"
#endif

// RISC OS
#ifdef __riscos__
#include "_riscos.c"
#endif

// add further platform files here

// Unix/Linux/others (also works on newer versions of Windows)
#ifndef platform_C
#include "_std.c"
#endif


// set or change library path
void platform_set_lib_path(const char new_path[])
{
	if (config.platform_lib_prefix) {
		// this is mostly for debugging so people can check if their env var setup works:
		printf("Changing library path from \"%s\" to \"%s\".\n", config.platform_lib_prefix, new_path);
		fflush(stdout);	// force output before any subsequent errors
	}
	config.platform_lib_prefix = new_path;
}


// stuff shared by some, but not all platforms:
#if PLATFORM_USE_ENV_VAR

#include <stdlib.h>	// for getenv()
#include "dynabuf.h"

// function to setup pointer to library tree from env var
void platform_read_env_var(void)
{
	char	*env_var;

	// find out the path of ACME's library
	env_var = getenv("ACME");
	// if environment variable was found, make a copy
	if (env_var) {
		dynabuf_clear(GlobalDynaBuf);
		// copy environment variable to global dynamic buffer
		dynabuf_add_string(GlobalDynaBuf, env_var);
		dynabuf_append(GlobalDynaBuf, '\0');	// add terminator
		platform_set_lib_path(dynabuf_get_copy(GlobalDynaBuf));
	}
}

#endif
