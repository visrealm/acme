// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2025 Marco Baye
// Have a look at "acme.c" for further info
//
// Global stuff - things that are needed by several modules
// 19 Nov 2014	Merged Johann Klasek's report listing generator patch
// 23 Nov 2014	Merged Martin Piper's "--msvc" error output patch
#ifndef global_H
#define global_H


#include <stdio.h>	// for FILE
#include "config.h"


// constants

#define LOCAL_PREFIX	'.'	// DO NOT CHANGE (or change expression parser accordingly)
#define CHEAP_PREFIX	'@'	// prefix character for cheap locals
#define NO_VALUE_GIVEN	(-1)	// used when only non-negative values are valid, e.g. for addresses

extern char		s_untitled[];
// error messages during assembly
extern const char	exception_negative_size[];
extern const char	exception_no_left_brace[];
extern const char	exception_no_memory_left[];
extern const char	exception_no_right_brace[];
//extern const char	exception_not_yet[];
extern const char	exception_number_out_of_range[];
extern const char	exception_number_out_of_8b_range[];
extern const char	exception_pc_undefined[];
extern const char	exception_symbol_defined[];
extern const char	exception_syntax[];
// byte flags table
extern const char	global_byte_flags[];
#define BYTE_STARTS_KEYWORD(b)		(global_byte_flags[(unsigned char) b] & (1u << 7))	// byte is allowed at start of keyword (a-z, A-Z, _, everything>127)
#define BYTE_CONTINUES_KEYWORD(b)	(global_byte_flags[(unsigned char) b] & (1u << 6))	// byte is allowed in a keyword (as above, plus digits)
//#define BYTE_TO_LOWER_CASE(b)	bit 5 means: "byte is upper case, and can be converted to lower case by ORing this bit" - but this is not used at the moment!
#define BYTE_IS_SYNTAX_CHAR(b)		(global_byte_flags[(unsigned char) b] & (1u << 4))	// special character for input syntax
#define BYTE_FOLLOWS_ANON(b)		(global_byte_flags[(unsigned char) b] & (1u << 3))	// preceding '-' are backward label
// bits 2, 1 and 0 are currently unused

enum dialect {
	V0_85__OLDEST_SUPPORTED,	// v0.85 looks like the oldest version it makes sense to actually support
	V0_86__DEPRECATE_REALPC,	// v0.86 made !pseudopc/!realpc give a warning to use !pseudopc{} instead, and !to wants a file format
	V0_93__SHORTER_SETPC_WARNING,	// v0.93 claimed to allow *= inside !pseudopc blocks, but didn't. It shortened the warning, but '}' or !realpc clobbered PC
	V0_94_6__RIGHT_ASSOC_POWER,	// v0.94.6 made "power of" operator right-associative
					// v0.94.7 fixed a bug: empty code segments no longer included in output file
	V0_94_8__DISABLED_OBSOLETE,	// v0.94.8 made *= work inside !pseudopc, disabled !cbm/!realpc/!subzone
	V0_94_12__NEW_FOR_SYNTAX,	// v0.94.12 introduced the new "!for" syntax
	V0_95_2__NEW_ANC_OPCODE,	// v0.95.2 changed ANC#8 from opcode 0x2b to 0x0b
	V0_97__BACKSLASH_ESCAPING,	// v0.97 introduced backslash escaping (and therefore strings)
	V__CURRENT_VERSION,		// "RELEASE"
	V0_98__PATHS_AND_SYMBOLCHANGE,	// v0.98 fixes paths and allows symbols to change
					// possible changes in future versions:
					//	ignore leading zeroes?
	V__FUTURE_VERSION		// future (for testing new features)
};
enum debuglevel {
	DEBUGLEVEL_SERIOUS	= -3,	// ACME stops right away
	DEBUGLEVEL_ERROR	= -2,	// something is wrong
	DEBUGLEVEL_WARNING	= -1,	// something looks wrong
	DEBUGLEVEL_INFO		= 0,	// info msg ("173 bytes left in code area!")
	DEBUGLEVEL_DEBUG	= 1	// debug msg
	// debug messages with higher levels are suppressed,
	// can be changed using "--debuglevel" cli switch.
};
enum outfile_format {
	OUTFILE_FORMAT_UNSPECIFIED,	// default (uses "plain" actually)
	OUTFILE_FORMAT_PLAIN,		// no header, just code
	OUTFILE_FORMAT_CBM,		// 16-bit load address, code (default for "!to" pseudo opcode)
	OUTFILE_FORMAT_APPLE,		// 16-bit load address, 16-bit length, code
	OUTFILE_FORMAT_HEX		// Intel HEX (text)
};
// configuration
struct config {
	char		pseudoop_prefix;	// '!' or '.'
	int		process_verbosity;	// level of additional output
	boolean		warn_on_indented_labels;	// warn if indented label is encountered
	boolean		warn_on_type_mismatch;	// use type-checking system
	int		warn_bin_mask;	// bitmask for digit counter of binary literals
	int		max_errors;	// errors before giving up
	int		sanity_limit;	// max recursion depth for "!src" and macro calls, also max number of passes
	boolean		format_msvc;		// enabled by --msvc
	boolean		format_color;		// enabled by --color
	FILE		*msg_stream;		// defaults to stderr, changed to stdout by --use-stdout
	boolean		honor_leading_zeroes;	// TRUE, disabled by --ignore-zeroes
	boolean		strict_segments;	// FALSE, enabled by --strict-segments and V0.98
	boolean		all_warnings_are_errors;	// FALSE, enabled by --strict
	boolean		test_new_features;	// FALSE, enabled by --test
	enum dialect	dialect;	// set by --dialect (and --test --test)
	int		politeness;	// incremented by --please
	int		debuglevel;	// set by --debuglevel, used by "!debug"
	const char	*platform_lib_prefix;	// default value depends on platform
	const struct cpu_type	*initial_cpu_type;
	const char	*symbollist_filename;
	const char	*vicelabels_filename;
	const char	*output_filename;	// TODO - put in "part" struct
	enum outfile_format	outfile_format;
	const char	*report_filename;	// TODO - put in "part" struct
// default value for these fields (if cli switch not used) is NO_VALUE_GIVEN:
	int		mem_init_value;	// set by --initmem
	intval_t	initial_pc;	// set by --setpc
	intval_t	outfile_start;	// set by --from-to
	intval_t	outfile_limit;	// end+1, set by --from-to
};
extern struct config	config;

