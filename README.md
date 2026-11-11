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
             interrupt control (DI/EI), interrupt/special-register operations, 16-bit absolute pair transfers,
             IX/IY register pairs and IXH/IXL/IYH/IYL, indexed memory
             operands (IX+d)/(IY+d), including DDCB/FDCB bit/rotate/shift forms,
             JR/DJNZ, JP/CALL/RET, RST, PUSH/POP, and EX/EXX exchange forms)
      6501   Flat binary (official 6501/6502 instruction set and addressing modes)

    Example: mini-asm -t arm -l
