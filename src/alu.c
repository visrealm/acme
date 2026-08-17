// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2025 Marco Baye
// Have a look at "acme.c" for further info
//
// Arithmetic/logic unit
// 11 Oct 2006	Improved float reading in parse_number_literal()
// 24 Nov 2007	Now accepts floats starting with decimal point
// 31 Jul 2009	Changed ASR again, just to be on the safe side.
// 14 Jan 2014	Changed associativity of "power-of" operator,
//		so a^b^c now means a^(b^c).
//  7 May 2014	C-style "==" operators are now recognized.
// 31 May 2014	Added "0b" binary number prefix as alternative to "%".
// 28 Apr 2015	Added symbol name output to "value not defined" error.
//  1 Feb 2019	Prepared to make "honor leading zeroes" optionally (now done)

// the words "operand"/"operator"/"operation" are too similar, so:
//	"op" means operator/operation
//	"arg" means argument (used instead of "operand")

#include "alu.h"
#include <stdlib.h>
#include <math.h>	// only for fp support
#include <string.h>	// for memcpy()
#include "platform.h"
#include "dynabuf.h"
#include "encoding.h"
#include "global.h"
#include "input.h"
#include "output.h"
#include "section.h"
#include "symbol.h"
#include "tree.h"


// constants

#define ERRORMSG_INITIALSIZE	256	// ad hoc
#define FUNCTION_INITIALSIZE	16	// enough for "is_string"
#define HALF_INITIAL_STACK_SIZE	8
static const char	exception_div_by_zero[]	= "Division by zero.";
static const char	exception_no_value[]	= "No value given.";
static const char	exception_paren_open[]	= "Too many '('.";
static const char	exception_not_number[]	= "Expression did not return a number.";
static const char	exception_float_to_int[]= "Converted to integer for binary logic operator.";
static const char	exception_lengthnot1[]	= "String length is not 1.";

enum op_group {
	OPGROUP_SPECIAL,	// start/end of expression, and parentheses
	OPGROUP_MONADIC,	// {result} = {op} {arg}
	OPGROUP_DYADIC		// {result} = {arg1} {op} {arg2}
};
enum op_id {
	// special (pseudo) operators:
	OPID_TERMINATOR,	//		(preliminary) end of expression (quasi-dyadic)
	OPID_START_EXPRESSION,	//		start of expression
	OPID_SUBEXPR_PAREN,	//	(v	'(' starts subexpression (quasi-monadic)
	OPID_START_LIST,	//	[1,2]	'[' starts non-empty list literal (quasi-monadic, followed by dyadic OPID_LIST_APPEND)
	OPID_SUBEXPR_BRACKET,	//	v[	'[' starts subexpression (quasi-monadic, after dyadic OPID_ATINDEX)
	// monadic operators (including functions):
	OPID_NOT,		//	!v	NOT v		bit-wise NOT
	OPID_NEGATE,		//	-v			negation
	OPID_LOWBYTEOF,		//	<v			low byte of
	OPID_HIGHBYTEOF,	//	>v			high byte of
	OPID_BANKBYTEOF,	//	^v			bank byte of
	OPID_ADDRESS,		//	addr(v)				FIXME - add nonaddr()?
	OPID_INT,		//	int(v)
	OPID_FLOAT,		//	float(v)
	OPID_SIN,		//	sin(v)
	OPID_COS,		//	cos(v)
	OPID_TAN,		//	tan(v)
	OPID_ARCSIN,		//	arcsin(v)
	OPID_ARCCOS,		//	arccos(v)
	OPID_ARCTAN,		//	arctan(v)
	OPID_LEN,		//	len(v)
	OPID_ISNUMBER,		//	is_number(v)
	OPID_ISLIST,		//	is_list(v)
	OPID_ISSTRING,		//	is_string(v)
	OPID_DEC,		//	dec(v)
	OPID_HEX,		//	hex(v)
// add CHR function to create 1-byte string? or rather add \xAB escape sequence?
	// dyadic operators:
	OPID_POWEROF,		//	v^w
	OPID_MULTIPLY,		//	v*w
	OPID_DIVIDE,		//	v/w				division
	OPID_INTDIV,		//	v/w	v DIV w			integer division
	OPID_MODULO,		//	v%w	v MOD w			remainder
	OPID_SHIFTLEFT,		//	v<<w	v ASL w	v LSL w		shift left
	OPID_ASR,		//	v>>w	v ASR w			arithmetic shift right
	OPID_LSR,		//	v>>>w	v LSR w			logical shift right
	OPID_ADD,		//	v+w
	OPID_SUBTRACT,		//	v-w
	OPID_EQUALS,		//	v=w
	OPID_LESSOREQUAL,	//	v<=w
	OPID_LESSTHAN,		//	v< w
	OPID_GREATEROREQUAL,	//	v>=w
	OPID_GREATERTHAN,	//	v> w
	OPID_NOTEQUAL,		//	v!=w	v<>w	v><w
	OPID_AND,		//	v&w		v AND w
	OPID_OR,		//	v|w		v OR w
	OPID_EOR,		//	v EOR w		v XOR w		deprecated!
	OPID_XOR,		//	v XOR w
	OPID_LIST_APPEND,	//			used internally when building list literal
	OPID_ATINDEX,		//	v[w]
};
struct op {
	int		priority;
	enum op_group	group;
	enum op_id	id;
	const char	*text_version;
};
static struct op ops_terminating_char	= {0, OPGROUP_SPECIAL,	OPID_TERMINATOR,	"end of expression"	};
static struct op ops_start_expression	= {2, OPGROUP_SPECIAL,	OPID_START_EXPRESSION,	"start of expression"	};
static struct op ops_subexpr_paren	= {4, OPGROUP_SPECIAL,	OPID_SUBEXPR_PAREN,	"left parenthesis"	};
static struct op ops_start_list		= {6, OPGROUP_SPECIAL,	OPID_START_LIST,	"start list"	};
static struct op ops_subexpr_bracket	= {8, OPGROUP_SPECIAL,	OPID_SUBEXPR_BRACKET,	"open index"	};
static struct op ops_list_append	= {14, OPGROUP_DYADIC,	OPID_LIST_APPEND,	"append to list"	};
static struct op ops_or			= {16, OPGROUP_DYADIC,	OPID_OR,	"logical or"	};
static struct op ops_eor		= {18, OPGROUP_DYADIC,	OPID_EOR,	"exclusive or"	};	// FIXME - remove
static struct op ops_xor		= {18, OPGROUP_DYADIC,	OPID_XOR,	"exclusive or"	};
static struct op ops_and		= {20, OPGROUP_DYADIC,	OPID_AND,	"logical and"	};
static struct op ops_equals		= {22, OPGROUP_DYADIC,	OPID_EQUALS,		"test for equality"	};
static struct op ops_not_equal		= {24, OPGROUP_DYADIC,	OPID_NOTEQUAL,		"test for inequality"	};
	// same priority for all comparison operators
static struct op ops_less_or_equal	= {26, OPGROUP_DYADIC,	OPID_LESSOREQUAL,	"less than or equal"	};
static struct op ops_less_than		= {26, OPGROUP_DYADIC,	OPID_LESSTHAN,		"less than"	};
static struct op ops_greater_or_equal	= {26, OPGROUP_DYADIC,	OPID_GREATEROREQUAL,	"greater than or equal"	};
static struct op ops_greater_than	= {26, OPGROUP_DYADIC,	OPID_GREATERTHAN,	"greater than"	};
	// same priority for all byte extraction operators
static struct op ops_low_byte_of	= {28, OPGROUP_MONADIC,	OPID_LOWBYTEOF,		"low byte of"	};
static struct op ops_high_byte_of	= {28, OPGROUP_MONADIC,	OPID_HIGHBYTEOF,	"high byte of"	};
static struct op ops_bank_byte_of	= {28, OPGROUP_MONADIC,	OPID_BANKBYTEOF,	"bank byte of"	};
	// same priority for all shift operators (left-associative, though they could be argued to be made right-associative :))
static struct op ops_shift_left		= {30, OPGROUP_DYADIC,	OPID_SHIFTLEFT,	"shift left"	};
static struct op ops_asr		= {30, OPGROUP_DYADIC,	OPID_ASR,	"arithmetic shift right"	};
static struct op ops_lsr		= {30, OPGROUP_DYADIC,	OPID_LSR,	"logical shift right"	};
	// same priority for "+" and "-"
static struct op ops_add		= {32, OPGROUP_DYADIC,	OPID_ADD,	"addition"	};
static struct op ops_subtract		= {32, OPGROUP_DYADIC,	OPID_SUBTRACT,	"subtraction"	};
	// same priority for "*", "/" and "%"
static struct op ops_multiply		= {34, OPGROUP_DYADIC,	OPID_MULTIPLY,	"multiplication"	};
static struct op ops_divide		= {34, OPGROUP_DYADIC,	OPID_DIVIDE,	"division"	};
static struct op ops_intdiv		= {34, OPGROUP_DYADIC,	OPID_INTDIV,	"integer division"	};
static struct op ops_modulo		= {34, OPGROUP_DYADIC,	OPID_MODULO,	"modulo"	};
	// highest "real" priorities
static struct op ops_negate		= {36, OPGROUP_MONADIC,	OPID_NEGATE,	"negation"	};
#define PRIO_POWEROF			37	// the single right-associative operator, so this gets checked explicitly
static struct op ops_powerof		= {PRIO_POWEROF, OPGROUP_DYADIC,	OPID_POWEROF,	"power of"	};
static struct op ops_not		= {38, OPGROUP_MONADIC,	OPID_NOT,	"logical not"	};
static struct op ops_atindex		= {40, OPGROUP_DYADIC,	OPID_ATINDEX,	"indexing"	};
	// function calls act as if they were monadic operators.
	// they need high priorities to make sure they are evaluated once the
	// parentheses' content is known:
	// "sin(3 + 4) DYADIC_OPERATOR 5" becomes "sin 7 DYADIC_OPERATOR 5",
	// so function calls' priority must be higher than all dyadic operators.
static struct op ops_addr		= {42, OPGROUP_MONADIC, OPID_ADDRESS,	"address()"	};
static struct op ops_int		= {42, OPGROUP_MONADIC, OPID_INT,	"int()"	};
static struct op ops_float		= {42, OPGROUP_MONADIC, OPID_FLOAT,	"float()"	};
static struct op ops_sin		= {42, OPGROUP_MONADIC, OPID_SIN,	"sin()"	};
static struct op ops_cos		= {42, OPGROUP_MONADIC, OPID_COS,	"cos()"	};
static struct op ops_tan		= {42, OPGROUP_MONADIC, OPID_TAN,	"tan()"	};
static struct op ops_arcsin		= {42, OPGROUP_MONADIC, OPID_ARCSIN,	"arcsin()"	};
static struct op ops_arccos		= {42, OPGROUP_MONADIC, OPID_ARCCOS,	"arccos()"	};
static struct op ops_arctan		= {42, OPGROUP_MONADIC, OPID_ARCTAN,	"arctan()"	};
static struct op ops_len		= {42, OPGROUP_MONADIC, OPID_LEN,	"len()"	};
static struct op ops_isnumber		= {42, OPGROUP_MONADIC, OPID_ISNUMBER,	"is_number()"	};
static struct op ops_islist		= {42, OPGROUP_MONADIC, OPID_ISLIST,	"is_list()"	};
static struct op ops_isstring		= {42, OPGROUP_MONADIC, OPID_ISSTRING,	"is_string()"	};
static struct op ops_dec		= {42, OPGROUP_MONADIC, OPID_DEC,	"dec()"	};
static struct op ops_hex		= {42, OPGROUP_MONADIC, OPID_HEX,	"hex()"	};


