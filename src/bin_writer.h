#ifndef BIN_WRITER_H
#define BIN_WRITER_H

#include "codegen.h"

// Flat binary output, like `objcopy -O binary`: just the assembled bytes,
// no header, no entry stub, no epilogue, no TRAP dispatcher.
void write_bin(const char *filename, const OutputBuffer *buf);

#endif // BIN_WRITER_H
