#include <string.h>
#include <stdlib.h>

#include "codegen.h"

// Write a single byte into buffer
void buffer_write(OutputBuffer *out, uint8_t byte) {
    if (out->size == out->capacity) {
        size_t new_capacity = out->capacity ? out->capacity * 2 : 1024;
        uint8_t *grown = realloc(out->data, new_capacity);
        if (!grown) abort();
        out->data = grown;
        out->capacity = new_capacity;
    }
    out->data[out->size++] = byte;
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
    return strcmp(m, "BRA") == 0 || strcmp(m, "BSR") == 0 ||
           strcmp(m, "JMP") == 0 || strcmp(m, "JSR") == 0 ||
           bcc_x86_cc(m) >= 0;
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
// and A0-A7 can't all be distinct. A1-A7 have no x86 register at all: they
// live in the emulated-register block (see "A1-A7" below), and only get one
// while emit_x86_two_operand() has a scratch register standing in for them
// (g_subst holds that register's code + 1, 0 = none). Otherwise -1.
static int g_subst[16];

static int x86_reg_code(int reg) {
    if (reg >= 0 && reg <= 7) return reg;
    if (reg == 8) return 6;  // A0 -> ESI
    if (reg >= 9 && reg <= 15) return g_subst[reg] - 1;
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

// ---- MOVE with address-register memory operands ---------------------------
// Modes: (An), (An)+ (post-increment), -(An) (pre-decrement). The pointer is
// A0, which lives in ESI on this target, or A1-A7, which are copied into a
// scratch register around the instruction (see "A1-A7" below). The
// increment/decrement is the operand size: 1, 2 or 4 bytes.
//
//   load   MOVE.s <mem>, Dn   : 8A /r | 66 8B /r | 8B /r      (mov d,[esi])
//          MOVEA.s <mem>, An  : 0F BF /r (.w, sign-extends) | 8B /r (.l)
//   store  MOVE.s Dn, <mem>   : 88 /r | 66 89 /r | 89 /r      (mov [esi],s)
//          MOVE.s #i, <mem>   : C6 /0 ib | 66 C7 /0 iw | C7 /0 id
//   mem to mem goes through a scratch register (EAX..EBX, saved with
//   push/pop), since x86 has no memory-to-memory mov.
// ModRM is 00 reg 110 ([esi]). The pointer update is "lea esi,[esi+-n]"
// (8D /r, disp8), which changes no flags. Flags are the 68K MOVE flags (N,Z
// from the data, V=C=0, X untouched), as for register MOVE: "test r,r" of the
// value moved, or, for an immediate, "cmp [mem],0" (CF=OF=0, SF/ZF from the
// stored value). MOVEA sets no flags.
//
// The 68K evaluates the source operand (with its increment/decrement) before
// the destination's, and the sequence below does the same, so e.g.
// MOVE.W (A0)+,(A0)+ copies a word to the next word. "MOVEA.s (A0)+,A0"
// ends up with the loaded value, because the register write comes after the
// increment, so the increment is skipped.
#define ERR_MEM_PTR "this address register can't be used as a memory pointer on this target"
#define ERR_MEM_ALIAS "D6 shares a register with A0 on this target, so it can't be used with an A0 memory operand yet"
#define ERR_MEM_SAMEREG "MOVE An,(An)+ and MOVE An,-(An) with the same register aren't supported (what gets stored isn't something I could confirm, and it may differ between 68K models)"

static int op_bytes(OpSize s) { return s == SIZE_B ? 1 : s == SIZE_W ? 2 : 4; }

// Pointer registers whose [reg] encoding needs no SIB byte or displacement.
static bool plain_base(int base) { return base >= 0 && base != 4 && base != 5; }

static void write_modrm_mem(OutputBuffer *out, int reg_field, int base) {
    buffer_write(out, (uint8_t)((reg_field << 3) | base));       // mod = 00
}

// lea base,[base+delta]
static void write_ptr_adjust(OutputBuffer *out, int base, int delta) {
    buffer_write(out, 0x8D);
    buffer_write(out, (uint8_t)(0x40 | (base << 3) | base));
    buffer_write(out, (uint8_t)(int8_t)delta);
}

static void mem_pre(OutputBuffer *out, OperandType mode, int base, int n) {
    if (mode == OPERAND_PREDEC) write_ptr_adjust(out, base, -n);
}

static void mem_post(OutputBuffer *out, OperandType mode, int base, int n) {
    if (mode == OPERAND_POSTINC) write_ptr_adjust(out, base, n);
}

// test r,r at the operation size (sets SF/ZF, clears CF/OF)
static void write_test(OutputBuffer *out, int r, OpSize size) {
    if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
    buffer_write(out, size == SIZE_B ? 0x84 : 0x85);
    buffer_write(out, (uint8_t)(0xC0 | (r << 3) | r));
}

// ---- byte order ------------------------------------------------------
// 68K memory is big-endian but x86 is little-endian, so a word or long moving
// between a register and memory is byte-swapped (bytes need nothing). That
// keeps the bytes in memory, and in dc.w / dc.l data, laid out as on a real
// 68K, while registers always hold the plain value.
//
// write_swap() is for a scratch register T = EAX..EBX and changes no flags
// (bswap, or xchg of the low and high byte halves). write_swap_any() works
// on any register but a word swap (rol) changes flags, so it is only for
// places where a "test" follows. EBX/ECX/EDX/EAX are the only registers with
// byte halves, which is why scratch registers come from that set.
#define ERR_ESP_SWAP \
    "D4 maps to ESP on this target, so it can't be the register operand of a word/long memory operation with AND/OR/EOR"

static void write_swap(OutputBuffer *out, int t, OpSize size) {
    if (size == SIZE_L) {
        buffer_write(out, 0x0F); buffer_write(out, (uint8_t)(0xC8 + t));      // bswap t
    } else if (size == SIZE_W) {
        buffer_write(out, 0x86); buffer_write(out, (uint8_t)(0xC0 | (t << 3) | (t + 4)));  // xchg t8,t8h
    }
}

static void write_swap_any(OutputBuffer *out, int r, OpSize size) {
    if (size == SIZE_L) {
        buffer_write(out, 0x0F); buffer_write(out, (uint8_t)(0xC8 + r));      // bswap r
    } else if (size == SIZE_W) {
        buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, 0xC1); buffer_write(out, (uint8_t)(0xC0 | r));      // rol r16,8
        buffer_write(out, 0x08);
    }
}

// A scratch register in EAX..EBX that isn't in `avoid` (a bit per x86
// register number), or -1.
static int pick_scratch(unsigned avoid) {
    for (int t = 0; t < 4; t++)
        if (!(avoid & (1u << t))) return t;
    return -1;
}
#define ERR_NO_SCRATCH \
    "no free scratch register for the byte swap of this memory operation"

static void write_push(OutputBuffer *out, int t) { buffer_write(out, (uint8_t)(0x50 + t)); }
static void write_pop(OutputBuffer *out, int t)  { buffer_write(out, (uint8_t)(0x58 + t)); }

// mov t,s  (t is already pushed, so a source of ESP is read through lea)
static void write_copy_reg(OutputBuffer *out, int t, int s) {
    if (s == 4) {                                    // lea t,[esp+4]
        buffer_write(out, 0x8D);
        buffer_write(out, (uint8_t)(0x44 | (t << 3)));
        buffer_write(out, 0x24); buffer_write(out, 0x04);
    } else {
        buffer_write(out, 0x89);
        buffer_write(out, (uint8_t)(0xC0 | (s << 3) | t));
    }
}

// mov t,imm at the operation size (.w writes only the low 16 bits)
static void write_mov_imm(OutputBuffer *out, int t, uint32_t v, OpSize size) {
    if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
    buffer_write(out, (uint8_t)(0xB8 + t));
    write_le(out, v, size == SIZE_W ? 2 : 4);
}

// mov r,[base] (load) at the operation size
static void write_load(OutputBuffer *out, int r, int base, OpSize size) {
    if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
    buffer_write(out, size == SIZE_B ? 0x8A : 0x8B);
    write_modrm_mem(out, r, base);
}

// mov [base],r (store) at the operation size
static void write_store(OutputBuffer *out, int r, int base, OpSize size) {
    if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
    buffer_write(out, size == SIZE_B ? 0x88 : 0x89);
    write_modrm_mem(out, r, base);
}

static bool emit_x86_move_mem(const Operand *operands, OpSize size, OutputBuffer *out) {
    const Operand *src = &operands[0], *dst = &operands[1];
    bool src_mem = operand_is_memory(src->type);
    bool dst_mem = operand_is_memory(dst->type);
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    int n = op_bytes(size);

    int sbase = -1, dbase = -1;
    if (src_mem) {
        sbase = x86_reg_code(src->value.reg);
        if (!plain_base(sbase)) return fail(ERR_MEM_PTR);
    }
    if (dst_mem) {
        dbase = x86_reg_code(dst->value.reg);
        if (!plain_base(dbase)) return fail(ERR_MEM_PTR);
    }

    // memory -> register
    if (src_mem && !dst_mem) {
        if (dst->type != OPERAND_REGISTER) return false;
        bool to_addr = dst->value.reg >= 8;
        int d = x86_reg_code(dst->value.reg);
        if (d < 0) return false;
        if (to_addr && size == SIZE_B)
            return fail("MOVE to an address register can't be byte-sized (MOVEA has no .b)");
        if (!to_addr && d == sbase) return fail(ERR_MEM_ALIAS);
        if (!to_addr && size == SIZE_B && d > 3) return fail(ERR_BYTE_REG);

        mem_pre(out, src->type, sbase, n);
        if (to_addr && size == SIZE_W) {                 // MOVEA.W: swap, then sign-extend
            int t = pick_scratch((1u << sbase) | (1u << d));
            if (t < 0) return fail(ERR_NO_SCRATCH);
            write_push(out, t);
            write_load(out, t, sbase, SIZE_W);
            write_swap(out, t, SIZE_W);                  // flags untouched, as MOVEA must
            buffer_write(out, 0x0F); buffer_write(out, 0xBF);          // movsx d,t16
            buffer_write(out, (uint8_t)(0xC0 | (d << 3) | t));
            write_pop(out, t);
        } else {
            OpSize load_size = to_addr ? SIZE_L : size;
            write_load(out, d, sbase, load_size);
            write_swap_any(out, d, load_size);           // a "test" (MOVE) or bswap (MOVEA) follows
        }
        if (!to_addr) write_test(out, d, size);
        // Writing the register after the increment makes the increment moot.
        if (!(to_addr && d == sbase)) mem_post(out, src->type, sbase, n);
        return true;
    }

    // register / immediate -> memory
    if (!src_mem && dst_mem) {
        if (src->type == OPERAND_IMMEDIATE) {
            if (!imm_fits(src->value.imm, size))
                return fail("immediate value doesn't fit the operation size");
            uint32_t v = (uint32_t)src->value.imm;
            mem_pre(out, dst->type, dbase, n);
            if (size == SIZE_B) {
                buffer_write(out, 0xC6);
                write_modrm_mem(out, 0, dbase);
                write_le(out, v, n);
                buffer_write(out, 0x80);                                // cmp [mem],0
                write_modrm_mem(out, 7, dbase);
                buffer_write(out, 0x00);
            } else {
                // Word/long: N and Z come from the plain value, then it is
                // stored byte-swapped.
                int t = pick_scratch(1u << dbase);
                if (t < 0) return fail(ERR_NO_SCRATCH);
                write_push(out, t);
                write_mov_imm(out, t, v, size);
                write_test(out, t, size);
                write_swap(out, t, size);
                write_store(out, t, dbase, size);
                write_pop(out, t);
            }
            mem_post(out, dst->type, dbase, n);
            return true;
        }
        if (src->type != OPERAND_REGISTER) return false;
        bool from_addr = src->value.reg >= 8;
        int s = x86_reg_code(src->value.reg);
        if (s < 0) return false;
        if (from_addr && size == SIZE_B)
            return fail("byte-sized MOVE can't use an address register as the source");
        if (from_addr && s == dbase && dst->type != OPERAND_IND)
            return fail(ERR_MEM_SAMEREG);
        if (!from_addr && s == dbase) return fail(ERR_MEM_ALIAS);
        if (size == SIZE_B && s > 3) return fail(ERR_BYTE_REG);

        mem_pre(out, dst->type, dbase, n);
        if (size == SIZE_B) {
            write_store(out, s, dbase, size);
            write_test(out, s, size);
        } else {
            write_test(out, s, size);                    // N,Z of the plain value
            int t = pick_scratch((1u << dbase) | (s <= 3 ? 1u << s : 0));
            if (t < 0) return fail(ERR_NO_SCRATCH);
            write_push(out, t);
            write_copy_reg(out, t, s);
            write_swap(out, t, size);
            write_store(out, t, dbase, size);
            write_pop(out, t);
        }
        mem_post(out, dst->type, dbase, n);
        return true;
    }

    // memory -> memory, through a scratch register
    if (!src_mem || !dst_mem) return false;
    int t = pick_scratch((1u << sbase) | (1u << dbase));  // EAX, ECX, EDX or EBX
    if (t < 0) return fail(ERR_NO_SCRATCH);
    write_push(out, t);
    mem_pre(out, src->type, sbase, n);
    write_load(out, t, sbase, size);
    mem_post(out, src->type, sbase, n);
    write_swap(out, t, size);                            // plain value, for N and Z
    write_test(out, t, size);
    write_swap(out, t, size);                            // back to big-endian
    mem_pre(out, dst->type, dbase, n);
    write_store(out, t, dbase, size);
    mem_post(out, dst->type, dbase, n);
    write_pop(out, t);                                   // flags untouched
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
    if (operand_is_memory(operands[0].type) || operand_is_memory(operands[1].type))
        return emit_x86_move_mem(operands, size, out);
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

static int x86_logic_opcode(const char *mnemonic, bool byte, bool memory_to_reg) {
    int base;
    if (strcmp(mnemonic, "AND") == 0) base = 0x20;
    else if (strcmp(mnemonic, "OR") == 0) base = 0x08;
    else base = 0x30;
    return base + (byte ? 0 : 1) + (memory_to_reg ? 2 : 0);
}

static bool emit_x86_logic(const OpcodeEntry *entry, const Operand *operands,
                           OpSize size, OutputBuffer *out) {
    const Operand *src = &operands[0], *dst = &operands[1];
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (src->type == OPERAND_REGISTER && dst->type == OPERAND_REGISTER) {
        if (src->value.reg >= 8 || dst->value.reg >= 8)
            return fail("AND/OR/EOR register operations need data registers");
        int s = x86_reg_code(src->value.reg), d = x86_reg_code(dst->value.reg);
        if (size == SIZE_B && (s > 3 || d > 3)) return fail(ERR_BYTE_REG);
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, (uint8_t)x86_logic_opcode(entry->mnemonic, size == SIZE_B, false));
        buffer_write(out, (uint8_t)(0xC0 | (s << 3) | d));
        return true;
    }

    bool src_mem = operand_is_memory(src->type);
    bool dst_mem = operand_is_memory(dst->type);
    if (src_mem == dst_mem) return false;
    const Operand *regop = src_mem ? dst : src;
    if (regop->type != OPERAND_REGISTER || regop->value.reg >= 8)
        return fail("AND/OR/EOR memory operations need a data register");
    int r = x86_reg_code(regop->value.reg);
    if (size == SIZE_B && r > 3) return fail(ERR_BYTE_REG);
    const Operand *mem = src_mem ? src : dst;
    int base = x86_reg_code(mem->value.reg);
    if (!plain_base(base)) return fail(ERR_MEM_PTR);
    mem_pre(out, mem->type, base, op_bytes(size));
    if (size == SIZE_B) {
        buffer_write(out, (uint8_t)x86_logic_opcode(entry->mnemonic, true, src_mem));
        write_modrm_mem(out, r, base);
    } else {
        // Word/long: work on the plain value in a scratch register. The
        // logic op comes last (or just before the swap back, which changes
        // no flags), so N and Z are those of the result.
        if (r == 4) return fail(ERR_ESP_SWAP);
        int t = pick_scratch((1u << base) | (r <= 3 ? 1u << r : 0));
        if (t < 0) return fail(ERR_NO_SCRATCH);
        write_push(out, t);
        write_load(out, t, base, size);
        write_swap(out, t, size);
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, (uint8_t)x86_logic_opcode(entry->mnemonic, false, false));
        if (src_mem) {
            buffer_write(out, (uint8_t)(0xC0 | (t << 3) | r));      // r = r op t
        } else {
            buffer_write(out, (uint8_t)(0xC0 | (r << 3) | t));      // t = t op r
            write_swap(out, t, size);
            write_store(out, t, base, size);
        }
        write_pop(out, t);
    }
    mem_post(out, mem->type, base, op_bytes(size));
    return true;
}

