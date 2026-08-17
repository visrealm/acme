// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2025 Marco Baye
// Have a look at "acme.c" for further info
//
// Global stuff - things that are needed by several modules
//  4 Oct 2006	Fixed a typo in a comment
// 22 Nov 2007	Added warn_on_indented_labels
//  2 Jun 2014	Added warn_on_old_for and warn_on_type_mismatch
// 19 Nov 2014	Merged Johann Klasek's report listing generator patch
// 23 Nov 2014	Merged Martin Piper's "--msvc" error output patch
//  9 Jan 2018	Made '/' a syntax char to allow for "//" comments
// 14 Apr 2020	Added config vars for "ignore zeroes" and "segment warnings to errors"
#include "global.h"
#include <string.h>	// for strcmp()
#if defined(_WIN32) || defined(_WIN64)
#include <stdlib.h>	// for _fullpath() and _MAX_PATH
#endif
#include "platform.h"
#include "acme.h"
#include "alu.h"
#include "cpu.h"
#include "dynabuf.h"
#include "encoding.h"
#include "input.h"
#include "macro.h"
#include "output.h"
#include "pseudoopcodes.h"
#include "section.h"
#include "symbol.h"
#include "tree.h"
#include "typesystem.h"


// constants
char		s_untitled[]	= "<untitled>";	// FIXME - this is actually const


// Exception messages during assembly
const char	exception_negative_size[]	= "Negative size argument.";
const char	exception_no_left_brace []	= "Expected '{' character.";
const char	exception_no_memory_left[]	= "Out of memory.";
const char	exception_no_right_brace []	= "Expected '}', found EOF instead.";
//const char	exception_not_yet[]	= "Sorry, feature not yet implemented.";
// TODO - show actual value in error message
const char	exception_number_out_of_range[]	= "Number out of range.";
const char	exception_number_out_of_8b_range[]	= "Number does not fit in 8 bits.";
static const char	exception_number_out_of_16b_range[]	= "Number does not fit in 16 bits.";
static const char	exception_number_out_of_24b_range[]	= "Number does not fit in 24 bits.";
const char	exception_pc_undefined[]	= "Program counter undefined.";
const char	exception_symbol_defined[]	= "Symbol already defined.";
const char	exception_syntax[]		= "Syntax error.";
// default value for number of errors before exiting
#define MAXERRORS	10

// Flag table:
// This table contains flags for all the 256 possible byte values. The
// assembler reads the table whenever it needs to know whether a byte is
// allowed to be in a label name, for example.
//   Bits	Meaning when set
// 7.......	Byte allowed to start keyword
// .6......	Byte allowed in keyword
// ..5.....	Byte is upper case, can be lowercased by OR-ing this bit(!)
// ...4....	special character for input syntax: 0x00 TAB LF CR SPC / : ; }
// ....3...	preceding sequence of '-' characters is anonymous backward
//		label. Currently only set for ')', ',' and CHAR_EOS.
// .....210	currently unused
const char	global_byte_flags[256]	= {
/*$00*/	0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// control characters
	0x00, 0x10, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*$20*/	0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,// " !"#$%&'"
	0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00, 0x10,// "()*+,-./"
	0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,// "01234567"
	0x40, 0x40, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,// "89:;<=>?"
/*$40*/	0x00, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,// "@ABCDEFG"
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,// "HIJKLMNO"
	0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0,// "PQRSTUVW"
	0xe0, 0xe0, 0xe0, 0x00, 0x00, 0x00, 0x00, 0xc0,// "XYZ[\]^_"
/*$60*/	0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,// "`abcdefg"
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,// "hijklmno"
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,// "pqrstuvw"
	0xc0, 0xc0, 0xc0, 0x00, 0x00, 0x10, 0x00, 0x00,// "xyz{|}~" BACKSPACE
/*$80*/	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,// umlauts etc. ...
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
/*$a0*/	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
/*$c0*/	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
/*$e0*/	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
	0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
};


// variables
struct report 	*report			= NULL;
struct config	config;
struct pass	pass;
struct sanity	sanity;

