#ifndef ELF_WRITER_H
#define ELF_WRITER_H

#include "elf32_min.h"   // provides Elf32_Ehdr, Elf32_Phdr, constants like ET_EXEC, EM_386

#include "codegen.h"

// call code_addr ; mov eax,1 ; xor ebx,ebx ; int 0x80  ->  CALL into the
// user's code, then Linux sys_exit(0) once it RETs back here. See
// write_elf() for why the CALL (not a fallthrough) is needed.
#define ELF_EPILOGUE_SIZE 14

void write_elf(const char *filename, const OutputBuffer *buf);

#endif
