#include <string.h>
#include <stdlib.h>

#include "srcloc.h"
#include "symtab.h"

typedef enum {
  SYMBOL_LABEL,
  SYMBOL_EQU
} SymbolKind;

typedef struct {
  char  *name;
  size_t offset;
  size_t line;
  char  *file;
  int32_t equ_value;
  SymbolKind kind;
} Symbol;

static Symbol *g_syms  = NULL;
static size_t  g_count = 0;
static size_t  g_cap   = 0;

int symtab_find(const char *name, size_t *offset) {
  for (size_t i = 0; i < g_count; i++) {
    if (g_syms[i].kind == SYMBOL_LABEL &&
        strcmp(g_syms[i].name, name) == 0) {
      if (offset) *offset = g_syms[i].offset;
      return 1;
    }
  }
  return 0;
}

int symtab_find_equ(const char *name, int32_t *value) {
  for (size_t i = 0; i < g_count; i++) {
    if (g_syms[i].kind == SYMBOL_EQU &&
        strcmp(g_syms[i].name, name) == 0) {
      if (value) *value = g_syms[i].equ_value;
      return 1;
    }
  }
  return 0;
}

static Symbol *new_symbol(const char *name, size_t line, size_t *prev_line,
                          const char **prev_file) {
  for (size_t i = 0; i < g_count; i++) {
    if (strcmp(g_syms[i].name, name) == 0) {
      if (prev_line) *prev_line = g_syms[i].line;
      if (prev_file) *prev_file = g_syms[i].file;
      return NULL;
    }
  }

  if (g_count == g_cap) {
    size_t new_cap = g_cap ? g_cap * 2 : 16;
    Symbol *grown = realloc(g_syms, new_cap * sizeof *grown);
    if (!grown) abort();
    g_syms = grown;
    g_cap  = new_cap;
  }

  size_t n = strlen(name) + 1;
  char *copy = malloc(n);
  if (!copy) abort();
  memcpy(copy, name, n);

  size_t fn = strlen(g_src_file) + 1;
  char *file_copy = malloc(fn);
  if (!file_copy) abort();
  memcpy(file_copy, g_src_file, fn);

  g_syms[g_count].name   = copy;
  g_syms[g_count].line   = line;
  g_syms[g_count].file   = file_copy;
  return &g_syms[g_count++];
}

int symtab_define(const char *name, size_t offset, size_t line, size_t *prev_line,
                  const char **prev_file) {
  Symbol *symbol = new_symbol(name, line, prev_line, prev_file);
  if (!symbol)
    return -1;
  symbol->kind = SYMBOL_LABEL;
  symbol->offset = offset;
  return 0;
}

int symtab_define_equ(const char *name, int32_t value, size_t line,
                      size_t *prev_line, const char **prev_file) {
  Symbol *symbol = new_symbol(name, line, prev_line, prev_file);
  if (!symbol)
    return -1;
  symbol->kind = SYMBOL_EQU;
  symbol->equ_value = value;
  return 0;
}

void symtab_free(void) {
  for (size_t i = 0; i < g_count; i++) {
    free(g_syms[i].name);
    free(g_syms[i].file);
  }
  free(g_syms);
  g_syms  = NULL;
  g_count = g_cap = 0;
}