// set configuration to default values
void config_default(struct config *conf)
{
	conf->pseudoop_prefix		= '!';	// can be changed to '.' by CLI switch
	conf->process_verbosity		= 0;	// level of additional output
	conf->warn_on_indented_labels	= TRUE;	// warn if indented label is encountered
	conf->warn_on_type_mismatch	= FALSE;	// use type-checking system
	conf->warn_bin_mask		= 3;  // %11 -> warn if not divisible by four
	conf->max_errors		= MAXERRORS;	// errors before giving up
	conf->sanity_limit		= SANITY_LIMIT;	// changed by --maxdepth
	conf->format_msvc		= FALSE;	// enabled by --msvc
	conf->format_color		= FALSE;	// enabled by --color
	conf->msg_stream		= stderr;	// set to stdout by --use-stdout
	conf->honor_leading_zeroes	= TRUE;		// disabled by --ignore-zeroes
	conf->strict_segments		= FALSE;	// enabled by --strict-segments and V0.98
	conf->all_warnings_are_errors	= FALSE;	// enabled by --strict
	conf->test_new_features		= FALSE;	// enabled by --test
	conf->dialect			= V__CURRENT_VERSION;	// changed by --dialect
	conf->politeness		= 0;		// changed by --please
	conf->debuglevel		= DEBUGLEVEL_DEBUG;	// changed by --debuglevel, used by "!debug"
	conf->platform_lib_prefix	= PLATFORM_LIBPREFIX;
	conf->initial_cpu_type		= NULL;
	conf->symbollist_filename	= NULL;
	conf->vicelabels_filename	= NULL;
	conf->output_filename		= NULL;
	conf->outfile_format		= OUTFILE_FORMAT_UNSPECIFIED;
	conf->report_filename		= NULL;
	conf->mem_init_value		= NO_VALUE_GIVEN;	// set by --initmem
	conf->initial_pc		= NO_VALUE_GIVEN;	// set by --setpc
	conf->outfile_start		= NO_VALUE_GIVEN;	// set by --from-to
	conf->outfile_limit		= NO_VALUE_GIVEN;	// end+1, set by --from-to
}

// memory allocation stuff

// allocate memory and die if not available
void *safe_malloc(size_t size)
{
	void	*block	= malloc(size);

	if (block == NULL)
		throw_serious_error(exception_no_memory_left);

	return block;
}


// Parser stuff


static boolean	in_addr_block	= FALSE;
static boolean	in_nowarn_block	= FALSE;

// set new value for "we are in !addr block" flag,
// return old value
// (called by "!addr { }")
boolean parser_change_addr_block_flag(boolean new_value)
{
	boolean	old_value	= in_addr_block;

	in_addr_block = new_value;
	return old_value;
}
// set new value for "we are in !nowarn block" flag,
// return old value
// (called by "!nowarn { }")
boolean parser_change_nowarn_block_flag(boolean new_value)
{
	boolean	old_value	= in_nowarn_block;

	in_nowarn_block = new_value;
	return old_value;
}

#define SF_FOUND_BLANK		(1u << 0)	// statement started with space or tab
#define SF_FOUND_SYMBOL		(1u << 1)	// statement had label or symbol definition
#define SF_ADDR_PREFIX		(1u << 2)	// explicit symbol definition is an address
#define SF_NOWARN_PREFIX	(1u << 3)	// suppress warnings for this statement
static bits	statement_flags;

// called by "!addr" pseudo op if used without block
extern void parser_set_addr_prefix(void)
{
	statement_flags |= SF_ADDR_PREFIX;
}
// called by "!nowarn" pseudo op if used without block
extern void parser_set_nowarn_prefix(void)
{
	statement_flags |= SF_NOWARN_PREFIX;
}

// Check and return whether first symbol of statement. Complain if not.
static int first_symbol_of_statement(void)
{
	if (statement_flags & SF_FOUND_SYMBOL) {
		throw_error("Unknown mnemonic");
		parser_skip_remainder();
		return FALSE;
	}
	statement_flags |= SF_FOUND_SYMBOL;	// now there has been one
	return TRUE;
}


