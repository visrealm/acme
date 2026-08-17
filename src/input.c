// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2025 Marco Baye
// Have a look at "acme.c" for further info
//
// Input stuff
// 19 Nov 2014	Merged Johann Klasek's report listing generator patch
//  9 Jan 2018	Allowed "//" comments
#include "input.h"
#include "config.h"
#include "alu.h"
#include "dynabuf.h"
#include "global.h"
#include "platform.h"
#include "section.h"
#include "symbol.h"
#include "tree.h"


// type definitions

// values for current_input.state
enum inputstate {
	INPUTSTATE_SOF,		// start of file (check for BOM and hashbang)
	INPUTSTATE_NORMAL,	// everything's fine
	INPUTSTATE_AGAIN,	// re-process last byte
	INPUTSTATE_SKIPBLANKS,	// shrink multiple spaces
	INPUTSTATE_LF,		// send start-of-line after end-of-statement
	INPUTSTATE_CR,		// same, but also remember to skip LF
	INPUTSTATE_SKIPLF,	// skip LF if that's next
	INPUTSTATE_COMMENT,	// skip characters until newline or EOF
	INPUTSTATE_EOB,		// send end-of-block after end-of-statement
	INPUTSTATE_EOF,		// send end-of-file after end-of-statement
	INPUTSTATE_BOMCHECK,	// first byte in file looks like first byte of BOM
	INPUTSTATE_BOMFAIL,	// send second byte of non-BOM and then the byte read from file
};


// constants

const char	FILE_READBINARY[]	= "rb";
#define CHAR_LF		(10)	// line feed		(in file)
		//	(10)	// start of line	(in high-level format)
#define CHAR_CR		(13)	// carriage return	(in file)
		//	(13)	// end of file		(in high-level format)
// if the characters above are changed, don't forget to adjust byte_flags[]!

// current input structure
static struct input	current_input	= {
	{
		"<none>",	// file name where code initially came from (differs during macro execution)
		0,		// line number
		// (these init values are for error msgs before any real input is established)
	},
	INPUTSRC_NONE,
	INPUTSTATE_EOF,	// state of input
	{
		NULL	// RAM read pointer or file handle
	},
	FALSE	// do not write to report file
};


// variables

char		GotByte;	// last byte read (processed)
// name of source file used for resolving relative paths
// (i.e. not changed during macro execution):
const char	*input_plat_pathref_filename	= "";	// file name in platform format


// functions

// report generator stuff:

// remember source code character for report generator
#define HEXBUFSIZE	9	// actually, 4+1 is enough, but for systems without snprintf(), let's be extra-safe.
#define IF_WANTED_REPORT_SRCCHAR(c)	do { if (current_input.report) report_srcchar(c); } while(0)

static void report_printline(int line_number)
{
	int	ii;
	char	hex_address[HEXBUFSIZE];
	char	hexdump[2 * REPORT_BINBUFSIZE + 2 + 1];	// +2 for '.' and terminator, +1 to suppress compiler warning

	// suppress empty lines
	if (report->bin_used == 0) {
		if ((report->asc_buf[0] == '\0')
		|| ((report->asc_buf[0] == ' ') && (report->asc_buf[1] == '\0'))) {
			report->asc_used = 0;
			return;
		}
	}

	// line start after line break detected and EOS processed,
	// build report line:
	// show line number...
	fprintf(report->fd, "%6d  ", line_number);
	// prepare outbytes' start address
	if (report->bin_used) {
#if _BSD_SOURCE || _XOPEN_SOURCE >= 500 || _ISOC99_SOURCE || _POSIX_C_SOURCE >= 200112L
		snprintf(hex_address, HEXBUFSIZE, "%04x", report->bin_address);
#else
		sprintf(hex_address, "%04x", report->bin_address);
#endif
	} else {
		hex_address[0] = '\0';
	}
	// prepare outbytes
	hexdump[0] = '\0';
	for (ii = 0; ii < report->bin_used; ++ii)
		sprintf(hexdump + 2 * ii, "%02x", (unsigned int) (unsigned char) (report->bin_buf[ii]));
	// if binary buffer is full, overwrite last byte with "..."
	if (report->bin_used == REPORT_BINBUFSIZE)
		sprintf(hexdump + 2 * (REPORT_BINBUFSIZE - 1), "...");
	// show address and bytes
	fprintf(report->fd, "%-4s %-19s", hex_address, hexdump);
	// at this point the output should be a multiple of 8 characters
	// so far to preserve tabs of the source...
	if (report->asc_used == REPORT_ASCBUFSIZE)
		--report->asc_used;
	report->asc_buf[report->asc_used] = '\0';
	fprintf(report->fd, "%s\n", report->asc_buf);	// show source line
	report->asc_used = 0;	// reset buffers
	report->bin_used = 0;
}

// flush line buffer
static void report_flush(void)
{
	if (current_input.report) {
		report_printline(current_input.location.line_number);
	}
}

// insert explanation about change in input
static void report_inputchange(const char prefix[], const char filename[])
{
	if (current_input.report) {
		fprintf(report->fd, "\n; ******** %s%s\n", prefix, filename);
		report->new_input = TRUE;
	}
}

static void report_srcchar(int new_char)
{
	static char	prev_char	= '\0';

	if (new_char == EOF)
		new_char = '\n';

	if (report->new_input) {
		report->new_input = FALSE;
		prev_char = '\0';
	}
	if (prev_char == '\n') {
		report_printline(current_input.location.line_number - 1);
	}
	if (new_char != '\n' && new_char != '\r') {	// detect line break
		if (report->asc_used < REPORT_ASCBUFSIZE)
			report->asc_buf[report->asc_used++] = new_char;
	}
	prev_char = new_char;
}


