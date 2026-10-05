#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "codegen.h"
#include "opcodes.h"
#include "parser.h"
#include "platform.h"
#include "symtab.h"
#include "directives.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#define MINI_ASM_POSIX_HOST 1
#include <sys/stat.h>
#include <sys/types.h>
#endif

static void print_usage(const char *prog)
{
  fprintf(stderr, "Usage: %s [-t target] [-o output] <source.asm>\n", prog);
  fprintf(stderr, "       %s [-t target] -l\n\n", prog);
  fprintf(
      stderr,
      "  -t, --target <name>   output format to assemble to (default: %s)\n",
      platform_default()->name);
  fprintf(stderr,
          "  -o, --output <file>   override the assembled file's name\n");
  fprintf(stderr,
          "  -l, --list            list the instructions and directives available\n"
          "                        for the target (no source file needed)\n");
  fprintf(stderr, "  -h, --help            show this help\n\n");
  fprintf(stderr, "Available targets:\n");
  platform_print_targets(stderr);
}

// Derives "name" from "path/name.ext" (drops directory components and
// the final extension) so the default output name tracks the source
// file instead of always being "a.out".
static void derive_base_name(const char *source_path, char *out,
                             size_t out_size)
{
  const char *slash = strrchr(source_path, '/');
  const char *bslash = strrchr(source_path, '\\');
  const char *base = source_path;
  if (slash && slash > base)
    base = slash + 1;
  if (bslash && bslash + 1 > base)
    base = bslash + 1;

  const char *dot = strrchr(base, '.');
  size_t len = dot ? (size_t)(dot - base) : strlen(base);
  if (len >= out_size)
    len = out_size - 1;

  memcpy(out, base, len);
  out[len] = '\0';
  if (out[0] == '\0')
  {
    // Fallback for degenerate paths (e.g. "" or ".asm")
    snprintf(out, out_size, "a");
  }
}

#define MAX_INCLUDE_DEPTH 32

static int parse_include_path(const char *line, const char *code,
                              const char **path_start,
                              size_t *path_len, size_t *column) {
  const char *p = code;
  while (*p && isspace((unsigned char)*p))
    p++;
  while (*p && !isspace((unsigned char)*p))
    p++;
  while (*p && isspace((unsigned char)*p))
    p++;

  *column = (size_t)(p - line) + 1;
  if (*p == '\0')
    return 0;

  char quote = '\0';
  if (*p == '"' || *p == '\'')
    quote = *p++;
  *path_start = p;
  if (quote) {
    while (*p && *p != quote)
      p++;
    if (*p != quote)
      return 0;
    *path_len = (size_t)(p - *path_start);
    p++;
  } else {
    while (*p && !isspace((unsigned char)*p))
      p++;
    *path_len = (size_t)(p - *path_start);
  }
  while (*p && isspace((unsigned char)*p))
    p++;
  return *path_len > 0 && *p == '\0';
}

static int path_is_absolute(const char *path) {
  return path[0] == '/' || path[0] == '\\' ||
         (isalpha((unsigned char)path[0]) && path[1] == ':');
}

static char *resolve_include_path(const char *source_path,
                                  const char *include_path) {
  if (path_is_absolute(include_path)) {
    size_t len = strlen(include_path) + 1;
    char *resolved = malloc(len);
    if (resolved)
      memcpy(resolved, include_path, len);
    return resolved;
  }

  const char *slash = strrchr(source_path, '/');
  const char *backslash = strrchr(source_path, '\\');
  const char *separator = slash;
  if (backslash && (!separator || backslash > separator))
    separator = backslash;
  size_t directory_len = separator ? (size_t)(separator - source_path + 1) : 0;
  size_t include_len = strlen(include_path);
  char *resolved = malloc(directory_len + include_len + 1);
  if (!resolved)
    return NULL;
  memcpy(resolved, source_path, directory_len);
  memcpy(resolved + directory_len, include_path, include_len + 1);
  return resolved;
}

