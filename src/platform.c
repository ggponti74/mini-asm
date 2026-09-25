#include <string.h>
#include <ctype.h>

#include "platform.h"
#include "pe_writer.h"
#include "arm_elf_writer.h"
#include "elf_writer.h"

// Small portable stand-in for strcasecmp()/_stricmp() so we don't have
// to pull in platform-specific headers just to compare target names.
static bool names_equal_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

// The full set of output formats mini-asm can assemble to. Adding a new
// target means adding a writer + one row here — nothing in main.c needs
// to change.
static const PlatformTarget k_targets[] = {
    {
        .name           = "x86",
        .description    = "Windows PE32 console executable (x86)",
        .cpu_arch       = "x86",
        .default_ext    = ".exe",
        .needs_exec_bit = false,
        .write          = write_pe,
    },
    {
        .name           = "arm",
        .description    = "Bare-metal/Linux ELF32 executable (ARM)",
        .cpu_arch       = "arm",
        .default_ext    = "",
        .needs_exec_bit = true,
        .write          = write_arm_elf,
    },
    {
        .name           = "elf",
        .description    = "Linux ELF32 executable (x86)",
        .cpu_arch       = "x86",
        .default_ext    = "",
        .needs_exec_bit = true,
        .write          = write_elf,
    },
};
#define NUM_TARGETS (sizeof(k_targets) / sizeof(k_targets[0]))

const PlatformTarget *platform_find(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < NUM_TARGETS; i++) {
        if (names_equal_ci(k_targets[i].name, name)) {
            return &k_targets[i];
        }
    }
    return NULL;
}

const PlatformTarget *platform_default(void) {
    // x86/PE matches the assembler's original default (see README).
    return platform_find("x86");
}

void platform_print_targets(FILE *out) {
    for (size_t i = 0; i < NUM_TARGETS; i++) {
        fprintf(out, "  %-6s %s\n", k_targets[i].name, k_targets[i].description);
    }
}