// the byte order mark (Unicode code point 0xfeff) becomes 0xef 0xbb 0xbf when
// encoded in UTF-8. though UTF-8 files no not need any byte order mark, some
// text editors add it anyway. these definitions are used to ignore it instead
// of treating it as a label:
#define UTF8BOM_1ST	0xef
#define UTF8BOM_2ND	0xbb
#define UTF8BOM_3RD	0xbf
// deliver source code from current file (!) in shortened high-level format
static char get_processed_from_file(void)
{
	static int	from_file	= 0;

	for (;;) {
		switch (current_input.state) {
		case INPUTSTATE_SOF:
			// fetch first byte from the current source file
			from_file = getc(current_input.u.fd);
			// check for first byte of UTF-8-encoded BOM
			if (from_file == UTF8BOM_1ST) {
				current_input.state = INPUTSTATE_BOMCHECK;
				break;
			}
			IF_WANTED_REPORT_SRCCHAR(from_file);
			// check for hashbang line and ignore
			if (from_file == '#') {
				// remember to skip remainder of line
				current_input.state = INPUTSTATE_COMMENT;
				return CHAR_EOS;	// end of statement
			}
			current_input.state = INPUTSTATE_AGAIN;
			break;
		case INPUTSTATE_NORMAL:
			// fetch a fresh byte from the current source file
			from_file = getc(current_input.u.fd);
			IF_WANTED_REPORT_SRCCHAR(from_file);
			// now process it
			/*FALLTHROUGH*/
		case INPUTSTATE_AGAIN:
			// Process the latest byte again. Of course, this only
			// makes sense if the loop has executed at least once,
			// otherwise the contents of from_file are undefined.
			// If the source is changed so there is a possibility
			// to enter INPUTSTATE_AGAIN mode without first having
			// defined "from_file", trouble may arise...
			current_input.state = INPUTSTATE_NORMAL;
			// EOF must be checked first because it cannot be used
			// as an index into global_byte_flags[]
			if (from_file == EOF) {
				// remember to send an end-of-file
				current_input.state = INPUTSTATE_EOF;
				return CHAR_EOS;	// end of statement
			}

			// check whether character is special one
			// if not, everything's cool and froody, so return it
			if (BYTE_IS_SYNTAX_CHAR(from_file) == 0)
				return (char) from_file;

			// check special characters ("0x00 TAB LF CR SPC / : ; }")
			switch (from_file) {
			case '\t':
			case ' ':
				// remember to skip all following blanks
				current_input.state = INPUTSTATE_SKIPBLANKS;
				return ' ';

			case CHAR_LF:	// LF character
				// remember to send a start-of-line
				current_input.state = INPUTSTATE_LF;
				return CHAR_EOS;	// end of statement

			case CHAR_CR:	// CR character
				// remember to check CRLF + send start-of-line
				current_input.state = INPUTSTATE_CR;
				return CHAR_EOS;	// end of statement

			case CHAR_EOB:
				// remember to send an end-of-block
				current_input.state = INPUTSTATE_EOB;
				return CHAR_EOS;	// end of statement

			case '/':
				// to check for "//", get another byte:
				from_file = getc(current_input.u.fd);
				IF_WANTED_REPORT_SRCCHAR(from_file);
				if (from_file != '/') {
					// not "//", so:
					current_input.state = INPUTSTATE_AGAIN;	// second byte must be parsed normally later on
					return '/';	// first byte is returned normally right now
				}
				// it's really "//", so act as if ';'
				/*FALLTHROUGH*/
			case ';':
				// remember to skip remainder of line
				current_input.state = INPUTSTATE_COMMENT;
				return CHAR_EOS;	// end of statement

			case ':':	// statement delimiter
				// just deliver an EOS instead
				return CHAR_EOS;	// end of statement

			default:
				// complain if byte is 0
				throw_error("Source file contains illegal character.");	// FIXME - throw some dynamic "did not expect XYZ character" error instead!
				return (char) from_file;
			}
		case INPUTSTATE_SKIPBLANKS:
			// read until non-blank, then deliver that
			do {
				from_file = getc(current_input.u.fd);
				IF_WANTED_REPORT_SRCCHAR(from_file);
			} while ((from_file == '\t') || (from_file == ' '));
			// re-process last byte
			current_input.state = INPUTSTATE_AGAIN;
			break;
		case INPUTSTATE_LF:
			// return start-of-line, then continue in normal mode
			current_input.state = INPUTSTATE_NORMAL;
			return CHAR_SOL;	// new line

		case INPUTSTATE_CR:
			// return start-of-line, remember to check for LF
			current_input.state = INPUTSTATE_SKIPLF;
			return CHAR_SOL;	// new line

		case INPUTSTATE_SKIPLF:
			from_file = getc(current_input.u.fd);
			IF_WANTED_REPORT_SRCCHAR(from_file);
			// if LF, ignore it and fetch another byte
			// otherwise, process current byte
			if (from_file == CHAR_LF)
				current_input.state = INPUTSTATE_NORMAL;
			else
				current_input.state = INPUTSTATE_AGAIN;
			break;
		case INPUTSTATE_COMMENT:
			// read until end-of-line or end-of-file
			do {
				from_file = getc(current_input.u.fd);
				IF_WANTED_REPORT_SRCCHAR(from_file);
			} while ((from_file != EOF) && (from_file != CHAR_CR) && (from_file != CHAR_LF));
			// re-process last byte
			current_input.state = INPUTSTATE_AGAIN;
			break;
		case INPUTSTATE_EOB:
			// deliver EOB
			current_input.state = INPUTSTATE_NORMAL;
			return CHAR_EOB;	// end of block

		case INPUTSTATE_EOF:
			// deliver EOF
			current_input.state = INPUTSTATE_NORMAL;
			return CHAR_EOF;	// end of file

// two cases for BOM:
		case INPUTSTATE_BOMCHECK:
			// first byte matches BOM, so check second:
			from_file = getc(current_input.u.fd);
			if (from_file != UTF8BOM_2ND) {
				// second byte does not match, so return first byte and remember to re-process second:
				IF_WANTED_REPORT_SRCCHAR(UTF8BOM_1ST);
				IF_WANTED_REPORT_SRCCHAR(from_file);
				current_input.state = INPUTSTATE_AGAIN;
				return UTF8BOM_1ST;
			}
			// first two bytes match BOM, so check third:
			from_file = getc(current_input.u.fd);
			if (from_file != UTF8BOM_3RD) {
				// third byte does not match, so return first byte and make sure the others are delivered later:
				IF_WANTED_REPORT_SRCCHAR(UTF8BOM_1ST);
				IF_WANTED_REPORT_SRCCHAR(UTF8BOM_2ND);
				IF_WANTED_REPORT_SRCCHAR(from_file);
				current_input.state = INPUTSTATE_BOMFAIL;	// next time, deliver second byte
				return UTF8BOM_1ST;
			}
			// found three-byte BOM, so ignore by starting normally with next byte:
			current_input.state = INPUTSTATE_NORMAL;
			break;

		case INPUTSTATE_BOMFAIL:
			// third byte did not match BOM. first byte has already been delivered.
			// deliver second byte and remember to re-process third:
			current_input.state = INPUTSTATE_AGAIN;	// next time, deliver byte last read
			return UTF8BOM_2ND;

		default:
			BUG("StrangeInputMode", current_input.state);
		}
	}
}

