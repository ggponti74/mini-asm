#ifndef PE_WRITER_H
#define PE_WRITER_H

#include <stdio.h>

#include "codegen.h"

void write_pe(const char *filename, const OutputBuffer *buf);

// Start a new PE file, writing DOS + PE headers
void pe_begin(FILE *f);

// Finalize the PE file
void pe_end(FILE *f);

// Later you can add:
// void pe_add_section(FILE *f, const char *name, const void *data, size_t size);
// void pe_end(FILE *f);

#endif // PE_WRITER_H
