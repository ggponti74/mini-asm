#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "directives.h"
#include "symtab.h"

// Size in bytes of one dc.x value: 1, 2 or 4. Returns 0 if `token` is
// not a dc directive.
static size_t dc_unit(const char *token) {
  if (strlen(token) != 4)
    return 0;
  if (tolower((unsigned char)token[0]) != 'd' ||
      tolower((unsigned char)token[1]) != 'c' || token[2] != '.')
    return 0;
  switch (tolower((unsigned char)token[3])) {
  case 'b':
    return 1;
  case 'w':
    return 2;
  case 'l':
    return 4;
  default:
    return 0;
  }
}

int directive_is(const char *token) { return dc_unit(token) != 0; }

static int dc_error(size_t line_num, const char *fmt, const char *arg) {
  fprintf(stderr, "Error at line %zu: ", line_num);
  fprintf(stderr, fmt, arg);
  fputc('\n', stderr);
  return 1;
}

// Parses a numeric literal: decimal (optionally signed), $hex or 0xhex.
// Returns 1 and sets *out on success, 0 if `tok` isn't a valid number.
static int parse_number(const char *tok, long long *out) {
  const char *p = tok;
  int neg = 0;
  if (*p == '-') {
    neg = 1;
    p++;
  } else if (*p == '+') {
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

  if (*p == '\0')
    return 0;
  for (const char *q = p; *q; q++) {
    if (base == 16 ? !isxdigit((unsigned char)*q) : !isdigit((unsigned char)*q))
      return 0;
  }

  errno = 0;
  long long v = strtoll(p, NULL, base);
  if (errno == ERANGE)
    return 0;
  *out = neg ? -v : v;
  return 1;
}

static int is_identifier(const char *s) {
  if (!(isalpha((unsigned char)s[0]) || s[0] == '_'))
    return 0;
  for (const char *p = s + 1; *p; p++)
    if (!(isalnum((unsigned char)*p) || *p == '_'))
      return 0;
  return 1;
}

// Both current targets (x86 PE/ELF, ARM ELF) are little-endian, so
// multi-byte values are emitted low byte first.
static void emit_value(OutputBuffer *buf, long long v, size_t unit) {
  unsigned long long u = (unsigned long long)v;
  for (size_t i = 0; i < unit; i++)
    buffer_write(buf, (uint8_t)((u >> (8 * i)) & 0xFF));
}

static int in_range(long long v, size_t unit) {
  switch (unit) {
  case 1:
    return v >= -128 && v <= 255;
  case 2:
    return v >= -32768 && v <= 65535;
  default:
    return v >= -2147483648LL && v <= 4294967295LL;
  }
}

int directive_assemble(const char *code, size_t line_num, int pass,
                       OutputBuffer *buf, uint32_t code_base) {
  const char *p = code;
  while (*p && isspace((unsigned char)*p))
    p++;
  const char *name_start = p;
  while (*p && !isspace((unsigned char)*p))
    p++;

  char name[16];
  size_t name_len = (size_t)(p - name_start);
  if (name_len >= sizeof name)
    return -1;
  memcpy(name, name_start, name_len);
  name[name_len] = '\0';

  size_t unit = dc_unit(name);
  if (unit == 0)
    return -1; // not a directive

  int count = 0;
  for (;;) {
    while (*p && isspace((unsigned char)*p))
      p++;
    if (*p == '\0') {
      if (count == 0)
        return dc_error(line_num, "%s needs at least one value", name);
      return dc_error(line_num, "%s: expected a value after ','", name);
    }

    // One value: everything up to a comma, whitespace or end of line.
    char tok[64];
    size_t len = 0;
    while (*p && *p != ',' && !isspace((unsigned char)*p)) {
      if (len < sizeof tok - 1)
        tok[len++] = *p;
      p++;
    }
    tok[len] = '\0';
    if (len == 0)
      return dc_error(line_num, "%s: expected a value", name);

    long long value = 0;
    if (tok[0] == '#') {
      return dc_error(line_num, "%s values don't take '#'", name);
    } else if (parse_number(tok, &value)) {
      /* literal */
    } else if (is_identifier(tok)) {
      if (pass == 2) {
        size_t off;
        if (!symtab_find(tok, &off))
          return dc_error(line_num, "undefined label '%s'", tok);
        value = (long long)code_base + (long long)off;
      }
    } else {
      return dc_error(line_num, "invalid value '%s'", tok);
    }

    if (!in_range(value, unit))
      return dc_error(line_num, "value '%s' is out of range for this size",
                      tok);

    emit_value(buf, value, unit);
    count++;

    while (*p && isspace((unsigned char)*p))
      p++;
    if (*p == '\0')
      return 0; // end of list
    if (*p != ',')
      return dc_error(line_num, "%s: expected ',' between values", name);
    p++; // consume the comma
  }
}