// variables
static	STRUCT_DYNABUF_REF(errormsg_dyna_buf, ERRORMSG_INITIALSIZE);	// to build variable-length error messages
static	STRUCT_DYNABUF_REF(function_dyna_buf, FUNCTION_INITIALSIZE);	// for fn names
// operator stack, current size and stack pointer:
static struct op	**op_stack	= NULL;
static int		opstack_size	= HALF_INITIAL_STACK_SIZE;
static int		op_sp;
// argument stack, current size and stack pointer:
static struct object	*arg_stack	= NULL;
static int		argstack_size	= HALF_INITIAL_STACK_SIZE;
static int		arg_sp;
enum alu_state {
	STATE_EXPECT_ARG_OR_MONADIC_OP,
	STATE_EXPECT_DYADIC_OP,
	STATE_MAX_GO_ON,	// "border value" to find the stoppers:
	STATE_ERROR,		// error has occurred
	STATE_END		// standard end
};
static enum alu_state	alu_state;	// deterministic finite automaton
// predefined stuff
static struct ronode	op_tree[]	= {
	PREDEF_START,
	PREDEFNODE("asr",	&ops_asr),
	PREDEFNODE("lsr",	&ops_lsr),
	PREDEFNODE("asl",	&ops_shift_left),
	PREDEFNODE("lsl",	&ops_shift_left),
	PREDEFNODE("div",	&ops_intdiv),
	PREDEFNODE("mod",	&ops_modulo),
	PREDEFNODE("and",	&ops_and),
	PREDEFNODE("or",	&ops_or),
	PREDEFNODE("eor",	&ops_eor),		// FIXME - remove
	PREDEF_END("xor",	&ops_xor),
	//    ^^^^ this marks the last element
};
static struct ronode	function_tree[]	= {
	PREDEF_START,
	PREDEFNODE("addr",	&ops_addr),
	PREDEFNODE("address",	&ops_addr),
	PREDEFNODE("int",	&ops_int),
	PREDEFNODE("float",	&ops_float),
	PREDEFNODE("len",	&ops_len),
	PREDEFNODE("is_number",	&ops_isnumber),
	PREDEFNODE("is_list",	&ops_islist),
	PREDEFNODE("is_string",	&ops_isstring),
	PREDEFNODE("dec",	&ops_dec),
	PREDEFNODE("hex",	&ops_hex),
	PREDEFNODE("arcsin",	&ops_arcsin),
	PREDEFNODE("arccos",	&ops_arccos),
	PREDEFNODE("arctan",	&ops_arctan),
	PREDEFNODE("sin",	&ops_sin),
	PREDEFNODE("cos",	&ops_cos),
	PREDEF_END("tan",	&ops_tan),
	//    ^^^^ this marks the last element
};

#define PUSH_OP(x)				\
do {						\
	op_stack[op_sp] = (x);			\
	if (++op_sp >= opstack_size)		\
		enlarge_operator_stack();	\
} while (0)

#define PUSH_INT_ARG(i, f, r)				\
do {							\
	arg_stack[arg_sp].type = &type_number;		\
	arg_stack[arg_sp].u.number.ntype = NUMTYPE_INT;	\
	arg_stack[arg_sp].u.number.flags = (f);		\
	arg_stack[arg_sp].u.number.val.intval = (i);	\
	arg_stack[arg_sp++].u.number.addr_refs = (r);	\
} while (0)
#define PUSH_FP_ARG(fp, f)				\
do {							\
	arg_stack[arg_sp].type = &type_number;		\
	arg_stack[arg_sp].u.number.ntype = NUMTYPE_FLOAT;\
	arg_stack[arg_sp].u.number.flags = (f);		\
	arg_stack[arg_sp].u.number.val.fpval = (fp);	\
	arg_stack[arg_sp++].u.number.addr_refs = 0;	\
} while (0)


// double the size of the operator stack
static void enlarge_operator_stack(void)
{
	opstack_size *= 2;
	//printf("Doubling op stack size to %d.\n", opstack_size);
	op_stack = realloc(op_stack, opstack_size * sizeof(*op_stack));
	if (op_stack == NULL)
		throw_serious_error(exception_no_memory_left);
}


// double the size of the argument stack
static void enlarge_argument_stack(void)
{
	argstack_size *= 2;
	//printf("Doubling arg stack size to %d.\n", argstack_size);
	arg_stack = realloc(arg_stack, argstack_size * sizeof(*arg_stack));
	if (arg_stack == NULL)
		throw_serious_error(exception_no_memory_left);
}


// not-so-braindead algorithm for calculating "to the power of" function for
// integer arguments.
// my_pow(whatever, 0) returns 1.
// my_pow(0, whatever_but_zero) returns 0.
static intval_t my_pow(intval_t mantissa, intval_t exponent)
{
	intval_t	result	= 1;

	while (exponent) {
		// handle exponent's lowmost bit
		if (exponent & 1)
			result *= mantissa;
		// square the mantissa, halve the exponent
		mantissa *= mantissa;
		exponent >>= 1;
	}
	return result;
}


// arithmetic shift right (works even if C compiler does not support it)
static intval_t my_asr(intval_t left, intval_t right)
{
	// if first argument is positive or zero, ASR and LSR are equivalent,
	// so just do it and return the result:
	if (left >= 0)
		return left >> right;

	// However, if the first argument is negative, the result is
	// implementation-defined: While most compilers will do ASR, some others
	// might do LSR instead, and *theoretically*, it is even possible for a
	// compiler to define silly stuff like "shifting a negative value to the
	// right will always return -1".
	// Therefore, in case of a negative argument, we'll use this quick and
	// simple workaround:
	return ~((~left) >> right);
}


// if wanted, throw "Value not defined" error
// This function is not allowed to change DynaBuf because the symbol's name
// might be stored there!
static void is_not_defined(struct symbol *symbol, char *name, size_t length)
{
	if (!pass.flags.complain_about_undefined)
		return;

	// only complain once per symbol
	if (symbol->has_been_reported)
		return;

	symbol->has_been_reported = TRUE;

	dynabuf_clear(errormsg_dyna_buf);
	dynabuf_add_string(errormsg_dyna_buf, "Symbol not defined (");
	length += errormsg_dyna_buf->size;

	dynabuf_add_string(errormsg_dyna_buf, name);
	if (errormsg_dyna_buf->size < length) {
		BUG("IllegalSymbolNameLength", errormsg_dyna_buf->size - length);
	} else {
		errormsg_dyna_buf->size = length;
	}
	dynabuf_add_string(errormsg_dyna_buf, ").");
	dynabuf_append(errormsg_dyna_buf, '\0');
	throw_error(errormsg_dyna_buf->buffer);
}


// Lookup (and create, if necessary) symbol tree item and return its value.
// DynaBuf holds the symbol's name and "scope" its scope.
// The name length must be given explicitly because of anonymous forward labels;
// their internal name is different (longer) than their displayed name.
//	FIXME - just split this up into two different buffers, "user name" and "internal name", that way the scope can be changed to a prefix as well!
// This function is not allowed to change DynaBuf because that's where the
// symbol name is stored!
// returns pointer to symbol because the "unpseudopc" caller needs it
static struct symbol *get_symbol_value(scope_t scope, size_t name_length, boolean check_pass_number)
{
	struct symbol	*symbol;
	struct object	*arg;

	symbol = symbol_find(scope);
	symbol->has_been_read = TRUE;
	if (symbol->object.type == NULL) {
		// finish symbol item by making it an undefined number
		symbol->object.type = &type_number;
		symbol->object.u.number.ntype = NUMTYPE_UNDEFINED;
		symbol->object.u.number.flags = NUMBER_EVER_UNDEFINED;	// reading undefined taints it
		symbol->object.u.number.addr_refs = 0;
	} else {
		// FIXME - add sanity check for UNDEFINED where EVER_UNDEFINED is false -> BUG()!
		// (because the only way to have UNDEFINED is the block above, and EVER_UNDEFINED taints everything it touches)

		// special kluge for anonymous backward labels:
		// if they are referenced before they are defined, the last
		// value of the previous pass would be used! so we check the
		// pass number and if the value is from the previous pass, we
		// delete it:
		if (check_pass_number && (symbol->pass_number != pass.number))
			symbol->object.u.number.ntype = NUMTYPE_UNDEFINED;
	}
	// first push on arg stack, so we have a local copy we can "unpseudo"
	arg = &arg_stack[arg_sp++];
	*arg = symbol->object;

	// if needed, output "value not defined" error
//	if (!(arg->type->is_defined(arg)))
// FIXME - now that lists with undefined items are "undefined", this fails in
// case of "!if len(some_list) {", so check for undefined _numbers_ explicitly:
	if ((arg->type == &type_number) && (arg->u.number.ntype == NUMTYPE_UNDEFINED))
		is_not_defined(symbol, GLOBALDYNABUF_CURRENT, name_length);
	// FIXME - if arg is list, increment ref count!
	return symbol;	// only needed by "unpseudopc" caller
}


// parse program counter ('*')
static void parse_program_counter(void)	// now GotByte = "*"
{
	struct number	pc;
	struct object	*arg;

	GetByte();
	programcounter_read_asterisk(&pc);
	// push to arg stack
	arg = &arg_stack[arg_sp++];
	arg->type = &type_number;
	arg->u.number = pc;
}


// make new string object
// (exported because symbol.c calls this for strings given on command line)
void string_prepare_string(struct object *self, int len)
{
	self->type = &type_string;
	self->u.string = safe_malloc(sizeof(*(self->u.string)) + len);
	self->u.string->payload[len] = 0;	// terminate, to facilitate string_print()
	self->u.string->length = len;	// length does not include the added terminator
	self->u.string->refs = 1;
}
// parse string or character
// characters will be converted using the current encoding, strings are kept as-is.
static void parse_quoted(char closing_quote)
{
	boolean		is_string;
	intval_t	value;

	if (input_read_string_literal(closing_quote))
		goto fail;	// unterminated or escaping error

	if (config.dialect >= V0_97__BACKSLASH_ESCAPING) {
		// since version 0.97, 'x' means character code and "x" means string:
		is_string = (closing_quote == '"');
	} else {
		// older versions did not support strings, both types of quotes were treated the same:
		is_string = FALSE;
	}

	if (is_string) {
		string_prepare_string(&arg_stack[arg_sp], GlobalDynaBuf->size);	// create string object and put on arg stack
		memcpy(arg_stack[arg_sp].u.string->payload, GLOBALDYNABUF_CURRENT, GlobalDynaBuf->size);	// copy payload
		++arg_sp;
	} else {
		// character code:

		// too short?
		if (GlobalDynaBuf->size == 0) {
			throw_error("There is no character in quotes.");
			goto fail;
		}

		// too long?
		if (GlobalDynaBuf->size != 1)
			throw_error("There's more than one character.");
		// parse character
		value = encoding_encode_char(GLOBALDYNABUF_CURRENT[0]);
		PUSH_INT_ARG(value, 0, 0);	// no flags, no addr refs
	}
	// now GotByte = char following closing quote (or CHAR_EOS on error)
	return;

fail:
	PUSH_INT_ARG(0, 0, 0);	// dummy, no flags, no addr refs
	alu_state = STATE_ERROR;
}


// Parse binary value. Apart from '0' and '1', it also accepts the characters
// '.' and '#', this is much more readable. The current value is stored as soon
// as a character is read that is none of those given above.
static void parse_binary_literal(void)	// now GotByte = "%" or "b"
{
	intval_t	value	= 0;
	bits		flags	= 0;
	int		digits	= -1;	// digit counter

	for (;;) {
		++digits;
		switch (GetByte()) {
		case '0':
		case '.':
			value <<= 1;
			continue;
		case '1':
		case '#':
			value = (value << 1) | 1;
			continue;
		}
		break;	// found illegal character
	}
	if (!digits) {
		throw_error("Binary literal without any digits.");
	}
	if (digits & config.warn_bin_mask) {
		throw_finalpass_warning("Binary literal with strange number of digits.");
	}
	// set force bits
	if (config.honor_leading_zeroes) {
		if (digits > 8) {
			if (digits > 16) {
				if (value < 65536)
					flags |= NUMBER_FORCES_24;
			} else {
				if (value < 256)
					flags |= NUMBER_FORCES_16;
			}
		}
	}
	PUSH_INT_ARG(value, flags, 0);
	// now GotByte = non-binary char
}


// Parse hexadecimal value. It accepts "0" to "9", "a" to "f" and "A" to "F".
// The current value is stored as soon as a character is read that is none of
// those given above.
static void parse_hex_literal(void)	// now GotByte = "$" or "x"
{
	char		byte;
	int		digits	= -1;	// digit counter
	bits		flags	= 0;
	intval_t	value	= 0;

	for (;;) {
		++digits;
		byte = GetByte();
		// if digit or legal character, add value
		if ((byte >= '0') && (byte <= '9')) {
			value = (value << 4) + (byte - '0');
			continue;
		}
		if ((byte >= 'a') && (byte <= 'f')) {
			value = (value << 4) + (byte - 'a') + 10;
			continue;
		}
		if ((byte >= 'A') && (byte <= 'F')) {
			value = (value << 4) + (byte - 'A') + 10;
			continue;
		}
		break;	// found illegal character
	}
	if (!digits)
		throw_error("Hex literal without any digits.");
	// set force bits
	if (config.honor_leading_zeroes) {
		if (digits > 2) {
			if (digits > 4) {
				if (value < 65536)
					flags |= NUMBER_FORCES_24;
			} else {
				if (value < 256)
					flags |= NUMBER_FORCES_16;
			}
		}
	}
	PUSH_INT_ARG(value, flags, 0);
	// Now GotByte = non-hexadecimal char
}


