#include <stdio.h>
#include <string.h>

#include "parser.h"
#include "opcodes.h"

// Simple error reporting
static void report_error(size_t line, size_t col, const char *msg) {
    fprintf(stderr, "Error at line %zu, column %zu: %s\n", line, col, msg);
}

// Parse a single line
const OpcodeEntry* parse_line(const char *line, size_t line_num) {
    char mnemonic[32];
    int n = sscanf(line, "%31s", mnemonic);
    if (n != 1) {
        report_error(line_num, 1, "Empty or invalid line");
        return NULL;
    }

    // Lookup mnemonic
    const OpcodeEntry *entry = lookup_opcode(mnemonic);
    if (!entry) {
        report_error(line_num, 1, "Unknown mnemonic");
        return NULL;
    }

    // Validate operand count
    if (strcmp(mnemonic, "RTS") == 0) {
        // RTS must have no operands
        char extra[32];
        if (sscanf(line + strlen(mnemonic), "%31s", extra) == 1) {
            report_error(line_num, (size_t)(strlen(mnemonic) + 2),
                         "RTS does not take operands");
            return NULL;
        }
    }
    // Future: add MOVE, ADD, etc. with operand validation

    return entry;
}
