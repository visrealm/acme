// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// Input stuff
#ifndef input_H
#define input_H


#include <stdio.h>	// for FILE
#include "config.h"	// for bits and scope_t


// type definitions

enum inputsrc {
	INPUTSRC_NONE,	// dummy before input is set up
	INPUTSRC_FILE,
	INPUTSRC_RAM
};
struct input {
	// the filename below (in "location") refers to the source file where
	// the current code initially came from, i.e. it may change during macro execution.
	struct location	location;	// file + line (during RAM reads as well)
	enum inputsrc	srctype;
	int		state;	// state of input (type is really "enum inputstate")
	union {
		FILE		*fd;		// file descriptor
		const char	*ram_ptr;	// RAM read ptr (loop or macro block)
	} u;
	boolean		report;	// copy to report file?
};
struct filespecflags {
	boolean	uses_lib;	// file name was given in <...> instead of "..."
	boolean	absolute;	// file name started with '/'
};


// constants

extern const char	FILE_READBINARY[];
// Special characters
// The program *heavily* relies on CHAR_EOS (end of statement) being 0x00!
#define CHAR_EOS	(0)	// end of statement	(in high-level format)
#define CHAR_SOB	'{'	// start of block
#define CHAR_EOB	'}'	// end of block
#define CHAR_SOL	(10)	// start of line	(in high-level format)
#define CHAR_EOF	(13)	// end of file		(in high-level format)
// if the characters above are changed, don't forget to adjust global_byte_flags[]!


// variables

extern char		GotByte;	// last byte read (processed)
// name of source file used for resolving relative paths
// (i.e. not changed during macro execution):
extern const char	*input_plat_pathref_filename;	// file name in platform format



// prototypes

// get next byte from currently active byte source in shortened high-level
// format. When inside quotes, use input_quoted_to_dynabuf() instead!
extern char GetByte(void);

// skip remainder of statement, for example on error
extern void parser_skip_remainder(void);

// ensure that the remainder of the current statement is empty, for example
// after mnemonics using implied addressing.
extern void parser_ensure_EOS(void);

// process backslash escapes in GlobalDynaBuf (so size might shrink)
// returns 1 on escaping errors
extern int unescape_dynabuf(void);

// clear dynabuf, read string to it until closing quote is found, then process
// backslash escapes (so size might shrink)
// returns 1 on error (unterminated or escaping errors)
extern int input_read_string_literal(char closing_quote);

// clear dynabuf, read remainder of statement into it, making sure to keep quoted stuff intact
extern void input_read_statement(char terminator);

// Skip block (starting with next byte, so call directly after reading opening brace).
// After calling this function, GotByte holds '}'. Unless EOF was found first,
// but then a serious error would have been thrown.
extern void input_block_skip(void);
// Read block into GlobalDynabuf, make a copy and store pointer in struct.
// (reading starts with next byte, so call directly after reading opening brace).
// After calling this function, GotByte holds '}'. Unless EOF was found first,
// but then a serious error would have been thrown.
extern void input_block_getcopy(struct block *block);

// append optional '.'/'@' prefix to GlobalDynaBuf, then keep
// appending while characters are legal for keywords.
// throw "Expected symbol name" error if none.
// return whether there was an error.
extern int input_append_symbol_name_to_global_dynabuf(void);

// FIXME - move these to "symbol.h" and remove dependency on "scope":
// read symbol name into GlobalDynaBuf, set scope,
// return whether there was an error (namely, "Expected symbol name.")
extern int input_readscopeandsymbolname(scope_t *scope, boolean dotkluge);
#define input_read_scope_and_symbol_name(scope)	input_readscopeandsymbolname(scope, FALSE)
#define input_read_scope_and_symbol_name_KLUGED(scope)	input_readscopeandsymbolname(scope, TRUE)

// Clear dynamic buffer, then append to it until an illegal (for a keyword)
// character is read. Zero-terminate the string. Return its length (without
// terminator).
// Zero lengths will produce an error.
extern int parser_read_keyword(void);

// Clear dynamic buffer, then append to it until an illegal (for a keyword)
// character is read. Zero-terminate the string, then convert to lower case.
// Return its length (without terminator).
// Zero lengths will produce an "Expected keyword" error.
extern int parser_read_and_lower_keyword(void);

// try to read a file name for an input file.
// library access by using <...> quoting is allowed.
// flags for "library access" and "absolute path" will be set accordingly.
// The file name given in the assembler source code is converted from
// UNIX style to platform style.
// on success, returns zero. filename is in GlobalDynaBuf, caller should then
//	call includepaths_open_ro().
// on error, returns nonzero. errors are handled and reported, but caller should
//	then call parser_skip_remainder().
extern int input_read_input_filename(struct filespecflags *flags);

// try to read a file name for an output file ("!to" and "!sl" only).
// library access by using <...> quoting is forbidden.
// The file name given in the assembler source code is converted from
// UNIX style to platform style.
// on success, returns zero. filename is in GlobalDynaBuf, caller should then
//	make a copy to pass it to fopen() later on.
// on error, returns nonzero. errors are handled and reported, but caller should
//	then call parser_skip_remainder().
// FIXME - the name suggests this fn reads "the" output filename, but it only
// reads "an" output filename: either symbollist or the real output file.
extern int input_read_output_filename(void);

// Try to read a comma, skipping spaces before and after. Return TRUE if comma
// found, otherwise FALSE.
extern int parser_accept_comma(void);

// Try to read given character.
// If found, eat character and return TRUE.
// If not found, throw syntax error and return FALSE.
extern int parser_expect(int chr);

// force input system to return "end of file" on next read
// (back end function for "!eof" pseudo opcode)
extern void input_force_eof(void);

// enable/disable writing to report file
// (only enable if report file has been opened!)
extern void input_set_report_enabled(boolean new_state);

// write current "location" (file name and line number) to given target
extern void input_get_location(struct location *target);


// "input change" stuff:

// treat this struct as opaque, its components should only be referenced by inputchange_* functions!
struct inputchange_buf {
	struct input	input;
	char		gb;	// buffer for GotByte
};
// save current input struct in buffer, then switch input to new source code file
extern void inputchange_new_file(struct inputchange_buf *icb, FILE *fd, const char *eternal_plat_filename);
// save current input struct in buffer, then switch to RAM
extern void inputchange_new_ram(struct inputchange_buf *icb);
// setup for reading from RAM (for parsing loop conditions etc.)
extern void inputchange_set_ram(int line_num, const char *body);
// switch input to macro parameters
extern void inputchange_macro1_params(const struct location *def, const char *params);
// switch from macro parameters to macro body
extern void inputchange_macro2_body(const char *macro_body);
// restore input struct from buffer
extern void inputchange_back(const struct inputchange_buf *icb);


// "include path" stuff:

// add entry
extern void includepaths_add(const char *path);

// open file for reading
// "flags" decide whether library access, search paths or absolute path is wanted.
// file name is expected in GlobalDynaBuf, in platform style and terminated.
// returns NULL or open stream
// on success, GlobalDynaBuf contains full file name in platform style
extern FILE *includepaths_open_ro(struct filespecflags *flags);


#endif