// parse fractional part of a floating-point value
static void parse_frac_part(int integer_part)	// now GotByte = first digit after decimal point
{
	double	denominator	= 1,
		fpval		= integer_part;

	// parse digits until no more
	while ((GotByte >= '0') && (GotByte <= '9')) {
		fpval = 10 * fpval + (GotByte & 15);	// this works. it's ASCII.
		denominator *= 10;
		GetByte();
	}
	// FIXME - add possibility to read 'e' and exponent!
	PUSH_FP_ARG(fpval / denominator, 0);
}


// Parse a decimal value. As decimal values don't use any prefixes, this
// function expects the first digit to be read already.
// If the first two digits are "0x", this function branches to the one for
// parsing hexadecimal values.
// If the first two digits are "0b", this function branches to the one for
// parsing binary values.
// If a decimal point is read, this function branches to the one for parsing
// floating-point values.
// This function accepts '0' through '9' and one dot ('.') as the decimal
// point. The current value is stored as soon as a character is read that is
// none of those given above. Float usage is only activated when a decimal
// point has been found, so don't expect "100000000000000000000" to work.
// CAUTION: "100000000000000000000.0" won't work either, because when the
// decimal point gets parsed, the integer value will have overflown already.
static void parse_number_literal(void)	// now GotByte = first digit
{
	intval_t	intval	= (GotByte & 15);	// this works. it's ASCII.

	GetByte();
	// check for "0b" (binary) and "0x" (hexadecimal) prefixes
	if (intval == 0) {
		if (GotByte == 'b') {
			parse_binary_literal();
			return;
		}
		if (GotByte == 'x') {
			parse_hex_literal();
			return;
		}
	}
	// parse digits until no more
	while ((GotByte >= '0') && (GotByte <= '9')) {
		intval = 10 * intval + (GotByte & 15);	// ASCII, see above
		GetByte();
	}
	// check whether it's a float
	if (GotByte == '.') {
		// read fractional part
		GetByte();
		parse_frac_part(intval);
	} else {
		PUSH_INT_ARG(intval, 0, 0);
	}
	// now GotByte = non-decimal char
}


// Parse octal value. It accepts "0" to "7". The current value is stored as
// soon as a character is read that is none of those given above.
static void parse_octal_literal(void)	// now GotByte = first octal digit
{
	intval_t	value	= 0;
	bits		flags	= 0;
	int		digits	= 0;	// digit counter

	while ((GotByte >= '0') && (GotByte <= '7')) {
		value = (value << 3) + (GotByte & 7);	// this works. it's ASCII.
		++digits;
		GetByte();
	}
	// set force bits
	if (config.honor_leading_zeroes) {
		if (digits > 3) {
			if (digits > 6) {
				if (value < 65536)
					flags |= NUMBER_FORCES_24;
			} else {
				if (value < 256)
					flags |= NUMBER_FORCES_16;
			}
		}
	}
	PUSH_INT_ARG(value, flags, 0);
	// Now GotByte = non-octal char
}


// parse function call (sin(), cos(), arctan(), ...)
static void parse_function_call(void)	// now GotByte = '('
{
	void	*node_body;

	// make lower case version of name in local dynamic buffer
	dynabuf_to_lower(function_dyna_buf, GlobalDynaBuf);
	// search for tree item
	if (tree_easy_scan(function_tree, &node_body, function_dyna_buf)) {
		PUSH_OP((struct op *) node_body);
	} else {
		throw_error("Unknown function.");
		alu_state = STATE_ERROR;
	}
}


// make empty list
static void list_init_list(struct object *self)
{
	self->type = &type_list;
	self->u.listhead = safe_malloc(sizeof(*(self->u.listhead)));
	self->u.listhead->next = self->u.listhead;
	self->u.listhead->prev = self->u.listhead;
	self->u.listhead->u.listinfo.length = 0;
	self->u.listhead->u.listinfo.refs = 1;
}
// extend list by appending a single object
static void list_append_object(struct listitem *head, const struct object *obj)
{
	struct listitem	*item;

	item = safe_malloc(sizeof(*item));
	item->u.payload = *obj;
	item->next = head;
	item->prev = head->prev;
	item->next->prev = item;
	item->prev->next = item;
	head->u.listinfo.length++;
}
// extend list by appending all items of a list
static void list_append_list(struct listitem *selfhead, struct listitem *otherhead)
{
	struct listitem	*item;

	if (selfhead == otherhead)
		BUG("ExtendingListWithItself", 0);
	item = otherhead->next;
	while (item != otherhead) {
		list_append_object(selfhead, &item->u.payload);
		item = item->next;
	}
}


// helper function for "monadic &" (either octal value or "unpseudo" operator)
// returns nonzero on (syntax) error
static int parse_octal_or_unpseudo(void)	// now GotByte = '&'
{
	unsigned int	unpseudo_count	= 1;
	struct pseudopc	*context;
	struct symbol	*symbol;
	scope_t		scope;

	// first count ampersand characters so we know how many layers to "unpseudo":
	while (GetByte() == '&')
		++unpseudo_count;

	// check for octal number:
	if ((unpseudo_count == 1) && (GotByte >= '0') & (GotByte <= '7')) {
		parse_octal_literal();	// now GotByte = non-octal char
		return 0;	// ok
	}

	// it's not an octal number, so it must be something to "unpseudo":
	if (GotByte == '*') {
		// program counter
		parse_program_counter();
		context = pseudopc_get_context();
	} else if (BYTE_STARTS_KEYWORD(GotByte) || (GotByte == LOCAL_PREFIX) || (GotByte == CHEAP_PREFIX)) {
		// symbol
		if (input_read_scope_and_symbol_name(&scope))	// now GotByte = illegal char
			return 1;	// error (no string given)

		symbol = get_symbol_value(scope, GlobalDynaBuf->size - 1, FALSE);	// -1 to not count terminator, no pass number check
		if (symbol->object.type != &type_number) {
			throw_error("Un-pseudopc operator '&' can only be applied to number symbols.");
			return 0;	// "ok" (because the error above does not inhibit parsing)
		}
		context = symbol->pseudopc;
//	} else if (...) {
//		// anonymous symbol
//		"unpseudo"-ing anonymous symbols is not supported
	} else {
                throw_error("Expected octal value or symbol name after '&'.");
		return 1;	// error
	}
	// now process argument on arg stack (sp has been incremented)
	pseudopc_unpseudo(&arg_stack[arg_sp - 1].u.number, context, unpseudo_count);
	return 0;	// ok
}


// helper function to tell user not to use the "EOR" operator anymore
// this gets called from two places, depending on whether the result
// would be defined or not - to make sure the user sees it.
static void eor_is_obsolete(void)
{
	if (config.dialect >= V0_98__PATHS_AND_SYMBOLCHANGE)
		throw_error("the \"EOR\" operator is obsolete; use \"XOR\" instead.");
	else
		throw_finalpass_warning("\"EOR\" is deprecated; use \"XOR\" instead.");
}


// expression parser


// handler for special operators like parentheses and start/end of expression
#define PREVIOUS_ARGUMENT	(arg_stack[arg_sp - 2])
#define NEWEST_ARGUMENT		(arg_stack[arg_sp - 1])
#define PREVIOUS_OPERATOR	(op_stack[op_sp - 2])
#define NEWEST_OPERATOR		(op_stack[op_sp - 1])
static void handle_special_operator(struct expression *expression, enum op_id previous)
{
	// when this gets called, "current" operator is OPID_TERMINATOR
	switch (previous) {
	case OPID_START_EXPRESSION:
		alu_state = STATE_END;	// we are done
		// don't touch "is_parenthesized", because start/end are obviously not "real" operators
		// not really needed, but there are sanity checks for stack pointers:
		// remove previous operator by overwriting with newest one...
		PREVIOUS_OPERATOR = NEWEST_OPERATOR;
		--op_sp;	// ...and shrinking operator stack
		break;
	case OPID_SUBEXPR_PAREN:
		expression->is_parenthesized = TRUE;	// found parentheses. if this is not the outermost level, the outermost level will fix this flag later on.
		if (GotByte == ')') {
			// matching parenthesis
			GetByte();	// eat ')'
			op_sp -= 2;	// remove both SUBEXPR_PAREN and TERMINATOR
			alu_state = STATE_EXPECT_DYADIC_OP;
		} else {
			// unmatched parenthesis, as in "lda ($80,x)"
			++(expression->open_parentheses);	// count
			// remove previous operator by overwriting with newest one...
			PREVIOUS_OPERATOR = NEWEST_OPERATOR;
			--op_sp;	// ...and shrinking operator stack
		}
		break;
	case OPID_START_LIST:
		if (GotByte == ',') {
			GetByte();	// eat ','
			NEWEST_OPERATOR = &ops_list_append;	// change "end of expression" to "append"
		} else if (GotByte == ']') {
			GetByte();	// eat ']'
			op_sp -= 2;	// remove both START_LIST and TERMINATOR
			alu_state = STATE_EXPECT_DYADIC_OP;
		} else {
			// unmatched bracket
			throw_error("Unterminated list.");
			alu_state = STATE_ERROR;
			// remove previous operator by overwriting with newest one...
			PREVIOUS_OPERATOR = NEWEST_OPERATOR;
			--op_sp;	// ...and shrinking operator stack
		}
		break;
	case OPID_SUBEXPR_BRACKET:
		if (GotByte == ']') {
			GetByte();	// eat ']'
			op_sp -= 2;	// remove both SUBEXPR_BRACKET and TERMINATOR
			alu_state = STATE_EXPECT_DYADIC_OP;
		} else {
			// unmatched bracket
			throw_error("Unterminated index spec.");
			alu_state = STATE_ERROR;
			// remove previous operator by overwriting with newest one...
			PREVIOUS_OPERATOR = NEWEST_OPERATOR;
			--op_sp;	// ...and shrinking operator stack
		}
		break;
	default:
		BUG("IllegalOperatorId", previous);
	}
}
// put dyadic operator on stack and try to reduce stacks by performing
// high-priority operations: as long as the second-to-last operator
// has a higher priority than the last one, perform the operation of
// that second-to-last one and remove it from stack.
static void push_dyadic_and_check(struct expression *expression, struct op *op)
{
	PUSH_OP(op);	// put newest operator on stack
	if (alu_state < STATE_MAX_GO_ON)
		alu_state = STATE_EXPECT_ARG_OR_MONADIC_OP;
	while (alu_state == STATE_EXPECT_ARG_OR_MONADIC_OP) {
		// if there is only one operator left on op stack, it must be
		// "start of expression", so there isn't anything to do here:
		if (op_sp < 2)
			return;

		// if previous operator has lower piority, nothing to do here:
		if (PREVIOUS_OPERATOR->priority < NEWEST_OPERATOR->priority)
			return;

		// if priorities are the same, check associativity:
		if ((PREVIOUS_OPERATOR->priority == NEWEST_OPERATOR->priority)
		&& (NEWEST_OPERATOR->priority == PRIO_POWEROF)) {
			if (config.dialect >= V0_94_6__RIGHT_ASSOC_POWER) {
				return;	// since 0.94.6, "power of" operator is right-associative, so we cannot do the lefthand operation yet
			} else {
				// in older versions, it was left-associative, so we go on
			}
		}

		// ok, so now perform operation indicated by previous operator!
		switch (PREVIOUS_OPERATOR->group) {
		case OPGROUP_MONADIC:
			// stacks:	...	...	previous op(monadic)	newest arg	newest op(dyadic)
			if (arg_sp < 1)
				BUG("ArgStackEmpty", arg_sp);
			NEWEST_ARGUMENT.type->monadic_op(&NEWEST_ARGUMENT, PREVIOUS_OPERATOR);
			expression->is_parenthesized = FALSE;	// operation was something other than parentheses
			// now remove previous operator by overwriting with newest one...
			PREVIOUS_OPERATOR = NEWEST_OPERATOR;
			--op_sp;	// ...and shrinking operator stack
			break;
		case OPGROUP_DYADIC:
			// stacks:	previous arg	previous op(dyadic)	newest arg	newest op(dyadic)
			if (arg_sp < 2)
				BUG("NotEnoughArgs", arg_sp);
			PREVIOUS_ARGUMENT.type->dyadic_op(&PREVIOUS_ARGUMENT, PREVIOUS_OPERATOR, &NEWEST_ARGUMENT);
			expression->is_parenthesized = FALSE;	// operation was something other than parentheses
			// now remove previous operator by overwriting with newest one...
			PREVIOUS_OPERATOR = NEWEST_OPERATOR;
			--op_sp;	// ...and shrinking operator stack
			--arg_sp;	// and then shrink argument stack because two arguments just became one
			break;
		case OPGROUP_SPECIAL:
			// stacks:	...	...	previous op(special)	newest arg	newest op(dyadic)
			if (NEWEST_OPERATOR->id != OPID_TERMINATOR)
				BUG("StrangeOperator", NEWEST_OPERATOR->id);
			handle_special_operator(expression, PREVIOUS_OPERATOR->id);
			// the function above fixes both stacks and "is_parenthesized"!
			break;
		default:
			BUG("IllegalOperatorGroup", PREVIOUS_OPERATOR->group);
		}
	}
}


