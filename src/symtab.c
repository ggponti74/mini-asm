#include <string.h>
#include <stdlib.h>

#include "symtab.h"

typedef struct {
  char  *name;
  size_t offset;
  size_t line;
} Symbol;

static Symbol *g_syms  = NULL;
static size_t  g_count = 0;
static size_t  g_cap   = 0;

int symtab_find(const char *name, size_t *offset) {
  for (size_t i = 0; i < g_count; i++) {
    if (strcmp(g_syms[i].name, name) == 0) {
      if (offset) *offset = g_syms[i].offset;
      return 1;
    }
  }
  return 0;
}

int symtab_define(const char *name, size_t offset, size_t line, size_t *prev_line) {
  for (size_t i = 0; i < g_count; i++) {
    if (strcmp(g_syms[i].name, name) == 0) {
      if (prev_line) *prev_line = g_syms[i].line;
      return -1;
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

  g_syms[g_count].name   = copy;
  g_syms[g_count].offset = offset;
  g_syms[g_count].line   = line;
  g_count++;
  return 0;
}

void symtab_free(void) {
  for (size_t i = 0; i < g_count; i++) free(g_syms[i].name);
  free(g_syms);
  g_syms  = NULL;
  g_count = g_cap = 0;
}