// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2025 Marco Baye
// Have a look at "acme.c" for further info
//
// Macro stuff
#include "macro.h"
#include <string.h>	// needs strlen() + memcpy()
#include "config.h"
#include "platform.h"
#include "alu.h"
#include "dynabuf.h"
#include "global.h"
#include "input.h"
#include "section.h"
#include "symbol.h"
#include "tree.h"


// Constants
#define NAME_INITIALSIZE	128
#define ARG_SEPARATOR	' '	// separates macro title from arg types
#define ARGTYPE_VALUE	'v'
#define ARGTYPE_REF	'r'
#define REFERENCE_CHAR	'~'	// prefix for call-by-reference
#define HALF_INITIAL_ARG_TABLE_SIZE	4


// macro struct type definition
struct macro {
	struct location	definition;	// for "macro twice" error
	char		*original_name,	// as section title in error msgs
			*parameter_list;	// parameters (whole line)
	struct block	body;	// RAM block containing macro body
};
// there's no need to make this a struct and add a type component:
// when the macro has been found, accessing its parameter_list component
// gives us the possibility to find out which args are call-by-value and
// which ones are call-by-reference.
union macro_arg_t {
	struct object	result;	// value and flags (call by value)
	struct symbol	*symbol;	// pointer to symbol struct (call by reference)
};


// Variables
static	STRUCT_DYNABUF_REF(user_macro_name, NAME_INITIALSIZE);	// original macro title
static	STRUCT_DYNABUF_REF(internal_name, NAME_INITIALSIZE);	// plus param type chars
static struct rwnode	*macro_forest[256];	// trees (because of 8b hash)
// Dynamic argument table
static union macro_arg_t	*arg_table	= NULL;
static int			argtable_size	= HALF_INITIAL_ARG_TABLE_SIZE;


// Functions

// Enlarge the argument table
static void enlarge_arg_table(void)
{
	argtable_size *= 2;
	//printf("Doubling arg table size to %d.\n", argtable_size);
	arg_table = realloc(arg_table, argtable_size * sizeof(*arg_table));
	if (arg_table == NULL)
		throw_serious_error(exception_no_memory_left);
}

// Read macro scope and title. Title is read to GlobalDynaBuf and then copied
// over to internal_name DynaBuf, where ARG_SEPARATOR is added.
// In user_macro_name DynaBuf, the original name is kept so a copy can be
// linked to the resulting macro struct.
static scope_t get_scope_and_title(void)
{
	scope_t	macro_scope;

	input_read_scope_and_symbol_name(&macro_scope);	// skips spaces before
	// now GotByte = illegal character after title
	// copy macro title to private dynabuf and add separator character
	dynabuf_clear(user_macro_name);
	dynabuf_add_string(user_macro_name, GLOBALDYNABUF_CURRENT);
	dynabuf_append(user_macro_name, '\0');	// make sure terminator is part of buffer so get_copy() includes it!
	dynabuf_clear(internal_name);
	dynabuf_add_string(internal_name, GLOBALDYNABUF_CURRENT);
	dynabuf_append(internal_name, ARG_SEPARATOR);
	SKIPSPACE();	// done here once so it's not necessary at two callers
	return macro_scope;
}

// Check for comma. If there, append to GlobalDynaBuf.
static int pipe_comma(void)
{
	int	result;

	result = parser_accept_comma();
	if (result)
		DYNABUF_APPEND(GlobalDynaBuf, ',');
	return result;
}

// This function is called from both macro definition and macro call.
// Terminate macro name and copy from internal_name to GlobalDynaBuf
// (because that's where tree_hard_scan() looks for the search string).
// Then try to find macro and return whether it was created.
static int search_for_macro(struct rwnode **result, scope_t scope, int create)
{
	dynabuf_append(internal_name, '\0');	// terminate macro name
	// now internal_name = macro_title SPC argument_specifiers NUL
	dynabuf_clear(GlobalDynaBuf);
	dynabuf_add_string(GlobalDynaBuf, internal_name->buffer);
	dynabuf_append(GlobalDynaBuf, '\0');
	return tree_hard_scan(result, macro_forest, scope, create);
}

