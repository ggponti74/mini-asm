#include <string.h>

#include "codegen.h"

// Write a single byte into buffer
void buffer_write(OutputBuffer *out, uint8_t byte) {
    if (out->size < out->capacity) {
        out->data[out->size++] = byte;
    }
}

// Encode one operand (simplified)
static void encode_operand(const Operand *op, OutputBuffer *out) {
    switch (op->type) {
        case OPERAND_REGISTER:
            buffer_write(out, (uint8_t)op->value.reg);
            break;
        case OPERAND_IMMEDIATE:
            buffer_write(out, (uint8_t)(op->value.imm >> 8));
            buffer_write(out, (uint8_t)(op->value.imm & 0xFF));
            break;
        case OPERAND_LABEL:
            // placeholder: labels resolved later
            buffer_write(out, 0x00);
            buffer_write(out, 0x00);
            break;
        default:
            break;
    }
}

static const char *g_emit_error = NULL;

const char *codegen_error(void) { return g_emit_error; }

static bool fail(const char *why) {
    g_emit_error = why;
    return false;
}

// 68K immediates must fit the operation size (as signed or unsigned).
static bool imm_fits(int32_t v, OpSize size) {
    if (size == SIZE_B) return v >= -128 && v <= 255;
    if (size == SIZE_W) return v >= -32768 && v <= 65535;
    return true;
}

static void write_le(OutputBuffer *out, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; i++)
        buffer_write(out, (uint8_t)((v >> (8 * i)) & 0xFF));
}

#define X86_OPSIZE_PREFIX 0x66  // switches a 32-bit x86 operation to 16 bits
#define ERR_BYTE_REG "byte-sized operations only work on D0-D3 on this target (D4-D7 would hit AH/CH/DH/BH)"

static uint32_t g_pc = 0;
static bool g_final_pass = false;
static uint32_t g_regfile_base = 0;

void codegen_set_regfile(uint32_t base) {
    g_regfile_base = base;
}

uint32_t codegen_regfile_base(void) {
    return g_regfile_base;
}

void codegen_set_context(uint32_t pc, bool final_pass) {
    g_pc = pc;
    g_final_pass = final_pass;
}

static int x86_reg_code(int reg);   // defined below

// CMP / CMPA / CMPI: compute (dst - src) at the operation size, set the
// flags, and change no register. 68K CMP sets N, Z, V and C (C is the
// borrow) and leaves X alone; x86 CMP sets SF, ZF, OF and CF the same way
// from the same subtraction, so it is a direct match:
//   cmp src, Dn : 38 /r | 66 39 /r | 39 /r   (ModRM = 11 sss ddd:
//                 "CMP r/m, r" is r/m - r, so r/m = dst, reg = src)
//   cmp #i, Dn  : 80 /7 ib | 66 81 /7 iw | 81 /7 id   (ModRM = 11 111 ddd)
// CMPA (also CMP with an A-register destination) always compares all 32
// bits of An; a .w source is sign-extended first:
//   immediate   -> sign-extended at assembly time, then 81 /7 id
//   register    -> push src; movsx src,src16; cmp ; pop src (pop leaves
//                  the flags alone), so the source register is unchanged.
//                  When src is An itself the compare goes against the
//                  pushed copy: cmp [esp], src.
typedef enum { CMP_PLAIN, CMP_ADDR, CMP_IMM } CmpKind;