// expect argument or monadic operator (hopefully inlined)
// returns TRUE if it ate any non-space (-> so expression isn't empty)
// returns FALSE if first non-space is delimiter (-> end of expression)
static boolean expect_argument_or_monadic_operator(struct expression *expression)
{
	struct op	*op;
	int		ugly_length_kluge;
	boolean		perform_negation;
	scope_t		scope;	// for parsing symbols

	SKIPSPACE();
	switch (GotByte) {
	case '+':	// anonymous forward label
		// count plus signs to build name of anonymous label
		dynabuf_clear(GlobalDynaBuf);
		do {
			DYNABUF_APPEND(GlobalDynaBuf, '+');
		} while (GetByte() == '+');
		ugly_length_kluge = GlobalDynaBuf->size;	// FIXME - get rid of this!
		symbol_fix_forward_anon_name(FALSE);	// FALSE: do not increment counter
		get_symbol_value(section_now->local_scope, ugly_length_kluge, FALSE);	// no prefix, no pass number check
		goto now_expect_dyadic_op;

	case '-':	// NEGATION operator or anonymous backward label
		// count minus signs in case it's an anonymous backward label
		perform_negation = FALSE;
		dynabuf_clear(GlobalDynaBuf);
		do {
			DYNABUF_APPEND(GlobalDynaBuf, '-');
			perform_negation = !perform_negation;
		} while (GetByte() == '-');
		SKIPSPACE();
		if (BYTE_FOLLOWS_ANON(GotByte)) {
			dynabuf_append(GlobalDynaBuf, '\0');
			get_symbol_value(section_now->local_scope, GlobalDynaBuf->size - 1, TRUE);	// no prefix, -1 to not count terminator, check pass number
			goto now_expect_dyadic_op;
		}

		if (perform_negation)
			PUSH_OP(&ops_negate);
		// State doesn't change
		break;//goto done;
// Real monadic operators (state doesn't change, still ExpectMonadic)
	case '!':	// NOT operator
		op = &ops_not;
		goto get_byte_and_push_monadic;

	case '<':	// LOWBYTE operator
		op = &ops_low_byte_of;
		goto get_byte_and_push_monadic;

	case '>':	// HIGHBYTE operator
		op = &ops_high_byte_of;
		goto get_byte_and_push_monadic;

	case '^':	// BANKBYTE operator
		op = &ops_bank_byte_of;
		goto get_byte_and_push_monadic;

// special operators
	case '[':	// start of list literal
		list_init_list(&arg_stack[arg_sp++]);	// put empty list on arg stack
		NEXTANDSKIPSPACE();
		if (GotByte == ']') {
			// list literal is empty, so we're basically done
			GetByte();
			goto now_expect_dyadic_op;

		} else {
			// non-empty list literal
			PUSH_OP(&ops_start_list);	// quasi-monadic "start of list", makes sure earlier ops do not process empty list
			push_dyadic_and_check(expression, &ops_list_append);	// dyadic "append to list", so next arg will be appended to list
			//now we're back in STATE_EXPECT_ARG_OR_MONADIC_OP
		}
		break;//goto done;

	case '(':	// left parenthesis
		op = &ops_subexpr_paren;
		goto get_byte_and_push_monadic;

// arguments (state changes to ExpectDyadic)
	case '"':	// character (old) or string (new)
	case '\'':	// character
		// Character will be converted using current encoding
		parse_quoted(GotByte);
		// Now GotByte = char following closing quote
		goto now_expect_dyadic_op;

	case '%':	// Binary value
		parse_binary_literal();	// Now GotByte = non-binary char
		goto now_expect_dyadic_op;

	case '&':	// octal value or "unpseudo" operator applied to label
		if (parse_octal_or_unpseudo() == 0)
			goto now_expect_dyadic_op;

		// if we're here, there was an error (like "no string given"):
		alu_state = STATE_ERROR;
		break;//goto done;
	case '$':	// Hexadecimal value
		parse_hex_literal();
		// Now GotByte = non-hexadecimal char
		goto now_expect_dyadic_op;

	case '*':	// Program counter
		parse_program_counter();
		// Now GotByte = char after closing quote
		goto now_expect_dyadic_op;

// FIXME - find a way to tell decimal point and LOCAL_PREFIX apart!
	case '.':	// local symbol or fractional part of float value
		GetByte();	// start after '.'
		// check for fractional part of float value
		if ((GotByte >= '0') && (GotByte <= '9')) {
			parse_frac_part(0);	// now GotByte = non-decimal char
			goto now_expect_dyadic_op;
		}

		// here we need to put '.' into GlobalDynaBuf even though we have already skipped it:
		if (input_read_scope_and_symbol_name_KLUGED(&scope) == 0) {	// now GotByte = illegal char
			get_symbol_value(scope, GlobalDynaBuf->size - 1, FALSE);	// -1 to not count terminator, no pass number check
			goto now_expect_dyadic_op;	// ok
		}

		// if we're here, an error has been thrown because there was no string after the '.'.
		alu_state = STATE_ERROR;
		break;//goto done;
	case CHEAP_PREFIX:	// cheap local symbol
		//printf("looking in cheap scope %d\n", section_now->cheap_scope);
		if (input_read_scope_and_symbol_name(&scope) == 0) {	// now GotByte = illegal char
			get_symbol_value(scope, GlobalDynaBuf->size - 1, FALSE);	// -1 to not count terminator, no pass number check
			goto now_expect_dyadic_op;	// ok
		}

		// if we're here, an error has been thrown because there was no string after the '@'.
		alu_state = STATE_ERROR;
		break;//goto done;
	// decimal values and global symbols
	default:	// all other characters
		if ((GotByte >= '0') && (GotByte <= '9')) {
			parse_number_literal();
			// now GotByte = non-decimal char
			goto now_expect_dyadic_op;
		}

		if (BYTE_STARTS_KEYWORD(GotByte)) {
			register int	length;

			// read global label (or "NOT")
			length = parser_read_keyword();
			// now GotByte = illegal char
			// Check for NOT. Okay, it's hardcoded,
			// but so what? Sue me...
			if ((length == 3)
			&& ((GlobalDynaBuf->buffer[0] | 32) == 'n')
			&& ((GlobalDynaBuf->buffer[1] | 32) == 'o')
			&& ((GlobalDynaBuf->buffer[2] | 32) == 't')) {
				PUSH_OP(&ops_not);
				// state doesn't change
			} else {
				if (GotByte == '(') {
					parse_function_call();
// i thought about making the parentheses optional, so you can write "a = sin b"
// just like "a = not b". but then each new function name would have to be made
// a reserved keyword, otherwise stuff like "a = sin * < b" would be ambiguous:
// it could mean either "compare sine of PC to b" or "multiply 'sin' by low byte
// of b".
// however, apart from that check above, function calls have nothing to do with
// parentheses: "sin(x+y)" gets parsed just like "not(x+y)".
				} else {
					get_symbol_value(SCOPE_GLOBAL, GlobalDynaBuf->size - 1, FALSE);	// no prefix, -1 to not count terminator, no pass number check
					goto now_expect_dyadic_op;
				}
			}
		} else {
			// illegal character read - so don't go on
			// we found end-of-expression instead of an argument,
			// that's either an empty expression or an erroneous one!
			PUSH_INT_ARG(0, 0, 0);	// push dummy argument so stack checking code won't bark	FIXME - use undefined?
			if (op_stack[op_sp - 1] == &ops_start_expression) {
				push_dyadic_and_check(expression, &ops_terminating_char);
			} else {
				throw_error(exception_syntax);
				alu_state = STATE_ERROR;
			}
			return FALSE;	// found delimiter
		}
		break;//goto done;

// no other possibilities, so here are the shared endings

get_byte_and_push_monadic:
		GetByte();
		PUSH_OP(op);
		// state doesn't change
		break;

now_expect_dyadic_op:
		// bugfix: if in error state, do not change state back to valid one
		if (alu_state < STATE_MAX_GO_ON)
			alu_state = STATE_EXPECT_DYADIC_OP;
		break;
	}
//done:
	return TRUE;	// parsed something
}


// expect dyadic operator (hopefully inlined)
static void expect_dyadic_operator(struct expression *expression)
{
	void		*node_body;
	struct op	*op;

	SKIPSPACE();
	switch (GotByte) {
// single-character dyadic operators
	case '^':	// "to the power of"
		op = &ops_powerof;
		goto get_byte_and_push_dyadic;

	case '+':	// add
		op = &ops_add;
		goto get_byte_and_push_dyadic;

	case '-':	// subtract
		op = &ops_subtract;
		goto get_byte_and_push_dyadic;

	case '*':	// multiply
		op = &ops_multiply;
		goto get_byte_and_push_dyadic;

	case '/':	// divide
		op = &ops_divide;
		goto get_byte_and_push_dyadic;

	case '%':	// modulo
		op = &ops_modulo;
		goto get_byte_and_push_dyadic;

	case '&':	// bitwise AND
		op = &ops_and;
		goto get_byte_and_push_dyadic;

	case '|':	// bitwise OR
		op = &ops_or;
		goto get_byte_and_push_dyadic;

// this part is commented out because there is no XOR character defined
//	case ???:	// bitwise exclusive OR
//		op = &ops_xor;
//		goto get_byte_and_push_dyadic;

	case '=':	// is equal
		op = &ops_equals;
		// atm, accept both "=" and "==". in future, prefer "=="!
		if (GetByte() == '=') {
			GetByte();	// eat second '=' character
		} else {
			//throw_finalpass_warning("old-style \"=\" comparison detected, please use \"==\" instead.");	ACTIVATE!
		}
		goto push_dyadic_op;

	case '[':	// indexing operator
		GetByte();	// eat char
		// first put high-priority dyadic on stack,
		// then low-priority special ops_subexpr_bracket
		push_dyadic_and_check(expression, &ops_atindex);
		// now we're in STATE_EXPECT_ARG_OR_MONADIC_OP
		PUSH_OP(&ops_subexpr_bracket);
		return;

// multi-character dyadic operators
	case '!':	// "!="
		GetByte();	// eat '!'
		if (parser_expect('=')) {
			op = &ops_not_equal;
			goto push_dyadic_op;
		}

		alu_state = STATE_ERROR;
		break;//goto end;
	case '<':	// "<", "<=", "<<" and "<>"
		switch (GetByte()) {
		case '=':	// "<=", less or equal
			op = &ops_less_or_equal;
			goto get_byte_and_push_dyadic;

		case '<':	// "<<", shift left
			op = &ops_shift_left;
			goto get_byte_and_push_dyadic;

		case '>':	// "<>", not equal
			op = &ops_not_equal;
			goto get_byte_and_push_dyadic;

		default:	// "<", less than
			op = &ops_less_than;
			goto push_dyadic_op;

		}
		//break; unreachable
	case '>':	// ">", ">=", ">>", ">>>" and "><"
		switch (GetByte()) {
		case '=':	// ">=", greater or equal
			op = &ops_greater_or_equal;
			goto get_byte_and_push_dyadic;

		case '<':	// "><", not equal
			op = &ops_not_equal;
			goto get_byte_and_push_dyadic;

		case '>':	// ">>" or ">>>", shift right
			op = &ops_asr;	// arithmetic shift right
			if (GetByte() != '>')
				goto push_dyadic_op;

			op = &ops_lsr;	// logical shift right
			goto get_byte_and_push_dyadic;

		default:	// ">", greater than
			op = &ops_greater_than;
			goto push_dyadic_op;

		}
		//break; unreachable
// end of expression or text version of dyadic operator
	default:
		// check string versions of operators
		if (BYTE_STARTS_KEYWORD(GotByte)) {
			parser_read_and_lower_keyword();
			// now GotByte = illegal char
			// search for tree item
			if (tree_easy_scan(op_tree, &node_body, GlobalDynaBuf)) {
				op = node_body;
				goto push_dyadic_op;
			}

			throw_error("Unknown operator.");
			alu_state = STATE_ERROR;
			//goto end;
		} else {
			// we found end-of-expression when expecting an operator, that's ok.
			op = &ops_terminating_char;
			goto push_dyadic_op;
		}
	}
//end:
	return;	// TODO - change the two points that go here and add a BUG() instead

// shared endings
get_byte_and_push_dyadic:
	GetByte();
push_dyadic_op:
	push_dyadic_and_check(expression, op);
}


