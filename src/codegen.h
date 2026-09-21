#ifndef CODEGEN_H
#define CODEGEN_H

#include "opcodes.h"
#include "parser.h"

// Simple output buffer
typedef struct {
    uint8_t *data;
    size_t   size;
    size_t   capacity;
} OutputBuffer;

void buffer_write(OutputBuffer *out, uint8_t byte);
void emit_code(const OpcodeEntry *entry, Operand *operands, OutputBuffer *out);

#endif
