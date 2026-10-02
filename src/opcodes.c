#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "opcodes.h"
#include "codegen.h"

// mini-asm's source language is 68K-style mnemonics (RTS, etc.) regardless
// of target platform. Only the encoded machine code bytes differ per
// architecture — the mnemonic stays "RTS" for both. Both tables are
// always compiled in now; opcodes_select_arch() picks which one
// lookup_opcode() searches, so the choice is a runtime parameter
// instead of a build-time #if.
static const OpcodeEntry x86_opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    {"RTS", 0xC3, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},           // x86 "RET"
    {"NOP", 0x90, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},           // x86 "NOP"
    {"MOVE", 0x89, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, SIZES_BWL}, // x86 "MOV"
    {"LEA", 0x8D, 1, 1, 2, {OPERAND_LABEL, OPERAND_REGISTER}, SIZES_L},   // x86 "LEA r32, [disp32]"
    // ADD: opcode byte here is only a placeholder; codegen.c picks 81 /0 (imm32)
    // or 01 /r (reg) depending on the source operand kind.
    {"ADD", 0x01, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, SIZES_BWL},
};

static const OpcodeEntry arm_opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    {"RTS", 0x1EFF2FE1, 4, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},           // ARM32 "BX LR"
    {"NOP", 0x0000A0E1, 4, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},           // ARM32 "MOV r0, r0"
    {"MOVE", 0xE0D1F002, 4, 1, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, 0}, // ARM32 "SBCS R15, Rn, Rm"
   {"LEA", 0x8D, 1, 1, 2, {OPERAND_LABEL, OPERAND_REGISTER}, 0},   // x86 "LEA r32, [disp32]"
};

bool operand_matches(OperandType expected, OperandType actual)
{
    if (expected == OPERAND_REG_OR_IMM)
        return actual == OPERAND_REGISTER || actual == OPERAND_IMMEDIATE;
    return expected == actual;
}

static const OpcodeEntry *g_active_table = NULL;
static size_t g_active_count = 0;
static const char *g_active_arch_name = NULL;

bool opcodes_select_arch(const char *arch_name)
{
    if (!arch_name)
        return false;

    if (strcmp(arch_name, "arm") == 0)
    {
        g_active_table = arm_opcode_table;
        g_active_count = sizeof(arm_opcode_table) / sizeof(arm_opcode_table[0]);
        g_active_arch_name = "arm";
        return true;
    }
    if (strcmp(arch_name, "x86") == 0)
    {
        g_active_table = x86_opcode_table;
        g_active_count = sizeof(x86_opcode_table) / sizeof(x86_opcode_table[0]);
        g_active_arch_name = "x86";
        return true;
    }
    return false;
}

const char *opcodes_active_arch_name(void)
{
    return g_active_arch_name;
}

static int ci_equal(const char *a, const char *b)
{
    size_t i = 0;

    while (a[i] != '\0' && b[i] != '\0')
    {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
        {
            return 0; // Mismatch found
        }
        i++;
    }

    // Return 1 (true) if both strings ended at the same index
    return a[i] == b[i];
}

const OpcodeEntry *lookup_opcode(const char *mnemonic)
{
    // Accept an optional .b/.w/.l suffix ("move.w"); it is looked up by the
    // base mnemonic here and parsed separately by parse_size_suffix().
    char base[32];
    size_t n = strlen(mnemonic);
    if (n >= sizeof base)
        return NULL;
    memcpy(base, mnemonic, n + 1);
    if (n >= 3 && base[n - 2] == '.' && strchr("bwlBWL", base[n - 1]))
        base[n - 2] = '\0';
    mnemonic = base;

    // opcodes_select_arch() must run before parsing starts; treat an
    // unset table as "nothing found" rather than crashing.
    for (size_t i = 0; i < g_active_count; i++)
    {
        if (ci_equal(g_active_table[i].mnemonic, mnemonic))
        {
            return &g_active_table[i];
        }
    }
    return NULL; // not found
}
