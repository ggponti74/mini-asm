#ifndef MINIASM_OPCODES_H
#define MINIASM_OPCODES_H

#include "registers.h"

/* Each (mnemonic, size) combination is its own opcode. This mirrors the
 * 68k, where size is encoded directly into the instruction, and keeps
 * cpu.c's dispatch a flat switch with no separate runtime size lookup. */
typedef enum {
    OP_MOVE_B,
    OP_MOVE_W,
    OP_MOVE_L,
    OP_COUNT
} Opcode;

/* Resolve a mnemonic ("move", already lowercased) plus an optional size
 * suffix character ('b', 'w', 'l', or '\0' for "no suffix given", which
 * defaults to byte) into an Opcode and its Size.
 *
 * Returns 1 and fills in opcode/size on success, 0 if the combination is
 * not recognized. */
int opcode_lookup(const char *mnemonic, char suffix, Opcode *opcode, Size *size);

#endif /* MINIASM_OPCODES_H */
