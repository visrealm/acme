// ToACME - converts other source codes to ACME format.
// Copyright (C) 2025 Sven Friedrichs
// Have a look at "main.c" for further info
//
// "TopAss" stuff

#include "config.h"
#include "acme.h"
#include "gighyp.h"
#include "io.h"
#include "pet2iso.h"
#include "mnemo.h"
#include <string.h>	// for strcpy and strncpy

// Constants
const char	*TokenOpcode_Table[]	= {
	MnemonicLDA, MnemonicSTA, MnemonicLDX, MnemonicLDY, MnemonicCMP, MnemonicADC, MnemonicAND, MnemonicSTX,
	MnemonicSTY, MnemonicINC, MnemonicCPX, MnemonicASL, MnemonicBIT, MnemonicLSR, MnemonicORA, MnemonicROL,
	MnemonicROR, MnemonicSBC, MnemonicCPY, MnemonicDEC, MnemonicEOR, MnemonicJMP, MnemonicJSR, MnemonicTXA,
	MnemonicTAX, MnemonicTYA, MnemonicTAY, MnemonicTSX, MnemonicTXS, MnemonicINY, MnemonicPLP, MnemonicPHA,
	MnemonicPLA, MnemonicINX, MnemonicDEY, MnemonicRTS, MnemonicNOP, MnemonicCLC, MnemonicSEC, MnemonicCLI,
	MnemonicSEI, MnemonicCLV, MnemonicCLD, MnemonicSED, MnemonicRTI, MnemonicPHP, MnemonicDEX, MnemonicBRK,
	MnemonicBPL, MnemonicBMI, MnemonicBVC, MnemonicBVS, MnemonicBCC, MnemonicBCS, MnemonicBNE, MnemonicBEQ,
};

// functions

// handle .base .text .source
struct Instructs process_pseudo_opcode(void)
{
	int			erstesZeichen,
				flags	= FLAG_INSERT_SPACE;
	char			txtOpcode[20];
	bool			isFound	= FALSE;
	struct Instructs	instructs;

	erstesZeichen = IO_get_byte();
	switch (erstesZeichen) {
	case 0x90:
		isFound = TRUE;
		IO_put_string(ACME_set_pc);
		break;
	case 0x91:
		isFound = TRUE;
		IO_put_string(ACME_po_pet);
		flags |= FLAG_TASK_WITH_COMMA;
		strncpy(txtOpcode, ACME_po_pet, 19);
		break;
	case 0x8f:
		isFound = TRUE;
		flags &= ~FLAG_INSERT_SPACE;
		break;
	case 0xa2:
		isFound = TRUE;
		IO_put_string(ACME_po_source);
		flags |= FLAG_TASK_WITH_COMMA;
		strncpy(txtOpcode, ACME_po_source, 19);
		break;
	default:
		// Optional: Add default behavior if necessary
		break;
	}

	if (!isFound) {
		char	hexString[255];

		IO_put_string("; ToACME: .");
		if (erstesZeichen) {
			sprintf(hexString, "0x%X", erstesZeichen); 
			IO_put_string(hexString);
		}
		IO_put_string(" cannot be converted\n");
	} else {
		IO_get_byte();	// exit with unused byte pre-read
	}
	
	instructs.flags = flags;
	strcpy(instructs.txtOpcode, txtOpcode);
	return instructs;
}

// process opcode
static void process_real_opcode(void)	// character was last read
{
	if (GotByte > 0x80 && GotByte < 0xc7) {
		IO_put_string(TokenOpcode_Table[GotByte - 0x81]);
		IO_get_byte();	// exit with unused byte pre-read
	}
}

// Main routine for TopAss conversion
//
void top_main(void)
{
	int	indent;

	IO_set_input_padding(0);
	IO_process_load_address();
	ACME_switch_to_pet();
	// loop: once for every line in the file
	while (!IO_reached_eof) {
		// skip link pointer (if it's zero, report as end marker)
		if (IO_get_le16() == 0)
			IO_put_string("; ToACME: Found BASIC end marker.\n");

		IO_get_le16();	// skip line number

		// process line
		IO_get_byte();
		indent = 0;
		if ((GotByte != ' ') && (GotByte != ';') && (GotByte != '\0'))
			indent = GigaHypra_label_definition();

		// skip spaces
		while (GotByte == ' ')
			IO_get_byte();

		// if there is an opcode, process it
		if ((GotByte != ';') && (GotByte != '\0')) {
			GigaHypra_indent(indent);
			// branch to relevant routine
			if (GotByte == '.') {
				GigaHypra_argument_with_instructs(process_pseudo_opcode(), indent);
			} else {
				process_real_opcode();
				GigaHypra_argument(FLAG_INSERT_SPACE);
			}
		}

		// skip comment, if there is one
		if (GotByte == ';')
			GigaHypra_comment();

		// end of line
		IO_put_byte('\n');
	}
}
