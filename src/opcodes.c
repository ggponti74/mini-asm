#include <stdio.h>
#include <string.h>

#include "opcodes.h"
#include "codegen.h"


// mini-asm's source language is 68K-style mnemonics (RTS, etc.) regardless
// of target platform. Only the encoded machine code bytes differ per
// architecture — the mnemonic stays "RTS" for both.
#if defined(TARGET_ARM)
static const OpcodeEntry opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    { "RTS", 0x1EFF2FE1, 4, 1, 0, { OPERAND_NONE, OPERAND_NONE } },  // ARM32 "BX LR"
};
#elif defined(TARGET_X86)
static const OpcodeEntry opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    { "RTS", 0xC3, 1, 1, 0, { OPERAND_NONE, OPERAND_NONE } },  // x86 "RET"
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
