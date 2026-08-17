// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// Character encoding stuff
#ifndef encoding_H
#define encoding_H


extern const struct encoder	*encoder_current;	// gets set before each pass	TODO - set for each part
extern const struct encoder	encoder_raw;
extern const struct encoder	encoder_pet;
extern const struct encoder	encoder_scr;
extern const struct encoder	encoder_file;
extern unsigned char		*encoding_loaded_table;	// ...loaded from file


// prototypes

// convert character using current encoding
extern unsigned char encoding_encode_char(unsigned char byte);

// set "raw" as default encoding
extern void encoding_passinit(void);


#endif