int		subst_chars_left	= 0;	// number of bytes left in buffer
boolean		subst_enabled		= TRUE;	// flag to disable substing (for example while substing, or when copying block to RAM)
const char	*subst_read_ptr		= NULL;	// pointer to read bytes from
STRUCT_DYNABUF_REF(subst_buffer, 40);	// buffer to hold substitution result

// fetch next byte from buffer
static char subst_get_char(void)
{
	if (subst_chars_left-- == 0)
		BUG("NoSubstLeft", 0);
	return *(subst_read_ptr++);
}
// read symbol name from source and then setup substitution buffer with contents
// of symbol (string or integer). int is converted to decimal digits.
// example:
// if "somesymbol" is 42 and "endsymbol" is 47,
//	basename?(somesymbol)middle?endsymbol=9
// becomes
//	basename42middle47=9
// this would happen in two steps,
// the first result would be "42m" and the second would be "47="
static void subst_substitute(void)	// now GotByte = '?'
{
	scope_t		tmp_scope;
	struct symbol	*tmp_symbol;
	boolean		parenthesized;

	if (GotByte != '?')
		BUG("NotQuestionMark", GotByte);

	GetByte();	// eat '?' character
	// handle parentheses
	if (GotByte == '(') {
		GetByte();	// eat '(' character
		parenthesized = TRUE;
	} else {
		parenthesized = FALSE;
	}

	// reading symbol name will clobber GlobalDynaBuf, so we'll use our own
	// buffer as tmp storage:
	dynabuf_clear(subst_buffer);
	dynabuf_add_bytes(subst_buffer, GlobalDynaBuf->buffer, GlobalDynaBuf->size);

	// read symbol name into GlobalDynaBuf and get symbol ptr:
	if (input_read_scope_and_symbol_name(&tmp_scope)) {
		tmp_symbol = NULL;	// remember failure (check after restoring GlobalDynaBuf)
	} else {
		tmp_symbol = symbol_find(tmp_scope);	// reads name from GlobalDynaBuf
		tmp_symbol->has_been_read = TRUE;
	}

	// restore previous contents of GlobalDynaBuf:
	dynabuf_clear(GlobalDynaBuf);
	dynabuf_add_bytes(GlobalDynaBuf, subst_buffer->buffer, subst_buffer->size);

	// read symbol and put value into subst buffer
	dynabuf_clear(subst_buffer);
	if (tmp_symbol == NULL) {
		goto fail;	// input_read_scope_and_symbol_name will have thrown error
	}
	if (tmp_symbol->object.type == NULL) {
		throw_error("Substitution symbol is undefined.");
		// FIXME - set type to undefined int, just to make sure later refs via type do not crash!
		goto fail;
	} else if (tmp_symbol->object.type == &type_number) {
		if (tmp_symbol->object.u.number.ntype != NUMTYPE_INT) {
			throw_error("Substitution symbol is undefined or not integer.");
			goto fail;
		}
		dynabuf_add_signed_long(subst_buffer, (long) tmp_symbol->object.u.number.val.intval);
	} else if (tmp_symbol->object.type == &type_string) {
		dynabuf_add_bytes(subst_buffer, tmp_symbol->object.u.string->payload, tmp_symbol->object.u.string->length);
	} else {
		throw_error("Substitution symbol is neither number nor string.");
		goto fail;
	}

	// check for closing parenthesis
	if (parenthesized) {
		SKIPSPACE();
		if (GotByte == ')') {
			GetByte();	// eat ')'
		} else {
			throw_error("Substitution does not end with ')' character.");
			goto fail;
		}
	}
	// now append the delimiter character to the buffer, otherwise it would
	// be lost because reading from the buffer keeps clobbering GotByte:
	dynabuf_append(subst_buffer, GotByte);
	subst_chars_left = subst_buffer->size;
	subst_read_ptr = subst_buffer->buffer;
	// FIXME: do we need checks for "buffer contains braces, colon, EOS, ..."
	// or is it enough to add "user, do not try stupid things" to docs?
	return;
fail:
	dynabuf_clear(subst_buffer);
	dynabuf_append(subst_buffer, GotByte);
	subst_chars_left = subst_buffer->size;
	subst_read_ptr = subst_buffer->buffer;
}

