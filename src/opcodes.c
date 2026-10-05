#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

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
    // MOVE also takes memory operands through an address register: (An), (An)+, -(An)
    {"MOVE", 0x89, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REG_OR_MEM}, SIZES_BWL}, // x86 "MOV"
    {"LEA", 0x8D, 1, 1, 2, {OPERAND_LABEL, OPERAND_REGISTER}, SIZES_L},   // x86 "LEA r32, [disp32]"
    // ADD: opcode byte here is only a placeholder; codegen.c picks 81 /0 (imm32)
    // or 01 /r (reg) depending on the source operand kind.
    {"ADD", 0x01, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, SIZES_BWL},
    // SUB: same shape as ADD (80 /5 ib | 81 /5 id | 28 /r | 29 /r).
    {"SUB", 0x29, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, SIZES_BWL},
    // The 68000 has no plain MUL/DIV: multiplication and division come as
    // unsigned (MULU/DIVU) and signed (MULS/DIVS) instructions, and only in
    // the .w form (16x16->32 multiply, 32/16->16r:16q divide). Opcode bytes
    // here are placeholders; codegen.c emits a multi-instruction sequence.
    {"MULU", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, SIZES_W},
    {"MULS", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, SIZES_W},
    {"DIVU", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, SIZES_W},
    {"DIVS", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, SIZES_W},
    // BRA/BSR: opcode byte is a placeholder; codegen.c picks EB (short jmp),
    // E9 (jmp rel32) or E8 (call rel32).
    {"BRA", 0xE9, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BSR", 0xE8, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    // Compares: opcode bytes are placeholders; codegen.c emits the x86 CMP.
    // CMP  <src>, Dn|An   (CMP #imm,Dn is CMPI and CMP src,An is CMPA, as in
    //                      the usual 68K assemblers)
    // CMPA <src>, An      (.w or .l only; a .w source is sign-extended)
    // CMPI #imm, Dn
    // CMPM needs memory operands and isn't supported yet.
    {"CMP",  0x39, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, SIZES_BWL},
    {"CMPA", 0x39, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, SIZES_W | SIZES_L},
    {"CMPI", 0x39, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, SIZES_BWL},
    {"MOVEQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, 0},
    {"CLR", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"TST", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"AND", 0x00, 1, 1, 2, {OPERAND_REG_OR_MEM, OPERAND_EA_ALT}, SIZES_BWL},
    {"OR", 0x00, 1, 1, 2, {OPERAND_REG_OR_MEM, OPERAND_EA_ALT}, SIZES_BWL},
    {"EOR", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_EA_ALT}, SIZES_BWL},
    {"JMP", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, 0},
    {"JSR", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, 0},
    {"ADDQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"SUBQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"DBRA", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    // Conditional branches (Bcc): opcode bytes are placeholders; codegen.c
    // emits the matching x86 Jcc. BHS = BCC and BLO = BCS (usual aliases).
    // DBRA is supported; the other DBcc and Scc forms aren't implemented yet.
    {"BHI", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BLS", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BCC", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BHS", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BCS", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BLO", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BNE", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BEQ", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BVC", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BVS", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BPL", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BMI", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BGE", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BLT", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BGT", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BLE", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
};

static const OpcodeEntry arm_opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    {"RTS", 0x1EFF2FE1, 4, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},           // ARM32 "BX LR"
    {"NOP", 0x0000A0E1, 4, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},           // ARM32 "MOV r0, r0"
    {"MOVE", 0xE0D1F002, 4, 1, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, SIZES_BWL},
    {"LEA", 0x8D, 1, 1, 2, {OPERAND_LABEL, OPERAND_REGISTER}, 0},
    {"MOVEQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, 0},
    {"CLR", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"TST", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"AND", 0x00, 1, 1, 2, {OPERAND_REG_OR_MEM, OPERAND_EA_ALT}, SIZES_BWL},
    {"OR", 0x00, 1, 1, 2, {OPERAND_REG_OR_MEM, OPERAND_EA_ALT}, SIZES_BWL},
    {"EOR", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_EA_ALT}, SIZES_BWL},
    {"JMP", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, 0},
    {"JSR", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, 0},
    {"ADDQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"SUBQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"DBRA", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
};

bool operand_is_memory(OperandType t)
{
    return t == OPERAND_IND || t == OPERAND_POSTINC || t == OPERAND_PREDEC;
}

bool operand_matches(OperandType expected, OperandType actual)
{
    if (expected == OPERAND_REG_OR_IMM)
        return actual == OPERAND_REGISTER || actual == OPERAND_IMMEDIATE;
    if (expected == OPERAND_EA_SRC)
        return actual == OPERAND_REGISTER || actual == OPERAND_IMMEDIATE ||
               operand_is_memory(actual);
    if (expected == OPERAND_REG_OR_MEM)
        return actual == OPERAND_REGISTER || operand_is_memory(actual);
    if (expected == OPERAND_EA_ALT)
        return actual == OPERAND_REGISTER || operand_is_memory(actual);
    if (expected == OPERAND_CONTROL)
        return actual == OPERAND_LABEL || actual == OPERAND_IND;
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

static const char *operand_kind_name(OperandType t)
{
    switch (t)
    {
    case OPERAND_REGISTER:   return "reg";
    case OPERAND_IMMEDIATE:  return "#imm";
    case OPERAND_LABEL:      return "label";
    case OPERAND_REG_OR_IMM: return "reg|#imm";
    case OPERAND_EA_SRC:     return "reg|#imm|mem";
    case OPERAND_REG_OR_MEM: return "reg|mem";
    case OPERAND_EA_ALT:     return "reg|mem";
    case OPERAND_CONTROL:    return "label|(An)";
    default:                 return "";
    }
}

static int compare_opcode_entries(const void *a, const void *b)
{
    const OpcodeEntry *const *ea = a;
    const OpcodeEntry *const *eb = b;
    return strcmp((*ea)->mnemonic, (*eb)->mnemonic);
}

static void format_opcode_entry(const OpcodeEntry *e, char *cell, size_t size)
{
    char sizes[8] = "";
    if (e->size_mask & SIZES_B) strcat(sizes, "b");
    if (e->size_mask & SIZES_W) strcat(sizes, "w");
    if (e->size_mask & SIZES_L) strcat(sizes, "l");

    char operands[40] = "";
    for (size_t k = 0; k < e->operand_count && k < 2; k++)
    {
        if (k) strcat(operands, ", ");
        strcat(operands, operand_kind_name(e->operand_types[k]));
    }

    if (sizes[0] && operands[0])
        snprintf(cell, size, "%s.%s %s", e->mnemonic, sizes, operands);
    else if (sizes[0])
        snprintf(cell, size, "%s.%s", e->mnemonic, sizes);
    else if (operands[0])
        snprintf(cell, size, "%s %s", e->mnemonic, operands);
    else
        snprintf(cell, size, "%s", e->mnemonic);
}

void opcodes_print_table(FILE *out)
{
    if (!g_active_table)
        return;

    const OpcodeEntry **sorted = malloc(g_active_count * sizeof *sorted);
    if (!sorted)
    {
        fprintf(stderr, "Unable to allocate memory to list instructions\n");
        return;
    }
    for (size_t i = 0; i < g_active_count; i++)
        sorted[i] = &g_active_table[i];
    qsort(sorted, g_active_count, sizeof *sorted, compare_opcode_entries);

    fprintf(out, "Instructions for target CPU '%s':\n", g_active_arch_name);
    for (size_t i = 0; i < g_active_count; i += 4)
    {
        fprintf(out, "  ");
        for (size_t j = i; j < i + 4 && j < g_active_count; j++)
        {
            char cell[64];
            format_opcode_entry(sorted[j], cell, sizeof cell);
            fprintf(out, "%-34s", cell);
        }
        fputc('\n', out);
    }
    free(sorted);
}
