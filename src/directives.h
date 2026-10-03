#ifndef DIRECTIVE_H
#define DIRECTIVE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "codegen.h"

// True if `token` is a recognized directive name (dc.b, dc.w, dc.l;
// case-insensitive).
int directive_is(const char *token);

// If `code` (a line with any label already removed) starts with a
// directive, assembles it into `buf` and returns the number of errors
// (0 or 1). Returns -1 if the line is not a directive, so the caller
// should fall through to the normal instruction path.
int directive_assemble(const char *code, size_t line_num, int pass,
                       OutputBuffer *buf, uint32_t code_base);

// Prints the directives mini-asm understands (for -l/--list).
void directive_print_list(FILE *out);

#endif // DIRECTIVE_H