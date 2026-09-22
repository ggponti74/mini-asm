#include <stdio.h>
#include <string.h>

#include "opcodes.h"
#include "codegen.h"


// Define raw opcode bytes per target architecture.
// RTS (ARM32 "BX LR") and RET (x86 "RET" near return) are the same
// logical instruction — "return from subroutine, no operands" — encoded
// differently per platform.
#if defined(TARGET_ARM)
static const OpcodeEntry opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    { "RTS", 0x1EFF2FE1, 4, 1, 0, { OPERAND_NONE, OPERAND_NONE } },
};
#elif defined(TARGET_X86)
static const OpcodeEntry opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    { "RET", 0xC3, 1, 1, 0, { OPERAND_NONE, OPERAND_NONE } },
};
#else
#error "No target architecture defined. Build with -DTARGET_ARM or -DTARGET_X86 (see Makefile TARGET variable)."
#endif

const OpcodeEntry* lookup_opcode(const char *mnemonic) {
    for (size_t i = 0; i < sizeof(opcode_table)/sizeof(opcode_table[0]); i++) {
        if (strcmp(opcode_table[i].mnemonic, mnemonic) == 0) {
            return &opcode_table[i];
        }
    }
    return NULL; // not found
}
