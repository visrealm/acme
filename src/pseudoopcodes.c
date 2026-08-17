// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2025 Marco Baye
// Have a look at "acme.c" for further info
//
// pseudo opcode stuff
#include "pseudoopcodes.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>	// for memcpy()
#include "config.h"
#include "cpu.h"
#include "alu.h"
#include "dynabuf.h"
#include "encoding.h"
#include "flow.h"
#include "input.h"
#include "macro.h"
#include "global.h"
#include "output.h"
#include "section.h"
#include "symbol.h"
#include "tree.h"
#include "typesystem.h"


// TODO - make new, keyword-based argument parser for !bin, !fill and !align
// helper function to support switching from positional arguments to keyword arguments
static boolean line_uses_keyword_arguments(void)
{
	// to be on the safe side, return FALSE if dialect < 0.98!
//	if (config.dialect < V0_98__PATHS_AND_SYMBOLCHANGE)
//		return FALSE;
//
//
	// read line to buffer, check if it begins with '[a-zA-Z]+='
	// change input to RAM, return result
	// ...and the caller needs to pass us some struct so it can change input back later on!
	return FALSE;
}


// different ways to handle end-of-statement:
enum eos {
	SKIP_REMAINDER,		// skip remainder of line - (after errors)
	ENSURE_EOS,		// make sure there's nothing left in statement
	PARSE_REMAINDER,	// parse what's left
	AT_EOS_ANYWAY		// actually, same as PARSE_REMAINDER
};

// constants
static const char	exception_want_IN_or_comma[]	= "Expected ',' or IN keyword after loop variable.";


// helper function, just for old, deprecated, obsolete, stupid "realpc":
static void end_all_pseudopc(void)
{
	while (pseudopc_isactive())
		pseudopc_end();
}


// "configuration" pseudo opcodes (can be overridden by cli arguments):


// define default value for empty memory ("!initmem" pseudo opcode)
static enum eos po_initmem(void)
{
	struct number	intresult;

	// ignore in later passes
	if (pass.number != 1)
		return SKIP_REMAINDER;

	// the "--initmem" cli arg and earlier calls have priority
	if (config.mem_init_value != NO_VALUE_GIVEN) {
		throw_warning("Memory already initialised.");
		return SKIP_REMAINDER;
	}

	// read value
	// (allowing undefined values in future versions does not make sense,
	// because all "configuration pseudo opcodes" should be skipped after
	// first pass)
	ALU_defined_int(&intresult);
	if ((intresult.val.intval > 255) || (intresult.val.intval < -128))
		throw_error(exception_number_out_of_8b_range);

	// remember value
	config.mem_init_value = intresult.val.intval & 0xff;

	return ENSURE_EOS;
}

// select output file name and format ("!to" pseudo opcode)
static enum eos po_to(void)
{
	enum outfile_format	format;

	// ignore in later passes
	if (pass.number != 1)
		return SKIP_REMAINDER;

	// the "--outfile" cli arg and earlier calls have priority
	if (config.output_filename) {
		throw_warning("Output file name already chosen.");
		return SKIP_REMAINDER;
	}

	// read filename to global dynamic buffer
	// if no file name given, exit (complaining will have been done)
	if (input_read_output_filename())
		return SKIP_REMAINDER;

	// get malloc'd copy of filename
	config.output_filename = dynabuf_get_copy(GlobalDynaBuf);

	// select output format
	if (parser_accept_comma()) {
		// parse output format name
		// if no keyword given, give up
		if (parser_read_and_lower_keyword() == 0)
			return SKIP_REMAINDER;	// "Expected keyword" error has already been thrown

		format = outputformat_find();
		if (format == OUTFILE_FORMAT_UNSPECIFIED) {
			throw_error("Unknown output format.");
			return SKIP_REMAINDER;
		}
	} else {
		// no comma: complain unless user requested really old behaviour
		if (config.dialect >= V0_86__DEPRECATE_REALPC) {
			throw_warning("Used \"!to\" without file format indicator.");
		} else {
			// older versions did not complain
		}
		// default to cbm
		format = OUTFILE_FORMAT_CBM;
	}

	// the "--format" cli arg has priority
	if (config.outfile_format != OUTFILE_FORMAT_UNSPECIFIED) {
		throw_warning("Output file format already chosen.");
	} else {
		config.outfile_format = format;
	}

	return ENSURE_EOS;
}

// set file name for symbol list
static enum eos po_symbollist(void)
{
	// ignore in later passes
	if (pass.number != 1)
		return SKIP_REMAINDER;

	// cli arg and earlier calls have priority
	if (config.symbollist_filename) {
		throw_warning("Symbol list file name already chosen.");
		return SKIP_REMAINDER;
	}

	// read filename to global dynamic buffer
	// if no file name given, exit (complaining will have been done)
	if (input_read_output_filename())
		return SKIP_REMAINDER;

	// get malloc'd copy of filename
	config.symbollist_filename = dynabuf_get_copy(GlobalDynaBuf);

	return ENSURE_EOS;
}


// set start and end (can also be overwritten by cli args):


// use current outbuf index as "first byte of output file"
static enum eos po_outfilestart(void)
{
	static int	last_pass_number	= -1;

	if ((config.outfile_start != NO_VALUE_GIVEN)
	|| (last_pass_number == pass.number)) {
		throw_pass1_warning("Start of output file already chosen.");
	} else {
		last_pass_number = pass.number;
		outbuf_set_outfile_start();
	}
	return ENSURE_EOS;
}

// use current outbuf index as "end+1 of output file"
static enum eos po_outfilelimit(void)
{
	static int	last_pass_number	= -1;

	if ((config.outfile_limit != NO_VALUE_GIVEN)
	|| (last_pass_number == pass.number)) {
		throw_pass1_warning("End of output file already chosen.");
	} else {
		last_pass_number = pass.number;
		outbuf_set_outfile_limit();
	}
	return ENSURE_EOS;
}


// "real" pseudo opcodes:


// change output "encryption" ("!xor" pseudo opcode)
// (allows for block, so must be reentrant)
static enum eos po_xor(void)
{
	char		old_value;
	intval_t	change;

	old_value = output_get_xor();
	ALU_any_int(&change);
	if ((change > 255) || (change < -128)) {
		throw_error(exception_number_out_of_8b_range);
		change = 0;
	}
	output_set_xor(old_value ^ change);
	// if there's a block, parse that and then restore old value!
	if (parse_optional_block())
		output_set_xor(old_value);
	return ENSURE_EOS;
}


// helper function for !8, !16, !24 and !32 pseudo opcodes
static enum eos iterate(void (*fn)(intval_t))
{
	struct iter_context	iter;
	struct object		object;

	iter.fn = fn;
	iter.accept_long_strings = FALSE;
	iter.stringxor = 0;
	do {
		ALU_any_result(&object);
		output_object(&object, &iter);
	} while (parser_accept_comma());
	return ENSURE_EOS;
}


// insert 8-bit values ("!8" / "!08" / "!by" / "!byte" pseudo opcode)
static enum eos po_byte(void)
{
	return iterate(output_8);
}