static bool emit_x86_cmp(const Operand *operands, OpSize size, CmpKind kind,
                         OutputBuffer *out) {
    const Operand *src = &operands[0], *dst = &operands[1];
    if (dst->type != OPERAND_REGISTER) return false;
    bool dst_addr = dst->value.reg >= 8;
    int d = x86_reg_code(dst->value.reg);
    if (d < 0) return false;

    if (kind == CMP_IMM && dst_addr)
        return fail("CMPI needs a data register (D0-D7) destination");
    if (kind == CMP_ADDR && !dst_addr)
        return fail("CMPA needs an address register destination (use CMP for data registers)");
    bool addr_cmp = dst_addr;                       // CMPA semantics
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (addr_cmp && size == SIZE_B)
        return fail("comparing with an address register can't be byte-sized (CMPA has no .b)");
    if (!addr_cmp && size == SIZE_B && d > 3) return fail(ERR_BYTE_REG);

    if (src->type == OPERAND_IMMEDIATE) {
        if (!imm_fits(src->value.imm, size))
            return fail("immediate value doesn't fit the operation size");
        uint32_t v = (uint32_t)src->value.imm;
        if (addr_cmp) {
            if (size == SIZE_W) v = (uint32_t)(int32_t)(int16_t)v;   // CMPA.W sign-extends
            buffer_write(out, 0x81);
            buffer_write(out, (uint8_t)(0xF8 | d));
            write_le(out, v, 4);
            return true;
        }
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, size == SIZE_B ? 0x80 : 0x81);
        buffer_write(out, (uint8_t)(0xF8 | d));
        write_le(out, v, size == SIZE_B ? 1 : size == SIZE_W ? 2 : 4);
        return true;
    }

    if (src->type != OPERAND_REGISTER) return false;
    if (size == SIZE_B && src->value.reg >= 8)
        return fail("byte-sized CMP can't use an address register as the source");
    int s = x86_reg_code(src->value.reg);
    if (s < 0) return false;
    if (size == SIZE_B && s > 3) return fail(ERR_BYTE_REG);

    if (addr_cmp && size == SIZE_W) {
        if (s == 4)
            return fail("CMPA.W with D4 as the source isn't supported on this target (D4 is ESP)");
        buffer_write(out, (uint8_t)(0x50 + s));                      // push src
        buffer_write(out, 0x0F); buffer_write(out, 0xBF);            // movsx src, src16
        buffer_write(out, (uint8_t)(0xC0 | (s << 3) | s));
        if (s == d) {                                                // cmp [esp], src
            buffer_write(out, 0x39);
            buffer_write(out, (uint8_t)(0x04 | (s << 3)));
            buffer_write(out, 0x24);
        } else {                                                     // cmp dst, src
            buffer_write(out, 0x39);
            buffer_write(out, (uint8_t)(0xC0 | (s << 3) | d));
        }
        buffer_write(out, (uint8_t)(0x58 + s));                      // pop src
        return true;
    }
    if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
    buffer_write(out, size == SIZE_B ? 0x38 : 0x39);
    buffer_write(out, (uint8_t)(0xC0 | (s << 3) | d));
    return true;
}

// 68K condition -> x86 condition code (the low nibble of Jcc: 7x / 0F 8x).
// After a 68K compare/arithmetic instruction the N, Z, V, C flags are in the
// x86 SF, ZF, OF, CF with the same meaning (C is the borrow for compares),
// so every 68K condition is exactly one x86 condition:
//   HI C=0&Z=0 -> A   LS C=1|Z=1 -> BE   CC/HS C=0 -> AE   CS/LO C=1 -> B
//   NE Z=0 -> NE      EQ Z=1 -> E        VC V=0 -> NO      VS V=1 -> O
//   PL N=0 -> NS      MI N=1 -> S        GE N=V -> GE      LT N!=V -> L
//   GT Z=0&N=V -> G   LE Z=1|N!=V -> LE
static const struct { const char *mnemonic; int x86_cc; } k_bcc[] = {
    {"BHI", 0x7}, {"BLS", 0x6}, {"BCC", 0x3}, {"BHS", 0x3}, {"BCS", 0x2}, {"BLO", 0x2},
    {"BNE", 0x5}, {"BEQ", 0x4}, {"BVC", 0x1}, {"BVS", 0x0}, {"BPL", 0x9}, {"BMI", 0x8},
    {"BGE", 0xD}, {"BLT", 0xC}, {"BGT", 0xF}, {"BLE", 0xE},
};

// Returns the x86 condition code for a Bcc mnemonic, or -1 if it isn't one.
static int bcc_x86_cc(const char *mnemonic) {
    for (size_t i = 0; i < sizeof k_bcc / sizeof k_bcc[0]; i++)
        if (strcmp(mnemonic, k_bcc[i].mnemonic) == 0) return k_bcc[i].x86_cc;
    return -1;
}

