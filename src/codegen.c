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

// x86's real "MOV r32, imm32" bakes the destination register into the
// low 3 bits of the opcode byte itself (0xB8 + reg) and takes a 4-byte
// little-endian immediate -- no ModRM byte at all. That doesn't fit the
// generic "opcode bytes, then each operand's bytes back to back" model
// below (which previously produced e.g. "89 00 00 00 00": a real x86
// opcode, 0x89 "MOV r/m32, r32", followed by garbage that the CPU reads
// as a ModRM byte -- decoding to "MOV [EAX], EAX", a write through
// whatever's in EAX, which is why this used to segfault immediately).
// So this specific (mnemonic, arch, operand shape) combination is
// special-cased here instead.
static bool emit_x86_move_imm32(const Operand *operands, OutputBuffer *out) {
    const Operand *imm = NULL, *reg = NULL;
    for (int i = 0; i < 2; i++) {
        if (operands[i].type == OPERAND_IMMEDIATE) imm = &operands[i];
        if (operands[i].type == OPERAND_REGISTER) reg = &operands[i];
    }
    if (!imm || !reg || reg->value.reg < 0 || reg->value.reg > 7) return false;

    buffer_write(out, (uint8_t)(0xB8 + reg->value.reg));
    uint32_t v = (uint32_t)imm->value.imm;
    buffer_write(out, (uint8_t)(v & 0xFF));
    buffer_write(out, (uint8_t)((v >> 8) & 0xFF));
    buffer_write(out, (uint8_t)((v >> 16) & 0xFF));
    buffer_write(out, (uint8_t)((v >> 24) & 0xFF));
    return true;
}

// Emit full instruction
void emit_code(const OpcodeEntry *entry, Operand *operands, OutputBuffer *out) {
    if (!entry || !out) return;

    const char *arch = opcodes_active_arch_name();
    if (arch && strcmp(arch, "x86") == 0 &&
        strcmp(entry->mnemonic, "MOVE") == 0 &&
        entry->operand_count == 2) {
        if (emit_x86_move_imm32(operands, out)) return;
        // Operands weren't the (immediate, register) shape this handles;
        // fall through to the generic path below.
    }

    // Write base opcode (entry->size bytes, most-significant byte first,
    // matching the literal byte order used in opcode_table[])
    for (size_t i = entry->size; i > 0; i--) {
        buffer_write(out, (uint8_t)(entry->opcode >> ((i - 1) * 8)));
    }

    // Write operands
    for (size_t i = 0; i < entry->operand_count; i++) {
        encode_operand(&operands[i], out);
    }
}
