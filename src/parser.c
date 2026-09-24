#include <ctype.h>
#include <stdio.h>
#include <stdlib.h> // for atoi
#include <string.h>

#include "opcodes.h"
#include "parser.h"

// Simple error reporting
static void report_error(size_t line, size_t col, const char *msg) {
  fprintf(stderr, "Error at line %zu, column %zu: %s\n", line, col, msg);
}

const OpcodeEntry *parse_line(const char *line, size_t line_num) {
  char mnemonic[32];
  int mnemonic_end = 0;
  int n = sscanf(line, "%31s%n", mnemonic, &mnemonic_end);
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

  // Validate operand count for zero-operand instructions (e.g. RTS, RET)
  if (entry->operand_count == 0) {
    char extra[32];
    if (sscanf(line + mnemonic_end, "%31s", extra) == 1) {
      report_error(line_num, (size_t)(mnemonic_end + 2),
                   "instruction does not take operands");
      return NULL;
    }
  }
  // Future: add MOVE, ADD, etc. with operand validation

  return entry;
}

// Very simple tokenizer for demo purposes
size_t extract_operands(const char *line, Operand *ops, size_t max_ops) {
  size_t count = 0;
  const char *p = strchr(line, ' '); // find first space after mnemonic
  if (!p)
    return 0;
  p++; // move past space

  char token[64];
  while (*p && count < max_ops) {

    // skip white spaces
    while (*p != '\0' && (isspace((unsigned char)*p) || *p == ',')) {
      p++;
    }

    // copy token until comma or end
    size_t len = 0;
    while (*p && *p != ',' && *p != '\n' && *p != '\t' &&
           len < sizeof(token) - 1) {
      token[len++] = *p++;
    }
    token[len] = '\0';

    // classify operand
    if (token[0] == 'D' && isdigit(token[1])) {
      ops[count].type = OPERAND_REGISTER;
      ops[count].value.reg = token[1] - '0';
    } else if (token[0] == '#') {
      ops[count].type = OPERAND_IMMEDIATE;
      ops[count].value.imm = atoi(&token[1]);
    } else {
      ops[count].type = OPERAND_LABEL;
      ops[count].value.label = strdup(token);
    }

    count++;
  }

  return count;
}