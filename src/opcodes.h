#ifndef OPCODES_H
#define OPCODES_H


#include <stddef.h>
#include <stdint.h>

// Operand kinds
typedef enum {
    OPERAND_NONE,      // no operand (e.g., RTS)
    OPERAND_REGISTER,  // register (Dn, An)
    OPERAND_IMMEDIATE, // immediate value (#123)
    OPERAND_LABEL      // symbolic label
} OperandType;

// Opcode entry structure
typedef struct {
    const       char *mnemonic;       // e.g., "RTS", "MOVE"
    int         opcode;         // base opcode value
    size_t      size;            // instruction size in bytes
    size_t      length;         // total instruction length in bytes
    size_t      operand_count;  // expected number of operands
    OperandType operand_types[2]; // expected operand types (up to 2 for simplicity)
} OpcodeEntry;

// Lookup function: find opcode by mnemonic
const OpcodeEntry* lookup_opcode(const char *mnemonic);

#endif // OPCODES_H
