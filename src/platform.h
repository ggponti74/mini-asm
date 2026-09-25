#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stdio.h>

#include "codegen.h"

// A PlatformTarget describes one output format mini-asm knows how to
// assemble to. Instead of picking a single target at compile time via
// #if defined(TARGET_...), main() parses the command line first and
// then resolves *one* PlatformTarget to drive the rest of the run —
// no scattered if/else chains, just a call through target->write().
typedef void (*platform_write_fn)(const char *filename, const OutputBuffer *buf);

typedef struct {
    const char       *name;           // canonical id, e.g. "x86", "arm" (matched case-insensitively)
    const char       *description;    // short human-readable summary, for -h/--help
    const char       *cpu_arch;       // instruction-encoding table to select, see opcodes_select_arch()
    const char       *default_ext;    // appended to the derived output name, e.g. ".exe" or "" for none
    bool              needs_exec_bit; // true if the output should get chmod +x on POSIX hosts
    platform_write_fn write;          // emits the final executable from the assembled buffer
} PlatformTarget;

// Look up a target by name (case-insensitive). Returns NULL if unknown.
const PlatformTarget *platform_find(const char *name);

// Target used when -t/--target isn't given on the command line.
const PlatformTarget *platform_default(void);

// Prints the list of available targets, one per line (for usage/-h text).
void platform_print_targets(FILE *out);

#endif // PLATFORM_H