// Insert 16-bit values ("!16" / "!wo" / "!word" pseudo opcode)
static enum eos po_16(void)
{
	return iterate((cpu_current_type->flags & CPUFLAG_ISBIGENDIAN) ? output_be16 : output_le16);
}
// Insert 16-bit values big-endian ("!be16" pseudo opcode)
static enum eos po_be16(void)
{
	return iterate(output_be16);
}
// Insert 16-bit values little-endian ("!le16" pseudo opcode)
static enum eos po_le16(void)
{
	return iterate(output_le16);
}


// Insert 24-bit values ("!24" pseudo opcode)
static enum eos po_24(void)
{
	return iterate((cpu_current_type->flags & CPUFLAG_ISBIGENDIAN) ? output_be24 : output_le24);
}
// Insert 24-bit values big-endian ("!be24" pseudo opcode)
static enum eos po_be24(void)
{
	return iterate(output_be24);
}
// Insert 24-bit values little-endian ("!le24" pseudo opcode)
static enum eos po_le24(void)
{
	return iterate(output_le24);
}


// Insert 32-bit values ("!32" pseudo opcode)
static enum eos po_32(void)
{
	return iterate((cpu_current_type->flags & CPUFLAG_ISBIGENDIAN) ? output_be32 : output_le32);
}
// Insert 32-bit values big-endian ("!be32" pseudo opcode)
static enum eos po_be32(void)
{
	return iterate(output_be32);
}
// Insert 32-bit values little-endian ("!le32" pseudo opcode)
static enum eos po_le32(void)
{
	return iterate(output_le32);
}


// Insert bytes given as pairs of hex digits (helper for source code generators)
static enum eos po_hex(void)	// now GotByte = illegal char
{
	int		digits	= 0;
	unsigned char	byte	= 0;

	for (;;) {
		if (digits == 2) {
			output_byte(byte);
			digits = 0;
			byte = 0;
		}
		if (GotByte >= '0' && GotByte <= '9') {
			byte = (byte << 4) | (GotByte - '0');
			++digits;
			GetByte();
			continue;
		}
		if (GotByte >= 'a' && GotByte <= 'f') {
			byte = (byte << 4) | (GotByte - 'a' + 10);
			++digits;
			GetByte();
			continue;
		}
		if (GotByte >= 'A' && GotByte <= 'F') {
			byte = (byte << 4) | (GotByte - 'A' + 10);
			++digits;
			GetByte();
			continue;
		}
		// if we're here, the current character is not a hex digit,
		// which is only allowed outside of pairs:
		if (digits == 1) {
			throw_error("Hex digits are not given in pairs.");
			return SKIP_REMAINDER;	// error exit
		}
		switch (GotByte) {
		case ' ':
		case '\t':
			GetByte();	// spaces and tabs are ignored (maybe add commas, too?)
			continue;
		case CHAR_EOS:
			return AT_EOS_ANYWAY;	// normal exit
		default:
			throw_error(exception_syntax);	// FIXME - include character in error message!
			return SKIP_REMAINDER;	// error exit
		}
	}
}


// "!cbm" pseudo opcode (now obsolete)
static enum eos po_cbm(void)
{
	if (config.dialect >= V0_94_8__DISABLED_OBSOLETE) {
		throw_error("\"!cbm\" is obsolete; use \"!ct pet\" instead.");
	} else {
		encoder_current = &encoder_pet;
		throw_finalpass_warning("\"!cbm\" is deprecated; use \"!ct pet\" instead.");
	}
	return ENSURE_EOS;
}

// read encoding table from file
// (allows for block, so must be reentrant)
static enum eos use_encoding_from_file(void)
{
	struct filespecflags	flags;
	FILE			*stream;
	unsigned char		local_table[256],
				*buffered_table;
	const struct encoder	*buffered_encoder;

	// read file name and convert from UNIX style to platform style
	if (input_read_input_filename(&flags))
		return SKIP_REMAINDER;	// if missing or unterminated, give up

	// read from file
	stream = includepaths_open_ro(&flags);
	if (stream) {
		// try to load encoding table from given file
		if (fread(local_table, sizeof(char), 256, stream) != 256)
			throw_error("Conversion table incomplete.");
		fclose(stream);
	}

	// now switch encoding
	buffered_encoder = encoder_current;
	encoder_current = &encoder_file;
	buffered_table = encoding_loaded_table;
	encoding_loaded_table = local_table;
	// if there's a block, parse that and then restore old values
	if (parse_optional_block()) {
		encoder_current = buffered_encoder;
	} else {
		// if there's *no* block, the table must be used from now on.
		// copy the local table to the "outer" table
		memcpy(buffered_table, local_table, 256);
	}
	// re-activate "outer" table (it might have been changed by memcpy())
	encoding_loaded_table = buffered_table;
	return ENSURE_EOS;
}

// use one of the pre-defined encodings (raw, pet, scr)
// (allows for block, so must be reentrant)
static enum eos use_predefined_encoding(const struct encoder *new_encoder)
{
	unsigned char		local_table[256],
				*buffered_table;
	const struct encoder	*buffered_encoder;

	// switch encoding
	buffered_encoder = encoder_current;
	encoder_current = new_encoder;
	buffered_table = encoding_loaded_table;
	encoding_loaded_table = local_table;
	// if there's a block, parse that and then restore old values
	if (parse_optional_block())
		encoder_current = buffered_encoder;
	// re-activate "outer" table
	encoding_loaded_table = buffered_table;
	return ENSURE_EOS;
}

// set current encoding ("!convtab" pseudo opcode)
// (allows for block, so must be reentrant)
static enum eos po_convtab(void)
{
	// is file name given old-style, without "file" keyword?
	if ((GotByte == '<') || (GotByte == '"'))
		return use_encoding_from_file();

	// expect keyword: either one of the pre-defined encodings or
	// "file" with a filename argument:
	if (parser_read_and_lower_keyword() == 0)
		return SKIP_REMAINDER;	// "Expected keyword" error has already been thrown

	// now check for known keywords:
	if (strcmp(GlobalDynaBuf->buffer, "pet") == 0)
		return use_predefined_encoding(&encoder_pet);

	if (strcmp(GlobalDynaBuf->buffer, "raw") == 0)
		return use_predefined_encoding(&encoder_raw);

	if (strcmp(GlobalDynaBuf->buffer, "scr") == 0)
		return use_predefined_encoding(&encoder_scr);

	if (strcmp(GlobalDynaBuf->buffer, "file") == 0) {
		SKIPSPACE();
		if (!parser_expect('=')) {
			return SKIP_REMAINDER;
		}
		return use_encoding_from_file();
	}

	throw_error("Unknown encoding.");
	return use_predefined_encoding(encoder_current);	// keep going
}
// insert string(s)
static enum eos encode_string(const struct encoder *inner_encoder, unsigned char xor)
{
	const struct encoder	*outer_encoder	= encoder_current;	// buffer encoder
	struct iter_context	iter;
	struct object		object;
	int			offset;