// parse label definition (can be either global or local).
// name must be held in GlobalDynaBuf.
// called by parse_symbol_definition, parse_backward_anon_def, parse_forward_anon_def
// "powers" is used by backward anons to allow changes
static void set_label(scope_t scope, bits force_bit, bits powers)
{
	struct symbol	*symbol;
	struct object	result;

	if ((statement_flags & SF_FOUND_BLANK) && config.warn_on_indented_labels) {
		throw_finalpass_warning("Label name not in leftmost column.");
	}
	symbol = symbol_find(scope);
	result.type = &type_number;
	programcounter_read_asterisk(&result.u.number);
	symbol_set_object(symbol, &result, powers);
	if (force_bit)
		symbol_set_force_bit(symbol, force_bit);
	// global labels must open new scope for cheap locals
	if (scope == SCOPE_GLOBAL)
		section_new_cheap_scope(section_now);
}


// call with symbol name in GlobalDynaBuf and '=' already eaten.
// fn is exported so "!set" pseudo opcode can call it.
// "powers" is for "!set" pseudo opcode so changes are allowed (see symbol.h for powers)
void parse_assignment(scope_t scope, bits force_bit, bits powers)
{
	struct symbol	*symbol;
	struct object	result;

	symbol = symbol_find(scope);
	ALU_any_result(&result);
	// if wanted, mark as address reference
	if (in_addr_block || (statement_flags & SF_ADDR_PREFIX)) {
		// FIXME - checking types explicitly is ugly...
		if (result.type == &type_number)
			result.u.number.addr_refs = 1;
	}
	symbol_set_object(symbol, &result, powers);
	if (force_bit)
		symbol_set_force_bit(symbol, force_bit);
}


// parse symbol definition (can be either global or local, may turn out to be a label).
// name must be held in GlobalDynaBuf.
static void parse_symbol_definition(scope_t scope)
{
	bits	force_bit;

	force_bit = parser_get_force_bit();	// skips spaces after	(yes, force bit is allowed for label definitions)
	if (GotByte == '=') {
		// explicit symbol definition (symbol = <something>)
		GetByte();	// eat '='
		parse_assignment(scope, force_bit, POWER_NONE);
		parser_ensure_EOS();
	} else {
		// implicit symbol definition (label)
		set_label(scope, force_bit, POWER_NONE);
	}
}


// Parse global symbol definition or assembler mnemonic
static void parse_mnemo_or_global_symbol_def(void)
{
	// read keyword and ask current cpu type if it's a mnemonic
	if (cpu_current_type->keyword_is_mnemonic(parser_read_keyword()))
		return;	// statement has been handled

	// if we're here, it wasn't a mnemonic, so it can only be a symbol name
	if (!first_symbol_of_statement())
		return;	// more than one symbol, error has been reported

	// Now GotByte = illegal char
	// 04 Jun 2005: this fix should help to explain "strange" error messages.
	// 17 May 2014: now it works for UTF-8 as well.
	if ((*GLOBALDYNABUF_CURRENT == (char) 0xa0)
	|| ((GlobalDynaBuf->size >= 2) && (GLOBALDYNABUF_CURRENT[0] == (char) 0xc2) && (GLOBALDYNABUF_CURRENT[1] == (char) 0xa0))) {
		throw_finalpass_warning("Symbol name starts with a shift-space character.");
	}
	parse_symbol_definition(SCOPE_GLOBAL);
}


// parse (cheap) local symbol definition
static void parse_local_symbol_def(void)
{
	scope_t	scope;

	if (!first_symbol_of_statement())
		return;

	if (input_read_scope_and_symbol_name(&scope) == 0)
		parse_symbol_definition(scope);
}


// parse anonymous backward label definition. Called with GotByte == '-'
static void parse_backward_anon_def(void)
{
	if (!first_symbol_of_statement())
		return;

	dynabuf_clear(GlobalDynaBuf);
	do {
		DYNABUF_APPEND(GlobalDynaBuf, '-');
	} while (GetByte() == '-');
	dynabuf_append(GlobalDynaBuf, '\0');
	// backward anons change their value!
	set_label(section_now->local_scope, NO_FORCE_BIT, POWER_CHANGE_VALUE);
}