// This function delivers the next byte from the currently active byte source
// in shortened high-level format. FIXME - use fn ptr?
// do not use inside quotes!
// CAUTION, symbol substitutions cause this fn to be called recursively!
char GetByte(void)
{
	// substitution buffer has priority, so if anything is in there,
	// deliver that:
	if (subst_chars_left) {
		GotByte = subst_get_char();
	} else {
		// otherwise get a byte from file/ram:

		// If byte source is RAM, then no conversions are
		// necessary, because in RAM the source already has
		// high-level format
		// Otherwise, the source is a file. This means we will call
		// get_processed_from_file() which will do a shit load of conversions.
		switch (current_input.srctype) {
		case INPUTSRC_RAM:
			GotByte = *(current_input.u.ram_ptr++);
			if (current_input.state== INPUTSTATE_NORMAL)
				IF_WANTED_REPORT_SRCCHAR(GotByte);
			break;
		case INPUTSRC_FILE:
			GotByte = get_processed_from_file();
			break;
		default:
			BUG("IllegalInputSrc", current_input.srctype);
		}

		if (GotByte == CHAR_SOL)
			current_input.location.line_number++;
	}
	// check for '?' substitutions
	if ((GotByte == '?') && (subst_enabled) && (subst_chars_left == 0)) {
		// start a new substitution
/*
the check for "subst_chars_left" is needed because characters from the buffer
are not allowed to start a new substitution, except for the very last char!
the last char wasn't part of the result, it was the original delimiter. example:
		cd = "xyz"
		!info ab?cd?ef
the first '?' causes a lookup for "cd" and will write the result "xyz" to the
buffer. then the second '?' (now GotByte because it was the keyword delimiter)
will be appended to the buffer because otherwise it would get lost during buffer
reads.
*/
		subst_enabled = FALSE;	// no substitutions in substitutions!
		subst_substitute();	// do the actual work
		subst_enabled = TRUE;	// back to previous state
		// because the delimiter is put into the buffer, we know the
		// buffer isn't empty, even if the substitution result was "".
		// so this is allowed:
		GotByte = subst_get_char();
	}
	return GotByte;
}

// This function delivers the next byte from the currently active byte source
// in un-shortened high-level format.
// This function complains if CHAR_EOS (end of statement) is read.
static void get_quoted_byte(void)
{
	int	from_file;	// must be an int to catch EOF

	// substitution buffer has priority, so if anything is in there,
	// deliver that:
	if (subst_chars_left) {
		GotByte = subst_get_char();
		return;
	}

	switch (current_input.srctype) {
	case INPUTSRC_RAM:
		// if byte source is RAM, then no conversion is necessary,
		// because in RAM the source already has high-level format
		GotByte = *(current_input.u.ram_ptr++);
		IF_WANTED_REPORT_SRCCHAR(GotByte);
		break;
	case INPUTSRC_FILE:
		// fetch a fresh byte from the current source file
		from_file = getc(current_input.u.fd);
		IF_WANTED_REPORT_SRCCHAR(from_file);
		switch (from_file) {
		case EOF:
			// remember to send an end-of-file
			current_input.state = INPUTSTATE_EOF;
			GotByte = CHAR_EOS;	// end of statement
			break;
		case CHAR_LF:	// LF character
			// remember to send a start-of-line
			current_input.state = INPUTSTATE_LF;
			GotByte = CHAR_EOS;	// end of statement
			break;
		case CHAR_CR:	// CR character
			// remember to check for CRLF + send a start-of-line
			current_input.state = INPUTSTATE_CR;
			GotByte = CHAR_EOS;	// end of statement
			break;
		default:
			GotByte = from_file;
		}
		break;
	default:
		BUG("IllegalInputSrc", current_input.srctype);
	}
	// now check for end of statement
	if (GotByte == CHAR_EOS)
		throw_error("Quotes still open at end of line.");
}

// ensure that the remainder of the current statement is empty, for example
// after mnemonics using implied addressing.
void parser_ensure_EOS(void)	// now GotByte = first char to test
{
	SKIPSPACE();
	if (GotByte) {
		// FIXME - move this to its own function!
		char	buf[80];	// actually needed are 51
		char	quote;		// character before and after

		// FIXME - change quoting: do not assume char is printable!
		quote = (GotByte == '\'') ? '"' : '\'';	// use single quotes, unless byte is a single quote (then use double quotes)
		sprintf(buf, "Expected end-of-statement, found %c%c%c instead.", quote, GotByte, quote);
		throw_error(buf);
		parser_skip_remainder();
	}
}

// read string to dynabuf until closing quote is found
// returns 1 on error (unterminated)
static int quoted_to_dynabuf(char closing_quote)
{
	boolean	escaped	= FALSE;

	//dynabuf_clear(GlobalDynaBuf);	// do not clear, some callers want to append to existing contents!
	for (;;) {
		get_quoted_byte();
		if (GotByte == CHAR_EOS)
			return 1;	// unterminated string constant; get_quoted_byte will have complained already

		if (escaped) {
			// previous byte was backslash, so do not check for closing quote nor backslash
			escaped = FALSE;
			// do not actually _convert_ escape sequences, that is
			// done in input_read_string_literal() below!
			// TODO - but maybe check for illegal escape sequences?
			// at the moment checking is only done when the string
			// gets used for something...
		} else {
			// non-escaped: only closing quote and backslash are of interest
			if (GotByte == closing_quote)
				return 0;	// ok

			if (GotByte == '\\') {
				if (config.dialect >= V0_97__BACKSLASH_ESCAPING) {
					escaped = TRUE;	// since v0.97, backslashes are escape characters
				} else {
					// earlier versions did not care about backslashes
				}
			}
		}
		DYNABUF_APPEND(GlobalDynaBuf, GotByte);
	}
}

// helper function to assemble a number from hex digits
// returns 1 on error
static int shift_hex_nibble(char *target, char digit)
{
	if (digit >= '0' && digit <= '9') {
		digit -= '0';
	} else if (digit >= 'a' && digit <= 'f') {
		digit = digit - 'a' + 10;
	} else if (digit >= 'A' && digit <= 'F') {
		digit = digit - 'A' + 10;
	} else {
		throw_error("Invalid hex digit after \\x, expected 0-9/a-f.");
		return 1;	// error
	}
	*target = (*target << 4) | digit;
	return 0;	// ok
}

