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

// Returns 0 on success, or -1 if the operands can't be encoded for the
// active architecture (e.g. a register with no mapping yet).
int emit_code(const OpcodeEntry *entry, Operand *operands, OpSize size,
              OutputBuffer *out);

// After emit_code() returns -1: a human-readable reason, or NULL if there
// isn't a specific one.
const char *codegen_error(void);

#endif
