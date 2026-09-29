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

// Strips a trailing ';' comment (and the whitespace before it) from `line`
// in place, along with any trailing newline/CR. Call this on each raw line
// before parse_line()/extract_operands() so neither has to know about
// comment syntax. After stripping, a comment-only or blank line becomes
// an empty string ("") -- callers should treat that as "nothing to do
// here", not a parse error.
void strip_comment(char *line);

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

// Splits an optional leading label off `line`. Copies the label name into
// label_out ("" if none) and returns a pointer to the instruction part of
// the line, which is "" for a label-only line. Returns NULL on error
// (already reported).
const char *split_label(const char *line, char *label_out,
                        size_t label_size, size_t line_num);
                        
#endif // PARSER_H
