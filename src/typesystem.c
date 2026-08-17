// ACME - a crossassembler for producing 6502/65c02/65816/65ce02 code.
// Copyright (C) 1998-2024 Marco Baye
// Have a look at "acme.c" for further info
//
// type system stuff
#include "typesystem.h"
#include "global.h"


// Functions

// warn if result is not integer
void typesystem_want_nonaddr(struct number *result)
{
	if (!config.warn_on_type_mismatch)
		return;

	if (result->ntype == NUMTYPE_UNDEFINED)
		return;

	if (result->addr_refs != 0) {
		throw_finalpass_warning("Wrong type - expected integer.");
		//printf("refcount should be 0, but is %d\n", result->addr_refs);
	}
}

// warn if result is not address
void typesystem_want_addr(struct number *result)
{
	if (!config.warn_on_type_mismatch)
		return;

	if (result->ntype == NUMTYPE_UNDEFINED)
		return;

	if (result->addr_refs != 1) {
		throw_finalpass_warning("Wrong type - expected address.");
		//printf("refcount should be 1, but is %d\n", result->addr_refs);
	}
}
