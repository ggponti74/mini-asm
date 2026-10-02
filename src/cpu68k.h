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

// 68K condition code register (low byte of SR): X N Z V C in bits 4..0.
#define SR_C 0x01u  // carry
#define SR_V 0x02u  // overflow
#define SR_Z 0x04u  // zero
#define SR_N 0x08u  // negative
#define SR_X 0x10u  // extend

#endif // CPU68K_H

