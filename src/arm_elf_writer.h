#ifndef ARM_ELF_WRITER_H
#define ARM_ELF_WRITER_H

#include <elf.h>   // provides Elf32_Ehdr, Elf32_Phdr, constants like ET_EXEC, EM_386

#include "codegen.h"

void write_arm_elf(const char *filename, const OutputBuffer *buf);

#endif