static int assemble_source(FILE *src, const char *source_path, int pass,
                           OutputBuffer *buf, uint32_t code_base,
                           size_t depth, const char **include_stack) {
  char line[1024];
  size_t line_num = 1;
  int errors = 0;

  while (fgets(line, sizeof(line), src)) {
    strip_comment(line);
    if (line[0] == '\0') {
      line_num++;
      continue;
    }

    char label[64];
    const char *code = split_label(line, label, sizeof label, line_num);
    if (!code) {
      errors++;
      line_num++;
      continue;
    }

    char first_token[16] = "";
    sscanf(code, "%15s", first_token);
    int equ_line = directive_is_equ(first_token);

    if (label[0] && pass == 1 && !equ_line) {
      size_t prev;
      if (symtab_define(label, buf->size, line_num, &prev) != 0) {
        fprintf(stderr,
                "Error at line %zu: label '%s' already defined at line %zu\n",
                line_num, label, prev);
        errors++;
      }
    }

    if (directive_is_include(first_token)) {
      const char *include_arg;
      size_t include_len;
      size_t column;
      if (!parse_include_path(line, code, &include_arg, &include_len,
              &column)) {
        fprintf(stderr,
                "Error at line %zu, column %zu: INCLUDE requires one path\n",
                line_num, column);
        errors++;
      } else {
        char *include_name = malloc(include_len + 1);
        if (!include_name) {
          fprintf(stderr, "Error at line %zu, column %zu: out of memory\n",
                  line_num, column);
          errors++;
        } else {
          memcpy(include_name, include_arg, include_len);
          include_name[include_len] = '\0';
          char *resolved_path = resolve_include_path(source_path, include_name);
          free(include_name);
          if (!resolved_path) {
            fprintf(stderr, "Error at line %zu, column %zu: out of memory\n",
                    line_num, column);
            errors++;
          } else if (depth >= MAX_INCLUDE_DEPTH) {
            fprintf(stderr,
                    "Error at line %zu, column %zu: INCLUDE nesting exceeds %d\n",
                    line_num, column, MAX_INCLUDE_DEPTH);
            errors++;
            free(resolved_path);
          } else {
            int cycle = 0;
            for (size_t i = 0; i <= depth; i++) {
              if (strcmp(include_stack[i], resolved_path) == 0) {
                cycle = 1;
                break;
              }
            }
            if (cycle) {
              fprintf(stderr,
                      "Error at line %zu, column %zu: recursive INCLUDE '%s'\n",
                      line_num, column, resolved_path);
              errors++;
              free(resolved_path);
            } else {
              FILE *included = fopen(resolved_path, "r");
              if (!included) {
                fprintf(stderr,
                        "Error at line %zu, column %zu: cannot open INCLUDE '%s'\n",
                        line_num, column, resolved_path);
                errors++;
                free(resolved_path);
              } else {
                include_stack[depth + 1] = resolved_path;
                errors += assemble_source(included, resolved_path, pass, buf,
                                          code_base, depth + 1, include_stack);
                fclose(included);
                free(resolved_path);
              }
            }
          }
        }
      }
      line_num++;
      continue;
    }

    if (*code == '\0') { /* label-only line */
      line_num++;
      continue;
    }

    int derr = directive_assemble(code, label, line_num, pass, buf, code_base);
    if (derr >= 0) {
      errors += derr;
      line_num++;
      continue;
    }

    const OpcodeEntry *entry = parse_line(code, line_num);
    if (!entry) {
      errors++;
      line_num++;
      continue;
    }

    OpSize size;
    if (!parse_size_suffix(code, entry, &size, line_num)) {
      errors++;
      line_num++;
      continue;
    }

    Operand ops[entry->operand_count];
    size_t op_count = extract_operands(code, ops, entry->operand_count);

    if (op_count != entry->operand_count) {
      fprintf(stderr, "Error at line %zu: expected %zu operands, got %zu\n",
              line_num, entry->operand_count, op_count);
      errors++;
      line_num++;
      continue;
    }

    /* Each operand must be the kind the opcode table asks for. */
    int ok = 1;
    for (size_t i = 0; i < entry->operand_count; i++) {
      if (ops[i].type == OPERAND_BAD) {
        fprintf(stderr,
                "Error at line %zu: operand %zu '%s' is malformed (address "
                "register modes are (An), (An)+ and -(An), no spaces inside)\n",
                line_num, i + 1, ops[i].value.label);
        ok = 0;
      } else if (!operand_matches(entry->operand_types[i], ops[i].type)) {
        fprintf(stderr, "Error at line %zu: operand %zu has the wrong type for %s\n",
                line_num, i + 1, entry->mnemonic);
        ok = 0;
      }
    }

    /* Label operands become absolute addresses. In pass 1 the label may
       not be defined yet, so a placeholder of the same size is used. */
    for (size_t i = 0; ok && i < entry->operand_count; i++) {
      if (ops[i].type == OPERAND_IMMEDIATE && ops[i].symbol) {
        if (pass == 2) {
          int32_t value;
          if (!symtab_find_equ(ops[i].symbol, &value)) {
            fprintf(stderr, "Error at line %zu: undefined EQU constant '%s'\n",
                    line_num, ops[i].symbol);
            ok = 0;
            break;
          }
          ops[i].value.imm = value;
        }
        continue;
      }
      if (ops[i].type != OPERAND_LABEL) continue;
      int32_t addr = 0;
      if (pass == 2) {
        size_t off;
        if (!symtab_find(ops[i].value.label, &off)) {
          fprintf(stderr, "Error at line %zu: undefined label '%s'\n",
                  line_num, ops[i].value.label);
          ok = 0;
          break;
        }
        addr = (int32_t)(code_base + off);
      }
      ops[i].type = OPERAND_IMMEDIATE;
      ops[i].value.imm = addr;
    }

    if (!ok) {
      errors++;
      line_num++;
      continue;
    }

    codegen_set_context(code_base + (uint32_t)buf->size, pass == 2);
    if (emit_code(entry, ops, size, buf) != 0) {
      const char *why = codegen_error();
      if (why) {
        fprintf(stderr, "Error at line %zu: %s\n", line_num, why);
        errors++;
        line_num++;
        continue;
      }
      fprintf(stderr,
              "Error at line %zu: can't encode %s with these operands on this "
              "target (supported registers: D0-D7 and A0; LEA needs an A register; ADD/SUB/MULU/MULS/DIVU/DIVS need a D register destination)\n",
              line_num, entry->mnemonic);
      errors++;
    }

    line_num++;
  }

  return errors;
}

