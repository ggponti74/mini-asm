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
    const Operand *imm = NULL, *reg = NULL;
    for (int i = 0; i < 2; i++) {
        if (operands[i].type == OPERAND_IMMEDIATE) imm = &operands[i];
        if (operands[i].type == OPERAND_REGISTER) reg = &operands[i];
    }
    if (!imm || !reg) return false;
    int r = x86_reg_code(reg->value.reg);
    if (r < 0) return false;

    bool is_addr = reg->value.reg >= 8;
    if (size == SIZE_UNSPEC) size = is_addr ? SIZE_L : DEFAULT_OPSIZE;
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

// x86 ADD, with 68K operand order (add src, dst  ->  dst += src), at the
// requested size (.b operates on the low byte only, .w on the low 16 bits,
// leaving the rest of the register untouched, as on the 68K):
//   add #imm, Dn : 80 /0 ib | 66 81 /0 iw | 81 /0 id   (ModRM = 11 000 ddd)
//   add Ds,  Dn  : 00 /r    | 66 01 /r    | 01 /r      (ModRM = 11 sss ddd)
// x86 ADD sets ZF/SF/CF/OF from the result at that size, which is what the
// 68K sets in Z/N/C/V, so no extra code is needed for the condition codes.
static bool emit_x86_add(const Operand *operands, OpSize size, OutputBuffer *out) {
    const Operand *src = &operands[0], *dst = &operands[1];
    if (dst->type != OPERAND_REGISTER) return false;
    // Like the real 68K, ADD targets a data register (A-register targets
    // would be ADDA, a different instruction).
    if (dst->value.reg > 7) return fail("ADD needs a data register (D0-D7) destination");
    int d = x86_reg_code(dst->value.reg);
    if (d < 0) return false;
    if (size == SIZE_UNSPEC) size = DEFAULT_OPSIZE;
    if (size == SIZE_B && d > 3) return fail(ERR_BYTE_REG);

    if (src->type == OPERAND_IMMEDIATE) {
        if (!imm_fits(src->value.imm, size))
            return fail("immediate value doesn't fit the operation size");
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, size == SIZE_B ? 0x80 : 0x81);
        buffer_write(out, (uint8_t)(0xC0 | d));
        write_le(out, (uint32_t)src->value.imm, size == SIZE_B ? 1 : size == SIZE_W ? 2 : 4);
        return true;
    }
    if (src->type == OPERAND_REGISTER) {
        if (size == SIZE_B && src->value.reg >= 8)
            return fail("byte-sized ADD can't use an address register as the source");
        int sr = x86_reg_code(src->value.reg);
        if (sr < 0) return false;
        if (size == SIZE_B && sr > 3) return fail(ERR_BYTE_REG);
        if (size == SIZE_W) buffer_write(out, X86_OPSIZE_PREFIX);
        buffer_write(out, size == SIZE_B ? 0x00 : 0x01);
        buffer_write(out, (uint8_t)(0xC0 | (sr << 3) | d));
        return true;
    }
    return false;
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
            return emit_x86_add(operands, size, out) ? 0 : -1;
    }

    for (size_t i = entry->size; i > 0; i--) {
        buffer_write(out, (uint8_t)(entry->opcode >> ((i - 1) * 8)));
    }
    for (size_t i = 0; i < entry->operand_count; i++) {
        encode_operand(&operands[i], out);
    }
    return 0;
}