#include <stdio.h>
#include <string.h>
#include "opcodes.h"

// Define raw opcode bytes
static const uint8_t RTS_BYTES[] = { 0xC3 }; // x86 RET

// Lookup table of supported mnemonics
static const OpcodeEntry opcode_table[] = {
    { "RTS", RTS_BYTES, sizeof(RTS_BYTES) }
    // Future entries: { "MOVE.L", ... }, { "ADD.L", ... }
};

// Lookup function
const OpcodeEntry* lookup_opcode(const char *mnemonic) {
    for (size_t i = 0; i < sizeof(opcode_table)/sizeof(opcode_table[0]); i++) {
        if (strcmp(opcode_table[i].mnemonic, mnemonic) == 0) {
            return &opcode_table[i];
        }
    }
    return NULL; // Not found
}

// Emit function
void emit_opcode(FILE *out, const OpcodeEntry *entry) {
    if (entry && out) {
        fwrite(entry->bytes, 1, entry->length, out);
    }
}
