#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "opcodes.h"
#include "codegen.h"
#include "elf_writer.h"
#include "arm_elf_writer.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source.asm>\n", argv[0]);
        return 1;
    }

    FILE *src = fopen(argv[1], "r");
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
        Operand ops[entry->size];
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
        write_arm_elf("a.out", &buf);   // ✅ produce ELF file
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
