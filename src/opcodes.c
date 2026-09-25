#include <stdio.h>
#include <string.h>

#include "opcodes.h"
#include "codegen.h"

// mini-asm's source language is 68K-style mnemonics (RTS, etc.) regardless
// of target platform. Only the encoded machine code bytes differ per
// architecture — the mnemonic stays "RTS" for both. Both tables are
// always compiled in now; opcodes_select_arch() picks which one
// lookup_opcode() searches, so the choice is a runtime parameter
// instead of a build-time #if.
static const OpcodeEntry arm_opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    { "RTS", 0x1EFF2FE1, 4, 1, 0, { OPERAND_NONE, OPERAND_NONE } },  // ARM32 "BX LR"
    { "NOP", 0x0000A0E1, 4, 1, 0, { OPERAND_NONE, OPERAND_NONE } },  // ARM32 "MOV r0, r0"
    { "MOVE", 0xE0D1F002, 4, 1, 2, { OPERAND_IMMEDIATE, OPERAND_REGISTER } },  // ARM32 "SBCS R15, Rn, Rm"
};

static const OpcodeEntry x86_opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    { "RTS", 0xC3, 1, 1, 0, { OPERAND_NONE, OPERAND_NONE } },  // x86 "RET"
    { "NOP", 0x90, 1, 1, 0, { OPERAND_NONE, OPERAND_NONE } },  // x86 "NOP"
    { "MOVE", 0x89, 1, 1, 2, { OPERAND_IMMEDIATE, OPERAND_REGISTER } },  // x86 "MOV r/m32, r32"
};

static const OpcodeEntry *g_active_table = NULL;
static size_t g_active_count = 0;

bool opcodes_select_arch(const char *arch_name) {
    if (!arch_name) return false;

    if (strcmp(arch_name, "arm") == 0) {
        g_active_table = arm_opcode_table;
        g_active_count = sizeof(arm_opcode_table) / sizeof(arm_opcode_table[0]);
        return true;
    }
    if (strcmp(arch_name, "x86") == 0) {
        g_active_table = x86_opcode_table;
        g_active_count = sizeof(x86_opcode_table) / sizeof(x86_opcode_table[0]);
        return true;
    }
    return false;
}

const OpcodeEntry* lookup_opcode(const char *mnemonic) {
    // opcodes_select_arch() must run before parsing starts; treat an
    // unset table as "nothing found" rather than crashing.
    for (size_t i = 0; i < g_active_count; i++) {
        if (strcmp(g_active_table[i].mnemonic, mnemonic) == 0) {
            return &g_active_table[i];
        }
    }
    return NULL; // not found
}