// parse anonymous forward label definition. called with GotByte == ?
static void parse_forward_anon_def(void)
{
	if (!first_symbol_of_statement())
		return;

	dynabuf_clear(GlobalDynaBuf);
	dynabuf_append(GlobalDynaBuf, '+');
	while (GotByte == '+') {
		DYNABUF_APPEND(GlobalDynaBuf, '+');
		GetByte();
	}
	symbol_fix_forward_anon_name(TRUE);	// TRUE: increment counter
	dynabuf_append(GlobalDynaBuf, '\0');
	//printf("[%d, %s]\n", section_now->local_scope, GlobalDynaBuf->buffer);
	set_label(section_now->local_scope, NO_FORCE_BIT, POWER_NONE);
}


static enum shortcut	current_shortcut_state	= SHORTCUT_NONE;
// return current shortcut state
enum shortcut parser_get_shortcut(void)
{
	return current_shortcut_state;
}
// start or end processing a !break/!continue/!return keyword
void parser_set_shortcut(enum shortcut new_shortcut)
{
	//printf("Changing shortcut state from %d to %d.\n", current_shortcut_state, new_shortcut);
	switch (new_shortcut) {
	case SHORTCUT_NONE:
		if (current_shortcut_state == SHORTCUT_NONE)
			BUG("ShortcutDoubleNone", 0);
		current_shortcut_state = new_shortcut;
		break;
	case SHORTCUT_BREAK:
	case SHORTCUT_CONT:
	case SHORTCUT_RETURN:
		if (current_shortcut_state != SHORTCUT_NONE)
			BUG("ShortcutDouble", current_shortcut_state);
		current_shortcut_state = new_shortcut;
		break;
	default:
		BUG("IllegalShortcut", new_shortcut);
	}
}
// status var to tell mainloop (actually "statement loop") to exit.
// this is better than the error handler exiting directly, because
// there are cases where an error message is followed by an info message
// (like "macro redefined _here_" and "original definition _there_"),
// and this way the program does not exit between the two.
// FIXME - there is still the problem of not getting a dump of the macro call stack for the final error!
static boolean	too_many_errors	= FALSE;

// Parse block, beginning with next byte.
// End reason (either CHAR_EOB or CHAR_EOF) can be found in GotByte afterwards
// Has to be re-entrant.
void parse_until_eob_or_eof(void)
{
	// start with next byte
	// (don't SKIPSPACE() here, we want to warn about "label not in leftmost column"!)
	GetByte();
	// loop until end of block or end of file
	while ((GotByte != CHAR_EOB) && (GotByte != CHAR_EOF)) {
		// process one statement
		statement_flags = 0;	// no spaces, no labels, no !addr, no !nowarn
		// Parse until end of statement. Only loops in these cases:
		// - statement contains label and something else
		// - "!ifdef"/"!ifndef" is used without block
		// - "!addr"/"!nowarn" is used without block
		do {
			// check for pseudo opcodes was moved out of switch,
			// because prefix character is now configurable.
			if (GotByte == config.pseudoop_prefix) {
				pseudoopcode_parse();
			} else {
				switch (GotByte) {
				case CHAR_EOS:	// end of statement
					// Ignore now, act later
					// (stops from being "default")
					break;
				case ' ':	// space
					statement_flags |= SF_FOUND_BLANK;
					/*FALLTHROUGH*/
				case CHAR_SOL:	// start of line
					GetByte();	// skip
					break;
				case '-':
					parse_backward_anon_def();
					break;
				case '+':
					GetByte();
					if ((GotByte == LOCAL_PREFIX)
					|| (GotByte == CHEAP_PREFIX)
					|| (BYTE_CONTINUES_KEYWORD(GotByte)))
						macro_parse_call();
					else
						parse_forward_anon_def();
					break;
				case '*':
					notreallypo_setpc();	// define program counter (fn is in pseudoopcodes.c)
					break;
				case LOCAL_PREFIX:
				case CHEAP_PREFIX:
					parse_local_symbol_def();
					break;
				default:
					if (BYTE_STARTS_KEYWORD(GotByte)) {
						parse_mnemo_or_global_symbol_def();
					} else {
						throw_error(exception_syntax);	// FIXME - include char in error message!
						parser_skip_remainder();
					}
				}
			}
		} while (GotByte != CHAR_EOS);	// until end-of-statement
		output_end_statement();	// adjust program counter
		// did the error handler decide to give up?
		if (too_many_errors)
			exit(ACME_finalize(EXIT_FAILURE));
		// was any of the shortcut POs used?
		if (current_shortcut_state == SHORTCUT_NONE) {
			// go on with next byte
			GetByte();	//NEXTANDSKIPSPACE();
		} else {
			// ignore remainder of block:
			input_block_skip();
		}
	}
}


