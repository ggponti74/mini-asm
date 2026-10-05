#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "directives.h"
#include "srcloc.h"
#include "symtab.h"

static int ci_equal(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}

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

int directive_is_equ(const char *token) { return ci_equal(token, "EQU"); }

int directive_is_include(const char *token) { return ci_equal(token, "INCLUDE"); }

int directive_is(const char *token) {
  return directive_is_equ(token) || directive_is_include(token) ||
         dc_unit(token) != 0;
}

void directive_print_list(FILE *out) {
  fprintf(out, "Directives:\n");
  fprintf(out, "  EQU              define an absolute constant: NAME EQU value\n");
  fprintf(out, "  INCLUDE          assemble another source file at this location\n");
  fprintf(out, "  dc.b dc.w dc.l   define constant data (1, 2 or 4 bytes per value)\n");
  fprintf(out, "                   dc.b also takes strings: dc.b \"Hello\",13,10,0  ('...' works too;\n"
               "                   a doubled quote inside is one quote; no backslash escapes)\n");
}

static int dc_error(size_t line_num, const char *fmt, const char *arg) {
  fprintf(stderr, "Error in %s at line %zu: ", g_src_file, line_num);
  fprintf(stderr, fmt, arg);
  fputc('\n', stderr);
  return 1;
}

static int equ_error(size_t line_num, const char *message) {
  fprintf(stderr, "Error in %s at line %zu: EQU %s\n", g_src_file, line_num, message);
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

// Copies the quoted string at *pp into the output, one byte per character,
// and leaves *pp just past the closing quote. Either ' or " opens a string,
// and the same character closes it; a doubled delimiter inside the string is
// one literal delimiter ('It''s' or "It's"). There are no backslash escapes:
// non-printing bytes such as CR/LF go in as separate numeric values
// (dc.b "Hi",13,10,0). Returns 1 on success, 0 after reporting an error.
static int emit_string(const char **pp, OutputBuffer *buf, const char *name,
                       size_t line_num) {
  const char *p = *pp;
  char delim = *p++;
  for (;;) {
    if (*p == '\0') {
      dc_error(line_num, "%s: unterminated string (missing closing quote)", name);
      return 0;
    }
    if (*p == delim) {
      if (p[1] == delim) { // doubled delimiter = one literal quote
        buffer_write(buf, (uint8_t)delim);
        p += 2;
        continue;
      }
      p++; // closing quote
      break;
    }
    buffer_write(buf, (uint8_t)*p++);
  }
  *pp = p;
  return 1;
}

int directive_assemble(const char *code, const char *label, size_t line_num,
                       int pass, OutputBuffer *buf, uint32_t code_base) {
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

  if (directive_is_equ(name)) {
    while (*p && isspace((unsigned char)*p))
      p++;
    if (!label || !*label)
      return equ_error(line_num, "requires a label");
    if (!*p)
      return equ_error(line_num, "requires a numeric value");

    char value_token[64];
    size_t value_len = 0;
    while (*p && !isspace((unsigned char)*p) && *p != ',') {
      if (value_len < sizeof value_token - 1)
        value_token[value_len++] = *p;
      p++;
    }
    value_token[value_len] = '\0';
    while (*p && isspace((unsigned char)*p))
      p++;
    if (*p)
      return equ_error(line_num, "takes exactly one numeric value");

    const char *number_token = value_token[0] == '#' ? value_token + 1 : value_token;
    long long value;
    if (!parse_number(number_token, &value)) {
      if (!is_identifier(number_token))
        return dc_error(line_num, "invalid EQU value '%s'", value_token);
      int32_t prior_value;
      if (!symtab_find_equ(number_token, &prior_value))
        return dc_error(line_num, "undefined EQU value '%s'", number_token);
      value = prior_value;
    }
    if (value < INT32_MIN || value > INT32_MAX)
      return equ_error(line_num, "value is outside the signed 32-bit range");
    if (pass == 1) {
      size_t prev;
      const char *prev_file;
      if (symtab_define_equ(label, (int32_t)value, line_num, &prev, &prev_file) != 0) {
        fprintf(stderr,
                "Error in %s at line %zu: symbol '%s' already defined in %s at line %zu\n",
                g_src_file, line_num, label, prev_file, prev);
        return 1;
      }
    }
    return 0;
  }

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

    if (*p == '"' || *p == '\'') {
      // A string: one byte per character, dc.b only.
      if (unit != 1)
        return dc_error(line_num, "%s: strings are only allowed in dc.b", name);
      if (!emit_string(&p, buf, name, line_num))
        return 1;
    } else {
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
          int32_t equ_value;
          if (symtab_find_equ(tok, &equ_value)) {
            value = equ_value;
          } else {
            size_t off;
            if (!symtab_find(tok, &off))
              return dc_error(line_num, "undefined label '%s'", tok);
            value = (long long)code_base + (long long)off;
          }
        }
      } else {
        return dc_error(line_num, "invalid value '%s'", tok);
      }

      if (!in_range(value, unit))
        return dc_error(line_num, "value '%s' is out of range for this size",
                        tok);

      emit_value(buf, value, unit);
    }
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