static bool is_branch_mnemonic(const char *m) {
    return strcmp(m, "BRA") == 0 || strcmp(m, "BSR") == 0 || bcc_x86_cc(m) >= 0;
}

// BRA / BSR / Bcc with the target already turned into an absolute address:
//   BRA.b      EB cb        jmp rel8    (2 bytes, target within -128..+127)
//   BRA.w/.l   E9 cd        jmp rel32   (5 bytes)
//   BSR.any    E8 cd        call rel32  (5 bytes)
//   Bcc.b      7x cb        jcc rel8    (2 bytes, target within -128..+127)
//   Bcc.w/.l   0F 8x cd     jcc rel32   (6 bytes)
// rel = target - address of the *next* x86 instruction. x86 has no short
// call, so BSR.b is as long as BSR.w. On the 68K the suffix is the
// displacement size; here only .b changes the encoding (a 16-bit x86 branch
// would truncate EIP). CALL pushes the return address and RTS (ret) pops it,
// the same stack protocol as BSR/RTS; jmp, jcc and call change no flags, like
// BRA, Bcc and BSR. The x86 distance differs from what the 68K encoding
// would need, so 68K's own .b/.w range limits are not enforced -- only the
// real x86 limit of the short form is.
static bool emit_x86_branch(const OpcodeEntry *entry, const Operand *operands,
                            OpSize size, OutputBuffer *out) {
    if (operands[0].type != OPERAND_IMMEDIATE) return false;  // resolved label
    bool is_bsr = strcmp(entry->mnemonic, "BSR") == 0;
    int cc = bcc_x86_cc(entry->mnemonic);                      // -1 for BRA/BSR
    if (size == SIZE_UNSPEC) size = BRANCH_DEFAULT_SIZE;
    bool is_short = !is_bsr && size == SIZE_B;
    uint32_t len = is_short ? 2 : (cc >= 0 ? 6 : 5);

    int32_t rel = 0;  // pass 1 only reserves space
    if (g_final_pass)
        rel = (int32_t)((uint32_t)operands[0].value.imm - (g_pc + len));

    if (is_short) {
        if (g_final_pass && (rel < -128 || rel > 127))
            return fail("branch target is too far for a .B branch on this target (the x86 code between is more than 127 bytes); use .W");
        buffer_write(out, cc >= 0 ? (uint8_t)(0x70 + cc) : 0xEB);
        write_le(out, (uint32_t)rel, 1);
    } else if (cc >= 0) {
        buffer_write(out, 0x0F);
        buffer_write(out, (uint8_t)(0x80 + cc));
        write_le(out, (uint32_t)rel, 4);
    } else {
        buffer_write(out, is_bsr ? 0xE8 : 0xE9);
        write_le(out, (uint32_t)rel, 4);
    }
    return true;
}

// Maps mini-asm register numbers (Dn = 0..7, An = 8..15) to x86 register
// encodings (0..7 = EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI). D0-D7 keep
// the same-numbered x86 register. A0 maps to ESI, which means it shares
// a register with D6 -- x86-32 only has 8 general registers, so D0-D7
// and A0-A7 can't all be distinct. Returns -1 for registers with no
// mapping yet.
static int x86_reg_code(int reg) {
    if (reg >= 0 && reg <= 7) return reg;
    if (reg == 8) return 6;  // A0 -> ESI
    return -1;
}