static bool emit_x86_clrtst(const char *mnemonic, const Operand *operand,
                            OpSize size, OutputBuffer *out) {
    bool is_clr = strcmp(mnemonic, "CLR") == 0;
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (operand->type == OPERAND_REGISTER) {
        if (operand->value.reg >= 8)
            return fail(is_clr ? "CLR needs a data register or memory destination"
                               : "TST needs a data register or memory operand");
        int r = x86_reg_code(operand->value.reg);
        if (size == SIZE_B && r > 3) return fail(ERR_BYTE_REG);
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        if (is_clr) {
            buffer_write(out, size == SIZE_B ? 0x30 : 0x31);
            buffer_write(out, (uint8_t)(0xC0 | (r << 3) | r));
        } else {
            buffer_write(out, size == SIZE_B ? 0x84 : 0x85);
            buffer_write(out, (uint8_t)(0xC0 | (r << 3) | r));
        }
        return true;
    }
    if (!operand_is_memory(operand->type)) return false;
    int base = x86_reg_code(operand->value.reg);
    if (!plain_base(base)) return fail(ERR_MEM_PTR);
    int n = op_bytes(size);
    mem_pre(out, operand->type, base, n);
    if (is_clr) {
        // All-zero bytes read the same in either byte order.
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, size == SIZE_B ? 0xC6 : 0xC7);
        write_modrm_mem(out, 0, base);
        write_le(out, 0, n);
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, size == SIZE_B ? 0x80 : 0x83);              // cmp [mem],0
        write_modrm_mem(out, 7, base);
        buffer_write(out, 0);
    } else if (size == SIZE_B) {
        // CMP [mem],0 sets N,Z like 68K TST. (TEST [mem],0 would always yield Z=1.)
        buffer_write(out, 0x80);
        write_modrm_mem(out, 7, base);
        buffer_write(out, 0);
    } else {
        // Word/long: the sign bit is in the first byte in memory, so test the
        // plain value in a scratch register.
        int t = pick_scratch(1u << base);
        if (t < 0) return fail(ERR_NO_SCRATCH);
        write_push(out, t);
        write_load(out, t, base, size);
        write_swap_any(out, t, size);
        write_test(out, t, size);
        write_pop(out, t);
    }
    mem_post(out, operand->type, base, n);
    return true;
}