// Skip space. If GotByte is CHAR_SOB ('{'), parse block and return TRUE.
// Otherwise (if there is no block), return FALSE.
// Don't forget to call EnsureEOL() afterwards.
int parse_optional_block(void)
{
	SKIPSPACE();
	if (GotByte != CHAR_SOB)
		return FALSE;

	parse_until_eob_or_eof();
	if (GotByte != CHAR_EOB)
		throw_serious_error(exception_no_right_brace);
	GetByte();
	return TRUE;
}

// parse a whole source code file
// file name must be given in platform style, i.e.
// "directory/basename.extension" on linux,
// "directory.basename/extension" on RISC OS, etc.
// and the pointer must remain valid forever!
void parse_source_code_file(FILE *fd, const char *eternal_plat_filename)
{
	struct inputchange_buf	icb;
	const char		*ppb;	// path buffer in platform format
	boolean		break_cont_allowed;
	boolean		return_allowed;

	// be verbose
	if (config.process_verbosity >= 3)
		printf("Parsing source file \"%s\".\n", eternal_plat_filename);

	// remember base for relative paths and set new one:
	ppb = input_plat_pathref_filename;
	input_plat_pathref_filename = eternal_plat_filename;
	// remember input and set up new one:
	inputchange_new_file(&icb, fd, eternal_plat_filename);
	// remember whether break/continue/return are allowed and forbid them in
	// new file (just to enforce "clean code"):
	break_cont_allowed = parser_allow_break_cont(FALSE);
	return_allowed = parser_allow_return(FALSE);

	// parse block and check end reason
	parse_until_eob_or_eof();
	if (GotByte != CHAR_EOF)
		throw_error("Expected EOF, found '}' instead." );

	// restore states of break/continue/return
	parser_allow_return(return_allowed);
	parser_allow_break_cont(break_cont_allowed);
	// restore outer input
	inputchange_back(&icb);
	// restore outer base for relative paths
	input_plat_pathref_filename = ppb;
}

// read optional info about parameter length
bits parser_get_force_bit(void)
{
	char	byte;
	bits	force_bit	= 0;

	if (GotByte == '+') {
		byte = GetByte();
		if (byte == '1')
			force_bit = NUMBER_FORCES_8;
		else if (byte == '2')
			force_bit = NUMBER_FORCES_16;
		else if (byte == '3')
			force_bit = NUMBER_FORCES_24;
		if (force_bit)
			GetByte();
		else
			throw_error("Illegal postfix.");
	}
	SKIPSPACE();
	return force_bit;
}

// return current state and set new state of "allow !break and !continue" flag
boolean parser_allow_break_cont(boolean new_state)
{
	static boolean	flag	= FALSE;
	boolean		temp;

	temp = flag;
	flag = new_state;
	return temp;
}
// return current state and set new state of "allow !return" flag
boolean parser_allow_return(boolean new_state)
{
	static boolean	flag	= FALSE;
	boolean		temp;

	temp = flag;
	flag = new_state;
	return temp;
}


// Error handling

