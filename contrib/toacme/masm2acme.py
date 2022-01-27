#!/usr/bin/env python3
import sys

def line_preprocess(line):
    "split line into comment, strings and everything else"
    result = []
    part = ""
    comment = None
    quotes = None
    for char in line:
        # are we inside comment?
        if comment:
            comment += char
            continue
        # are we inside quotes?
        if quotes:
            part += char
            if char == quotes:
                # end of quotes
                # if previous part was also quoted, we have to combine them,
                # because "a""b" really means a"b
                if result and result[-1][-1] == quotes:
                    part = result[-1][:-1] + "\\" + part
                    result.pop()
                # use singlequotes for 1-char strings
                if len(part) == 3:
                    part = "'" + part[1] + "'"
                # escape backslash, singlequote, doublequote
                if part == "'\\'":
                    part = "'\\\\'"
                elif part == "'''":
                    part = "'\\''"
                elif part == '"\\""':
                    part = "'\"'"
                result.append(part) # move quoted string to result list
                part = ""
                quotes = None
            continue
        # not in quotes
        if char == '"' or char == "'":
            # new quotes, so finish old part
            if part and part != ' ':
                result.append(part)
            # ...and start new one
            part = char
            quotes = char
            continue
        # comment?
        if char == ';':
            # finish old part
            if part and part != ' ':
                result.append(part)
            part = ""
            # ...and start comment
            comment = char
            continue
        ## tab-to-space:
        #if char == '\t':
        #    char = ' '
        # skip blanks after blank
        if part.endswith(' ') and char == ' ':
            pass
        else:
            # all other characters:
            part += char
    # quotes still open at end of line?
    if quotes:
        raise Exception("Unterminated string constant in input data")
    # append last part
    if part:
        result.append(part)
    return result, comment

def single_out(items, substring):
    "split any item containing substring into first part, substring, second part. empty parts are dropped."
    result = []
    for i in items:
        while substring in i:
            parts = i.partition(substring)
            if parts[0]:
                result.append(parts[0])
            result.append(substring)
            i = parts[2]
        if i:
            result.append(i)
    return result

def unquoted_tokenize(part):
    "split part into tokens (so do not pass string literals!)"
    # split at spaces (and throw away all spaces)
    items = part.split()
    # split at commas, braces, ...
    items = single_out(items, ',')
    items = single_out(items, '/')
    items = single_out(items, '=')
    items = single_out(items, '+')
    items = single_out(items, '-')
    items = single_out(items, '*')
    return items

opcodes_to_keep = [
    # std 6502:
    "brk", "rti", "rts", "nop",
    "php", "plp", "pha", "pla",
    "bpl", "bmi", "bvc", "bvs", "bcc", "bcs", "bne", "beq",
    "clc", "sec", "cli", "sei", "clv", "cld", "sed",
    "dex", "dey", "inx", "iny",
    "tax", "tay", "txa", "tya", "tsx", "txs",
    # new in 65c02:
    "phx", "plx", "phy", "ply", "bra"   # inc, dec
]

opcodes_with_arg = [
    # std 6502:
    "ora", "and", "eor", "adc", "sta", "lda", "cmp", "sbc",
    "asl", "rol", "lsr", "ror", "dec", "inc",
    "ldx", "stx", "cpx", "ldy", "sty", "cpy",
    "jsr", "jmp", "bit",
    # new in 65c02:
    "tsb", "trb", "stz"
]

token_substitutions = {
    ".": "*",   # program counter
    ":not:": "not", # operator
    ":eor:": "xor", # operator
    ":msb:": ">",   # operator?
}

opcodes_to_replace = {
    "org": "*=",
    "cpu": ";!cpu",         # TODO: support properly!
    "=": "!tx",
    "$": "!wo", # actually & instead of $, but substitution was done earlier
    "end": "!eof",
    "assert": "+assert",
    "lnk": ";!source",      # TODO: support properly!
    "asla": "\tasl",
    "lsra": "\tlsr",
    "rola": "\trol",
    "rora": "\tror",
    "dea": "\tdec",    # 65c02
    "ina": "\tinc",    # 65c02
}

