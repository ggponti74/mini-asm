#ifndef ELF32_MIN_H
#define ELF32_MIN_H

// Minimal, self-contained ELF32 definitions.
//
// arm_elf_writer.c and elf_writer.c only need a handful of ELF32
// structures/constants to hand-assemble a headers-only executable.
// The previous code pulled these from the host's <elf.h>, which is a
// glibc/Linux header and doesn't exist when building on Windows/MSVC.
// Since target selection now happens at runtime (see platform.h) and a
// single binary must be able to emit ELF output regardless of which
// host it was built on, we define the needed subset ourselves instead
// of depending on the host toolchain's ELF header.

#include <stdint.h>

#define EI_NIDENT 16

typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

// e_ident[] indices
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5
#define EI_VERSION 6

// e_ident[] magic values
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

// EI_CLASS
#define ELFCLASS32 1

// EI_DATA
#define ELFDATA2LSB 1

// e_version / EI_VERSION
#define EV_CURRENT 1

// e_type
#define ET_EXEC 2

// e_machine
#define EM_386 3
#define EM_ARM 40

// p_type
#define PT_LOAD 1

// p_flags
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

#endif // ELF32_MIN_H