// x86 MOVE Rs, Rd (register to register) at the requested size.
//   MOVE Ds/As, Dn : 88 /r | 66 89 /r | 89 /r   (ModRM = 11 sss ddd)
//     .b/.w only replace the low byte/word of Dn, like the 68K. Sets N,Z from
//     the value moved and clears V,C (X untouched): x86 "mov" touches no flags,
//     so a same-size "test d,d" follows.
//   MOVEA Rs, An   : 89 /r (.l)  |  0F BF /r movsx (.w, sign-extends)
//     No byte size, and no flags change.
// As on the 68K, an address register can't be a byte-sized source.
static bool emit_x86_move_reg(const Operand *src, const Operand *dst, OpSize size,
                              OutputBuffer *out) {
    int s = x86_reg_code(src->value.reg), d = x86_reg_code(dst->value.reg);
    if (s < 0 || d < 0) return false;
    bool to_addr = dst->value.reg >= 8;

    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (size == SIZE_B && to_addr)
        return fail("MOVE to an address register can't be byte-sized (MOVEA has no .b)");
    if (size == SIZE_B && src->value.reg >= 8)
        return fail("byte-sized MOVE can't use an address register as the source");
    if (size == SIZE_B && (s > 3 || d > 3)) return fail(ERR_BYTE_REG);

    if (to_addr) {
        if (size == SIZE_W) {                       // MOVEA.W: sign-extend
            buffer_write(out, 0x0F); buffer_write(out, 0xBF);
            buffer_write(out, (uint8_t)(0xC0 | (d << 3) | s));
        } else {
            buffer_write(out, 0x89);
            buffer_write(out, (uint8_t)(0xC0 | (s << 3) | d));
        }
        return true;
    }
    if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
    buffer_write(out, size == SIZE_B ? 0x88 : 0x89);
    buffer_write(out, (uint8_t)(0xC0 | (s << 3) | d));
    if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
    buffer_write(out, size == SIZE_B ? 0x84 : 0x85);            // test d,d
    buffer_write(out, (uint8_t)(0xC0 | (d << 3) | d));
    return true;
}

// x86 MOVE #imm, reg at the requested size:
//   .b  B0+r ib              (writes only the low byte: AL/CL/DL/BL)
//   .w  66 B8+r iw           (writes only the low 16 bits)
//   .l  B8+r id
// Like the 68K, .b/.w leave the register's upper bits untouched.
// MOVE to an address register is MOVEA: no byte size, .w sign-extends to
// 32 bits, and no flags change. Otherwise 68K MOVE sets N and Z from the
// value and clears V and C (X untouched); x86 "mov" touches no flags, so a
// "test r,r" of the same size follows (84 /r, 66 85 /r or 85 /r): it sets
// SF/ZF, clears CF/OF, and leaves the register alone.
static bool emit_x86_move_imm(const Operand *operands, OpSize size, OutputBuffer *out) {
    if (operands[0].type == OPERAND_REGISTER && operands[1].type == OPERAND_REGISTER)
        return emit_x86_move_reg(&operands[0], &operands[1], size, out);
    const Operand *imm = NULL, *reg = NULL;
    for (int i = 0; i < 2; i++) {
        if (operands[i].type == OPERAND_IMMEDIATE) imm = &operands[i];
        if (operands[i].type == OPERAND_REGISTER) reg = &operands[i];
    }
    if (!imm || !reg) return false;
    int r = x86_reg_code(reg->value.reg);
    if (r < 0) return false;

    bool is_addr = reg->value.reg >= 8;
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (is_addr && size == SIZE_B)
        return fail("MOVE to an address register can't be byte-sized (MOVEA has no .b)");
    if (!imm_fits(imm->value.imm, size))
        return fail("immediate value doesn't fit the operation size");
    if (size == SIZE_B && r > 3)
        return fail(ERR_BYTE_REG);

    uint32_t v = (uint32_t)imm->value.imm;
    if (is_addr) {
        if (size == SIZE_W) v = (uint32_t)(int32_t)(int16_t)v;  // MOVEA.W sign-extends
        buffer_write(out, (uint8_t)(0xB8 + r));
        write_le(out, v, 4);
        return true;
    }

    if (size == SIZE_B) {
        buffer_write(out, (uint8_t)(0xB0 + r));
        write_le(out, v, 1);
        buffer_write(out, 0x84);
    } else if (size == SIZE_W) {
        buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, (uint8_t)(0xB8 + r));
        write_le(out, v, 2);
        buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, 0x85);
    } else {
        buffer_write(out, (uint8_t)(0xB8 + r));
        write_le(out, v, 4);
        buffer_write(out, 0x85);
    }
    buffer_write(out, (uint8_t)(0xC0 | (r << 3) | r));
    return true;
}