	iter.fn = output_8;
	iter.accept_long_strings = TRUE;
	iter.stringxor = xor;
	// make given encoder the current one (for ALU-parsed values of character codes)
	encoder_current = inner_encoder;
	// algo depends on dialect:
	if (config.dialect >= V0_97__BACKSLASH_ESCAPING) {
		// since v0.97 the expression parser supports strings, so we pass all args to it:
		do {
			ALU_any_result(&object);
			output_object(&object, &iter);
		} while (parser_accept_comma());
	} else {
		// in older versions the expression parser complained about
		// strings longer than one character, so we assumed everything
		// starting with a doublequote is a string literal and handled
		// those separately:
		do {
			if (GotByte == '"') {
				// string literal
				if (input_read_string_literal('"'))
					break;	// unterminated
				// send characters
				for (offset = 0; offset < GlobalDynaBuf->size; ++offset)
					output_8(xor ^ encoding_encode_char(GLOBALDYNABUF_CURRENT[offset]));
			} else {
				// everything else is passed to expression parser:
				ALU_any_result(&object);
				output_object(&object, &iter);
			}
		} while (parser_accept_comma());
	}
	// reactivate buffered encoder:
	encoder_current = outer_encoder;
	return ENSURE_EOS;
}
// insert text string (default format)
static enum eos po_text(void)
{
	return encode_string(encoder_current, 0);
}
// insert raw string
static enum eos po_raw(void)
{
	return encode_string(&encoder_raw, 0);
}
// insert PetSCII string
static enum eos po_pet(void)
{
	return encode_string(&encoder_pet, 0);
}
// insert screencode string
static enum eos po_scr(void)
{
	return encode_string(&encoder_scr, 0);
}
// insert screencode string, XOR'd
static enum eos po_scrxor(void)
{
	intval_t	xor;

	ALU_any_int(&xor);
	if (parser_expect(',')) {
		return encode_string(&encoder_scr, xor);
	}
	return SKIP_REMAINDER;
}

// "backend" function for "!binary"
static void include_file(FILE *stream, intval_t size, intval_t offset)
{
	int	byte,
		amount;

	// check whether including is a waste of time
	// FIXME - future changes ("several-projects-at-once")
	// may be incompatible with this!
	if ((size >= 0) && (!pass.flags.generate_output)) {
		// no need to read actual data from file, so use zeroes instead.
		// do not call output_skip()! we want this area to be included
		// when calculating outbuf size, even if at start/end!
		while (size != 0) {
			output_byte(0);
			--size;
		}
		return;
	}

	// really insert file
	fseek(stream, offset, SEEK_SET);	// set read pointer
	// read "size" bytes. if size is "NO_VALUE_GIVEN" (which is -1),
	// decrementing it won't give zero, so we'll read until EOF,
	// which is exactly what we want if no size was given:
	while (size != 0) {
		byte = getc(stream);
		if (byte == EOF)
			break;
		output_byte(byte);
		--size;
	}
	// if more should have been read, warn and add padding
	if (size > 0) {
		throw_warning("Padding with zeroes.");
		do {
			output_byte(0);
		} while (--size);
	}
	// if verbose, produce some output
	if (config.process_verbosity >= 2) {
		amount = output_get_statement_size();
		printf("Loaded %d (0x%04x) bytes from file offset %d (0x%04x).\n",
			amount, amount, offset, offset);
	}
}
// include binary file ("!binary" pseudo opcode)
static enum eos po_binary(void)
{
	struct filespecflags	flags;
	FILE		*stream;
	struct number	arg;
	intval_t	size	= NO_VALUE_GIVEN,	// means "until EOF"
			skip	= 0;

	// read file name and convert from UNIX style to platform style
	if (input_read_input_filename(&flags))
		return SKIP_REMAINDER;	// if missing or unterminated, give up

	// try to open file
	stream = includepaths_open_ro(&flags);
	if (stream == NULL)
		return SKIP_REMAINDER;

	// any more arguments?
	if (parser_accept_comma()) {
		if (line_uses_keyword_arguments()) {
			// read keyword arguments
			// TODO!
			BUG("KeywordArgsNotYet", 0);
		} else {
			// read old-style, positional arguments:
			if ((GotByte != ',') && (GotByte != CHAR_EOS)) {
				// first arg is "size"
				ALU_defined_int(&arg);
				size = arg.val.intval;
				// CAUTION: do not move this check elsewhere,
				// because the default value -1 is ok!
				if (size < 0)
					throw_serious_error(exception_negative_size);
			}
			// more?
			if (parser_accept_comma()) {
				// second arg is "skip"
				if (GotByte != CHAR_EOS) {
					// then parse it
					ALU_defined_int(&arg);
					skip = arg.val.intval;
				}
			}
		}
	}
	// FIXME - add check for "skip" and complain if negative!
	include_file(stream, size, skip);
	fclose(stream);
	return ENSURE_EOS;
}


// reserve space by sending bytes of given value ("!fi" / "!fill" pseudo opcode)
static enum eos po_fill(void)
{
	struct number	sizeresult;
	intval_t	fill	= FILLVALUE_FILL;

	if (line_uses_keyword_arguments()) {
		// read keyword arguments
		// TODO!
		BUG("KeywordArgsNotYet", 1);
	} else {
		// read old-style, positional arguments:
		ALU_defined_int(&sizeresult);	// FIXME - forbid addresses!
		if (parser_accept_comma())
			ALU_any_int(&fill);	// FIXME - forbid addresses!
	}

	while (sizeresult.val.intval--)
		output_8(fill);
	return ENSURE_EOS;
}


// skip over some bytes in output without starting a new segment.
// in contrast to "*=*+AMOUNT", "!skip AMOUNT" does not start a new segment.
static enum eos po_skip(void)	// now GotByte = illegal char
{
	struct number	amount;

	ALU_defined_int(&amount);	// FIXME - forbid addresses!
	if (amount.val.intval < 0)
		throw_serious_error(exception_negative_size);	// TODO - allow this?
	else
		output_skip(amount.val.intval);
	return ENSURE_EOS;
}


// insert byte until PC fits condition
// TODO:
// now: "!align ANDVALUE, EQUALVALUE [,FILLVALUE]"
// in future: use keyword arguments so "!align fillvalue=17, blocksize=64" is
// possible (where blocksize must be a power of two)
static enum eos po_align(void)
{
	struct number	arg;
	intval_t	andresult,
			equalresult,
			fill	= cpu_current_type->default_align_value;
	struct number	pc;

	if (line_uses_keyword_arguments()) {
		// read keyword arguments
		// TODO!
		BUG("KeywordArgsNotYet", 2);
	} else {
		// read old-style, positional arguments:
		ALU_defined_int(&arg);	// FIXME - forbid addresses!
		andresult = arg.val.intval;
		if (parser_expect(',')) {
			// fn already does everything for us
		}
		ALU_defined_int(&arg);	// ...allow addresses (unlikely, but possible)
		equalresult = arg.val.intval;
		if (parser_accept_comma())
			ALU_any_int(&fill);
	}

	// make sure PC is defined
	programcounter_read_pc(&pc);
	if (pc.ntype == NUMTYPE_UNDEFINED) {
		throw_error(exception_pc_undefined);
		return SKIP_REMAINDER;
	}

	while ((pc.val.intval++ & andresult) != equalresult)
		output_8(fill);
	return ENSURE_EOS;
}


