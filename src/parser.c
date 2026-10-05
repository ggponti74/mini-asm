#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "directives.h"
#include "opcodes.h"
#include "parser.h"

static int parse_register(const char *s, int *reg);

// malloc-based copy (strdup isn't standard C).
static char *copy_string(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}

static int parse_immediate(const char *token, int32_t *value,
                           const char **symbol) {
  const char *p = token;
  int negative = 0;
  *symbol = NULL;
  if (*p == '-' || *p == '+') {
    negative = *p == '-';
    p++;
  }

  int base = 10;
  if (*p == '$') {
    base = 16;
    p++;
  } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    base = 16;
    p += 2;
  }
  if (!*p) return 0;

  int numeric = 1;
  for (const char *q = p; *q; q++)
    if (base == 16 ? !isxdigit((unsigned char)*q) : !isdigit((unsigned char)*q))
      numeric = 0;
  if (!numeric) {
    if (negative || !(isalpha((unsigned char)token[0]) || token[0] == '_'))
      return 0;
    for (const char *q = token + 1; *q; q++)
      if (!(isalnum((unsigned char)*q) || *q == '_'))
        return 0;
    *symbol = copy_string(token);
    return *symbol != NULL;
  }

  errno = 0;
  long long parsed = strtoll(p, NULL, base);
  if (errno == ERANGE) return 0;
  if (negative) parsed = -parsed;
  if (parsed < INT32_MIN || parsed > (long long)UINT32_MAX) return 0;
  *value = (int32_t)(uint32_t)parsed;
  return 1;
}

// Recognizes the address-register memory modes:  (An)  (An)+  -(An)
// Returns 0 if `tok` isn't an attempt at one (it doesn't start with '(' or
// "-("), 1 and sets *type / *reg (An = 8..15) if it is valid, or -1 if it
// looks like one but is malformed. Spaces inside the token aren't allowed
// (the tokenizer splits on them), and data registers can't be used.
static int parse_indirect(const char *tok, OperandType *type, int *reg) {
  const char *p = tok;
  int pre = 0;
  if (p[0] == '-' && p[1] == '(') {
    pre = 1;
    p++;
  }
  if (p[0] != '(')
    return 0;

  char name[3] = {p[1], p[1] ? p[2] : '\0', '\0'};
  int r;
  if (!parse_register(name, &r) || r < 8 || p[3] != ')')
    return -1;

  const char *tail = p + 4;
  if (pre) {
    if (*tail != '\0')
      return -1;
    *type = OPERAND_PREDEC;
  } else if (*tail == '\0') {
    *type = OPERAND_IND;
  } else if (tail[0] == '+' && tail[1] == '\0') {
    *type = OPERAND_POSTINC;
  } else {
    return -1;
  }
  *reg = r;
  return 1;
}

// Simple error reporting
static void report_error(size_t line, size_t col, const char *msg) {
  fprintf(stderr, "Error at line %zu, column %zu: %s\n", line, col, msg);
}

