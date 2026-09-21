#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#include "cpu68k.h"

// Parse a single line of source code.
// Returns an OpcodeEntry pointer if valid, NULL if error.
// line_num is used for error reporting.
const OpcodeEntry* parse_line(const char *line, size_t line_num);

#endif // PARSER_H