// helper function: create and output error message about (argument/)operator/argument combination
static void unsupported_operation(const struct object *optional, const struct op *op, const struct object *arg)
{
	if (optional) {
		if (op->group != OPGROUP_DYADIC)
			BUG("OperatorIsNotDyadic", op->id);
	} else {
		if (op->group != OPGROUP_MONADIC)
			BUG("OperatorIsNotMonadic", op->id);
	}
	dynabuf_clear(errormsg_dyna_buf);
	dynabuf_add_string(errormsg_dyna_buf, "Operation not supported: Cannot apply \"");
	dynabuf_add_string(errormsg_dyna_buf, op->text_version);
	dynabuf_add_string(errormsg_dyna_buf, "\" to \"");
	if (optional) {
		dynabuf_add_string(errormsg_dyna_buf, optional->type->name);
		dynabuf_add_string(errormsg_dyna_buf, "\" and \"");
	}
	dynabuf_add_string(errormsg_dyna_buf, arg->type->name);
	dynabuf_add_string(errormsg_dyna_buf, "\".");
	dynabuf_append(errormsg_dyna_buf, '\0');
	throw_error(errormsg_dyna_buf->buffer);
}


// int/float

// int:
// create byte-sized int object (for comparison results, converted characters, ...)
// FIXME - as this does not set the FITS_BYTE flag, why "byte-sized"?! why not int_create_int?
static void int_create_byte(struct object *self, intval_t byte)
{
	self->type = &type_number;
	self->u.number.ntype = NUMTYPE_INT;
	self->u.number.flags = 0;
	self->u.number.val.intval = byte;
	self->u.number.addr_refs = 0;
}

// int:
// convert to float
inline static void int_to_float(struct object *self)
{
	self->u.number.ntype = NUMTYPE_FLOAT;
	self->u.number.val.fpval = self->u.number.val.intval;
}

// float:
// convert to int
inline static void float_to_int(struct object *self)
{
	self->u.number.ntype = NUMTYPE_INT;
	self->u.number.val.intval = self->u.number.val.fpval;
}

// list:
// replace with item at index
static void list_to_item(struct object *self, int index)
{
	struct listitem	*item;

	item = self->u.listhead->next;
	while (index) {
		item = item->next;
		--index;
	}
	self->u.listhead->u.listinfo.refs--;	// FIXME - call some fn for this (and do _after_ next line)
	*self = item->u.payload;	// FIXME - if item is a list, it would gain a ref by this...
}

// string:
// replace with char at index
static void string_to_byte(struct object *self, int index)
{
	intval_t	byte;

	byte = encoding_encode_char(self->u.string->payload[index]);
	self->u.string->refs--;	// FIXME - call a function for this...
	int_create_byte(self, byte);
}

// int/float:
// return DEFINED flag
static boolean number_is_defined(const struct object *self)
{
	return self->u.number.ntype != NUMTYPE_UNDEFINED;
}

// list:
// return TRUE only if completely defined
static boolean list_is_defined(const struct object *self)
{
	struct listitem	*item;

	// iterate over items: if an undefined one is found, return FALSE
	item = self->u.listhead->next;
	while (item != self->u.listhead) {
		if (!(item->u.payload.type->is_defined(&item->u.payload)))
			return FALSE;	// we found something undefined

		item = item->next;
	}
	// otherwise, list is defined
	return TRUE;
}

// string:
// ...is always considered "defined"
static boolean object_return_true(const struct object *self)
{
	return TRUE;
}

// int/float:
// check if new value differs from old
// returns FALSE in case of undefined value(s), because undefined is not necessarily different!
static boolean number_differs(const struct object *self, const struct object *other)
{
	if (self->u.number.ntype == NUMTYPE_UNDEFINED)
		return FALSE;

	if (other->u.number.ntype == NUMTYPE_UNDEFINED)
		return FALSE;

	if (self->u.number.ntype == NUMTYPE_INT) {
		if (other->u.number.ntype == NUMTYPE_INT)
			return self->u.number.val.intval != other->u.number.val.intval;
		else
			return self->u.number.val.intval != other->u.number.val.fpval;
	} else {
		if (other->u.number.ntype == NUMTYPE_INT)
			return self->u.number.val.fpval != other->u.number.val.intval;
		else
			return self->u.number.val.fpval != other->u.number.val.fpval;
	}
}
// list:
// check if new value differs from old
static boolean list_differs(const struct object *self, const struct object *other)
{
	struct listitem	*arthur,
			*ford;

	if (self->u.listhead->u.listinfo.length != other->u.listhead->u.listinfo.length)
		return TRUE;	// lengths differ

	// same length, so iterate over lists and check items
	arthur = self->u.listhead->next;
	ford = other->u.listhead->next;
	while (arthur != self->u.listhead) {
		if (ford == other->u.listhead)
			BUG("ListLengthError", 0);
		if (arthur->u.payload.type != ford->u.payload.type)
			return TRUE;	// item types differ

		if (arthur->u.payload.type->differs(&arthur->u.payload, &ford->u.payload))
			return TRUE;	// item values differ

		arthur = arthur->next;
		ford = ford->next;
	}
	if (ford != other->u.listhead)
		BUG("ListLengthError", 1);
	return FALSE;	// no difference found
}
// string:
// check if new value differs from old
static boolean string_differs(const struct object *self, const struct object *other)
{
	if (self->u.string->length != other->u.string->length)
		return TRUE;

	return !!memcmp(self->u.string->payload, other->u.string->payload, self->u.string->length);
}

// int/float:
// assign new value
static boolean number_assign(struct object *self, const struct object *new_value, boolean accept_change)
{
	bits	own_flags	= self->u.number.flags,
		other_flags	= new_value->u.number.flags;
	// local copies of the flags are used because
	//	self->...flags might get overwritten when copying struct over, and
	//	new_value-> is const so shouldn't be touched.
	boolean	redefined	= FALSE;

	// accepting a different value is easily done by just forgetting the old one:
	if (accept_change) {
		self->u.number.ntype = NUMTYPE_UNDEFINED;
		own_flags &= ~(NUMBER_FITS_BYTE);
	}

	// copy struct over?
	if (self->u.number.ntype != NUMTYPE_UNDEFINED) {
		// symbol is already defined, so compare new and old values
		// if values differ, remember to tell caller
		if (number_differs(self, new_value))
			redefined = TRUE;	// -> throw "symbol already defined" error
	}
	// use new value:
	*self = *new_value;	// copy type and flags/value/addr_refs

	// now fix flags:

	// if symbol has no force bits of its own, use the ones from new value:
	if ((own_flags & NUMBER_FORCEBITS) == 0)
		own_flags = (own_flags & ~NUMBER_FORCEBITS) | (other_flags & NUMBER_FORCEBITS);

	// tainted symbols without "fits byte" flag must never get that flag
	if ((own_flags & (NUMBER_EVER_UNDEFINED | NUMBER_FITS_BYTE)) == NUMBER_EVER_UNDEFINED)
		other_flags &= ~NUMBER_FITS_BYTE;
	// now OR together "fits byte", "defined" and "tainted"
	// (any hypothetical problems like "what if new value is later found out
	// to _not_ fit byte?" would be detected in a later pass because trying
	// to assign that new value would throw an error)
	own_flags |= other_flags & (NUMBER_FITS_BYTE | NUMBER_EVER_UNDEFINED);

	self->u.number.flags = own_flags;
	return redefined;
}


// list:
// assign new value
static boolean list_assign(struct object *self, const struct object *new_value, boolean accept_change)
{
	boolean	redefined	= FALSE;

	if ((!accept_change) && list_differs(self, new_value))
		redefined = TRUE;	// -> throw "symbol already defined" error

	*self = *new_value;
	return redefined;
}

// string:
// assign new value
static boolean string_assign(struct object *self, const struct object *new_value, boolean accept_change)
{
	boolean	redefined	= FALSE;

	if ((!accept_change) && string_differs(self, new_value))
		redefined = TRUE;	// -> throw "symbol already defined" error

	*self = *new_value;
	return redefined;
}


// undefined:
// handle monadic operator (includes functions)
static void undef_handle_monadic_operator(struct object *self, const struct op *op)
{
	switch (op->id) {
	case OPID_INT:
	case OPID_FLOAT:
		self->u.number.addr_refs = 0;
		break;
	case OPID_SIN:
	case OPID_COS:
	case OPID_TAN:
	case OPID_ARCSIN:
	case OPID_ARCCOS:
	case OPID_ARCTAN:
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		self->u.number.addr_refs = 0;
		break;
	case OPID_NOT:
	case OPID_NEGATE:
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		self->u.number.addr_refs = -(self->u.number.addr_refs);	// negate address ref count
		break;
	case OPID_LOWBYTEOF:
	case OPID_HIGHBYTEOF:
	case OPID_BANKBYTEOF:
		self->u.number.flags |= NUMBER_FITS_BYTE;
		self->u.number.flags &= ~NUMBER_FORCEBITS;
		self->u.number.addr_refs = 0;
		break;
	case OPID_DEC:
	case OPID_HEX:
		// undefined number results in empty string:
		// (so program grows in later passes but does not shrink)
		string_prepare_string(self, 0);	// replace self with zero-length string
		// we just converted an undefined argument into a result that is
		// defined, so the expression parser won't count it as undefined
		// when returning to the caller. therefore, we count it ourself:
		++pass.counters.undefineds;
		break;
// add new monadic operators here
//	case OPID_:
//		break;
	default:
		unsupported_operation(NULL, op, self);
	}
}

// int:
// helper function to replace int object with string version (either decimal or hexadecimal)
// (also see number_print() further down which does something similar but not identical)
#define NUMBUFSIZE	64	// large enough(tm) even for 64bit systems
static void int_to_string(struct object *self, const char formatstring[])
{
	char	buffer[NUMBUFSIZE];
	int	length;

#if _BSD_SOURCE || _XOPEN_SOURCE >= 500 || _ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L
	length = snprintf(buffer, NUMBUFSIZE, formatstring, (long) self->u.number.val.intval);
#else
	length = sprintf(buffer, formatstring, (long) self->u.number.val.intval);
#endif
	string_prepare_string(self, length);	// create string object and put on arg stack
	// (the fn above has already put a terminator at the correct position)
	memcpy(self->u.string->payload, buffer, length);
}

// prototype for int/float passing
static void float_handle_monadic_operator(struct object *self, const struct op *op);
// int:
// handle monadic operator (includes functions)
static void int_handle_monadic_operator(struct object *self, const struct op *op)
{
	int	refs	= 0;	// default for "addr_refs", shortens this fn

	switch (op->id) {
	case OPID_INT:
		break;
	case OPID_FLOAT:
		int_to_float(self);
		break;
	case OPID_SIN:
	case OPID_COS:
	case OPID_TAN:
	case OPID_ARCSIN:
	case OPID_ARCCOS:
	case OPID_ARCTAN:
		// convert int to fp and ask fp handler to do the work
		int_to_float(self);
		float_handle_monadic_operator(self, op);	// TODO - put recursion check around this?
		return;	// float handler has done everything

	case OPID_NOT:
		self->u.number.val.intval = ~(self->u.number.val.intval);
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		refs = -(self->u.number.addr_refs);	// negate address ref count
		break;
	case OPID_NEGATE:
		self->u.number.val.intval = -(self->u.number.val.intval);
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		refs = -(self->u.number.addr_refs);	// negate address ref count as well
		break;
	case OPID_LOWBYTEOF:
		self->u.number.val.intval = (self->u.number.val.intval) & 255;
		self->u.number.flags |= NUMBER_FITS_BYTE;
		self->u.number.flags &= ~NUMBER_FORCEBITS;
		break;
	case OPID_HIGHBYTEOF:
		self->u.number.val.intval = ((self->u.number.val.intval) >> 8) & 255;
		self->u.number.flags |= NUMBER_FITS_BYTE;
		self->u.number.flags &= ~NUMBER_FORCEBITS;
		break;
	case OPID_BANKBYTEOF:
		self->u.number.val.intval = ((self->u.number.val.intval) >> 16) & 255;
		self->u.number.flags |= NUMBER_FITS_BYTE;
		self->u.number.flags &= ~NUMBER_FORCEBITS;
		break;
	case OPID_DEC:
		int_to_string(self, "%ld");	// decimal format
		break;
	case OPID_HEX:
		int_to_string(self, "%lx");	// hexadecimal format
		break;
// add new monadic operators here
//	case OPID_:
//		break;
	default:
		unsupported_operation(NULL, op, self);
	}
	self->u.number.addr_refs = refs;	// update address refs with local copy
}

