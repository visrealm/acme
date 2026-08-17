// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2025 Marco Baye
// Have a look at "acme.c" for further info
//
// Flow control stuff (loops, conditional assembly etc.)
//
// Macros, conditional assembly, loops and sourcefile-includes are all based on
// parsing blocks of code. When defining macros or using loops or conditional
// assembly, the block starts with "{" and ends with "}". In the case of
// "!source", the given file is treated like a block - the outermost assembler
// function uses the same technique to parse the top level file.
//
// 24 Nov 2007	Added "!ifndef"
#include "flow.h"
#include <string.h>
#include "alu.h"
#include "config.h"
#include "dynabuf.h"
#include "global.h"
#include "input.h"
#include "mnemo.h"
#include "symbol.h"
#include "tree.h"


// helper functions for if/ifdef/ifndef/else/for/do/while


// parse symbol name and return if symbol has defined value (called by ifdef/ifndef)
boolean check_ifdef_condition(void)
{
	scope_t		scope;
	struct rwnode	*node;
	struct symbol	*symbol;

	// read symbol name
	if (input_read_scope_and_symbol_name(&scope))	// skips spaces before
		return FALSE;	// there was an error, it has been reported, so return value is more or less meaningless anway

	// look for it
	tree_hard_scan(&node, symbols_forest, scope, FALSE);
	if (!node)
		return FALSE;	// not found -> no, not defined

	symbol = (struct symbol *) node->body;
	symbol->has_been_read = TRUE;	// we did not really read the symbol's value, but checking for its existence still counts as "used it"
	if (symbol->object.type == NULL)
		BUG("ObjectHasNullType", 0);
	return symbol->object.type->is_defined(&symbol->object);
}


// parse a loop body (TODO - also use for macro body?)
static void parse_ram_block(struct block *block)
{
	// set line number to loop start
	// set RAM read pointer to loop
	inputchange_set_ram(block->line_number, block->body);
	// parse block
	parse_until_eob_or_eof();
	if (GotByte != CHAR_EOB)
		BUG("IllegalBlockTerminator", GotByte);
}


// function for "!for" with counter variable
static void counting_for(struct for_loop *loop)
{
	struct object	loop_var;
	enum shortcut	shortcut;

	// init counter
	loop_var.type = &type_number;
	loop_var.u.number.ntype = NUMTYPE_INT;
	loop_var.u.number.flags = 0;
	loop_var.u.number.val.intval = 0;	// SEE BELOW - default value if old algo skips loop entirely
	loop_var.u.number.addr_refs = loop->u.counter.addr_refs;
	// CAUTION: next line does not have power to change symbol type, but if
	// "symbol already defined" error is thrown, the type will still have
	// been changed. this was done so the code below has a counter var.
	symbol_set_object(loop->symbol, &loop_var, POWER_CHANGE_VALUE);
	// TODO: in versions before 0.97, force bit handling was broken
	// in both "!set" and "!for":
	// trying to change a force bit correctly raised an error, but
	// in any case, ALL FORCE BITS WERE CLEARED in symbol. only
	// cases like !set N=N+1 worked, because the force bit was
	// taken from result.
	// maybe support this behaviour via --dialect?
	if (loop->u.counter.force_bit)
		symbol_set_force_bit(loop->symbol, loop->u.counter.force_bit);
	loop_var = loop->symbol->object;	// update local copy with force bit
	loop->symbol->has_been_read = TRUE;	// lock force bit
	loop_var.u.number.val.intval = loop->u.counter.first;	// SEE ABOVE - this may be nonzero, but has not yet been copied to user symbol!
	while (loop->iterations_left) {
		loop->symbol->object = loop_var;	// overwrite whole struct, in case some joker has re-assigned loop counter var
		parse_ram_block(&loop->block);
		loop_var.u.number.val.intval += loop->u.counter.increment;
		loop->iterations_left--;

		// was there a "!break" or "!continue"?
		shortcut = parser_get_shortcut();
		if (shortcut == SHORTCUT_BREAK) {
			parser_set_shortcut(SHORTCUT_NONE);
			break;
		} else if (shortcut == SHORTCUT_CONT) {
			parser_set_shortcut(SHORTCUT_NONE);
		}
	}
	// new algo wants illegal value in loop counter after block:
	if (loop->algorithm == FORALGO_NEWCOUNT)
		loop->symbol->object = loop_var;	// overwrite whole struct, in case some joker has re-assigned loop counter var
}

// function for "!for" with iterating variable
static void iterating_for(struct for_loop *loop)
{
	intval_t	index	= 0;
	struct object	obj;
	enum shortcut	shortcut;

	while (loop->iterations_left) {
		loop->u.iter.obj.type->at(&loop->u.iter.obj, &obj, index++);
		symbol_set_object(loop->symbol, &obj, POWER_CHANGE_VALUE | POWER_CHANGE_OBJTYPE);
		parse_ram_block(&loop->block);
		loop->iterations_left--;

		// was there a "!break" or "!continue"?
		shortcut = parser_get_shortcut();
		if (shortcut == SHORTCUT_BREAK) {
			parser_set_shortcut(SHORTCUT_NONE);
			break;
		} else if (shortcut == SHORTCUT_CONT) {
			parser_set_shortcut(SHORTCUT_NONE);
		}
	}
}