// not using a block is no longer allowed
static void old_offset_assembly(void)
{
	if (config.dialect >= V0_94_8__DISABLED_OBSOLETE) {
		// now it's obsolete
		throw_error("\"!pseudopc/!realpc\" are obsolete; use \"!pseudopc {}\" instead.");	// FIXME - amend msg, tell user how to use old behaviour!
	} else if (config.dialect >= V0_86__DEPRECATE_REALPC) {
		// earlier it was deprecated
		throw_finalpass_warning("\"!pseudopc/!realpc\" are deprecated; use \"!pseudopc {}\" instead.");
	} else {
		// really old versions allowed it
	}
}

// start offset assembly
// (allows for block, so must be reentrant)
static enum eos po_pseudopc(void)
{
	struct number	new_pc;

	// get new value
	ALU_defined_int(&new_pc);	// complaining about non-addresses would be logical, but annoying
/* TODO - add this. check if code can be shared with "*="!
	// check for modifiers
	while (parser_accept_comma()) {
		// parse modifier. if no keyword given, give up
		if (parser_read_and_lower_keyword() == 0)
			return SKIP_REMAINDER;	// "Expected keyword" error has already been thrown

		if (strcmp(GlobalDynaBuf->buffer, "limit") == 0) {
			skip '='
			read memory limit
		} else if (strcmp(GlobalDynaBuf->buffer, "name") == 0) {
			skip '='
			read segment name (quoted string!)
		} else {
			throw_error("Unknown !pseudopc segment modifier.");
			return SKIP_REMAINDER;
		}
	}
*/
	if (new_pc.val.intval < 0) {
		throw_error("Program counter cannot be negative.");
		new_pc.val.intval = cpu_current_type->dummy_pc;
	}
	pseudopc_start(&new_pc);
	// if there's a block, parse that and then restore old value!
	if (parse_optional_block()) {
		// restore old state
		if (pseudopc_isactive()) {
			pseudopc_end();
		} else {
			// calling pseudopc_end() here would create a segfault.
			// the only way this point can be reached is when the user
			// a) asked for an older dialect where "*=" disabled "!pseudopc"
			// and
			// b) did exactly that in the source.
			//
			// ...*or* it is a bug! maybe set a flag in the case above which
			// can then be checked here, and if it isn't set, call BUG()?
		}
	} else {
		old_offset_assembly();
	}
	return ENSURE_EOS;
}


// "!realpc" pseudo opcode (now obsolete)
static enum eos po_realpc(void)
{
	old_offset_assembly();
	end_all_pseudopc();	// restore outermost state
	return ENSURE_EOS;
}


// select CPU ("!cpu" pseudo opcode)
// (allows for block, so must be reentrant)
static enum eos po_cpu(void)
{
	const struct cpu_type	*cpu_buffer	= cpu_current_type;	// remember
	const struct cpu_type	*new_cpu_type;

	if (parser_read_and_lower_keyword()) {
		new_cpu_type = cputype_find();
		if (new_cpu_type)
			cpu_current_type = new_cpu_type;	// activate new cpu type
		else
			throw_error("Unknown processor.");
	}
	// if there's a block, parse that and then restore old value
	if (parse_optional_block())
		cpu_current_type = cpu_buffer;
	return ENSURE_EOS;
}


// set register length, block-wise if needed.
// (allows for block, so must be reentrant)
static enum eos set_register_length(boolean *var, boolean make_long)
{
	boolean	long_before	= *var;

	// set new register length (or complain - whichever is more fitting)
	vcpu_check_and_set_reg_length(var, make_long);
	// if there's a block, parse that and then restore old value!
	if (parse_optional_block())
		vcpu_check_and_set_reg_length(var, long_before);	// restore old length
	return ENSURE_EOS;
}
// switch to long accumulator ("!al" pseudo opcode)
static enum eos po_al(void)
{
	return set_register_length(&cpu_a_is_long, TRUE);
}
// switch to short accumulator ("!as" pseudo opcode)
static enum eos po_as(void)
{
	return set_register_length(&cpu_a_is_long, FALSE);
}
// switch to long index registers ("!rl" pseudo opcode)
static enum eos po_rl(void)
{
	return set_register_length(&cpu_xy_are_long, TRUE);
}
// switch to short index registers ("!rs" pseudo opcode)
static enum eos po_rs(void)
{
	return set_register_length(&cpu_xy_are_long, FALSE);
}


#if 0
// enumerate constants ("!enum")
static enum eos po_enum(void)	// now GotByte = illegal char
{
	struct number	step;

	step.val.intval = 1;
	ALU_defined_int(&step);
throw_serious_error("Not yet");	// FIXME
	return ENSURE_EOS;
}
#endif


// (re)set symbol
static enum eos po_set(void)	// now GotByte = illegal char
{
	scope_t	scope;
	int	force_bit;

	if (input_read_scope_and_symbol_name(&scope))	// skips spaces before
		return SKIP_REMAINDER;	// zero length

	force_bit = parser_get_force_bit();	// skips spaces after
	if (!parser_expect('='))
		return SKIP_REMAINDER;

	// TODO: in versions before 0.97, force bit handling was broken in both
	// "!set" and "!for":
	// trying to change a force bit raised an error (which is correct), but
	// in any case, ALL FORCE BITS WERE CLEARED in symbol. only cases like
	// !set N=N+1 worked, because the force bit was taken from result.
	// maybe support this behaviour via --dialect? I'd rather not...
	parse_assignment(scope, force_bit, POWER_CHANGE_VALUE | POWER_CHANGE_OBJTYPE);
	return ENSURE_EOS;
}


// switch to new zone ("!zone" or "!zn"). has to be re-entrant.
// (allows for block, so must be reentrant)
static enum eos po_zone(void)
{
	struct section	entry_values;	// buffer for outer zone
	char		*new_title;
	int		allocated;

	// remember everything about current structure
	entry_values = *section_now;
	// set default values in case there is no valid title
	new_title = s_untitled;
	allocated = FALSE;
	// check whether a zone title is given. if yes and it can be read,
	// get copy, remember pointer and remember to free it later on.
	if (BYTE_CONTINUES_KEYWORD(GotByte)) {
		// because we know of one character for sure,
		// there's no need to check the return value.
		parser_read_keyword();
		new_title = dynabuf_get_copy(GlobalDynaBuf);
		allocated = TRUE;
	}
	// setup new section
	// section type is "subzone", just in case a block follows
	section_new(section_now, "Subzone", new_title, allocated);
	if (parse_optional_block()) {
		// block has been parsed, so it was a SUBzone.
		section_finalize(section_now);	// end inner zone
		*section_now = entry_values;	// restore entry values
	} else {
		// no block found, so it's a normal zone change
		section_finalize(&entry_values);	// end outer zone
		section_now->type = "Zone";	// fix type
	}
	return ENSURE_EOS;
}

// "!subzone" or "!sz" pseudo opcode (now obsolete)
static enum eos po_subzone(void)
{
	if (config.dialect >= V0_94_8__DISABLED_OBSOLETE) {
		throw_error("\"!subzone {}\" is obsolete; use \"!zone {}\" instead.");
	} else {
		throw_finalpass_warning("\"!subzone {}\" is deprecated; use \"!zone {}\" instead.");
	}
	// call "!zone" instead
	return po_zone();
}

