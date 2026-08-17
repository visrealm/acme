// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// Type system stuff
#ifndef typesystem_H
#define typesystem_H


#include "config.h"


// warn if result is not integer
extern void typesystem_want_nonaddr(struct number *result);

// warn if result is not address
extern void typesystem_want_addr(struct number *result);


#endif
