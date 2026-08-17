// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// platform specific stuff (in this case, for AmigaOS)
#ifndef platform_C
#define platform_C


// convert UNIX-style path name to local platform style path name and decide
// whether path is absolute or relative. p points to the terminated input
// string, to be overwritten with the terminated output string.
// conversions done for AmigaOS:
// "/absolute/path" -> ":absolute/path"
// "../../twolevelsup" -> "//twolevelsup"
void platform_convert_path(boolean *is_absolute, char *readptr)
{
	char	*writeptr,
		previous;

	// init
	*is_absolute = FALSE;	// there are two ways for this to become true
	writeptr = readptr;	// good thing the string can only shrink, but not grow
	previous = '/';	// make sure ".." substitution also works for the first component
	// check for leading '/'
	if (*readptr == '/') {
		*is_absolute = TRUE;
		++readptr;
		*(writeptr++) = ':';
	}
	// now scan remaining characters and convert all "../" components to "/"
	while (*readptr) {
		if ((previous == '/')
		&& (*readptr == '.')
		&& (readptr[1] == '.')
		&& (readptr[2] == '/')) {
			readptr += 2;
		}
		previous = *readptr;	// remember for next ".." check
		// prefixes like "workbench:" also mean the path is absolute
		if (*readptr == ':') {
			*is_absolute = TRUE;
		}
		*writeptr = *readptr;	// copy character
		++readptr;
		++writeptr;
	}
	*writeptr = *readptr;	// copy terminator
}


#endif
