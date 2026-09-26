#include "cpu.h"

#include <stdio.h>

void cpu_init(CPU *cpu) {
    registers_init(&cpu->registers);
}

static ExecResult exec_move(CPU *cpu, const Instruction *instr, char *errbuf, size_t errbuf_size) {
    uint32_t value;
    if (instr->src.kind == OPERAND_IMMEDIATE) {
        value = (uint32_t)instr->src.immediate;
    } else {
        value = registers_get(&cpu->registers, instr->src.reg_index, instr->size);
    }

    if (instr->dst.kind != OPERAND_REGISTER) {
        snprintf(errbuf, errbuf_size,
                 "line %d: move destination must be a data register: \"%s\"",
                 instr->line_number, instr->source_line);
        return EXEC_ERROR;
    }

    registers_set(&cpu->registers, instr->dst.reg_index, value, instr->size);
    return EXEC_OK;
}

ExecResult cpu_run(CPU *cpu, const Program *prog, char *errbuf, size_t errbuf_size) {
    for (size_t i = 0; i < prog->count; i++) {
        const Instruction *instr = &prog->items[i];
        switch (instr->opcode) {
            case OP_MOVE_B:
            case OP_MOVE_W:
            case OP_MOVE_L:
                if (exec_move(cpu, instr, errbuf, errbuf_size) != EXEC_OK) {
                    return EXEC_ERROR;
                }
                break;
            default:
                snprintf(errbuf, errbuf_size,
                         "line %d: unsupported opcode in: \"%s\"",
                         instr->line_number, instr->source_line);
                return EXEC_ERROR;
        }
    }
    return EXEC_OK;
}