// state machine for backslash escaping:
enum escape {
	ESCAPE_NONE,		// un-escaped, i.e. normal
	ESCAPE_BACKSLASH,	// after reading a backslash
	ESCAPE_HEX1,		// after reading "\x", expecting first hex digit
	ESCAPE_HEX2,		// after reading "\x", expecting second hex digit
};
// process backslash escapes in GlobalDynaBuf (so size might shrink)
// returns 1 on excaping errors
int unescape_dynabuf(void)
{
	int		read_index	= 0,
			write_index	= 0;
	char		byte;
	enum escape	escape_state	= ESCAPE_NONE;
	char		hexbyte		= 0;
	int		err		= 0;

	// CAUTION - contents of dynabuf are not terminated:
	while (read_index < GlobalDynaBuf->size) {
		byte = GLOBALDYNABUF_CURRENT[read_index++];
		switch (escape_state) {
		case ESCAPE_NONE:
			if (byte == '\\') {
				escape_state = ESCAPE_BACKSLASH;
			} else {
				GLOBALDYNABUF_CURRENT[write_index++] = byte;
			}
			break;
		case ESCAPE_BACKSLASH:
			escape_state = ESCAPE_NONE;	// default for all chars but 'x'!
			switch (byte) {
			case '\\':
			case '\'':
			case '"':
				break;
			case '0':	// NUL
				byte = 0;
				break;
			case 't':	// TAB
				byte = 9;
				break;
			case 'n':	// LF
				byte = 10;
				break;
			case 'r':	// CR
				byte = 13;
				break;
			case 'x':	// hex value
				escape_state = ESCAPE_HEX1;	// now expect first hex digit
				// now an "x" will be written, which will be overwritten later!
				break;
			// TODO - 'a' to BEL? others?
			default:
				throw_error("Unsupported backslash sequence.");	// TODO - add unexpected character to error message?
				err = 1;
			}
			GLOBALDYNABUF_CURRENT[write_index++] = byte;
			break;
		case ESCAPE_HEX1:
			hexbyte = 0;
			if (shift_hex_nibble(&hexbyte, byte)) {
				escape_state = ESCAPE_NONE;	// error -> back to default state
				err = 1;
			} else {
				escape_state = ESCAPE_HEX2;	// ok -> expect second hex digit
			}
			break;
		case ESCAPE_HEX2:
			if (shift_hex_nibble(&hexbyte, byte)) {
				err = 1;
			} else {
				// parsing "\x" has written "x", so overwrite it:
				GLOBALDYNABUF_CURRENT[write_index - 1] = hexbyte;
			}
			escape_state = ESCAPE_NONE;	// go back to default state
			break;
		default:
			BUG("StrangeEscapeState1", escape_state);
		}
	}
	GlobalDynaBuf->size = write_index;
	switch (escape_state) {
	case ESCAPE_NONE:
		break;
	case ESCAPE_BACKSLASH:	// this should have been caught earlier
		BUG("PartialEscapeSequence", 0);
	case ESCAPE_HEX1:
	case ESCAPE_HEX2:
		throw_error("Expected two hex digits after \\x sequence.");	// TODO - add unexpected character to error message?
		err = 1;
		break;
	default:
		BUG("StrangeEscapeState2", escape_state);
	}
	return err;
}


// clear dynabuf, read string to it until closing quote is found, then
// process backslash escapes (so size might shrink)
// returns 1 on error (unterminated or escaping error)
int input_read_string_literal(char closing_quote)
{
	dynabuf_clear(GlobalDynaBuf);
	if (quoted_to_dynabuf(closing_quote))
		return 1;	// unterminated

	// eat closing quote
	GetByte();

	// since v0.97 we need to process backslashes:
	if (config.dialect >= V0_97__BACKSLASH_ESCAPING) {
		return unescape_dynabuf();
	} else {
		return 0;	// older versions did not support backslash escapes, so return "ok"
	}
}


// skip remainder of statement, for example on error
// FIXME - compare to fn below! merge?
void parser_skip_remainder(void)
{
	// read characters until end-of-statement, but check for quotes,
	// otherwise this might treat a quoted colon like EOS!
	dynabuf_clear(GlobalDynaBuf);
	while (GotByte != CHAR_EOS) {
		// check for quotes
		if ((GotByte == '"') || (GotByte == '\'')) {
			if (quoted_to_dynabuf(GotByte))
				break;	// error (CHAR_EOS before closing quote)
		}
		GetByte();
	}
	dynabuf_clear(GlobalDynaBuf);
}

// clear dynabuf, read remainder of statement into it, making sure to keep quoted stuff intact
void input_read_statement(char terminator)
{
	int	err;

	SKIPSPACE();
	dynabuf_clear(GlobalDynaBuf);
	while ((GotByte != terminator) && (GotByte != CHAR_EOS)) {
		// append to GlobalDynaBuf and check for quotes
		DYNABUF_APPEND(GlobalDynaBuf, GotByte);
		if ((GotByte == '"') || (GotByte == '\'')) {
			err = quoted_to_dynabuf(GotByte);
			// here GotByte changes, it might become CHAR_EOS
			DYNABUF_APPEND(GlobalDynaBuf, GotByte);	// add closing quotes (or CHAR_EOS) as well
			if (err)
				break;	// on error, exit before eating CHAR_EOS via GetByte()
		}
		GetByte();
	}
	dynabuf_append(GlobalDynaBuf, CHAR_EOS);	// ensure terminator
}

// Read block into GlobalDynabuf
// (reading starts with next byte, so call directly after reading opening brace).
// After calling this function, GotByte holds '}'. Unless EOF was found first,
// but then a serious error would have been thrown.
static void block_to_dynabuf(void)
{
	char	byte;
	int	depth	= 1;	// to find matching block end
	boolean	substflagbuf	= subst_enabled;

	// make sure not to subst while reading block (should only be done while parsing)
	subst_enabled = FALSE;

	// prepare global dynamic buffer
	dynabuf_clear(GlobalDynaBuf);
	do {
		byte = GetByte();
		// store
		DYNABUF_APPEND(GlobalDynaBuf, byte);
		// now check for some special characters
		switch (byte) {
		case CHAR_EOF:	// End-of-file in block? Sorry, no way.
			throw_serious_error(exception_no_right_brace);

		case '"':	// Quotes? Okay, read quoted stuff.
		case '\'':
			quoted_to_dynabuf(byte);
			DYNABUF_APPEND(GlobalDynaBuf, GotByte);	// add closing quote
			break;
		case CHAR_SOB:
			++depth;
			break;
		case CHAR_EOB:
			--depth;
			break;
		}
	} while (depth);

	// restore subst state
	subst_enabled = substflagbuf;
}
// Skip block (starting with next byte, so call directly after reading opening brace).
// After calling this function, GotByte holds '}'. Unless EOF was found first,
// but then a serious error would have been thrown.
void input_block_skip(void)
{
	block_to_dynabuf();
}
// Read block into GlobalDynabuf, make a copy and store pointer in struct.
// (reading starts with next byte, so call directly after reading opening brace).
// After calling this function, GotByte holds '}'. Unless EOF was found first,
// but then a serious error would have been thrown.
void input_block_getcopy(struct block *block)
{
	// first store line number...
	block->line_number = current_input.location.line_number;
	// ...then get block
	block_to_dynabuf();
	// add EOF, just to make sure block is never read too far
	dynabuf_append(GlobalDynaBuf, CHAR_EOS);
	dynabuf_append(GlobalDynaBuf, CHAR_EOF);
	// store pointer to copy
	block->body = dynabuf_get_copy(GlobalDynaBuf);
}

