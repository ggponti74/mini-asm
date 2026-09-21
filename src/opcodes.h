#ifndef OPCODES_H
#define OPCODES_H

#include <stddef.h>
#include <stdint.h>

#include "cpu68k.h"

// Lookup function: find an opcode entry by mnemonic
const OpcodeEntry* lookup_opcode(const char *mnemonic);

// Emit function: write opcode bytes to output stream
void emit_opcode(FILE *out, const OpcodeEntry *entry);

#endif // OPCODES_H