// float:
// helper function for asin/acos:
// make sure arg is in [-1, 1] range before calling function
static void float_ranged_fn(double (*fn)(double), struct object *self)
{
	if ((self->u.number.val.fpval >= -1) && (self->u.number.val.fpval <= 1)) {
		self->u.number.val.fpval = fn(self->u.number.val.fpval);
	} else {
		throw_error("Argument out of range.");	// TODO - add number output to error message
		self->u.number.val.fpval = 0;
	}
}

// float:
// handle monadic operator (includes functions)
static void float_handle_monadic_operator(struct object *self, const struct op *op)
{
	int	refs	= 0;	// default for "addr_refs", shortens this fn

	switch (op->id) {
	case OPID_INT:
		float_to_int(self);
		break;
	case OPID_FLOAT:
		break;
	case OPID_SIN:
		self->u.number.val.fpval = sin(self->u.number.val.fpval);
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		break;
	case OPID_COS:
		self->u.number.val.fpval = cos(self->u.number.val.fpval);
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		break;
	case OPID_TAN:
		self->u.number.val.fpval = tan(self->u.number.val.fpval);
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		break;
	case OPID_ARCSIN:
		float_ranged_fn(asin, self);
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		break;
	case OPID_ARCCOS:
		float_ranged_fn(acos, self);
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		break;
	case OPID_ARCTAN:
		self->u.number.val.fpval = atan(self->u.number.val.fpval);
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		break;
	case OPID_NEGATE:
		self->u.number.val.fpval = -(self->u.number.val.fpval);
		self->u.number.flags &= ~NUMBER_FITS_BYTE;
		refs = -(self->u.number.addr_refs);	// negate address ref count as well
		break;
	case OPID_NOT:
	case OPID_LOWBYTEOF:
	case OPID_HIGHBYTEOF:
	case OPID_BANKBYTEOF:
		// convert fp to int and ask int handler to do the work
		float_to_int(self);
		int_handle_monadic_operator(self, op);	// TODO - put recursion check around this?
		return;	// int handler has done everything

// add new monadic operators here
//	case OPID_:
//		break;
	default:
		unsupported_operation(NULL, op, self);
	}
	self->u.number.addr_refs = refs;	// update address refs with local copy
}

// num:
// handle monadic operator (includes functions)
static void number_handle_monadic_operator(struct object *self, const struct op *op)
{
	// first check operators where we don't care about number type or value
	switch (op->id) {
	case OPID_ADDRESS:
		self->u.number.addr_refs = 1;	// result now is an address
		return;

	case OPID_ISNUMBER:
		int_create_byte(self, TRUE);
		return;

	case OPID_ISLIST:
	case OPID_ISSTRING:
		int_create_byte(self, FALSE);
		return;

	default:
		break;
	}
	// it's none of those, so split according to number type
	switch (self->u.number.ntype) {
	case NUMTYPE_UNDEFINED:
		undef_handle_monadic_operator(self, op);
		break;
	case NUMTYPE_INT:
		int_handle_monadic_operator(self, op);
		break;
	case NUMTYPE_FLOAT:
		float_handle_monadic_operator(self, op);
		break;
	default:
		BUG("IllegalNumberType1", self->u.number.ntype);
	}
}

// list:
// handle monadic operator (includes functions)
static void list_handle_monadic_operator(struct object *self, const struct op *op)
{
	int	length;

	switch (op->id) {
	case OPID_LEN:
		length = self->u.listhead->u.listinfo.length;
		self->u.listhead->u.listinfo.refs--;	// FIXME - call some list_decrement_refs() instead...
		self->type = &type_number;
		self->u.number.ntype = NUMTYPE_INT;
		self->u.number.flags = 0;
		self->u.number.val.intval = length;
		self->u.number.addr_refs = 0;
		break;
	case OPID_ISLIST:
		int_create_byte(self, TRUE);
		break;
	case OPID_ISNUMBER:
	case OPID_ISSTRING:
		int_create_byte(self, FALSE);
		break;
	default:
		unsupported_operation(NULL, op, self);
	}
}

// string:
// handle monadic operator (includes functions)
static void string_handle_monadic_operator(struct object *self, const struct op *op)
{
	int	length;

	switch (op->id) {
	case OPID_LEN:
		length = self->u.string->length;
		self->u.string->refs--;	// FIXME - call some string_decrement_refs() instead...
		self->type = &type_number;
		self->u.number.ntype = NUMTYPE_INT;
		self->u.number.flags = 0;
		self->u.number.val.intval = length;
		self->u.number.addr_refs = 0;
		break;
	case OPID_ISNUMBER:
	case OPID_ISLIST:
		int_create_byte(self, FALSE);
		break;
	case OPID_ISSTRING:
		int_create_byte(self, TRUE);
		break;
	default:
		unsupported_operation(NULL, op, self);
	}
}

// int/float:
// merge result flags
// (used by both int and float handlers for comparison operators)
static void intfloat_fix_result_after_comparison(struct object *self, const struct object *other, intval_t result)
{
	bits	flags;

	self->type = &type_number;
	self->u.number.ntype = NUMTYPE_INT;
	self->u.number.val.intval = result;
	self->u.number.addr_refs = 0;
	flags = (self->u.number.flags | other->u.number.flags) & NUMBER_EVER_UNDEFINED;	// EVER_UNDEFINED flags are ORd together
	flags |= NUMBER_FITS_BYTE;	// comparison results are either 0 or 1, so fit in byte
	// (FORCEBITS are cleared)
	self->u.number.flags = flags;
}
// (used by both int and float handlers for all other dyadic operators)
static void intfloat_fix_result_after_dyadic(struct object *self, const struct object *other)
{
	self->u.number.flags |= other->u.number.flags & (NUMBER_EVER_UNDEFINED | NUMBER_FORCEBITS);	// EVER_UNDEFINED and FORCEBITs are ORd together
	self->u.number.flags &= ~NUMBER_FITS_BYTE;	// clear FITS_BYTE because result could be anything
}

// undefined/int/float:
// handle dyadic operator
// (both args are numbers, but at least one of them is undefined!)
static void undef_handle_dyadic_operator(struct object *self, const struct op *op, struct object *other)
{
	int	refs	= 0;	// default for "addr_refs", shortens this fn

	switch (op->id) {
	case OPID_POWEROF:
	case OPID_MULTIPLY:
	case OPID_DIVIDE:
	case OPID_INTDIV:
	case OPID_MODULO:
	case OPID_SHIFTLEFT:
	case OPID_ASR:
	case OPID_LSR:
		break;

	case OPID_SUBTRACT:
		refs = self->u.number.addr_refs - other->u.number.addr_refs;	// subtract address references
		break;

	case OPID_LESSOREQUAL:
	case OPID_LESSTHAN:
	case OPID_GREATEROREQUAL:
	case OPID_GREATERTHAN:
	case OPID_NOTEQUAL:
	case OPID_EQUALS:
		// only for comparisons:
		self->u.number.flags |= NUMBER_FITS_BYTE;	// result is either 0 or 1, so fits in byte
		self->u.number.flags &= ~NUMBER_FORCEBITS;	// FORCEBITS are cleared
		goto shared;

	case OPID_EOR:
		eor_is_obsolete();
		/*FALLTHROUGH*/
	case OPID_XOR:
	case OPID_AND:
	case OPID_OR:
	case OPID_ADD:
		refs = self->u.number.addr_refs + other->u.number.addr_refs;	// add address references
		break;
// add new dyadic operators here
//	case OPID_:
//		break;
	default:
		unsupported_operation(self, op, other);
		return;
	}
	// CAUTION: comparisons goto label below instead of jumping here
	self->u.number.flags |= (other->u.number.flags & NUMBER_FORCEBITS);	// FORCEBITs are ORd together
	self->u.number.flags &= ~NUMBER_FITS_BYTE;	// clear FITS_BYTE because result could be anything
shared:
	self->u.number.flags |= (other->u.number.flags & NUMBER_EVER_UNDEFINED);	// EVER_UNDEFINED flags are ORd together
	self->u.number.ntype = NUMTYPE_UNDEFINED;
	self->u.number.addr_refs = refs;	// update address refs with local copy
}

// prototype for int/float passing
static void float_handle_dyadic_operator(struct object *self, const struct op *op, struct object *other);
// int:
// handle dyadic operator
static void int_handle_dyadic_operator(struct object *self, const struct op *op, struct object *other)
{
	int	refs	= 0;	// default for "addr_refs", shortens this fn

	// first check type of second arg:
	if (other->u.number.ntype == NUMTYPE_INT) {
		// ok
	} else if (other->u.number.ntype == NUMTYPE_FLOAT) {
		// handle according to operation
		switch (op->id) {
		case OPID_POWEROF:
		case OPID_MULTIPLY:
		case OPID_DIVIDE:
		case OPID_INTDIV:
		case OPID_ADD:
		case OPID_SUBTRACT:
		case OPID_EQUALS:
		case OPID_LESSOREQUAL:
		case OPID_LESSTHAN:
		case OPID_GREATEROREQUAL:
		case OPID_GREATERTHAN:
		case OPID_NOTEQUAL:
			// become float, delegate to float handler
			int_to_float(self);
			float_handle_dyadic_operator(self, op, other);	// TODO - put recursion check around this?
			return;	// float handler has done everything

		case OPID_LSR:
		case OPID_AND:
		case OPID_OR:
		case OPID_EOR:
		case OPID_XOR:
			// convert other to int, warning user
			throw_finalpass_warning(exception_float_to_int);
			/*FALLTHROUGH*/
		case OPID_MODULO:
		case OPID_SHIFTLEFT:
		case OPID_ASR:
			// convert other to int
			float_to_int(other);
			break;
// add new dyadic operators here:
//		case OPID_:
//			break;
		default:
			unsupported_operation(self, op, other);
			return;
		}
// add new types here:
//	} else if (other->u.number.ntype == NUMTYPE_) {
//		...
	} else {
		unsupported_operation(self, op, other);
		return;
	}
	// maybe put this into an extra "int_dyadic_int" function?
	// sanity check, now "other" must be an int
	if (other->u.number.ntype != NUMTYPE_INT)
		BUG("SecondArgIsNotAnInt", op->id);

	// part 2: now we got rid of non-ints, perform actual operation:
	switch (op->id) {
	case OPID_POWEROF:
		if (other->u.number.val.intval >= 0) {
			self->u.number.val.intval = my_pow(self->u.number.val.intval, other->u.number.val.intval);
		} else {
			throw_error("Exponent is negative.");
			self->u.number.val.intval = 0;
		}
		break;
	case OPID_MULTIPLY:
		self->u.number.val.intval *= other->u.number.val.intval;
		break;
	case OPID_DIVIDE:
	case OPID_INTDIV:
		if (other->u.number.val.intval) {
			self->u.number.val.intval /= other->u.number.val.intval;
			break;
		}
		// "division by zero" output is below
		/*FALLTHROUGH*/
	case OPID_MODULO:
		if (other->u.number.val.intval) {
			self->u.number.val.intval %= other->u.number.val.intval;
		} else {
			throw_error(exception_div_by_zero);
			self->u.number.val.intval = 0;
		}
		break;
	case OPID_ADD:
		self->u.number.val.intval += other->u.number.val.intval;
		refs = self->u.number.addr_refs + other->u.number.addr_refs;	// add address references
		break;
	case OPID_SUBTRACT:
		self->u.number.val.intval -= other->u.number.val.intval;
		refs = self->u.number.addr_refs - other->u.number.addr_refs;	// subtract address references
		break;
	case OPID_SHIFTLEFT:
		self->u.number.val.intval <<= other->u.number.val.intval;
		break;
	case OPID_ASR:
		self->u.number.val.intval = my_asr(self->u.number.val.intval, other->u.number.val.intval);
		break;
	case OPID_LSR:
		self->u.number.val.intval = ((uintval_t) (self->u.number.val.intval)) >> other->u.number.val.intval;
		break;
	case OPID_LESSOREQUAL:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.intval <= other->u.number.val.intval);
		return;

	case OPID_LESSTHAN:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.intval < other->u.number.val.intval);
		return;

	case OPID_GREATEROREQUAL:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.intval >= other->u.number.val.intval);
		return;

	case OPID_GREATERTHAN:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.intval > other->u.number.val.intval);
		return;

	case OPID_NOTEQUAL:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.intval != other->u.number.val.intval);
		return;

	case OPID_EQUALS:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.intval == other->u.number.val.intval);
		return;

	case OPID_AND:
		self->u.number.val.intval &= other->u.number.val.intval;
		refs = self->u.number.addr_refs + other->u.number.addr_refs;	// add address references
		break;
	case OPID_OR:
		self->u.number.val.intval |= other->u.number.val.intval;
		refs = self->u.number.addr_refs + other->u.number.addr_refs;	// add address references
		break;
	case OPID_EOR:
		eor_is_obsolete();
		/*FALLTHROUGH*/
	case OPID_XOR:
		self->u.number.val.intval ^= other->u.number.val.intval;
		refs = self->u.number.addr_refs + other->u.number.addr_refs;	// add address references
		break;
