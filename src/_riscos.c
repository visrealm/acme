// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// Platform specific stuff (in this case, for RISC OS)
#ifndef platform_C
#define platform_C


#include <stdlib.h>
#include <string.h>	// for strlen and memmove
#include <kernel.h>
#include "input.h"	// for input_get_location()


// constants

// SWIs
#define  OS_FILE			0x00008
#define XDDEUTILS_THROWBACKSTART	0x62587
#define XDDEUTILS_THROWBACKSEND		0x62588
#define XDDEUTILS_THROWBACKEND		0x62589


// variables
bits	RISCOS_flags	= 0;	// used to store platform-specific flags


// exit handler: if throwback was used, de-register now
void RISCOS_exit(void)
{
	_kernel_swi_regs	regs;

	if (RISCOS_flags & RISCOSFLAG_THROWN) {
		_kernel_swi(XDDEUTILS_THROWBACKEND, &regs, &regs);
		RISCOS_flags &= ~RISCOSFLAG_THROWN;
	}
}


// used as PLATFORM_INIT: registers exit handler
void RISCOS_entry(void)
{
	atexit(RISCOS_exit);
}


// convert UNIX-style path name to local platform style path name and decide
// whether path is absolute or relative. p points to the terminated input
// string, to be overwritten with the terminated output string.
// there is enough space allocated for the output string to be _one_(1!) byte
// longer than the input string!
// conversions done for RISC OS:
// "path/to/file.ext" -> "path.to.file/ext"
// "/absolute/path" -> "$.absolute.path"	(this needs the one extra byte!)
// "../../twolevelsup" -> "^.^.twolevelsup"
void platform_convert_path(boolean *is_absolute, char *readptr)
{
	char	*writeptr,
		previous;
	size_t	bytes;

	// init
	*is_absolute = FALSE;	// there are several ways for this to become true
	// check for leading '/' and add '$' prefix:
	if (*readptr == '/') {
		// this is the extremely unlikely situation where an extra byte
		// is needed, so let's get it done:
		*is_absolute = TRUE;
		bytes = strlen(readptr) + 1;	// count terminator as well
		memmove(readptr + 1, readptr, bytes);	// "/path\0\0" -> "//path\0"
		*(readptr++) = '$';	// "//path\0" -> "$/path\0"
	}

	writeptr = readptr;
	previous = '/';	// make sure ".." substitution also works for the first component
	// now scan remaining characters and convert all "../" components to "^."
	while (*readptr) {
		if ((previous == '/')
		&& (*readptr == '.')
		&& (readptr[1] == '.')
		&& (readptr[2] == '/')) {
			readptr += 2;
			*(writeptr++) = '^';
		}
		previous = *readptr;	// remember for next ".." check
		if ((*readptr == ':')	// path prefixes like "myproject:"...
		|| (*readptr == '$')	// ... or root directory indicator '$'...
		|| (*readptr == '&')	// ... or "user root directory" indicator '&'...
		|| (*readptr == '%')) {	// ... or "user library directory" indicator '%'...
			*is_absolute = TRUE;	// ...mean path is absolute
		}
		// convert characters
		if (*readptr == '.') {
			*writeptr = '/';
		} else if (*readptr == '/') {
			*writeptr = '.';
		} else if (*readptr == '?') {
			*writeptr = '#';
		} else if (*readptr == '#') {
			*writeptr = '?';
		} else {
			*writeptr = *readptr;	// copy character
		}
		++readptr;
		++writeptr;
	}
	*writeptr = *readptr;	// copy terminator
}


// setting the created files' types
void RISCOS_set_filetype(const char *filename, int file_type)
{
	_kernel_swi_regs	regs;

	regs.r[0] = 18;	// reason code (set file type)
	regs.r[1] = (int) filename;
	regs.r[2] = file_type;
	_kernel_swi(OS_FILE, &regs, &regs);
}


// throwback protocol: "type" can be 0, 1 or 2 (DDEUtils message types)
void RISCOS_throwback(const char *message, int type)
{
	struct location		location;
	_kernel_swi_regs	regs;

	// only use throwback protocol if wanted
	if ((RISCOS_flags & RISCOSFLAG_THROWBACK) == 0)
		return;

	input_get_location(&location);

	// if this is the first throwback, set it up and send info
	if ((RISCOS_flags & RISCOSFLAG_THROWN) == 0) {
		RISCOS_flags |= RISCOSFLAG_THROWN;
		_kernel_swi(XDDEUTILS_THROWBACKSTART, &regs, &regs);
		regs.r[0] = 0;
		regs.r[1] = 0;
	//	regs.r[2] = (int) toplevel_source;
		regs.r[2] = (int) location.plat_filename;
		_kernel_swi(XDDEUTILS_THROWBACKSEND, &regs, &regs);
	}
	// send throwback message
	regs.r[0] = 1;
	regs.r[1] = 0;
	regs.r[2] = (int) location.plat_filename;
	regs.r[3] = location.line_number;
	regs.r[4] = type;
	regs.r[5] = (int) message;
	_kernel_swi(XDDEUTILS_THROWBACKSEND, &regs, &regs);
}


#endif
