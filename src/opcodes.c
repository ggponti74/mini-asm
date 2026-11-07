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
    {"RTS", 0xC3, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0}, // x86 "RET"
    {"NOP", 0x90, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0}, // x86 "NOP"
    // MOVE also takes memory operands through an address register: (An), (An)+, -(An)
    {"MOVE", 0x89, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REG_OR_MEM}, SIZES_BWL}, // x86 "MOV"
    {"LEA", 0x8D, 1, 1, 2, {OPERAND_CONTROL, OPERAND_REGISTER}, SIZES_L},     // LEA label|(An)|d(An),An
    // ADD: opcode byte here is only a placeholder; codegen.c picks 81 /0 (imm32)
    // or 01 /r (reg) depending on the source operand kind.
    {"ADD", 0x01, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},
    // SUB: same shape as ADD (80 /5 ib | 81 /5 id | 28 /r | 29 /r).
    {"SUB", 0x29, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},
    // The 68000 has no plain MUL/DIV: multiplication and division come as
    // unsigned (MULU/DIVU) and signed (MULS/DIVS) instructions, and only in
    // the .w form (16x16->32 multiply, 32/16->16r:16q divide). Opcode bytes
    // here are placeholders; codegen.c emits a multi-instruction sequence.
    {"MULU", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W},
    {"MULS", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W},
    {"DIVU", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W},
    {"DIVS", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W},
    // BRA/BSR: opcode byte is a placeholder; codegen.c picks EB (short jmp),
    // E9 (jmp rel32) or E8 (call rel32).
    {"BRA", 0xE9, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BSR", 0xE8, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    // TRAP #0: mini-asm service call. codegen.c emits CALL to the target's
    // dispatcher (core.c); the opcode byte here is a placeholder.
    {"TRAP", 0x00, 1, 1, 1, {OPERAND_IMMEDIATE, OPERAND_NONE}, 0},
    // Compares: opcode bytes are placeholders; codegen.c emits the x86 CMP.
    // CMP  <src>, Dn|An   (CMP #imm,Dn is CMPI and CMP src,An is CMPA, as in
    //                      the usual 68K assemblers)
    // CMPA <src>, An      (.w or .l only; a .w source is sign-extended)
    // CMPI #imm, Dn
    // CMPM needs memory operands and isn't supported yet.
    {"CMP", 0x39, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},   // CMP #imm,<ea> is CMPI
    {"CMPA", 0x39, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W | SIZES_L},
    {"ADDA", 0x01, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W | SIZES_L},
    {"SUBA", 0x29, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W | SIZES_L},
    {"CMPM", 0x39, 1, 1, 2, {OPERAND_POSTINC, OPERAND_POSTINC}, SIZES_BWL},
    {"CMPI", 0x39, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"MOVEQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, 0},
    {"CLR", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"TST", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"AND", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},     // AND #imm,<ea> is ANDI
    {"OR", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},      // OR #imm,<ea> is ORI
    {"EOR", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_BWL}, // EOR #imm,<ea> is EORI
    // Immediate forms: placeholders; codegen.c runs them as AND/OR/EOR/ADD/SUB
    // (emit_code maps them to the base entry), the types here only restrict the syntax.
    {"ANDI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"ORI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"EORI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"ADDI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"SUBI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"JMP", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, 0},
    {"JSR", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, 0},
    {"ADDQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"SUBQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"DBRA", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    // Scc <ea> (byte: 0xFF if the condition holds, else 0; flags unchanged), DBcc Dn,label,
    // EXG and PEA. Opcode fields are placeholders; codegen.c emits the code.
    {"ST", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SF", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SHI", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SLS", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SCC", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SHS", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SCS", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SLO", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SNE", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SEQ", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SVC", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SVS", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SPL", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SMI", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SGE", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SLT", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SGT", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SLE", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"DBT", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBF", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBHI", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBLS", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBCC", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBHS", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBCS", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBLO", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBNE", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBEQ", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBVC", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBVS", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBPL", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBMI", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBGE", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBLT", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBGT", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBLE", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"EXG", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_REGISTER}, SIZES_L},
    {"PEA", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, SIZES_L},
    {"LINK", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_IMMEDIATE}, SIZES_W},
    {"UNLK", 0x00, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"MOVEM", 0x00, 1, 1, 2, {OPERAND_MOVEM_ARG, OPERAND_MOVEM_ARG}, SIZES_W | SIZES_L},
    // Conditional branches (Bcc): opcode bytes are placeholders; codegen.c
    // emits the matching x86 Jcc. BHS = BCC and BLO = BCS (usual aliases).
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
    // Bit manipulation. Opcode bytes are placeholders; codegen.c emits the code.
    // NOT/NEG <ea>; SWAP Dn; EXT.w/.l Dn
    // BTST/BSET/BCLR/BCHG Dn|#n, Dn (long) or <mem> (byte)
    // ASL/ASR/LSL/LSR/ROL/ROR  Dx|#1..8, Dy   or   <mem> (word, one bit)
    // ROXL/ROXR need the X flag, which isn't modelled yet, so they're not here.
    {"NOT", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"NEG", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"SWAP", 0x00, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, SIZES_W},
    {"EXT", 0x00, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, SIZES_W | SIZES_L},
    {"BTST", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_B | SIZES_L},
    {"BSET", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_B | SIZES_L},
    {"BCLR", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_B | SIZES_L},
    {"BCHG", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_B | SIZES_L},
    {"ASL", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"ASR", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"LSL", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"LSR", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"ROL", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"ROR", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
};

static const OpcodeEntry arm_opcode_table[] = {
    // mnemonic, opcode, size (bytes), length (words), operand_count, operand_types[]
    {"RTS", 0x1EFF2FE1, 4, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0}, // ARM32 "BX LR"
    {"NOP", 0x0000A0E1, 4, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0}, // ARM32 "MOV r0, r0"
    // Opcode words below are placeholders: codegen.c emits the real ARM
    // instruction sequences (the 68K registers live in the register block).
    {"MOVE", 0xE0D1F002, 4, 1, 2, {OPERAND_EA_SRC, OPERAND_REG_OR_MEM}, SIZES_BWL},
    {"LEA", 0x8D, 1, 1, 2, {OPERAND_CONTROL, OPERAND_REGISTER}, 0},
    {"ADD", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},
    {"SUB", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},
    {"MULU", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W},
    {"MULS", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W},
    {"DIVU", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W},
    {"DIVS", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W},
    {"CMP", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},
    {"CMPA", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W | SIZES_L},
    {"ADDA", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W | SIZES_L},
    {"SUBA", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_REGISTER}, SIZES_W | SIZES_L},
    {"CMPM", 0x00, 1, 1, 2, {OPERAND_POSTINC, OPERAND_POSTINC}, SIZES_BWL},
    {"CMPI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"BRA", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    {"BSR", 0x00, 1, 1, 1, {OPERAND_LABEL, OPERAND_NONE}, SIZES_BWL},
    // TRAP #0: mini-asm service call. codegen.c emits push {lr}; ldr r12,=block;
    // bl dispatcher; pop {lr} (24 bytes); the size field here is a placeholder.
    {"TRAP", 0x00, 4, 1, 1, {OPERAND_IMMEDIATE, OPERAND_NONE}, 0},
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

    {"MOVEQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, 0},
    {"CLR", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"TST", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"AND", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},     // AND #imm,<ea> is ANDI
    {"OR", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_EA_ALT}, SIZES_BWL},      // OR #imm,<ea> is ORI
    {"EOR", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_BWL}, // EOR #imm,<ea> is EORI
    // Immediate forms: placeholders; codegen.c runs them as AND/OR/EOR/ADD/SUB
    // (emit_code maps them to the base entry), the types here only restrict the syntax.
    {"ANDI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"ORI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"EORI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"ADDI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"SUBI", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"JMP", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, 0},
    {"JSR", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, 0},
    {"ADDQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"SUBQ", 0x00, 1, 1, 2, {OPERAND_IMMEDIATE, OPERAND_EA_ALT}, SIZES_BWL},
    {"DBRA", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    // Scc <ea> (byte: 0xFF if the condition holds, else 0; flags unchanged), DBcc Dn,label,
    // EXG and PEA. Opcode fields are placeholders; codegen.c emits the code.
    {"ST", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SF", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SHI", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SLS", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SCC", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SHS", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SCS", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SLO", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SNE", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SEQ", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SVC", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SVS", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SPL", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SMI", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SGE", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SLT", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SGT", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"SLE", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_B},
    {"DBT", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBF", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBHI", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBLS", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBCC", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBHS", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBCS", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBLO", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBNE", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBEQ", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBVC", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBVS", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBPL", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBMI", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBGE", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBLT", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBGT", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"DBLE", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_LABEL}, 0},
    {"EXG", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_REGISTER}, SIZES_L},
    {"PEA", 0x00, 1, 1, 1, {OPERAND_CONTROL, OPERAND_NONE}, SIZES_L},
    {"LINK", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_IMMEDIATE}, SIZES_W},
    {"UNLK", 0x00, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"MOVEM", 0x00, 1, 1, 2, {OPERAND_MOVEM_ARG, OPERAND_MOVEM_ARG}, SIZES_W | SIZES_L},
    // Bit manipulation (same forms as the x86 table above).
    // NOT/NEG <ea>; SWAP Dn; EXT.w/.l Dn
    // BTST/BSET/BCLR/BCHG Dn|#n, Dn (long) or <mem> (byte)
    // ASL/ASR/LSL/LSR/ROL/ROR  Dx|#1..8, Dy   or   <mem> (word, one bit)
    // ROXL/ROXR need the X flag, which isn't modelled yet, so they're not here.
    {"NOT", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"NEG", 0x00, 1, 1, 1, {OPERAND_EA_ALT, OPERAND_NONE}, SIZES_BWL},
    {"SWAP", 0x00, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, SIZES_W},
    {"EXT", 0x00, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, SIZES_W | SIZES_L},
    {"BTST", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_B | SIZES_L},
    {"BSET", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_B | SIZES_L},
    {"BCLR", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_B | SIZES_L},
    {"BCHG", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_EA_ALT}, SIZES_B | SIZES_L},
    {"ASL", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"ASR", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"LSL", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"LSR", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"ROL", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
    {"ROR", 0x00, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, SIZES_BWL},
};

static const OpcodeEntry z80_opcode_table[] = {
    {"NOP", 0x00, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"HALT", 0x76, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"RET", 0xC9, 1, 1, 1, {OPERAND_OPT_REG, OPERAND_NONE}, 0},
    {"LD", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REG_OR_IMM}, 0},
    {"JP", 0xC3, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_OPT_ANY}, 0},
    {"JR", 0x18, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_OPT_ANY}, 0},
    {"DJNZ", 0x10, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"CALL", 0xCD, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_OPT_ANY}, 0},
    {"PUSH", 0xC5, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"POP", 0xC1, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"ADD", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_REG_OR_IMM}, 0},
    {"ADC", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_REG_OR_IMM}, 0},
    {"SUB", 0x00, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"SBC", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_REG_OR_IMM}, 0},
    {"AND", 0x00, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"OR", 0x00, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"XOR", 0x00, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"CP", 0x00, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"INC", 0x00, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"DEC", 0x00, 1, 1, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"RLC", 0xCB00, 2, 2, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"RRC", 0xCB08, 2, 2, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"RL", 0xCB10, 2, 2, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"RR", 0xCB18, 2, 2, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"SLA", 0xCB20, 2, 2, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"SRA", 0xCB28, 2, 2, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"SRL", 0xCB38, 2, 2, 1, {OPERAND_REGISTER, OPERAND_NONE}, 0},
    {"BIT", 0xCB40, 2, 2, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, 0},
    {"RES", 0xCB80, 2, 2, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, 0},
    {"SET", 0xCBC0, 2, 2, 2, {OPERAND_IMMEDIATE, OPERAND_REGISTER}, 0},
    {"LDI", 0xEDA0, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"LDIR", 0xEDB0, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"LDD", 0xEDA8, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"LDDR", 0xEDB8, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CPI", 0xEDA1, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CPIR", 0xEDB1, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CPD", 0xEDA9, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CPDR", 0xEDB9, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"IN", 0x00, 1, 1, 2, {OPERAND_REGISTER, OPERAND_REG_OR_IMM}, 0},
    {"OUT", 0x00, 1, 1, 2, {OPERAND_REG_OR_IMM, OPERAND_REGISTER}, 0},
    {"INI", 0xEDA2, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"INIR", 0xEDB2, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"IND", 0xEDAA, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"INDR", 0xEDBA, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"OUTI", 0xEDA3, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"OTIR", 0xEDB3, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"OUTD", 0xEDAB, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"OTDR", 0xEDBB, 2, 2, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"RLCA", 0x07, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"RRCA", 0x0F, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"RLA", 0x17, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"RRA", 0x1F, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CPL", 0x2F, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"DAA", 0x27, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"SCF", 0x37, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CCF", 0x3F, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"RST", 0xC7, 1, 1, 1, {OPERAND_IMMEDIATE, OPERAND_NONE}, 0},
};

