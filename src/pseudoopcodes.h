// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// pseudo opcode stuff
#ifndef pseudoopcodes_H
#define pseudoopcodes_H


// parse pseudo opcode. has to be re-entrant.
extern void pseudoopcode_parse(void);

// this is not really a pseudo opcode, but similar enough to be put here:
// call when "*= EXPRESSION" is parsed
extern void notreallypo_setpc(void);


#endif
