# Minimal 68K assembler for the x86/ELF/ARM architectures.

# Usage

  mini-asm [-t target] [-o output] <source.asm>

    -t, --target <name>   output format to assemble to (default: x86)
    -o, --output <file>   override the assembled file's name
    -h, --help            show this help

  Available targets:
    x86    Windows PE32 console executable (x86)
    arm    Bare-metal/Linux ELF32 executable (ARM)
    elf    Linux ELF32 executable (x86)
  
# Feedback

  Send your comments and suggestions to /dev/null