// x86 "LEA r32, [disp32]": opcode 8D, ModRM = 00 rrr 101 (absolute
// 32-bit address, no base register), then the address as a 4-byte
// little-endian value. main.c has already turned the label operand into
// an immediate holding the label's absolute address.
static bool emit_x86_lea(const Operand *operands, OutputBuffer *out) {
    const Operand *addr = &operands[0], *reg = &operands[1];
    if (addr->type != OPERAND_IMMEDIATE || reg->type != OPERAND_REGISTER)
        return false;
    // Like the real 68K, LEA targets an address register (A0-A7) only.
    if (reg->value.reg < 8) return false;
    int r = x86_reg_code(reg->value.reg);
    if (r < 0) return false;

    buffer_write(out, 0x8D);
    buffer_write(out, (uint8_t)((r << 3) | 0x05));
    uint32_t v = (uint32_t)addr->value.imm;
    buffer_write(out, (uint8_t)(v & 0xFF));
    buffer_write(out, (uint8_t)((v >> 8) & 0xFF));
    buffer_write(out, (uint8_t)((v >> 16) & 0xFF));
    buffer_write(out, (uint8_t)((v >> 24) & 0xFF));
    return true;
}

// x86 ADD / SUB, with 68K operand order (op src, dst  ->  dst = dst op src),
// at the requested size (.b operates on the low byte only, .w on the low 16
// bits, leaving the rest of the register untouched, as on the 68K):
//   op #imm, Dn : 80 /n ib | 66 81 /n iw | 81 /n id   (ModRM = 11 nnn ddd)
//   op Ds,  Dn  : 00 /r    | 66 01 /r    | 01 /r      (ModRM = 11 sss ddd)
// with /n = /0 and base opcode 00/01 for ADD, /5 and 28/29 for SUB.
// x86 ADD/SUB set ZF/SF/CF/OF from the result at that size, which is what
// the 68K sets in Z/N/C/V (for SUB, x86 CF is a borrow, like the 68K's C),
// so no extra code is needed for the condition codes.
static bool emit_x86_addsub(const Operand *operands, OpSize size, bool is_sub,
                            OutputBuffer *out) {
    const Operand *src = &operands[0], *dst = &operands[1];
    if (dst->type != OPERAND_REGISTER) return false;
    // Like the real 68K, these target a data register (A-register targets
    // would be ADDA/SUBA, different instructions).
    if (dst->value.reg > 7)
        return fail(is_sub ? "SUB needs a data register (D0-D7) destination"
                           : "ADD needs a data register (D0-D7) destination");
    int d = x86_reg_code(dst->value.reg);
    if (d < 0) return false;
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (size == SIZE_B && d > 3) return fail(ERR_BYTE_REG);

    if (src->type == OPERAND_IMMEDIATE) {
        if (!imm_fits(src->value.imm, size))
            return fail("immediate value doesn't fit the operation size");
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, size == SIZE_B ? 0x80 : 0x81);
        buffer_write(out, (uint8_t)((is_sub ? 0xE8 : 0xC0) | d));
        write_le(out, (uint32_t)src->value.imm, size == SIZE_B ? 1 : size == SIZE_W ? 2 : 4);
        return true;
    }
    if (src->type == OPERAND_REGISTER) {
        if (size == SIZE_B && src->value.reg >= 8)
            return fail(is_sub ? "byte-sized SUB can't use an address register as the source"
                               : "byte-sized ADD can't use an address register as the source");
        int sr = x86_reg_code(src->value.reg);
        if (sr < 0) return false;
        if (size == SIZE_B && sr > 3) return fail(ERR_BYTE_REG);
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, (uint8_t)((is_sub ? 0x28 : 0x00) + (size == SIZE_B ? 0 : 1)));
        buffer_write(out, (uint8_t)(0xC0 | (sr << 3) | d));
        return true;
    }
    return false;
}

