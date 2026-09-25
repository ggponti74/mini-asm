#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "opcodes.h"
#include "codegen.h"
#include "platform.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#define MINI_ASM_POSIX_HOST 1
#include <sys/stat.h>
#include <sys/types.h>
#endif

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-t target] [-o output] <source.asm>\n\n", prog);
    fprintf(stderr, "  -t, --target <name>   output format to assemble to (default: %s)\n",
            platform_default()->name);
    fprintf(stderr, "  -o, --output <file>   override the assembled file's name\n");
    fprintf(stderr, "  -h, --help            show this help\n\n");
    fprintf(stderr, "Available targets:\n");
    platform_print_targets(stderr);
}

// Derives "name" from "path/name.ext" (drops directory components and
// the final extension) so the default output name tracks the source
// file instead of always being "a.out".
static void derive_base_name(const char *source_path, char *out, size_t out_size) {
    const char *slash = strrchr(source_path, '/');
    const char *bslash = strrchr(source_path, '\\');
    const char *base = source_path;
    if (slash && slash > base) base = slash + 1;
    if (bslash && bslash + 1 > base) base = bslash + 1;

    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (len >= out_size) len = out_size - 1;

    memcpy(out, base, len);
    out[len] = '\0';
    if (out[0] == '\0') {
        // Fallback for degenerate paths (e.g. "" or ".asm")
        snprintf(out, out_size, "a");
    }
}

int main(int argc, char *argv[]) {
    const PlatformTarget *target = NULL;
    const char *output_override = NULL;
    const char *source_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--target") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: %s requires an argument\n", argv[0], argv[i]);
                return 1;
            }
            target = platform_find(argv[++i]);
            if (!target) {
                fprintf(stderr, "%s: unknown target '%s'\n\n", argv[0], argv[i]);
                fprintf(stderr, "Available targets:\n");
                platform_print_targets(stderr);
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: %s requires an argument\n", argv[0], argv[i]);
                return 1;
            }
            output_override = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (!source_path) {
            source_path = argv[i];
        } else {
            fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!source_path) {
        print_usage(argv[0]);
        return 1;
    }
    if (!target) {
        target = platform_default();
    }

    // Instruction encoding is CPU-specific, not just the container format,
    // so the target also drives which opcode table parsing/codegen uses.
    if (!opcodes_select_arch(target->cpu_arch)) {
        fprintf(stderr, "%s: internal error: no opcode table for arch '%s'\n",
                argv[0], target->cpu_arch);
        return 1;
    }

    FILE *src = fopen(source_path, "r");
    if (!src) {
        perror("fopen");
        return 1;
    }

    OutputBuffer buf;
    buf.data = malloc(1024);
    buf.size = 0;
    buf.capacity = 1024;

    char line[256];
    size_t line_num = 1;
    int errors = 0;

    while (fgets(line, sizeof(line), src)) {
        // Parse line → returns OpcodeEntry or NULL
        const OpcodeEntry *entry = parse_line(line, line_num);
        if (!entry) {
            errors++;
            line_num++;
            continue;
        }

        // Build operands (parser should fill this)
        Operand ops[entry->operand_count];
        size_t op_count = extract_operands(line, ops, entry->operand_count);

        // Validate operand count
        if (op_count != entry->operand_count) {
            fprintf(stderr, "Error at line %zu: expected %zu operands, got %zu\n",
                    line_num, entry->operand_count, op_count);
            errors++;
            line_num++;
            continue;
        }

        // Emit machine code
        emit_code(entry, ops, &buf);

        line_num++;
    }

    fclose(src);

    if (errors == 0) {
        printf("Assembly complete. %zu bytes emitted.\n", buf.size);

        char output_name[512];
        if (output_override) {
            snprintf(output_name, sizeof(output_name), "%s", output_override);
        } else {
            char base[400];
            derive_base_name(source_path, base, sizeof(base));
            snprintf(output_name, sizeof(output_name), "%s%s", base, target->default_ext);
        }

        target->write(output_name, &buf);
        printf("Wrote %s (target: %s)\n", output_name, target->name);

#if defined(MINI_ASM_POSIX_HOST)
        if (target->needs_exec_bit) {
            struct stat st;
            if (stat(output_name, &st) == 0) {
                mode_t mode = st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH;
                if (chmod(output_name, mode) != 0) {
                    perror("chmod");
                }
            }
        }
#endif
    } else {
        printf("Assembly failed with %d error(s).\n", errors);
    }

    // Dump hex output
    for (size_t i = 0; i < buf.size; i++) {
        printf("%02X ", buf.data[i]);
    }
    printf("\n");

    free(buf.data);
    return errors ? 1 : 0;
}
