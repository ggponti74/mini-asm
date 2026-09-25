#ifndef OPCODES_H
#define OPCODES_H


#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

// Selects which CPU architecture's opcode table lookup_opcode() searches.
// The instruction encoding (not just the container format) differs per
// target CPU, so this must be called once — e.g. right after main()
// resolves the PlatformTarget — before any call to lookup_opcode().
// Recognized names: "arm", "x86". Returns false for an unrecognized name.
bool opcodes_select_arch(const char *arch_name);

// Lookup function: find opcode by mnemonic in the currently selected
// architecture's table (see opcodes_select_arch()).
const OpcodeEntry* lookup_opcode(const char *mnemonic);

#endif // OPCODES_H