// ---- MULU/MULS/DIVU/DIVS ---------------------------------------------------
// The 68000 forms are word-sized only:
//   MULU.W / MULS.W  <ea>,Dn : Dn(32) = Dn.w * <ea>.w   (unsigned / signed)
//   DIVU.W / DIVS.W  <ea>,Dn : Dn(32) / <ea>.w -> low word = quotient,
//                              high word = remainder
// The source is a data register or an immediate (never an address register).
// Flags: MUL sets N,Z from the 32-bit result and clears V,C. DIV sets N,Z
// from the 16-bit quotient and clears C; V is set (and Dn left untouched) if
// the quotient doesn't fit 16 bits (N and Z are undefined then; we leave
// N=1, Z=0). X is not modelled on x86 (see the other instructions).
// Dividing by zero traps, as on the 68K (on Linux: SIGFPE).
//
// D4 maps to ESP, which these sequences push/pop through, so it's refused.
#define X86_ESP 4
#define ERR_ESP_REG "D4 maps to ESP on this target; MULU/MULS/DIVU/DIVS can't use it yet"

// Shared operand checks. On success fills *d (x86 code of Dn), *s (x86
// code of the source register, or -1 for an immediate) and *imm.
static bool muldiv_operands(const Operand *operands, OpSize size,
                            int *d, int *s, int32_t *imm) {
    const Operand *src = &operands[0], *dst = &operands[1];
    if (dst->type != OPERAND_REGISTER || dst->value.reg > 7)
        return fail("MULU/MULS/DIVU/DIVS need a data register (D0-D7) destination");
    if (size != SIZE_UNSPEC && size != SIZE_W)
        return fail("MULU/MULS/DIVU/DIVS only exist as .w");
    *d = x86_reg_code(dst->value.reg);
    if (*d < 0) return false;
    if (*d == X86_ESP) return fail(ERR_ESP_REG);
    *s = -1;
    *imm = 0;
    if (src->type == OPERAND_IMMEDIATE) {
        if (!imm_fits(src->value.imm, SIZE_W))
            return fail("immediate value doesn't fit the operation size");
        *imm = src->value.imm;
        return true;
    }
    if (src->type != OPERAND_REGISTER) return false;
    if (src->value.reg >= 8)
        return fail("MULU/MULS/DIVU/DIVS can't take an address register as the source");
    *s = x86_reg_code(src->value.reg);
    if (*s < 0) return false;
    if (*s == X86_ESP) return fail(ERR_ESP_REG);
    return true;
}

// MULU.W / MULS.W. x86 "imul r32,r/m32" gives the right low 32 bits for
// both signs once the 16-bit inputs are extended (zero-extended for MULU,
// sign-extended for MULS), and the product always fits 32 bits. imul leaves
// SF/ZF undefined, so a final "test d,d" sets them (and clears CF/OF = C/V).
//   [push s ; ext s,s16 ;] ext d,d16 ; imul d,s|imm ; [pop s ;] test d,d
// The source register is saved on the stack and restored: the 68K never
// modifies the source.
static bool emit_x86_mul(const Operand *operands, OpSize size, bool is_signed,
                         OutputBuffer *out) {
    int d, s;
    int32_t imm;
    if (!muldiv_operands(operands, size, &d, &s, &imm))
        return false;
    uint8_t ext = is_signed ? 0xBF : 0xB7;  // movsx / movzx r32,r/m16

    if (s >= 0 && s != d) {
        // Save the source first, then extend it in place.
        buffer_write(out, (uint8_t)(0x50 + s));                     // push s
        buffer_write(out, 0x0F); buffer_write(out, ext);
        buffer_write(out, (uint8_t)(0xC0 | (s << 3) | s));          // ext s, s16
    }
    buffer_write(out, 0x0F); buffer_write(out, ext);
    buffer_write(out, (uint8_t)(0xC0 | (d << 3) | d));              // ext d, d16

    if (s < 0) {
        uint32_t v = is_signed ? (uint32_t)(int32_t)(int16_t)imm : (uint32_t)(uint16_t)imm;
        buffer_write(out, 0x69);                                    // imul d,d,imm32
        buffer_write(out, (uint8_t)(0xC0 | (d << 3) | d));
        write_le(out, v, 4);
    } else {
        buffer_write(out, 0x0F); buffer_write(out, 0xAF);           // imul d,s (s==d: squares)
        buffer_write(out, (uint8_t)(0xC0 | (d << 3) | s));
        if (s != d)
            buffer_write(out, (uint8_t)(0x58 + s));                 // pop s
    }
    buffer_write(out, 0x85);                                        // test d,d
    buffer_write(out, (uint8_t)(0xC0 | (d << 3) | d));
    return true;
}

