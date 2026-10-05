#ifndef ARM_ELF_WRITER_H
#define ARM_ELF_WRITER_H

#include "elf32_min.h"   // provides Elf32_Ehdr, Elf32_Phdr, constants like ET_EXEC, EM_386

#include "codegen.h"

#define EPILOGUE_SIZE 24   // BL, load D0, exit syscall, and D0 slot address

void write_arm_elf(const char *filename, const OutputBuffer *buf);

#endif