enum shortcut {
	SHORTCUT_NONE,	// normal execution
	SHORTCUT_BREAK,	// after "!break"
	SHORTCUT_CONT,	// after "!continue"
	SHORTCUT_RETURN	// after "!return"
};
struct pass {
	int	number;	// counts up from one
	struct {
		int	undefineds;	// counts undefined expression results (if this stops decreasing, next pass must list them as errors)
		//int	needvalue;	// counts undefined expression results actually needed for output (when this hits zero, we're done)
// okay, this "needvalue" idea is problematic: evaluating "dec(UNDEFINED)" gives
// a defined result, namely an empty string. so the evaluation has to increment
// *some* counter to make sure more passes are done. but it cannot increment the
// "needvalue" counter because the expression parser does not know whether the
// result is actually put into memory!
		int	symbolchanges;	// count symbol changes (if nonzero, another pass is needed)
		int	errors;		// if nonzero -> stop after this pass
		int	warnings;	// this is needed for showing macro call stack
		int	suppressed_errors;	// this is for
		// "errors not reported because pass flags said so", because
		// then we can read the value after all symbol changes have
		// finally settled and know if the next pass is a victory lap or
		// an error output pass.
	} counters;
	struct {
		boolean	complain_about_undefined;	// will be FALSE until error pass is needed
		boolean	throw_all_errors;	// will be FALSE until error pass is needed
		boolean	is_final_pass;	// needed for warnings like "converted float to int for xor"
		boolean	generate_output;	// create output and/or report file
	} flags;
};
extern struct pass	pass;

struct sanity {
	int	macro_recursions_left;	// for macro calls
	int	source_recursions_left;	// for "!src"
};
extern struct sanity	sanity;

// report stuff
#define REPORT_ASCBUFSIZE	1024
#define REPORT_BINBUFSIZE	9	// eight are shown, then "..."
struct report {
	FILE		*fd;		// report file descriptor (NULL => no report)
	boolean		new_input;
	size_t		asc_used;
	size_t		bin_used;
	int		bin_address;	// address at start of bin_buf[]
	char		asc_buf[REPORT_ASCBUFSIZE];	// source bytes
	char		bin_buf[REPORT_BINBUFSIZE];	// output bytes
};
extern struct report	*report;	// TODO - put in "part" struct

// Macros for skipping a single space character
#define SKIPSPACE()		\
do {				\
	if (GotByte == ' ')	\
		GetByte();	\
} while (0)
#define NEXTANDSKIPSPACE()	\
do {				\
	if (GetByte() == ' ')	\
		GetByte();	\
} while (0)


// prototypes

// set configuration to default values
extern void config_default(struct config *conf);

// allocate memory and die if not available
extern void *safe_malloc(size_t amount);

