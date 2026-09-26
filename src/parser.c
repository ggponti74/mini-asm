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

void strip_comment(char *line) {
  char *semi = strchr(line, ';');
  if (semi) *semi = '\0';

  // Trim trailing whitespace, including the newline fgets() leaves in.
  size_t len = strlen(line);
  while (len > 0 && isspace((unsigned char)line[len - 1])) {
    line[--len] = '\0';
  }
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

  // Find where the operand list starts: skip any leading indentation,
  // then skip the mnemonic itself (the run of non-whitespace characters),
  // then skip the whitespace that separates it from the operands.
  //
  // The previous approach ("the first space in the whole line") broke on
  // any leading indentation before the mnemonic -- that first space would
  // be one of the indentation characters, not the one after the mnemonic,
  // so the mnemonic itself got glued onto the first operand into one
  // unrecognizable token.
  const char *p = line;
  while (*p && isspace((unsigned char)*p)) p++;
  while (*p && !isspace((unsigned char)*p)) p++;
  while (*p && isspace((unsigned char)*p)) p++;
  if (!*p) return 0;

  char token[64];
  while (*p && count < max_ops) {

    // skip white spaces and commas between tokens
    while (*p != '\0' && (isspace((unsigned char)*p) || *p == ',')) {
      p++;
    }
    if (*p == '\0') break;

    // copy token until comma, whitespace, or end
    size_t len = 0;
    while (*p && *p != ',' && !isspace((unsigned char)*p) &&
           len < sizeof(token) - 1) {
      token[len++] = *p++;
    }
    token[len] = '\0';
    if (len == 0) break;

    // classify operand
    if ((token[0] == 'D' || token[0] == 'd') && isdigit((unsigned char)token[1])) {
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