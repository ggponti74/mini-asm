#ifndef ARM_ELF_WRITER_H
#define ARM_ELF_WRITER_H

#include "elf32_min.h"   // provides Elf32_Ehdr, Elf32_Phdr, constants like ET_EXEC, EM_386

#include "codegen.h"

#define EPILOGUE_SIZE 72   // entry stub (argc/argv -> D0/A0), BL, exit(D0), and the register-block address

void write_arm_elf(const char *filename, const OutputBuffer *buf);

#endif