static bool emit_x86_quick(const Operand *operands, OpSize size, bool is_sub,
                           OutputBuffer *out) {
    const Operand *imm = &operands[0], *dst = &operands[1];
    int q = imm->value.imm;
    if (q < 1 || q > 8)
        return fail("ADDQ/SUBQ immediate must be in the range 1..8");
    OpSize requested = size;
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (dst->type == OPERAND_REGISTER && dst->value.reg >= 8) {
        if (requested == SIZE_B)
            return fail("ADDQ/SUBQ to an address register can't be byte-sized");
        int r = x86_reg_code(dst->value.reg);
        if (r < 0) return false;
        buffer_write(out, 0x8D);
        buffer_write(out, (uint8_t)(0x40 | (r << 3) | r));
        buffer_write(out, (uint8_t)(is_sub ? -q : q));
        return true;
    }
    if (operand_is_memory(dst->type)) {
        int base = x86_reg_code(dst->value.reg);
        if (!plain_base(base)) return fail(ERR_MEM_PTR);
        mem_pre(out, dst->type, base, op_bytes(size));
        if (size == SIZE_B) {
            buffer_write(out, 0x80);
            write_modrm_mem(out, is_sub ? 5 : 0, base);
            buffer_write(out, (uint8_t)q);
        } else {
            // Word/long: add/sub on the plain value in a scratch register
            // (the carry has to ripple the 68K way), then store it swapped.
            int t = pick_scratch(1u << base);
            if (t < 0) return fail(ERR_NO_SCRATCH);
            write_push(out, t);
            write_load(out, t, base, size);
            write_swap(out, t, size);
            if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
            buffer_write(out, 0x83);
            buffer_write(out, (uint8_t)((is_sub ? 0xE8 : 0xC0) | t));
            buffer_write(out, (uint8_t)q);
            write_swap(out, t, size);                    // flags untouched
            write_store(out, t, base, size);
            write_pop(out, t);
        }
        mem_post(out, dst->type, base, op_bytes(size));
        return true;
    }
    Operand adjusted[2] = { *imm, *dst };
    adjusted[0].value.imm = q;
    return emit_x86_addsub(adjusted, size, is_sub, out);
}

static bool emit_x86_moveq(const Operand *operands, OutputBuffer *out) {
    const Operand *imm = &operands[0], *dst = &operands[1];
    if (imm->type != OPERAND_IMMEDIATE || dst->type != OPERAND_REGISTER ||
        dst->value.reg >= 8)
        return fail("MOVEQ requires an immediate and a data register");
    if (imm->value.imm < -128 || imm->value.imm > 127)
        return fail("MOVEQ immediate must fit in a signed byte");
    int d = x86_reg_code(dst->value.reg);
    if (d < 0) return false;
    buffer_write(out, (uint8_t)(0xB8 + d));
    write_le(out, (uint32_t)(int32_t)(int8_t)imm->value.imm, 4);
    write_test(out, d, SIZE_L);
    return true;
}

