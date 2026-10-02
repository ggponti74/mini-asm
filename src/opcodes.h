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
    OPERAND_LABEL,     // symbolic label
    OPERAND_REG_OR_IMM // table-only: accepts a register OR an immediate
} OperandType;

// Operation size from a .b/.w/.l suffix. SIZE_UNSPEC means the mnemonic had
// no suffix; codegen resolves it (DEFAULT_OPSIZE, or .l where a byte is
// illegal, e.g. MOVE to an address register).
typedef enum { SIZE_UNSPEC = 0, SIZE_B, SIZE_W, SIZE_L } OpSize;

#define DEFAULT_OPSIZE SIZE_B   // mini-asm's rule: no suffix means byte

// Bit masks for OpcodeEntry.size_mask (which suffixes an entry accepts).
#define SIZES_B   0x1u
#define SIZES_W   0x2u
#define SIZES_L   0x4u
#define SIZES_BWL (SIZES_B | SIZES_W | SIZES_L)

// Opcode entry structure
typedef struct {
    const       char *mnemonic;       // e.g., "RTS", "MOVE"
    int         opcode;         // base opcode value
    size_t      size;            // instruction size in bytes
    size_t      length;         // total instruction length in bytes
    size_t      operand_count;  // expected number of operands
    OperandType operand_types[2]; // expected operand types (up to 2 for simplicity)
    unsigned    size_mask;      // accepted size suffixes (SIZES_*); 0 = no suffix allowed
} OpcodeEntry;

// True if an operand of kind `actual` satisfies the table slot `expected`.
// OPERAND_REG_OR_IMM accepts a register or an immediate; every other
// slot kind must match exactly.
bool operand_matches(OperandType expected, OperandType actual);

// Selects which CPU architecture's opcode table lookup_opcode() searches.
// The instruction encoding (not just the container format) differs per
// target CPU, so this must be called once — e.g. right after main()
// resolves the PlatformTarget — before any call to lookup_opcode().
// Recognized names: "arm", "x86". Returns false for an unrecognized name.
bool opcodes_select_arch(const char *arch_name);

// Lookup function: find opcode by mnemonic in the currently selected
// architecture's table (see opcodes_select_arch()).
const OpcodeEntry* lookup_opcode(const char *mnemonic);

// Returns the arch name passed to the most recent successful
// opcodes_select_arch() call ("x86", "arm"), or NULL if none has
// succeeded yet. Lets codegen.c special-case per-architecture encoding
// rules that don't fit the generic "write opcode bytes, then each
// operand's bytes in turn" model -- e.g. x86's MOV r32,imm32 bakes the
// destination register into the opcode byte itself and takes no ModRM
// byte at all.
const char *opcodes_active_arch_name(void);

#endif // OPCODES_H