// add new dyadic operators here
//	case OPID_:
//		break;
	default:
		unsupported_operation(self, op, other);
		return;
	}
	// CAUTION: comparisons call intfloat_fix_result_after_comparison instead of jumping here
	self->u.number.addr_refs = refs;	// update address refs with local copy
	intfloat_fix_result_after_dyadic(self, other);	// fix result flags
}

// float:
// handle dyadic operator
static void float_handle_dyadic_operator(struct object *self, const struct op *op, struct object *other)
{
	int	refs	= 0;	// default for "addr_refs", shortens this fn

	// first check type of second arg:
	if (other->u.number.ntype == NUMTYPE_FLOAT) {
		// ok
	} else if (other->u.number.ntype == NUMTYPE_INT) {
		// handle according to operation
		switch (op->id) {
		// these want two floats
		case OPID_POWEROF:
		case OPID_MULTIPLY:
		case OPID_DIVIDE:
		case OPID_INTDIV:
		case OPID_ADD:
		case OPID_SUBTRACT:
		case OPID_LESSOREQUAL:
		case OPID_LESSTHAN:
		case OPID_GREATEROREQUAL:
		case OPID_GREATERTHAN:
		case OPID_NOTEQUAL:
		case OPID_EQUALS:
			// convert other to float
			int_to_float(other);
			break;
		// these jump to int handler anyway
		case OPID_MODULO:
		case OPID_LSR:
		case OPID_AND:
		case OPID_OR:
		case OPID_EOR:
		case OPID_XOR:
		// these actually want a float and an int
		case OPID_SHIFTLEFT:
		case OPID_ASR:
			break;
// add new dyadic operators here
//		case OPID_:
//			break;
		default:
			unsupported_operation(self, op, other);
			return;
		}
// add new types here
//	} else if (other->u.number.ntype == NUMTYPE_) {
//		...
	} else {
		unsupported_operation(self, op, other);
		return;
	}

	switch (op->id) {
	case OPID_POWEROF:
		self->u.number.val.fpval = pow(self->u.number.val.fpval, other->u.number.val.fpval);
		break;
	case OPID_MULTIPLY:
		self->u.number.val.fpval *= other->u.number.val.fpval;
		break;
	case OPID_DIVIDE:
		if (other->u.number.val.fpval) {
			self->u.number.val.fpval /= other->u.number.val.fpval;
		} else {
			throw_error(exception_div_by_zero);
			self->u.number.val.fpval = 0;
		}
		break;
	case OPID_INTDIV:
		if (other->u.number.val.fpval) {
			self->u.number.val.intval = self->u.number.val.fpval / other->u.number.val.fpval;	// fp becomes int!
		} else {
			throw_error(exception_div_by_zero);
			self->u.number.val.intval = 0;
		}
		self->u.number.ntype = NUMTYPE_INT;	// result is int
		break;
	case OPID_LSR:
	case OPID_AND:
	case OPID_OR:
	case OPID_EOR:
	case OPID_XOR:
		throw_finalpass_warning(exception_float_to_int);
		/*FALLTHROUGH*/
	case OPID_MODULO:
		float_to_int(self);
		// int handler will check other and, if needed, convert to int
		int_handle_dyadic_operator(self, op, other);	// TODO - put recursion check around this?
		return;	// int handler has done everything

	case OPID_ADD:
		self->u.number.val.fpval += other->u.number.val.fpval;
		refs = self->u.number.addr_refs + other->u.number.addr_refs;	// add address references
		break;
	case OPID_SUBTRACT:
		self->u.number.val.fpval -= other->u.number.val.fpval;
		refs = self->u.number.addr_refs - other->u.number.addr_refs;	// subtract address references
		break;
	case OPID_SHIFTLEFT:
		if (other->u.number.ntype == NUMTYPE_FLOAT)
			float_to_int(other);
		self->u.number.val.fpval *= pow(2.0, other->u.number.val.intval);
		break;
	case OPID_ASR:
		if (other->u.number.ntype == NUMTYPE_FLOAT)
			float_to_int(other);
		self->u.number.val.fpval /= (1 << other->u.number.val.intval);	// FIXME - why not use pow() as in SL above?
		break;
	case OPID_LESSOREQUAL:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.fpval <= other->u.number.val.fpval);
		return;

	case OPID_LESSTHAN:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.fpval < other->u.number.val.fpval);
		return;

	case OPID_GREATEROREQUAL:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.fpval >= other->u.number.val.fpval);
		return;

	case OPID_GREATERTHAN:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.fpval > other->u.number.val.fpval);
		return;

	case OPID_NOTEQUAL:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.fpval != other->u.number.val.fpval);
		return;

	case OPID_EQUALS:
		intfloat_fix_result_after_comparison(self, other, self->u.number.val.fpval == other->u.number.val.fpval);
		return;

// add new dyadic operators here
//	case OPID_:
//		break;
	default:
		unsupported_operation(self, op, other);
		return;
	}
	// CAUTION: comparisons call intfloat_fix_result_after_comparison instead of jumping here
	self->u.number.addr_refs = refs;	// update address refs with local copy
	intfloat_fix_result_after_dyadic(self, other);	// fix result flags
}

// num:
// handle dyadic operator
static void number_handle_dyadic_operator(struct object *self, const struct op *op, struct object *other)
{
	// first check type of second arg:
	if (other->type != &type_number) {
		unsupported_operation(self, op, other);
		return;
	}

	if ((self->u.number.ntype == NUMTYPE_UNDEFINED)
	|| (other->u.number.ntype == NUMTYPE_UNDEFINED))
		undef_handle_dyadic_operator(self, op, other);
	else if (self->u.number.ntype == NUMTYPE_INT)
		int_handle_dyadic_operator(self, op, other);
	else if (self->u.number.ntype == NUMTYPE_FLOAT)
		float_handle_dyadic_operator(self, op, other);
	else
		BUG("IllegalNumberType2", self->u.number.ntype);
}


// helper function for lists and strings, check index
// return zero on success, nonzero on error
static int get_valid_index(int *target, int length, const struct object *self, const struct op *op, struct object *other)
{
	int	index;

	if (other->type != &type_number) {
		unsupported_operation(self, op, other);
		return 1;
	}
	if (other->u.number.ntype == NUMTYPE_UNDEFINED) {
		throw_error("Index is undefined.");
		return 1;
	}
	if (other->u.number.ntype == NUMTYPE_FLOAT)
		float_to_int(other);
	if (other->u.number.ntype != NUMTYPE_INT)
		BUG("IllegalNumberType3", other->u.number.ntype);

	index = other->u.number.val.intval;
	// negative indices access from the end
	if (index < 0)
		index += length;
	if ((index < 0) || (index >= length)) {
		throw_error("Index out of range.");
		return 1;
	}
	*target = index;
	return 0;	// ok
}

// list:
// handle dyadic operator
static void list_handle_dyadic_operator(struct object *self, const struct op *op, struct object *other)
{
	struct listitem	*item;
	int		length;
	int		index;

	length = self->u.listhead->u.listinfo.length;
	switch (op->id) {
	case OPID_LIST_APPEND:
		list_append_object(self->u.listhead, other);
		// no need to check/update ref count of "other": it loses the ref on the stack and gains one in the list
		return;

	case OPID_ATINDEX:
		if (get_valid_index(&index, length, self, op, other))
			return;	// error has been thrown

		list_to_item(self, index);
		return;	// ok

	case OPID_ADD:
		if (other->type != &type_list)
			break;	// complain
		item = self->u.listhead;	// get ref to first list
		list_init_list(self);	// replace first list on arg stack with new one
		list_append_list(self->u.listhead, item);
		item->u.listinfo.refs--;	// FIXME - call a function for this...
		item = other->u.listhead;
		list_append_list(self->u.listhead, item);
		item->u.listinfo.refs--;	// FIXME - call a function for this...
		return;

	case OPID_EQUALS:
		if (other->type != &type_list)
			break;	// complain	FIXME - return FALSE?
		int_create_byte(self, !list_differs(self, other));
		// FIXME - call function to decrement refs!
		return;

	case OPID_NOTEQUAL:
		if (other->type != &type_list)
			break;	// complain	FIXME - return TRUE?
		int_create_byte(self, list_differs(self, other));
		// FIXME - call function to decrement refs!
		return;

	//case ...:	// maybe comparisons?
	default:
		break;	// complain
	}
	unsupported_operation(self, op, other);
}

// string:
// handle dyadic operator
static void string_handle_dyadic_operator(struct object *self, const struct op *op, struct object *other)
{
	int		length;
	int		index;
	struct string	*arthur,
			*ford;

	length = self->u.string->length;
	switch (op->id) {
	case OPID_ATINDEX:
		if (get_valid_index(&index, length, self, op, other))
			return;	// error has already been reported

		string_to_byte(self, index);
		return;	// ok

	case OPID_ADD:
		if (other->type != &type_string)
			break;	// complain
		arthur = self->u.string;
		ford = other->u.string;
		string_prepare_string(self, arthur->length + ford->length);	// create string object and put on arg stack
		memcpy(self->u.string->payload, arthur->payload, arthur->length);
		memcpy(self->u.string->payload + arthur->length, ford->payload, ford->length);
		arthur->refs--;	// FIXME - call a function for this...
		ford->refs--;	// FIXME - call a function for this...
		return;
		
	case OPID_EQUALS:
		if (other->type != &type_string)
			break;	// complain	FIXME - return FALSE?
		arthur = self->u.string;
		ford = other->u.string;
		int_create_byte(self, !string_differs(self, other));
		arthur->refs--;	// FIXME - call a function for this...
		ford->refs--;	// FIXME - call a function for this...
		return;

	case OPID_NOTEQUAL:
		if (other->type != &type_string)
			break;	// complain	FIXME - return TRUE?
		arthur = self->u.string;
		ford = other->u.string;
		int_create_byte(self, string_differs(self, other));
		arthur->refs--;	// FIXME - call a function for this...
		ford->refs--;	// FIXME - call a function for this...
		return;

	//case ...:	// maybe comparisons?
	default:
		break;	// complain
	}
	unsupported_operation(self, op, other);
}

// int/float:
// set flags according to result
static void number_fix_result(struct object *self)
{
	// only allow a single force bit
	if (self->u.number.flags & NUMBER_FORCES_24)
		self->u.number.flags &= ~(NUMBER_FORCES_16 | NUMBER_FORCES_8);
	else if (self->u.number.flags & NUMBER_FORCES_16)
		self->u.number.flags &= ~NUMBER_FORCES_8;
}

// list/string:
// no need to fix results
static void object_no_op(struct object *self)
{
}

// int/float:
// print value for user message
// (also see int_to_string() further up which does something similar but not identical)
static void number_print(const struct object *self, struct dynabuf *db)
{
	char	buffer[NUMBUFSIZE];

	if (self->u.number.ntype == NUMTYPE_UNDEFINED) {
		dynabuf_add_string(db, "<UNDEFINED NUMBER>");
	} else if (self->u.number.ntype == NUMTYPE_INT) {
#if _BSD_SOURCE || _XOPEN_SOURCE >= 500 || _ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L
		snprintf(buffer, NUMBUFSIZE, "%ld (0x%lx)", (long) self->u.number.val.intval, (long) self->u.number.val.intval);
#else
		sprintf(buffer, "%ld (0x%lx)", (long) self->u.number.val.intval, (long) self->u.number.val.intval);
#endif
		dynabuf_add_string(db, buffer);
	} else if (self->u.number.ntype == NUMTYPE_FLOAT) {
		// write up to 30 significant characters.
		// remaining 10 should suffice for sign,
		// decimal point, exponent, terminator etc.
		sprintf(buffer, "%.30g", self->u.number.val.fpval);
		dynabuf_add_string(db, buffer);
	} else {
		BUG("IllegalNumberType5", self->u.number.ntype);
	}
}

