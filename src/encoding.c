// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// Character encoding stuff
#include "encoding.h"


// struct definition
struct encoder {
	unsigned char	(*fn)(unsigned char);
	// maybe add table pointer?
};


// variables
static unsigned char	outermost_table[256];	// space for encoding table...
const struct encoder	*encoder_current;	// gets set before each pass
unsigned char		*encoding_loaded_table	= outermost_table;	// ...loaded from file


// encoder functions:


// convert raw to raw (do not convert at all)
static unsigned char encoderfn_raw(unsigned char byte)
{
	return byte;
}
// convert raw to petscii
static unsigned char encoderfn_pet(unsigned char byte)
{
	if ((byte >= (unsigned char) 'A') && (byte <= (unsigned char) 'Z'))
		return byte | 0x80;
	if ((byte >= (unsigned char) 'a') && (byte <= (unsigned char) 'z'))
		return byte - 32;
	return byte;
}
// convert raw to C64 screencode
static unsigned char encoderfn_scr(unsigned char byte)
{
	if ((byte >= (unsigned char) 'a') && (byte <= (unsigned char) 'z'))
		return byte - 96;	// shift uppercase down
	if ((byte >= (unsigned char) '[') && (byte <= (unsigned char) '_'))
		return byte - 64;	// shift [\]^_ down
	if (byte == '`')
		return 64;	// shift ` down
	if (byte == '@')
		return 0;	// shift @ down
	return byte;
}
// convert raw to whatever is defined in table
static unsigned char encoderfn_file(unsigned char byte)
{
	return encoding_loaded_table[byte];
}


// predefined encoder structs:


const struct encoder	encoder_raw	= {
	encoderfn_raw
};
const struct encoder	encoder_pet	= {
	encoderfn_pet
};
const struct encoder	encoder_scr	= {
	encoderfn_scr
};
const struct encoder	encoder_file	= {
	encoderfn_file
};


// exported functions


// convert character using current encoding (exported for use by alu.c and pseudoopcodes.c)
unsigned char encoding_encode_char(unsigned char byte)
{
	return encoder_current->fn(byte);
}

// set "raw" as default encoding
void encoding_passinit(void)
{
	encoder_current = &encoder_raw;
}
