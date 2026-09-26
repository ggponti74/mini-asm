#include "opcodes.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    char suffix;   /* 'b', 'w', or 'l' -- the table always stores the resolved suffix */
    Opcode opcode;
    Size size;
} OpcodeEntry;

static const OpcodeEntry OPCODE_TABLE[] = {
    { "move", 'b', OP_MOVE_B, SIZE_B },
    { "move", 'w', OP_MOVE_W, SIZE_W },
    { "move", 'l', OP_MOVE_L, SIZE_L },
};

static const size_t OPCODE_TABLE_LEN = sizeof(OPCODE_TABLE) / sizeof(OPCODE_TABLE[0]);

int opcode_lookup(const char *mnemonic, char suffix, Opcode *opcode, Size *size) {
    char resolved_suffix = (suffix == '\0') ? 'b' : suffix; /* no suffix -> default byte */

    for (size_t i = 0; i < OPCODE_TABLE_LEN; i++) {
        const OpcodeEntry *entry = &OPCODE_TABLE[i];
        if (strcmp(entry->mnemonic, mnemonic) == 0 && entry->suffix == resolved_suffix) {
            *opcode = entry->opcode;
            *size = entry->size;
            return 1;
        }
    }
    return 0;
}