// This function will do the actual output for warnings, errors and serious
// errors. It shows the given message string, as well as the current
// context: file name, line number, source type and source title.
// if the "optional alternative location" given is NULL, the current location is used
static void print_msg(const char *message, const char *ansicolor, const char *type, struct location *opt_alt_loc)
{
	const char	*resetcolor	= "\033[0m";
	struct location	location;
	const char	*filename;

	if (!config.format_color) {
		ansicolor = "";
		resetcolor = "";
	}

	// optional alternative location given?
	if (opt_alt_loc)
		location = *opt_alt_loc;	// use alternative location
	else
		input_get_location(&location);	// use current location

	filename = location.plat_filename;
#if defined(_WIN32) || defined(_WIN64)
	// show absolute paths so IDEs can jump straight to the offending line.
	// on failure (or for pseudo-filenames like "-D SYMBOL=VALUE") just keep
	// what we were given.
	{
		static char	abs_path[_MAX_PATH];

		if (filename && _fullpath(abs_path, filename, _MAX_PATH))
			filename = abs_path;
	}
#endif

	if (config.format_msvc) {
		fprintf(config.msg_stream, "%s(%d) : %s%s%s (%s %s): %s\n",
			filename, location.line_number,
			ansicolor, type, resetcolor,
			section_now->type, section_now->title, message);
	} else {
		fprintf(config.msg_stream, "%s%s%s - File %s, line %d (%s %s): %s\n",
			ansicolor, type, resetcolor,
			filename, location.line_number,
			section_now->type, section_now->title, message);
	}
}

// flag, tells throw_error how to behave. set to FALSE by throw__done_with_cli_args().
// this is needed because errors like "\xZZ is not a valid backslash sequence" are
// handled by calling throw_error(), but they can happen
//	- in cli args, using the "-D SYMBOL=VALUE" syntax, where we want to exit, and
//	- in source codes, where we want a "file=F, line=N" output and go on!
static boolean	error_is_in_cli_args	= TRUE;

// generate debug/info/warning/error message
// if the "optional alternative location" given is NULL, the current location is used
void throw_message(enum debuglevel level, const char msg[], struct location *opt_alt_loc)
{
	// FIXME: we only expect normal errors in cli args, no warnings or
	// other stuff, so enable this:
	//if (error_is_in_cli_args)
	//	BUG("damn");

	// if level is taken from source, ensure valid value:
	if (level < DEBUGLEVEL_SERIOUS)
		level = DEBUGLEVEL_SERIOUS;

	switch (level) {
	case DEBUGLEVEL_SERIOUS:
		// output a serious error
		// (assembly stops, for example if outbuffer overruns).
		PLATFORM_SERIOUS(msg);
		print_msg(msg, "\033[1m\033[31m", "Serious error", opt_alt_loc);	// bold + red
		//++pass.counters.errors;	// FIXME - needed when problem below is solved
		exit(ACME_finalize(EXIT_FAILURE)); // FIXME - this inhibits output of macro call stack
	case DEBUGLEVEL_ERROR:
		// output an error
		// (something is wrong, no output file will be generated).
		PLATFORM_ERROR(msg);
		print_msg(msg, "\033[31m", "Error", opt_alt_loc);	// red
		++pass.counters.errors;
		if (pass.counters.errors >= config.max_errors)
			too_many_errors = TRUE;	// this causes mainloop to exit
		break;
	case DEBUGLEVEL_WARNING:
		// output a warning
		// (something looks wrong, like "label name starts with shift-space character")
		// first check if warnings should be suppressed right now:
		if (in_nowarn_block || (statement_flags & SF_NOWARN_PREFIX))
			break;
		PLATFORM_WARNING(msg);
		print_msg(msg, "\033[33m", "Warning", opt_alt_loc);	// yellow
		++pass.counters.warnings;
		// then check if warnings should be handled like errors:
		if (config.all_warnings_are_errors) {
			++pass.counters.errors;
			if (pass.counters.errors >= config.max_errors)
				too_many_errors = TRUE;	// this causes mainloop to exit
		}
		break;
	case DEBUGLEVEL_INFO:
		PLATFORM_INFO(msg);
		print_msg(msg, "\033[32m", "Info", opt_alt_loc);	// green
		break;
	default:
		// debug
		print_msg(msg, "\033[36m", "Debug", opt_alt_loc);	// cyan
		break;
	}
}