// include source file ("!source" or "!src"). has to be re-entrant.
static enum eos po_source(void)	// now GotByte = illegal char
{
	struct filespecflags	flags;
	FILE		*stream;
	const char	*eternal_plat_filename;

	// read file name and convert from UNIX style to platform style
	if (input_read_input_filename(&flags))
		return SKIP_REMAINDER;	// if missing or unterminated, give up

	// enter new nesting level
	// quit program if recursion too deep
	if (--sanity.source_recursions_left < 0)
		throw_serious_error("Too deeply nested. Recursive \"!source\"?");
	// if file could be opened, parse it. otherwise, complain
	stream = includepaths_open_ro(&flags);
	if (stream) {
		eternal_plat_filename = dynabuf_get_copy(GlobalDynaBuf);
		parse_source_code_file(stream, eternal_plat_filename);
		fclose(stream);
	}
	// leave nesting level
	++sanity.source_recursions_left;
	return ENSURE_EOS;
}

// if/ifdef/ifndef/else
enum ifmode {
	IFMODE_IF,	// parse expression, then block
	IFMODE_IFDEF,	// check symbol, then parse block or line
	IFMODE_IFNDEF,	// check symbol, then parse block or line
	IFMODE_ELSE	// unconditional last block
};
// has to be re-entrant
static enum eos ifelse(enum ifmode mode)
{
	boolean		skip_the_rest	= FALSE;	// once a block gets executed, this becomes TRUE, so all others will be skipped even if condition met
	boolean		condition_met;	// condition result for next block
	struct number	ifresult;

	for (;;) {
		// check condition according to mode
		switch (mode) {
		case IFMODE_IF:
			if (skip_the_rest) {
				// parsing the condition might have side effects
				// we do not want, so just skip it:
				input_read_statement(CHAR_SOB);
				//condition_met = does not matter because of "skip_the_rest"
			} else {
				// we need to parse the condition, and we need a
				// defined result to know how to go on:
				ALU_defined_int(&ifresult);
				condition_met = !!ifresult.val.intval;
			}
			if (GotByte != CHAR_SOB)
				throw_serious_error(exception_no_left_brace);
			break;
		case IFMODE_IFDEF:
			condition_met = check_ifdef_condition();
			break;
		case IFMODE_IFNDEF:
			condition_met = !check_ifdef_condition();
			break;
		case IFMODE_ELSE:
			condition_met = TRUE;
			break;
		default:
			BUG("IllegalIfMode", mode);
			condition_met = TRUE;	// inhibit compiler warning ;)
		}
		SKIPSPACE();
		// execute this block?
		if (condition_met && !skip_the_rest) {
			skip_the_rest = TRUE;	// we execute this block, so skip all others
			if (GotByte == CHAR_SOB) {
		                parse_until_eob_or_eof();	// parse block
        		        // if block isn't correctly terminated, complain and exit
                		if (GotByte != CHAR_EOB)
                		        throw_serious_error(exception_no_right_brace);
			} else {
				return PARSE_REMAINDER;	// parse line (only for ifdef/ifndef)
			}
		} else {
			if (GotByte == CHAR_SOB) {
				input_block_skip();	// skip block
			} else {
				return SKIP_REMAINDER;	// skip line (only for ifdef/ifndef)
			}
		}
		// now GotByte = '}'
		NEXTANDSKIPSPACE();
		// after ELSE {} it's all over. it must be.
		if (mode == IFMODE_ELSE) {
			// we could just return ENSURE_EOS, but checking here allows for better error message
			if (GotByte != CHAR_EOS)
				throw_error("Expected end-of-statement after ELSE block.");
			return SKIP_REMAINDER;	// normal exit after ELSE {...}
		}

		// anything more?
		if (GotByte == CHAR_EOS)
			return AT_EOS_ANYWAY;	// normal exit if there is no ELSE {...} block

		// read keyword (expected to be "elif" or "else")
		if (parser_read_and_lower_keyword() == 0)
			return SKIP_REMAINDER;	// "Expected keyword" error -> ignore rest of line

		// if it's "elif", we can just do another iteration:
		if (strcmp(GlobalDynaBuf->buffer, "elif") == 0) {
			mode = IFMODE_IF;
			continue;
		}

		// make sure it's "else"
		if (strcmp(GlobalDynaBuf->buffer, "else")) {
			throw_error("Expected end-of-statement or ELIF/ELSE keyword after '}'.");
			return SKIP_REMAINDER;	// an error has been reported, so ignore rest of line
		}
		// anything more?
		SKIPSPACE();
		if (GotByte == CHAR_SOB) {
			// ELSE {...} -> one last round
			mode = IFMODE_ELSE;
			continue;
		}

		// read keyword after else (expected to be if/ifdef/ifndef)
		if (parser_read_and_lower_keyword() == 0)
			return SKIP_REMAINDER;	// "Expected keyword" error -> ignore rest of line

		// which one is it?
		if (strcmp(GlobalDynaBuf->buffer, "if") == 0) {
			mode = IFMODE_IF;
		} else if (strcmp(GlobalDynaBuf->buffer, "ifdef") == 0) {
			mode = IFMODE_IFDEF;
		} else if (strcmp(GlobalDynaBuf->buffer, "ifndef") == 0) {
			mode = IFMODE_IFNDEF;
		} else {
			throw_error("Expected '{' or IF/IFDEF/IFNDEF keyword after ELSE keyword.");
			return SKIP_REMAINDER;	// an error has been reported, so ignore rest of line
		}
	}
}

// conditional assembly ("!if"). has to be re-entrant.
static enum eos po_if(void)	// now GotByte = illegal char
{
	return ifelse(IFMODE_IF);
}


// conditional assembly ("!ifdef"). has to be re-entrant.
static enum eos po_ifdef(void)	// now GotByte = illegal char
{
	return ifelse(IFMODE_IFDEF);
}


// conditional assembly ("!ifndef"). has to be re-entrant.
static enum eos po_ifndef(void)	// now GotByte = illegal char
{
	return ifelse(IFMODE_IFNDEF);
}


// looping assembly ("!for"). has to be re-entrant.
// old counter syntax: !for VAR, END { BLOCK }		VAR counts from 1 to END
// new counter syntax: !for VAR, START, END { BLOCK }	VAR counts from START to END
// iterating syntax: !for VAR in ITERABLE { BLOCK }	VAR iterates over string/list contents
static enum eos po_for(void)	// now GotByte = illegal char
{
	scope_t		scope;
	bits		force_bit;
	struct for_loop	loop;
	struct number	intresult;

	if (input_read_scope_and_symbol_name(&scope))	// skips spaces before
		return SKIP_REMAINDER;	// zero length