opcodes_to_rename = {
    "clr": "stz"    # 65c02
}

def convert_opcodes(parts):
    "convert mnemonics and pseudo opcodes"
    if not parts:
        return parts
    op = parts[0]
    parts = parts[1:]
    if op in opcodes_to_keep:
        return ["\t" + op] + parts
    if op in opcodes_to_replace:
        return [opcodes_to_replace[op]] + parts
    # wtf?!
    if op == "jmi":
        op = "jmpi"
    elif op == "jmix":
        op = "jmpxi"
    # convert addressing modes
    if len(op) > 3:
        oldop = op
        am = op[3:]
        op = op[:3]
        if am == "im":
            if parts[0] == '/':
                parts[0] = "#>"
            elif parts[0][0] >= 'a' and parts[0][0] <= 'z':
                parts[0] = "#< " + parts[0]
            else:
                parts[0] = "#" + parts[0]
        elif am == "ax" or am == "zx":
            parts[-1] = parts[-1] + ", x"
        elif am == "ay" or am == "zy":
            parts[-1] = parts[-1] + ", y"
        elif am == "xi":
            parts[0] = "(" + parts[0]
            parts[-1] = parts[-1] + ", x)"
        elif am == "iy":
            parts[0] = "(" + parts[0]
            parts[-1] = parts[-1] + "), y"
        elif am == "i":
            parts[0] = "(" + parts[0]
            parts[-1] = parts[-1] + ")"
        else:
            op = oldop
    # convert
    if op in opcodes_to_rename:
        op = opcodes_to_rename[op]
    if op in opcodes_with_arg:
        return ["\t" + op] + parts
    return [op] + parts

def process_code(parts):
    "split code parts at special characters"
    prefix = ""
    # remember if line starts with space
    indented = (parts[0][0] == " ")
    # because now spaces are dropped
    result = []
    for part in parts:
        # do not process quoted strings any further
        if part.startswith("'") or part.startswith('"'):
            result.append(part)
            continue
        # convert to lower case
        part = part.lower()
        # substitute: & becomes $
        part = "$".join(part.split("&"))
        # all other parts are split up into tokens
        result.extend(unquoted_tokenize(part))
    # convert some tokens (string literals are not in danger, as they include quotes)
    parts = result
    result = []
    for part in parts:
        if part in token_substitutions:
            part = token_substitutions[part]
        result.append(part)
    # now convert
    label = ""
    if indented:
        # code
        result = convert_opcodes(result)
    else:
        # label or symbol definition
        if len(result) > 1 and result[1] == "*":
            # symbol definition
            symdef = result[0] + "\t="
            result = [symdef] + convert_opcodes(result[2:])
        else:
            # label
            label = result[0]
            result = convert_opcodes(result[1:])
    if result:
        label = label + "\t"
    return label, result

def process_line(line):
    "process a single line of input and return converted version"
    # remove line ending, if there is one. don't care if NL or CR or combination
    while len(line) != 0 and (line[-1] == "\r" or line[-1] == "\n"):
        line = line[:-1]
    # step 1: split into strings, comments and everything else
    codeparts, comment = line_preprocess(line)
    # step 2: if there is anything before comment, process that
    if codeparts:
        prefix, codeparts = process_code(codeparts)
        # reassemble line
        line = prefix + " ".join(codeparts)
    else:
        line = ""
    if comment:
        line = line + comment
    return line + "\n"

def convert_file(input, output):
    "convert input file to output file line-by-line"
    with open(input, "rt") as infile:
        with open(output, "wt") as outfile:
            outfile.write(";ACME 0.97\n")
            for line in infile:
                outfile.write(process_line(line))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(
"Error: wrong number of arguments\n"
"\n"
"masm2acme.py converts a file from MASM to ACME syntax.\n"
"Usage: masm2acme.py INPUTFILE OUTPUTFILE\n"
        )
    convert_file(sys.argv[1], sys.argv[2])
