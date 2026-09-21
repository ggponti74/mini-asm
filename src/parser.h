#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>
#include <stddef.h>

#include "cpu68k.h"
#include "opcodes.h"

// Parse a single line of source code.
// Returns an OpcodeEntry pointer if valid, NULL if error.
// line_num is used for error reporting.
const OpcodeEntry* parse_line(const char *line, size_t line_num);

// Operand structure
typedef struct {
    OperandType type;
    union {
        int reg;          // register index (e.g., D0 = 0, A0 = 8, etc.)
        int32_t imm;      // immediate value
        const char *label;// label name (resolved later)
    } value;
} Operand;

// Extract operands from a line into an array
size_t extract_operands(const char *line, Operand *ops, size_t max_ops);

#endif // PARSER_H
