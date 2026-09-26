#ifndef MINIASM_REGISTERS_H
#define MINIASM_REGISTERS_H

#include <stdint.h>

#define NUM_DATA_REGISTERS 8

/* Access width for a register read/write. Mirrors 68k-style sized access:
 * a byte/word write only replaces the register's low-order bits and leaves
 * the rest of the register untouched; a long write replaces all 32 bits. */
typedef enum {
    SIZE_B,
    SIZE_W,
    SIZE_L
} Size;

typedef struct {
    uint32_t d[NUM_DATA_REGISTERS];
} Registers;

void registers_init(Registers *regs);

/* Read register D<index>, masked to `size`. */
uint32_t registers_get(const Registers *regs, int index, Size size);

/* Write `value` into register D<index> at the given `size`. Only the bits
 * covered by `size` are replaced; the rest of the register is preserved. */
void registers_set(Registers *regs, int index, uint32_t value, Size size);

#endif /* MINIASM_REGISTERS_H */