// output a warning (something looks wrong)
void throw_warning(const char msg[])
{
	throw_message(DEBUGLEVEL_WARNING, msg, NULL);
}

// output an error (something is wrong, no output file will be generated).
// the assembler will try to go on with the assembly, so the user gets to know
// about more than one of their typos at a time.
void throw_error(const char msg[])
{
	if (error_is_in_cli_args)
		ACME_cli_args_error(msg);	// does not return...
	else
		throw_message(DEBUGLEVEL_ERROR, msg, NULL);
}

// output a serious error (assembly stops, for example if outbuffer overruns).
extern void throw_serious_error(const char msg[])
{
	throw_message(DEBUGLEVEL_SERIOUS, msg, NULL);
}

// output a warning, but only if we are in first pass, otherwise suppress
// (only use this if using throw_finalpass_warning is impossible,
// for example in cases where the code is skipped in later passes)
void throw_pass1_warning(const char msg[])
{
	if (pass.number == 1)
		throw_warning(msg);
}

// output a warning, but only if we are in final pass, otherwise suppress
// (for example "converted float to int for XOR" -> must be done in
// final pass because values might be undefined earlier!)
void throw_finalpass_warning(const char msg[])
{
	if (pass.flags.is_final_pass)
		throw_warning(msg);
}

// throw "macro twice"/"symbol twice" error
// first output error as "error", then location of initial definition as "info"
void throw_redef_error(const char error_msg[], struct location *old_def, const char info_msg[])
{
	const char	*buffered_section_type;
	char		*buffered_section_title;

	// show error with current location
	throw_error(error_msg);

	// symbol structs do not necessarily have valid location data:
	if (old_def->plat_filename == NULL)
		return;

	// CAUTION, ugly kluge: fiddle with section_now data to generate
	// "earlier definition" section.
	// buffer old section
	buffered_section_type = section_now->type;
	buffered_section_title = section_now->title;
	// set new (fake) section
	// FIXME - maybe store section in definition, just as location is stored?
	// then we could use real data here instead of faking it, but it would
	// take a bit more memory...
	section_now->type = "earlier";
	section_now->title = "section";
	// show info message with location of earlier definition
	throw_message(DEBUGLEVEL_INFO, info_msg, old_def);
	// restore old section
	section_now->type = buffered_section_type;
	section_now->title = buffered_section_title;
}

// ugly kluge, switches throw_error() from cli args mode to normal mode
void throw__done_with_cli_args(void)
{
	error_is_in_cli_args = FALSE;
}

// handle bugs
// FIXME - use a local buffer and sprintf/snprintf to put error code into message!
void BUG(const char *message, int code)
{
	throw_warning("Bug in ACME, code follows");
	fprintf(stderr, "(0x%x:)", code);
	throw_serious_error(message);
}

// process error that might vanish if symbols change:
// if current pass is an "error output" pass, actually throw error.
// otherwise just increment counter to let mainloop know this pass wasn't successful.
void countorthrow_value_error(const char *msg)
{
	boolean	complain;

	if (config.dialect >= V0_98__PATHS_AND_SYMBOLCHANGE)
		complain = pass.flags.throw_all_errors;	// since 0.98, "value errors" are only thrown if pass flag is set
	else
		complain = TRUE;	// in earlier versions they were always thrown

	if (complain)
		throw_error(msg);
	else
		++pass.counters.suppressed_errors;
}


