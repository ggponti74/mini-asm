#ifndef SYMTAB_H
#define SYMTAB_H

#include <stddef.h>
#include <stdint.h>

// Defines a label. Returns 0 on success, or -1 if the name is already
// defined (the line of the earlier definition is stored in *prev_line).
int symtab_define(const char *name, size_t offset, size_t line, size_t *prev_line);

// Defines an absolute EQU constant. Shares the namespace with labels.
int symtab_define_equ(const char *name, int32_t value, size_t line,
                      size_t *prev_line);

// Looks up a label. Returns 1 and sets *offset if found, otherwise 0.
int symtab_find(const char *name, size_t *offset);

// Looks up an EQU constant. Returns 1 and sets *value if found, otherwise 0.
int symtab_find_equ(const char *name, int32_t *value);

// Frees everything. Call once at the end of main().
void symtab_free(void);

#endif // SYMTAB_H