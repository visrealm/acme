// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// output buffer stuff
#ifndef output_H
#define output_H


#include "config.h"


// constants

// segment flags
#define	SEGMENT_FLAG_OVERLAY	(1u << 0)	// do not warn about this segment overwriting another one
#define	SEGMENT_FLAG_INVISIBLE	(1u << 1)	// do not warn about other segments overwriting this one


// prototypes

// called once on startup, inits structs
extern void output_init(void);

// called before each pass, clears segment list and disables output
extern void output_passinit(void);

// called after each pass, closes last code segment and calculates outbuffer size
extern void output_endofpass(void);

// skip over some bytes in output buffer without starting a new segment
// (used by "!skip")
extern void output_skip(int size);

// send low byte of arg to output buffer and advance pointer
// FIXME - replace by output_sequence(char *src, size_t size)
extern void (*output_byte)(intval_t);

// remember current outbuf index as start/limit of output file
extern void outbuf_set_outfile_start(void);
extern void outbuf_set_outfile_limit(void);

// get/set "encryption" byte
extern char output_get_xor(void);
extern void output_set_xor(char xor);

// set program counter to defined value -> start a new segment.
// this will in turn set the outbuf index according to the current pseudopc offset.
extern void programcounter_set(intval_t new_pc, bits segment_flags);

// get program counter value
extern void programcounter_read_pc(struct number *target);
// get program counter and check if defined
extern void programcounter_read_asterisk(struct number *target);

// get size of current statement (until now) - needed for "!bin" verbose output
extern int output_get_statement_size(void);

// adjust program counter (called at end of each statement)
extern void output_end_statement(void);

// return start and size of memory block to write to output file,
// along with load address for cbm/apple headers.
extern void output_get_result(const char **ptr, intval_t *size, intval_t *loadaddr);


// pseudopc stuff:
struct pseudopc;

// start offset assembly
extern void pseudopc_start(struct number *new_pc);

// end offset assembly
extern void pseudopc_end(void);

// un-pseudopc a value by given number of levels
extern void pseudopc_unpseudo(struct number *target, struct pseudopc *context, unsigned int levels);

// return pointer to current "pseudopc" struct
// this gets called when parsing label definitions
extern struct pseudopc *pseudopc_get_context(void);

// returns nonzero if "!pseudopc" is in effect, zero otherwise
extern int pseudopc_isactive(void);


#endif