// list:
// print value for user message
static void list_print(const struct object *self, struct dynabuf *db)
{
	struct listitem	*item;
	int		length;
	struct object	*obj;
	const char	*prefix	= "";	// first item does not get a prefix

	dynabuf_append(db, '[');
	length = self->u.listhead->u.listinfo.length;
	item = self->u.listhead->next;
	while (length--) {
		obj = &item->u.payload;
		dynabuf_add_string(db, prefix);
		obj->type->print(obj, db);
		item = item->next;
		prefix = ", ";	// following items are prefixed
	}
	dynabuf_append(db, ']');
}

// string:
// print value for user message
static void string_print(const struct object *self, struct dynabuf *db)
{
	dynabuf_add_string(db, self->u.string->payload);	// there is a terminator after the actual payload, so this works
}

// number:
// is not iterable
static int has_no_length(const struct object *self)
{
	return -1;	// not iterable
}

// list:
// return length
static int list_get_length(const struct object *self)
{
	return self->u.listhead->u.listinfo.length;
}

// string:
// return length
static int string_get_length(const struct object *self)
{
	return self->u.string->length;
}

// number:
// cannot be indexed
static void cannot_be_indexed(const struct object *self, struct object *target, int index)
{
	BUG("TriedToIndexNumber", index);
}

// list:
// return item at index
static void list_at(const struct object *self, struct object *target, int index)
{
	*target = *self;
	list_to_item(target, index);
}

// string:
// return char at index
static void string_at(const struct object *self, struct object *target, int index)
{
	*target = *self;
	string_to_byte(target, index);
}

// "class" definitions
struct type	type_number	= {
	"number",
	number_is_defined,
	number_differs,
	number_assign,
	number_handle_monadic_operator,
	number_handle_dyadic_operator,
	number_fix_result,
	number_print,
	has_no_length,
	cannot_be_indexed
};
struct type	type_list	= {
	"list",
	list_is_defined,
	list_differs,
	list_assign,
	list_handle_monadic_operator,
	list_handle_dyadic_operator,
	object_no_op,	// no need to fix list results
	list_print,
	list_get_length,
	list_at
};
struct type	type_string	= {
	"string",
	object_return_true,	// strings are always defined
	string_differs,
	string_assign,
	string_handle_monadic_operator,
	string_handle_dyadic_operator,
	object_no_op,	// no need to fix string results
	string_print,
	string_get_length,
	string_at
};


// this is what the exported functions call
// returns nonzero on parse error
static int parse_expression(struct expression *expression)
{
	struct object	*result	= &expression->result;

	// make sure stacks are ready (if not yet initialised, do it now)
	if (arg_stack == NULL)
		enlarge_argument_stack();
	if (op_stack == NULL)
		enlarge_operator_stack();

	// init
	expression->is_empty = TRUE;	// becomes FALSE when first valid char gets parsed
	expression->open_parentheses = 0;
	expression->is_parenthesized = FALSE;	// '(' operator sets this to TRUE, all others to FALSE. outermost operator wins!
	//expression->number will be overwritten later, so no need to init

	op_sp = 0;	// operator stack pointer
	arg_sp = 0;	// argument stack pointer
	// begin by reading an argument (or a monadic operator)
	PUSH_OP(&ops_start_expression);
	alu_state = STATE_EXPECT_ARG_OR_MONADIC_OP;
	do {
		// check arg stack size. enlarge if needed
		if (arg_sp >= argstack_size)
			enlarge_argument_stack();
		// (op stack size is checked whenever pushing an operator)
		switch (alu_state) {
		case STATE_EXPECT_ARG_OR_MONADIC_OP:
			if (expect_argument_or_monadic_operator(expression))
				expression->is_empty = FALSE;
			break;
		case STATE_EXPECT_DYADIC_OP:
			expect_dyadic_operator(expression);
			break;
		case STATE_MAX_GO_ON:	// suppress
		case STATE_ERROR:	// compiler
		case STATE_END:		// warnings
			break;
		}
	} while (alu_state < STATE_MAX_GO_ON);
	// done. check state.
	if (alu_state == STATE_END) {
		// check for bugs
		if (arg_sp != 1)
			BUG("ArgStackNotEmpty", arg_sp);
		if (op_sp != 1)
			BUG("OperatorStackNotEmpty", op_sp);
		// copy result
		*result = arg_stack[0];
		// if there was nothing to parse, mark as undefined	FIXME - change this! make "nothing" its own result type; only numbers may be undefined
		// (so ALU_defined_int() can react)
		if (expression->is_empty) {
			result->type = &type_number;
			result->u.number.ntype = NUMTYPE_UNDEFINED;
			result->u.number.flags = NUMBER_EVER_UNDEFINED;
			result->u.number.addr_refs = 0;
		} else {
			// not empty. undefined?
			if (!(result->type->is_defined(result))) {
				// then count (in all passes)
				++pass.counters.undefineds;
			}
		}
		// do some checks depending on int/float
		result->type->fix_result(result);
		return 0;	// ok
	} else {
		// State is STATE_ERROR. Errors have already been reported,
		// but we must make sure not to pass bogus data to caller.
		// FIXME - just use the return value to indicate "there were errors, do not use result!"
		result->type = &type_number;
		result->u.number.ntype = NUMTYPE_UNDEFINED;	// maybe use NUMTYPE_INT to suppress follow-up errors?
		result->u.number.flags = 0;
		//result->u.number.val.intval = 0;
		result->u.number.addr_refs = 0;
		// make sure no additional (spurious) errors are reported:
		parser_skip_remainder();
		// FIXME - remove this when new function interface gets used:
		// callers must decide for themselves what to do when expression
		// parser returns error (and may decide to call parser_skip_remainder)
		return 1;	// error
	}
}


// store int value (if undefined, store zero)
// For empty expressions, an error is thrown.
// OPEN_PARENTHESIS: complain
// EMPTY: complain
// UNDEFINED: allow
// FLOAT: convert to int
void ALU_any_int(intval_t *target)	// ACCEPT_UNDEFINED
{
	struct expression	expression;

	parse_expression(&expression);	// FIXME - check return value and pass to caller!
	if (expression.open_parentheses)
		throw_error(exception_paren_open);
	if (expression.is_empty)
		throw_error(exception_no_value);
	if (expression.result.type == &type_number) {
		if (expression.result.u.number.ntype == NUMTYPE_UNDEFINED)
			*target = 0;
		else if (expression.result.u.number.ntype == NUMTYPE_INT)
			*target = expression.result.u.number.val.intval;
		else if (expression.result.u.number.ntype == NUMTYPE_FLOAT)
			*target = expression.result.u.number.val.fpval;
		else
			BUG("IllegalNumberType6", expression.result.u.number.ntype);
	} else if (expression.result.type == &type_string) {
		// accept single-char strings, to be more
		// compatible with versions before 0.97:
		if (expression.result.u.string->length != 1) {
			throw_error(exception_lengthnot1);
		} else {
			// FIXME - throw a warning?
		}
		string_to_byte(&(expression.result), 0);
		*target = expression.result.u.number.val.intval;
	} else {
		*target = 0;
		throw_error(exception_not_number);
	}
}


// stores int value and flags (floats are transformed to int)
// if result is empty or undefined, serious error is thrown
// OPEN_PARENTHESIS: complain
// EMPTY: complain _seriously_
// UNDEFINED: complain _seriously_
// FLOAT: convert to int
// FIXME - only very few callers actually _need_ a serious error to be thrown,
// so throw a normal one here and pass ok/fail as return value, so caller can react.
void ALU_defined_int(struct number *intresult)	// no ACCEPT constants?
{
	struct expression	expression;
	boolean			buf	= pass.flags.complain_about_undefined;

	pass.flags.complain_about_undefined = TRUE;
	parse_expression(&expression);	// FIXME - check return value and pass to caller!
	pass.flags.complain_about_undefined = buf;
/*
FIXME - that "buffer COMPLAIN status" thing no longer works: now that we have
lists, stuff like
	[2, 3, undefined][0]
or
	len([2,3,undefined])
throws errors even though the result is defined!
*/
	if (expression.open_parentheses)
		throw_error(exception_paren_open);
	if (expression.is_empty)
		throw_serious_error(exception_no_value);
	if (expression.result.type == &type_number) {
		if (expression.result.u.number.ntype == NUMTYPE_UNDEFINED) {
			throw_serious_error("Value not defined.");
			expression.result.u.number.val.intval = 0;
		} else if (expression.result.u.number.ntype == NUMTYPE_INT) {
			// ok
		} else if (expression.result.u.number.ntype == NUMTYPE_FLOAT) {
			float_to_int(&expression.result);
		} else {
			BUG("IllegalNumberType7", expression.result.u.number.ntype);
		}
	} else if (expression.result.type == &type_string) {
		// accept single-char strings, to be more
		// compatible with versions before 0.97:
		if (expression.result.u.string->length != 1) {
			throw_error(exception_lengthnot1);
		} else {
			// FIXME - throw a warning?
		}
		string_to_byte(&(expression.result), 0);
	} else {
		throw_serious_error(exception_not_number);
	}
	*intresult = expression.result.u.number;
}


// Store int value and flags.
// This function allows for "paren" '(' too many. Needed when parsing indirect
// addressing modes where internal indices have to be possible.
// For empty expressions, an error is thrown.
// OPEN_PARENTHESIS: depends on arg
// UNDEFINED: allow
// EMPTY: complain
// FLOAT: convert to int
void ALU_addrmode_int(struct expression *expression, int paren)	// ACCEPT_UNDEFINED | ACCEPT_OPENPARENTHESIS
{
	parse_expression(expression);	// FIXME - check return value and pass to caller!
	if (expression->result.type == &type_number) {
		// convert float to int
		if (expression->result.u.number.ntype == NUMTYPE_FLOAT)
			float_to_int(&(expression->result));
		else if (expression->result.u.number.ntype == NUMTYPE_UNDEFINED)
			expression->result.u.number.val.intval = 0;
	} else if (expression->result.type == &type_string) {
		// accept single-char strings, to be more
		// compatible with versions before 0.97:
		if (expression->result.u.string->length != 1) {
			throw_error(exception_lengthnot1);
		} else {
			// FIXME - throw a warning?
		}
		string_to_byte(&(expression->result), 0);
	} else {
		throw_error(exception_not_number);
	}
	if (expression->open_parentheses > paren) {
		expression->open_parentheses = 0;
		throw_error(exception_paren_open);
	}
	if (expression->is_empty)
		throw_error(exception_no_value);
}


// Store resulting object
// For empty expressions, an error is thrown.
// OPEN_PARENTHESIS: complain
// EMPTY: complain
// UNDEFINED: allow
// FLOAT: keep
void ALU_any_result(struct object *result)	// ACCEPT_UNDEFINED | ACCEPT_FLOAT
{
	struct expression	expression;

	parse_expression(&expression);	// FIXME - check return value and pass to caller!
	*result = expression.result;
	if (expression.open_parentheses)
		throw_error(exception_paren_open);
	if (expression.is_empty)
		throw_error(exception_no_value);
}


/* TODO

maybe move
	if (expression.is_empty)
		throw_error(exception_no_value);
to end of parse_expression()


// stores int value and flags, allowing for "paren" '(' too many (x-indirect addr).
void ALU_addrmode_int(struct expression *expression, int paren)
	mnemo.c
		when parsing addressing modes						needvalue!

// store resulting object
void ALU_any_result(struct object *result)
	macro.c
		macro call, when parsing call-by-value arg				don't care
	pseudoopcodes.c
		!set									don't care
		when throwing user-specified errors					don't care
		iterator for !by, !wo, etc.						needvalue!
		byte values in !raw, !tx, etc.						needvalue!
		!scrxor									needvalue!
	symbol.c
		explicit symbol definition						don't care

// stores int value and flags (floats are transformed to int)
// if result was undefined, serious error is thrown
void ALU_defined_int(struct number *intresult)
	flow.c
		when parsing loop conditions		make bool			serious
	pseudoopcodes.c
		*=					(FIXME, allow undefined)	needvalue!
		!initmem								error
		!fill (1st arg)				(maybe allow undefined?)	needvalue!
		!skip					(maybe allow undefined?)	needvalue!
		!align (1st + 2nd arg)			(maybe allow undefined?)	needvalue!
		!pseudopc				(FIXME, allow undefined)	needvalue!
		!if					make bool			serious
		twice in !for								serious
		twice for !binary			(maybe allow undefined?)	needvalue!
		//!enum

// store int value (0 if result was undefined)
void ALU_any_int(intval_t *target)
	pseudoopcodes.c
		!xor									needvalue!
		!fill (2nd arg)								needvalue!
		!align (3rd arg)							needvalue!
*/