// back end function for "!for" pseudo opcode, called with GotByte = '}'
void flow_forloop(struct for_loop *loop)
{
	struct inputchange_buf	icb;
	boolean			break_cont_buf;

	// remember input and set up new one:
	inputchange_new_ram(&icb);
	// fix line number (not for block, but in case symbol handling throws errors)
	inputchange_set_ram(loop->block.line_number, NULL);
	// remember break/cont state and allow:
	break_cont_buf = parser_allow_break_cont(TRUE);

	switch (loop->algorithm) {
	case FORALGO_OLDCOUNT:
	case FORALGO_NEWCOUNT:
		counting_for(loop);
		break;
	case FORALGO_ITERATE:
		iterating_for(loop);
		break;
	default:
		BUG("IllegalLoopAlgo", loop->algorithm);
	}

	// restore outer break/cont state:
	parser_allow_break_cont(break_cont_buf);
	// restore outer input
	inputchange_back(&icb);
}


// read condition, make copy, link to struct
// FIXME - remove!
static void copy_condition(struct condition *condition, char terminator)
{
	input_read_statement(terminator);
	condition->block.body = dynabuf_get_copy(GlobalDynaBuf);
}

// try to read a condition into DynaBuf and store pointer to copy in
// given loop_condition structure.
// if no condition given, NULL is written to structure.
// call with GotByte = first interesting character
void flow_store_doloop_condition(struct condition *condition, char terminator)
{
	struct location	loc;

	// write line number
	input_get_location(&loc);	// FIXME - get rid of this when changing copy_condition to input_line_getcopy!
	condition->block.line_number = loc.line_number;
	// set defaults
	condition->invert = FALSE;
	condition->block.body = NULL;
	// check for empty condition
	if (GotByte == terminator)
		return;

	// seems as if there really *is* a condition, so check for until/while
	if (parser_read_and_lower_keyword()) {
		if (strcmp(GlobalDynaBuf->buffer, "while") == 0) {
			//condition.invert = FALSE;
		} else if (strcmp(GlobalDynaBuf->buffer, "until") == 0) {
			condition->invert = TRUE;
		} else {
			throw_error("Expected WHILE or UNTIL keyword, or an empty loop condition.");
			return;
		}
		// write given condition into buffer
		copy_condition(condition, terminator);
	}
}


// read a condition into DynaBuf and store pointer to copy in
// given loop_condition structure.
// call with GotByte = first interesting character
void flow_store_while_condition(struct condition *condition)
{
	struct location	loc;

	input_get_location(&loc);	// FIXME - get rid of this when changing copy_condition to input_line_getcopy!
	condition->block.line_number = loc.line_number;
	condition->invert = FALSE;
	copy_condition(condition, CHAR_SOB);
}


// check a condition expression
static boolean check_condition(struct condition *condition)
{
	struct number	intresult;

	// first, check whether there actually *is* a condition
	if (condition->block.body == NULL)
		return TRUE;	// non-existing conditions are always true

	// set up input for expression evaluation
	inputchange_set_ram(condition->block.line_number, condition->block.body);	// FIXME - just pass condition->block!)

	GetByte();	// proceed with next char
	ALU_defined_int(&intresult);
	if (GotByte)
		throw_serious_error("Unexpected char when evaluating loop condition.");	// FIXME - include that char in the error message!
	return condition->invert ? !intresult.val.intval : !!intresult.val.intval;
}


// back end function for "!do" and "!while" pseudo opcodes
void flow_do_while(struct do_while *loop)
{
	struct inputchange_buf	icb;
	boolean			break_cont_buf;
	enum shortcut		shortcut;

	// remember input and prepare new one:
	inputchange_new_ram(&icb);
	// remember break/cont state and allow:
	break_cont_buf = parser_allow_break_cont(TRUE);

	for (;;) {
		// check head condition
		if (!check_condition(&loop->head_cond))
			break;

		parse_ram_block(&loop->block);

		// was there a "!break" or "!continue"?
		shortcut = parser_get_shortcut();
		if (shortcut == SHORTCUT_BREAK) {
			parser_set_shortcut(SHORTCUT_NONE);
			break;
		} else if (shortcut == SHORTCUT_CONT) {
			parser_set_shortcut(SHORTCUT_NONE);
		}

		// check tail condition
		if (!check_condition(&loop->tail_cond))
			break;
	}

	// restore outer break/cont state:
	parser_allow_break_cont(break_cont_buf);
	// restore outer input
	inputchange_back(&icb);
}