// set new value for "we are in !addr block" flag,
// return old value
// (called by "!addr { }")
extern boolean parser_change_addr_block_flag(boolean new_value);

// set new value for "we are in !nowarn block" flag,
// return old value
// (called by "!nowarn { }")
extern boolean parser_change_nowarn_block_flag(boolean new_value);

// called by "!addr" pseudo op if used without block
extern void parser_set_addr_prefix(void);

// called by "!nowarn" pseudo op if used without block
extern void parser_set_nowarn_prefix(void);

// call with symbol name in GlobalDynaBuf and '=' already eaten.
// "powers" is for "!set" pseudo opcode so changes are allowed (see symbol.h for powers)
extern void parse_assignment(scope_t scope, bits force_bit, bits powers);

// Parse block, beginning with next byte.
// End reason (either CHAR_EOB or CHAR_EOF) can be found in GotByte afterwards
// Has to be re-entrant.
extern void parse_until_eob_or_eof(void);

// Skip space. If GotByte is CHAR_SOB ('{'), parse block and return TRUE.
// Otherwise (if there is no block), return FALSE.
// Don't forget to call EnsureEOL() afterwards.
extern int parse_optional_block(void);

// parse a whole source code file
// file name must be given in platform style, i.e.
// "directory/basename.extension" on linux,
// "directory.basename/extension" on RISC OS, etc.
// and the pointer must remain valid forever!
extern void parse_source_code_file(FILE *fd, const char *eternal_plat_filename);

// read optional info about parameter length
extern bits parser_get_force_bit(void);

// return current shortcut state
extern enum shortcut parser_get_shortcut(void);

// start or end processing a !break/!continue/!return keyword
extern void parser_set_shortcut(enum shortcut);

// return current state and set new state of "allow !break and !continue" flag
extern boolean parser_allow_break_cont(boolean new_state);

// return current state and set new state of "allow !return" flag
extern boolean parser_allow_return(boolean new_state);

// generate a debug/info/warning/error message
// if the "optional alternative location" given is NULL, the current location is used
extern void throw_message(enum debuglevel level, const char msg[], struct location *opt_alt_loc);

// output a warning (something looks wrong)
extern void throw_warning(const char msg[]);

// output an error (something is wrong, no output file will be generated).
// the assembler will try to go on with the assembly, so the user gets to know
// about more than one of their typos at a time.
extern void throw_error(const char msg[]);

// output a serious error (assembly stops, for example if outbuffer overruns).
extern void throw_serious_error(const char msg[]);

// output a warning, but only if we are in first pass, otherwise suppress
// (only use this if using throw_finalpass_warning is impossible,
// for example in cases where the code is skipped in later passes)
extern void throw_pass1_warning(const char msg[]);

// output a warning, but only if we are in final pass, otherwise suppress
// (for example "converted float to int for XOR" -> must be done in
// final pass because values might be undefined earlier!)
extern void throw_finalpass_warning(const char msg[]);

// throw "macro twice"/"symbol twice" error
// first output error as "error", then location of initial definition as "info"
extern void throw_redef_error(const char error_msg[], struct location *old_def, const char info_msg[]);

// ugly kluge, switches throw_error() from cli args mode to normal mode
extern void throw__done_with_cli_args(void);

// handle bugs
extern void BUG(const char *msg, int code);

// process error that might vanish if symbols change:
// if current pass is an "error output" pass, actually throw error.
// otherwise just increment counter to let mainloop know this pass wasn't successful.
extern void countorthrow_value_error(const char *msg);

// insert object (in case of list, will iterate/recurse until done)
struct iter_context {
	void		(*fn)(intval_t);	// output function
	boolean		accept_long_strings;	// if FALSE, only 1-char-strings work
	unsigned char	stringxor;		// for !scrxor, 0 otherwise
};
extern void output_object(struct object *object, struct iter_context *iter);

// output 8-bit value with range check
extern void output_8(intval_t value);

// output 16-bit value with range check big-endian
extern void output_be16(intval_t value);

// output 16-bit value with range check little-endian
extern void output_le16(intval_t value);

// output 24-bit value with range check big-endian
extern void output_be24(intval_t value);

// output 24-bit value with range check little-endian
extern void output_le24(intval_t value);

// output 32-bit value (without range check) big-endian
extern void output_be32(intval_t value);

// output 32-bit value (without range check) little-endian
extern void output_le32(intval_t value);


// string to show if outputformat_set() returns nonzero
extern const char	outputformat_names[];

// convert output format name held in DynaBuf to enum.
// returns OUTFILE_FORMAT_UNSPECIFIED on error.
extern enum outfile_format outputformat_find(void);


#endif
