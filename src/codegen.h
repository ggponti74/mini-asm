#ifndef CODEGEN_H
#define CODEGEN_H

#include "opcodes.h"
#include "parser.h"

// Simple output buffer
typedef struct {
    uint8_t *data;
    size_t   size;
    size_t   capacity;
} OutputBuffer;

void buffer_write(OutputBuffer *out, uint8_t byte);

// Returns 0 on success, or -1 if the operands can't be encoded for the
// active architecture (e.g. a register with no mapping yet).
int emit_code(const OpcodeEntry *entry, Operand *operands, OpSize size,
              OutputBuffer *out);

// Tells codegen where the next instruction will sit (its absolute address)
// and whether labels are resolved yet (pass 2). Branches need this to turn
// an absolute target into a relative displacement; in pass 1 they only
// reserve the right number of bytes. Call before each emit_code().
void codegen_set_context(uint32_t pc, bool final_pass);

// Emulated-register block. The 68K has 16 registers (D0-D7, A0-A7) but x86
// has 8, so registers with no physical home live in a small zero-filled
// block in the program's own writable memory. The block sits right after
// the code (plus whatever the target's writer appends after it, e.g. the
// ELF epilogue), rounded up to a 4-byte boundary. Writers reserve
// REGFILE_SIZE bytes there; main() computes the address after pass 1 (when
// the code size is known) and hands it to codegen. Instruction lengths never
// depend on the address (it is always a 4-byte displacement), so pass 1 and
// pass 2 produce the same sizes.
#define REGFILE_SIZE 64
#define REGFILE_ALIGN_UP(n) (((n) + 3u) & ~3u)

void codegen_set_regfile(uint32_t base);
uint32_t codegen_regfile_base(void);

// After emit_code() returns -1: a human-readable reason, or NULL if there
// isn't a specific one.
const char *codegen_error(void);

#endif