	// now GotByte = illegal char
	force_bit = parser_get_force_bit();	// skips spaces after
	loop.symbol = symbol_find(scope);	// if not number, error will be reported on first assignment
	if (parser_accept_comma()) {
		// counter syntax (old or new)
		loop.u.counter.force_bit = force_bit;
		ALU_defined_int(&intresult);	// read first argument
		loop.u.counter.addr_refs = intresult.addr_refs;
		if (parser_accept_comma()) {
			// new counter syntax
			loop.algorithm = FORALGO_NEWCOUNT;
			if (config.dialect >= V0_94_12__NEW_FOR_SYNTAX) {
				// version 0.94.12 introduced the new syntax,
			} else {
				// so if user chooses an older dialect, warn about the new syntax:
				throw_finalpass_warning("Found new \"!for\" syntax.");
			}
			loop.u.counter.first = intresult.val.intval;	// use first argument
			ALU_defined_int(&intresult);	// read second argument
			// compare addr_ref counts and complain if not equal!
			if (config.warn_on_type_mismatch
			&& (intresult.addr_refs != loop.u.counter.addr_refs)) {
				throw_finalpass_warning("Wrong type for loop's END value - must match type of START value.");
			}
			// setup direction and total
			if (loop.u.counter.first <= intresult.val.intval) {
				// count up
				loop.iterations_left = 1 + intresult.val.intval - loop.u.counter.first;
				loop.u.counter.increment = 1;
			} else {
				// count down
				loop.iterations_left = 1 + loop.u.counter.first - intresult.val.intval;
				loop.u.counter.increment = -1;
			}
		} else {
			// old counter syntax
			loop.algorithm = FORALGO_OLDCOUNT;
			if (config.dialect >= V0_94_12__NEW_FOR_SYNTAX) {
				// version 0.94.12 introduced the new syntax, so warn when encountering the old one:
				throw_finalpass_warning("Found old \"!for\" syntax.");
			}
			if (intresult.val.intval < 0) {
				throw_serious_error("Loop count is negative.");
			}
			// count up
			loop.u.counter.first = 1;
			loop.iterations_left = intresult.val.intval;	// use given argument
			loop.u.counter.increment = 1;
		}
	} else {
		// iterator syntax
		loop.algorithm = FORALGO_ITERATE;
		// check for "in" keyword
		if ((GotByte != 'i') && (GotByte != 'I')) {
			throw_error(exception_want_IN_or_comma);
			return SKIP_REMAINDER;	// FIXME - this ignores '{' and will then complain about '}'
		}
/* checking for the first character explicitly here looks dumb, but actually
serves a purpose: we're here because the check for comma failed, but maybe that
was just a typo. if the current byte is '.' or '-' or whatever, then trying to
read a keyword will result in "Expected keyword" - which is confusing for the
user if they did not even want to put a keyword there.
so if the current byte is not the start of "in" we just throw a syntax error.
knowing there is an "i" also makes sure that parser_read_and_lower_keyword()
does not fail. */
		parser_read_and_lower_keyword();
		if (strcmp(GlobalDynaBuf->buffer, "in") != 0) {
			throw_error(exception_want_IN_or_comma);
			return SKIP_REMAINDER;	// FIXME - this ignores '{' and will then complain about '}'
		}
		if (force_bit) {
			throw_error("Force bits can only be given to counters, not when iterating over string/list contents.");
			return SKIP_REMAINDER;	// FIXME - this ignores '{' and will then complain about '}'
		}
		ALU_any_result(&loop.u.iter.obj);	// get iterable
		loop.iterations_left = loop.u.iter.obj.type->length(&loop.u.iter.obj);
		if (loop.iterations_left < 0) {
			throw_error("Given object is not iterable.");
			return SKIP_REMAINDER;	// FIXME - this ignores '{' and will then complain about '}'
		}
	}

	if (GotByte != CHAR_SOB)
		throw_serious_error(exception_no_left_brace);

	input_block_getcopy(&loop.block);	// "body" must be freed afterward...
	flow_forloop(&loop);
	free(loop.block.body);	// ...so free it 

	// GotByte is '}'
	GetByte();	// fetch next byte
	return ENSURE_EOS;
}


// looping assembly ("!do"). has to be re-entrant.
static enum eos po_do(void)	// now GotByte = illegal char
{
	struct do_while	loop;

	// read head condition to buffer
	SKIPSPACE();
	flow_store_doloop_condition(&loop.head_cond, CHAR_SOB);	// must be freed!
	if (GotByte != CHAR_SOB)
		throw_serious_error(exception_no_left_brace);

	input_block_getcopy(&loop.block);	// "body" must be freed!
	// now GotByte = '}'
	NEXTANDSKIPSPACE();	// now GotByte = first non-blank char after block
	// read tail condition to buffer
	flow_store_doloop_condition(&loop.tail_cond, CHAR_EOS);	// must be freed!
	// now GotByte = CHAR_EOS
	flow_do_while(&loop);
	// free memory
	free(loop.tail_cond.block.body);
	free(loop.block.body);
	free(loop.head_cond.block.body);
	return AT_EOS_ANYWAY;
}


// looping assembly ("!while", alternative for people used to c-style loops). has to be re-entrant.
static enum eos po_while(void)	// now GotByte = illegal char
{
	struct do_while	loop;

	// read condition to buffer
	SKIPSPACE();
	flow_store_while_condition(&loop.head_cond);	// "body" must be freed!
	if (GotByte != CHAR_SOB)
		throw_serious_error(exception_no_left_brace);
	// read block
	input_block_getcopy(&loop.block);	// "body" must be freed!
	// clear tail condition
	loop.tail_cond.block.body = NULL;
	flow_do_while(&loop);
	// free memory
	free(loop.block.body);
	free(loop.head_cond.block.body);
	GetByte();	// fetch next byte (last byte read was '}')
	return ENSURE_EOS;
}


// macro definition ("!macro").
static enum eos po_macro(void)	// now GotByte = illegal char
{
	// in first pass, parse. In all other passes, skip.
	if (pass.number == 1) {
		macro_parse_definition();	// now GotByte = '}'
	} else {
		// skip until CHAR_SOB ('{') is found.
		// no need to check for end-of-statement, because such an
		// error would already have been detected in first pass.
		// for the same reason, there is no need to check for quotes.
		while (GotByte != CHAR_SOB)
			GetByte();
		input_block_skip();	// now GotByte = '}'
	}
	GetByte();	// Proceed with next character
	return ENSURE_EOS;
}

/*
// trace/watch
#define TRACEWATCH_LOAD		(1u << 0)
#define TRACEWATCH_STORE	(1u << 1)
#define TRACEWATCH_EXEC		(1u << 2)
#define TRACEWATCH_DEFAULT	(TRACEWATCH_LOAD | TRACEWATCH_STORE | TRACEWATCH_EXEC)
#define TRACEWATCH_BREAK	(1u << 3)
static enum eos tracewatch(boolean enter_monitor)
{
	struct number	pc;
	bits		flags	= 0;

	programcounter_read(&pc);
	SKIPSPACE();
	// check for flags
	if (GotByte != CHAR_EOS) {
		do {
			// parse flag. if no keyword given, give up
			if (parser_read_and_lower_keyword() == 0)
				return SKIP_REMAINDER;	// "Expected keyword" error has already been thrown

			if (strcmp(GlobalDynaBuf->buffer, "load") == 0) {
				flags |= TRACEWATCH_LOAD;
			} else if (strcmp(GlobalDynaBuf->buffer, "store") == 0) {
				flags |= TRACEWATCH_STORE;
			} else if (strcmp(GlobalDynaBuf->buffer, "exec") == 0) {
				flags |= TRACEWATCH_EXEC;
			} else {
				throw_error("Unknown flag (known are: load, store, exec).");	// FIXME - add to docs!
				return SKIP_REMAINDER;
			}
		} while (parser_accept_comma());
	}
	// shortcut: no flags at all -> set all flags!
	if (!flags)
		flags = TRACEWATCH_DEFAULT;
	if (enter_monitor)
		flags |= TRACEWATCH_BREAK;
	if (pc.ntype != NUMTYPE_UNDEFINED) {
		//FIXME - store pc and flags!
	}
	return ENSURE_EOS;
}
// make next byte a trace point (for VICE debugging)
static enum eos po_trace(void)
{
	return tracewatch(FALSE);	// do not enter monitor, just output
}
// make next byte a watch point (for VICE debugging)
static enum eos po_watch(void)
{
	return tracewatch(TRUE);	// break into monitor
}
*/