// insert object (in case of list, will iterate/recurse until done)
void output_object(struct object *object, struct iter_context *iter)
{
	struct listitem	*item;
	int		length;
	char		*read;

	if (object->type == &type_number) {
		if (object->u.number.ntype == NUMTYPE_UNDEFINED)
			iter->fn(0);
		else if (object->u.number.ntype == NUMTYPE_INT)
			iter->fn(object->u.number.val.intval);
		else if (object->u.number.ntype == NUMTYPE_FLOAT)
			iter->fn(object->u.number.val.fpval);
		else
			BUG("IllegalNumberType0", object->u.number.ntype);
	} else if (object->type == &type_list) {
		// iterate over list
		item = object->u.listhead->next;
		while (item != object->u.listhead) {
			output_object(&item->u.payload, iter);
			item = item->next;
		}
	} else if (object->type == &type_string) {
		// iterate over string
		read = object->u.string->payload;
		length = object->u.string->length;
		// single-char strings are accepted, to be more compatible with
		// versions before 0.97 (and empty strings are not really a problem...)
		if (iter->accept_long_strings || (length < 2)) {
			while (length--)
				iter->fn(iter->stringxor ^ encoding_encode_char(*(read++)));
		} else {
			throw_error("There's more than one character.");	// see alu.c for the original of this error
		}
	} else {
		BUG("IllegalObjectType", 0);
	}
}


// output 8-bit value with range check
void output_8(intval_t value)
{
	if ((value < -0x80) || (value > 0xff))
		countorthrow_value_error(exception_number_out_of_8b_range);
	output_byte(value);
}


// output 16-bit value with range check big-endian
void output_be16(intval_t value)
{
	if ((value < -0x8000) || (value > 0xffff))
		countorthrow_value_error(exception_number_out_of_16b_range);
	output_byte(value >> 8);
	output_byte(value);
}


// output 16-bit value with range check little-endian
void output_le16(intval_t value)
{
	if ((value < -0x8000) || (value > 0xffff))
		countorthrow_value_error(exception_number_out_of_16b_range);
	output_byte(value);
	output_byte(value >> 8);
}


// output 24-bit value with range check big-endian
void output_be24(intval_t value)
{
	if ((value < -0x800000) || (value > 0xffffff))
		countorthrow_value_error(exception_number_out_of_24b_range);
	output_byte(value >> 16);
	output_byte(value >> 8);
	output_byte(value);
}


// output 24-bit value with range check little-endian
void output_le24(intval_t value)
{
	if ((value < -0x800000) || (value > 0xffffff))
		countorthrow_value_error(exception_number_out_of_24b_range);
	output_byte(value);
	output_byte(value >> 8);
	output_byte(value >> 16);
}


// FIXME - the range checks below are commented out because 32-bit
// signed integers cannot exceed the range of 32-bit signed integers.
// But now that 64-bit machines are the norm, "intval_t" might be a
// 64-bit int. I need to address this problem one way or another.


// output 32-bit value (without range check) big-endian
void output_be32(intval_t value)
{
//	if ((value < -0x80000000) || (value > 0xffffffff))
//		countorthrow_value_error(exception_number_out_of_32b_range);
	output_byte(value >> 24);
	output_byte(value >> 16);
	output_byte(value >> 8);
	output_byte(value);
}


// output 32-bit value (without range check) little-endian
void output_le32(intval_t value)
{
//	if ((value < -0x80000000) || (value > 0xffffffff))
//		countorthrow_value_error(exception_number_out_of_32b_range);
	output_byte(value);
	output_byte(value >> 8);
	output_byte(value >> 16);
	output_byte(value >> 24);
}


// string shown in CLI error message if outputformat_set() returns nonzero:
const char	outputformat_names[]	= "'plain', 'cbm', 'apple', 'hex'";

// convert output format name held in DynaBuf to enum.
// returns OUTFILE_FORMAT_UNSPECIFIED on error.
enum outfile_format outputformat_find(void)
{
	if (strcmp(GlobalDynaBuf->buffer, "plain") == 0)
		return OUTFILE_FORMAT_PLAIN;
	else if (strcmp(GlobalDynaBuf->buffer, "cbm") == 0)
		return OUTFILE_FORMAT_CBM;
	else if (strcmp(GlobalDynaBuf->buffer, "apple") == 0)
		return OUTFILE_FORMAT_APPLE;
	else if (strcmp(GlobalDynaBuf->buffer, "hex") == 0)
		return OUTFILE_FORMAT_HEX;
//	else if (strcmp(GlobalDynaBuf->buffer, "o65") == 0)
//		return OUTFILE_FORMAT_O65;

	return OUTFILE_FORMAT_UNSPECIFIED;
}