// Append to GlobalDynaBuf while characters are legal for keywords.
// Throws given error message if no characters found.
// Returns number of characters added.
static int append_keyword_to_global_dynabuf(const char error_for_zero_length[])
{
	int	length	= 0;

	// add characters to buffer until an illegal one comes along
	while (BYTE_CONTINUES_KEYWORD(GotByte)) {
		DYNABUF_APPEND(GlobalDynaBuf, GotByte);
		++length;
		GetByte();
	}
	if (length == 0)
		throw_error(error_for_zero_length);
	return length;
}

const char	exception_no_symbol_name[]	= "Expected symbol name.";
// append optional '.'/'@' prefix to GlobalDynaBuf, then keep
// appending while characters are legal for keywords.
// throw "Expected symbol name" error if none.
// return whether there was an error.
int input_append_symbol_name_to_global_dynabuf(void)
{
	if ((GotByte == LOCAL_PREFIX)
	|| (GotByte == CHEAP_PREFIX)) {
		dynabuf_append(GlobalDynaBuf, GotByte);
		GetByte();
	} else if (!BYTE_STARTS_KEYWORD(GotByte)) {
		// FIXME - show invalid char in error message!
		throw_error(exception_no_symbol_name);
		return 1;	// error
	}
	return append_keyword_to_global_dynabuf(exception_no_symbol_name) == 0;	// zero length -> error!
}

// read symbol name into GlobalDynaBuf, set scope,
// return whether there was an error (namely, "Expected symbol name.")
int input_readscopeandsymbolname(scope_t *scope, boolean dotkluge)
{
	int	err;

	SKIPSPACE();
	dynabuf_clear(GlobalDynaBuf);

	if (dotkluge) {
		// this happens after the expression parser has eaten the '.'
		// and did not find a decimal digit. -> not a float value ->
		// must be a local symbol -> we must restore the '.' in front!
		dynabuf_append(GlobalDynaBuf, '.');
		err = append_keyword_to_global_dynabuf(exception_no_symbol_name) == 0;	// zero length -> error!
	} else {
		err = input_append_symbol_name_to_global_dynabuf();
	}
	// add terminator to buffer (increments buffer's length counter)
	dynabuf_append(GlobalDynaBuf, '\0');
	if (err) {
		*scope = SCOPE_GLOBAL;	// bogus, but at least not un-initialized
		return 1;	// error
	}
	if (GLOBALDYNABUF_CURRENT[0] == LOCAL_PREFIX) {
		*scope = section_now->local_scope;
	} else if (GLOBALDYNABUF_CURRENT[0] == CHEAP_PREFIX) {
		*scope = section_now->cheap_scope;
	} else {
		*scope = SCOPE_GLOBAL;
	}
	return 0;	// no error
}

// Clear dynamic buffer, then append to it until an illegal (for a keyword)
// character is read. Zero-terminate the string. Return its length (without
// terminator).
// Zero lengths will produce an error.
int parser_read_keyword(void)
{
	int	length;

	dynabuf_clear(GlobalDynaBuf);
// FIXME: atm, all callers have checked the first char already, therefore the
// error message in the next line will never be shown, and that's why it's not
// in the docs. Clean this up!
	length = append_keyword_to_global_dynabuf("Expected keyword or symbol name.");
	// add terminator to buffer (increments buffer's length counter)
	dynabuf_append(GlobalDynaBuf, '\0');
	return length;
}

// Clear dynamic buffer, then append to it until an illegal (for a keyword)
// character is read. Zero-terminate the string, then convert to lower case.
// Return its length (without terminator).
// Zero lengths will produce an "Expected keyword" error.
int parser_read_and_lower_keyword(void)
{
	int	length;

	dynabuf_clear(GlobalDynaBuf);
	length = append_keyword_to_global_dynabuf("Expected keyword.");
	// add terminator to buffer (increments buffer's length counter)
	dynabuf_append(GlobalDynaBuf, '\0');
	dynabuf_to_lower(GlobalDynaBuf, GlobalDynaBuf);	// convert to lower case
	return length;
}

// shared ending when trying to read a file name.
// The file name given in the assembler source code is converted from
// UNIX style to platform style.
// Returns nonzero on error. Filename in GlobalDynaBuf, including terminator.
// Errors are handled and reported, but caller should call
// parser_skip_remainder() then.
static int read_filename_shared_end(boolean *absolute)
{
	// check length
	if (GlobalDynaBuf->size == 0) {
		throw_error("No file name given.");
		return 1;	// error
	}

	// terminate string
	dynabuf_append(GlobalDynaBuf, '\0');
	// add another zero byte to make sure the buffer is large enough so the
	// string could grow another byte:
	dynabuf_append(GlobalDynaBuf, '\0');
	// (this is an extremely ugly kluge for an extremely unlikely situation,
	// but still less ugly than all other workarounds I could think of. see
	// _riscos.c for the extremely unlikely situation where this is needed)

	// platform-specific path name conversion
	// (and tell absolute/relative paths apart)
	platform_convert_path(absolute, GLOBALDYNABUF_CURRENT);

	return 0;	// ok
}

