#include <stdio.h>
#include <stdlib.h>

#include "cpu.h"
#include "parser.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <program.asm>\n", argv[0]);
        return 2;
    }

    char *source = read_file(argv[1]);
    if (!source) {
        fprintf(stderr, "%s: could not read file\n", argv[1]);
        return 2;
    }

    char errbuf[256];
    Program program;
    if (!parse_program(source, &program, errbuf, sizeof errbuf)) {
        fprintf(stderr, "%s: %s\n", argv[1], errbuf);
        free(source);
        return 1;
    }
    free(source);

    CPU cpu;
    cpu_init(&cpu);
    if (cpu_run(&cpu, &program, errbuf, sizeof errbuf) != EXEC_OK) {
        fprintf(stderr, "%s: %s\n", argv[1], errbuf);
        program_free(&program);
        return 1;
    }
    program_free(&program);

    /* Unix exit codes are a single byte; mask D0 down to that range. */
    return (int)registers_get(&cpu.registers, 0, SIZE_B);
}
