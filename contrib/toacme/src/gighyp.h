// ToACME - converts other source codes to ACME format.
// Copyright (C) 1999-2025 Marco Baye
// Have a look at "main.c" for further info
//
// stuff needed for "Hypra-Ass", "Giga-Ass", "Top-Ass"
#ifndef gigahypra_H
#define gigahypra_H


#include "config.h"


// Constants
#define FLAG_INSERT_SPACE	(1u << 0)	// insert space before arg
#define FLAG_ADD_LEFT_BRACE	(1u << 1)	// add '{' at end of statement
#define FLAG_ADD_CBM		(1u << 2)	// add file format indicator
#define FLAG_ADD_ZERO		(1u << 3)	// giga string specialty:
	// open quote at end of line is *normal*. Closed quote: add ",0".
#define FLAG_SKIP_OPENING	(1u << 4)	// strip '(' before args
#define FLAG_SKIP_CLOSING	(1u << 5)	// strip ')' after args
#define FLAG_CHANGE_LEFTARROW	(1u << 6)	// giga string specialty:
	// '_' (left arrow on C64) is transformed to CR (0x0d).
#define FLAG_TASK_WITH_COMMA	(1u << 7)	// top allows devices in .source
	// or comma-separated lists after .text


struct Instructs {
	int	flags;
	char	txtOpcode[20];
};


// Prototypes
extern void GigaHypra_comment(void);
extern void GigaHypra_operator(void);
extern void GigaHypra_indent(int indent);
extern void GigaHypra_argument_with_instructs(struct Instructs instructs, int indent);
extern void GigaHypra_argument(int flags);
extern int GigaHypra_label_definition(void);


#endif