// force explicit symbol definitions to set "address" flag.
// if this is used without a block, it is only a prefix for a single statement.
// has to be re-entrant.
static enum eos po_address(void)	// now GotByte = illegal char
{
	boolean	buf;

	buf = parser_change_addr_block_flag(TRUE);
	if (parse_optional_block()) {
		// when used with a block, is used throughout the block
		parser_change_addr_block_flag(buf);	// restore after block
		return ENSURE_EOS;
	} else {
		// without a block, it's only a "prefix" for the current statement
		parser_change_addr_block_flag(buf);	// there was no block, so restore...
		parser_set_addr_prefix();	// ...and set flag...
		return PARSE_REMAINDER;	// ...just for this statement.
	}
}


// disable warnings.
// if this is used without a block, it is only a prefix for a single statement.
// has to be re-entrant.
static enum eos po_nowarn(void)	// now GotByte = illegal char
{
	boolean	buf;

	buf = parser_change_nowarn_block_flag(TRUE);
	if (parse_optional_block()) {
		// when used with a block, is used throughout the block
		parser_change_nowarn_block_flag(buf);	// restore after block
		return ENSURE_EOS;
	} else {
		// without a block, it's only a "prefix" for the current statement
		parser_change_nowarn_block_flag(buf);	// there was no block, so restore...
		parser_set_nowarn_prefix();	// ...and set flag...
		return PARSE_REMAINDER;	// ...just for this statement.
	}
}


// exit innermost loop
static enum eos po_break(void)	// now GotByte = illegal char
{
	boolean	allowed;

	allowed = parser_allow_break_cont(FALSE);
	parser_allow_break_cont(allowed);
	if (allowed)
		parser_set_shortcut(SHORTCUT_BREAK);
	else
		throw_error("!break not within a loop.");
	return ENSURE_EOS;
}

// end innermost loop iteration
static enum eos po_continue(void)	// now GotByte = illegal char
{
	boolean	allowed;

	allowed = parser_allow_break_cont(FALSE);
	parser_allow_break_cont(allowed);
	if (allowed)
		parser_set_shortcut(SHORTCUT_CONT);
	else
		throw_error("!continue not within a loop.");
	return ENSURE_EOS;
}

// return from innermost macro
static enum eos po_return(void)	// now GotByte = illegal char
{
	boolean	allowed;

	// if ACME ever gets real functions, this is the place to parse the
	// return value and store it via some innermost_function->result
	// pointer.
	allowed = parser_allow_return(FALSE);
	parser_allow_return(allowed);
	if (allowed)
		parser_set_shortcut(SHORTCUT_RETURN);
	else
		throw_error("!return not within a macro.");
	return ENSURE_EOS;
}


// variables
static	STRUCT_DYNABUF_REF(user_message, 80);	// for !debug/info/warn/error/serious


// helper function to show user-defined messages
static enum eos throw_src_string(enum debuglevel level, const char prefix[])
{
	struct object	object;

	dynabuf_clear(user_message);
	dynabuf_add_string(user_message, prefix);
	// algo depends on dialect:
	if (config.dialect >= V0_97__BACKSLASH_ESCAPING) {
		// since v0.97 the expression parser supports strings, so we pass all args to it:
		do {
			ALU_any_result(&object);
			object.type->print(&object, user_message);
		} while (parser_accept_comma());
	} else {
		// in older versions the expression parser complained about
		// strings longer than one character, so we assumed everything
		// starting with a doublequote is a string literal and handled
		// those separately:
		do {
			if (GotByte == '"') {
				// string literal
				if (input_read_string_literal('"'))
					break;	// unterminated
				dynabuf_append(GlobalDynaBuf, '\0');	// terminate string
				dynabuf_add_string(user_message, GLOBALDYNABUF_CURRENT);	// add to message
			} else {
				// everything else is passed to expression parser:
				ALU_any_result(&object);
				object.type->print(&object, user_message);
			}
		} while (parser_accept_comma());
	}
	dynabuf_append(user_message, '\0');
	throw_message(level, user_message->buffer, NULL);
	return ENSURE_EOS;
}

// show debug data as given in source code
static enum eos po_debug(void)
{
	struct number	debuglevel;

	ALU_defined_int(&debuglevel);
	if (!parser_expect(','))
		return SKIP_REMAINDER;

	// drop this one?
	if (debuglevel.val.intval > config.debuglevel)
		return SKIP_REMAINDER;

	return throw_src_string(debuglevel.val.intval, "!debug: ");
}

// show info as given in source code
static enum eos po_info(void)
{
	return throw_src_string(DEBUGLEVEL_INFO, "!info: ");
}

// throw warning as given in source code
static enum eos po_warn(void)
{
	return throw_src_string(DEBUGLEVEL_WARNING, "!warn: ");
}

// throw error as given in source code
static enum eos po_error(void)
{
	return throw_src_string(DEBUGLEVEL_ERROR, "!error: ");
}

// throw serious error as given in source code
static enum eos po_serious(void)
{
	return throw_src_string(DEBUGLEVEL_SERIOUS, "!serious: ");
}



// end of source file ("!endoffile" or "!eof")
static enum eos po_endoffile(void)
{
	parser_ensure_EOS();	// make sure there are no args
	input_force_eof();	// fake end of file
	return AT_EOS_ANYWAY;
}