static int assemble_pass(FILE *src, const char *source_path, int pass,
                         OutputBuffer *buf, uint32_t code_base) {
  const char *include_stack[MAX_INCLUDE_DEPTH + 1];
  include_stack[0] = source_path;
  int errors = assemble_source(src, source_path, pass, buf, code_base, 0,
                               include_stack);
  printf("Pass %d complete. %d error(s) found.\n", pass, errors);
  return errors;
}

int main(int argc, char *argv[])
{
  const PlatformTarget *target = NULL;
  const char *output_override = NULL;
  const char *source_path = NULL;
  int list_only = 0;

  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--target") == 0)
    {
      if (i + 1 >= argc)
      {
        fprintf(stderr, "%s: %s requires an argument\n", argv[0], argv[i]);
        return 1;
      }
      target = platform_find(argv[++i]);
      if (!target)
      {
        fprintf(stderr, "%s: unknown target '%s'\n\n", argv[0], argv[i]);
        fprintf(stderr, "Available targets:\n");
        platform_print_targets(stderr);
        return 1;
      }
    }
    else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0)
    {
      if (i + 1 >= argc)
      {
        fprintf(stderr, "%s: %s requires an argument\n", argv[0], argv[i]);
        return 1;
      }
      output_override = argv[++i];
    }
    else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0)
    {
      list_only = 1;
    }
    else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
    {
      print_usage(argv[0]);
      return 0;
    }
    else if (!source_path)
    {
      source_path = argv[i];
    }
    else
    {
      fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  if (!source_path && !list_only)
  {
    print_usage(argv[0]);
    return 1;
  }
  if (!target)
  {
    target = platform_default();
  }

  // Instruction encoding is CPU-specific, not just the container format,
  // so the target also drives which opcode table parsing/codegen uses.
  if (!opcodes_select_arch(target->cpu_arch))
  {
    fprintf(stderr, "%s: internal error: no opcode table for arch '%s'\n",
            argv[0], target->cpu_arch);
    return 1;
  }

  if (list_only)
  {
    opcodes_print_table(stdout);
    printf("\n");
    directive_print_list(stdout);
    printf("\nSuffix: .b/.w/.l (unsuffixed = .w). Mnemonics are case-insensitive.\n");
    printf("Memory operands: (An), (An)+ (post-increment), -(An) (pre-decrement);\n"
           "                 see each instruction's operand forms above.\n");
    return 0;
  }

  FILE *src = fopen(source_path, "r");
  if (!src)
  {
    perror("fopen");
    return 1;
  }

  OutputBuffer buf;
  buf.data = malloc(1024);
  buf.size = 0;
  buf.capacity = 1024;

  int errors = assemble_pass(src, source_path, 1, &buf, target->code_base);

  if (errors == 0)
  {
    if (target->has_regfile)
    {
      // Code size is final after pass 1 (pass 2 emits the same number of
      // bytes), so this is where the writer's REGFILE_SIZE bytes will sit.
      // code_base is 4-byte aligned, so rounding the absolute address up
      // matches the writers' round-up of the code-relative offset.
      uint32_t regfile = REGFILE_ALIGN_UP(target->code_base + (uint32_t)buf.size +
                                          target->code_tail);
      codegen_set_regfile(regfile);
      printf("Register block at 0x%08X (%d bytes)\n", regfile, REGFILE_SIZE);
    }
    rewind(src);
    buf.size = 0; /* discard pass-1 output */
    errors = assemble_pass(src, source_path, 2, &buf, target->code_base);
  }

  // char line[256];
  // size_t line_num = 1;
  // int errors = 0;

  // errors =
  //     assemble_pass(src, 1, &buf); /* pass 1: measure, define labels later */

  // if (errors > 0)
  //   rewind(src);

  // buf.size = 0; /* discard pass-1 output */
  // errors = assemble_pass(src, 2, &buf);

  // while (fgets(line, sizeof(line), src))
  // {
  //   strip_comment(line);
  //   if (line[0] == '\0')
  //   {
  //     // Blank line, or a line that was only a comment -- nothing to assemble.
  //     line_num++;
  //     continue;
  //   }

  //   // Parse line → returns OpcodeEntry or NULL
  //   const OpcodeEntry *entry = parse_line(line, line_num);
  //   if (!entry)
  //   {
  //     errors++;
  //     line_num++;
  //     continue;
  //   }

  //   // Build operands (parser should fill this)
  //   Operand ops[entry->operand_count];
  //   size_t op_count = extract_operands(line, ops, entry->operand_count);

  //   // Validate operand count
  //   if (op_count != entry->operand_count)
  //   {
  //     fprintf(stderr, "Error at line %zu: expected %zu operands, got %zu\n",
  //             line_num, entry->operand_count, op_count);
  //     errors++;
  //     line_num++;
  //     continue;
  //   }

  //   // Emit machine code
  //   emit_code(entry, ops, &buf);

  //   line_num++;
  // }

  fclose(src);

  if (errors == 0)
  {
    printf("Assembly complete. %zu bytes emitted.\n", buf.size);

    char output_name[512];
    if (output_override)
    {
      snprintf(output_name, sizeof(output_name), "%s", output_override);
    }
    else
    {
      char base[400];
      derive_base_name(source_path, base, sizeof(base));
      snprintf(output_name, sizeof(output_name), "%s%s", base,
               target->default_ext);
    }

    target->write(output_name, &buf);
    printf("Wrote %s (target: %s)\n", output_name, target->name);

#if defined(MINI_ASM_POSIX_HOST)
    if (target->needs_exec_bit)
    {
      struct stat st;
      if (stat(output_name, &st) == 0)
      {
        mode_t mode = st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH;
        if (chmod(output_name, mode) != 0)
        {
          perror("chmod");
        }
      }
    }
#endif
  }
  else
  {
    printf("Assembly failed with %d error(s).\n", errors);
  }

  // Dump hex output
  for (size_t i = 0; i < buf.size; i++)
  {
    printf("%02X ", buf.data[i]);
  }
  printf("\n");

  symtab_free();
  free(buf.data);

  return errors ? 1 : 0;
}
