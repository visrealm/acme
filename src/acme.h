// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// Main definitions
#ifndef acme_H
#define acme_H


//#include "config.h"


// Prototypes

// tidy up before exiting by saving symbol dump
extern int ACME_finalize(int exit_code);

// exit with "Error in CLI arguments: ..." message
extern void ACME_cli_args_error(const char msg[]);


#endif