// pseudo opcode table
static struct ronode	pseudo_opcode_tree[]	= {
	PREDEF_START,
	PREDEFNODE("initmem",		po_initmem),
	PREDEFNODE("to",		po_to),
	PREDEFNODE("sl",		po_symbollist),
	PREDEFNODE("symbollist",	po_symbollist),
	PREDEFNODE("outfilestart",	po_outfilestart),
	PREDEFNODE("outfilestop",	po_outfilelimit),
	PREDEFNODE("outfilelimit",	po_outfilelimit),
	PREDEFNODE("xor",		po_xor),
	PREDEFNODE("by",		po_byte),
	PREDEFNODE("byt",		po_byte),
	PREDEFNODE("byte",		po_byte),
	PREDEFNODE("8",			po_byte),
	PREDEFNODE("08",		po_byte),	// legacy alias, don't ask...
	PREDEFNODE("wo",		po_16),
	PREDEFNODE("word",		po_16),
	PREDEFNODE("16",		po_16),
	PREDEFNODE("be16",		po_be16),
	PREDEFNODE("le16",		po_le16),
	PREDEFNODE("24",		po_24),
	PREDEFNODE("be24",		po_be24),
	PREDEFNODE("le24",		po_le24),
	PREDEFNODE("32",		po_32),
	PREDEFNODE("be32",		po_be32),
	PREDEFNODE("le32",		po_le32),
	PREDEFNODE("h",			po_hex),
	PREDEFNODE("hex",		po_hex),
	PREDEFNODE("cbm",		po_cbm),	// obsolete
	PREDEFNODE("ct",		po_convtab),
	PREDEFNODE("convtab",		po_convtab),
	PREDEFNODE("tx",		po_text),
	PREDEFNODE("txt",		po_text),
	PREDEFNODE("text",		po_text),
	PREDEFNODE("raw",		po_raw),
	PREDEFNODE("pet",		po_pet),
	PREDEFNODE("scr",		po_scr),
	PREDEFNODE("scrxor",		po_scrxor),
	PREDEFNODE("bin",		po_binary),
	PREDEFNODE("binary",		po_binary),
	PREDEFNODE("fi",		po_fill),
	PREDEFNODE("fill",		po_fill),
	PREDEFNODE("skip",		po_skip),
	PREDEFNODE("align",		po_align),
	PREDEFNODE("pseudopc",		po_pseudopc),
	PREDEFNODE("realpc",		po_realpc),	// obsolete
	PREDEFNODE("cpu",		po_cpu),
	PREDEFNODE("al",		po_al),
	PREDEFNODE("as",		po_as),
	PREDEFNODE("rl",		po_rl),
	PREDEFNODE("rs",		po_rs),
//	PREDEFNODE("enum",		po_enum),
	PREDEFNODE("set",		po_set),
	PREDEFNODE("zn",		po_zone),
	PREDEFNODE("zone",		po_zone),
	PREDEFNODE("sz",		po_subzone),	// obsolete
	PREDEFNODE("subzone",		po_subzone),	// obsolete
	PREDEFNODE("src",		po_source),
	PREDEFNODE("source",		po_source),
	PREDEFNODE("if",		po_if),
	PREDEFNODE("ifdef",		po_ifdef),
	PREDEFNODE("ifndef",		po_ifndef),
	PREDEFNODE("for",		po_for),
	PREDEFNODE("do",		po_do),
	PREDEFNODE("while",		po_while),
	PREDEFNODE("macro",		po_macro),
/*	PREDEFNODE("trace",		po_trace),
	PREDEFNODE("watch",		po_watch),	*/
	PREDEFNODE("addr",		po_address),
	PREDEFNODE("address",		po_address),
	PREDEFNODE("nowarn",		po_nowarn),
	PREDEFNODE("break",		po_break),
	PREDEFNODE("continue",		po_continue),
	PREDEFNODE("return",		po_return),
	PREDEFNODE("debug",		po_debug),
	PREDEFNODE("info",		po_info),
	PREDEFNODE("warn",		po_warn),
	PREDEFNODE("error",		po_error),
	PREDEFNODE("serious",		po_serious),
	PREDEFNODE("eof",		po_endoffile),
	PREDEF_END("endoffile",		po_endoffile),
	//    ^^^^ this marks the last element
};


// parse a pseudo opcode. has to be re-entrant.
void pseudoopcode_parse(void)	// now GotByte = '!' (or '.' in case of --fullstop)
{
	void		*node_body;
	enum eos	(*fn)(void),
			then	= SKIP_REMAINDER;	// prepare for errors

	GetByte();	// read next byte
	// on missing keyword, return (complaining will have been done)
	if (parser_read_and_lower_keyword()) {
		// search for tree item
		if ((tree_easy_scan(pseudo_opcode_tree, &node_body, GlobalDynaBuf))
		&& node_body) {
			fn = (enum eos (*)(void)) node_body;
			SKIPSPACE();
			// call function
			then = fn();
		} else {
			throw_error("Unknown pseudo opcode.");
		}
	}
	if (then == SKIP_REMAINDER)
		parser_skip_remainder();
	else if (then == ENSURE_EOS)
		parser_ensure_EOS();
	// the other two possibilities (PARSE_REMAINDER and AT_EOS_ANYWAY)
	// will lead to the remainder of the line being parsed by the mainloop.
}


// this is not really a pseudo opcode, but similar enough to be put here:
// called when "*= EXPRESSION" is parsed, to set the program counter
void notreallypo_setpc(void)	// GotByte is '*'
{
	bits		segment_flags	= 0;
	boolean		do_outfilestart	= FALSE;
	struct number	intresult;

	// next non-space must be '='
	NEXTANDSKIPSPACE();
	if (!parser_expect('='))
		goto fail;

	ALU_defined_int(&intresult);	// read new address
	// check for modifiers
	while (parser_accept_comma()) {
		// parse modifier. if no keyword given, give up
		if (parser_read_and_lower_keyword() == 0)
			goto fail;

		if (strcmp(GlobalDynaBuf->buffer, "overlay") == 0) {
			segment_flags |= SEGMENT_FLAG_OVERLAY;
		} else if (strcmp(GlobalDynaBuf->buffer, "invisible") == 0) {
			segment_flags |= SEGMENT_FLAG_INVISIBLE;
		} else if (strcmp(GlobalDynaBuf->buffer, "outfilestart") == 0) {
			do_outfilestart	= TRUE;
/*TODO		} else if (strcmp(GlobalDynaBuf->buffer, "limit") == 0) {
			skip '='
			read memory limit
		} else if (strcmp(GlobalDynaBuf->buffer, "stay" or "same" or something like that) == 0) {
			mutually exclusive with all other arguments!
			this would mean to keep all previous segment data,
			so it could be used with "*=*-5" or "*=*+3"
		} else if (strcmp(GlobalDynaBuf->buffer, "name") == 0) {
			skip '='
			read segment name (quoted string!)	*/
		} else {
			throw_error("Unknown \"*=\" segment modifier.");
			goto fail;
		}
	}

	// before actually setting pc,
	// support stupidly bad, old, ancient, deprecated, obsolete behaviour:
	if (pseudopc_isactive()) {
		if (config.dialect >= V0_94_8__DISABLED_OBSOLETE) {
			// current behaviour:
			// setting pc does not disable pseudopc
		} else if (config.dialect >= V0_93__SHORTER_SETPC_WARNING) {
			throw_warning("Offset assembly still active at end of segment.");
			end_all_pseudopc();	// warning did not say it would
			// disable pseudopc, but still did. nevertheless, there
			// is something different to older versions: when the
			// closing '}' or !realpc is encountered, _really_ weird
			// stuff happens! i see no reason to try to mimic that.
		} else {
			// prior to 0.93, setting pc disabled pseudopc with a warning:
			throw_warning("Offset assembly still active at end of segment. Switched it off.");
			end_all_pseudopc();
		}
	}

	if (intresult.val.intval < 0) {
		throw_error("Program counter cannot be negative.");
		intresult.val.intval = cpu_current_type->dummy_pc;
	}
	programcounter_set(intresult.val.intval, segment_flags);

	// if wanted, perform "!outfilestart":
	if (do_outfilestart)
		po_outfilestart();

	parser_ensure_EOS();
	return;

fail:
	parser_skip_remainder();
}
