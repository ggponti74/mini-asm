#include <stdio.h>
#include <string.h>

#include "opcodes.h"
#include "codegen.h"


// Define raw opcode bytes
//static const uint8_t RTS_BYTES[] = { 0xC3 }; // x86 RET

static const OpcodeEntry opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    { "RTS", 0x1EFF2FE1, 4, 1, 0, { OPERAND_NONE, OPERAND_NONE } },
};

const OpcodeEntry* lookup_opcode(const char *mnemonic) {
    for (size_t i = 0; i < sizeof(opcode_table)/sizeof(opcode_table[0]); i++) {
        if (strcmp(opcode_table[i].mnemonic, mnemonic) == 0) {
            return &opcode_table[i];
        }
    }
    return NULL; // not found
}