// parse string and return in GlobalDynaBuf
static int get_string(void)
{
	struct object	object;

	if (config.dialect >= V0_97__BACKSLASH_ESCAPING) {
		// since v0.97 the expression parser supports string objects:
		ALU_any_result(&object);
		if (object.type == &type_string) {
			dynabuf_clear(GlobalDynaBuf);
			dynabuf_add_bytes(GlobalDynaBuf, object.u.string->payload, object.u.string->length);
		} else {
			throw_error("Expression did not return a string.");
			return 1;
		}
	} else {
		// in older versions, strings could only be given as literals:
		if (GotByte != '"') {
			throw_error("Quotes not found.");
			return 1;
		}
		if (input_read_string_literal('"')) {
			return 1;	// unterminated or escaping error
		}
	}
	return 0;	// ok
}

// try to read a file name for an input file.
// library access by using <...> quoting is allowed.
// flags for "library access" and "absolute path" will be set accordingly.
// The file name given in the assembler source code is converted from
// UNIX style to platform style.
// on success, returns zero. filename is in GlobalDynaBuf, caller should then
//	call includepaths_open_ro().
// on error, returns nonzero. errors are handled and reported, but caller should
//	then call parser_skip_remainder().
int input_read_input_filename(struct filespecflags *flags)
{
	SKIPSPACE();
	if (GotByte == '<') {
		// <path/to/file> means library access:
		flags->uses_lib = TRUE;
		if (input_read_string_literal('>'))
			return 1;	// unterminated or escaping error
	} else {
		// "path/to/file" or SOME_STRING_SYMBOL means non-library access:
		flags->uses_lib = FALSE;
		if (get_string())
			return 1;	// unterminated, escaping error or not a string
	}
	// check length, remember abs/rel, terminate, do platform conversion
	return read_filename_shared_end(&flags->absolute);
}

// Try to read a comma, skipping spaces before and after. Return TRUE if comma
// found, otherwise FALSE.
int parser_accept_comma(void)
{
	SKIPSPACE();
	if (GotByte != ',')
		return FALSE;

	NEXTANDSKIPSPACE();
	return TRUE;
}

// Try to read given character.
// If found, eat character and return TRUE.
// If not found, throw syntax error and return FALSE.
int parser_expect(int chr)
{
	// one caller uses this to read the '=' part of "!=", so
	// do not call SKIPSPACE() here!
	if (GotByte == chr) {
		GetByte();	// eat expected char
		return TRUE;
	}
	throw_error(exception_syntax);	// FIXME - build "expected X, found Y" error msg!
	return FALSE;
}

// force input system to return "end of file" on next read
// (back end function for "!eof" pseudo opcode)
void input_force_eof(void)
{
	current_input.state = INPUTSTATE_EOF;
}


// enable/disable writing to report file
// (only enable if report file has been opened!)
void input_set_report_enabled(boolean new_state)
{
	current_input.report = new_state;
}


static	STRUCT_DYNABUF_REF(pathbuf, 256);	// to combine search path and file spec

// copy platform-specific library search path into pathbuf:
static void library_path_to_pathbuf(void)
{
	dynabuf_clear(pathbuf);
	// CAUTION: the second part of the condition below implies the first part,
	// so the first part seems useless. but the first part allows for the
	// compiler to remove the check completely on some platforms.
	if ((PLATFORM_USE_ENV_VAR) && (config.platform_lib_prefix == NULL)) {
		throw_error("Library path not set. Use --libpath or create an environment variable.");
	} else {
		dynabuf_add_string(pathbuf, config.platform_lib_prefix);
	}
}

// copy "default search path" from current file's file name into pathbuf:
static void default_path_to_pathbuf(void)
{
	const char	*start	= input_plat_pathref_filename,
			*readptr,
			*found;

	dynabuf_clear(pathbuf);
	if (config.dialect >= V0_98__PATHS_AND_SYMBOLCHANGE) {
		// scan filename for last directory separator
		readptr = start;
		found = NULL;
		while (*readptr) {
			if ((*readptr == DIRECTORY_SEPARATOR)
			|| (*readptr == ALTERNATIVE_DIR_SEP)) {
				found = readptr;
			}
			++readptr;
		}
		if (found) {
			// +1 because we want the separator as well:
			dynabuf_add_bytes(pathbuf, start, found - start + 1);
		}
	} else {
		// do nothing -
		// pathbuf is empty, which means "default search path" is "",
		// which is exactly like it was in older versions.
	}
}

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
int input_read_output_filename(void)
{
	boolean	absolute;

	SKIPSPACE();
	if (GotByte == '<') {
		// <path/to/file> means library access:
		throw_error("Writing to library not supported.");
		return 1;	// error
	}
	// we expect "path/to/file" or SOME_STRING_SYMBOL:
// in the past, output filenames had to be given as literals and could not be
// created dynamically at runtime. because this "security feature" can now be
// circumvented using the symbol expansion mechanism, it does not make sense to
// keep it, it would only add complexity.
	if (get_string())
		return 1;	// unterminated, escaping error or not a string

	// check length, remember abs/rel, terminate, do platform conversion:
	if (read_filename_shared_end(&absolute))
		return 1;	// empty string

	if (absolute) {
		// keep file name as it is
	} else {
		// get current file's path
		default_path_to_pathbuf();
		// add output file name
		dynabuf_add_string(pathbuf, GLOBALDYNABUF_CURRENT);
		// terminate
		dynabuf_append(pathbuf, '\0');
		// copy full file name back to GlobalDynaBuf
		dynabuf_clear(GlobalDynaBuf);
		dynabuf_add_string(GlobalDynaBuf, pathbuf->buffer);
		dynabuf_append(GlobalDynaBuf, '\0');
	}

	return 0;	// ok
}

// write current "location" (file name and line number) to given target
void input_get_location(struct location *target)
{
	*target = current_input.location;
}


// "input change" stuff:

