#ifndef CPU68K_H
#define CPU68K_H

#include <stdint.h>

// Virtual Motorola 68K CPU state
typedef struct {
    uint32_t D[8];   // Data registers D0-D7
    uint32_t A[8];   // Address registers A0-A7
    uint32_t SP;     // Stack pointer
    uint32_t PC;     // Program counter
    uint32_t SR;     // Status register (flags)
} CPU68K;

#endif // CPU68K_H

