## Minimal 68K assembler for x86/ELF/ARM

  ### Usage

    mini-asm [-t target] [-o output] <source.asm>
    mini-asm [-t target] -l

      -t, --target <name>   output format to assemble to (default: x86)
      -o, --output <file>   override the assembled file's name
      -l, --list            list the instructions and directives available
                           for the selected target (no source file needed)
      -h, --help            show this help

    Available targets:

      x86    Windows PE32 console executable (x86)
      arm    Bare-metal/Linux ELF32 executable (ARM)
      elf    Linux ELF32 executable (x86)
      bin    Flat binary (x86)
      bin-arm Flat binary (ARM)
      z80    Flat binary (Z80; core encodings for NOP, LD, arithmetic/logical,
             rotates/shifts, BIT/RES/SET, block transfer/search, IN/OUT and block I/O,
             interrupt/special-register operations, 16-bit absolute pair transfers,
             IX/IY register pairs and IXH/IXL/IYH/IYL, indexed memory
             operands (IX+d)/(IY+d), including DDCB/FDCB bit/rotate/shift forms,
             JR/DJNZ, JP/CALL/RET, PUSH/POP, and EX/EXX exchange forms)
      6501   Flat binary (official 6501/6502 instruction set and addressing modes)

    Example: mini-asm -t arm -l

    Constants can be declared with `NAME EQU value` and used as immediate
    operands (`MOVE #NAME,D0`) or `dc` values. Values may be decimal, `$`-hex,
    or `0x`-hex literals.

    Source files can include other files with `INCLUDE "path/to/file.asm"`.
    Relative paths are resolved from the file containing the INCLUDE directive.

    The 68K-style instruction set includes MOVEQ, CLR, TST, AND, OR, EOR, JMP, JSR,
    ADDQ, SUBQ, and DBRA. Use `-l` for the complete target-specific listing.
    Supported effective-address forms are listed per instruction; current
    memory operands are `(An)`, `(An)+`, and `-(An)`.

    The Z80 and 6501 targets are flat-binary encoders for their respective CPU
    instruction sets. On Z80, indexed memory syntax is `(IX)`, `(IX+5)`,
    or `(IY-2)`; signed offsets must fit in -128..127 and may use EQU constants.
    The 6501 target supports official NMOS 6501/6502 opcodes
    and implied, accumulator, immediate, zero-page, absolute, indexed, indirect,
    and relative addressing. Use `#value` for immediates, `ASL A` for an
    accumulator shift, `$xx` for zero-page
    addresses, `$xxxx` for absolute addresses, `address,X`/`address,Y` for
    indexed addressing, `($xx,X)` for indexed-indirect and `($xx),Y` for indirect-indexed addressing,
    and `JMP ($xxxx)` for indirect jumps. Numeric addresses through `$FF`
    select zero-page forms when that instruction has a matching zero-page
    encoding; otherwise the assembler uses its available absolute form
    (for example, `JMP $10` and `LDA $10,Y`). Symbolic addresses use absolute
    forms to keep instruction sizes stable across assembly passes. `BRA` remains available as
    a 65C02 extension for compatibility, but is not part of the original NMOS
    6501/6502 instruction set. The test suite checks exact raw bytes across the
    official instruction forms and validates relative-branch ranges.
  
  ### Feedback

    Send your comments and suggestions to /dev/null