// This function is only called during the first pass, so there's no need to
// check whether to skip the definition or not.
// Return with GotByte = '}'
void macro_parse_definition(void)	// Now GotByte = illegal char after "!macro"
{
	char		*formal_parameters;
	struct rwnode	*macro_node;
	struct macro	*original_macro;	// for "macro twice" error
	struct macro	*new_macro;
	scope_t		macro_scope	= get_scope_and_title();

	// now GotByte = first non-space after title
	dynabuf_clear(GlobalDynaBuf);	// prepare to hold formal parameters
	// GlobalDynaBuf = "" (will hold formal parameter list)
	// user_macro_name = MacroTitle NUL
	// internal_name = MacroTitle ARG_SEPARATOR (grows to signature)
	// Accept n>=0 comma-separated formal parameters before CHAR_SOB ('{').
	// Valid argument formats are:
	// .LOCAL_LABEL_BY_VALUE
	// ~.LOCAL_LABEL_BY_REFERENCE
	// @CHEAP_LOCAL_LABEL_BY_VALUE
	// ~@CHEAP_LOCAL_LABEL_BY_REFERENCE
	// GLOBAL_LABEL_BY_VALUE	global args are very uncommon,
	// ~GLOBAL_LABEL_BY_REFERENCE	but not forbidden
	// now GotByte = non-space
	if (GotByte != CHAR_SOB) {	// any at all?
		do {
			// handle call-by-reference character ('~')
			if (GotByte != REFERENCE_CHAR) {
				dynabuf_append(internal_name, ARGTYPE_VALUE);
			} else {
				dynabuf_append(internal_name, ARGTYPE_REF);
				dynabuf_append(GlobalDynaBuf, REFERENCE_CHAR);
				GetByte();
			}
			// handle symbol name (including '.'/'@' prefix)
			input_append_symbol_name_to_global_dynabuf();
		} while (pipe_comma());
		// ensure CHAR_SOB ('{')
		if (GotByte != CHAR_SOB)
			throw_serious_error(exception_no_left_brace);
	}
	dynabuf_append(GlobalDynaBuf, CHAR_EOS);	// terminate param list
	// now GlobalDynaBuf = comma-separated parameter list without spaces,
	// but terminated with CHAR_EOS.
	formal_parameters = dynabuf_get_copy(GlobalDynaBuf);
	// now GlobalDynaBuf = unused
	// Reading the macro body would change the line number. To have correct
	// error messages, we're checking for "macro twice" *now*.
	// Search for macro. Create if not found.
	// But if found, complain (macro twice).
	if (search_for_macro(&macro_node, macro_scope, TRUE) == FALSE) {
		original_macro = macro_node->body;
		throw_redef_error("Macro already defined.", &(original_macro->definition), "Initial definition.");
	}
	// Create new macro struct and set it up. Finally we'll read the body.
	new_macro = safe_malloc(sizeof(*new_macro));
	new_macro->original_name = dynabuf_get_copy(user_macro_name);
	new_macro->parameter_list = formal_parameters;
	input_get_location(&new_macro->definition);	// includes line number
	input_block_getcopy(&new_macro->body);	// also includes line number (and then changes it)
	macro_node->body = new_macro;	// link macro struct to tree node
	// and that about sums it up
}

