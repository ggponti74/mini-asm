#include "registers.h"

static uint32_t mask_for(Size size) {
    switch (size) {
        case SIZE_B: return 0xFFu;
        case SIZE_W: return 0xFFFFu;
        case SIZE_L: return 0xFFFFFFFFu;
    }
    return 0xFFFFFFFFu; /* unreachable, silences -Wreturn-type on some compilers */
}

void registers_init(Registers *regs) {
    for (int i = 0; i < NUM_DATA_REGISTERS; i++) {
        regs->d[i] = 0;
    }
}

uint32_t registers_get(const Registers *regs, int index, Size size) {
    return regs->d[index] & mask_for(size);
}

void registers_set(Registers *regs, int index, uint32_t value, Size size) {
    uint32_t mask = mask_for(size);
    value &= mask;
    if (size == SIZE_L) {
        regs->d[index] = value;
    } else {
        regs->d[index] = (regs->d[index] & ~mask) | value;
    }
}
