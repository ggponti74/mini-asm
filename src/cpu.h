#ifndef MINIASM_CPU_H
#define MINIASM_CPU_H

#include "parser.h"
#include "registers.h"

typedef struct {
    Registers registers;
} CPU;

typedef enum {
    EXEC_OK,
    EXEC_ERROR
} ExecResult;

void cpu_init(CPU *cpu);

/* Execute every instruction in `prog` in order. On EXEC_ERROR, a
 * human-readable message is written to errbuf. */
ExecResult cpu_run(CPU *cpu, const Program *prog, char *errbuf, size_t errbuf_size);

#endif /* MINIASM_CPU_H */