// Parse macro call ("+MACROTITLE"). Has to be re-entrant.
// TODO: split this into smaller functions, some of the inner blocks are much too long
void macro_parse_call(void)	// Now GotByte = first char of macro name
{
	struct symbol	*symbol;
	struct section	new_section,
			*outer_section;
	struct inputchange_buf	icb;
	struct macro	*actual_macro;
	struct rwnode	*macro_node,
			*symbol_node;
	scope_t		macro_scope,
			symbol_scope;
	int		arg_count	= 0;
	int		outer_msg_sum;
	boolean		break_cont_allowed;
	boolean		return_allowed;

	// make sure arg_table is ready (if not yet initialised, do it now)
	if (arg_table == NULL)
		enlarge_arg_table();

	// Enter deeper nesting level
	// Quit program if recursion too deep.
	if (--sanity.macro_recursions_left < 0)
		throw_serious_error("Too deeply nested. Recursive macro calls?");
	macro_scope = get_scope_and_title();
	// now GotByte = first non-space after title
	// internal_name = MacroTitle ARG_SEPARATOR (grows to signature)
	// Accept n>=0 comma-separated arguments before CHAR_EOS.
	// Valid argument formats are:
	//	~SYMBOL		call by ref
	//	EXPRESSION	call by value (everything that does NOT start with '~')
	// now GotByte = non-space
	if (GotByte != CHAR_EOS) {	// any at all?
		do {
			// if arg table cannot take another element, enlarge
			if (argtable_size <= arg_count)
				enlarge_arg_table();
			// Decide whether call-by-reference or call-by-value
			// In both cases, GlobalDynaBuf may be used.
			if (GotByte == REFERENCE_CHAR) {
				// read call-by-reference arg
				dynabuf_append(internal_name, ARGTYPE_REF);
				GetByte();	// eat '~'
				input_read_scope_and_symbol_name(&symbol_scope);
				// GotByte = illegal char
				arg_table[arg_count].symbol = symbol_find(symbol_scope);	// CAUTION, object type may be NULL!
			} else {
				// read call-by-value arg
				dynabuf_append(internal_name, ARGTYPE_VALUE);
				ALU_any_result(&(arg_table[arg_count].result));
			}
			++arg_count;
		} while (parser_accept_comma());
	}
	// now arg_table contains the arguments
	// now GlobalDynaBuf = unused
	// check for "unknown macro"
	// Search for macro. Do not create if not found.
	search_for_macro(&macro_node, macro_scope, FALSE);
	if (macro_node == NULL) {
		throw_error("Macro not defined (or wrong signature).");
		parser_skip_remainder();
	} else {
		// make macro_node point to the macro struct
		actual_macro = macro_node->body;

		// remember input and set up new one:
		inputchange_new_ram(&icb);
		inputchange_macro1_params(&actual_macro->definition, actual_macro->parameter_list);

		outer_msg_sum = pass.counters.warnings + pass.counters.errors;	// remember for call stack decision

		// remember old section
		outer_section = section_now;
		// start new section (with new scope)
		// FALSE = title mustn't be freed
		section_new(&new_section, "Macro", actual_macro->original_name, FALSE);
		section_new_cheap_scope(&new_section);

		// assign arguments
		GetByte();	// fetch first byte of parameter list
		if (GotByte != CHAR_EOS) {	// any at all?
			arg_count = 0;
			do {
				// Decide whether call-by-reference
				// or call-by-value
				// In both cases, GlobalDynaBuf may be used.
				if (GotByte == REFERENCE_CHAR) {
					// assign call-by-reference arg
					GetByte();	// eat '~'
					input_read_scope_and_symbol_name(&symbol_scope);
					// create new tree node and link existing symbol struct from arg list to it
					if (tree_hard_scan(&symbol_node, symbols_forest, symbol_scope, TRUE) == FALSE) {
						// we expect it to exist in later passes, but in pass 1 it's an error:
						if (pass.number == 1)
							throw_error("Macro parameter twice.");
					}
					symbol_node->body = arg_table[arg_count].symbol;	// CAUTION, object type may be NULL
				} else {
					// assign call-by-value arg
					input_read_scope_and_symbol_name(&symbol_scope);
					symbol = symbol_find(symbol_scope);
// FIXME - find out if symbol was just created.
// Then check for the same error message here as above ("Macro parameter twice.").
// TODO - on the other hand, this would rule out globals as args (stupid anyway, but not illegal yet!)
					symbol->object = arg_table[arg_count].result;	// FIXME - this assignment redefines globals/whatever without throwing errors!
				}
				++arg_count;
			} while (parser_accept_comma());
		}

		// remember whether break/continue/return are allowed and set new states
		break_cont_allowed = parser_allow_break_cont(FALSE);	// forbid !break/!continue
		return_allowed = parser_allow_return(TRUE);	// allow !return

		// and now, finally, parse the actual macro body
// maybe call parse_ram_block(actual_macro->definition.line_number, actual_macro->body)
		inputchange_macro2_body(actual_macro->body.body);
		parse_until_eob_or_eof();
		if (GotByte != CHAR_EOB)
			BUG("IllegalBlockTerminator", GotByte);
		// was there a "!return"?
		if (parser_get_shortcut() == SHORTCUT_RETURN)
			parser_set_shortcut(SHORTCUT_NONE);

		// restore states of break/continue/return
		parser_allow_return(return_allowed);
		parser_allow_break_cont(break_cont_allowed);

		// end section (free title memory, if needed)
		section_finalize(&new_section);
		// restore previous section
		section_now = outer_section;

		// restore outer input
		inputchange_back(&icb);

		// if needed, dump call stack
		if (outer_msg_sum != pass.counters.warnings + pass.counters.errors)
			throw_message(DEBUGLEVEL_INFO, "...called from here.", NULL);

		parser_ensure_EOS();
	}
	++sanity.macro_recursions_left;	// leave this nesting level
}