void strip_comment(char *line) {
  // A ';' starts a comment unless it sits inside a '...' or "..." string
  // (dc.b "a;b"). A doubled delimiter inside a string just closes and
  // reopens it, so plain toggling handles that too.
  char quote = '\0';
  for (char *p = line; *p; p++) {
    if (quote) {
      if (*p == quote)
        quote = '\0';
    } else if (*p == '"' || *p == '\'') {
      quote = *p;
    } else if (*p == ';') {
      *p = '\0';
      break;
    }
  }

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

int parse_size_suffix(const char *line, const OpcodeEntry *entry, OpSize *size,
                      size_t line_num) {
  *size = SIZE_UNSPEC;

  const char *start = line;
  while (*start && isspace((unsigned char)*start))
    start++;
  const char *end = start;
  while (*end && !isspace((unsigned char)*end))
    end++;

  const char *dot = NULL;
  for (const char *q = start; q < end; q++)
    if (*q == '.')
      dot = q;
  if (!dot)
    return 1;

  size_t col = (size_t)(start - line) + 1;
  char c = (dot + 2 == end) ? (char)tolower((unsigned char)dot[1]) : '\0';
  OpSize s = c == 'b' ? SIZE_B : c == 'w' ? SIZE_W : c == 'l' ? SIZE_L : SIZE_UNSPEC;
  if (s == SIZE_UNSPEC) {
    report_error(line_num, col, "Invalid size suffix (use .b, .w or .l)");
    return 0;
  }
  if (!(entry->size_mask & (1u << (s - 1)))) {
    report_error(line_num, col,
                 "this instruction does not accept that size on this target");
    return 0;
  }
  *size = s;
  return 1;
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
  while (*p && isspace((unsigned char)*p))
    p++;
  while (*p && !isspace((unsigned char)*p))
    p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (!*p)
    return 0;

  char token[64];
  while (*p && count < max_ops) {

    // skip white spaces and commas between tokens
    while (*p != '\0' && (isspace((unsigned char)*p) || *p == ',')) {
      p++;
    }
    if (*p == '\0')
      break;

    // copy token until comma, whitespace, or end
    size_t len = 0;
    while (*p && *p != ',' && !isspace((unsigned char)*p) &&
           len < sizeof(token) - 1) {
      token[len++] = *p++;
    }
    token[len] = '\0';
    if (len == 0)
      break;

    ops[count].symbol = NULL;

    // classify operand
    int reg;
    OperandType mem_type;
    int ind = parse_indirect(token, &mem_type, &reg);
    if (ind == 1) {
      ops[count].type = mem_type;
      ops[count].value.reg = reg;
    } else if (ind == -1) {
      ops[count].type = OPERAND_BAD;
      ops[count].value.label = copy_string(token);
    } else if (parse_register(token, &reg)) {
      ops[count].type = OPERAND_REGISTER;
      ops[count].value.reg = reg;
    } else if (token[0] == '#') {
      ops[count].type = OPERAND_IMMEDIATE;
        ops[count].value.imm = 0;
        if (!parse_immediate(token + 1, &ops[count].value.imm,
                           &ops[count].symbol)) {
        ops[count].type = OPERAND_BAD;
        ops[count].value.label = copy_string(token);
      }
    } else {
      ops[count].type = OPERAND_LABEL;
      ops[count].value.label = copy_string(token);
    }

    count++;
  }

  return count;
}

// Recognizes a register name: exactly "D0".."D7" or "A0".."A7" (either
// case). Returns 1 and sets *reg (Dn = 0..7, An = 8..15) on success.
static int parse_register(const char *s, int *reg) {
  if (!s[0] || !s[1] || s[2] != '\0')
    return 0;
  if (s[1] < '0' || s[1] > '7')
    return 0;
  int n = s[1] - '0';
  if (s[0] == 'D' || s[0] == 'd') {
    *reg = n;
    return 1;
  }
  if (s[0] == 'A' || s[0] == 'a') {
    *reg = 8 + n;
    return 1;
  }
  return 0;
}

static int is_register_name(const char *s) {
  int reg;
  return parse_register(s, &reg);
}

// Label names: start with a letter or '_', then letters, digits or '_'.
// Register names are rejected so a label can never be shadowed by the
// register classification in extract_operands().
static int is_valid_label_name(const char *s) {
  if (!(isalpha((unsigned char)s[0]) || s[0] == '_'))
    return 0;
  for (const char *p = s + 1; *p; p++) {
    if (!(isalnum((unsigned char)*p) || *p == '_'))
      return 0;
  }
  return !is_register_name(s);
}

const char *split_label(const char *line, char *label_out, size_t label_size,
                        size_t line_num) {
  label_out[0] = '\0';

  // Skip indentation; `start` is the first token's first character.
  const char *start = line;
  while (*start && isspace((unsigned char)*start))
    start++;

  // First token = run of non-whitespace characters.
  const char *end = start;
  while (*end && !isspace((unsigned char)*end))
    end++;
  size_t len = (size_t)(end - start);
  if (len == 0)
    return start; // empty line; parse_line() will complain

  char first[64];
  if (len >= sizeof first)
    len = sizeof first - 1;
  memcpy(first, start, len);
  first[len] = '\0';

  // Case 1: trailing colon forces label parsing (so "move:" is legal).
  if (first[len - 1] == ':') {
    first[len - 1] = '\0';
    if (!is_valid_label_name(first) || strlen(first) >= label_size) {
      report_error(line_num, (size_t)(start - line) + 1, "Invalid label name");
      return NULL;
    }
    strcpy(label_out, first);
    const char *rest = end;
    while (*rest && isspace((unsigned char)*rest))
      rest++;
    return rest;
  }

  // Case 2: first token is a mnemonic or directive -> no label.
  if (lookup_opcode(first) || directive_is(first))
    return start;

  // Case 3: not a mnemonic, but the second token is -> colon-less label.
  const char *second = end;
  while (*second && isspace((unsigned char)*second))
    second++;
  const char *second_end = second;
  while (*second_end && !isspace((unsigned char)*second_end))
    second_end++;
  size_t len2 = (size_t)(second_end - second);

  if (len2 > 0 && len2 < 32) {
    char tok[32];
    memcpy(tok, second, len2);
    tok[len2] = '\0';
    if (lookup_opcode(tok) || directive_is(tok)) {
      if (!is_valid_label_name(first) || strlen(first) >= label_size) {
        report_error(line_num, (size_t)(start - line) + 1,
                     "Invalid label name");
        return NULL;
      }
      strcpy(label_out, first);
      return second;
    }
  }

  // Case 4: no label; let parse_line() report "Unknown mnemonic".
  return start;
}