// DIVU.W / DIVS.W via x86 div/idiv r32 (EDX:EAX / r32), which needs EAX and
// EDX, plus a scratch register t for the extended divisor. EAX, EDX and t
// are saved on the stack and restored, so only Dn changes. The 32-bit x86
// quotient is then range-checked against 16 bits to reproduce the 68K's
// overflow rule (the x86 itself only traps when the *32-bit* quotient
// overflows, which can't happen for unsigned and is special-cased for
// signed INT_MIN / -1).
//
//   push eax ; push edx ; push t
//   t = ext16(divisor) | imm
//   mov eax,Dn ; xor edx,edx | cdq
//   div t | (cmp t,-1 ; jne L ; neg eax ; xor edx,edx ; jmp M ; L: idiv t ; M:)
//   overflow check -> jne OVF
//   Dn = (edx << 16) | ax ; test ax,ax     (flags: N,Z from quotient, C=V=0)
//   jmp DONE
//   OVF: mov al,7Fh ; add al,1             (flags: V=1, C=0; Dn untouched)
//   DONE: pop t ; pop edx ; pop eax
static bool emit_x86_div(const Operand *operands, OpSize size, bool is_signed,
                         OutputBuffer *out) {
    int d, s;
    int32_t imm;
    if (!muldiv_operands(operands, size, &d, &s, &imm))
        return false;

    // Scratch register: first of ECX, EBX, EBP, ESI, EDI not used as Dn / source.
    static const int cand[] = {1, 3, 5, 6, 7};
    int t = -1;
    for (size_t i = 0; i < sizeof cand / sizeof cand[0]; i++)
        if (cand[i] != d && cand[i] != s) { t = cand[i]; break; }

    buffer_write(out, 0x50);                                        // push eax
    buffer_write(out, 0x52);                                        // push edx
    buffer_write(out, (uint8_t)(0x50 + t));                         // push t

    if (s >= 0) {                                                   // movzx/movsx t, s16
        buffer_write(out, 0x0F); buffer_write(out, is_signed ? 0xBF : 0xB7);
        buffer_write(out, (uint8_t)(0xC0 | (t << 3) | s));
    } else {                                                        // mov t, imm32
        uint32_t v = is_signed ? (uint32_t)(int32_t)(int16_t)imm : (uint32_t)(uint16_t)imm;
        buffer_write(out, (uint8_t)(0xB8 + t));
        write_le(out, v, 4);
    }
    if (d != 0) {                                                   // mov eax, Dn
        buffer_write(out, 0x89);
        buffer_write(out, (uint8_t)(0xC0 | (d << 3)));
    }

    if (!is_signed) {
        buffer_write(out, 0x31); buffer_write(out, 0xD2);           // xor edx,edx
        buffer_write(out, 0xF7); buffer_write(out, (uint8_t)(0xF0 | t));  // div t
        buffer_write(out, 0xA9);                                    // test eax,FFFF0000h
        write_le(out, 0xFFFF0000u, 4);
    } else {
        buffer_write(out, 0x99);                                    // cdq
        buffer_write(out, 0x83); buffer_write(out, (uint8_t)(0xF8 | t));
        buffer_write(out, 0xFF);                                    // cmp t,-1
        buffer_write(out, 0x75); buffer_write(out, 0x06);           // jne L (skip 6 bytes)
        buffer_write(out, 0xF7); buffer_write(out, 0xD8);           // neg eax
        buffer_write(out, 0x31); buffer_write(out, 0xD2);           // xor edx,edx
        buffer_write(out, 0xEB); buffer_write(out, 0x02);           // jmp M (skip idiv)
        buffer_write(out, 0xF7); buffer_write(out, (uint8_t)(0xF8 | t));  // L: idiv t
        buffer_write(out, 0x0F); buffer_write(out, 0xBF);
        buffer_write(out, (uint8_t)(0xC0 | (t << 3)));              // M: movsx t,ax
        buffer_write(out, 0x39); buffer_write(out, (uint8_t)(0xC0 | (t << 3)));  // cmp eax,t
    }

    // Size of the "normal" path that follows, so the jne can skip it.
    size_t write_len = (d == 0 || d == 2) ? 4 : 2;                  // store result into Dn
    size_t normal_len = 3 + 3 + 2 + 3 + write_len + 2;              // shl, movzx, or, test, store, jmp
    buffer_write(out, 0x75); buffer_write(out, (uint8_t)normal_len);// jne/jnz OVF

    buffer_write(out, 0xC1); buffer_write(out, 0xE2); buffer_write(out, 0x10);  // shl edx,16
    buffer_write(out, 0x0F); buffer_write(out, 0xB7); buffer_write(out, 0xC0);  // movzx eax,ax
    buffer_write(out, 0x09); buffer_write(out, 0xD0);                           // or eax,edx
    buffer_write(out, 0x66); buffer_write(out, 0x85); buffer_write(out, 0xC0);  // test ax,ax
    if (d == 0) {                                                   // result replaces the saved EAX
        buffer_write(out, 0x89); buffer_write(out, 0x44); buffer_write(out, 0x24); buffer_write(out, 0x08);
    } else if (d == 2) {                                            // ... or the saved EDX
        buffer_write(out, 0x89); buffer_write(out, 0x44); buffer_write(out, 0x24); buffer_write(out, 0x04);
    } else {                                                        // mov Dn, eax
        buffer_write(out, 0x89); buffer_write(out, (uint8_t)(0xC0 | d));
    }
    buffer_write(out, 0xEB); buffer_write(out, 0x04);               // jmp DONE (skip OVF)

    buffer_write(out, 0xB0); buffer_write(out, 0x7F);               // OVF: mov al,7Fh
    buffer_write(out, 0x04); buffer_write(out, 0x01);               //      add al,1  -> OF=1, CF=0

    buffer_write(out, (uint8_t)(0x58 + t));                         // pop t
    buffer_write(out, 0x5A);                                        // pop edx
    buffer_write(out, 0x58);                                        // pop eax
    return true;
}