// save current input struct in buffer, then switch input to new source code file
void inputchange_new_file(struct inputchange_buf *icb, FILE *fd, const char *eternal_plat_filename)
{
	report_flush();

	// remember old state in buffer struct
	icb->input = current_input;
	icb->gb = GotByte;
	// set new state
	current_input.location.plat_filename	= eternal_plat_filename;
	current_input.location.line_number	= 1;
	current_input.srctype			= INPUTSRC_FILE;
	current_input.state			= INPUTSTATE_SOF;
	current_input.u.fd			= fd;

	report_inputchange("Source: ", eternal_plat_filename);
}
// save current input struct in buffer, then switch to RAM
void inputchange_new_ram(struct inputchange_buf *icb)
{
	report_flush();

	// remember old state in buffer struct
	icb->input = current_input;
	icb->gb = GotByte;
	// set new state (not useable yet, as pointer and line number are not yet set up)
	current_input.srctype	= INPUTSRC_RAM;	// set new byte source
	current_input.u.ram_ptr	= NULL;		// force crash if used before setup is finished
}
// FIXME - merge these three functions into a single one (by always using a "location"):
// setup for reading from RAM (for parsing loop conditions etc.)
void inputchange_set_ram(int line_num, const char *body)
{
	current_input.location.line_number	= line_num;
	current_input.u.ram_ptr			= body;
	input_set_report_enabled(FALSE);	// do not expand loops in report file (reverts to previous state when changing input back)
}
// switch input to macro parameters
void inputchange_macro1_params(const struct location *def, const char *params)
{
	current_input.location	= *def;
	current_input.u.ram_ptr	= params;
	current_input.state	= INPUTSTATE_NORMAL;	// FIXME - fix others!

	report_inputchange("macro from ", def->plat_filename);
}
// switch from macro parameters to macro body
void inputchange_macro2_body(const char *macro_body)
{
	current_input.u.ram_ptr	= macro_body;
	current_input.state	= INPUTSTATE_NORMAL;	// FIXME - fix others!
}
// restore input struct from buffer
void inputchange_back(const struct inputchange_buf *icb)
{
	report_flush();

	// restore old state from buffer struct
	current_input = icb->input;
	GotByte = icb->gb;

	switch (current_input.srctype) {
	case INPUTSRC_NONE:
		report_inputchange("done", "");
		break;
	case INPUTSRC_FILE:
		report_inputchange("Source: ", current_input.location.plat_filename);
		break;
	case INPUTSRC_RAM:
		report_inputchange("back", "");
		break;
	default:
		BUG("IllegalInputSrc", current_input.srctype);
	}
}


// "include path" stuff:

// ring list struct for "include path items"
struct ipi {
	struct ipi	*next,
			*prev;
	const char	*path;
};

// head element
static struct ipi	ipi_head	= {&ipi_head, &ipi_head, NULL};

// add entry
void includepaths_add(const char *path)
{
	struct ipi	*ipi;

	ipi = safe_malloc(sizeof(*ipi));
	ipi->path = path;
	ipi->next = &ipi_head;
	ipi->prev = ipi_head.prev;
	ipi->next->prev = ipi;
	ipi->prev->next = ipi;
}

// add filename (from GlobalDynaBuf) to pathbuf and try to open file:
static FILE *combine_and_open_ro(void)
{
	FILE	*stream;

	// if path does not end with directory separator, add one:
	if (pathbuf->size
	&& (pathbuf->buffer[pathbuf->size - 1] != DIRECTORY_SEPARATOR)
	&& (pathbuf->buffer[pathbuf->size - 1] != ALTERNATIVE_DIR_SEP)) {
		dynabuf_append(pathbuf, DIRECTORY_SEPARATOR);
	}
	// add file name
	dynabuf_add_string(pathbuf, GLOBALDYNABUF_CURRENT);
	// terminate
	dynabuf_append(pathbuf, '\0');
	// try to open for reading
	stream = fopen(pathbuf->buffer, FILE_READBINARY);
	if (config.process_verbosity >= 9)
		printf("Trying \"%s\"...%s\n", pathbuf->buffer, stream ? " OK!" : "");
	return stream;
}

// open file for reading
// "flags" decide whether library access, search paths or absolute path is wanted.
// file name is expected in GlobalDynaBuf, in platform style and terminated.
// returns NULL or open stream
// on success, GlobalDynaBuf contains full file name in platform style
FILE *includepaths_open_ro(struct filespecflags *flags)
{
	FILE		*stream;
	struct ipi	*ipi;

	if (flags->uses_lib) {
		// use library prefix
		library_path_to_pathbuf();
		stream = combine_and_open_ro();
	} else {
		// first try current default prefix
		if (flags->absolute)
			dynabuf_clear(pathbuf);	// which is "" if absolute
		else
			default_path_to_pathbuf();	// or current path if relative
		stream = combine_and_open_ro();
		if (stream == NULL) {
			// default prefix failed, so try list entries:
			// (does not seem to make much sense for absolute paths,
			// but maybe some windows user used search paths like
			// "D:" or "E:" - oh well...)
			for (ipi = ipi_head.next; ipi != &ipi_head; ipi = ipi->next) {
				dynabuf_clear(pathbuf);
				dynabuf_add_string(pathbuf, ipi->path);
				stream = combine_and_open_ro();
				if (stream) {
					break;
				}
			}
		}
	}
	if (stream) {
		// copy successful file name back to GlobalDynaBuf
		dynabuf_clear(GlobalDynaBuf);
		dynabuf_add_string(GlobalDynaBuf, pathbuf->buffer);
		dynabuf_append(GlobalDynaBuf, '\0');
	} else {
		// CAUTION, I'm re-using the path dynabuf to assemble the error message:
		dynabuf_clear(pathbuf);
		dynabuf_add_string(pathbuf, "Cannot open input file ");
		dynabuf_append(pathbuf, flags->uses_lib ? '<' : '\"');
		dynabuf_add_string(pathbuf, GLOBALDYNABUF_CURRENT);
		dynabuf_append(pathbuf, flags->uses_lib ? '>' : '\"');
		dynabuf_append(pathbuf, '.');
		dynabuf_append(pathbuf, '\0');
		throw_error(pathbuf->buffer);
	}
	//fprintf(stderr, "File is [%s]\n", GLOBALDYNABUF_CURRENT);
	return stream;
}
