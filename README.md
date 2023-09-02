# ACME Assembler

![msbuild-badge](https://github.com/visrealm/acme/actions/workflows/msbuild.yml/badge.svg)

ACME is a free cross assembler released under the GNU GPL.

It can produce code for the following processors: 6502, 6510 (including illegal opcodes), 65c02 and 65816.

ACME supports the standard assembler stuff like global/local/anonymous labels, offset assembly, conditional assembly and looping assembly. It can include other source files as well as binaries while assembling.

Calculations can be done in integer or float mode.

Oh, and it is fast.

## Why visrealm/acme ?

This fork was created from the SVN source at https://sourceforge.net/projects/acme-crossass (Revision 323) to support development of the [HBC-56 Homebrew Computer](https://github.com/visrealm/hbc-56) and [vrEmu6502 6502/65C02 Emulator](https://github.com/visrealm/vrEmu6502). 

### Improvements
* Added support for [Intel HEX](https://en.wikipedia.org/wiki/Intel_HEX) output format `-f hex`.
* Output absolute filename of source file in error messages.

The absolute filename output allows you to write a problemMatcher for VSCode using "absolute" fileLocation. This is necessary if you're using the `-I` command-line argument to ACME as VSCode doesn't know where the file came from. Example problemMatcher for a build task:

```json
"problemMatcher": {
    "owner": "acme",
    "fileLocation": [
        "absolute"
    ],
    "pattern": {
        "regexp": "([Ee]rror - File\\s+(.*), line (\\d+) (\\((Zone|Macro) .*\\))?:\\s+(.*))$",
        "file": 2,
        "location": 3,
        "message": 1
    }
},
```

# License

This code is licensed under the [GNU General Public License version 2.0 (GPLv2)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) license