// Emit full instruction
int emit_code(const OpcodeEntry *entry, Operand *operands, OpSize size,
              OutputBuffer *out) {
    if (!entry || !out) return -1;
    g_emit_error = NULL;

    const char *arch = opcodes_active_arch_name();
    if (arch && strcmp(arch, "x86") == 0 && entry->operand_count == 2) {
        if (strcmp(entry->mnemonic, "MOVE") == 0)
            return emit_x86_move_imm(operands, size, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "LEA") == 0)
            return emit_x86_lea(operands, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "ADD") == 0)
            return emit_x86_addsub(operands, size, false, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "SUB") == 0)
            return emit_x86_addsub(operands, size, true, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "MULU") == 0)
            return emit_x86_mul(operands, size, false, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "MULS") == 0)
            return emit_x86_mul(operands, size, true, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "DIVU") == 0)
            return emit_x86_div(operands, size, false, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "DIVS") == 0)
            return emit_x86_div(operands, size, true, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "CMP") == 0)
            return emit_x86_cmp(operands, size, CMP_PLAIN, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "CMPA") == 0)
            return emit_x86_cmp(operands, size, CMP_ADDR, out) ? 0 : -1;
        if (strcmp(entry->mnemonic, "CMPI") == 0)
            return emit_x86_cmp(operands, size, CMP_IMM, out) ? 0 : -1;
    }
    if (arch && strcmp(arch, "x86") == 0 && entry->operand_count == 1 &&
        is_branch_mnemonic(entry->mnemonic))
        return emit_x86_branch(entry, operands, size, out) ? 0 : -1;

    for (size_t i = entry->size; i > 0; i--) {
        buffer_write(out, (uint8_t)(entry->opcode >> ((i - 1) * 8)));
    }
    for (size_t i = 0; i < entry->operand_count; i++) {
        encode_operand(&operands[i], out);
    }
    return 0;
}