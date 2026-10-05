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

    Example: mini-asm -t arm -l

    Constants can be declared with `NAME EQU value` and used as immediate
    operands (`MOVE #NAME,D0`) or `dc` values. Values may be decimal, `$`-hex,
    or `0x`-hex literals.

    The instruction set includes MOVEQ, CLR, TST, AND, OR, EOR, JMP, JSR,
    ADDQ, SUBQ, and DBRA. Use `-l` for the complete target-specific listing.
    Supported effective-address forms are listed per instruction; current
    memory operands are `(An)`, `(An)+`, and `-(An)`.
  
  ### Feedback

    Send your comments and suggestions to /dev/null
