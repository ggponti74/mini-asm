#ifndef ELF_WRITER_H
#define ELF_WRITER_H

#include "elf32_min.h"   // provides Elf32_Ehdr, Elf32_Phdr, constants like ET_EXEC, EM_386

#include "codegen.h"

// mov eax,[esp] ; lea esi,[esp+4] ; call code_addr ; mov ebx,eax ;
// mov eax,1 ; int 0x80  ->  load the command line into D0/A0, CALL into the
// user's code, then Linux sys_exit(D0) once it RETs back here. See
// write_elf() for why the CALL (not a fallthrough) is needed.
#define ELF_EPILOGUE_SIZE 21

void write_elf(const char *filename, const OutputBuffer *buf);

#endif
