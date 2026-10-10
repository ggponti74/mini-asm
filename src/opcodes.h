#ifndef OPCODES_H
#define OPCODES_H


#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Operand kinds
typedef enum {
    OPERAND_NONE,      // no operand (e.g., RTS)
    OPERAND_REGISTER,  // register (Dn, An)
    OPERAND_IMMEDIATE, // immediate value (#123)
    OPERAND_LABEL,     // symbolic label
    OPERAND_REG_OR_IMM, // table-only: accepts a register OR an immediate
    OPERAND_IND,       // address register indirect:  (An)
    OPERAND_POSTINC,   // post-increment:             (An)+
    OPERAND_PREDEC,    // pre-decrement:              -(An)
    OPERAND_EA_SRC,    // table-only: register, immediate or memory operand
    OPERAND_REG_OR_MEM,// table-only: register or memory operand
    OPERAND_EA_ALT,    // table-only: register or memory destination
    OPERAND_CONTROL,   // table-only: label or address-register indirect
    OPERAND_OPT_REG,   // table-only: a register that may be left out (ASL/LSL/... <mem> has one
                       // operand, ASL/LSL/... #n,Dn has two); a missing operand reaches codegen
                       // as OPERAND_NONE
    OPERAND_BAD        // parser-only: malformed operand (never matches a table slot)
} OperandType;

// True for the three address-register memory modes: (An), (An)+, -(An).
bool operand_is_memory(OperandType t);

// Operation size from a .b/.w/.l suffix. SIZE_UNSPEC means the mnemonic had
// no suffix; codegen resolves it to DEFAULT_OPSIZE.
typedef enum { SIZE_UNSPEC = 0, SIZE_B, SIZE_W, SIZE_L } OpSize;

#define DEFAULT_OPSIZE SIZE_W   // mini-asm's rule: no suffix means word (.w)

// On a branch (BRA/BSR) the suffix picks the displacement size, not an
// operation size. Unsuffixed branches use .w (the safest: reaches anywhere
// the x86 targets can encode), independent of DEFAULT_OPSIZE.
#define BRANCH_DEFAULT_SIZE SIZE_W

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
// OPERAND_REG_OR_IMM accepts a register or an immediate, OPERAND_EA_SRC a
// register, an immediate or a memory operand, OPERAND_REG_OR_MEM a register
// or a memory operand; every other slot kind must match exactly.
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

// Prints the currently selected architecture's opcode table to `out`: one
// line per mnemonic with its accepted size suffixes and operand forms.
// Backs the -l/--list switch. Does nothing if no arch has been selected.
void opcodes_print_table(FILE *out);

#endif // OPCODES_H