static bool emit_x86_jump(const char *mnemonic, const Operand *operand,
                          OutputBuffer *out) {
    bool call = strcmp(mnemonic, "JSR") == 0;
    if (operand->type == OPERAND_IMMEDIATE) {
        int32_t rel = g_final_pass
            ? (int32_t)((uint32_t)operand->value.imm - (g_pc + 5))
            : 0;
        buffer_write(out, call ? 0xE8 : 0xE9);
        write_le(out, (uint32_t)rel, 4);
        return true;
    }
    if (operand->type != OPERAND_IND)
        return fail("JMP/JSR support labels and (An) indirect targets");
    if (operand->value.reg >= 9 && operand->value.reg <= 15) {
        buffer_write(out, 0xFF);
        buffer_write(out, call ? 0x15 : 0x25); // indirect through absolute slot
        write_le(out, g_regfile_base + 4u * (uint32_t)operand->value.reg, 4);
        return true;
    }
    int r = x86_reg_code(operand->value.reg);
    if (!plain_base(r)) return fail(ERR_MEM_PTR);
    buffer_write(out, 0xFF);
    write_modrm_mem(out, call ? 2 : 4, r);
    return true;
}

static bool emit_x86_dbra(const Operand *operands, OutputBuffer *out) {
    const Operand *reg = &operands[0], *target = &operands[1];
    if (reg->type != OPERAND_REGISTER || reg->value.reg >= 8 ||
        target->type != OPERAND_IMMEDIATE)
        return fail("DBRA requires a data register and a label");
    int r = x86_reg_code(reg->value.reg);
    if (r == 4)
        return fail("DBRA can't use D4 on this target");
    int32_t rel = g_final_pass
        ? (int32_t)((uint32_t)target->value.imm - (g_pc + 15))
        : 0;
    buffer_write(out, 0x9C);                                  // pushfd
    buffer_write(out, 0x66);
    buffer_write(out, (uint8_t)(0x48 + r));                   // dec Dn.w
    buffer_write(out, 0x66);
    buffer_write(out, 0x83);
    buffer_write(out, (uint8_t)(0xF8 | r));
    buffer_write(out, 0xFF);                                  // cmp Dn.w,-1
    buffer_write(out, 0x74);
    buffer_write(out, 0x06);                                  // skip popfd+jmp at -1
    buffer_write(out, 0x9D);                                  // popfd
    buffer_write(out, 0xE9);
    write_le(out, (uint32_t)rel, 4);
    buffer_write(out, 0x9D);                                  // popfd, no branch
    return true;
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

// ---- A1-A7: address registers kept in the emulated-register block ---------
// D0-D7 already use all eight x86 registers (A0 shares ESI with D6), so
// A1-A7 live in memory: register number r (Dn = 0..7, An = 8..15) has the
// 4-byte slot at regfile_base + 4*r, i.e. A1 = base+36 ... A7 = base+60.
// The slot is addressed with an absolute 32-bit displacement, so the
// instruction length never depends on the base address (which is only
// known after pass 1).
//
// The per-instruction encoders above only know x86 registers, so an
// instruction that names A1-A7 (as a register, or as the pointer of
// (An), (An)+ or -(An)) is wrapped, with a scratch register T standing in
// for each such An (T is one of EAX, ECX, EDX, EBX that the instruction
// doesn't use itself):
//
//   push T ; mov T,[slot]          save T, load An (skipped when the
//                                  instruction only writes An)
//   <the instruction, with T as An>
//   mov [slot],T ; pop T           store An back if it changed, restore T
//
// push, pop and mov change no flags, so the flags the instruction sets are
// the ones the program sees. An changes when it is a written destination
// (MOVE/MOVEA/LEA) or the pointer of a pre-decrement/post-increment mode;
// the encoders already update the scratch copy in those cases. Up to two
// different A1-A7 can appear in one instruction (e.g. MOVE.B (A1)+,(A2)+).
//
// D4 is ESP on this target and the wrapper uses the stack, so D4 can't be
// combined with A1-A7 yet.
#define ERR_SPILL_ESP "D4 maps to ESP on this target, so it can't be combined with A1-A7 yet"

static bool is_reg_operand(const Operand *op) {
    return op->type == OPERAND_REGISTER || operand_is_memory(op->type);
}

static bool is_spilled(const Operand *op) {
    return is_reg_operand(op) && op->value.reg >= 9 && op->value.reg <= 15;
}

static bool is_move_or_lea(const char *m) {
    return strcmp(m, "MOVE") == 0 || strcmp(m, "LEA") == 0;
}

static bool x86_writes_destination(const char *m) {
    return is_move_or_lea(m) || strcmp(m, "ADD") == 0 ||
           strcmp(m, "SUB") == 0 || strcmp(m, "CLR") == 0 ||
           strcmp(m, "AND") == 0 || strcmp(m, "OR") == 0 ||
           strcmp(m, "EOR") == 0 || strcmp(m, "ADDQ") == 0 ||
           strcmp(m, "SUBQ") == 0;
}

static bool spill_capable(const char *m) {
    return is_move_or_lea(m) || strcmp(m, "ADD") == 0 || strcmp(m, "SUB") == 0 ||
           strcmp(m, "CMP") == 0 || strcmp(m, "CMPA") == 0 || strcmp(m, "CMPI") == 0 ||
           strcmp(m, "CLR") == 0 || strcmp(m, "TST") == 0 ||
           strcmp(m, "AND") == 0 || strcmp(m, "OR") == 0 ||
           strcmp(m, "EOR") == 0 || strcmp(m, "ADDQ") == 0 ||
           strcmp(m, "SUBQ") == 0;
}

// mov t,[slot] (opcode 8B) or mov [slot],t (opcode 89), ModRM = 00 ttt 101
static void write_slot_access(OutputBuffer *out, uint8_t opcode, int t, int reg) {
    buffer_write(out, opcode);
    buffer_write(out, (uint8_t)((t << 3) | 0x05));
    write_le(out, g_regfile_base + 4u * (uint32_t)reg, 4);
}

// Runs the encoder for a two-operand x86 mnemonic: 1 = encoded, 0 = failed,
// -1 = not one of the mnemonics handled here.
static int x86_dispatch2(const OpcodeEntry *entry, Operand *operands, OpSize size,
                         OutputBuffer *out) {
    const char *m = entry->mnemonic;
    if (strcmp(m, "MOVE") == 0) return emit_x86_move_imm(operands, size, out) ? 1 : 0;
    if (strcmp(m, "MOVEQ") == 0) return emit_x86_moveq(operands, out) ? 1 : 0;
    if (strcmp(m, "LEA") == 0)  return emit_x86_lea(operands, out) ? 1 : 0;
    if (strcmp(m, "ADD") == 0)  return emit_x86_addsub(operands, size, false, out) ? 1 : 0;
    if (strcmp(m, "SUB") == 0)  return emit_x86_addsub(operands, size, true, out) ? 1 : 0;
    if (strcmp(m, "MULU") == 0) return emit_x86_mul(operands, size, false, out) ? 1 : 0;
    if (strcmp(m, "MULS") == 0) return emit_x86_mul(operands, size, true, out) ? 1 : 0;
    if (strcmp(m, "DIVU") == 0) return emit_x86_div(operands, size, false, out) ? 1 : 0;
    if (strcmp(m, "DIVS") == 0) return emit_x86_div(operands, size, true, out) ? 1 : 0;
    if (strcmp(m, "CMP") == 0)  return emit_x86_cmp(operands, size, CMP_PLAIN, out) ? 1 : 0;
    if (strcmp(m, "CMPA") == 0) return emit_x86_cmp(operands, size, CMP_ADDR, out) ? 1 : 0;
    if (strcmp(m, "CMPI") == 0) return emit_x86_cmp(operands, size, CMP_IMM, out) ? 1 : 0;
    if (strcmp(m, "AND") == 0 || strcmp(m, "OR") == 0 || strcmp(m, "EOR") == 0)
        return emit_x86_logic(entry, operands, size, out) ? 1 : 0;
    if (strcmp(m, "ADDQ") == 0)
        return emit_x86_quick(operands, size, false, out) ? 1 : 0;
    if (strcmp(m, "SUBQ") == 0)
        return emit_x86_quick(operands, size, true, out) ? 1 : 0;
    if (strcmp(m, "DBRA") == 0) return emit_x86_dbra(operands, out) ? 1 : 0;
    return -1;
}

static int x86_dispatch1(const OpcodeEntry *entry, Operand *operand, OpSize size,
                         OutputBuffer *out) {
    if (strcmp(entry->mnemonic, "CLR") == 0 || strcmp(entry->mnemonic, "TST") == 0)
        return emit_x86_clrtst(entry->mnemonic, operand, size, out) ? 1 : 0;
    if (strcmp(entry->mnemonic, "JMP") == 0 || strcmp(entry->mnemonic, "JSR") == 0)
        return emit_x86_jump(entry->mnemonic, operand, out) ? 1 : 0;
    return -1;
}

// Same return values as x86_dispatch2, plus the A1-A7 wrapper described above.
static int emit_x86_two_operand(const OpcodeEntry *entry, Operand *operands, OpSize size,
                                OutputBuffer *out) {
    int regs[2], n = 0;
    if (spill_capable(entry->mnemonic)) {
        for (int i = 0; i < 2; i++) {
            if (!is_spilled(&operands[i])) continue;
            if (n == 1 && regs[0] == operands[i].value.reg) continue;   // same An twice
            regs[n++] = operands[i].value.reg;
        }
    }
    if (n == 0) return x86_dispatch2(entry, operands, size, out);

    // Scratch registers must not collide with the x86 registers the
    // instruction uses directly (D0-D7, A0).
    bool used[8] = { false };
    for (int i = 0; i < 2; i++) {
        if (!is_reg_operand(&operands[i]) || is_spilled(&operands[i])) continue;
        int c = x86_reg_code(operands[i].value.reg);
        if (c == X86_ESP) { fail(ERR_SPILL_ESP); return 0; }
        if (c >= 0) used[c] = true;
    }
    int scratch[2];
    for (int k = 0; k < n; k++) {
        int t = 0;
        while (t < 4 && used[t]) t++;
        if (t == 4) { fail("no free scratch register for A1-A7 in this instruction"); return 0; }
        used[t] = true;
        scratch[k] = t;
    }

    // Per register: is the old value needed, and does the instruction change it?
    bool load[2], store[2];
    bool dst_write = x86_writes_destination(entry->mnemonic) &&
                     operands[1].type == OPERAND_REGISTER;
    for (int k = 0; k < n; k++) {
        int r = regs[k];
        bool in_src = is_reg_operand(&operands[0]) && operands[0].value.reg == r;
        bool in_dst = is_reg_operand(&operands[1]) && operands[1].value.reg == r;
        store[k] = (dst_write && in_dst && operands[1].type == OPERAND_REGISTER) ||
                   (in_src && (operands[0].type == OPERAND_POSTINC || operands[0].type == OPERAND_PREDEC)) ||
                   (in_dst && (operands[1].type == OPERAND_POSTINC || operands[1].type == OPERAND_PREDEC));
        // Only MOVE/LEA overwrite the destination; ADDQ/SUBQ to An read it.
        bool overwrites = is_move_or_lea(entry->mnemonic);
        load[k] = !(overwrites && dst_write && in_dst && operands[1].type == OPERAND_REGISTER && !in_src);
    }

    size_t start = out->size;
    for (int k = 0; k < n; k++) {
        buffer_write(out, (uint8_t)(0x50 + scratch[k]));                // push T
        g_subst[regs[k]] = scratch[k] + 1;
    }
    for (int k = 0; k < n; k++)
        if (load[k]) write_slot_access(out, 0x8B, scratch[k], regs[k]); // mov T,[slot]

    int r = x86_dispatch2(entry, operands, size, out);

    for (int k = 0; k < n; k++) g_subst[regs[k]] = 0;
    if (r != 1) {
        out->size = start;                 // drop the partial sequence
        return 0;
    }
    for (int k = 0; k < n; k++)
        if (store[k]) write_slot_access(out, 0x89, scratch[k], regs[k]); // mov [slot],T
    for (int k = n - 1; k >= 0; k--)
        buffer_write(out, (uint8_t)(0x58 + scratch[k]));                // pop T
    return 1;
}

static int emit_x86_one_operand(const OpcodeEntry *entry, Operand *operand,
                                OpSize size, OutputBuffer *out) {
    if (!spill_capable(entry->mnemonic) || !is_spilled(operand))
        return x86_dispatch1(entry, operand, size, out);
    int reg = operand->value.reg;
    int scratch = 0;
    size_t start = out->size;
    buffer_write(out, (uint8_t)(0x50 + scratch));
    g_subst[reg] = scratch + 1;
    write_slot_access(out, 0x8B, scratch, reg);
    int r = x86_dispatch1(entry, operand, size, out);
    g_subst[reg] = 0;
    if (r != 1) {
        out->size = start;
        return 0;
    }
    write_slot_access(out, 0x89, scratch, reg);
    buffer_write(out, (uint8_t)(0x58 + scratch));
    return 1;
}

static void arm_write_word(OutputBuffer *out, uint32_t word) {
    buffer_write(out, (uint8_t)word);
    buffer_write(out, (uint8_t)(word >> 8));
    buffer_write(out, (uint8_t)(word >> 16));
    buffer_write(out, (uint8_t)(word >> 24));
}

static bool arm_encode_imm(uint32_t value, uint32_t *operand2) {
    for (uint32_t rotate = 0; rotate < 16; rotate++) {
        uint32_t amount = rotate * 2;
        uint32_t rotated = amount == 0 ? value
            : (value << amount) | (value >> (32 - amount));
        if ((rotated & ~0xFFu) == 0) {
            *operand2 = (rotate << 8) | rotated;
            return true;
        }
    }
    return false;
}

static uint32_t arm_dp_reg_word(unsigned opcode, bool set_flags, unsigned rd,
                                unsigned rn, unsigned rm) {
    return 0xE0000000u | (opcode << 21) | (set_flags ? 1u << 20 : 0) |
           (rn << 16) | (rd << 12) | rm;
}

static bool arm_dp_imm(OutputBuffer *out, unsigned opcode, bool set_flags,
                      unsigned rd, unsigned rn, uint32_t immediate) {
    uint32_t operand2;
    if (!arm_encode_imm(immediate, &operand2))
        return false;
    arm_write_word(out, 0xE2000000u | (opcode << 21) |
                   (set_flags ? 1u << 20 : 0) | (rn << 16) |
                   (rd << 12) | (1u << 25) | operand2);
    return true;
}

static bool arm_update_flags(OutputBuffer *out, unsigned opcode,
                             uint32_t mask) {
    arm_write_word(out, 0xE10FC000u); // mrs r12,cpsr
    if (!arm_dp_imm(out, opcode, false, 12, 12, mask))
        return fail("internal ARM error: flag mask is not encodable");
    arm_write_word(out, 0xE128F00Cu); // msr cpsr_f,r12
    return true;
}

static bool arm_clear_cv_flags(OutputBuffer *out) {
    return arm_update_flags(out, 14, 0x30000000u); // bic C and V
}

static bool arm_invert_carry_flag(OutputBuffer *out) {
    return arm_update_flags(out, 1, 0x20000000u); // eor C
}

static void arm_slot_access(OutputBuffer *out, bool load, unsigned physical,
                            unsigned virtual_reg) {
    uint32_t address = codegen_regfile_base() + 4u * virtual_reg;
    arm_write_word(out, 0xE59FC004u); // ldr r12, [pc, #4]
    arm_write_word(out, (load ? 0xE59C0000u : 0xE58C0000u) |
                   (physical << 12));
    arm_write_word(out, 0xEA000000u); // skip the literal
    arm_write_word(out, address);
}

static void arm_load_vreg(OutputBuffer *out, unsigned physical,
                          unsigned virtual_reg) {
    arm_slot_access(out, true, physical, virtual_reg);
}

static void arm_store_vreg(OutputBuffer *out, unsigned physical,
                           unsigned virtual_reg) {
    arm_slot_access(out, false, physical, virtual_reg);
}

static void arm_load_literal(OutputBuffer *out, unsigned physical,
                             uint32_t value) {
    arm_write_word(out, 0xE59F0000u | (physical << 12));
    arm_write_word(out, 0xEA000000u);
    arm_write_word(out, value);
}

static int arm_bytes(OpSize size) {
    if (size == SIZE_B) return 1;
    if (size == SIZE_W) return 2;
    return 4;
}

static bool arm_pointer_load(OutputBuffer *out, const Operand *memory,
                             unsigned physical) {
    if (!operand_is_memory(memory->type))
        return false;
    arm_load_vreg(out, physical, (unsigned)memory->value.reg);
    return true;
}

static void arm_pointer_adjust(OutputBuffer *out, unsigned base, int bytes) {
    unsigned opcode = bytes < 0 ? 2 : 4;
    uint32_t amount = (uint32_t)(bytes < 0 ? -bytes : bytes);
    if (!arm_dp_imm(out, opcode, false, base, base, amount))
        fail("internal ARM error: pointer adjustment is not encodable");
}

// rev / rev16 rd,rd (ARMv6+): the byte swaps between a register and 68K
// (big-endian) memory. They change no flags.
static void arm_swap(OutputBuffer *out, unsigned reg, OpSize size) {
    if (size == SIZE_L)
        arm_write_word(out, 0xE6BF0F30u | (reg << 12) | reg);      // rev
    else if (size == SIZE_W)
        arm_write_word(out, 0xE6BF0FB0u | (reg << 12) | reg);      // rev16
}

// Loads leave the plain value in `data`; stores leave `data` unchanged
// (it is swapped for the store and swapped back).
static void arm_mem_transfer(OutputBuffer *out, bool load, unsigned data,
                             unsigned base, OpSize size) {
    if (!load) arm_swap(out, data, size);
    if (size == SIZE_W) {
        arm_write_word(out, (load ? 0xE1D000B0u : 0xE1C000B0u) |
                       (base << 16) | (data << 12));
    } else {
        uint32_t opcode = size == SIZE_B
            ? (load ? 0xE5D00000u : 0xE5C00000u)
            : (load ? 0xE5900000u : 0xE5800000u);
        arm_write_word(out, opcode | (base << 16) | (data << 12));
    }
    arm_swap(out, data, size);       // load: make it the plain value; store: restore
}

static void arm_mem_begin(OutputBuffer *out, const Operand *memory,
                          unsigned base, OpSize size) {
    arm_pointer_load(out, memory, base);
    if (memory->type == OPERAND_PREDEC)
        arm_pointer_adjust(out, base, -arm_bytes(size));
}

static void arm_mem_finish(OutputBuffer *out, const Operand *memory,
                           unsigned base, OpSize size) {
    if (memory->type == OPERAND_POSTINC)
        arm_pointer_adjust(out, base, arm_bytes(size));
    if (memory->type == OPERAND_PREDEC || memory->type == OPERAND_POSTINC)
        arm_store_vreg(out, base, (unsigned)memory->value.reg);
}

static void arm_test_value(OutputBuffer *out, unsigned value, OpSize size) {
    if (size == SIZE_L) {
        arm_write_word(out, arm_dp_reg_word(8, true, 0, value, value));
        return;
    }
    unsigned shift = size == SIZE_B ? 24 : 16;
    arm_write_word(out, 0xE1A00000u | (12u << 12) | (value << 0) |
                   (shift << 7) | (1u << 20));        // movs r12,value,lsl #shift
}

static void arm_mov_shift(OutputBuffer *out, unsigned rd, unsigned rm,
                          bool right, unsigned amount) {
    uint32_t shift_type = right ? 1u : 0u;
    uint32_t encoded_amount = amount == 32 ? 0 : amount;
    arm_write_word(out, 0xE1A00000u | (rd << 12) | (encoded_amount << 7) |
                   (shift_type << 5) | rm);
}

static bool arm_write_sized_result(OutputBuffer *out, unsigned original,
                                   unsigned rhs, unsigned result,
                                   unsigned opcode, OpSize size) {
    if (size == SIZE_L) {
        arm_write_word(out, arm_dp_reg_word(opcode, true, original, original, rhs));
        if (opcode == 2)
            return arm_invert_carry_flag(out);
        if (opcode == 0 || opcode == 1 || opcode == 12)
            return arm_clear_cv_flags(out);
        return true;
    }
    unsigned shift = size == SIZE_B ? 24 : 16;
    arm_mov_shift(out, result, original, false, shift);
    if (opcode == 2 || opcode == 4) {
        arm_mov_shift(out, 12, rhs, false, shift);
        arm_write_word(out, arm_dp_reg_word(opcode, true, result, result, 12));
        arm_write_word(out, 0xE10FC000u); // save arithmetic flags
        arm_mov_shift(out, result, result, true, shift);
        arm_mov_shift(out, original, original, true, 32 - shift);
        arm_mov_shift(out, original, original, false, shift);
        arm_write_word(out, arm_dp_reg_word(12, false, original, original, result));
        if (opcode == 2 &&
            !arm_dp_imm(out, 1, false, 12, 12, 0x20000000u))
            return fail("internal ARM error: carry mask is not encodable");
        arm_write_word(out, 0xE128F00Cu); // restore operation flags
        return true;
    }
    arm_mov_shift(out, result, result, true, 32 - shift);
    arm_mov_shift(out, 12, rhs, false, shift);
    arm_mov_shift(out, 12, 12, true, 32 - shift);
    arm_write_word(out, arm_dp_reg_word(opcode, false, result, result, 12));
    arm_mov_shift(out, result, result, false, shift);
    arm_mov_shift(out, result, result, true, 32 - shift);
    arm_mov_shift(out, original, original, true, 32 - shift);
    arm_mov_shift(out, original, original, false, shift);
    arm_write_word(out, arm_dp_reg_word(12, false, original, original, result));
    arm_test_value(out, result, size);
    return arm_clear_cv_flags(out);
}

static bool arm_emit_quick(const Operand *operands, OpSize size, bool is_sub,
                           OutputBuffer *out) {
    const Operand *immediate = &operands[0], *dst = &operands[1];
    int amount = immediate->value.imm;
    if (amount < 1 || amount > 8)
        return fail("ADDQ/SUBQ immediate must be in the range 1..8");
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (dst->type == OPERAND_REGISTER) {
        unsigned reg = (unsigned)dst->value.reg;
        if (reg >= 16) return false;
        if (reg >= 8) {
            if (size != SIZE_W && size != SIZE_L)
                return fail("ADDQ/SUBQ to an address register only supports .w or .l");
            arm_load_vreg(out, 0, reg);
            if (!arm_dp_imm(out, is_sub ? 2 : 4, false, 0, 0, (uint32_t)amount))
                return false;
            arm_store_vreg(out, 0, reg);
            return true;
        }
        arm_load_vreg(out, 0, reg);
        if (size == SIZE_L) {
            if (!arm_dp_imm(out, is_sub ? 2 : 4, true, 0, 0, (uint32_t)amount))
                return false;
        } else {
            if (!arm_dp_imm(out, 13, false, 1, 0, (uint32_t)amount))
                return false;
            arm_write_sized_result(out, 0, 1, 2, is_sub ? 2 : 4, size);
        }
        arm_store_vreg(out, 0, reg);
        return true;
    }
    if (!operand_is_memory(dst->type)) return false;
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    arm_mem_begin(out, dst, 1, size);
    arm_mem_transfer(out, true, 0, 1, size);
    if (!arm_dp_imm(out, 13, false, 2, 0, (uint32_t)amount))
        return false;
    arm_write_sized_result(out, 0, 2, 3, is_sub ? 2 : 4, size);
    arm_mem_transfer(out, false, 0, 1, size);
    arm_mem_finish(out, dst, 1, size);
    return true;
}

static bool arm_emit_clrtst(const char *mnemonic, const Operand *operand,
                            OpSize size, OutputBuffer *out) {
    bool clear = strcmp(mnemonic, "CLR") == 0;
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (operand->type == OPERAND_REGISTER) {
        unsigned reg = (unsigned)operand->value.reg;
        if (reg >= 8)
            return fail("CLR/TST accept data registers or address-register memory operands");
        arm_load_vreg(out, 0, reg);
        if (clear) {
            if (size == SIZE_L) {
                arm_write_word(out, 0xE3B00000u); // movs r0,#0
            } else {
                unsigned shift = size == SIZE_B ? 24 : 16;
                arm_mov_shift(out, 0, 0, true, 32 - shift);
                arm_mov_shift(out, 0, 0, false, shift);
                arm_test_value(out, 0, size);
            }
            if (!arm_clear_cv_flags(out)) return false;
            arm_store_vreg(out, 0, reg);
        } else {
            if (size == SIZE_L)
                arm_write_word(out, arm_dp_reg_word(8, true, 0, 0, 0));
            else
                arm_test_value(out, 0, size);
            if (!arm_clear_cv_flags(out)) return false;
        }
        return true;
    }
    if (!operand_is_memory(operand->type)) return false;
    arm_mem_begin(out, operand, 1, size);
    if (clear) {
        arm_write_word(out, 0xE3A00000u);
        arm_mem_transfer(out, false, 0, 1, size);
        arm_test_value(out, 0, size);
    } else {
        arm_mem_transfer(out, true, 0, 1, size);
        arm_test_value(out, 0, size);
    }
    if (!arm_clear_cv_flags(out)) return false;
    arm_mem_finish(out, operand, 1, size);
    return true;
}

static bool arm_emit_logic(const OpcodeEntry *entry, const Operand *operands,
                           OpSize size, OutputBuffer *out) {
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    unsigned op = strcmp(entry->mnemonic, "AND") == 0 ? 0
        : strcmp(entry->mnemonic, "EOR") == 0 ? 1 : 12;
    const Operand *src = &operands[0], *dst = &operands[1];
    bool src_mem = operand_is_memory(src->type);
    bool dst_mem = operand_is_memory(dst->type);
    if (src_mem && dst_mem) return false;
    unsigned destination = 0;
    if (!dst_mem) {
        if (dst->type != OPERAND_REGISTER || dst->value.reg >= 8)
            return fail("ARM logical operations require a data-register destination");
        destination = (unsigned)dst->value.reg;
    }
    unsigned source = 1;
    if (src_mem) {
        arm_mem_begin(out, src, 2, size);
        arm_mem_transfer(out, true, source, 2, size);
        arm_mem_finish(out, src, 2, size);
    } else if (src->type == OPERAND_REGISTER && src->value.reg < 8) {
        arm_load_vreg(out, source, (unsigned)src->value.reg);
    } else {
        return fail("ARM logical operations require data-register operands");
    }
    if (dst_mem) {
        arm_mem_begin(out, dst, 2, size);
        arm_mem_transfer(out, true, 0, 2, size);
        arm_write_sized_result(out, 0, source, 3, op, size);
        arm_mem_transfer(out, false, 0, 2, size);
        arm_mem_finish(out, dst, 2, size);
    } else {
        arm_load_vreg(out, 0, destination);
        arm_write_sized_result(out, 0, source, 3, op, size);
        arm_store_vreg(out, 0, destination);
    }
    return true;
}

static bool arm_emit_moveq(const Operand *operands, OutputBuffer *out) {
    int32_t value = operands[0].value.imm;
    unsigned reg = (unsigned)operands[1].value.reg;
    if (operands[0].type != OPERAND_IMMEDIATE ||
        operands[1].type != OPERAND_REGISTER || reg >= 8)
        return fail("MOVEQ requires an immediate and a data register");
    if (value < -128 || value > 127)
        return fail("MOVEQ immediate must fit in a signed byte");
    uint32_t extended = (uint32_t)value;
    arm_load_literal(out, 0, extended);
    arm_test_value(out, 0, SIZE_L);
    if (!arm_clear_cv_flags(out)) return false;
    arm_store_vreg(out, 0, reg);
    return true;
}

static bool arm_emit_move(const Operand *operands, OpSize size,
                          OutputBuffer *out) {
    if (operands[0].type != OPERAND_IMMEDIATE ||
        operands[1].type != OPERAND_REGISTER)
        return fail("ARM MOVE currently requires an immediate and a register");
    unsigned reg = (unsigned)operands[1].value.reg;
    if (reg >= 16) return false;
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (reg >= 8) {
        if (size == SIZE_B)
            return fail("MOVE to an address register can't be byte-sized");
        int32_t value = operands[0].value.imm;
        if (size == SIZE_W) {
            if (!imm_fits(value, SIZE_W))
                return fail("immediate value doesn't fit the operation size");
            value = (int32_t)(int16_t)value;
        }
        arm_load_literal(out, 0, (uint32_t)value);
        arm_store_vreg(out, 0, reg);
        return true;
    }
    if (!imm_fits(operands[0].value.imm, size))
        return fail("immediate value doesn't fit the operation size");
    uint32_t value = (uint32_t)operands[0].value.imm;
    if (size == SIZE_L) {
        arm_load_literal(out, 0, value);
        arm_test_value(out, 0, SIZE_L);
    } else {
        unsigned shift = size == SIZE_B ? 24 : 16;
        uint32_t mask = size == SIZE_B ? 0xFFu : 0xFFFFu;
        arm_load_vreg(out, 0, reg);
        arm_load_literal(out, 1, value & mask);
        arm_mov_shift(out, 0, 0, true, 32 - shift);
        arm_mov_shift(out, 0, 0, false, shift);
        arm_mov_shift(out, 1, 1, false, shift);
        arm_mov_shift(out, 1, 1, true, 32 - shift);
        arm_write_word(out, arm_dp_reg_word(12, false, 0, 0, 1));
        arm_test_value(out, 1, size);
    }
    if (!arm_clear_cv_flags(out)) return false;
    arm_store_vreg(out, 0, reg);
    return true;
}

static bool arm_emit_lea(const Operand *operands, OutputBuffer *out) {
    if (operands[0].type != OPERAND_IMMEDIATE ||
        operands[1].type != OPERAND_REGISTER || operands[1].value.reg < 8)
        return fail("LEA requires a label and an address register");
    arm_load_literal(out, 0, (uint32_t)operands[0].value.imm);
    arm_store_vreg(out, 0, (unsigned)operands[1].value.reg);
    return true;
}

static bool arm_emit_jump(const char *mnemonic, const Operand *operand,
                          OutputBuffer *out) {
    bool call = strcmp(mnemonic, "JSR") == 0;
    if (operand->type == OPERAND_IMMEDIATE) {
        if (call) arm_write_word(out, 0xE92D4000u); // push {lr}
        int64_t delta = g_final_pass
            ? (int64_t)(uint32_t)operand->value.imm -
                  (int64_t)(g_pc + (call ? 12 : 8))
            : 0;
        if ((delta & 3) != 0 || delta < -33554432 || delta > 33554428)
            return fail("ARM branch target is outside the encodable range");
        uint32_t imm24 = (uint32_t)(delta >> 2) & 0x00FFFFFFu;
        arm_write_word(out, (call ? 0xEB000000u : 0xEA000000u) | imm24);
        if (call) arm_write_word(out, 0xE8BD4000u); // pop {lr}
        return true;
    }
    if (operand->type != OPERAND_IND)
        return fail("ARM JMP/JSR support labels and (An) indirect targets");
    arm_load_vreg(out, 0, (unsigned)operand->value.reg);
    if (call) {
        arm_write_word(out, 0xE92D4000u); // push {lr}
        arm_write_word(out, 0xE12FFF30u); // blx r0
        arm_write_word(out, 0xE8BD4000u); // pop {lr}
    } else {
        arm_write_word(out, 0xE1A0F000u); // mov pc,r0
    }
    return true;
}

static bool arm_emit_dbra(const Operand *operands, OutputBuffer *out) {
    size_t start = out->size;
    unsigned reg = (unsigned)operands[0].value.reg;
    if (operands[0].type != OPERAND_REGISTER || reg >= 8 ||
        operands[1].type != OPERAND_IMMEDIATE)
        return fail("DBRA requires a data register and a label");
    arm_load_vreg(out, 0, reg);
    arm_write_word(out, 0xE10F3000u);             // mrs r3,cpsr
    arm_write_word(out, 0xE1A01820u);             // mov r1,r0,lsr #16
    arm_write_word(out, 0xE2400001u);             // sub r0,r0,#1
    arm_write_word(out, 0xE1A00800u);             // mov r0,r0,lsl #16
    arm_write_word(out, 0xE1A00820u);             // mov r0,r0,lsr #16
    arm_write_word(out, 0xE1800801u);             // orr r0,r0,r1,lsl #16
    arm_store_vreg(out, 0, reg);
    arm_write_word(out, 0xE1A02800u);             // mov r2,r0,lsl #16
    if (!arm_dp_imm(out, 11, true, 0, 2, 0x10000u))
        return false;                             // cmn r2,#0x10000
    arm_write_word(out, 0x0A000001u);             // beq over restore+branch
    arm_write_word(out, 0xE128F003u);             // msr cpsr_f,r3
    int64_t delta = g_final_pass
        ? (int64_t)(uint32_t)operands[1].value.imm -
              (int64_t)(g_pc + (uint32_t)(out->size - start) + 8)
        : 0;
    if ((delta & 3) != 0 || delta < -33554432 || delta > 33554428)
        return fail("ARM DBRA target is outside the encodable range");
    arm_write_word(out, 0xEA000000u |
                   ((uint32_t)(delta >> 2) & 0x00FFFFFFu));
    arm_write_word(out, 0xE128F003u);             // restore original flags
    return true;
}

static int emit_arm_instruction(const OpcodeEntry *entry, Operand *operands,
                                OpSize size, OutputBuffer *out) {
    const char *m = entry->mnemonic;
    if (strcmp(m, "MOVE") == 0) return arm_emit_move(operands, size, out) ? 1 : 0;
    if (strcmp(m, "LEA") == 0) return arm_emit_lea(operands, out) ? 1 : 0;
    if (strcmp(m, "MOVEQ") == 0) return arm_emit_moveq(operands, out) ? 1 : 0;
    if (strcmp(m, "CLR") == 0 || strcmp(m, "TST") == 0)
        return arm_emit_clrtst(m, &operands[0], size, out) ? 1 : 0;
    if (strcmp(m, "AND") == 0 || strcmp(m, "OR") == 0 ||
        strcmp(m, "EOR") == 0)
        return arm_emit_logic(entry, operands, size, out) ? 1 : 0;
    if (strcmp(m, "ADDQ") == 0)
        return arm_emit_quick(operands, size, false, out) ? 1 : 0;
    if (strcmp(m, "SUBQ") == 0)
        return arm_emit_quick(operands, size, true, out) ? 1 : 0;
    if (strcmp(m, "JMP") == 0 || strcmp(m, "JSR") == 0)
        return arm_emit_jump(m, &operands[0], out) ? 1 : 0;
    if (strcmp(m, "DBRA") == 0) return arm_emit_dbra(operands, out) ? 1 : 0;
    return -1;
}

// Emit full instruction
int emit_code(const OpcodeEntry *entry, Operand *operands, OpSize size,
              OutputBuffer *out) {
    if (!entry || !out) return -1;
    g_emit_error = NULL;

    const char *arch = opcodes_active_arch_name();
    if (arch && strcmp(arch, "arm") == 0) {
        int r = emit_arm_instruction(entry, operands, size, out);
        if (r >= 0) return r ? 0 : -1;
    }
    if (arch && strcmp(arch, "x86") == 0 && entry->operand_count == 2) {
        int r = emit_x86_two_operand(entry, operands, size, out);
        if (r >= 0) return r ? 0 : -1;
    }
    if (arch && strcmp(arch, "x86") == 0 && entry->operand_count == 1 &&
        is_branch_mnemonic(entry->mnemonic))
        return (strcmp(entry->mnemonic, "JMP") == 0 ||
                strcmp(entry->mnemonic, "JSR") == 0
                    ? emit_x86_one_operand(entry, &operands[0], size, out) == 1
                    : emit_x86_branch(entry, operands, size, out))
                   ? 0 : -1;
    if (arch && strcmp(arch, "x86") == 0 && entry->operand_count == 1) {
        int r = emit_x86_one_operand(entry, &operands[0], size, out);
        if (r >= 0) return r ? 0 : -1;
    }

    for (size_t i = entry->size; i > 0; i--) {
        buffer_write(out, (uint8_t)(entry->opcode >> ((i - 1) * 8)));
    }
    for (size_t i = 0; i < entry->operand_count; i++) {
        encode_operand(&operands[i], out);
    }
    return 0;
}