static const OpcodeEntry m6501_opcode_table[] = {
    {"NOP", 0xEA, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"BRK", 0x00, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"RTI", 0x40, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"RTS", 0x60, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"PHP", 0x08, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"PLP", 0x28, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"PHA", 0x48, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"PLA", 0x68, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"TAX", 0xAA, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"TXA", 0x8A, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"TAY", 0xA8, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"TYA", 0x98, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"TSX", 0xBA, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"TXS", 0x9A, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"INX", 0xE8, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"DEX", 0xCA, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"INY", 0xC8, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"DEY", 0x88, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CLC", 0x18, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"SEC", 0x38, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CLI", 0x58, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"SEI", 0x78, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CLV", 0xB8, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"CLD", 0xD8, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"SED", 0xF8, 1, 1, 0, {OPERAND_NONE, OPERAND_NONE}, 0},
    {"JMP", 0x4C, 1, 1, 1, {OPERAND_EA_SRC, OPERAND_NONE}, 0},
    {"JSR", 0x20, 1, 1, 1, {OPERAND_EA_SRC, OPERAND_NONE}, 0},
    {"LDA", 0xA9, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"LDX", 0xA2, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"LDY", 0xA0, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"STA", 0x85, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"STX", 0x86, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"STY", 0x84, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"ADC", 0x69, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"SBC", 0xE9, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"AND", 0x29, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"ORA", 0x09, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"EOR", 0x49, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"CMP", 0xC9, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"CPX", 0xE0, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"CPY", 0xC0, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"BIT", 0x24, 1, 1, 1, {OPERAND_EA_SRC, OPERAND_NONE}, 0},
    {"INC", 0xE6, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"DEC", 0xC6, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"ASL", 0x06, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"LSR", 0x46, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"ROL", 0x26, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"ROR", 0x66, 1, 1, 2, {OPERAND_EA_SRC, OPERAND_OPT_REG}, 0},
    {"BRA", 0x80, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"BCC", 0x90, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"BCS", 0xB0, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"BEQ", 0xF0, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"BMI", 0x30, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"BNE", 0xD0, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"BPL", 0x10, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"BVC", 0x50, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
    {"BVS", 0x70, 1, 1, 1, {OPERAND_REG_OR_IMM, OPERAND_NONE}, 0},
};

bool operand_is_memory(OperandType t)
{
    return t == OPERAND_IND || t == OPERAND_POSTINC || t == OPERAND_PREDEC || t == OPERAND_DISP;
}

bool operand_matches(OperandType expected, OperandType actual)
{
    if (expected == OPERAND_REG_OR_IMM)
        return actual == OPERAND_REGISTER || actual == OPERAND_IMMEDIATE || actual == OPERAND_LABEL;
    if (expected == OPERAND_EA_SRC)
        return actual == OPERAND_REGISTER || actual == OPERAND_IMMEDIATE || actual == OPERAND_LABEL ||
               operand_is_memory(actual);
    if (expected == OPERAND_REG_OR_MEM)
        return actual == OPERAND_REGISTER || actual == OPERAND_LABEL || operand_is_memory(actual);
    if (expected == OPERAND_EA_ALT)
        return actual == OPERAND_REGISTER || actual == OPERAND_LABEL || operand_is_memory(actual);
    if (expected == OPERAND_CONTROL)
        return actual == OPERAND_LABEL || actual == OPERAND_IND || actual == OPERAND_DISP;
    if (expected == OPERAND_MOVEM_ARG)
        return actual == OPERAND_REGISTER || actual == OPERAND_REGLIST ||
               operand_is_memory(actual);
    if (expected == OPERAND_OPT_REG)
        return actual == OPERAND_REGISTER;
    if (expected == OPERAND_OPT_ANY)
        return actual != OPERAND_NONE && actual != OPERAND_BAD;
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
    if (strcmp(arch_name, "z80") == 0)
    {
        g_active_table = z80_opcode_table;
        g_active_count = sizeof(z80_opcode_table) / sizeof(z80_opcode_table[0]);
        g_active_arch_name = "z80";
        return true;
    }
    if (strcmp(arch_name, "6501") == 0)
    {
        g_active_table = m6501_opcode_table;
        g_active_count = sizeof(m6501_opcode_table) / sizeof(m6501_opcode_table[0]);
        g_active_arch_name = "6501";
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
    case OPERAND_REGISTER:
        return "reg";
    case OPERAND_IMMEDIATE:
        return "#imm";
    case OPERAND_LABEL:
        return "label";
    case OPERAND_REG_OR_IMM:
        return "reg|#imm";
    case OPERAND_EA_SRC:
        return "reg|#imm|mem";
    case OPERAND_REG_OR_MEM:
        return "reg|mem";
    case OPERAND_EA_ALT:
        return "reg|mem";
    case OPERAND_CONTROL:
        return "label|(An)";
    case OPERAND_OPT_REG:
        return "[reg]";
    case OPERAND_OPT_ANY:
        return "[operand]";
    default:
        return "";
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
    if (e->size_mask & SIZES_B)
        strcat(sizes, "b");
    if (e->size_mask & SIZES_W)
        strcat(sizes, "w");
    if (e->size_mask & SIZES_L)
        strcat(sizes, "l");

    char operands[40] = "";
    for (size_t k = 0; k < e->operand_count && k < 2; k++)
    {
        if (k)
            strcat(operands, ", ");
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