#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/parser.h"
#include "src/opcodes.h"
#include "src/pe_writer.h"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source.asm> <output.exe>\n", argv[0]);
        return 1;
    }

    const char *src_file = argv[1];
    const char *out_file = argv[2];

    FILE *in = fopen(src_file, "r");
    if (!in) {
        perror("Error opening source file");
        return 1;
    }

    FILE *out = fopen(out_file, "wb");
    if (!out) {
        perror("Error creating output file");
        fclose(in);
        return 1;
    }

    char line[256];
    size_t line_num = 1;

    // Start PE writer (headers + section setup)
    pe_begin(out);

    while (fgets(line, sizeof(line), in)) {
        // Strip newline
        line[strcspn(line, "\r\n")] = 0;

        const OpcodeEntry *entry = parse_line(line, line_num);
        if (entry) {
            emit_opcode(out, entry);
        }
        line_num++;
    }

    // Finalize PE (patch sizes, entry point, etc.)
    pe_end(out);

    fclose(in);
    fclose(out);

    printf("Assembly complete: %s -> %s\n", src_file, out_file);
    return 0;
}
